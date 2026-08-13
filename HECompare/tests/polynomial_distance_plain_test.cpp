#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Vec = std::vector<int64_t>;
using WideInt = __int128_t;

struct Dataset {
    int num_features = 0;
    int num_train = 0;
    int num_test = 0;
    std::vector<Vec> train_samples;
    std::vector<int64_t> train_labels;
    std::vector<Vec> test_samples;
    std::vector<int64_t> test_labels;
};

struct ArceneStats {
    int64_t max_abs_feature = 0;
    int64_t max_norm2 = 0;
    int64_t max_abs_dot = 0;
    int64_t max_distance = 0;
    int64_t max_abs_score = 0;
};

int64_t abs_i64(int64_t value)
{
    return value >= 0 ? value : -value;
}

int64_t to_i64_checked(WideInt value, const std::string& label)
{
    const WideInt min_i64 = static_cast<WideInt>(std::numeric_limits<int64_t>::min());
    const WideInt max_i64 = static_cast<WideInt>(std::numeric_limits<int64_t>::max());

    if (value < min_i64 || value > max_i64)
        throw std::overflow_error(label + " does not fit in int64_t");

    return static_cast<int64_t>(value);
}

WideInt ordinary_dot_wide(const Vec& lhs, const Vec& rhs)
{
    if (lhs.size() != rhs.size())
        throw std::runtime_error("ordinary_dot size mismatch");

    WideInt sum = 0;
    for (size_t i = 0; i < lhs.size(); ++i)
        sum += static_cast<WideInt>(lhs[i]) * static_cast<WideInt>(rhs[i]);
    return sum;
}

int64_t ordinary_dot(const Vec& lhs, const Vec& rhs)
{
    return to_i64_checked(ordinary_dot_wide(lhs, rhs), "ordinary_dot");
}

Vec encode_query_polynomial(const Vec& query)
{
    return query;
}

Vec encode_reversed_train_polynomial(const Vec& train)
{
    return Vec(train.rbegin(), train.rend());
}

Vec negacyclic_convolution(const Vec& lhs, const Vec& rhs, size_t ring_degree)
{
    if (ring_degree == 0)
        throw std::invalid_argument("ring_degree must be greater than zero");

    std::vector<WideInt> wide_result(ring_degree, 0);
    for (size_t i = 0; i < lhs.size(); ++i) {
        for (size_t j = 0; j < rhs.size(); ++j) {
            const size_t degree = i + j;
            const WideInt value = static_cast<WideInt>(lhs[i]) * static_cast<WideInt>(rhs[j]);

            if (degree < ring_degree)
                wide_result[degree] += value;
            else
                wide_result[degree - ring_degree] -= value;
        }
    }

    Vec result(ring_degree, 0);
    for (size_t i = 0; i < ring_degree; ++i)
        result[i] = to_i64_checked(wide_result[i], "negacyclic_convolution");
    return result;
}

int64_t target_coefficient_dot(const Vec& query_poly, const Vec& train_poly)
{
    if (query_poly.size() != train_poly.size())
        throw std::runtime_error("target_coefficient_dot size mismatch");

    const size_t gamma = query_poly.size();
    WideInt coeff = 0;
    for (size_t j = 0; j < gamma; ++j)
        coeff += static_cast<WideInt>(query_poly[j]) *
                 static_cast<WideInt>(train_poly[gamma - 1 - j]);
    return to_i64_checked(coeff, "target_coefficient_dot");
}

int64_t polynomial_dot_single_block(const Vec& query, const Vec& train)
{
    return target_coefficient_dot(
            encode_query_polynomial(query),
            encode_reversed_train_polynomial(train));
}

int64_t polynomial_dot_chunked(
        const Vec& query,
        const Vec& train,
        size_t chunk_size,
        std::vector<size_t>* chunk_sizes = nullptr)
{
    if (chunk_size == 0)
        throw std::invalid_argument("chunk_size must be greater than zero");

    if (query.size() != train.size())
        throw std::runtime_error("polynomial_dot_chunked size mismatch");

    if (query.empty())
        return 0;

    WideInt total = 0;
    for (size_t begin = 0; begin < query.size(); begin += chunk_size) {
        const size_t end = std::min(begin + chunk_size, query.size());
        Vec query_block(query.begin() + begin, query.begin() + end);
        Vec train_block(train.begin() + begin, train.begin() + end);
        total += polynomial_dot_single_block(query_block, train_block);
        if (chunk_sizes)
            chunk_sizes->push_back(end - begin);
    }
    return to_i64_checked(total, "polynomial_dot_chunked");
}

int64_t squared_norm(const Vec& values)
{
    return to_i64_checked(ordinary_dot_wide(values, values), "squared_norm");
}

int64_t squared_distance(const Vec& query, const Vec& train)
{
    if (query.size() != train.size())
        throw std::runtime_error("squared_distance size mismatch");

    WideInt sum = 0;
    for (size_t i = 0; i < query.size(); ++i) {
        const WideInt diff =
                static_cast<WideInt>(query[i]) - static_cast<WideInt>(train[i]);
        sum += diff * diff;
    }
    return to_i64_checked(sum, "squared_distance");
}

int64_t polynomial_score_without_query_norm(
        const Vec& query,
        const Vec& train,
        size_t chunk_size)
{
    const WideInt score =
            static_cast<WideInt>(squared_norm(train)) -
            2 * static_cast<WideInt>(polynomial_dot_chunked(query, train, chunk_size));
    return to_i64_checked(score, "polynomial_score_without_query_norm");
}

std::vector<int64_t> ranking_indices_by_distance(
        const Vec& query,
        const std::vector<Vec>& trains)
{
    std::vector<std::pair<int64_t, int64_t>> keyed;
    keyed.reserve(trains.size());
    for (size_t i = 0; i < trains.size(); ++i)
        keyed.emplace_back(squared_distance(query, trains[i]),
                static_cast<int64_t>(i));

    std::sort(keyed.begin(), keyed.end());

    std::vector<int64_t> indices;
    indices.reserve(keyed.size());
    for (const auto& item : keyed)
        indices.push_back(item.second);
    return indices;
}

std::vector<int64_t> ranking_indices_by_polynomial_score(
        const Vec& query,
        const std::vector<Vec>& trains,
        size_t chunk_size)
{
    std::vector<std::pair<int64_t, int64_t>> keyed;
    keyed.reserve(trains.size());
    for (size_t i = 0; i < trains.size(); ++i)
        keyed.emplace_back(
                polynomial_score_without_query_norm(query, trains[i], chunk_size),
                static_cast<int64_t>(i));

    std::sort(keyed.begin(), keyed.end());

    std::vector<int64_t> indices;
    indices.reserve(keyed.size());
    for (const auto& item : keyed)
        indices.push_back(item.second);
    return indices;
}

void expect_equal(int64_t lhs, int64_t rhs, const std::string& label)
{
    if (lhs != rhs) {
        std::cerr << label << " mismatch: lhs=" << lhs << " rhs=" << rhs
                  << '\n';
        std::abort();
    }
}

void expect_equal(
        const std::vector<int64_t>& lhs,
        const std::vector<int64_t>& rhs,
        const std::string& label)
{
    if (lhs != rhs) {
        std::cerr << label << " ranking mismatch" << '\n';
        std::abort();
    }
}

Vec make_random_vector(
        size_t size,
        std::mt19937_64& rng,
        int64_t min_value,
        int64_t max_value)
{
    std::uniform_int_distribution<int64_t> dist(min_value, max_value);
    Vec values(size);
    for (auto& value : values)
        value = dist(rng);
    return values;
}

int64_t read_i64_checked(
        std::ifstream& file,
        const std::string& label,
        int row,
        int col = -1)
{
    int64_t value = 0;
    if (!(file >> value)) {
        std::string message = "failed to read " + label +
                              " at row=" + std::to_string(row);
        if (col >= 0)
            message += " col=" + std::to_string(col);
        throw std::runtime_error(message);
    }
    return value;
}

Dataset load_arcene_dataset(const std::string& root)
{
    Dataset data;

    {
        std::ifstream meta_file(root + "/Knn-meta");
        if (!meta_file)
            throw std::runtime_error("failed to open Arcene meta file");
        if (!(meta_file >> data.num_features >> data.num_train >> data.num_test))
            throw std::runtime_error("failed to parse Arcene meta file");
    }

    data.train_samples.assign(data.num_train, Vec(data.num_features));
    data.train_labels.assign(data.num_train, 0);
    data.test_samples.assign(data.num_test, Vec(data.num_features));
    data.test_labels.assign(data.num_test, 0);

    {
        std::ifstream train_file(root + "/P0-0-X-Train");
        if (!train_file)
            throw std::runtime_error("failed to open Arcene train feature file");
        for (int i = 0; i < data.num_train; ++i)
            for (int j = 0; j < data.num_features; ++j)
                data.train_samples[i][j] =
                        read_i64_checked(train_file, "Arcene train value", i, j);
    }

    {
        std::ifstream train_label_file(root + "/P0-0-Y-Train");
        if (!train_label_file)
            throw std::runtime_error("failed to open Arcene train label file");
        for (int i = 0; i < data.num_train; ++i)
            data.train_labels[i] =
                    read_i64_checked(train_label_file, "Arcene train label", i);
    }

    {
        std::ifstream test_file(root + "/P1-0-X-Test");
        if (!test_file)
            throw std::runtime_error("failed to open Arcene test feature file");
        for (int i = 0; i < data.num_test; ++i)
            for (int j = 0; j < data.num_features; ++j)
                data.test_samples[i][j] =
                        read_i64_checked(test_file, "Arcene test value", i, j);
    }

    {
        std::ifstream test_label_file(root + "/P1-0-Y-Test");
        if (!test_label_file)
            throw std::runtime_error("failed to open Arcene test label file");
        for (int i = 0; i < data.num_test; ++i)
            data.test_labels[i] =
                    read_i64_checked(test_label_file, "Arcene test label", i);
    }

    return data;
}

ArceneStats collect_arcene_stats(const Dataset& data)
{
    ArceneStats stats;

    for (const auto& train : data.train_samples) {
        for (const auto& value : train)
            stats.max_abs_feature = std::max(stats.max_abs_feature, abs_i64(value));
        stats.max_norm2 = std::max(stats.max_norm2, squared_norm(train));
    }

    for (const auto& test : data.test_samples) {
        for (const auto& value : test)
            stats.max_abs_feature = std::max(stats.max_abs_feature, abs_i64(value));

        for (const auto& train : data.train_samples) {
            const int64_t dot = ordinary_dot(test, train);
            const int64_t distance = squared_distance(test, train);
            const int64_t score =
                    polynomial_score_without_query_norm(test, train, 4096);

            stats.max_abs_dot = std::max(stats.max_abs_dot, abs_i64(dot));
            stats.max_distance = std::max(stats.max_distance, distance);
            stats.max_abs_score = std::max(stats.max_abs_score, abs_i64(score));
        }
    }

    return stats;
}

void run_manual_target_coefficient_test()
{
    const Vec query = {3, -2, 5};
    const Vec train = {7, 4, -1};

    expect_equal(polynomial_dot_single_block(query, train),
            ordinary_dot(query, train),
            "manual 3d polynomial dot");
}

void run_manual_negacyclic_convolution_test()
{
    const Vec query = {3, -2, 5};
    const Vec train = {7, 4, -1};
    const Vec query_poly = encode_query_polynomial(query);
    const Vec train_poly = encode_reversed_train_polynomial(train);
    const Vec product = negacyclic_convolution(query_poly, train_poly, 8);

    expect_equal(product[query.size() - 1],
            ordinary_dot(query, train),
            "manual negacyclic target coefficient");
}

void run_random_32d_test(std::mt19937_64& rng)
{
    const Vec query = make_random_vector(32, rng, -50, 50);
    const Vec train = make_random_vector(32, rng, -50, 50);

    expect_equal(polynomial_dot_single_block(query, train),
            ordinary_dot(query, train),
            "random 32d polynomial dot");
}

void run_single_block_4096d_test(std::mt19937_64& rng)
{
    const Vec query = make_random_vector(4096, rng, -3, 3);
    const Vec train = make_random_vector(4096, rng, -3, 3);

    expect_equal(polynomial_dot_single_block(query, train),
            ordinary_dot(query, train),
            "single block 4096d polynomial dot");
}

void run_chunked_10000d_test(std::mt19937_64& rng)
{
    const Vec query = make_random_vector(10000, rng, -3, 3);
    const Vec train = make_random_vector(10000, rng, -3, 3);

    std::vector<size_t> chunk_sizes;
    const int64_t chunked = polynomial_dot_chunked(query, train, 4096, &chunk_sizes);
    const int64_t ordinary = ordinary_dot(query, train);

    expect_equal(chunked, ordinary, "chunked 10000d polynomial dot");

    const std::vector<size_t> expected = {4096, 4096, 1808};
    if (chunk_sizes != expected) {
        std::cerr << "chunk sizes mismatch for 10000d test" << '\n';
        std::abort();
    }
}

void run_arcene_single_pair_test(const Dataset& data)
{
    if (data.num_test < 1 || data.num_train < 1)
        throw std::runtime_error("Arcene dataset too small for single-pair test");

    const Vec& query = data.test_samples[0];
    const Vec& train = data.train_samples[0];

    expect_equal(polynomial_dot_chunked(query, train, 4096),
            ordinary_dot(query, train),
            "arcene single pair polynomial dot");
}

void run_arcene_ranking_test(const Dataset& data)
{
    if (data.num_test < 1 || data.num_train != 199 || data.num_features != 10000)
        throw std::runtime_error("Arcene dataset shape mismatch for ranking test");

    const Vec& query = data.test_samples[0];

    for (const auto& train : data.train_samples) {
        expect_equal(polynomial_dot_chunked(query, train, 4096),
                ordinary_dot(query, train),
                "arcene train polynomial dot");
    }

    expect_equal(ranking_indices_by_polynomial_score(query, data.train_samples, 4096),
            ranking_indices_by_distance(query, data.train_samples),
            "arcene score vs distance");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::string dataset_root =
                argc >= 2 ? argv[1] : "Player-Data/Knn-Data/knn-1/arcene-data";

        std::mt19937_64 rng(20260731);

        run_manual_target_coefficient_test();
        run_manual_negacyclic_convolution_test();
        run_random_32d_test(rng);
        run_single_block_4096d_test(rng);
        run_chunked_10000d_test(rng);

        const Dataset arcene = load_arcene_dataset(dataset_root);
        run_arcene_single_pair_test(arcene);
        run_arcene_ranking_test(arcene);
        const ArceneStats stats = collect_arcene_stats(arcene);

        std::cout << "polynomial-distance-plain-test passed" << '\n';
        std::cout << "arcene_root=" << dataset_root << '\n';
        std::cout << "arcene_features=" << arcene.num_features
                  << " arcene_train=" << arcene.num_train
                  << " arcene_test=" << arcene.num_test << '\n';
        std::cout << "max_abs_feature=" << stats.max_abs_feature
                  << " max_norm2=" << stats.max_norm2
                  << " max_abs_dot=" << stats.max_abs_dot
                  << " max_distance=" << stats.max_distance
                  << " max_abs_score=" << stats.max_abs_score << '\n';
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "polynomial-distance-plain-test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
