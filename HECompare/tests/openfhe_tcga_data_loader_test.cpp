#include "tests/tcga_data_loader.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::string data_csv_default()
{
    return "HECompare/testdata/tcga-pancan-raw/data.csv";
}

std::string labels_csv_default()
{
    return "HECompare/testdata/tcga-pancan-raw/labels.csv";
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

        std::map<std::string, int> label_counts;
        for (const auto& label : dataset.labels)
            ++label_counts[label];

        expect(dataset.sample_count == 801, "sample_count mismatch");
        expect(dataset.feature_count == 20531, "feature_count mismatch");
        expect(dataset.sample_ids.size() == 801, "sample_ids size mismatch");
        expect(dataset.labels.size() == 801, "labels size mismatch");
        expect(dataset.values.size() == 801ull * 20531ull, "values size mismatch");

        expect(label_counts["PRAD"] == 136, "PRAD count mismatch");
        expect(label_counts["LUAD"] == 141, "LUAD count mismatch");
        expect(label_counts["BRCA"] == 300, "BRCA count mismatch");
        expect(label_counts["KIRC"] == 146, "KIRC count mismatch");
        expect(label_counts["COAD"] == 78, "COAD count mismatch");

        double max_value = 0.0;
        for (double value : dataset.values)
            max_value = std::max(max_value, value);
        expect(max_value < 1.0, "scaled max value is not < 1");

        const auto chunks =
                hecompare::test::make_ckks_chunks(dataset, 0, 4096);

        expect(chunks.size() == 6, "chunk count mismatch");
        expect(chunks.back().size() == 4096, "last chunk size mismatch");

        for (std::size_t i = 51; i < chunks.back().size(); ++i) {
            if (chunks.back()[i] != 0.0)
                throw std::runtime_error("last chunk padding is not zero");
        }

        std::vector<double> reconstructed;
        reconstructed.reserve(dataset.feature_count);
        for (const auto& chunk : chunks) {
            const std::size_t remaining =
                    dataset.feature_count - reconstructed.size();
            const std::size_t take = std::min<std::size_t>(chunk.size(), remaining);
            reconstructed.insert(
                    reconstructed.end(),
                    chunk.begin(),
                    chunk.begin() + static_cast<std::ptrdiff_t>(take));
        }

        expect(
                reconstructed.size() == dataset.feature_count,
                "reconstructed feature count mismatch");

        const double* sample0 = dataset.sample_data(0);
        for (std::size_t i = 0; i < dataset.feature_count; ++i) {
            if (reconstructed[i] != sample0[i]) {
                throw std::runtime_error(
                        "chunk reconstruction mismatch at feature index " +
                        std::to_string(i));
            }
        }

        std::cout << "TCGA_DATA_LOAD = PASS" << '\n';
        std::cout << "SAMPLE_COUNT = " << dataset.sample_count << '\n';
        std::cout << "FEATURE_COUNT = " << dataset.feature_count << '\n';
        std::cout << "LABEL_ALIGNMENT = PASS" << '\n';
        std::cout << "GLOBAL_SCALE = 21" << '\n';
        std::cout << "CHUNK_COUNT = " << chunks.size() << '\n';
        std::cout << "CHUNK_RECONSTRUCTION = PASS" << '\n';
        std::cout << "READY_FOR_CKKS_DISTANCE_INPUT = YES" << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr << "TCGA_DATA_LOAD = FAIL" << '\n';
        std::cerr << "READY_FOR_CKKS_DISTANCE_INPUT = NO" << '\n';
        std::cerr << "tcga-data-loader-test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
