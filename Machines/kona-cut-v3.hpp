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

inline Plan prepare(NetworkPlan network)
{
    Plan result;
    result.blocks.plan = std::move(network);
    prepare_block_levels(result.blocks);
    result.terminal = prepare_terminal(result.blocks, Terminal::Pruned);
    return result;
}

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

    NetworkPlan plan;
    plan.n = n;
    plan.k = k;
    plan.outer_partition_cancelled = layout.size() == 1;
    int begin = 0;
    for (int length : layout)
    {
        append_cong_block(plan, begin, length);
        begin += length;
        plan.matrix.candidate_count += plan.blocks.back().keep;
    }
    plan.matrix.output_k = k;
    return prepare(std::move(plan));
}
// The caller owns outer timers. These hooks wrap the same protocol calls for
// either layout, with no logging, synchronization or result opening here.
template<class Phase, class Batch, class Local>
Output<64> execute(vector<SharePair<64>>& work, const Plan& plan,
        const string& profile, const Policy& policy, KonaDcfCompare::Compare64<64>& dcf,
        RealTwoPartyPlayer* player, Phase phase, Batch batch, Local local)
{
    size_t depth = 0;
    auto observe_block = [&](const char*, size_t layer, size_t, auto operation) {
        depth = layer;
        operation();
    };
    auto split_layer = [&](auto& shares, const auto& indices, const auto&, const auto&,
            auto&, auto*) {
        const size_t width = indices.size() / 2;
        vector<Z2<64>> bits(indices.size());
        const auto backend = policy.choose(width, profile);
        batch("block_compare", depth, width, backend, [&] {
            KonaReducedDepthHybrid::compare_pairs<64>(
                    shares, indices, bits, true, profile, policy, dcf, player);
        });
        batch("scs", depth, width, Backend::AUTO, [&] {
            KonaCongKonaAdapter::ss_vec_kona_l2<64>(shares, indices, bits, player);
        });
        return backend;
    };
    phase("block_cong", [&] {
        execute_blocks<64>(work, plan.blocks, profile, policy, dcf, player,
                observe_block, split_layer);
    });

    Output<64> output;
    phase("terminal", [&] {
        vector<SharePair<64>> candidates;
        local("candidate_collect", [&] {
            candidates = collect_block_candidates<64>(work, plan.blocks.plan,
                    plan.blocks.block_offsets);
        });
        const char* name = nullptr;
        size_t width = 0, index = 0;
        auto observe_terminal = [&](const char* step, size_t layer, size_t count, auto operation) {
            name = std::string_view(step) == "rank_mask" ? "rank_mask" : "matrix_compare";
            width = count;
            index = layer;
            operation();
        };
        auto compare = [&](auto operation) {
            auto backend = std::string_view(name) == "rank_mask" ?
                    policy.choose(width, profile) : policy.choose_matrix(width, profile);
            batch(name, index, width, backend, [&] { operation(); });
            return backend;
        };
        output = execute_terminal<64>(std::move(candidates), plan.terminal,
                plan.blocks.plan.k, profile, policy, dcf, player, observe_terminal, compare, local);
    });
    return output;
}

struct BatchShape
{
    uint64_t bytes, logical, exchanges, dcf_calls;
};

inline BatchShape batch_shape(size_t width, Backend backend)
{
    if (backend == Backend::AUTO)
        return {64 * width, 1, KonaBenchConfig::chunks(2 * width, KonaBenchConfig::mul_chunk()), 0};
    if (backend == Backend::PCR)
        return {70 * width, 8, 7 + KonaBenchConfig::chunks(width, KonaBenchConfig::mul_chunk()), 0};
    return {8 * width, 1, KonaBenchConfig::chunks(width, KonaBenchConfig::dcf_chunk()), 2 * width};
}

}
#endif
