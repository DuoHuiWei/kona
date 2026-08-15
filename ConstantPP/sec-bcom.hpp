#ifndef CONSTANTPP_SEC_BCOM_HPP_
#define CONSTANTPP_SEC_BCOM_HPP_

#include <cstddef>
#include <vector>

#include "../Machines/kona-dcf-compare.hpp"
#include "../Networking/Player.h"
#include "constantpp-types.hpp"
#include "sec-bcom-material.hpp"

namespace ConstantPP
{

struct SecBComStats
{
    std::uint64_t unordered_pairs = 0;
    std::uint64_t dcf_compare_pairs = 0;
    std::uint64_t kona_dcf_evaluate_calls = 0;
    double kona_dcf_evaluate_ms = 0.0;

    void reset()
    {
        unordered_pairs = 0;
        dcf_compare_pairs = 0;
        kona_dcf_evaluate_calls = 0;
        kona_dcf_evaluate_ms = 0.0;
    }
};

/*
 * ConstantPP SecBCom engineering implementation.
 *
 * Algorithm structure:
 *   - ConstantPP/source-like all-pairs rank counting
 *   - upper triangle only: i < j
 *   - one secure comparison per unordered pair
 *   - update both su_i and su_j from the same secret comparison share
 *
 * Secure comparison primitive:
 *   - KonaDcfCompare::Compare64<64>
 *   - real two-process communication through RealTwoPartyPlayer
 *   - Kona's existing masked comparison/FSS implementation
 *
 * This deliberately does NOT reconstruct x_i/x_j in plaintext.
 *
 * Tie handling:
 * The public constantPP-KNN upper-triangle implementation effectively
 * assigns exactly one direction to every unordered pair, including equality.
 * We preserve this source-like total-order behavior by using the complement
 * for the reverse direction. Therefore equal values are deterministically
 * ordered according to pair orientation.
 *
 * Logical protocol accounting:
 * SecBCom is one ConstantPP comparison stage. Kona Compare64 may split a
 * large pair batch into multiple transport chunks (MAX_COMPARE_CHUNK);
 * those are transport rounds, not additional ConstantPP logical rounds.
 */
std::vector<Ring> sec_bcom_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        CompareOp op,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats = nullptr,
        SecBComStats* bcom_stats = nullptr);

/*
 * Same protocol with an explicit pre-built upper-triangle plan.
 * This is useful for benchmark setup so pair topology construction stays
 * outside the online timer.
 */
std::vector<Ring> sec_bcom_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        CompareOp op,
        const SecBComUpperTrianglePlan& plan,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats = nullptr,
        SecBComStats* bcom_stats = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_BCOM_HPP_
