#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

#include "Machines/kona-reduced-depth-cong-topk.hpp"

using KonaReducedDepthCongTopK::NetworkPlan;

static void check_no_conflict_in_cong_levels(const NetworkPlan& plan)
{
    for (const auto& block : plan.blocks)
    {
        for (const auto& level : block.network.levels)
        {
            std::vector<int> used(block.padded_size, 0);
            for (const auto& p : level)
            {
                assert(p[0] >= 0 && p[0] < block.padded_size);
                assert(p[1] >= 0 && p[1] < block.padded_size);
                assert(p[0] != p[1]);
                assert(!used[p[0]]);
                assert(!used[p[1]]);
                used[p[0]] = 1;
                used[p[1]] = 1;
            }
        }
    }
}

static void check_matrix_pairs(const NetworkPlan& plan)
{
    if (plan.outer_partition_cancelled)
    {
        assert(plan.blocks.size() == 1);
        assert(plan.blocks[0].real_size == plan.n);
        assert(plan.matrix.candidate_count == plan.k);
        assert(plan.matrix.all_pairs.empty());
        assert(plan.matrix.pruned_cross_pairs.empty());
        return;
    }
    const int m = plan.matrix.candidate_count;
    const std::size_t expected =
        static_cast<std::size_t>(m) * static_cast<std::size_t>(m - 1) / 2;

    assert(plan.matrix.all_pairs.size() == expected);

    std::vector<std::vector<int>> seen(
        m, std::vector<int>(m, 0));

    for (const auto& p : plan.matrix.all_pairs)
    {
        assert(p[0] >= 0 && p[0] < m);
        assert(p[1] >= 0 && p[1] < m);
        assert(p[0] < p[1]);
        assert(!seen[p[0]][p[1]]);
        seen[p[0]][p[1]] = 1;
    }
}

// Cleartext semantic smoke test for the frozen high-level construction.
// It does NOT try to emulate secure compare-and-swap internals.
// Instead it verifies the mathematical fact used by the construction:
// global Top-k is contained in the union of each block's local Top-k.
static void check_cleartext_topk_semantics(
    int n, int k, std::uint64_t seed)
{
    NetworkPlan plan =
        KonaReducedDepthCongTopK::build_reduced_depth_cong_plan(n, k);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::int64_t> dist(-1000000, 1000000);

    std::vector<std::int64_t> x(n);
    for (auto& v : x)
        v = dist(rng);

    std::vector<std::int64_t> candidates;
    for (const auto& block : plan.blocks)
    {
        std::vector<std::int64_t> local(
            x.begin() + block.real_begin,
            x.begin() + block.real_begin + block.real_size);

        std::sort(local.begin(), local.end());
        local.resize(block.keep);

        candidates.insert(
            candidates.end(), local.begin(), local.end());
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.resize(k);

    std::vector<std::int64_t> truth = x;
    std::sort(truth.begin(), truth.end());
    truth.resize(k);

    if (candidates != truth)
        throw std::runtime_error("cleartext Top-k semantic check failed");
}

static void print_case(int n, int k)
{
    NetworkPlan p =
        KonaReducedDepthCongTopK::build_reduced_depth_cong_plan(n, k);

    auto baseline = KonaCongTopK::build_cong_network(n, k);
    std::size_t baseline_c =
        KonaReducedDepthCongTopK::comparator_count(baseline);
    std::size_t baseline_d = baseline.levels.size();

    std::cout
        << "N=" << n
        << " K=" << k
        << " rho=" << p.rho
        << " h=" << p.h
        << " Lmax=" << p.lmax
        << " outer_partition_cancelled=" << int(p.outer_partition_cancelled)
        << " blocks=" << p.blocks.size()
        << " padding=" << p.padding
        << " candidates=" << p.matrix.candidate_count
        << "\n";

    std::cout
        << "  original_cong: C=" << baseline_c
        << " D=" << baseline_d
        << "\n";

    std::cout
        << "  new_v1:        C=" << p.total_comparators
        << " D_struct=" << p.structural_depth
        << " block_C=" << p.block_comparators
        << " matrix_C=" << p.matrix_comparators
        << " block_D=" << p.block_depth
        << "\n";

    if (baseline_c != 0)
    {
        const double dc =
            100.0 * (static_cast<double>(p.total_comparators) /
                     static_cast<double>(baseline_c) - 1.0);
        std::cout << "  delta_C=" << dc << "%\n";
    }

    if (baseline_d != 0)
    {
        const double dd =
            100.0 * (static_cast<double>(p.structural_depth) /
                     static_cast<double>(baseline_d) - 1.0);
        std::cout << "  delta_D=" << dd << "%\n";
    }
}

int main()
{
    const std::vector<std::pair<int,int>> cases = {
        {16, 3},
        {17, 3},
        {31, 5},
        {32, 5},
        {33, 5},   // tail=1 for K=5
        {34, 5},   // tail=2 for K=5
        {36, 5},   // tail=K-1 for K=5
        {37, 5},   // tail=K for K=5
        {38, 5},   // tail=K+1 for K=5
        {199, 3},
        {199, 32}, // equality at K/Lmax=1/4 keeps 128+71
        {199, 33}, // strictly above 1/4 uses original Cong(199,33)
        {199, 100},
        {200, 5},
        {300, 3},
        {800, 31},
        {1024, 5},
        {4096, 16}
    };

    for (const auto& [n, k] : cases)
    {
        NetworkPlan p =
            KonaReducedDepthCongTopK::build_reduced_depth_cong_plan(n, k);

        check_no_conflict_in_cong_levels(p);
        check_matrix_pairs(p);

        for (std::uint64_t seed = 1; seed <= 20; ++seed)
            check_cleartext_topk_semantics(n, k, seed);

        print_case(n, k);
    }

    std::cout << "SMOKE_TEST=PASS\n";
    return 0;
}
