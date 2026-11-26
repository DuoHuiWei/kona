#ifndef MACHINES_KONA_CUT_V3_HPP
#define MACHINES_KONA_CUT_V3_HPP

#include <charconv>
#include <sstream>
#include <string_view>
#include "Machines/kona-reduced-depth-cong-topk.hpp"

namespace KonaCutV3
{
using namespace KonaReducedDepthCongTopK;

inline int positive(std::string_view value)
{
    int result = 0;
    const auto end = value.data() + value.size();
    const auto parsed = std::from_chars(value.data(), end, result);
    if (parsed.ec != std::errc() || parsed.ptr != end || result <= 0)
        throw std::invalid_argument("expected positive integer: " + std::string(value));
    return result;
}

inline string layout_string(const vector<int>& layout)
{
    std::ostringstream out;
    for (size_t i = 0; i < layout.size(); ++i)
        out << (i ? "," : "") << layout[i];
    return out.str();
}

inline vector<int> explicit_layout(std::string_view value)
{
    vector<int> layout;
    for (;;)
    {
        const auto comma = value.find(',');
        layout.push_back(positive(value.substr(0, comma)));
        if (comma == value.npos)
            return layout;
        value.remove_prefix(comma + 1);
    }
}

inline vector<int> blocks_layout(int n, int blocks)
{
    if (blocks == 1)
        return {n};

    const int target = 1 + (n - 1) / blocks;
    const uint64_t length = uint64_t(1) << ceil_log2_positive(target);
    vector<int> layout(n / length, length);
    if (n % length)
        layout.push_back(n % length);
    if (layout.size() != size_t(blocks))
        throw std::invalid_argument("requested " + std::to_string(blocks) +
                " blocks, generated " + layout_string(layout) + "; use --block-sizes");
    return layout;
}

struct Plan
{
    PreparedReducedPlan blocks;
    TerminalPlan terminal;
};

inline Plan prepare(int n, int k, const vector<int>& layout)
{
    if (k <= 0 || k > n || layout.empty())
        throw std::invalid_argument("require 1 <= k <= n and a nonempty layout");

    int covered = 0;
    for (int length : layout)
    {
        if (length <= 0 || length > n - covered)
            throw std::invalid_argument("block sizes must be positive and sum to n");
        covered += length;
    }
    if (covered != n)
        throw std::invalid_argument("block sizes must sum to n");

    Plan result;
    auto& plan = result.blocks.plan;
    plan.n = n;
    plan.k = k;
    plan.outer_partition_cancelled = layout.size() == 1;
    vector<int> keep;
    int begin = 0;
    for (int length : layout)
    {
        append_cong_block(plan, begin, length);
        begin += length;
        keep.push_back(plan.blocks.back().keep);
        plan.matrix.candidate_count += keep.back();
    }
    plan.matrix.output_k = k;
    prepare_block_levels(result.blocks);
    result.terminal = prepare_terminal(result.blocks, Terminal::Pruned);
    return result;
}
}
#endif
