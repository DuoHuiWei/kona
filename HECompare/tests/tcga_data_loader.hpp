#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hecompare::test {

inline void strip_trailing_cr(std::string& line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}

struct TcgaDataset {
    std::size_t sample_count = 0;
    std::size_t feature_count = 0;

    // Row-major layout:
    // values[sample_index * feature_count + feature_index]
    std::vector<double> values;

    std::vector<std::string> sample_ids;
    std::vector<std::string> labels;

    const double* sample_data(std::size_t sample_index) const
    {
        return values.data() + sample_index * feature_count;
    }
};

inline std::vector<std::string> split_csv_line(const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;

    while (std::getline(stream, field, ','))
        fields.push_back(field);

    if (!line.empty() && line.back() == ',')
        fields.emplace_back();

    return fields;
}

inline std::runtime_error make_tcga_error(
        const std::string& file_path,
        std::size_t line_number,
        const std::string& reason)
{
    std::ostringstream message;
    message << "TCGA loader error: file=" << file_path
            << " line=" << line_number
            << " reason=" << reason;
    return std::runtime_error(message.str());
}

inline TcgaDataset load_tcga_dataset(
        const std::string& data_csv_path,
        const std::string& labels_csv_path,
        double global_scale = 21.0)
{
    if (!std::isfinite(global_scale) || global_scale <= 0.0)
        throw std::invalid_argument("global_scale must be finite and positive");

    TcgaDataset dataset;

    std::ifstream data_file(data_csv_path);
    if (!data_file)
        throw std::runtime_error("failed to open data csv: " + data_csv_path);

    std::string line;
    std::size_t line_number = 0;

    if (!std::getline(data_file, line))
        throw make_tcga_error(data_csv_path, 1, "missing header line");
    ++line_number;
    strip_trailing_cr(line);

    const auto header = split_csv_line(line);
    if (header.size() < 2)
        throw make_tcga_error(data_csv_path, line_number, "header must contain sample id column plus feature columns");

    if (header.front().empty()) {
        // Accept the UCI file's blank sample-id header cell.
    } else if (header.front() != "sample" && header.front() != "Sample" &&
               header.front() != "sample_id" && header.front() != "SampleID") {
        throw make_tcga_error(
                data_csv_path,
                line_number,
                "unexpected first header cell '" + header.front() + "'");
    }

    dataset.feature_count = header.size() - 1;
    if (dataset.feature_count != 20531) {
        throw make_tcga_error(
                data_csv_path,
                line_number,
                "unexpected feature count " + std::to_string(dataset.feature_count));
    }

    std::unordered_set<std::string> seen_sample_ids;
    seen_sample_ids.reserve(801);
    dataset.sample_ids.reserve(801);
    dataset.values.reserve(801ull * dataset.feature_count);

    while (std::getline(data_file, line)) {
        ++line_number;
        strip_trailing_cr(line);
        if (line.empty())
            throw make_tcga_error(data_csv_path, line_number, "empty row is not allowed");

        const auto fields = split_csv_line(line);
        if (fields.size() != dataset.feature_count + 1) {
            throw make_tcga_error(
                    data_csv_path,
                    line_number,
                    "expected " + std::to_string(dataset.feature_count + 1) +
                            " columns, got " + std::to_string(fields.size()));
        }

        const std::string& sample_id = fields[0];
        if (sample_id.empty())
            throw make_tcga_error(data_csv_path, line_number, "empty sample id");

        if (!seen_sample_ids.insert(sample_id).second) {
            throw make_tcga_error(
                    data_csv_path,
                    line_number,
                    "duplicate sample id '" + sample_id + "'");
        }

        dataset.sample_ids.push_back(sample_id);

        for (std::size_t feature_index = 0; feature_index < dataset.feature_count; ++feature_index) {
            const std::string& token = fields[feature_index + 1];
            double value = 0.0;
            try {
                std::size_t consumed = 0;
                value = std::stod(token, &consumed);
                if (consumed != token.size()) {
                    throw make_tcga_error(
                            data_csv_path,
                            line_number,
                            "invalid numeric token '" + token +
                                    "' at feature index " + std::to_string(feature_index));
                }
            } catch (const std::invalid_argument&) {
                throw make_tcga_error(
                        data_csv_path,
                        line_number,
                        "invalid numeric token '" + token +
                                "' at feature index " + std::to_string(feature_index));
            } catch (const std::out_of_range&) {
                throw make_tcga_error(
                        data_csv_path,
                        line_number,
                        "out-of-range numeric token '" + token +
                                "' at feature index " + std::to_string(feature_index));
            }

            if (std::isnan(value))
                throw make_tcga_error(data_csv_path, line_number, "NaN value detected");
            if (!std::isfinite(value))
                throw make_tcga_error(data_csv_path, line_number, "Inf value detected");

            const double scaled = value / global_scale;
            if (!(scaled >= 0.0 && scaled < 1.0)) {
                throw make_tcga_error(
                        data_csv_path,
                        line_number,
                        "scaled value out of range [0,1): sample=" + sample_id +
                                " feature_index=" + std::to_string(feature_index) +
                                " scaled=" + std::to_string(scaled));
            }

            dataset.values.push_back(scaled);
        }
    }

    dataset.sample_count = dataset.sample_ids.size();

    std::ifstream labels_file(labels_csv_path);
    if (!labels_file)
        throw std::runtime_error("failed to open labels csv: " + labels_csv_path);

    if (!std::getline(labels_file, line))
        throw make_tcga_error(labels_csv_path, 1, "missing header line");
    strip_trailing_cr(line);

    std::unordered_map<std::string, std::string> label_by_sample;
    label_by_sample.reserve(dataset.sample_count);
    line_number = 1;

    while (std::getline(labels_file, line)) {
        ++line_number;
        strip_trailing_cr(line);
        if (line.empty())
            throw make_tcga_error(labels_csv_path, line_number, "empty row is not allowed");

        const auto fields = split_csv_line(line);
        if (fields.size() != 2) {
            throw make_tcga_error(
                    labels_csv_path,
                    line_number,
                    "expected 2 columns, got " + std::to_string(fields.size()));
        }

        const std::string& sample_id = fields[0];
        const std::string& label = fields[1];
        if (sample_id.empty())
            throw make_tcga_error(labels_csv_path, line_number, "empty sample id");
        if (label.empty())
            throw make_tcga_error(labels_csv_path, line_number, "empty label");

        if (!label_by_sample.emplace(sample_id, label).second) {
            throw make_tcga_error(
                    labels_csv_path,
                    line_number,
                    "duplicate label entry for sample id '" + sample_id + "'");
        }
    }

    if (label_by_sample.size() != dataset.sample_count) {
        throw std::runtime_error(
                "TCGA loader error: label sample count mismatch in " +
                labels_csv_path);
    }

    for (const auto& [sample_id, _label] : label_by_sample) {
        if (seen_sample_ids.find(sample_id) == seen_sample_ids.end()) {
            throw std::runtime_error(
                    "TCGA loader error: label exists for unknown sample id '" +
                    sample_id + "'");
        }
    }

    dataset.labels.reserve(dataset.sample_count);
    for (const auto& sample_id : dataset.sample_ids) {
        const auto it = label_by_sample.find(sample_id);
        if (it == label_by_sample.end()) {
            throw std::runtime_error(
                    "TCGA loader error: missing label for sample id '" + sample_id +
                    "' in " + labels_csv_path);
        }
        dataset.labels.push_back(it->second);
    }

    if (dataset.sample_count != 801)
        throw std::runtime_error("TCGA loader error: sample_count != 801");
    if (dataset.feature_count != 20531)
        throw std::runtime_error("TCGA loader error: feature_count != 20531");
    if (dataset.sample_ids.size() != 801)
        throw std::runtime_error("TCGA loader error: sample_ids.size() != 801");
    if (dataset.labels.size() != 801)
        throw std::runtime_error("TCGA loader error: labels.size() != 801");
    if (dataset.values.size() != 801 * 20531ull)
        throw std::runtime_error("TCGA loader error: values.size() != 801 * 20531");

    return dataset;
}

inline std::vector<std::vector<double>> make_ckks_chunks(
        const TcgaDataset& dataset,
        std::size_t sample_index,
        std::size_t slot_count)
{
    if (slot_count == 0)
        throw std::invalid_argument("slot_count must be greater than zero");
    if (sample_index >= dataset.sample_count)
        throw std::out_of_range("sample_index out of range");

    const double* sample = dataset.sample_data(sample_index);
    std::vector<std::vector<double>> chunks;

    for (std::size_t offset = 0; offset < dataset.feature_count; offset += slot_count) {
        const std::size_t take =
                std::min(slot_count, dataset.feature_count - offset);
        std::vector<double> chunk(slot_count, 0.0);
        for (std::size_t i = 0; i < take; ++i)
            chunk[i] = sample[offset + i];
        chunks.push_back(std::move(chunk));
    }

    return chunks;
}

} // namespace hecompare::test
