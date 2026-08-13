#include "hecompare/ckks_context.hpp"
#include "hecompare/ckks_distance.hpp"
#include "hecompare/ckks_packed_compare.hpp"

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
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using hecompare::CkksCiphertext;
using hecompare::CkksEnvironment;
using hecompare::CkksParameters;
using hecompare::PackedBinaryLabelCandidate;
using hecompare::RealVector;

struct Config {
    uint32_t active_count = 16;
    uint32_t repetitions = 1;
    uint32_t label_bit_count = 10;
    double scale_sign = 262144.0;
    double compare_tolerance = 0.25;
    double bit_tolerance = 0.25;
    std::string csv_path;
    bool append_csv = false;
};

struct SlotCase {
    double lhs_score = 0.0;
    double rhs_score = 0.0;
    uint32_t lhs_label = 0;
    uint32_t rhs_label = 0;
    double expected_compare = 0.0;
    double expected_min_score = 0.0;
    double expected_max_score = 0.0;
    uint32_t expected_min_label = 0;
    uint32_t expected_max_label = 0;
};

struct DecodedLabels {
    std::vector<uint32_t> labels;
    double maximum_bit_error = 0.0;
    bool all_bits_decodable = true;
};

Config parse_args(int argc, char** argv)
{
    Config config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--active-count" &&
            i + 1 < argc) {
            config.active_count =
                    static_cast<uint32_t>(
                            std::stoul(argv[++i]));
        }
        else if (arg == "--repetitions" &&
                 i + 1 < argc) {
            config.repetitions =
                    static_cast<uint32_t>(
                            std::stoul(argv[++i]));
        }
        else if (arg == "--label-bit-count" &&
                 i + 1 < argc) {
            config.label_bit_count =
                    static_cast<uint32_t>(
                            std::stoul(argv[++i]));
        }
        else if (arg == "--scale-sign" &&
                 i + 1 < argc) {
            config.scale_sign =
                    std::stod(argv[++i]);
        }
        else if (arg == "--csv" &&
                 i + 1 < argc) {
            config.csv_path = argv[++i];
        }
        else if (arg == "--append-csv") {
            config.append_csv = true;
        }
        else {
            throw std::invalid_argument(
                    "unknown or incomplete argument: " +
                    arg);
        }
    }

    if (config.active_count == 0 ||
        config.active_count > 256) {
        throw std::invalid_argument(
                "active_count must be in [1, 256]");
    }

    if (config.repetitions == 0)
        throw std::invalid_argument(
                "repetitions must be positive");

    if (config.label_bit_count == 0 ||
        config.label_bit_count > 31) {
        throw std::invalid_argument(
                "label_bit_count must be in [1, 31]");
    }

    return config;
}

CkksParameters make_parameters(
        const Config& config)
{
    CkksParameters parameters;
    parameters.slot_count = 4096;
    parameters.ring_dimension = 8192;
    parameters.multiplicative_depth = 20;
    parameters.scale_sign = config.scale_sign;
    parameters.scaling_technique =
            lbcrypto::FLEXIBLEAUTOEXT;
    parameters.fhew_parameter_set =
            lbcrypto::STD128;
    parameters.switching_value_count =
            config.active_count;
    return parameters;
}

std::vector<SlotCase> build_slot_cases(
        uint32_t active_count,
        uint32_t label_bit_count)
{
    constexpr double gap =
            3.2036021124100776e-06;

    const uint32_t maximum_label =
            (uint32_t{1} << label_bit_count) - 1;

    // 10 bits cover 0..1023 and therefore all 801 TCGA sample IDs.
    const std::vector<uint32_t> lhs_pattern{
        0, 800, 15, 696,
        118, 454, 497, 367,
        1, 799, 97, 340,
        4, 700, 255, 512};

    const std::vector<uint32_t> rhs_pattern{
        800, 0, 696, 15,
        454, 118, 367, 497,
        799, 1, 340, 97,
        700, 4, 512, 255};

    std::vector<SlotCase> cases;
    cases.reserve(active_count);

    for (uint32_t i = 0;
         i < active_count;
         ++i) {
        SlotCase slot;

        const double low =
                0.00265 +
                static_cast<double>(i) *
                        0.00001;
        const double high = low + gap;

        slot.lhs_label =
                lhs_pattern[i %
                    lhs_pattern.size()] &
                maximum_label;
        slot.rhs_label =
                rhs_pattern[i %
                    rhs_pattern.size()] &
                maximum_label;

        if (i % 2 == 0) {
            slot.lhs_score = low;
            slot.rhs_score = high;
            slot.expected_compare = 1.0;
            slot.expected_min_score =
                    slot.lhs_score;
            slot.expected_max_score =
                    slot.rhs_score;
            slot.expected_min_label =
                    slot.lhs_label;
            slot.expected_max_label =
                    slot.rhs_label;
        }
        else {
            slot.lhs_score = high;
            slot.rhs_score = low;
            slot.expected_compare = 0.0;
            slot.expected_min_score =
                    slot.rhs_score;
            slot.expected_max_score =
                    slot.lhs_score;
            slot.expected_min_label =
                    slot.rhs_label;
            slot.expected_max_label =
                    slot.lhs_label;
        }

        cases.push_back(slot);
    }

    return cases;
}

RealVector collect_scores(
        const std::vector<SlotCase>& cases,
        bool lhs)
{
    RealVector values;
    values.reserve(cases.size());

    for (const auto& slot : cases)
        values.push_back(
                lhs
                    ? slot.lhs_score
                    : slot.rhs_score);

    return values;
}

std::vector<RealVector> collect_label_bit_planes(
        const std::vector<SlotCase>& cases,
        uint32_t bit_count,
        bool lhs)
{
    std::vector<RealVector> planes(
            bit_count,
            RealVector(cases.size(), 0.0));

    for (std::size_t slot = 0;
         slot < cases.size();
         ++slot) {
        const uint32_t label =
                lhs
                    ? cases[slot].lhs_label
                    : cases[slot].rhs_label;

        for (uint32_t bit = 0;
             bit < bit_count;
             ++bit) {
            planes[bit][slot] =
                    static_cast<double>(
                        (label >> bit) & 1U);
        }
    }

    return planes;
}

std::vector<CkksCiphertext> encrypt_planes(
        const CkksEnvironment& environment,
        const std::vector<RealVector>& planes)
{
    std::vector<CkksCiphertext> encrypted;
    encrypted.reserve(planes.size());

    for (const auto& plane : planes)
        encrypted.push_back(
                hecompare::encrypt_query(
                        environment,
                        plane));

    return encrypted;
}

std::vector<double> decrypt_slots(
        const CkksEnvironment& environment,
        const CkksCiphertext& ciphertext,
        uint32_t count)
{
    lbcrypto::Plaintext plaintext;
    environment.context->Decrypt(
            environment.key_pair.secretKey,
            ciphertext,
            &plaintext);

    if (!plaintext)
        throw std::runtime_error(
                "CKKS decryption returned null plaintext");

    plaintext->SetLength(count);
    const auto packed =
            plaintext->GetRealPackedValue();

    if (packed.size() < count)
        throw std::runtime_error(
                "decrypted vector is too short");

    return std::vector<double>(
            packed.begin(),
            packed.begin() + count);
}

DecodedLabels decrypt_and_decode_labels(
        const CkksEnvironment& environment,
        const std::vector<CkksCiphertext>& planes,
        uint32_t active_count,
        double bit_tolerance,
        const char* output_name)
{
    DecodedLabels decoded;
    decoded.labels.assign(active_count, 0);

    for (std::size_t bit = 0;
         bit < planes.size();
         ++bit) {
        const auto values =
                decrypt_slots(
                        environment,
                        planes[bit],
                        active_count);

        for (uint32_t slot = 0;
             slot < active_count;
             ++slot) {
            const double nearest =
                    std::round(values[slot]);
            const double error =
                    std::abs(
                        values[slot] -
                        nearest);

            decoded.maximum_bit_error =
                    std::max(
                        decoded.maximum_bit_error,
                        error);

            const bool bit_decodable =
                    std::isfinite(values[slot]) &&
                    (nearest == 0.0 ||
                     nearest == 1.0) &&
                    error <= bit_tolerance;

            decoded.all_bits_decodable =
                    decoded.all_bits_decodable &&
                    bit_decodable;

            if (bit_decodable &&
                nearest == 1.0) {
                decoded.labels[slot] |=
                        uint32_t{1} << bit;
            }

            std::cout
                << std::setprecision(17)
                << "LABEL_BIT"
                << " output=" << output_name
                << " slot=" << slot
                << " bit=" << bit
                << " actual=" << values[slot]
                << " decoded=" << nearest
                << " bit_error=" << error
                << " pass="
                << (bit_decodable
                        ? "YES"
                        : "NO")
                << '\n';
        }
    }

    return decoded;
}

bool validate_compare(
        const std::vector<double>& actual,
        const std::vector<SlotCase>& cases,
        double tolerance)
{
    bool pass = true;

    for (std::size_t i = 0;
         i < cases.size();
         ++i) {
        const double expected =
                cases[i].expected_compare;
        const bool slot_pass =
                std::isfinite(actual[i]) &&
                std::abs(
                    actual[i] -
                    expected) <= tolerance;

        pass = pass && slot_pass;

        std::cout
            << std::setprecision(17)
            << "COMPARE_SLOT"
            << " index=" << i
            << " expected=" << expected
            << " actual=" << actual[i]
            << " pass="
            << (slot_pass ? "YES" : "NO")
            << '\n';
    }

    return pass;
}

bool validate_scores(
        const char* name,
        const std::vector<double>& actual,
        const std::vector<SlotCase>& cases,
        bool minimum,
        double tolerance)
{
    bool pass = true;

    for (std::size_t i = 0;
         i < cases.size();
         ++i) {
        const double expected =
                minimum
                    ? cases[i].expected_min_score
                    : cases[i].expected_max_score;
        const bool slot_pass =
                std::isfinite(actual[i]) &&
                std::abs(actual[i] - expected) <=
                        tolerance;
        pass = pass && slot_pass;

        std::cout
            << std::setprecision(17)
            << "SCORE_SLOT"
            << " output=" << name
            << " index=" << i
            << " expected=" << expected
            << " actual=" << actual[i]
            << " pass="
            << (slot_pass ? "YES" : "NO")
            << '\n';
    }

    return pass;
}

bool validate_labels(
        const char* name,
        const DecodedLabels& actual,
        const std::vector<SlotCase>& cases,
        bool minimum)
{
    bool pass = actual.all_bits_decodable;

    for (std::size_t i = 0;
         i < cases.size();
         ++i) {
        const uint32_t expected =
                minimum
                    ? cases[i].expected_min_label
                    : cases[i].expected_max_label;
        const bool slot_pass =
                actual.labels[i] == expected;
        pass = pass && slot_pass;

        std::cout
            << "LABEL_RESULT"
            << " output=" << name
            << " index=" << i
            << " expected=" << expected
            << " actual=" << actual.labels[i]
            << " pass="
            << (slot_pass ? "YES" : "NO")
            << '\n';
    }

    return pass;
}

double max_score_error(
        const std::vector<double>& values,
        const std::vector<SlotCase>& cases,
        bool minimum)
{
    double maximum = 0.0;

    for (std::size_t i = 0;
         i < cases.size();
         ++i) {
        const double expected =
                minimum
                    ? cases[i].expected_min_score
                    : cases[i].expected_max_score;
        maximum =
                std::max(
                    maximum,
                    std::abs(
                        values[i] -
                        expected));
    }

    return maximum;
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
        double setup_ms,
        double encryption_ms,
        double compare_ms,
        double swap_ms,
        double decrypt_ms,
        double compare_max_error,
        double min_score_max_error,
        double max_score_max_error,
        double min_label_bit_max_error,
        double max_label_bit_max_error,
        bool compare_pass,
        bool score_pass,
        bool label_pass)
{
    if (config.csv_path.empty())
        return;

    const bool has_content =
            file_nonempty(config.csv_path);

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
                "failed to open CSV");

    if (!has_content || !config.append_csv) {
        output
            << "active_count,label_bit_count,"
            << "scale_sign,p_lwe,"
            << "setup_ms,encryption_ms,"
            << "compare_ms,swap_ms,decrypt_ms,"
            << "comparisons_per_second,"
            << "compare_max_error,"
            << "min_score_max_error,"
            << "max_score_max_error,"
            << "min_label_bit_max_error,"
            << "max_label_bit_max_error,"
            << "compare_pass,score_pass,"
            << "label_pass,overall_pass\n";
    }

    const double throughput =
            compare_ms > 0.0
                ? config.active_count *
                    1000.0 / compare_ms
                : std::numeric_limits<double>::
                    infinity();

    const bool overall =
            compare_pass &&
            score_pass &&
            label_pass;

    output
        << std::setprecision(17)
        << config.active_count << ','
        << config.label_bit_count << ','
        << config.scale_sign << ','
        << environment.p_lwe << ','
        << setup_ms << ','
        << encryption_ms << ','
        << compare_ms << ','
        << swap_ms << ','
        << decrypt_ms << ','
        << throughput << ','
        << compare_max_error << ','
        << min_score_max_error << ','
        << max_score_max_error << ','
        << min_label_bit_max_error << ','
        << max_label_bit_max_error << ','
        << (compare_pass ? 1 : 0) << ','
        << (score_pass ? 1 : 0) << ','
        << (label_pass ? 1 : 0) << ','
        << (overall ? 1 : 0)
        << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Config config =
                parse_args(argc, argv);
        const auto cases =
                build_slot_cases(
                        config.active_count,
                        config.label_bit_count);

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
            << "PACKED_BINARY_CONTEXT"
            << " active_count="
            << config.active_count
            << " label_bit_count="
            << config.label_bit_count
            << " switching_value_count="
            << environment.switching_value_count
            << " scale_sign="
            << environment.scale_sign
            << " p_lwe="
            << environment.p_lwe
            << " setup_ms="
            << setup_ms
            << '\n';

        const auto lhs_scores =
                collect_scores(cases, true);
        const auto rhs_scores =
                collect_scores(cases, false);
        const auto lhs_planes =
                collect_label_bit_planes(
                        cases,
                        config.label_bit_count,
                        true);
        const auto rhs_planes =
                collect_label_bit_planes(
                        cases,
                        config.label_bit_count,
                        false);

        const auto encryption_begin =
                Clock::now();

        PackedBinaryLabelCandidate lhs{
            hecompare::encrypt_query(
                environment,
                lhs_scores),
            encrypt_planes(
                environment,
                lhs_planes),
        };

        PackedBinaryLabelCandidate rhs{
            hecompare::encrypt_query(
                environment,
                rhs_scores),
            encrypt_planes(
                environment,
                rhs_planes),
        };

        const auto encryption_end =
                Clock::now();
        const double encryption_ms =
                std::chrono::duration<
                    double,
                    std::milli>(
                    encryption_end -
                    encryption_begin).count();

        const auto compare_begin =
                Clock::now();
        auto compare =
                hecompare::encrypted_less_than_packed(
                        environment,
                        lhs.score,
                        rhs.score,
                        config.active_count);
        const auto compare_end =
                Clock::now();
        const double compare_ms =
                std::chrono::duration<
                    double,
                    std::milli>(
                    compare_end -
                    compare_begin).count();

        const auto compare_values =
                decrypt_slots(
                        environment,
                        compare,
                        config.active_count);
        const bool compare_pass =
                validate_compare(
                        compare_values,
                        cases,
                        config.compare_tolerance);

        double compare_max_error = 0.0;
        for (std::size_t i = 0;
             i < cases.size();
             ++i) {
            compare_max_error =
                    std::max(
                        compare_max_error,
                        std::abs(
                            compare_values[i] -
                            cases[i].
                                expected_compare));
        }

        const auto swap_begin =
                Clock::now();
        const auto swapped =
                hecompare::
                    packed_compare_and_swap_binary_labels_ascending(
                        environment,
                        lhs,
                        rhs,
                        config.active_count);
        const auto swap_end =
                Clock::now();
        const double swap_ms =
                std::chrono::duration<
                    double,
                    std::milli>(
                    swap_end -
                    swap_begin).count();

        const auto decrypt_begin =
                Clock::now();

        const auto minimum_scores =
                decrypt_slots(
                        environment,
                        swapped.first.score,
                        config.active_count);
        const auto maximum_scores =
                decrypt_slots(
                        environment,
                        swapped.second.score,
                        config.active_count);

        const auto minimum_labels =
                decrypt_and_decode_labels(
                        environment,
                        swapped.first.label_bits,
                        config.active_count,
                        config.bit_tolerance,
                        "minimum");
        const auto maximum_labels =
                decrypt_and_decode_labels(
                        environment,
                        swapped.second.label_bits,
                        config.active_count,
                        config.bit_tolerance,
                        "maximum");

        const auto decrypt_end =
                Clock::now();
        const double decrypt_ms =
                std::chrono::duration<
                    double,
                    std::milli>(
                    decrypt_end -
                    decrypt_begin).count();

        const bool min_score_pass =
                validate_scores(
                        "minimum",
                        minimum_scores,
                        cases,
                        true,
                        1e-3);
        const bool max_score_pass =
                validate_scores(
                        "maximum",
                        maximum_scores,
                        cases,
                        false,
                        1e-3);
        const bool score_pass =
                min_score_pass &&
                max_score_pass;

        const bool min_label_pass =
                validate_labels(
                        "minimum",
                        minimum_labels,
                        cases,
                        true);
        const bool max_label_pass =
                validate_labels(
                        "maximum",
                        maximum_labels,
                        cases,
                        false);
        const bool label_pass =
                min_label_pass &&
                max_label_pass;

        const double min_score_max_error =
                max_score_error(
                        minimum_scores,
                        cases,
                        true);
        const double max_score_max_error =
                max_score_error(
                        maximum_scores,
                        cases,
                        false);

        const bool overall_pass =
                compare_pass &&
                score_pass &&
                label_pass;

        std::cout
            << std::setprecision(17)
            << "PACKED_BINARY_COMPARE_RESULT"
            << " compare_ms=" << compare_ms
            << " compare_max_error="
            << compare_max_error
            << " compare_pass="
            << (compare_pass ? "YES" : "NO")
            << '\n';

        std::cout
            << std::setprecision(17)
            << "PACKED_BINARY_SWAP_RESULT"
            << " swap_ms=" << swap_ms
            << " min_score_max_error="
            << min_score_max_error
            << " max_score_max_error="
            << max_score_max_error
            << " min_label_bit_max_error="
            << minimum_labels.maximum_bit_error
            << " max_label_bit_max_error="
            << maximum_labels.maximum_bit_error
            << " score_pass="
            << (score_pass ? "YES" : "NO")
            << " label_pass="
            << (label_pass ? "YES" : "NO")
            << '\n';

        std::cout
            << "PACKED_BINARY_COMPARE_AND_SWAP = "
            << (overall_pass ? "PASS" : "FAIL")
            << '\n';

        append_csv(
                config,
                environment,
                setup_ms,
                encryption_ms,
                compare_ms,
                swap_ms,
                decrypt_ms,
                compare_max_error,
                min_score_max_error,
                max_score_max_error,
                minimum_labels.maximum_bit_error,
                maximum_labels.maximum_bit_error,
                compare_pass,
                score_pass,
                label_pass);

        return overall_pass
                ? EXIT_SUCCESS
                : EXIT_FAILURE;
    }
    catch (const std::exception& error) {
        std::cerr
            << "PACKED_BINARY_COMPARE_AND_SWAP = FAIL\n"
            << error.what()
            << '\n';
        return EXIT_FAILURE;
    }
}
