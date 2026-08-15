#ifndef CONSTANTPP_SEC_MUL_HPP_
#define CONSTANTPP_SEC_MUL_HPP_

#include <vector>

#include "../Networking/Player.h"
#include "../Tools/random.h"
#include "constantpp-types.hpp"

namespace ConstantPP
{

BeaverTripleDealer dealer_generate_beaver_triple(PRNG& prng);

Ring sec_mul(
        RealTwoPartyPlayer* player,
        int playerno,
        const Ring& x_share,
        const Ring& y_share,
        const BeaverTripleParty& triple,
        ProtocolStats* stats = nullptr);

std::vector<Ring> sec_mul_vector_reuse(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        const std::vector<Ring>& y_share,
        const BeaverTripleParty& triple,
        ProtocolStats* stats = nullptr);

std::vector<Ring> sec_mul_vector_fresh(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        const std::vector<Ring>& y_share,
        const std::vector<BeaverTripleParty>& triples,
        ProtocolStats* stats = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_MUL_HPP_
