#ifndef CONSTANTPP_SEC_MAX_HPP_
#define CONSTANTPP_SEC_MAX_HPP_

#include <vector>

#include "../Machines/kona-dcf-compare.hpp"
#include "../Networking/Player.h"
#include "constantpp-types.hpp"
#include "sec-bcom.hpp"
#include "sec-eq.hpp"
#include "sec-mul.hpp"

namespace ConstantPP
{

struct SecMaxStats
{
    SecBComStats bcom;
    SecEqStats eq;
    std::uint64_t secure_mul_pairs = 0;

    void reset()
    {
        bcom.reset();
        eq.reset();
        secure_mul_pairs = 0;
    }
};

/*
 * Public-repository SecMax logic:
 *
 *   1. SecBCom(frequency, Greater)
 *   2. SecEqTest(su_i, k-1)
 *   3. SecMul(label_i, indicator_i)
 *   4. local sum
 *
 * Steps 1-3 each contribute one ConstantPP logical round for normal k>=2,
 * giving SecMax = 3 rounds.
 *
 * The same Beaver triple is reused across the label*indicator vector to
 * match the public ConstantPP repository's engineering behavior.
 */
Ring sec_max_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& frequency_share,
        const std::vector<Ring>& label_share,
        const BeaverTripleParty& reused_mul_triple,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats = nullptr,
        SecMaxStats* max_stats = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_MAX_HPP_
