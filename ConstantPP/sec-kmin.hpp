#ifndef CONSTANTPP_SEC_KMIN_HPP_
#define CONSTANTPP_SEC_KMIN_HPP_

#include <cstddef>
#include <vector>

#include "../Machines/kona-dcf-compare.hpp"
#include "../Networking/Player.h"
#include "constantpp-types.hpp"
#include "sec-bcom.hpp"
#include "sec-shuffle.hpp"

namespace ConstantPP
{

struct SecKMinStats
{
    std::uint64_t selected_count = 0;
    std::uint64_t threshold_compare_pairs = 0;
    std::uint64_t threshold_dcf_evaluate_calls = 0;
    double threshold_dcf_evaluate_ms = 0.0;

    void reset()
    {
        selected_count = 0;
        threshold_compare_pairs = 0;
        threshold_dcf_evaluate_calls = 0;
        threshold_dcf_evaluate_ms = 0.0;
    }
};

/*
 * ConstantPP SecKMin over secret-shared distance/label pairs.
 *
 * Structure:
 *   1. SecShuffle(distance,label)                       2 rounds
 *   2. SecBCom(shuffled_distance, Less)                1 round
 *   3. secure threshold compare:
 *        u_i = 1{su_i > n-k-1}                         1 round
 *   4. open all u_i and select shuffled label shares   1 round
 *
 * Total ConstantPP logical rounds: 5.
 *
 * Engineering policy:
 * Follow the public ConstantPP repository's batch/key-reuse logic, while
 * replacing its single-process FSS simulation with Kona's real two-process
 * DCF comparison primitive.  The threshold predicate is evaluated securely
 * as 1{su_i > n-k-1}; su_i is never reconstructed in plaintext.
 *
 * Normal benchmark path: 1 <= k < n, total 5 ConstantPP logical rounds.
 * Edge cases k==0 and k==n return early with correct functionality.
 *
 * The caller supplies prebuilt shuffle and upper-triangle materials so
 * topology/material setup can stay outside the online timer.
 */
std::vector<Ring> sec_kmin_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& distance_share,
        const std::vector<Ring>& label_share,
        std::size_t k,
        const ShufflePartyMaterial& shuffle_material,
        const SecBComUpperTrianglePlan& bcom_plan,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats = nullptr,
        SecBComStats* bcom_stats = nullptr,
        SecKMinStats* kmin_stats = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_KMIN_HPP_
