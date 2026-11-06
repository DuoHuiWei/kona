#ifndef CONSTANTPP_SEC_ED_BATCH_HPP_
#define CONSTANTPP_SEC_ED_BATCH_HPP_

#include <vector>

#include "../Networking/Player.h"
#include "constantpp-types.hpp"
#include "sec-ed.hpp"

namespace ConstantPP
{

/*
 * Batched SecED wrapper preserving the existing sec_ed_n_dim arithmetic.
 *
 * It computes distances from one query share to every database row share.
 * All masked differences for all rows and dimensions are opened in one
 * exchange, reducing N independent SecED rounds to one protocol round.
 */
std::vector<Ring> sec_ed_batch_n_dim(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& query_share,
        const std::vector<std::vector<Ring>>& database_share,
        const SecEdPartyMaterial& material,
        ProtocolStats* stats = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_ED_BATCH_HPP_
