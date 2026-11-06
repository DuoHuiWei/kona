#ifndef MACHINES_KONA_ORDERED_BLOCK_TERMINAL_NETWORKS_HPP_
#define MACHINES_KONA_ORDERED_BLOCK_TERMINAL_NETWORKS_HPP_

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Machines/kona-cong-topk.hpp"

namespace KonaOrderedBlockTerminal
{

struct Comparator
{
    uint32_t left = 0;
    uint32_t right = 0;
};

using ComparatorLayer = std::vector<Comparator>;

struct TerminalNetwork
{
    std::string name;
    uint32_t input_count = 0;
    uint32_t output_count = 0;
    std::vector<uint32_t> output_lanes;
    std::vector<ComparatorLayer> layers;

    uint64_t comparator_count() const
    {
        uint64_t result = 0;
        for (const auto& layer : layers)
            result += layer.size();
        return result;
    }

    uint32_t depth() const
    {
        return static_cast<uint32_t>(layers.size());
    }
};

inline uint32_t ceil_log2_size(size_t n)
{
    uint32_t result = 0;
    size_t value = 1;
    while (value < n)
    {
        value <<= 1;
        ++result;
    }
    return result;
}

inline uint32_t popcount_size(size_t value)
{
    uint32_t result = 0;
    while (value)
    {
        result += value & 1;
        value >>= 1;
    }
    return result;
}

inline void validate_network(const TerminalNetwork& network)
{
    for (size_t d = 0; d < network.layers.size(); ++d)
    {
        std::vector<unsigned char> used(network.input_count, 0);
        for (const auto& c : network.layers[d])
        {
            if (c.left == c.right || c.left >= network.input_count ||
                    c.right >= network.input_count)
                throw std::runtime_error("invalid comparator in " +
                        network.name + " layer " + std::to_string(d));
            if (used[c.left] || used[c.right])
                throw std::runtime_error("wire conflict in " + network.name +
                        " layer " + std::to_string(d));
            used[c.left] = used[c.right] = 1;
        }
    }
    for (auto lane : network.output_lanes)
        if (lane >= network.input_count)
            throw std::runtime_error("invalid output lane in " + network.name);
}

inline std::vector<ComparatorLayer> splitter_layers(
        const std::vector<uint32_t>& lanes)
{
    std::vector<ComparatorLayer> result;
    if (lanes.size() < 2)
        return result;
    const uint32_t layer_count = ceil_log2_size(lanes.size());
    for (uint32_t i = 0; i < layer_count; ++i)
    {
        const size_t stride = size_t(1) << (layer_count - i - 1);
        ComparatorLayer layer;
        for (size_t offset = 0; offset < lanes.size(); offset += 2 * stride)
        {
            const size_t stop = std::min(offset + stride,
                    lanes.size() > stride ? lanes.size() - stride : size_t(0));
            for (size_t j = offset; j < stop; ++j)
                layer.push_back({lanes[j], lanes[j + stride]});
        }
        result.push_back(std::move(layer));
    }
    return result;
}

inline TerminalNetwork build_octm(
        const std::vector<size_t>& block_sizes, size_t k)
{
    TerminalNetwork network;
    network.name = "OCTM-Graph";
    network.output_count = static_cast<uint32_t>(k);
    std::vector<std::vector<int>> chains;
    uint32_t cursor = 0;
    for (size_t size : block_sizes)
    {
        std::vector<int> chain;
        for (size_t j = 0; j < size; ++j)
            chain.push_back(cursor++);
        chains.push_back(std::move(chain));
    }
    network.input_count = cursor;

    while (chains.size() > 1)
    {
        std::vector<std::vector<int>> next;
        std::vector<ComparatorLayer> tree_level;
        for (size_t i = 0; i + 1 < chains.size(); i += 2)
        {
            auto merged = KonaCongTopK::build_merge_network(
                    chains[i], chains[i + 1], static_cast<int>(k));
            if (tree_level.size() < merged.second.size())
                tree_level.resize(merged.second.size());
            for (size_t d = 0; d < merged.second.size(); ++d)
                for (const auto& c : merged.second[d])
                    tree_level[d].push_back({
                            static_cast<uint32_t>(c[0]),
                            static_cast<uint32_t>(c[1])});
            next.push_back(std::move(merged.first));
        }
        if (chains.size() % 2)
            next.push_back(std::move(chains.back()));
        for (auto& layer : tree_level)
            if (!layer.empty())
                network.layers.push_back(std::move(layer));
        chains = std::move(next);
    }
    if (chains.empty() || chains[0].size() != k)
        throw std::runtime_error("OCTM output count mismatch");
    for (int lane : chains[0])
        network.output_lanes.push_back(static_cast<uint32_t>(lane));
    validate_network(network);
    return network;
}

inline TerminalNetwork build_obssn(
        const std::vector<size_t>& block_sizes, size_t k)
{
    TerminalNetwork network;
    network.name = "OB-SSN";
    std::vector<size_t> min_rank;
    for (size_t size : block_sizes)
        for (size_t j = 0; j < size; ++j)
            min_rank.push_back(j);
    network.input_count = static_cast<uint32_t>(min_rank.size());
    network.output_count = static_cast<uint32_t>(k);
    std::vector<size_t> next_layer(min_rank.size(), 0);

    int rank_end = -1;
    while (true)
    {
        bool added = false;
        for (int rank = static_cast<int>(k) - 1; rank > rank_end; --rank)
        {
            std::vector<uint32_t> selected;
            for (uint32_t lane = 0; lane < min_rank.size(); ++lane)
                if (min_rank[lane] == static_cast<size_t>(rank))
                    selected.push_back(lane);
            if (selected.size() < 2)
                continue;

            auto extension = splitter_layers(selected);
            size_t offset = 0;
            for (auto lane : selected)
                offset = std::max(offset, next_layer[lane]);
            if (network.layers.size() < offset + extension.size())
                network.layers.resize(offset + extension.size());
            for (size_t d = 0; d < extension.size(); ++d)
                network.layers[offset + d].insert(
                        network.layers[offset + d].end(),
                        extension[d].begin(), extension[d].end());
            offset += extension.size();
            for (size_t local = 0; local < selected.size(); ++local)
            {
                min_rank[selected[local]] +=
                        (size_t(1) << popcount_size(local)) - 1;
                next_layer[selected[local]] = offset;
            }
            added = true;
        }
        ++rank_end;
        if (!added)
            break;
    }
    for (uint32_t lane = 0; lane < min_rank.size(); ++lane)
        if (min_rank[lane] < k)
            network.output_lanes.push_back(lane);
    if (network.output_lanes.size() != k)
        throw std::runtime_error("OB-SSN output count mismatch: " +
                std::to_string(network.output_lanes.size()));
    while (!network.layers.empty() && network.layers.back().empty())
        network.layers.pop_back();
    validate_network(network);
    return network;
}

} // namespace KonaOrderedBlockTerminal

#endif
