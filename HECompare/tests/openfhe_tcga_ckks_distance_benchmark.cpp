#include "hecompare/ckks_context.hpp"
#include "hecompare/ckks_distance.hpp"
#include "tests/tcga_data_loader.hpp"
#include "tests/test_helpers.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using hecompare::CkksEnvironment;
using hecompare::CkksParameters;
using hecompare::RealVector;
using hecompare::test::TcgaDataset;

struct DistanceRecord {
    std::size_t train_index = 0;
    std::string sample_id;
    std::string label;
    double plaintext_distance = 0.0;
    double ckks_distance = 0.0;
    double absolute_error = 0.0;
    std::size_t level = 0;
    double scale = 0.0;
};

std::string data_csv_default()
{
    return "HECompare/testdata/tcga-pancan-raw/data.csv";
}

std::string labels_csv_default()
{
    return "HECompare/testdata/tcga-pancan-raw/labels.csv";
}

std::string output_csv_default()
{
    return "KNN-experiment-res/he_ckks_tcga_query0_distances.csv";
}

std::size_t parse_nonnegative_count(
        const char* text,
        const char* label)
{
    try {
        const unsigned long long value = std::stoull(text);
        return static_cast<std::size_t>(value);
    }
    catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid ") + label + ": " + text);
    }
}

bool parse_bool_flag(const char* text, const char* label)
{
    const std::string value(text);
    if (value == "1" || value == "true" || value == "TRUE" || value == "yes")
        return true;
    if (value == "0" || value == "false" || value == "FALSE" || value == "no")
        return false;
    throw std::runtime_error(std::string("invalid ") + label + ": " + text);
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

void append_distance_records_csv(
        const std::string& output_csv,
        std::size_t query_index,
        const std::string& query_sample_id,
        const std::string& query_label,
        const std::vector<DistanceRecord>& records,
        bool append)
{
    const bool file_exists = std::filesystem::exists(output_csv);
    std::ios::openmode mode = std::ios::out;
    if (append)
        mode |= std::ios::app;
    else
        mode |= std::ios::trunc;

    std::ofstream out(output_csv, mode);
    if (!out)
        throw std::runtime_error("failed to open output csv: " + output_csv);

    if (!append || !file_exists) {
        out << "query_idx,query_sample_id,query_label,train_idx,train_sample_id,label,"
               "plaintext_distance,ckks_distance,abs_error,level,scale\n";
    }
    out << std::setprecision(17);
    for (const auto& record : records) {
        out << query_index << ','
            << query_sample_id << ','
            << query_label << ','
            << record.train_index << ','
            << record.sample_id << ','
            << record.label << ','
            << record.plaintext_distance << ','
            << record.ckks_distance << ','
            << record.absolute_error << ','
            << record.level << ','
            << record.scale << '\n';
    }
}

void append_single_distance_record_csv(
        const std::string& output_csv,
        std::size_t query_index,
        const std::string& query_sample_id,
        const std::string& query_label,
        const DistanceRecord& record,
        bool append)
{
    std::vector<DistanceRecord> one{record};
    append_distance_records_csv(
            output_csv,
            query_index,
            query_sample_id,
            query_label,
            one,
            append);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::string data_csv =
                argc >= 2 ? argv[1] : data_csv_default();
        const std::string labels_csv =
                argc >= 3 ? argv[2] : labels_csv_default();
        const std::string output_csv =
                argc >= 4 ? argv[3] : output_csv_default();
        const std::size_t start_train_index =
                argc >= 5 ? parse_nonnegative_count(argv[4], "start_train_index") : 1;
        const std::size_t max_candidates =
                argc >= 6 ? parse_nonnegative_count(argv[5], "max_candidates") : 12;
        const bool append_output =
                argc >= 7 ? parse_bool_flag(argv[6], "append_output") : false;

        using Clock = std::chrono::steady_clock;

        const auto total_begin = Clock::now();

        const auto load_begin = Clock::now();
        const auto dataset =
                hecompare::test::load_tcga_dataset(data_csv, labels_csv, 21.0);
        const auto load_end = Clock::now();

        constexpr std::size_t query_index = 0;
        if (dataset.sample_count != 801)
            throw std::runtime_error("expected 801 TCGA samples");
        if (query_index >= dataset.sample_count)
            throw std::runtime_error("query index out of range");
        if (start_train_index >= dataset.sample_count)
            throw std::runtime_error("start_train_index out of range");

        CkksParameters params;
        params.slot_count = 4096;
        params.ring_dimension = 8192;
        params.multiplicative_depth = 20;
        params.scale_sign = 64.0;
        params.scaling_technique = lbcrypto::FLEXIBLEAUTOEXT;
        params.fhew_parameter_set = lbcrypto::STD128;
        params.switching_value_count = 1;

        const auto keygen_begin = Clock::now();
        auto environment = hecompare::make_ckks_environment(params);
        const auto keygen_end = Clock::now();

        RealVector query(
                dataset.sample_data(query_index),
                dataset.sample_data(query_index) +
                        static_cast<std::ptrdiff_t>(dataset.feature_count));

        const auto encrypt_begin = Clock::now();
        const auto encrypted_query =
                hecompare::encrypt_query_chunks(environment, query);
        const auto encrypt_end = Clock::now();

        append_distance_records_csv(
                output_csv,
                query_index,
                dataset.sample_ids[query_index],
                dataset.labels[query_index],
                {},
                append_output);

        std::vector<DistanceRecord> records;
        const std::size_t remaining_after_start =
                dataset.sample_count > start_train_index
                        ? dataset.sample_count - start_train_index
                        : 0;
        const std::size_t planned_count =
                max_candidates == 0
                        ? remaining_after_start
                        : std::min(remaining_after_start, max_candidates);
        records.reserve(planned_count);

        const auto compute_begin = Clock::now();
        double distance_compute_total_seconds = 0.0;
        double decrypt_total_seconds = 0.0;

        for (std::size_t i = start_train_index; i < dataset.sample_count; ++i) {
            if (i == query_index)
                continue;
            if (max_candidates != 0 && records.size() >= max_candidates)
                break;

            RealVector train(
                    dataset.sample_data(i),
                    dataset.sample_data(i) +
                            static_cast<std::ptrdiff_t>(dataset.feature_count));

            const auto distance_begin = Clock::now();
            auto ciphertext = hecompare::evaluate_chunked_mean_squared_l2_distance(
                    environment,
                    encrypted_query,
                    train);
            const auto distance_end = Clock::now();

            const auto decrypt_begin = Clock::now();
            const double ckks_distance =
                    hecompare::test::decrypt_first_slot_for_test(environment, ciphertext);
            const auto decrypt_end = Clock::now();

            distance_compute_total_seconds +=
                    std::chrono::duration<double>(distance_end - distance_begin).count();
            decrypt_total_seconds +=
                    std::chrono::duration<double>(decrypt_end - decrypt_begin).count();

            const double plaintext_distance =
                    plaintext_distance_he(dataset, query_index, i);
            const double absolute_error =
                    std::abs(ckks_distance - plaintext_distance);

            DistanceRecord record;
            record.train_index = i;
            record.sample_id = dataset.sample_ids[i];
            record.label = dataset.labels[i];
            record.plaintext_distance = plaintext_distance;
            record.ckks_distance = ckks_distance;
            record.absolute_error = absolute_error;
            record.level = ciphertext->GetLevel();
            record.scale = ciphertext->GetScalingFactor();
            records.push_back(record);

            append_single_distance_record_csv(
                    output_csv,
                    query_index,
                    dataset.sample_ids[query_index],
                    dataset.labels[query_index],
                    record,
                    true);

            std::cout << "ITEM "
                      << records.size() << "/" << planned_count
                      << " train_idx=" << record.train_index
                      << " sample_id=" << record.sample_id
                      << " label=" << record.label
                      << " plaintext_distance=" << std::setprecision(17)
                      << record.plaintext_distance
                      << " ckks_distance=" << record.ckks_distance
                      << " abs_error=" << record.absolute_error
                      << " level=" << record.level
                      << " scale=" << record.scale
                      << '\n'
                      << std::flush;
        }
        const auto compute_end = Clock::now();

        const auto write_begin = Clock::now();
        const auto write_end = Clock::now();

        const auto total_end = Clock::now();

        const double load_seconds =
                std::chrono::duration<double>(load_end - load_begin).count();
        const double keygen_seconds =
                std::chrono::duration<double>(keygen_end - keygen_begin).count();
        const double encrypt_seconds =
                std::chrono::duration<double>(encrypt_end - encrypt_begin).count();
        const double compute_loop_seconds =
                std::chrono::duration<double>(compute_end - compute_begin).count();
        const double write_seconds =
                std::chrono::duration<double>(write_end - write_begin).count();
        const double total_seconds =
                std::chrono::duration<double>(total_end - total_begin).count();

        double max_abs_error = 0.0;
        for (const auto& record : records)
            max_abs_error = std::max(max_abs_error, record.absolute_error);

        std::cout << "BENCHMARK = HE_CKKS_TCGA_DISTANCE" << '\n';
        std::cout << "QUERY_INDEX = " << query_index << '\n';
        std::cout << "QUERY_SAMPLE_ID = " << dataset.sample_ids[query_index] << '\n';
        std::cout << "QUERY_LABEL = " << dataset.labels[query_index] << '\n';
        std::cout << "SAMPLE_COUNT = " << dataset.sample_count << '\n';
        std::cout << "FEATURE_COUNT = " << dataset.feature_count << '\n';
        std::cout << "START_TRAIN_INDEX = " << start_train_index << '\n';
        std::cout << "TRAIN_DISTANCE_COUNT = " << records.size() << '\n';
        std::cout << "APPEND_OUTPUT = " << (append_output ? "YES" : "NO") << '\n';
        std::cout << "QUERY_ENCRYPTION_COUNT = "
                  << encrypted_query.ciphertexts.size() << '\n';
        std::cout << "QUERY_REUSED_FOR_ALL_SELECTED = YES" << '\n';
        std::cout << "CKKS_SLOT_COUNT = " << environment.slot_count << '\n';
        std::cout << "CKKS_RING_DIMENSION = " << params.ring_dimension << '\n';
        std::cout << "CKKS_SCALE_SIGN = " << environment.scale_sign << '\n';
        std::cout << "LOAD_DATA_SECONDS = " << std::setprecision(10)
                  << load_seconds << '\n';
        std::cout << "KEYGEN_SECONDS = " << std::setprecision(10)
                  << keygen_seconds << '\n';
        std::cout << "ENCRYPT_QUERY_SECONDS = " << std::setprecision(10)
                  << encrypt_seconds << '\n';
        std::cout << "DISTANCE_COMPUTE_TOTAL_SECONDS = " << std::setprecision(10)
                  << distance_compute_total_seconds << '\n';
        std::cout << "DISTANCE_COMPUTE_LOOP_SECONDS = " << std::setprecision(10)
                  << compute_loop_seconds << '\n';
        std::cout << "AVG_DISTANCE_COMPUTE_MS = " << std::setprecision(10)
                  << distance_compute_total_seconds * 1000.0 /
                             static_cast<double>(records.size()) << '\n';
        std::cout << "DECRYPT_TOTAL_SECONDS = " << std::setprecision(10)
                  << decrypt_total_seconds << '\n';
        std::cout << "AVG_DECRYPT_MS = " << std::setprecision(10)
                  << decrypt_total_seconds * 1000.0 /
                             static_cast<double>(records.size()) << '\n';
        std::cout << "WRITE_CSV_SECONDS = " << std::setprecision(10)
                  << write_seconds << '\n';
        std::cout << "MAX_ABS_ERROR = " << std::setprecision(17)
                  << max_abs_error << '\n';
        std::cout << "OUTPUT_CSV = " << output_csv << '\n';
        std::cout << "TOTAL_SECONDS = " << std::setprecision(10)
                  << total_seconds << '\n';
        std::cout << "HE_CKKS_TCGA_DISTANCE_BENCH = PASS" << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr << "HE_CKKS_TCGA_DISTANCE_BENCH = FAIL" << '\n';
        std::cerr << "ERROR = " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
