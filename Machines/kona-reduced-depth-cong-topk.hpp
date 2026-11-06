#ifndef MACHINES_KONA_REDUCED_DEPTH_CONG_TOPK_HPP_
#define MACHINES_KONA_REDUCED_DEPTH_CONG_TOPK_HPP_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Machines/kona-cong-topk.hpp"
#include "Machines/kona-reduced-depth-hybrid-adapter.hpp"
#include "Machines/kona-ordered-block-terminal-networks.hpp"

// Unified planning and execution module for block Cong + matrix Top-k.
// The frozen "standard partition + matrix finalization" rule is unchanged.
//
// Design intent:
//   1) Partition N into several power-of-two blocks.
//   2) Run the existing Cong Top-k network independently on every block.
//      All blocks are independent and therefore their Cong levels are executed
//      in parallel; the block phase depth is max(block_depth).
//   3) Collect K survivors from every block.
//   4) Finalize globally with an all-pairs comparison matrix over B*K candidates.
//
// IMPORTANT:
// The matrix finalization is NOT represented as KonaCongTopK::CongLevel.
// In a compare-swap network a wire may appear at most once in one level, while
// an all-pairs comparison matrix intentionally compares one candidate against
// many other candidates in the same logical comparison stage. The secure
// backend should therefore evaluate all matrix comparisons from the immutable
// candidate snapshot, accumulate ranks, and then select global Top-k.
//
// Frozen construction rule:
//   a     = ceil(log2(N))
//   rho   = ceil(log2(N / K))
//   h     = max(1, floor((rho - 6) / 2))
//   Lmax  = 2^(a-h)
//   q     = floor(N / Lmax)
//   r     = N - q*Lmax
//   body  = q blocks of exactly Lmax
//   tail  = one block of exactly r real items, iff r > 0
//
// Before partitioning, cancel the outer partition iff 4*K > Lmax, using the
// candidate Lmax above (never the tail or N). Equality still permits splitting.
// Cancellation uses the unchanged original Cong(N,K), including its internal
// recursion, and bypasses all cross-block terminal matrix/rank-mask work.
// Otherwise power-of-two sizing is a
// full-block optimization preference, not a legality requirement for the whole
// network: the tail prioritizes avoiding dummy comparators over preserving a
// power-of-two block shape.

namespace KonaReducedDepthCongTopK
{

using ComparePair = KonaCongTopK::ComparePair;
using CongLevel = KonaCongTopK::CongLevel;
using CongNetwork = KonaCongTopK::CongNetwork;

inline int ceil_log2_positive(std::uint64_t x)
{
    if (x == 0)
        throw std::invalid_argument("ceil_log2_positive requires x > 0");

    int p = 0;
    std::uint64_t v = 1;
    while (v < x)
    {
        if (v > (std::numeric_limits<std::uint64_t>::max() >> 1))
            throw std::overflow_error("ceil_log2_positive overflow");
        v <<= 1;
        ++p;
    }
    return p;
}

inline int ceil_log2_ratio(int n, int k)
{
    if (n <= 0 || k <= 0)
        throw std::invalid_argument("n and k must be positive");

    // ceil(log2(n/k)) computed without floating point:
    // smallest rho such that k*2^rho >= n.
    int rho = 0;
    std::uint64_t cap = static_cast<std::uint64_t>(k);
    while (cap < static_cast<std::uint64_t>(n))
    {
        cap <<= 1;
        ++rho;
    }
    return rho;
}

inline int pow2_ceil(int x)
{
    if (x <= 0)
        throw std::invalid_argument("pow2_ceil requires x > 0");

    int p = 1;
    while (p < x)
    {
        if (p > (std::numeric_limits<int>::max() >> 1))
            throw std::overflow_error("pow2_ceil overflow");
        p <<= 1;
    }
    return p;
}

struct BlockPlan
{
    int real_begin = 0;     // inclusive position in original input
    int real_size = 0;      // number of real items
    int padded_size = 0;    // network size; full blocks use power-of-two Lmax
    int keep = 0;           // min(K, real_size)

    // Cong network is built for padded_size and K' = min(K, padded_size).
    // For full blocks padded_size == real_size == Lmax. The final tail block
    // deliberately keeps padded_size == real_size and uses the original Cong
    // arbitrary-size Sort(real_size,K), avoiding dummy comparator overhead.
    CongNetwork network;
};

struct MatrixFinalizationPlan
{
    int candidate_count = 0;
    int output_k = 0;

    // All unordered pairs (i,j), 0 <= i < j < candidate_count.
    // These comparisons may all be evaluated from one immutable candidate
    // snapshot. They are NOT compare-swap pairs and may share candidate indices.
    std::vector<ComparePair> all_pairs;

    // Candidate metadata in collection order. position is one-based within the
    // sorted survivors of its source block.
    std::vector<int> candidate_block;
    std::vector<int> candidate_position;

    // Pruned cross-block matrix:
    // compare candidates from different blocks only when u + v <= K + 1.
    std::vector<ComparePair> pruned_cross_pairs;
};

struct NetworkPlan
{
    int n = 0;
    int k = 0;

    int a = 0;
    int rho = 0;
    int h = 0;
    int lmax = 0;
    // lmax always records the original formula's candidate main-block size,
    // even when the actual single block has N records. q/r describe the actual
    // chosen layout (1/0 when outer_partition_cancelled).
    bool outer_partition_cancelled = false;

    int q = 0;
    int r = 0;
    int padding = 0;

    std::vector<BlockPlan> blocks;
    MatrixFinalizationPlan matrix;

    std::size_t block_comparators = 0;
    std::size_t matrix_comparators = 0;
    std::size_t total_comparators = 0;

    // Structural comparison depth:
    //   max Cong block depth + one matrix-comparison stage.
    // A later rank/selection implementation may add local or interactive depth;
    // keep that backend cost separate from this structural metric.
    std::size_t block_depth = 0;
    std::size_t structural_depth = 0;
};

inline std::size_t comparator_count(const CongNetwork& network)
{
    std::size_t c = 0;
    for (const auto& level : network.levels)
        c += level.size();
    return c;
}

// Standalone terminal planning from public block output lengths. No Cong or
// secret values are needed. This is the same full/pruned rule used below.
inline MatrixFinalizationPlan build_matrix_finalization_plan(
        const std::vector<int>& block_keep, int k)
{
    if (block_keep.empty() || k <= 0) throw std::invalid_argument("invalid matrix layout");
    for (int keep : block_keep)
        if (keep <= 0 || keep > k) throw std::invalid_argument("invalid block keep");
    MatrixFinalizationPlan matrix;
    int candidates = 0;
    for (size_t b = 0; b < block_keep.size(); ++b)
    {
        for (int u = 1; u <= block_keep[b]; ++u)
        {
            matrix.candidate_block.push_back(static_cast<int>(b));
            matrix.candidate_position.push_back(u);
            ++candidates;
        }
    }

    matrix.candidate_count = candidates;
    matrix.output_k = k;

    matrix.all_pairs.reserve(
        static_cast<std::size_t>(candidates) *
        static_cast<std::size_t>(candidates > 0 ? candidates - 1 : 0) / 2);

    for (int i = 0; i < candidates; ++i)
        for (int j = i + 1; j < candidates; ++j)
        {
            matrix.all_pairs.push_back({i, j});
            if (matrix.candidate_block[i] !=
                    matrix.candidate_block[j] &&
                    matrix.candidate_position[i] +
                            matrix.candidate_position[j] <= k + 1)
                matrix.pruned_cross_pairs.push_back({i, j});
        }

    if (matrix.candidate_count < k) throw std::invalid_argument("not enough candidates");
    return matrix;
}

inline bool should_cancel_outer_partition(int k, int candidate_lmax)
{
    if (k <= 0 || candidate_lmax <= 0)
        throw std::invalid_argument("partition cutoff requires positive k and Lmax");
    // Promote BEFORE multiplication: int inputs cannot overflow uint64_t * 4.
    return std::uint64_t(k) * 4 > std::uint64_t(candidate_lmax);
}

inline NetworkPlan build_reduced_depth_cong_plan(int n, int k)
{
    if (n <= 0)
        throw std::invalid_argument("n must be positive");
    if (k <= 0 || k > n)
        throw std::invalid_argument("k must satisfy 1 <= k <= n");

    NetworkPlan plan;
    plan.n = n;
    plan.k = k;

    plan.a = ceil_log2_positive(static_cast<std::uint64_t>(n));
    plan.rho = ceil_log2_ratio(n, k);

    // floor((rho-6)/2) must be handled explicitly for negative odd values.
    int floor_half = 0;
    if (plan.rho >= 6)
        floor_half = (plan.rho - 6) / 2;
    else
        floor_half = -((6 - plan.rho + 1) / 2);

    plan.h = std::max(1, floor_half);

    const int exponent = plan.a - plan.h;
    if (exponent < 0)
        throw std::runtime_error(
            "frozen rule produced Lmax < 1; unsupported parameter combination");

    if (exponent >= 30)
        throw std::overflow_error("Lmax exceeds 32-bit int range");

    plan.lmax = 1 << exponent;

    if (should_cancel_outer_partition(k, plan.lmax))
    {
        plan.outer_partition_cancelled = true;
        plan.q = 1;
        plan.r = 0;
        BlockPlan block;
        block.real_size = block.padded_size = n;
        block.keep = k;
        block.network = KonaCongTopK::build_cong_network(n, k);
        plan.block_comparators = comparator_count(block.network);
        plan.block_depth = block.network.levels.size();
        plan.blocks.push_back(std::move(block));
        // Only output metadata: no quadratic matrix/index construction.
        plan.matrix.candidate_count = plan.matrix.output_k = k;
        for (int i = 0; i < k; ++i) {
            plan.matrix.candidate_block.push_back(0);
            plan.matrix.candidate_position.push_back(i + 1);
        }
        plan.total_comparators = plan.block_comparators;
        plan.structural_depth = plan.block_depth;
        return plan;
    }

    // Retain the existing partition and short-tail rules verbatim.
    plan.q = n / plan.lmax;
    plan.r = n - plan.q * plan.lmax;

    int cursor = 0;

    for (int i = 0; i < plan.q; ++i)
    {
        BlockPlan block;
        block.real_begin = cursor;
        block.real_size = plan.lmax;
        block.padded_size = plan.lmax;
        block.keep = std::min(k, block.real_size);

        const int network_k = std::min(k, block.padded_size);
        block.network =
            KonaCongTopK::build_cong_network(block.padded_size, network_k);

        plan.block_comparators += comparator_count(block.network);
        plan.block_depth = std::max(
            plan.block_depth, block.network.levels.size());

        plan.blocks.push_back(std::move(block));
        cursor += plan.lmax;
    }

    if (plan.r > 0)
    {
        BlockPlan tail;
        tail.real_begin = cursor;
        tail.real_size = plan.r;
        tail.padded_size = tail.real_size;
        tail.keep = std::min(k, tail.real_size);

        const int network_k = std::min(k, tail.real_size);
        tail.network =
            KonaCongTopK::build_cong_network(tail.real_size, network_k);

        plan.block_comparators += comparator_count(tail.network);
        plan.block_depth = std::max(
            plan.block_depth, tail.network.levels.size());

        plan.blocks.push_back(std::move(tail));
        cursor += plan.r;
    }

    if (cursor != n)
        throw std::runtime_error("partition did not cover exactly n real items");

    std::vector<int> block_keep;
    for (const auto& block : plan.blocks) block_keep.push_back(block.keep);
    plan.matrix = build_matrix_finalization_plan(block_keep, k);
    const int candidates = plan.matrix.candidate_count;

    plan.matrix_comparators = plan.matrix.all_pairs.size();
    plan.total_comparators =
        plan.block_comparators + plan.matrix_comparators;

    plan.structural_depth =
        plan.block_depth + (candidates > 1 ? 1u : 0u);

    return plan;
}

// Execution API: same fixed plans and existing PCR/DCF/swap operations.
// No input synthesis, network initialization, timers, logs or reveal here.
// prepare_reduced_plan -> prepare_terminal(Full/Pruned) -> execute_topk
// executes block Cong, candidate collection, matrix ranking and rank-mask.
// The individual stages remain callable for terminal-only measurements.

using std::vector;
using std::string;
using std::size_t;
using std::runtime_error;
template<int K> using SharePair = std::array<Z2<K>, 2>;
using Backend = KonaReducedDepthHybrid::Backend;
using Policy = KonaReducedDepthHybrid::Policy;
struct PreparedReducedPlan
{
    KonaReducedDepthCongTopK::NetworkPlan plan;
    vector<vector<int>> fused_block_levels;
    vector<int> block_offsets;
    vector<int> candidate_block;
    vector<int> candidate_pos;
    vector<int> initial_rank;
    vector<int> full_matrix_idx;
    vector<int> pruned_matrix_idx;
    vector<int> reverse_two_block_idx;
    vector<int> reverse_prefilter_idx;
    vector<int> reverse_prefilter_matrix_idx;
    size_t full_matrix_comparators = 0;
    size_t pruned_matrix_comparators = 0;
    size_t reverse_prefilter_comparators = 0;
    size_t reverse_prefilter_candidate_count = 0;
    size_t reverse_prefilter_matrix_comparators = 0;
    KonaOrderedBlockTerminal::TerminalNetwork obssn;
    KonaOrderedBlockTerminal::TerminalNetwork octm;
};

inline PreparedReducedPlan prepare_reduced_plan(int n, int k)
{
    PreparedReducedPlan prepared;
    prepared.plan =
            KonaReducedDepthCongTopK::build_reduced_depth_cong_plan(n, k);

    int offset = 0;
    for (const auto& block : prepared.plan.blocks)
    {
        prepared.block_offsets.push_back(offset);
        offset += block.padded_size;
    }
    prepared.candidate_block = prepared.plan.matrix.candidate_block;
    prepared.candidate_pos = prepared.plan.matrix.candidate_position;
    prepared.initial_rank = prepared.candidate_pos;
    for (auto& x : prepared.initial_rank)
        --x;

    prepared.fused_block_levels.resize(prepared.plan.block_depth);
    for (size_t depth = 0; depth < prepared.plan.block_depth; ++depth)
    {
        auto& idx = prepared.fused_block_levels[depth];
        for (size_t b = 0; b < prepared.plan.blocks.size(); ++b)
        {
            const auto& block = prepared.plan.blocks[b];
            if (depth >= block.network.levels.size())
                continue;

            const int base = prepared.block_offsets[b];
            for (const auto& pair : block.network.levels[depth])
            {
                idx.push_back(base + pair[0]);
                idx.push_back(base + pair[1]);
            }
        }
    }

    if (prepared.plan.outer_partition_cancelled)
        return prepared;

    for (const auto& pair : prepared.plan.matrix.all_pairs)
    {
        prepared.full_matrix_idx.push_back(pair[0]);
        prepared.full_matrix_idx.push_back(pair[1]);
    }
    for (const auto& pair : prepared.plan.matrix.pruned_cross_pairs)
    {
        prepared.pruned_matrix_idx.push_back(pair[0]);
        prepared.pruned_matrix_idx.push_back(pair[1]);
    }

    prepared.full_matrix_comparators =
            prepared.full_matrix_idx.size() / 2;
    prepared.pruned_matrix_comparators =
            prepared.pruned_matrix_idx.size() / 2;
    if (prepared.plan.blocks.size() == 2 &&
            prepared.plan.blocks[0].keep == k &&
            prepared.plan.blocks[1].keep == k)
    {
        prepared.reverse_two_block_idx.reserve(2 * k);
        for (int i = 0; i < k; ++i)
        {
            prepared.reverse_two_block_idx.push_back(i);
            prepared.reverse_two_block_idx.push_back(
                    k + (k - 1 - i));
        }
    }
    bool all_k_blocks = !prepared.plan.blocks.empty();
    for (const auto& block : prepared.plan.blocks)
        all_k_blocks = all_k_blocks && block.keep == k;
    if (all_k_blocks && prepared.plan.blocks.size() >= 2)
    {
        const size_t block_count = prepared.plan.blocks.size();
        prepared.reverse_prefilter_candidate_count =
                ((block_count + 1) / 2) * size_t(k);
        for (size_t b = 0; b + 1 < block_count; b += 2)
            for (int i = 0; i < k; ++i)
            {
                prepared.reverse_prefilter_idx.push_back(
                        int(b * size_t(k) + i));
                prepared.reverse_prefilter_idx.push_back(
                        int((b + 1) * size_t(k) + (k - 1 - i)));
            }
        prepared.reverse_prefilter_comparators =
                prepared.reverse_prefilter_idx.size() / 2;
        for (size_t i = 0; i < prepared.reverse_prefilter_candidate_count; ++i)
            for (size_t j = i + 1; j < prepared.reverse_prefilter_candidate_count; ++j)
            {
                prepared.reverse_prefilter_matrix_idx.push_back(int(i));
                prepared.reverse_prefilter_matrix_idx.push_back(int(j));
            }
        prepared.reverse_prefilter_matrix_comparators =
                prepared.reverse_prefilter_matrix_idx.size() / 2;
    }
    vector<size_t> terminal_block_sizes;
    for (const auto& block : prepared.plan.blocks)
        terminal_block_sizes.push_back(block.keep);
    prepared.obssn = KonaOrderedBlockTerminal::build_obssn(
            terminal_block_sizes, k);
    prepared.octm = KonaOrderedBlockTerminal::build_octm(
            terminal_block_sizes, k);
    return prepared;
}

template<int K>
inline void build_padded_work(
        const vector<SharePair<K>>& input,
        const KonaReducedDepthCongTopK::NetworkPlan& plan,
        int playerno,
        vector<SharePair<K>>& work,
        vector<int>& block_offsets)
{
    block_offsets.clear();
    work.clear();

    const Z2<K> dummy_value =
            (playerno == 0) ? Z2<K>(0) :
            Z2<K>(static_cast<mp_limb_t>(uint64_t(1) << 62));
    const Z2<K> dummy_label = Z2<K>(0);

    for (const auto& block : plan.blocks)
    {
        block_offsets.push_back((int)work.size());

        for (int i = 0; i < block.real_size; ++i)
            work.push_back(input[block.real_begin + i]);

        for (int i = block.real_size; i < block.padded_size; ++i)
            work.push_back({dummy_value, dummy_label});
    }
}

template<int K>
inline vector<SharePair<K>> collect_block_candidates(
        const vector<SharePair<K>>& work,
        const KonaReducedDepthCongTopK::NetworkPlan& plan,
        const vector<int>& block_offsets)
{
    vector<SharePair<K>> candidates;
    candidates.reserve(plan.matrix.candidate_count);

    for (size_t b = 0; b < plan.blocks.size(); ++b)
    {
        const auto& block = plan.blocks[b];
        const int base = block_offsets[b];

        for (int i = 0; i < block.keep; ++i)
        {
            const int local_wire = block.network.output_wires[i];
            candidates.push_back(work[base + local_wire]);
        }
    }

    if ((int)candidates.size() != plan.matrix.candidate_count)
        throw runtime_error("candidate count mismatch");

    return candidates;
}

template<int K>
inline KonaReducedDepthHybrid::Backend secure_matrix_rank_hybrid(
        const vector<SharePair<K>>& candidates,
        const vector<int>& matrix_idx,
        const vector<int>& initial_rank,
        vector<Z2<K>>& rank_share,
        const string& profile,
        const KonaReducedDepthHybrid::Policy& policy,
        KonaDcfCompare::Compare64<K>& dcf,
        RealTwoPartyPlayer* player)
{
    const int playerno = player->my_num();
    const int m = (int)candidates.size();
    rank_share.assign(m, Z2<K>(0));
    if (!initial_rank.empty() && (int)initial_rank.size() != m)
        throw runtime_error("initial rank size mismatch");

    if (playerno == 1)
    {
        for (int i = 0; i < m; ++i)
            rank_share[i] += Z2<K>(
                    static_cast<mp_limb_t>(
                            initial_rank.empty() ? 0 : initial_rank[i]));
    }

    if (matrix_idx.empty())
        return policy.choose(0, profile);

    vector<Z2<K>> compare_res(matrix_idx.size());

    const auto backend = KonaReducedDepthHybrid::compare_pairs<K>(
            candidates, matrix_idx, compare_res, true,
            profile, policy, dcf, player, policy.matrix_backend_override);

    const Z2<K> one =
            playerno == 0 ? Z2<K>(0) :
            Z2<K>(static_cast<mp_limb_t>(1));

    for (size_t p = 0; p < matrix_idx.size() / 2; ++p)
    {
        const int i = matrix_idx[2 * p];
        const int j = matrix_idx[2 * p + 1];
        const Z2<K> c = compare_res[2 * p];

        rank_share[i] += c;
        rank_share[j] += one - c;
    }

    return backend;
}

template<int K>
inline KonaReducedDepthHybrid::Backend topk_mask_from_rank_hybrid(
        const vector<Z2<K>>& rank_share,
        int k,
        vector<Z2<K>>& mask,
        const string& profile,
        const KonaReducedDepthHybrid::Policy& policy,
        KonaDcfCompare::Compare64<K>& dcf,
        RealTwoPartyPlayer* player)
{
    const int playerno = player->my_num();
    const int m = (int)rank_share.size();

    vector<Z2<K>> values(2 * m);
    vector<int> idx(2 * m);

    for (int i = 0; i < m; ++i)
    {
        values[2 * i] = rank_share[i];
        values[2 * i + 1] =
                playerno == 0 ? Z2<K>(0) :
                Z2<K>(static_cast<mp_limb_t>(k));

        idx[2 * i] = 2 * i;
        idx[2 * i + 1] = 2 * i + 1;
    }

    vector<Z2<K>> duplicated(2 * m);
    const auto backend = KonaReducedDepthHybrid::compare_values<K>(
            values, idx, duplicated, false,
            profile, policy, dcf, player);

    mask.resize(m);
    for (int i = 0; i < m; ++i)
        mask[i] = duplicated[2 * i];

    return backend;
}

struct PreparedCongPlan
{
    KonaCongTopK::CongNetwork network;
    vector<vector<int>> flattened_levels;
    size_t comparators = 0;
};
inline PreparedCongPlan prepare_cong(int n, int k)
{
    PreparedCongPlan p;
    p.network = KonaCongTopK::build_cong_network(n, k);
    for (const auto& layer : p.network.levels) {
        p.flattened_levels.push_back(KonaCongTopK::flatten_level(layer));
        p.comparators += layer.size();
    }
    return p;
}
template<int K, class Observer>
void execute_cong(vector<SharePair<K>>& shares, const PreparedCongPlan& plan,
        const string& profile, const Policy& policy, KonaDcfCompare::Compare64<K>& dcf,
        RealTwoPartyPlayer* player, Observer&& observe)
{
    for (size_t d = 0; d < plan.flattened_levels.size(); ++d) {
        const auto& idx = plan.flattened_levels[d];
        if (!idx.empty()) observe("cong", d, idx.size()/2, [&]() {
            return KonaReducedDepthHybrid::compare_and_swap_level<K>(
                    shares, idx, true, profile, policy, dcf, player);
        });
    }
    KonaCongTopK::move_output_wires_to_tail(shares, plan.network.output_wires);
}

// Stage observer owns measurement. It must invoke the supplied operation
// exactly once; protocol code never performs I/O for diagnostics.
struct NoObserver
{
    template<class F>
    void operator()(const char*, size_t, size_t, F operation) const { operation(); }
};

enum class Terminal { Full, Pruned, ReverseTwo, ReversePrefilter, Obssn, Octm };

struct TerminalPlan
{
    vector<int> reverse_idx, matrix_idx, initial_rank, retained_lanes;
    vector<vector<int>> layers;
    vector<int> output_lanes;
    size_t input_count = 0, candidate_count = 0;
    size_t reverse_comparators = 0, matrix_comparators = 0;
    size_t mask_comparators = 0, comparators = 0, depth = 0;
    bool direct = false, network = false, reverse_used = false;
    string output_scope = "topk_membership_mask";
};

// Independent matrix module: usable after ANY producer of sorted local
// candidate blocks, including externally supplied shares. No Cong is built.
inline TerminalPlan prepare_matrix(const vector<int>& block_keep, int k, Terminal kind)
{
    if (kind != Terminal::Full && kind != Terminal::Pruned)
        throw std::invalid_argument("prepare_matrix requires Full or Pruned");
    const auto matrix = KonaReducedDepthCongTopK::build_matrix_finalization_plan(block_keep, k);
    TerminalPlan t;
    t.input_count = t.candidate_count = matrix.candidate_count;
    const auto& pairs = kind == Terminal::Full ? matrix.all_pairs : matrix.pruned_cross_pairs;
    for (const auto& pair : pairs) { t.matrix_idx.push_back(pair[0]); t.matrix_idx.push_back(pair[1]); }
    if (kind == Terminal::Pruned)
        for (int pos : matrix.candidate_position) t.initial_rank.push_back(pos - 1);
    t.matrix_comparators = pairs.size();
    t.mask_comparators = matrix.candidate_count;
    t.comparators = t.matrix_comparators + t.mask_comparators;
    t.depth = (t.matrix_comparators ? 1 : 0) + (t.mask_comparators ? 1 : 0);
    return t;
}

inline TerminalPlan prepare_terminal(const PreparedReducedPlan& p, Terminal kind)
{
    if (p.plan.outer_partition_cancelled)
    {
        TerminalPlan t;
        t.input_count = t.candidate_count = p.plan.k;
        t.direct = true;
        t.output_scope = "ordered_topk";
        return t;
    }
    if (kind == Terminal::Full || kind == Terminal::Pruned)
    {
        vector<int> keep;
        for (const auto& block : p.plan.blocks) keep.push_back(block.keep);
        return prepare_matrix(keep, p.plan.k, kind);
    }
    TerminalPlan t;
    t.input_count = t.candidate_count = p.plan.matrix.candidate_count;
    const bool two = !p.reverse_two_block_idx.empty();
    const bool prefilter = !p.reverse_prefilter_idx.empty();
    if (kind == Terminal::Obssn || kind == Terminal::Octm)
    {
        const auto& network = kind == Terminal::Octm ? p.octm : p.obssn;
        t.network = true;
        t.output_scope = kind == Terminal::Octm ? "ordered_topk" : "obssn_output_lanes";
        for (const auto& layer : network.layers)
        {
            vector<int> idx;
            for (const auto& pair : layer) { idx.push_back(pair.left); idx.push_back(pair.right); }
            t.comparators += layer.size();
            t.layers.push_back(std::move(idx));
        }
        for (auto lane : network.output_lanes) t.output_lanes.push_back(int(lane));
        t.depth = t.layers.size();
        return t;
    }
    if ((kind == Terminal::ReverseTwo || kind == Terminal::ReversePrefilter) && two)
    {
        t.reverse_idx = p.reverse_two_block_idx;
        t.direct = t.reverse_used = true;
        t.reverse_comparators = t.comparators = p.plan.k;
        t.candidate_count = p.plan.k;
        t.depth = 1;
        t.output_scope = "unordered_topk";
        for (int i = 0; i < p.plan.k; ++i) t.retained_lanes.push_back(i);
        return t;
    }
    if (kind == Terminal::ReversePrefilter && prefilter)
    {
        t.reverse_used = true;
        t.reverse_idx = p.reverse_prefilter_idx;
        t.reverse_comparators = p.reverse_prefilter_comparators;
        t.candidate_count = p.reverse_prefilter_candidate_count;
        for (size_t b = 0; b + 1 < p.plan.blocks.size(); b += 2)
            for (int i = 0; i < p.plan.k; ++i)
                t.retained_lanes.push_back(int(b * p.plan.k + i));
        if (p.plan.blocks.size() % 2)
            for (int i = 0; i < p.plan.k; ++i)
                t.retained_lanes.push_back(int((p.plan.blocks.size() - 1) * p.plan.k + i));
        t.matrix_idx = p.reverse_prefilter_matrix_idx;
    }
    else if (kind == Terminal::Full) t.matrix_idx = p.full_matrix_idx;
    else
    {
        t.matrix_idx = p.pruned_matrix_idx;
        t.initial_rank = p.initial_rank;
    }
    t.matrix_comparators = t.matrix_idx.size() / 2;
    t.mask_comparators = t.candidate_count;
    t.comparators = t.reverse_comparators + t.matrix_comparators + t.mask_comparators;
    t.depth = (t.reverse_used ? 1 : 0) + (t.matrix_comparators ? 1 : 0) + (t.mask_comparators ? 1 : 0);
    return t;
}

template<int K>
struct Output
{
    vector<SharePair<K>> candidates;
    vector<Z2<K>> rank, mask;
    string output_scope;
};

template<int K, class Observer>
void execute_blocks(vector<SharePair<K>>& work, const PreparedReducedPlan& plan,
        const string& profile, const Policy& policy, KonaDcfCompare::Compare64<K>& dcf,
        RealTwoPartyPlayer* player, Observer&& observe)
{
    if (work.size() != size_t(plan.plan.n)) throw runtime_error("block input size mismatch");
    for (size_t depth = 0; depth < plan.fused_block_levels.size(); ++depth)
    {
        const auto& idx = plan.fused_block_levels[depth];
        if (!idx.empty()) observe(plan.plan.outer_partition_cancelled ? "single_cong" : "block",
                depth, idx.size() / 2, [&]() {
            return KonaReducedDepthHybrid::compare_and_swap_level<K>(
                    work, idx, true, profile, policy, dcf, player);
        });
    }
}

// Accepts already sorted local Top-k blocks in public collection order.
// Full uses all unordered pairs and rank=0; Pruned uses cross-block pairs
// satisfying u+v<=K+1 and initial_rank=u-1. Neither matrix performs swap.
template<int K, class Observer>
Output<K> execute_terminal(vector<SharePair<K>> candidates, const TerminalPlan& plan,
        int k, const string& profile, const Policy& policy,
        KonaDcfCompare::Compare64<K>& dcf, RealTwoPartyPlayer* player, Observer&& observe)
{
    if (candidates.size() != plan.input_count) throw runtime_error("terminal input size mismatch");
    Output<K> out;
    out.output_scope = plan.output_scope;
    if (plan.network)
    {
        for (size_t d = 0; d < plan.layers.size(); ++d)
        {
            const auto& idx = plan.layers[d];
            if (!idx.empty()) observe("terminal_network", d, idx.size() / 2, [&]() {
                return KonaReducedDepthHybrid::compare_and_swap_level<K>(
                        candidates, idx, true, profile, policy, dcf, player);
            });
        }
        for (auto lane : plan.output_lanes) out.candidates.push_back(candidates.at(lane));
        return out;
    }
    if (!plan.reverse_idx.empty())
    {
        observe("reverse_prefilter", 0, plan.reverse_comparators, [&]() {
            return KonaReducedDepthHybrid::compare_and_swap_level<K>(
                    candidates, plan.reverse_idx, true, profile, policy, dcf, player);
        });
        vector<SharePair<K>> retained;
        retained.reserve(plan.retained_lanes.size());
        for (auto lane : plan.retained_lanes) retained.push_back(candidates.at(lane));
        candidates.swap(retained);
    }
    if (!plan.direct)
    {
        if (!plan.matrix_idx.empty()) observe("matrix_rank", plan.reverse_used ? 1 : 0,
                plan.matrix_comparators, [&]() {
            return secure_matrix_rank_hybrid<K>(candidates, plan.matrix_idx,
                    plan.initial_rank, out.rank, profile, policy, dcf, player);
        });
        else
        {
            out.rank.assign(candidates.size(), Z2<K>(0));
            if (player->my_num() == 1)
                for (size_t i = 0; i < plan.initial_rank.size(); ++i)
                    out.rank[i] += Z2<K>(static_cast<mp_limb_t>(plan.initial_rank[i]));
        }
        if (!out.rank.empty()) observe("rank_mask", plan.reverse_used ? 2 : 1,
                out.rank.size(), [&]() {
            return topk_mask_from_rank_hybrid<K>(out.rank, k, out.mask, profile, policy, dcf, player);
        });
    }
    out.candidates = std::move(candidates);
    return out;
}

// Composition convenience; callers can instead execute blocks, collect, and
// execute_terminal separately. All secret-dependent operations remain online.
template<int K, class Observer>
Output<K> execute_topk(vector<SharePair<K>>& work, const PreparedReducedPlan& plan,
        const TerminalPlan& terminal, const string& profile, const Policy& policy,
        KonaDcfCompare::Compare64<K>& dcf, RealTwoPartyPlayer* player, Observer&& observe)
{
    execute_blocks<K>(work, plan, profile, policy, dcf, player, observe);
    auto candidates = collect_block_candidates<K>(work, plan.plan, plan.block_offsets);
    return execute_terminal<K>(std::move(candidates), terminal, plan.plan.k,
            profile, policy, dcf, player, observe);
}

} // namespace KonaReducedDepthCongTopK

#endif /* MACHINES_KONA_REDUCED_DEPTH_CONG_TOPK_HPP_ */
