#ifndef CONSTANTPP_SEC_ED_HPP_
#define CONSTANTPP_SEC_ED_HPP_

#include <array>
#include <vector>

#include "../Networking/Player.h"
#include "../Tools/random.h"
#include "constantpp-types.hpp"

namespace ConstantPP
{

/*
 * Offline dealer helper for the first compatibility implementation.
 * One (a, c=a^2) pair is generated and additively shared between S0/S1.
 * The same local material can then be reused across dimensions, matching
 * upstream secED_nDim() behavior requested for this baseline.
 */
SecEdDealerMaterial dealer_generate_sec_ed_material(PRNG& prng);

/*
 * Algorithm-4-style online SecED with real two-party communication.
 *
 * Each process passes only its own shares:
 *   x_share[j] == [x_j]_p
 *   y_share[j] == [y_j]_p
 *   material    == ([a]_p, [c]_p)
 *
 * The function performs one batched exchange of all [e_j]_p values and
 * returns this server's additive share [d]_p of squared Euclidean distance.
 */
Ring sec_ed_n_dim(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        const std::vector<Ring>& y_share,
        const SecEdPartyMaterial& material,
        ProtocolStats* stats = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_ED_HPP_
