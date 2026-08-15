#ifndef CONSTANTPP_SEC_FRE_HPP_
#define CONSTANTPP_SEC_FRE_HPP_

#include <vector>

#include "../Machines/kona-dcf-compare.hpp"
#include "../Networking/Player.h"
#include "constantpp-types.hpp"
#include "sec-eq.hpp"

namespace ConstantPP
{

struct SecFreStats
{
    std::uint64_t upper_triangle_eq_predicates = 0;
    SecEqStats eq;

    void reset()
    {
        upper_triangle_eq_predicates = 0;
        eq.reset();
    }
};

/*
 * Public-repository SecFre logic:
 *
 *   for i<j:
 *       e_ij = 1{label_i == label_j}
 *       f_i += e_ij
 *       f_j += e_ij
 *   f_i += 1 for the item itself
 *
 * All equality predicates are evaluated in one Kona DCF batch, so SecFre
 * contributes one ConstantPP logical round for k>=2.
 */
std::vector<Ring> sec_fre_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& label_share,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats = nullptr,
        SecFreStats* fre_stats = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_FRE_HPP_
