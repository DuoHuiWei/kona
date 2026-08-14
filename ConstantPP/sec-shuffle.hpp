#ifndef CONSTANTPP_SEC_SHUFFLE_HPP_
#define CONSTANTPP_SEC_SHUFFLE_HPP_

#include <cstddef>
#include <vector>

#include "../Networking/Player.h"
#include "../Tools/random.h"
#include "constantpp-types.hpp"

namespace ConstantPP
{

/*
 * Offline trusted-dealer generation for Algorithm 5.
 *
 * num_columns lets one hidden permutation be applied consistently to
 * multiple aligned fields (for example distance and label), while each
 * field receives independent additive masks/correction shares.
 */
ShuffleDealerMaterial dealer_generate_shuffle_material(
        std::size_t n,
        std::size_t num_columns,
        PRNG& prng);

/*
 * Generic Algorithm-5 online shuffle.
 *
 * local_columns[c][i] is this server's additive share of row i, column c.
 * The same hidden permutation is applied to every column.
 *
 * The function returns this server's output share columns and performs
 * exactly the two dependent communication stages of SecShuffle:
 *   round 1: S0 -> S1, [x']_0
 *   round 2: S1 -> S0, x''
 */
std::vector<std::vector<Ring>> sec_shuffle_columns(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<std::vector<Ring>>& local_columns,
        const ShufflePartyMaterial& material,
        ProtocolStats* stats = nullptr);

/* Convenience wrapper for a single secret-shared vector. */
std::vector<Ring> sec_shuffle_vector(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& local_share,
        const ShufflePartyMaterial& material,
        ProtocolStats* stats = nullptr);

/*
 * Convenience wrapper for aligned (value,label) shares.
 * result[0] = shuffled value shares
 * result[1] = shuffled label shares
 */
std::vector<std::vector<Ring>> sec_shuffle_value_label(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& value_share,
        const std::vector<Ring>& label_share,
        const ShufflePartyMaterial& material,
        ProtocolStats* stats = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_SHUFFLE_HPP_
