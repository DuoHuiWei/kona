#ifndef CONSTANTPP_SEC_EQ_HPP_
#define CONSTANTPP_SEC_EQ_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../Machines/kona-dcf-compare.hpp"
#include "../Networking/Player.h"
#include "constantpp-types.hpp"
#include "sec-bcom-material.hpp"

namespace ConstantPP
{

struct SecEqStats
{
    std::uint64_t equality_predicates = 0;
    std::uint64_t kona_secure_compare_pairs = 0;
    std::uint64_t kona_dcf_evaluate_calls = 0;
    double kona_dcf_evaluate_ms = 0.0;

    void reset()
    {
        equality_predicates = 0;
        kona_secure_compare_pairs = 0;
        kona_dcf_evaluate_calls = 0;
        kona_dcf_evaluate_ms = 0.0;
    }
};

/*
 * Secure equality using Kona's strict DCF comparator:
 *
 *   1{x == y} = 1 - 1{x < y} - 1{y < x}.
 *
 * Both strict comparisons are packed into ONE Compare64 batch, therefore
 * one equality stage still contributes one ConstantPP logical round.
 *
 * This follows the public ConstantPP repository's "one Eq-FSS object reused
 * over the whole batch" engineering style, while using Kona's real
 * two-process DCF implementation instead of the repository's single-process
 * FSS simulation.
 */

/* Equality for every upper-triangle pair in plan. Output order == plan.pairs. */
std::vector<Ring> sec_eq_pairs_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        const SecBComUpperTrianglePlan& plan,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats = nullptr,
        SecEqStats* eq_stats = nullptr);

/* Equality of each secret-shared x_i against one public scalar y. */
std::vector<Ring> sec_eq_public_scalar_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        std::uint64_t y,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats = nullptr,
        SecEqStats* eq_stats = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_EQ_HPP_
