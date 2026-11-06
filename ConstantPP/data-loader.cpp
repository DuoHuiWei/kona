#include "data-loader.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

namespace ConstantPP
{
namespace
{

std::string join_path(
        const std::string& base,
        const std::string& leaf)
{
    if (base.empty())
        return leaf;
    if (base.back() == '/')
        return base + leaf;
    return base + "/" + leaf;
}

std::ifstream open_input(
        const std::string& path)
{
    std::ifstream in(path.c_str());
    if (!in.good())
        throw std::runtime_error("ConstantPP dataset loader: missing file " + path);
    return in;
}

std::uint64_t read_u64(
        std::istream& in,
        const std::string& what)
{
    long long value = 0;
    if (!(in >> value))
        throw std::runtime_error("ConstantPP dataset loader: failed reading " + what);
    if (value < 0)
        throw std::runtime_error("ConstantPP dataset loader: negative value in " + what);
    return static_cast<std::uint64_t>(value);
}

} // namespace

ConstantPPDataset load_constantpp_dataset(
        const std::string& dataset_path)
{
    ConstantPPDataset dataset;

    auto meta = open_input(join_path(dataset_path, "Knn-meta"));
    std::size_t num_train = 0;
    std::size_t num_test = 0;
    if (!(meta >> dataset.num_features >> num_train >> num_test))
        throw std::runtime_error("ConstantPP dataset loader: invalid Knn-meta");
    if (dataset.num_features == 0 || num_train == 0 || num_test == 0)
        throw std::runtime_error("ConstantPP dataset loader: empty dataset metadata");

    dataset.database.assign(
            num_train,
            std::vector<std::uint64_t>(dataset.num_features));
    auto train_x = open_input(join_path(dataset_path, "P0-0-X-Train"));
    for (std::size_t i = 0; i < num_train; ++i)
        for (std::size_t j = 0; j < dataset.num_features; ++j)
            dataset.database[i][j] = read_u64(train_x, "P0-0-X-Train");

    dataset.labels.resize(num_train);
    auto train_y = open_input(join_path(dataset_path, "P0-0-Y-Train"));
    for (std::size_t i = 0; i < num_train; ++i)
        dataset.labels[i] = read_u64(train_y, "P0-0-Y-Train");

    dataset.query.resize(dataset.num_features);
    auto test_x = open_input(join_path(dataset_path, "P1-0-X-Test"));
    for (std::size_t j = 0; j < dataset.num_features; ++j)
        dataset.query[j] = read_u64(test_x, "P1-0-X-Test");

    auto test_y = open_input(join_path(dataset_path, "P1-0-Y-Test"));
    dataset.query_label = read_u64(test_y, "P1-0-Y-Test");

    return dataset;
}

} // namespace ConstantPP
