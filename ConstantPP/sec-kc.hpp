#ifndef CONSTANTPP_SEC_KC_HPP_
#define CONSTANTPP_SEC_KC_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../Machines/kona-dcf-compare.hpp"
#include "../Networking/Player.h"

#include "constantpp-types.hpp"
#include "sec-bcom.hpp"
#include "sec-ed.hpp"
#include "sec-fre.hpp"
#include "sec-kmin.hpp"
#include "sec-max.hpp"
#include "sec-mul.hpp"
#include "sec-shuffle.hpp"

namespace ConstantPP
{

struct SecKCStats
{
    std::uint64_t distance_count = 0;
    SecBComStats kmin_bcom;
    SecKMinStats kmin;
    SecFreStats fre;
    SecMaxStats max;
    std::uint64_t logical_rounds = 0;

    void reset()
    {
        distance_count = 0;
        kmin_bcom.reset();
        kmin.reset();
        fre.reset();
        max.reset();
        logical_rounds = 0;
    }
};

/*
 * Complete ConstantPP KNN classification pipeline over the current Kona
 * component APIs.
 *
 * The caller supplies offline material explicitly so SecKC does not alter or
 * wrap the already validated SecED/SecShuffle/SecBCom/SecKMin/SecFre/SecMax
 * implementations.
 */
Ring sec_kc_kona(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& query_share,
        const std::vector<std::vector<Ring>>& database_share,
        const std::vector<Ring>& label_share,
        std::size_t k,
        const SecEdPartyMaterial& ed_material,
        const ShufflePartyMaterial& shuffle_material,
        const SecBComUpperTrianglePlan& bcom_plan,
        const BeaverTripleParty& reused_triple,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats = nullptr,
        SecKCStats* kc_stats = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_KC_HPP_
