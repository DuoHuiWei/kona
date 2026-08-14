#include "sec-ed.hpp"

#include <stdexcept>

#include "constantpp-comm.hpp"

namespace ConstantPP
{
namespace
{

inline Ring random_ring(PRNG& prng)
{
    return Ring(static_cast<mp_limb_t>(prng.get_word()));
}

} // namespace

SecEdDealerMaterial dealer_generate_sec_ed_material(PRNG& prng)
{
    SecEdDealerMaterial out;

    const Ring a = random_ring(prng);
    const Ring c = a * a;

    const Ring a0 = random_ring(prng);
    const Ring c0 = random_ring(prng);

    out.party[0].a_share = a0;
    out.party[1].a_share = a - a0;
    out.party[0].c_share = c0;
    out.party[1].c_share = c - c0;

    return out;
}

Ring sec_ed_n_dim(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        const std::vector<Ring>& y_share,
        const SecEdPartyMaterial& material,
        ProtocolStats* stats)
{
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument("ConstantPP SecED: playerno must be 0 or 1");
    if (x_share.size() != y_share.size())
        throw std::invalid_argument("ConstantPP SecED: dimension mismatch");

    const std::size_t m = x_share.size();
    std::vector<Ring> e_local(m);
    for (std::size_t j = 0; j < m; ++j)
        e_local[j] = x_share[j] - y_share[j] - material.a_share;

    // Algorithm 4 online communication: open the masked vector e.
    // All dimensions are packed into the same protocol stage.
    std::vector<Ring> e_peer;
    exchange_ring_vector(player, e_local, e_peer, stats);
    if (stats)
        ++stats->logical_rounds;

    Ring distance_share(0);
    const Ring two(2);

    for (std::size_t j = 0; j < m; ++j)
    {
        const Ring e = e_local[j] + e_peer[j];
        const Ring cross = two * e * material.a_share;

        if (playerno == 0)
            distance_share += e * e + cross + material.c_share;
        else
            distance_share += cross + material.c_share;
    }

    return distance_share;
}

} // namespace ConstantPP
