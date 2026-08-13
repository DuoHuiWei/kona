#ifndef MACHINES_KONA_CONG_TOPK_HPP_
#define MACHINES_KONA_CONG_TOPK_HPP_

#include <algorithm>
#include <array>
#include <cassert>
#include <stdexcept>
#include <vector>

namespace KonaCongTopK
{

typedef std::array<int, 2> ComparePair;
typedef std::vector<ComparePair> CongLevel;

inline std::vector<CongLevel> parallel_levels(
        const std::vector<CongLevel>& levels_a,
        const std::vector<CongLevel>& levels_b)
{
    size_t depth = std::max(levels_a.size(), levels_b.size());
    std::vector<CongLevel> result(depth);
    for (size_t i = 0; i < depth; i++)
    {
        if (i < levels_a.size())
            result[i].insert(result[i].end(), levels_a[i].begin(), levels_a[i].end());
        if (i < levels_b.size())
            result[i].insert(result[i].end(), levels_b[i].begin(), levels_b[i].end());
    }
    return result;
}

inline std::vector<int> interleave(
        const std::vector<int>& a,
        const std::vector<int>& b)
{
    std::vector<int> result;
    size_t length = std::max(a.size(), b.size());
    result.reserve(a.size() + b.size());
    for (size_t i = 0; i < length; i++)
    {
        if (i < a.size())
            result.push_back(a[i]);
        if (i < b.size())
            result.push_back(b[i]);
    }
    return result;
}

inline int chunk_size(int n, int k)
{
    int power_of_two = 1;
    while (power_of_two < k)
        power_of_two <<= 1;

    if (n <= power_of_two)
        return (n + 1) / 2;

    return power_of_two * ((n + 2 * power_of_two - 1) / (2 * power_of_two));
}

inline std::pair<std::vector<int>, std::vector<CongLevel>> build_merge_network(
        const std::vector<int>& left_wires,
        const std::vector<int>& right_wires,
        int k)
{
    if (k <= 0)
        return std::make_pair(std::vector<int>(), std::vector<CongLevel>());

    if (left_wires.empty() || right_wires.empty())
    {
        std::vector<int> output = left_wires;
        output.insert(output.end(), right_wires.begin(), right_wires.end());
        if ((int)output.size() > k)
            output.resize(k);
        return std::make_pair(output, std::vector<CongLevel>());
    }

    if (left_wires.size() == 1 && right_wires.size() == 1)
    {
        std::vector<int> output;
        output.push_back(left_wires[0]);
        output.push_back(right_wires[0]);
        if ((int)output.size() > k)
            output.resize(k);
        std::vector<CongLevel> levels(1);
        levels[0].push_back({left_wires[0], right_wires[0]});
        return std::make_pair(output, levels);
    }

    std::vector<int> left_even, left_odd, right_even, right_odd;
    for (size_t i = 0; i < left_wires.size(); i += 2)
        left_even.push_back(left_wires[i]);
    for (size_t i = 1; i < left_wires.size(); i += 2)
        left_odd.push_back(left_wires[i]);
    for (size_t i = 0; i < right_wires.size(); i += 2)
        right_even.push_back(right_wires[i]);
    for (size_t i = 1; i < right_wires.size(); i += 2)
        right_odd.push_back(right_wires[i]);

    std::pair<std::vector<int>, std::vector<CongLevel>> even_part =
            build_merge_network(left_even, right_even, k / 2 + 1);
    std::pair<std::vector<int>, std::vector<CongLevel>> odd_part =
            build_merge_network(left_odd, right_odd, k / 2);

    std::vector<CongLevel> levels =
            parallel_levels(even_part.second, odd_part.second);
    std::vector<int> merged_wires = interleave(even_part.first, odd_part.first);

    CongLevel final_level;
    int pair_count = ((int)merged_wires.size() - 1) / 2;
    for (int i = 1; i <= pair_count; i++)
        final_level.push_back({merged_wires[2 * i - 1], merged_wires[2 * i]});
    if (!final_level.empty())
        levels.push_back(final_level);

    if ((int)merged_wires.size() > k)
        merged_wires.resize(k);
    return std::make_pair(merged_wires, levels);
}

inline std::pair<std::vector<int>, std::vector<CongLevel>> build_sort_network(
        const std::vector<int>& wires,
        int k)
{
    int n = (int)wires.size();
    if (n == 1)
        return std::make_pair(wires, std::vector<CongLevel>());

    int split = chunk_size(n, k);
    std::vector<int> left(wires.begin(), wires.begin() + split);
    std::vector<int> right(wires.begin() + split, wires.end());

    std::pair<std::vector<int>, std::vector<CongLevel>> left_part =
            build_sort_network(left, k);
    std::pair<std::vector<int>, std::vector<CongLevel>> right_part =
            build_sort_network(right, k);

    std::vector<CongLevel> levels =
            parallel_levels(left_part.second, right_part.second);
    std::pair<std::vector<int>, std::vector<CongLevel>> merged =
            build_merge_network(left_part.first, right_part.first, k);
    levels.insert(levels.end(), merged.second.begin(), merged.second.end());
    return std::make_pair(merged.first, levels);
}

struct CongNetwork
{
    std::vector<CongLevel> levels;
    std::vector<int> output_wires;
};

inline CongNetwork build_cong_network(int n, int k)
{
    if (n <= 0)
        throw std::invalid_argument("n must be positive");
    if (k <= 0 || k > n)
        throw std::invalid_argument("k must satisfy 1 <= k <= n");

    std::vector<int> wires(n);
    for (int i = 0; i < n; i++)
        wires[i] = i;

    std::pair<std::vector<int>, std::vector<CongLevel>> result =
            build_sort_network(wires, k);

    std::vector<CongLevel> filtered;
    for (size_t depth = 0; depth < result.second.size(); depth++)
    {
        if (result.second[depth].empty())
            continue;

        std::vector<int> used;
        for (size_t j = 0; j < result.second[depth].size(); j++)
        {
            int left = result.second[depth][j][0];
            int right = result.second[depth][j][1];
            if (left == right)
                throw std::runtime_error("wire conflict: self compare");
            if (std::find(used.begin(), used.end(), left) != used.end() ||
                    std::find(used.begin(), used.end(), right) != used.end())
                throw std::runtime_error("wire conflict in same level");
            used.push_back(left);
            used.push_back(right);
        }
        filtered.push_back(result.second[depth]);
    }

    CongNetwork network;
    network.levels = filtered;
    network.output_wires = result.first;
    return network;
}

inline std::vector<int> flatten_level(const CongLevel& level)
{
    std::vector<int> compare_idx_vec;
    compare_idx_vec.reserve(level.size() * 2);
    for (size_t i = 0; i < level.size(); i++)
    {
        compare_idx_vec.push_back(level[i][0]);
        compare_idx_vec.push_back(level[i][1]);
    }
    return compare_idx_vec;
}

template<class T>
inline void move_output_wires_to_tail(std::vector<T>& shares,
        const std::vector<int>& output_wires)
{
    std::vector<T> original = shares;
    const int n = (int)shares.size();
    const int k = (int)output_wires.size();
    std::vector<int> is_output(n, 0);
    for (int i = 0; i < k; i++)
        is_output[output_wires[i]] = 1;

    int write_pos = 0;
    for (int i = 0; i < n; i++)
        if (!is_output[i])
            shares[write_pos++] = original[i];

    for (int i = 0; i < k; i++)
        shares[n - k + i] = original[output_wires[i]];
}

template<class T, class CompareLevelFn, class SwapLevelFn>
inline void cong_top_k(
        std::vector<T>& shares,
        int n,
        int k,
        CompareLevelFn compare_level_fn,
        SwapLevelFn swap_level_fn)
{
    assert((int)shares.size() == n);
    CongNetwork network = build_cong_network(n, k);
    for (size_t depth = 0; depth < network.levels.size(); depth++)
    {
        std::vector<int> compare_idx_vec = flatten_level(network.levels[depth]);
        compare_level_fn(shares, compare_idx_vec);
        swap_level_fn(shares, compare_idx_vec);
    }
    move_output_wires_to_tail(shares, network.output_wires);
}

} // namespace KonaCongTopK

#endif /* MACHINES_KONA_CONG_TOPK_HPP_ */
