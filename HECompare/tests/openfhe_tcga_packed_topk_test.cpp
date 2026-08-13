#include "hecompare/ckks_context.hpp"
#include "hecompare/ckks_distance.hpp"
#include "hecompare/ckks_packed_topk.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using hecompare::CkksCiphertext;
using hecompare::CkksEnvironment;
using hecompare::CkksParameters;
using hecompare::PackedBinaryLabelCandidate;
using hecompare::RealVector;

struct Config {
    uint32_t candidate_count = 8;
    uint32_t k = 3;
    uint32_t active_count = 1;
    uint32_t label_bit_count = 3;
    uint32_t slot_count = 16;
    uint32_t multiplicative_depth = 12;
    double scale_sign = 262144.0;
    double score_tolerance = 1e-3;
    double bit_tolerance = 0.25;
    std::string csv_path;
    bool append_csv = false;
};

struct PlainCandidate {
    double score = 0.0;
    uint32_t label = 0;
};

struct DecodedWire {
    std::vector<double> scores;
    std::vector<uint32_t> labels;
    double maximum_bit_error = 0.0;
    bool all_bits_decodable = true;
};

bool is_power_of_two(uint32_t value)
{
    return value != 0 &&
           (value & (value - 1)) == 0;
}

Config parse_args(int argc, char** argv)
{
    Config config;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];

        if (argument == "--candidate-count" &&
            i + 1 < argc) {
            config.candidate_count =
                    static_cast<uint32_t>(
                            std::stoul(argv[++i]));
        }
        else if (argument == "--k" &&
                 i + 1 < argc) {
            config.k =
                    static_cast<uint32_t>(
                            std::stoul(argv[++i]));
        }
        else if (argument == "--active-count" &&
                 i + 1 < argc) {
            config.active_count =
                    static_cast<uint32_t>(
                            std::stoul(argv[++i]));
        }
        else if (argument == "--label-bit-count" &&
                 i + 1 < argc) {
            config.label_bit_count =
                    static_cast<uint32_t>(
                            std::stoul(argv[++i]));
        }
        else if (argument == "--slot-count" &&
                 i + 1 < argc) {
            config.slot_count =
                    static_cast<uint32_t>(
                            std::stoul(argv[++i]));
        }
        else if (argument == "--multiplicative-depth" &&
                 i + 1 < argc) {
            config.multiplicative_depth =
                    static_cast<uint32_t>(
                            std::stoul(argv[++i]));
        }
        else if (argument == "--scale-sign" &&
                 i + 1 < argc) {
            config.scale_sign =
                    std::stod(argv[++i]);
        }
        else if (argument == "--csv" &&
                 i + 1 < argc) {
            config.csv_path = argv[++i];
        }
        else if (argument == "--append-csv") {
            config.append_csv = true;
        }
        else {
            throw std::invalid_argument(
                    "unknown or incomplete argument: " +
                    argument);
        }
    }

    if (!is_power_of_two(config.candidate_count) ||
        config.candidate_count < 2 ||
        config.candidate_count > 8) {
        throw std::invalid_argument(
                "candidate_count must be 2, 4, or 8");
    }

    if (config.k == 0 ||
        config.k > config.candidate_count) {
        throw std::invalid_argument(
                "k must be in [1, candidate_count]");
    }

    if (config.active_count == 0 ||
        config.active_count > 16) {
        throw std::invalid_argument(
                "active_count must be in [1, 16]");
    }

    if (config.label_bit_count == 0 ||
        config.label_bit_count > 16) {
        throw std::invalid_argument(
                "label_bit_count must be in [1, 16]");
    }

    if (config.slot_count < config.active_count ||
        config.slot_count > 4096 ||
        !is_power_of_two(config.slot_count)) {
        throw std::invalid_argument(
                "slot_count must be a power of two in "
                "[active_count, 4096]");
    }

    if (config.multiplicative_depth < 8 ||
        config.multiplicative_depth > 20) {
        throw std::invalid_argument(
                "multiplicative_depth must be in [8, 20]");
    }

    const uint32_t label_capacity =
            uint32_t{1} <<
            config.label_bit_count;

    if (config.candidate_count >
        label_capacity) {
        throw std::invalid_argument(
                "label_bit_count cannot encode every wire label");
    }

    return config;
}

CkksParameters make_parameters(
        const Config& config)
{
    CkksParameters parameters;
    parameters.slot_count = config.slot_count;
    parameters.ring_dimension = 8192;
    parameters.multiplicative_depth =
            config.multiplicative_depth;
    parameters.scale_sign = config.scale_sign;
    parameters.scaling_technique =
            lbcrypto::FLEXIBLEAUTOEXT;
    parameters.fhew_parameter_set =
            lbcrypto::STD128;
    parameters.switching_value_count =
            config.active_count;
    return parameters;
}

std::vector<std::vector<PlainCandidate>>
build_plain_queries(const Config& config)
{
    constexpr double base_score =
            0.00265;
    constexpr double p05_gap =
            3.2036021124100776e-06;

    std::vector<std::vector<PlainCandidate>>
            queries(
                config.active_count,
                std::vector<PlainCandidate>(
                        config.candidate_count));

    for (uint32_t query = 0;
         query < config.active_count;
         ++query) {
        // Multipliers are odd for candidate_count 2/4/8,
        // so every query receives a permutation of ranks.
        const uint32_t multiplier =
                (query % 2 == 0)
                    ? 3
                    : 5;
        const uint32_t shift =
                (query * 3 + 1) %
                config.candidate_count;

        for (uint32_t wire = 0;
             wire < config.candidate_count;
             ++wire) {
            const uint32_t rank =
                    (wire * multiplier + shift) %
                    config.candidate_count;

            queries[query][wire] =
                    PlainCandidate{
                        base_score +
                            static_cast<double>(rank) *
                            p05_gap,
                        wire};
        }
    }

    return queries;
}

std::vector<std::vector<PlainCandidate>>
build_expected_sorted(
        const std::vector<
            std::vector<PlainCandidate>>& queries)
{
    auto expected = queries;

    for (auto& query : expected) {
        std::sort(
            query.begin(),
            query.end(),
            [](const PlainCandidate& lhs,
               const PlainCandidate& rhs) {
                if (lhs.score != rhs.score)
                    return lhs.score < rhs.score;
                return lhs.label < rhs.label;
            });
    }

    return expected;
}

std::vector<PackedBinaryLabelCandidate>
encrypt_candidate_wires(
        const CkksEnvironment& environment,
        const Config& config,
        const std::vector<
            std::vector<PlainCandidate>>& queries)
{
    std::vector<PackedBinaryLabelCandidate>
            encrypted;
    encrypted.reserve(config.candidate_count);

    for (uint32_t wire = 0;
         wire < config.candidate_count;
         ++wire) {
        RealVector scores(
                config.active_count,
                0.0);
        std::vector<RealVector> bit_planes(
                config.label_bit_count,
                RealVector(
                    config.active_count,
                    0.0));

        for (uint32_t query = 0;
             query < config.active_count;
             ++query) {
            const auto& candidate =
                    queries[query][wire];
            scores[query] =
                    candidate.score;

            for (uint32_t bit = 0;
                 bit < config.label_bit_count;
                 ++bit) {
                bit_planes[bit][query] =
                        static_cast<double>(
                            (candidate.label >> bit) &
                            1U);
            }
        }

        PackedBinaryLabelCandidate candidate;
        candidate.score =
                hecompare::encrypt_query(
                        environment,
                        scores);
        candidate.label_bits.reserve(
                config.label_bit_count);

        for (const auto& plane :
             bit_planes) {
            candidate.label_bits.push_back(
                hecompare::encrypt_query(
                        environment,
                        plane));
        }

        encrypted.push_back(
                std::move(candidate));
    }

    return encrypted;
}

std::vector<double> decrypt_slots(
        const CkksEnvironment& environment,
        const CkksCiphertext& ciphertext,
        uint32_t active_count)
{
    lbcrypto::Plaintext plaintext;
    environment.context->Decrypt(
            environment.key_pair.secretKey,
            ciphertext,
            &plaintext);

    if (!plaintext)
        throw std::runtime_error(
                "CKKS decryption returned null plaintext");

    plaintext->SetLength(active_count);
    const auto values =
            plaintext->GetRealPackedValue();

    if (values.size() < active_count)
        throw std::runtime_error(
                "decrypted vector is too short");

    return std::vector<double>(
            values.begin(),
            values.begin() +
                active_count);
}

DecodedWire decrypt_wire(
        const CkksEnvironment& environment,
        const PackedBinaryLabelCandidate& wire,
        const Config& config)
{
    DecodedWire decoded;
    decoded.scores =
            decrypt_slots(
                environment,
                wire.score,
                config.active_count);
    decoded.labels.assign(
            config.active_count,
            0);

    for (uint32_t bit = 0;
         bit < config.label_bit_count;
         ++bit) {
        const auto values =
                decrypt_slots(
                    environment,
                    wire.label_bits[bit],
                    config.active_count);

        for (uint32_t query = 0;
             query < config.active_count;
             ++query) {
            const double nearest =
                    std::round(values[query]);
            const double error =
                    std::abs(
                        values[query] -
                        nearest);

            decoded.maximum_bit_error =
                    std::max(
                        decoded.maximum_bit_error,
                        error);

            const bool bit_ok =
                    std::isfinite(values[query]) &&
                    (nearest == 0.0 ||
                     nearest == 1.0) &&
                    error <=
                        config.bit_tolerance;

            decoded.all_bits_decodable =
                    decoded.all_bits_decodable &&
                    bit_ok;

            if (bit_ok && nearest == 1.0) {
                decoded.labels[query] |=
                        uint32_t{1} << bit;
            }
        }
    }

    return decoded;
}

bool file_nonempty(
        const std::string& path)
{
    if (path.empty())
        return false;

    std::error_code error;
    const auto size =
            std::filesystem::file_size(
                    path,
                    error);
    return !error && size > 0;
}

void append_csv(
        const Config& config,
        const CkksEnvironment& environment,
        uint32_t comparator_count,
        uint32_t network_depth,
        double setup_ms,
        double encryption_ms,
        double topk_ms,
        double decrypt_ms,
        double maximum_score_error,
        double maximum_bit_error,
        bool topk_pass)
{
    if (config.csv_path.empty())
        return;

    const bool has_content =
            file_nonempty(
                    config.csv_path);

    std::ios::openmode mode =
            std::ios::out;
    mode |= config.append_csv
                ? std::ios::app
                : std::ios::trunc;

    std::ofstream output(
            config.csv_path,
            mode);
    if (!output)
        throw std::runtime_error(
                "failed to open Top-k CSV");

    if (!has_content || !config.append_csv) {
        output
            << "candidate_count,k,active_count,"
            << "label_bit_count,slot_count,"
            << "multiplicative_depth,scale_sign,p_lwe,"
            << "comparator_count,network_depth,"
            << "setup_ms,encryption_ms,topk_ms,"
            << "decrypt_ms,batched_queries_per_second,"
            << "comparator_lanes_per_second,"
            << "maximum_score_error,"
            << "maximum_bit_error,topk_pass\n";
    }

    const double batched_queries_per_second =
            topk_ms > 0.0
                ? config.active_count *
                    1000.0 / topk_ms
                : std::numeric_limits<double>::
                    infinity();

    const double comparator_lanes_per_second =
            topk_ms > 0.0
                ? static_cast<double>(
                    config.active_count) *
                    comparator_count *
                    1000.0 / topk_ms
                : std::numeric_limits<double>::
                    infinity();

    output
        << std::setprecision(17)
        << config.candidate_count << ','
        << config.k << ','
        << config.active_count << ','
        << config.label_bit_count << ','
        << config.slot_count << ','
        << config.multiplicative_depth << ','
        << config.scale_sign << ','
        << environment.p_lwe << ','
        << comparator_count << ','
        << network_depth << ','
        << setup_ms << ','
        << encryption_ms << ','
        << topk_ms << ','
        << decrypt_ms << ','
        << batched_queries_per_second << ','
        << comparator_lanes_per_second << ','
        << maximum_score_error << ','
        << maximum_bit_error << ','
        << (topk_pass ? 1 : 0)
        << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Config config =
                parse_args(argc, argv);
        const auto queries =
                build_plain_queries(config);
        const auto expected =
                build_expected_sorted(
                        queries);

        const auto setup_begin =
                Clock::now();
        auto environment =
                hecompare::make_ckks_environment(
                        make_parameters(config));
        const auto setup_end =
                Clock::now();

        const double setup_ms =
                std::chrono::duration<
                    double,
                    std::milli>(
                    setup_end -
                    setup_begin).count();

        std::cout
            << std::setprecision(17)
            << "PACKED_TOPK_CONTEXT"
            << " candidate_count="
            << config.candidate_count
            << " k=" << config.k
            << " active_count="
            << config.active_count
            << " label_bit_count="
            << config.label_bit_count
            << " slot_count="
            << config.slot_count
            << " multiplicative_depth="
            << config.multiplicative_depth
            << " scale_sign="
            << environment.scale_sign
            << " p_lwe="
            << environment.p_lwe
            << " setup_ms="
            << setup_ms
            << '\n';

        const auto encryption_begin =
                Clock::now();
        auto encrypted_candidates =
                encrypt_candidate_wires(
                        environment,
                        config,
                        queries);
        const auto encryption_end =
                Clock::now();

        const double encryption_ms =
                std::chrono::duration<
                    double,
                    std::milli>(
                    encryption_end -
                    encryption_begin).count();

        const auto topk_begin =
                Clock::now();
        auto sorted =
                hecompare::
                    packed_bitonic_sort_ascending(
                        environment,
                        std::move(
                            encrypted_candidates),
                        config.active_count);
        const auto topk_end =
                Clock::now();

        const double topk_ms =
                std::chrono::duration<
                    double,
                    std::milli>(
                    topk_end -
                    topk_begin).count();

        const auto decrypt_begin =
                Clock::now();

        std::vector<DecodedWire> decoded_topk;
        decoded_topk.reserve(config.k);

        for (uint32_t rank = 0;
             rank < config.k;
             ++rank) {
            decoded_topk.push_back(
                decrypt_wire(
                    environment,
                    sorted.sorted_candidates[rank],
                    config));
        }

        const auto decrypt_end =
                Clock::now();
        const double decrypt_ms =
                std::chrono::duration<
                    double,
                    std::milli>(
                    decrypt_end -
                    decrypt_begin).count();

        bool topk_pass = true;
        double maximum_score_error = 0.0;
        double maximum_bit_error = 0.0;

        for (uint32_t rank = 0;
             rank < config.k;
             ++rank) {
            maximum_bit_error =
                    std::max(
                        maximum_bit_error,
                        decoded_topk[rank].
                            maximum_bit_error);
            topk_pass =
                    topk_pass &&
                    decoded_topk[rank].
                        all_bits_decodable;

            for (uint32_t query = 0;
                 query < config.active_count;
                 ++query) {
                const auto& expected_candidate =
                        expected[query][rank];
                const double actual_score =
                        decoded_topk[rank].
                            scores[query];
                const uint32_t actual_label =
                        decoded_topk[rank].
                            labels[query];

                const double score_error =
                        std::abs(
                            actual_score -
                            expected_candidate.score);
                maximum_score_error =
                        std::max(
                            maximum_score_error,
                            score_error);

                const bool slot_pass =
                        std::isfinite(
                            actual_score) &&
                        score_error <=
                            config.score_tolerance &&
                        actual_label ==
                            expected_candidate.label;

                topk_pass =
                        topk_pass &&
                        slot_pass;

                std::cout
                    << std::setprecision(17)
                    << "TOPK_RESULT"
                    << " query=" << query
                    << " rank=" << rank
                    << " expected_score="
                    << expected_candidate.score
                    << " actual_score="
                    << actual_score
                    << " score_error="
                    << score_error
                    << " expected_label="
                    << expected_candidate.label
                    << " actual_label="
                    << actual_label
                    << " pass="
                    << (slot_pass
                            ? "YES"
                            : "NO")
                    << '\n';
            }
        }

        const double query_throughput =
                topk_ms > 0.0
                    ? config.active_count *
                        1000.0 / topk_ms
                    : std::numeric_limits<double>::
                        infinity();

        const double comparator_lane_throughput =
                topk_ms > 0.0
                    ? static_cast<double>(
                        config.active_count) *
                        sorted.comparator_count *
                        1000.0 / topk_ms
                    : std::numeric_limits<double>::
                        infinity();

        std::cout
            << std::setprecision(17)
            << "PACKED_TOPK_RESULT"
            << " candidate_count="
            << config.candidate_count
            << " k=" << config.k
            << " active_count="
            << config.active_count
            << " comparator_count="
            << sorted.comparator_count
            << " network_depth="
            << sorted.network_depth
            << " topk_ms="
            << topk_ms
            << " batched_queries_per_second="
            << query_throughput
            << " comparator_lanes_per_second="
            << comparator_lane_throughput
            << " maximum_score_error="
            << maximum_score_error
            << " maximum_bit_error="
            << maximum_bit_error
            << " topk_pass="
            << (topk_pass ? "YES" : "NO")
            << '\n';

        append_csv(
                config,
                environment,
                sorted.comparator_count,
                sorted.network_depth,
                setup_ms,
                encryption_ms,
                topk_ms,
                decrypt_ms,
                maximum_score_error,
                maximum_bit_error,
                topk_pass);

        std::cout
            << "PACKED_BITONIC_TOPK = "
            << (topk_pass ? "PASS" : "FAIL")
            << '\n';

        return topk_pass
                ? EXIT_SUCCESS
                : EXIT_FAILURE;
    }
    catch (const std::exception& error) {
        std::cerr
            << "PACKED_BITONIC_TOPK = FAIL\n"
            << error.what()
            << '\n';
        return EXIT_FAILURE;
    }
}
