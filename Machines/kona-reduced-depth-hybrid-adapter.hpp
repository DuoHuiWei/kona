#ifndef MACHINES_KONA_REDUCED_DEPTH_HYBRID_ADAPTER_HPP_
#define MACHINES_KONA_REDUCED_DEPTH_HYBRID_ADAPTER_HPP_

#include "kona-bench-config.hpp"
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "Math/Z2k.hpp"
#include "Networking/Player.h"
#include "Machines/kona-cong-kona-adapter.hpp"
#include "Machines/kona-dcf-compare.hpp"
#include "Machines/kona-pcr-compare.hpp"

namespace KonaReducedDepthHybrid
{

enum class Backend
{
    AUTO,
    DCF,
    PCR
};

inline const char* backend_name(Backend b)
{
    if (b == Backend::DCF)
        return "DCF";
    if (b == Backend::PCR)
        return "PCR";
    return "AUTO";
}

struct Policy
{
    // Frozen provisional thresholds for this experiment.
    // Interpretation:
    //   comparators <= threshold -> DCF
    //   comparators >  threshold -> PCR
    size_t lan_threshold = KonaBenchConfig::LAN_THRESHOLD;
    size_t wan_threshold = KonaBenchConfig::WAN_THRESHOLD;

    // Optional hard override. 0 means "use profile threshold".
    size_t threshold_override = 0;

    // Diagnostic-only override for the matrix ranking stage.  AUTO preserves
    // the normal threshold policy and never changes block or rank-mask stages.
    Backend matrix_backend_override = Backend::AUTO;

    // Explicit all-stage diagnostic selection. AUTO preserves the historical
    // per-width policy and its defaults exactly.
    Backend comparator_override = Backend::AUTO;

    size_t threshold_for(const std::string& profile) const
    {
        if (threshold_override != 0)
            return threshold_override;
        if (profile == "lan")
            return lan_threshold;
        if (profile == "wan")
            return wan_threshold;
        throw std::invalid_argument(
                "hybrid profile must be lan or wan, got: " + profile);
    }

    Backend choose(size_t comparator_count, const std::string& profile) const
    {
        if (comparator_override != Backend::AUTO)
            return comparator_override;
        return comparator_count <= threshold_for(profile) ?
                Backend::DCF : Backend::PCR;
    }

    Backend choose_matrix(size_t comparator_count, const std::string& profile) const
    {
        if (matrix_backend_override != Backend::AUTO)
            return matrix_backend_override;
        return choose(comparator_count, profile);
    }
};

struct SelectionStats
{
    size_t pcr_stages = 0;
    size_t dcf_stages = 0;

    size_t pcr_comparators = 0;
    size_t dcf_comparators = 0;

    // Count changes between consecutive non-empty secure comparison stages.
    size_t backend_switches = 0;

    bool has_previous = false;
    Backend previous = Backend::DCF;

    void record(Backend backend, size_t comparators)
    {
        if (has_previous && backend != previous)
            ++backend_switches;

        has_previous = true;
        previous = backend;

        if (backend == Backend::PCR)
        {
            ++pcr_stages;
            pcr_comparators += comparators;
        }
        else
        {
            ++dcf_stages;
            dcf_comparators += comparators;
        }
    }
};

template<int K>
using SharePair = std::array<Z2<K>, 2>;

template<int K>
inline Backend compare_pairs(
        const std::vector<SharePair<K>>& shares,
        const std::vector<int>& compare_idx_vec,
        std::vector<Z2<K>>& compare_res,
        bool greater_than,
        const std::string& profile,
        const Policy& policy,
        KonaDcfCompare::Compare64<K>& dcf,
        RealTwoPartyPlayer* player,
        Backend forced_backend = Backend::AUTO)
{
    if (compare_idx_vec.empty() || compare_idx_vec.size() % 2 != 0)
        throw std::invalid_argument(
                "compare_idx_vec must contain a non-empty even number of indices");

    const size_t comparators = compare_idx_vec.size() / 2;
    const Backend backend = forced_backend == Backend::AUTO ?
            policy.choose(comparators, profile) : forced_backend;

    compare_res.resize(compare_idx_vec.size());

    if (backend == Backend::DCF)
        dcf.compare_in_vec(
                shares, compare_idx_vec, compare_res, greater_than);
    else
        KonaPcrCompare::pcr_compare_in_vec_l2(
                shares, compare_idx_vec, compare_res, greater_than, player);

    return backend;
}

template<int K>
inline Backend compare_values(
        const std::vector<Z2<K>>& values,
        const std::vector<int>& compare_idx_vec,
        std::vector<Z2<K>>& compare_res,
        bool greater_than,
        const std::string& profile,
        const Policy& policy,
        KonaDcfCompare::Compare64<K>& dcf,
        RealTwoPartyPlayer* player)
{
    if (compare_idx_vec.empty() || compare_idx_vec.size() % 2 != 0)
        throw std::invalid_argument(
                "compare_idx_vec must contain a non-empty even number of indices");

    const size_t comparators = compare_idx_vec.size() / 2;
    const Backend backend = policy.choose(comparators, profile);

    compare_res.resize(compare_idx_vec.size());

    if (backend == Backend::DCF)
        dcf.compare_in_vec(
                values, compare_idx_vec, compare_res, greater_than);
    else
        KonaPcrCompare::pcr_compare_in_vec_l2(
                values, compare_idx_vec, compare_res, greater_than, player);

    return backend;
}

template<int K>
inline Backend compare_and_swap_level(
        std::vector<SharePair<K>>& shares,
        const std::vector<int>& compare_idx_vec,
        bool greater_than,
        const std::string& profile,
        const Policy& policy,
        KonaDcfCompare::Compare64<K>& dcf,
        RealTwoPartyPlayer* player)
{
    std::vector<Z2<K>> compare_res(compare_idx_vec.size());

    Backend backend = compare_pairs<K>(
            shares,
            compare_idx_vec,
            compare_res,
            greater_than,
            profile,
            policy,
            dcf,
            player);

    KonaCongKonaAdapter::ss_vec_kona_l2<K>(
            shares, compare_idx_vec, compare_res, player);

    return backend;
}

} // namespace KonaReducedDepthHybrid

#endif /* MACHINES_KONA_REDUCED_DEPTH_HYBRID_ADAPTER_HPP_ */
