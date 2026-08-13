#include "hecompare/ckks_compare.hpp"
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
    double decrypted_distance = 0.0;
    std::size_t level = 0;
    double scale = 0.0;
    std::size_t tower_count = 0;
};

ChunkDistanceResult finalize_distance_metadata(
        const CkksEnvironment& environment,
        const CkksCiphertext& ciphertext)
{
    ChunkDistanceResult result;
    result.ciphertext = ciphertext;
    result.decrypted_distance =
            hecompare::test::decrypt_first_slot_for_test(environment, ciphertext);
    result.level = ciphertext->GetLevel();
    result.scale = ciphertext->GetScalingFactor();
    if (!ciphertext->GetElements().empty())
        result.tower_count = ciphertext->GetElements()[0].GetNumOfElements();
    return result;
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
        params.multiplicative_depth = 20;
        params.scale_sign = 64.0;
        params.scaling_technique = lbcrypto::FLEXIBLEAUTOEXT;
        params.fhew_parameter_set = lbcrypto::STD128;
        params.switching_value_count = 1;
        auto environment = hecompare::make_ckks_environment(params);

        RealVector query(dataset.sample_data(query_index),
                dataset.sample_data(query_index) + static_cast<std::ptrdiff_t>(dataset.feature_count));
        RealVector near_train(dataset.sample_data(near_candidate.index),
                dataset.sample_data(near_candidate.index) + static_cast<std::ptrdiff_t>(dataset.feature_count));
        RealVector far_train(dataset.sample_data(far_candidate.index),
                dataset.sample_data(far_candidate.index) + static_cast<std::ptrdiff_t>(dataset.feature_count));

        const auto encrypted_query =
                hecompare::encrypt_query_chunks(environment, query);
        const auto near_result = finalize_distance_metadata(
                environment,
                hecompare::evaluate_chunked_mean_squared_l2_distance(
                        environment, encrypted_query, near_train));
        const auto far_result = finalize_distance_metadata(
                environment,
                hecompare::evaluate_chunked_mean_squared_l2_distance(
                        environment, encrypted_query, far_train));

        const bool same_context =
                near_result.ciphertext->GetCryptoContext().get() == environment.context.get() &&
                far_result.ciphertext->GetCryptoContext().get() == environment.context.get();

        const auto compare_begin = std::chrono::steady_clock::now();

        double forward_actual = std::numeric_limits<double>::quiet_NaN();
        double reverse_actual = std::numeric_limits<double>::quiet_NaN();

        try {
            auto forward = hecompare::encrypted_less_than(
                    environment,
                    near_result.ciphertext,
                    far_result.ciphertext);
            auto reverse = hecompare::encrypted_less_than(
                    environment,
                    far_result.ciphertext,
                    near_result.ciphertext);

            forward_actual =
                    hecompare::test::decrypt_first_slot_for_test(environment, forward);
            reverse_actual =
                    hecompare::test::decrypt_first_slot_for_test(environment, reverse);
        }
        catch (const std::exception& error) {
            const auto compare_end = std::chrono::steady_clock::now();
            const double compare_time_ms =
                    std::chrono::duration<double, std::milli>(
                            compare_end - compare_begin).count();

            std::cout << "DISTANCE_TO_COMPARE = FAIL" << '\n';
            std::cout << "FORWARD_COMPARE_EXPECTED = 1" << '\n';
            std::cout << "FORWARD_COMPARE_ACTUAL = compare_failed" << '\n';
            std::cout << "REVERSE_COMPARE_EXPECTED = 0" << '\n';
            std::cout << "REVERSE_COMPARE_ACTUAL = compare_failed" << '\n';
            std::cout << "SAME_CRYPTO_CONTEXT = " << (same_context ? "YES" : "NO") << '\n';
            std::cout << "DISTANCE_LEVEL = " << near_result.level << '\n';
            std::cout << "DISTANCE_SCALE = " << std::setprecision(17)
                      << near_result.scale << '\n';
            std::cout << "DISTANCE_TOWER_COUNT = " << near_result.tower_count << '\n';
            std::cout << "COMPARE_TIME_MS = " << std::setprecision(10)
                      << compare_time_ms << '\n';
            std::cout << "READY_FOR_RESOLUTION_TEST = NO" << '\n';
            std::cout << "COMPARE_FAILURE_REASON = " << error.what() << '\n';
            return EXIT_FAILURE;
        }

        const auto compare_end = std::chrono::steady_clock::now();
        const double compare_time_ms =
                std::chrono::duration<double, std::milli>(
                        compare_end - compare_begin).count();

        std::cout << "FORWARD_COMPARE_EXPECTED = 1" << '\n';
        std::cout << "FORWARD_COMPARE_ACTUAL = " << std::setprecision(17)
                  << forward_actual << '\n';
        std::cout << "REVERSE_COMPARE_EXPECTED = 0" << '\n';
        std::cout << "REVERSE_COMPARE_ACTUAL = " << std::setprecision(17)
                  << reverse_actual << '\n';
        std::cout << "SAME_CRYPTO_CONTEXT = " << (same_context ? "YES" : "NO") << '\n';
        std::cout << "DISTANCE_LEVEL = " << near_result.level << '\n';
        std::cout << "DISTANCE_SCALE = " << std::setprecision(17)
                  << near_result.scale << '\n';
        std::cout << "DISTANCE_TOWER_COUNT = " << near_result.tower_count << '\n';
        std::cout << "EFFECTIVE_SCALE_SIGN = " << environment.scale_sign << '\n';
        std::cout << "EFFECTIVE_SWITCHING_VALUE_COUNT = "
                  << environment.switching_value_count << '\n';
        std::cout << "COMPARE_TIME_MS = " << std::setprecision(10)
                  << compare_time_ms << '\n';
        if (std::abs(forward_actual - 1.0) <= 0.25 &&
            std::abs(reverse_actual - 0.0) <= 0.25) {
            std::cout << "DISTANCE_TO_COMPARE = PASS" << '\n';
            std::cout << "READY_FOR_RESOLUTION_TEST = YES" << '\n';
            return EXIT_SUCCESS;
        }

        std::cout << "DISTANCE_TO_COMPARE = FAIL" << '\n';
        std::cout << "READY_FOR_RESOLUTION_TEST = NO" << '\n';
        return EXIT_FAILURE;
    }
    catch (const std::exception& error) {
        std::cerr << "DISTANCE_TO_COMPARE = FAIL" << '\n';
        std::cerr << "READY_FOR_RESOLUTION_TEST = NO" << '\n';
        std::cerr << "openfhe-tcga-distance-to-compare-test failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
