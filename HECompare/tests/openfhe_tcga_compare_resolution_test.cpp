#include "hecompare/ckks_compare.hpp"
#include "hecompare/ckks_context.hpp"
#include "hecompare/ckks_distance.hpp"
#include "tests/tcga_data_loader.hpp"
#include "tests/test_helpers.hpp"

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
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using hecompare::CkksEnvironment;
using hecompare::CkksParameters;
using hecompare::RealVector;
using hecompare::test::TcgaDataset;

enum class RunMode {
    Quick,
    Medium,
    Full,
};

struct Config {
    RunMode mode = RunMode::Quick;

    std::string data_csv =
            "HECompare/testdata/tcga-pancan-raw/data.csv";
    std::string labels_csv =
            "HECompare/testdata/tcga-pancan-raw/labels.csv";

    // Full 801-query plaintext D5/D6 cache.
    std::string boundary_cache_path;

    // Reuse selected cases from a prior raw result CSV. This bypasses
    // the O(n^2 * feature_count) plaintext scan entirely.
    std::string cases_csv_path;

    std::string raw_csv_path;
    std::string summary_path;
    std::string aggregate_raw_path;

    std::optional<std::string> case_name;

    double scale_sign = 64.0;
    int key_repeat_index = 0;
    double decision_tolerance = 0.25;

    bool rebuild_boundary_cache = false;
    bool scan_only = false;
    bool append_raw_csv = false;
};

struct BoundaryCase {
    std::string name;
    std::size_t query_index = 0;
    std::size_t d5_candidate_index = 0;
    std::size_t d6_candidate_index = 0;
    double d5 = 0.0;
    double d6 = 0.0;
    double gap = 0.0;
};

struct PlaintextScan {
    std::vector<BoundaryCase> boundaries;

    double pairwise_distance_min =
            std::numeric_limits<double>::infinity();
    double pairwise_distance_max =
            -std::numeric_limits<double>::infinity();
    double d5_min =
            std::numeric_limits<double>::infinity();
    double d5_max =
            -std::numeric_limits<double>::infinity();
    double d6_min =
            std::numeric_limits<double>::infinity();
    double d6_max =
            -std::numeric_limits<double>::infinity();

    std::size_t nonpositive_gap_count = 0;
    double elapsed_ms = 0.0;
};

struct RawCompareResult {
    std::string mode;
    double scale_sign = 0.0;
    int key_repeat_index = 0;
    BoundaryCase boundary;

    double context_setup_ms = 0.0;
    double encryption_ms = 0.0;
    double forward_compare_ms = 0.0;
    double reverse_compare_ms = 0.0;

    double forward_actual =
            std::numeric_limits<double>::quiet_NaN();
    double reverse_actual =
            std::numeric_limits<double>::quiet_NaN();
    double margin =
            std::numeric_limits<double>::quiet_NaN();

    uint32_t p_lwe = 0;
    double signed_half_p_lwe = 0.0;
    double scaled_gap = 0.0;
    double rounded_scaled_gap = 0.0;
    double signed_range_usage = 0.0;

    bool forward_pass = false;
    bool reverse_pass = false;
    bool pair_pass = false;
};

struct AggregateCase {
    BoundaryCase boundary;
    int total_runs = 0;
    int successful_runs = 0;

    double min_forward =
            std::numeric_limits<double>::infinity();
    double max_forward =
            -std::numeric_limits<double>::infinity();
    double min_reverse =
            std::numeric_limits<double>::infinity();
    double max_reverse =
            -std::numeric_limits<double>::infinity();
    double min_margin =
            std::numeric_limits<double>::infinity();

    double total_forward_ms = 0.0;
    double total_reverse_ms = 0.0;
};

struct AggregateScale {
    double scale_sign = 0.0;
    std::map<std::string, AggregateCase> cases;
};

std::string mode_name(RunMode mode)
{
    switch (mode) {
    case RunMode::Quick:
        return "quick";
    case RunMode::Medium:
        return "medium";
    case RunMode::Full:
        return "full";
    }
    return "unknown";
}

std::vector<std::string> split_csv_line(
        const std::string& line)
{
    // All files written by this test contain unquoted numeric / identifier
    // fields. Reject quoted CSV rather than silently parsing it incorrectly.
    if (line.find('"') != std::string::npos)
        throw std::runtime_error(
                "quoted CSV fields are not supported");

    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;

    while (std::getline(stream, field, ','))
        fields.push_back(field);

    if (!line.empty() && line.back() == ',')
        fields.emplace_back();

    return fields;
}

std::map<std::string, std::size_t> make_header_index(
        const std::vector<std::string>& header)
{
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < header.size(); ++i)
        index.emplace(header[i], i);
    return index;
}

const std::string& required_field(
        const std::vector<std::string>& fields,
        const std::map<std::string, std::size_t>& index,
        const std::string& name)
{
    const auto it = index.find(name);
    if (it == index.end())
        throw std::runtime_error(
                "CSV is missing required column: " + name);
    if (it->second >= fields.size())
        throw std::runtime_error(
                "CSV row is shorter than header");
    return fields[it->second];
}

bool file_exists_and_nonempty(
        const std::string& path)
{
    if (path.empty())
        return false;

    std::error_code error;
    const auto size =
            std::filesystem::file_size(path, error);
    return !error && size > 0;
}

Config parse_args(int argc, char** argv)
{
    Config config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--quick") {
            config.mode = RunMode::Quick;
        }
        else if (arg == "--medium") {
            config.mode = RunMode::Medium;
        }
        else if (arg == "--full") {
            config.mode = RunMode::Full;
        }
        else if (arg == "--data" && i + 1 < argc) {
            config.data_csv = argv[++i];
        }
        else if (arg == "--labels" && i + 1 < argc) {
            config.labels_csv = argv[++i];
        }
        else if (arg == "--boundary-cache" && i + 1 < argc) {
            config.boundary_cache_path = argv[++i];
        }
        else if (arg == "--cases-csv" && i + 1 < argc) {
            config.cases_csv_path = argv[++i];
        }
        else if (arg == "--raw-csv" && i + 1 < argc) {
            config.raw_csv_path = argv[++i];
        }
        else if (arg == "--summary" && i + 1 < argc) {
            config.summary_path = argv[++i];
        }
        else if (arg == "--aggregate-raw" && i + 1 < argc) {
            config.aggregate_raw_path = argv[++i];
        }
        else if (arg == "--case-name" && i + 1 < argc) {
            config.case_name = argv[++i];
        }
        else if (arg == "--scale-sign" && i + 1 < argc) {
            config.scale_sign = std::stod(argv[++i]);
        }
        else if (arg == "--key-repeat-index" && i + 1 < argc) {
            config.key_repeat_index = std::stoi(argv[++i]);
        }
        else if (arg == "--decision-tol" && i + 1 < argc) {
            config.decision_tolerance = std::stod(argv[++i]);
        }
        else if (arg == "--rebuild-boundary-cache") {
            config.rebuild_boundary_cache = true;
        }
        else if (arg == "--scan-only") {
            config.scan_only = true;
        }
        else if (arg == "--append-raw-csv") {
            config.append_raw_csv = true;
        }
        else {
            throw std::invalid_argument(
                    "unknown or incomplete argument: " + arg);
        }
    }

    if (!config.aggregate_raw_path.empty())
        return config;

    if (!std::isfinite(config.scale_sign) ||
        config.scale_sign <= 0.0) {
        throw std::invalid_argument(
                "scale_sign must be finite and positive");
    }

    if (config.key_repeat_index < 0)
        throw std::invalid_argument(
                "key_repeat_index must be non-negative");

    if (!std::isfinite(config.decision_tolerance) ||
        config.decision_tolerance <= 0.0 ||
        config.decision_tolerance >= 0.5) {
        throw std::invalid_argument(
                "decision tolerance must be in (0, 0.5)");
    }

    if (!config.cases_csv_path.empty() &&
        config.rebuild_boundary_cache) {
        throw std::invalid_argument(
                "--cases-csv and --rebuild-boundary-cache "
                "cannot be used together");
    }

    return config;
}

CkksParameters make_tcga_compare_parameters(
        double scale_sign)
{
    CkksParameters params;
    params.slot_count = 4096;
    params.ring_dimension = 8192;
    params.multiplicative_depth = 20;
    params.scale_sign = scale_sign;
    params.scaling_technique =
            lbcrypto::FLEXIBLEAUTOEXT;
    params.fhew_parameter_set =
            lbcrypto::STD128;
    params.switching_value_count = 1;
    return params;
}

double pairwise_mean_squared_distance(
        const double* lhs,
        const double* rhs,
        std::size_t feature_count)
{
    double sum = 0.0;
    std::size_t i = 0;

    for (; i + 3 < feature_count; i += 4) {
        const double d0 = lhs[i] - rhs[i];
        const double d1 = lhs[i + 1] - rhs[i + 1];
        const double d2 = lhs[i + 2] - rhs[i + 2];
        const double d3 = lhs[i + 3] - rhs[i + 3];

        sum += d0 * d0 +
               d1 * d1 +
               d2 * d2 +
               d3 * d3;
    }

    for (; i < feature_count; ++i) {
        const double difference = lhs[i] - rhs[i];
        sum += difference * difference;
    }

    return sum /
           static_cast<double>(feature_count);
}

PlaintextScan scan_plaintext_boundaries(
        const TcgaDataset& dataset)
{
    if (dataset.sample_count < 7)
        throw std::invalid_argument(
                "at least 7 samples are required");
    if (dataset.feature_count == 0)
        throw std::invalid_argument(
                "feature vectors must not be empty");

    const auto begin = Clock::now();
    const std::size_t sample_count =
            dataset.sample_count;

    std::vector<double> distances(
            sample_count * sample_count,
            0.0);

    PlaintextScan scan;
    scan.boundaries.reserve(sample_count);

    for (std::size_t lhs_index = 0;
         lhs_index < sample_count;
         ++lhs_index) {
        const double* lhs =
                dataset.sample_data(lhs_index);

        for (std::size_t rhs_index = lhs_index + 1;
             rhs_index < sample_count;
             ++rhs_index) {
            const double distance =
                    pairwise_mean_squared_distance(
                            lhs,
                            dataset.sample_data(rhs_index),
                            dataset.feature_count);

            distances[
                    lhs_index * sample_count +
                    rhs_index] = distance;
            distances[
                    rhs_index * sample_count +
                    lhs_index] = distance;

            scan.pairwise_distance_min =
                    std::min(
                            scan.pairwise_distance_min,
                            distance);
            scan.pairwise_distance_max =
                    std::max(
                            scan.pairwise_distance_max,
                            distance);
        }
    }

    for (std::size_t query_index = 0;
         query_index < sample_count;
         ++query_index) {
        std::vector<std::pair<double, std::size_t>>
                ordered;
        ordered.reserve(sample_count - 1);

        for (std::size_t candidate_index = 0;
             candidate_index < sample_count;
             ++candidate_index) {
            if (candidate_index == query_index)
                continue;

            ordered.emplace_back(
                    distances[
                            query_index * sample_count +
                            candidate_index],
                    candidate_index);
        }

        std::sort(
                ordered.begin(),
                ordered.end(),
                [](const auto& lhs, const auto& rhs) {
                    if (lhs.first != rhs.first)
                        return lhs.first < rhs.first;
                    return lhs.second < rhs.second;
                });

        BoundaryCase boundary;
        boundary.query_index = query_index;
        boundary.d5 = ordered.at(4).first;
        boundary.d5_candidate_index =
                ordered.at(4).second;
        boundary.d6 = ordered.at(5).first;
        boundary.d6_candidate_index =
                ordered.at(5).second;
        boundary.gap = boundary.d6 - boundary.d5;

        scan.d5_min =
                std::min(scan.d5_min, boundary.d5);
        scan.d5_max =
                std::max(scan.d5_max, boundary.d5);
        scan.d6_min =
                std::min(scan.d6_min, boundary.d6);
        scan.d6_max =
                std::max(scan.d6_max, boundary.d6);

        if (boundary.gap <= 0.0)
            ++scan.nonpositive_gap_count;

        scan.boundaries.push_back(boundary);
    }

    const auto end = Clock::now();
    scan.elapsed_ms =
            std::chrono::duration<double, std::milli>(
                    end - begin).count();

    return scan;
}

void validate_boundary_against_dataset(
        const BoundaryCase& boundary,
        const TcgaDataset& dataset)
{
    const std::size_t sample_count =
            dataset.sample_count;

    if (boundary.query_index >= sample_count ||
        boundary.d5_candidate_index >= sample_count ||
        boundary.d6_candidate_index >= sample_count) {
        throw std::runtime_error(
                "boundary cache contains out-of-range sample index");
    }

    if (!std::isfinite(boundary.d5) ||
        !std::isfinite(boundary.d6) ||
        !std::isfinite(boundary.gap)) {
        throw std::runtime_error(
                "boundary cache contains non-finite value");
    }

    const double recomputed_gap =
            boundary.d6 - boundary.d5;
    const double tolerance =
            std::max(
                    1e-15,
                    std::abs(recomputed_gap) * 1e-12);

    if (std::abs(recomputed_gap - boundary.gap) >
        tolerance) {
        throw std::runtime_error(
                "boundary cache gap is inconsistent "
                "with d5 and d6");
    }
}

void save_boundary_cache(
        const std::string& path,
        const PlaintextScan& scan,
        const TcgaDataset& dataset)
{
    if (path.empty())
        throw std::invalid_argument(
                "boundary cache path must not be empty");

    std::ofstream output(path);
    if (!output)
        throw std::runtime_error(
                "failed to create boundary cache: " + path);

    output << std::setprecision(17);
    output << "# schema=tcga_d5d6_boundary_cache_v1\n";
    output << "# sample_count="
           << dataset.sample_count << '\n';
    output << "# feature_count="
           << dataset.feature_count << '\n';
    output << "# normalization_divisor=21\n";
    output << "# distance_definition=mean_squared_l2\n";
    output << "query_index,query_id,"
              "d5_candidate_index,d5_candidate_id,"
              "d6_candidate_index,d6_candidate_id,"
              "d5,d6,gap\n";

    for (const auto& boundary : scan.boundaries) {
        output
            << boundary.query_index << ','
            << dataset.sample_ids.at(
                    boundary.query_index) << ','
            << boundary.d5_candidate_index << ','
            << dataset.sample_ids.at(
                    boundary.d5_candidate_index) << ','
            << boundary.d6_candidate_index << ','
            << dataset.sample_ids.at(
                    boundary.d6_candidate_index) << ','
            << boundary.d5 << ','
            << boundary.d6 << ','
            << boundary.gap << '\n';
    }

    output.flush();
    if (!output)
        throw std::runtime_error(
                "failed while writing boundary cache");
}

PlaintextScan load_boundary_cache(
        const std::string& path,
        const TcgaDataset& dataset)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error(
                "failed to open boundary cache: " + path);

    PlaintextScan scan;
    std::string line;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> header;

    while (std::getline(input, line)) {
        if (line.empty())
            continue;

        if (line.rfind("# ", 0) == 0)
            line.erase(0, 2);

        if (line.rfind("#", 0) == 0) {
            line.erase(0, 1);
            const auto equals = line.find('=');
            if (equals != std::string::npos) {
                metadata.emplace(
                        line.substr(0, equals),
                        line.substr(equals + 1));
            }
            continue;
        }

        header = split_csv_line(line);
        break;
    }

    if (header.empty())
        throw std::runtime_error(
                "boundary cache has no CSV header");

    if (metadata["schema"] !=
        "tcga_d5d6_boundary_cache_v1") {
        throw std::runtime_error(
                "unsupported boundary cache schema");
    }

    if (std::stoull(metadata["sample_count"]) !=
        dataset.sample_count) {
        throw std::runtime_error(
                "boundary cache sample_count mismatch");
    }

    if (std::stoull(metadata["feature_count"]) !=
        dataset.feature_count) {
        throw std::runtime_error(
                "boundary cache feature_count mismatch");
    }

    if (metadata["normalization_divisor"] != "21" ||
        metadata["distance_definition"] !=
                "mean_squared_l2") {
        throw std::runtime_error(
                "boundary cache distance definition mismatch");
    }

    const auto index = make_header_index(header);

    while (std::getline(input, line)) {
        if (line.empty())
            continue;

        const auto fields = split_csv_line(line);

        BoundaryCase boundary;
        boundary.query_index =
                std::stoull(required_field(
                        fields, index, "query_index"));
        boundary.d5_candidate_index =
                std::stoull(required_field(
                        fields, index,
                        "d5_candidate_index"));
        boundary.d6_candidate_index =
                std::stoull(required_field(
                        fields, index,
                        "d6_candidate_index"));
        boundary.d5 =
                std::stod(required_field(
                        fields, index, "d5"));
        boundary.d6 =
                std::stod(required_field(
                        fields, index, "d6"));
        boundary.gap =
                std::stod(required_field(
                        fields, index, "gap"));

        validate_boundary_against_dataset(
                boundary,
                dataset);

        if (required_field(
                    fields, index, "query_id") !=
            dataset.sample_ids.at(
                    boundary.query_index)) {
            throw std::runtime_error(
                    "boundary cache query sample ID mismatch");
        }

        if (required_field(
                    fields, index,
                    "d5_candidate_id") !=
            dataset.sample_ids.at(
                    boundary.d5_candidate_index)) {
            throw std::runtime_error(
                    "boundary cache D5 sample ID mismatch");
        }

        if (required_field(
                    fields, index,
                    "d6_candidate_id") !=
            dataset.sample_ids.at(
                    boundary.d6_candidate_index)) {
            throw std::runtime_error(
                    "boundary cache D6 sample ID mismatch");
        }

        scan.d5_min =
                std::min(scan.d5_min, boundary.d5);
        scan.d5_max =
                std::max(scan.d5_max, boundary.d5);
        scan.d6_min =
                std::min(scan.d6_min, boundary.d6);
        scan.d6_max =
                std::max(scan.d6_max, boundary.d6);

        if (boundary.gap <= 0.0)
            ++scan.nonpositive_gap_count;

        scan.boundaries.push_back(boundary);
    }

    if (scan.boundaries.size() !=
        dataset.sample_count) {
        throw std::runtime_error(
                "boundary cache does not contain "
                "one boundary per query");
    }

    return scan;
}

std::vector<BoundaryCase> load_selected_cases_from_raw_csv(
        const std::string& path,
        const TcgaDataset& dataset)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error(
                "failed to open cases CSV: " + path);

    std::string line;
    if (!std::getline(input, line))
        throw std::runtime_error(
                "cases CSV is empty");

    const auto header = split_csv_line(line);
    const auto index = make_header_index(header);

    std::vector<BoundaryCase> selected;
    std::set<std::tuple<
            std::string,
            std::size_t,
            std::size_t,
            std::size_t>> seen;

    while (std::getline(input, line)) {
        if (line.empty())
            continue;

        const auto fields = split_csv_line(line);

        BoundaryCase boundary;
        boundary.name =
                required_field(
                        fields, index, "case_name");
        boundary.query_index =
                std::stoull(required_field(
                        fields, index, "query_index"));
        boundary.d5_candidate_index =
                std::stoull(required_field(
                        fields, index,
                        "d5_candidate_index"));
        boundary.d6_candidate_index =
                std::stoull(required_field(
                        fields, index,
                        "d6_candidate_index"));
        boundary.d5 =
                std::stod(required_field(
                        fields, index, "d5"));
        boundary.d6 =
                std::stod(required_field(
                        fields, index, "d6"));
        boundary.gap =
                std::stod(required_field(
                        fields, index, "gap"));

        validate_boundary_against_dataset(
                boundary,
                dataset);

        const auto identity =
                std::make_tuple(
                        boundary.name,
                        boundary.query_index,
                        boundary.d5_candidate_index,
                        boundary.d6_candidate_index);

        if (seen.insert(identity).second)
            selected.push_back(boundary);
    }

    if (selected.empty())
        throw std::runtime_error(
                "cases CSV contains no reusable boundary cases");

    return selected;
}

std::size_t quantile_index(
        std::size_t count,
        double quantile)
{
    if (count == 0)
        throw std::invalid_argument(
                "quantile of empty boundary collection");

    if (quantile <= 0.0)
        return 0;
    if (quantile >= 1.0)
        return count - 1;

    return static_cast<std::size_t>(
            std::llround(
                    quantile *
                    static_cast<double>(count - 1)));
}

void add_unique_case(
        std::vector<BoundaryCase>& selected,
        BoundaryCase boundary,
        const std::string& name)
{
    for (const auto& existing : selected) {
        if (existing.query_index ==
                    boundary.query_index &&
            existing.d5_candidate_index ==
                    boundary.d5_candidate_index &&
            existing.d6_candidate_index ==
                    boundary.d6_candidate_index) {
            return;
        }
    }

    boundary.name = name;
    selected.push_back(std::move(boundary));
}

std::vector<BoundaryCase> select_boundary_cases(
        const PlaintextScan& scan,
        RunMode mode)
{
    std::vector<BoundaryCase> positive;
    for (const auto& boundary : scan.boundaries) {
        if (boundary.gap > 0.0)
            positive.push_back(boundary);
    }

    if (positive.empty())
        throw std::runtime_error(
                "no positive D5/D6 boundary gap exists");

    std::sort(
            positive.begin(),
            positive.end(),
            [](const BoundaryCase& lhs,
               const BoundaryCase& rhs) {
                if (lhs.gap != rhs.gap)
                    return lhs.gap < rhs.gap;
                return lhs.query_index <
                       rhs.query_index;
            });

    std::vector<BoundaryCase> selected;

    add_unique_case(
            selected,
            positive.front(),
            "gap_min_positive");

    if (mode == RunMode::Quick) {
        add_unique_case(
                selected,
                positive.at(quantile_index(
                        positive.size(), 0.05)),
                "gap_p05");
        add_unique_case(
                selected,
                positive.at(quantile_index(
                        positive.size(), 0.50)),
                "gap_p50");
        return selected;
    }

    add_unique_case(
            selected,
            positive.at(quantile_index(
                    positive.size(), 0.01)),
            "gap_p01");
    add_unique_case(
            selected,
            positive.at(quantile_index(
                    positive.size(), 0.05)),
            "gap_p05");
    add_unique_case(
            selected,
            positive.at(quantile_index(
                    positive.size(), 0.50)),
            "gap_p50");
    add_unique_case(
            selected,
            positive.back(),
            "gap_max");

    if (mode == RunMode::Full) {
        auto by_base = positive;
        std::sort(
                by_base.begin(),
                by_base.end(),
                [](const BoundaryCase& lhs,
                   const BoundaryCase& rhs) {
                    if (lhs.d5 != rhs.d5)
                        return lhs.d5 < rhs.d5;
                    return lhs.query_index <
                           rhs.query_index;
                });

        add_unique_case(
                selected,
                by_base.front(),
                "base_low");
        add_unique_case(
                selected,
                by_base.at(quantile_index(
                        by_base.size(), 0.50)),
                "base_mid");
        add_unique_case(
                selected,
                by_base.back(),
                "base_high");
    }

    return selected;
}

void filter_case_name(
        std::vector<BoundaryCase>& cases,
        const std::optional<std::string>& case_name)
{
    if (!case_name.has_value())
        return;

    cases.erase(
            std::remove_if(
                    cases.begin(),
                    cases.end(),
                    [&](const BoundaryCase& boundary) {
                        return boundary.name !=
                               *case_name;
                    }),
            cases.end());

    if (cases.empty())
        throw std::runtime_error(
                "requested case_name was not found: " +
                *case_name);
}

RawCompareResult run_compare_case(
        const std::string& mode,
        const CkksEnvironment& environment,
        double scale_sign,
        int key_repeat_index,
        double context_setup_ms,
        const BoundaryCase& boundary,
        double decision_tolerance)
{
    RawCompareResult result;
    result.mode = mode;
    result.scale_sign = scale_sign;
    result.key_repeat_index = key_repeat_index;
    result.context_setup_ms = context_setup_ms;
    result.boundary = boundary;

    result.p_lwe = environment.p_lwe;
    result.signed_half_p_lwe =
            static_cast<double>(environment.p_lwe) / 2.0;
    result.scaled_gap =
            std::abs(boundary.gap) * environment.scale_sign;
    result.rounded_scaled_gap =
            std::round(result.scaled_gap);
    result.signed_range_usage =
            result.signed_half_p_lwe > 0.0
                ? result.scaled_gap /
                        result.signed_half_p_lwe
                : std::numeric_limits<double>::infinity();

    const auto encryption_begin = Clock::now();

    const auto encrypted_d5 =
            hecompare::encrypt_query(
                    environment,
                    RealVector{boundary.d5});

    const auto encrypted_d6 =
            hecompare::encrypt_query(
                    environment,
                    RealVector{boundary.d6});

    const auto encryption_end = Clock::now();
    result.encryption_ms =
            std::chrono::duration<double, std::milli>(
                    encryption_end -
                    encryption_begin).count();

    const auto forward_begin = Clock::now();
    const auto forward =
            hecompare::encrypted_less_than(
                    environment,
                    encrypted_d5,
                    encrypted_d6);
    const auto forward_end = Clock::now();

    result.forward_compare_ms =
            std::chrono::duration<double, std::milli>(
                    forward_end -
                    forward_begin).count();

    result.forward_actual =
            hecompare::test::
                    decrypt_first_slot_for_test(
                            environment,
                            forward);

    const auto reverse_begin = Clock::now();
    const auto reverse =
            hecompare::encrypted_less_than(
                    environment,
                    encrypted_d6,
                    encrypted_d5);
    const auto reverse_end = Clock::now();

    result.reverse_compare_ms =
            std::chrono::duration<double, std::milli>(
                    reverse_end -
                    reverse_begin).count();

    result.reverse_actual =
            hecompare::test::
                    decrypt_first_slot_for_test(
                            environment,
                            reverse);

    result.margin =
            result.forward_actual -
            result.reverse_actual;

    result.forward_pass =
            std::isfinite(result.forward_actual) &&
            std::abs(result.forward_actual - 1.0) <=
                    decision_tolerance;

    result.reverse_pass =
            std::isfinite(result.reverse_actual) &&
            std::abs(result.reverse_actual) <=
                    decision_tolerance;

    result.pair_pass =
            result.forward_pass &&
            result.reverse_pass &&
            result.margin >=
                    1.0 -
                    2.0 * decision_tolerance;

    return result;
}

std::string raw_csv_header()
{
    return
        "mode,scale_sign,key_repeat,case_name,"
        "query_index,d5_candidate_index,d6_candidate_index,"
        "d5,d6,gap,context_setup_ms,encryption_ms,"
        "forward_actual,reverse_actual,margin,"
        "forward_pass,reverse_pass,pair_pass,"
        "forward_compare_ms,reverse_compare_ms,"
        "p_lwe,signed_half_p_lwe,scaled_gap,"
        "rounded_scaled_gap,signed_range_usage";
}

std::string raw_csv_row(
        const RawCompareResult& result)
{
    std::ostringstream output;
    output << std::setprecision(17)
           << result.mode << ','
           << result.scale_sign << ','
           << result.key_repeat_index << ','
           << result.boundary.name << ','
           << result.boundary.query_index << ','
           << result.boundary.d5_candidate_index
           << ','
           << result.boundary.d6_candidate_index
           << ','
           << result.boundary.d5 << ','
           << result.boundary.d6 << ','
           << result.boundary.gap << ','
           << result.context_setup_ms << ','
           << result.encryption_ms << ','
           << result.forward_actual << ','
           << result.reverse_actual << ','
           << result.margin << ','
           << (result.forward_pass ? 1 : 0)
           << ','
           << (result.reverse_pass ? 1 : 0)
           << ','
           << (result.pair_pass ? 1 : 0)
           << ','
           << result.forward_compare_ms << ','
           << result.reverse_compare_ms << ','
           << result.p_lwe << ','
           << result.signed_half_p_lwe << ','
           << result.scaled_gap << ','
           << result.rounded_scaled_gap << ','
           << result.signed_range_usage;

    return output.str();
}

std::ofstream open_raw_csv(
        const Config& config)
{
    if (config.raw_csv_path.empty())
        return {};

    const bool existing_nonempty =
            file_exists_and_nonempty(
                    config.raw_csv_path);

    std::ios::openmode mode =
            std::ios::out;

    if (config.append_raw_csv)
        mode |= std::ios::app;
    else
        mode |= std::ios::trunc;

    std::ofstream output(
            config.raw_csv_path,
            mode);

    if (!output)
        throw std::runtime_error(
                "failed to open raw CSV: " +
                config.raw_csv_path);

    if (!config.append_raw_csv ||
        !existing_nonempty) {
        output << raw_csv_header() << '\n';
        output.flush();
    }

    return output;
}

std::ofstream open_summary(
        const Config& config)
{
    if (config.summary_path.empty())
        return {};

    std::ofstream output(
            config.summary_path,
            std::ios::out | std::ios::trunc);

    if (!output)
        throw std::runtime_error(
                "failed to open summary: " +
                config.summary_path);

    return output;
}

void write_summary_line(
        std::ostream& output,
        const std::string& line)
{
    output << line << '\n';
    output.flush();
}

RawCompareResult parse_raw_result(
        const std::vector<std::string>& fields,
        const std::map<std::string, std::size_t>& index)
{
    RawCompareResult result;
    result.mode =
            required_field(
                    fields, index, "mode");
    result.scale_sign =
            std::stod(required_field(
                    fields, index, "scale_sign"));
    result.key_repeat_index =
            std::stoi(required_field(
                    fields, index, "key_repeat"));

    result.boundary.name =
            required_field(
                    fields, index, "case_name");
    result.boundary.query_index =
            std::stoull(required_field(
                    fields, index, "query_index"));
    result.boundary.d5_candidate_index =
            std::stoull(required_field(
                    fields, index,
                    "d5_candidate_index"));
    result.boundary.d6_candidate_index =
            std::stoull(required_field(
                    fields, index,
                    "d6_candidate_index"));
    result.boundary.d5 =
            std::stod(required_field(
                    fields, index, "d5"));
    result.boundary.d6 =
            std::stod(required_field(
                    fields, index, "d6"));
    result.boundary.gap =
            std::stod(required_field(
                    fields, index, "gap"));

    result.context_setup_ms =
            std::stod(required_field(
                    fields, index,
                    "context_setup_ms"));
    result.encryption_ms =
            std::stod(required_field(
                    fields, index,
                    "encryption_ms"));
    result.forward_actual =
            std::stod(required_field(
                    fields, index,
                    "forward_actual"));
    result.reverse_actual =
            std::stod(required_field(
                    fields, index,
                    "reverse_actual"));
    result.margin =
            std::stod(required_field(
                    fields, index, "margin"));

    result.forward_pass =
            std::stoi(required_field(
                    fields, index,
                    "forward_pass")) != 0;
    result.reverse_pass =
            std::stoi(required_field(
                    fields, index,
                    "reverse_pass")) != 0;
    result.pair_pass =
            std::stoi(required_field(
                    fields, index,
                    "pair_pass")) != 0;

    result.forward_compare_ms =
            std::stod(required_field(
                    fields, index,
                    "forward_compare_ms"));
    result.reverse_compare_ms =
            std::stod(required_field(
                    fields, index,
                    "reverse_compare_ms"));

    return result;
}

int aggregate_raw_csv(
        const std::string& path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error(
                "failed to open aggregate raw CSV: " +
                path);

    std::string line;
    if (!std::getline(input, line))
        throw std::runtime_error(
                "aggregate raw CSV is empty");

    const auto header = split_csv_line(line);
    const auto index = make_header_index(header);

    std::map<double, AggregateScale> scales;
    std::set<std::string> global_case_names;

    while (std::getline(input, line)) {
        if (line.empty())
            continue;

        const auto fields = split_csv_line(line);
        const auto raw =
                parse_raw_result(fields, index);

        auto& scale =
                scales[raw.scale_sign];
        scale.scale_sign = raw.scale_sign;

        auto& aggregate_case =
                scale.cases[raw.boundary.name];

        if (aggregate_case.total_runs == 0)
            aggregate_case.boundary =
                    raw.boundary;

        ++aggregate_case.total_runs;
        if (raw.pair_pass)
            ++aggregate_case.successful_runs;

        aggregate_case.min_forward =
                std::min(
                        aggregate_case.min_forward,
                        raw.forward_actual);
        aggregate_case.max_forward =
                std::max(
                        aggregate_case.max_forward,
                        raw.forward_actual);
        aggregate_case.min_reverse =
                std::min(
                        aggregate_case.min_reverse,
                        raw.reverse_actual);
        aggregate_case.max_reverse =
                std::max(
                        aggregate_case.max_reverse,
                        raw.reverse_actual);
        aggregate_case.min_margin =
                std::min(
                        aggregate_case.min_margin,
                        raw.margin);
        aggregate_case.total_forward_ms +=
                raw.forward_compare_ms;
        aggregate_case.total_reverse_ms +=
                raw.reverse_compare_ms;

        global_case_names.insert(
                raw.boundary.name);
    }

    if (scales.empty())
        throw std::runtime_error(
                "aggregate raw CSV contains no rows");

    std::optional<double> candidate_scale_sign;

    std::cout << std::setprecision(17);
    std::cout << "AGGREGATE_SOURCE = "
              << path << '\n';
    std::cout << "AGGREGATE_CASE_COUNT = "
              << global_case_names.size() << '\n';

    for (const auto& [scale_value, scale] : scales) {
        bool all_cases_present =
                scale.cases.size() ==
                global_case_names.size();
        bool all_cases_stable =
                all_cases_present;

        std::cout << "SCALE_SIGN_GROUP_BEGIN = "
                  << scale_value << '\n';

        for (const auto& case_name :
             global_case_names) {
            const auto it =
                    scale.cases.find(case_name);

            if (it == scale.cases.end()) {
                all_cases_stable = false;
                std::cout
                    << "CASE_RESULT"
                    << " name=" << case_name
                    << " present=NO"
                    << " stable=NO\n";
                continue;
            }

            const auto& aggregate_case =
                    it->second;
            const bool stable =
                    aggregate_case.total_runs > 0 &&
                    aggregate_case.successful_runs ==
                            aggregate_case.total_runs;

            all_cases_stable =
                    all_cases_stable &&
                    stable;

            std::cout
                << "CASE_RESULT"
                << " name=" << case_name
                << " gap="
                << aggregate_case.boundary.gap
                << " success="
                << aggregate_case.successful_runs
                << "/"
                << aggregate_case.total_runs
                << " stable="
                << (stable ? "YES" : "NO")
                << " min_forward="
                << aggregate_case.min_forward
                << " max_forward="
                << aggregate_case.max_forward
                << " min_reverse="
                << aggregate_case.min_reverse
                << " max_reverse="
                << aggregate_case.max_reverse
                << " min_margin="
                << aggregate_case.min_margin
                << " mean_forward_ms="
                << aggregate_case.total_forward_ms /
                   aggregate_case.total_runs
                << " mean_reverse_ms="
                << aggregate_case.total_reverse_ms /
                   aggregate_case.total_runs
                << '\n';
        }

        std::cout << "ALL_CASES_PRESENT = "
                  << (all_cases_present ? "YES" : "NO")
                  << '\n';
        std::cout << "ALL_CASES_STABLE = "
                  << (all_cases_stable ? "YES" : "NO")
                  << '\n';
        std::cout << "SCALE_SIGN_GROUP_END = "
                  << scale_value << '\n';

        if (all_cases_stable &&
            (!candidate_scale_sign.has_value() ||
             scale_value <
                    *candidate_scale_sign)) {
            candidate_scale_sign =
                    scale_value;
        }
    }

    if (candidate_scale_sign.has_value()) {
        std::cout << "CANDIDATE_SCALE_SIGN_FOR_PRESENT_CASES = "
                  << *candidate_scale_sign << '\n';
        std::cout
            << "TCGA_SINGLE_VALUE_COMPARE_RESOLUTION = PASS\n";
        return EXIT_SUCCESS;
    }

    std::cout << "CANDIDATE_SCALE_SIGN_FOR_PRESENT_CASES = not_found\n";
    std::cout
        << "TCGA_SINGLE_VALUE_COMPARE_RESOLUTION = FAIL\n";
    return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Config config =
                parse_args(argc, argv);

        if (!config.aggregate_raw_path.empty())
            return aggregate_raw_csv(
                    config.aggregate_raw_path);

        const std::string mode =
                mode_name(config.mode);

        const auto dataset =
                hecompare::test::load_tcga_dataset(
                        config.data_csv,
                        config.labels_csv,
                        21.0);

        std::vector<BoundaryCase> selected_cases;
        std::optional<PlaintextScan> scan;

        if (!config.cases_csv_path.empty()) {
            selected_cases =
                    load_selected_cases_from_raw_csv(
                            config.cases_csv_path,
                            dataset);

            std::cout
                << "BOUNDARY_SOURCE = CASES_CSV\n";
            std::cout
                << "PLAINTEXT_SCAN_REUSED = YES\n";
        }
        else {
            const bool can_load_cache =
                    !config.boundary_cache_path.empty() &&
                    file_exists_and_nonempty(
                            config.boundary_cache_path) &&
                    !config.rebuild_boundary_cache;

            if (can_load_cache) {
                scan =
                    load_boundary_cache(
                            config.boundary_cache_path,
                            dataset);

                std::cout
                    << "BOUNDARY_SOURCE = FULL_CACHE\n";
                std::cout
                    << "PLAINTEXT_SCAN_REUSED = YES\n";
            }
            else {
                scan =
                    scan_plaintext_boundaries(
                            dataset);

                std::cout
                    << "BOUNDARY_SOURCE = FRESH_SCAN\n";
                std::cout
                    << "PLAINTEXT_SCAN_REUSED = NO\n";

                if (!config.boundary_cache_path.empty()) {
                    save_boundary_cache(
                            config.boundary_cache_path,
                            *scan,
                            dataset);

                    std::cout
                        << "BOUNDARY_CACHE_WRITTEN = "
                        << config.boundary_cache_path
                        << '\n';
                }
            }

            selected_cases =
                    select_boundary_cases(
                            *scan,
                            config.mode);
        }

        filter_case_name(
                selected_cases,
                config.case_name);

        std::cout << std::setprecision(17);
        std::cout << "TCGA_COMPARE_RESOLUTION_MODE = "
                  << mode << '\n';
        std::cout << "SAMPLE_COUNT = "
                  << dataset.sample_count << '\n';
        std::cout << "FEATURE_COUNT = "
                  << dataset.feature_count << '\n';
        std::cout << "SELECTED_CASE_COUNT = "
                  << selected_cases.size() << '\n';

        for (const auto& boundary :
             selected_cases) {
            std::cout
                << "SELECTED_CASE"
                << " name=" << boundary.name
                << " query_index="
                << boundary.query_index
                << " d5_candidate_index="
                << boundary.d5_candidate_index
                << " d6_candidate_index="
                << boundary.d6_candidate_index
                << " d5=" << boundary.d5
                << " d6=" << boundary.d6
                << " gap=" << boundary.gap
                << '\n';
        }

        if (config.scan_only) {
            std::cout << "PLAINTEXT_SCAN_ONLY = PASS\n";
            return EXIT_SUCCESS;
        }

        auto raw_csv = open_raw_csv(config);
        auto summary = open_summary(config);

        std::ostream* summary_output =
                summary
                    ? static_cast<std::ostream*>(&summary)
                    : static_cast<std::ostream*>(&std::cout);

        write_summary_line(
                *summary_output,
                "TCGA_COMPARE_RESOLUTION_MODE = " +
                mode);
        write_summary_line(
                *summary_output,
                "SINGLE_PROCESS_SCALE_SIGN = " +
                std::to_string(config.scale_sign));
        write_summary_line(
                *summary_output,
                "KEY_REPEAT_INDEX = " +
                std::to_string(
                        config.key_repeat_index));
        write_summary_line(
                *summary_output,
                "SELECTED_CASE_COUNT = " +
                std::to_string(
                        selected_cases.size()));

        const auto setup_begin = Clock::now();

        auto environment =
                hecompare::make_ckks_environment(
                        make_tcga_compare_parameters(
                                config.scale_sign));

        const auto setup_end = Clock::now();
        const double setup_ms =
                std::chrono::duration<double, std::milli>(
                        setup_end -
                        setup_begin).count();

        {
            std::ostringstream diagnostic;
            diagnostic << std::setprecision(17)
                       << "SCALING_CONTEXT"
                       << " scale_sign="
                       << environment.scale_sign
                       << " p_lwe="
                       << environment.p_lwe
                       << " signed_half_p_lwe="
                       << (static_cast<double>(
                               environment.p_lwe) / 2.0)
                       << " setup_ms="
                       << setup_ms;
            write_summary_line(
                    *summary_output,
                    diagnostic.str());
            if (summary)
                std::cout << diagnostic.str() << '\n';
        }

        bool all_cases_pass = true;

        for (const auto& boundary :
             selected_cases) {
            const auto result =
                    run_compare_case(
                            mode,
                            environment,
                            config.scale_sign,
                            config.key_repeat_index,
                            setup_ms,
                            boundary,
                            config.decision_tolerance);

            all_cases_pass =
                    all_cases_pass &&
                    result.pair_pass;

            if (raw_csv) {
                raw_csv
                    << raw_csv_row(result)
                    << '\n';
                raw_csv.flush();
            }

            std::ostringstream diagnostic;
            diagnostic
                 << std::setprecision(17)
                 << "SCALING_DIAGNOSTIC"
                 << " name="
                 << boundary.name
                 << " gap="
                 << boundary.gap
                 << " scale_sign="
                 << result.scale_sign
                 << " scaled_gap="
                 << result.scaled_gap
                 << " rounded_scaled_gap="
                 << result.rounded_scaled_gap
                 << " p_lwe="
                 << result.p_lwe
                 << " signed_half_p_lwe="
                 << result.signed_half_p_lwe
                 << " signed_range_usage="
                 << result.signed_range_usage;

            write_summary_line(
                    *summary_output,
                    diagnostic.str());

            if (summary)
                std::cout << diagnostic.str() << '\n';

            std::ostringstream line;
            line << std::setprecision(17)
                 << "CASE_RESULT"
                 << " name="
                 << boundary.name
                 << " gap="
                 << boundary.gap
                 << " forward="
                 << result.forward_actual
                 << " reverse="
                 << result.reverse_actual
                 << " margin="
                 << result.margin
                 << " pair_pass="
                 << (result.pair_pass
                        ? "YES"
                        : "NO")
                 << " forward_ms="
                 << result.forward_compare_ms
                 << " reverse_ms="
                 << result.reverse_compare_ms;

            write_summary_line(
                    *summary_output,
                    line.str());

            if (summary)
                std::cout << line.str() << '\n';
        }

        write_summary_line(
                *summary_output,
                std::string(
                    "SCALE_SIGN_ALL_CASES_STABLE = ") +
                (all_cases_pass ? "YES" : "NO"));

        write_summary_line(
                *summary_output,
                std::string(
                    "TCGA_SINGLE_VALUE_COMPARE_RESOLUTION = ") +
                (all_cases_pass ? "PASS" : "FAIL"));

        write_summary_line(
                *summary_output,
                std::string(
                    "READY_FOR_SELECTED_CASE_DISTANCE_TO_COMPARE = ") +
                (all_cases_pass ? "YES" : "NO"));

        const bool selected_worst_gap_case =
                selected_cases.size() == 1 &&
                selected_cases.front().name ==
                        "gap_min_positive";

        write_summary_line(
                *summary_output,
                std::string(
                    "READY_FOR_WORST_GAP_DISTANCE_TO_COMPARE = ") +
                (all_cases_pass && selected_worst_gap_case
                    ? "YES"
                    : "NOT_TESTED"));

        if (summary) {
            std::cout
                << "SCALE_SIGN_ALL_CASES_STABLE = "
                << (all_cases_pass ? "YES" : "NO")
                << '\n';
            std::cout
                << "TCGA_SINGLE_VALUE_COMPARE_RESOLUTION = "
                << (all_cases_pass ? "PASS" : "FAIL")
                << '\n';
        }

        return all_cases_pass
                ? EXIT_SUCCESS
                : EXIT_FAILURE;
    }
    catch (const std::exception& error) {
        std::cerr
            << "TCGA_SINGLE_VALUE_COMPARE_RESOLUTION = FAIL\n";
        std::cerr
            << "READY_FOR_SELECTED_CASE_DISTANCE_TO_COMPARE = NO\n";
        std::cerr
            << "READY_FOR_WORST_GAP_DISTANCE_TO_COMPARE = NOT_TESTED\n";
        std::cerr
            << "openfhe-tcga-compare-resolution-test failed: "
            << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
