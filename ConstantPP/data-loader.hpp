#ifndef CONSTANTPP_DATA_LOADER_HPP_
#define CONSTANTPP_DATA_LOADER_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ConstantPP
{

struct ConstantPPDataset
{
    std::size_t num_features = 0;
    std::vector<std::vector<std::uint64_t>> database;
    std::vector<std::uint64_t> labels;
    std::vector<std::uint64_t> query;
    std::uint64_t query_label = 0;
};

ConstantPPDataset load_constantpp_dataset(
        const std::string& dataset_path);

} // namespace ConstantPP

#endif // CONSTANTPP_DATA_LOADER_HPP_
