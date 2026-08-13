#include "hecompare/ckks_context.hpp"
#include "hecompare/ckks_distance.hpp"
#include "tests/tcga_data_loader.hpp"
#include "tests/test_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

using hecompare::CkksCiphertext;
using hecompare::CkksEnvironment;
using hecompare::CkksParameters;
using hecompare::RealVector;
using hecompare::test::TcgaDataset;

struct CandidateInfo {
    std::size_t index = 0;
    std::string sample_id;
    std::string label;
    double plaintext_distance = 0.0;
};

std::string data_csv_default()
{
    return "HECompare/testdata/tcga-pancan-raw/data.csv";
}

std::string labels_csv_default()
{
    return "HECompare/testdata/tcga-pancan-raw/labels.csv";
}

double plaintext_distance_he(
        const TcgaDataset& dataset,
        std::size_t query_index,
        std::size_t sample_index)
{
    const double* query = dataset.sample_data(query_index);
    const double* sample = dataset.sample_data(sample_index);

    double sum = 0.0;
    for (std::size_t i = 0; i < dataset.feature_count; ++i) {
        const double diff = sample[i] - query[i];
        sum += diff * diff;
    }
    return sum / static_cast<double>(dataset.feature_count);
}

std::pair<CandidateInfo, CandidateInfo> select_near_and_far(
        const TcgaDataset& dataset,
        std::size_t query_index)
{
    CandidateInfo near;
    CandidateInfo far;

    near.plaintext_distance = std::numeric_limits<double>::infinity();
    far.plaintext_distance = -1.0;

    for (std::size_t i = 0; i < dataset.sample_count; ++i) {
        if (i == query_index)
            continue;

        const double distance = plaintext_distance_he(dataset, query_index, i);
        if (distance < near.plaintext_distance) {
            near.index = i;
            near.sample_id = dataset.sample_ids[i];
            near.label = dataset.labels[i];
            near.plaintext_distance = distance;
        }
        if (distance > far.plaintext_distance) {
            far.index = i;
            far.sample_id = dataset.sample_ids[i];
            far.label = dataset.labels[i];
            far.plaintext_distance = distance;
        }
    }

    if (!std::isfinite(near.plaintext_distance) || far.plaintext_distance < 0.0)
        throw std::runtime_error("failed to select near/far TCGA candidates");

    return {near, far};
}

struct ChunkDistanceResult {
    CkksCiphertext ciphertext;
    std::vector<double> chunk_times_ms;
    double total_time_ms = 0.0;
    std::size_t chunk_count = 0;
    double decrypted_distance = 0.0;
    double absolute_error = 0.0;
    double relative_error = 0.0;
    std::size_t level = 0;
    double scale = 0.0;
};

ChunkDistanceResult evaluate_full_feature_distance(
        const CkksEnvironment& environment,
        const hecompare::EncryptedQueryChunks& encrypted_query,
        const RealVector& plaintext_train)
{
    ChunkDistanceResult result;
    result.chunk_count = encrypted_query.ciphertexts.size();

    const auto total_begin = std::chrono::steady_clock::now();
    result.ciphertext = hecompare::evaluate_chunked_mean_squared_l2_distance(
            environment,
            encrypted_query,
            plaintext_train);

    const auto total_end = std::chrono::steady_clock::now();
    result.total_time_ms =
            std::chrono::duration<double, std::milli>(total_end - total_begin).count();

    result.decrypted_distance =
            hecompare::test::decrypt_first_slot_for_test(environment, result.ciphertext);
    result.level = result.ciphertext->GetLevel();
    result.scale = result.ciphertext->GetScalingFactor();
    result.chunk_times_ms.clear();
    return result;
}

void print_candidate(const std::string& prefix, const CandidateInfo& candidate)
{
    std::cout << prefix << "_INDEX = " << candidate.index << '\n';
    std::cout << prefix << "_SAMPLE_ID = " << candidate.sample_id << '\n';
    std::cout << prefix << "_LABEL = " << candidate.label << '\n';
    std::cout << prefix << "_PLAINTEXT_DISTANCE = " << std::setprecision(17)
              << candidate.plaintext_distance << '\n';
}

void print_result(const std::string& prefix, const ChunkDistanceResult& result)
{
    std::cout << prefix << "_CKKS_DISTANCE = " << std::setprecision(17)
              << result.decrypted_distance << '\n';
    std::cout << prefix << "_ABS_ERROR = " << std::setprecision(17)
              << result.absolute_error << '\n';
    std::cout << prefix << "_REL_ERROR = " << std::setprecision(17)
              << result.relative_error << '\n';
    std::cout << prefix << "_LEVEL = " << result.level << '\n';
    std::cout << prefix << "_SCALE = " << std::setprecision(17)
              << result.scale << '\n';
    std::cout << prefix << "_TOTAL_TIME_MS = " << std::setprecision(10)
              << result.total_time_ms << '\n';
    for (std::size_t i = 0; i < result.chunk_times_ms.size(); ++i) {
        std::cout << prefix << "_CHUNK_" << i << "_TIME_MS = "
                  << std::setprecision(10)
                  << result.chunk_times_ms[i] << '\n';
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::string data_csv =
                argc >= 2 ? argv[1] : data_csv_default();
        const std::string labels_csv =
                argc >= 3 ? argv[2] : labels_csv_default();

        const auto dataset =
                hecompare::test::load_tcga_dataset(data_csv, labels_csv, 21.0);

        constexpr std::size_t query_index = 0;
        const auto [near_candidate, far_candidate] =
                select_near_and_far(dataset, query_index);

        CkksParameters params;
        params.slot_count = 4096;
        params.ring_dimension = 8192;
        auto environment = hecompare::make_ckks_environment(params);

        RealVector query(dataset.sample_data(query_index),
                dataset.sample_data(query_index) + static_cast<std::ptrdiff_t>(dataset.feature_count));
        RealVector near_train(dataset.sample_data(near_candidate.index),
                dataset.sample_data(near_candidate.index) + static_cast<std::ptrdiff_t>(dataset.feature_count));
        RealVector far_train(dataset.sample_data(far_candidate.index),
                dataset.sample_data(far_candidate.index) + static_cast<std::ptrdiff_t>(dataset.feature_count));

        const auto encrypted_query =
                hecompare::encrypt_query_chunks(environment, query);

        const auto near_result =
                evaluate_full_feature_distance(environment, encrypted_query, near_train);
        const auto far_result =
                evaluate_full_feature_distance(environment, encrypted_query, far_train);

        auto near_checked = near_result;
        auto far_checked = far_result;
        near_checked.absolute_error =
                std::abs(near_checked.decrypted_distance - near_candidate.plaintext_distance);
        far_checked.absolute_error =
                std::abs(far_checked.decrypted_distance - far_candidate.plaintext_distance);
        near_checked.relative_error =
                near_candidate.plaintext_distance == 0.0
                        ? near_checked.absolute_error
                        : near_checked.absolute_error / near_candidate.plaintext_distance;
        far_checked.relative_error =
                far_candidate.plaintext_distance == 0.0
                        ? far_checked.absolute_error
                        : far_checked.absolute_error / far_candidate.plaintext_distance;

        if (!std::isfinite(near_checked.decrypted_distance) ||
            !std::isfinite(far_checked.decrypted_distance)) {
            throw std::runtime_error("decrypted distance is not finite");
        }

        if (near_checked.decrypted_distance < -1e-8 ||
            far_checked.decrypted_distance < -1e-8) {
            throw std::runtime_error("decrypted distance is negative beyond tolerance");
        }

        if (!(near_candidate.plaintext_distance < far_candidate.plaintext_distance))
            throw std::runtime_error("plaintext near/far ordering is invalid");

        if (!(near_checked.decrypted_distance < far_checked.decrypted_distance))
            throw std::runtime_error("CKKS near/far ordering is invalid");

        if (near_result.chunk_count != 6 || far_result.chunk_count != 6)
            throw std::runtime_error("chunk count mismatch");

        std::cout << "QUERY_INDEX = " << query_index << '\n';
        std::cout << "QUERY_SAMPLE_ID = " << dataset.sample_ids[query_index] << '\n';
        std::cout << "QUERY_LABEL = " << dataset.labels[query_index] << '\n';
        std::cout << "QUERY_CHUNK_COUNT = " << encrypted_query.ciphertexts.size() << '\n';
        std::cout << "QUERY_ENCRYPTION_COUNT = " << encrypted_query.ciphertexts.size() << '\n';
        std::cout << "QUERY_REUSED_FOR_CANDIDATES = YES" << '\n';

        print_candidate("NEAR", near_candidate);
        print_candidate("FAR", far_candidate);
        print_result("NEAR", near_checked);
        print_result("FAR", far_checked);

        std::cout << "TCGA_FULL_FEATURE_DISTANCE = PASS" << '\n';
        std::cout << "FEATURE_COUNT_USED = " << dataset.feature_count << '\n';
        std::cout << "CHUNK_COUNT = 6" << '\n';
        std::cout << "PLAINTEXT_ORDER_CORRECT = YES" << '\n';
        std::cout << "CKKS_ORDER_CORRECT = YES" << '\n';
        std::cout << "READY_FOR_DISTANCE_COMPARE_SMOKE = YES" << '\n';

        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr << "TCGA_FULL_FEATURE_DISTANCE = FAIL" << '\n';
        std::cerr << "READY_FOR_DISTANCE_COMPARE_SMOKE = NO" << '\n';
        std::cerr << "openfhe-tcga-full-feature-distance-test failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
