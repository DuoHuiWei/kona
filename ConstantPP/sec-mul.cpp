#include "sec-mul.hpp"

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

inline void validate_player(int playerno)
{
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument(
                "ConstantPP SecMul: playerno must be 0 or 1");
}

} // namespace

BeaverTripleDealer dealer_generate_beaver_triple(PRNG& prng)
{
    BeaverTripleDealer out;

    const Ring a = random_ring(prng);
    const Ring b = random_ring(prng);
    const Ring c = a * b;

    const Ring a0 = random_ring(prng);
    const Ring b0 = random_ring(prng);
    const Ring c0 = random_ring(prng);

    out.party[0].a_share = a0;
    out.party[1].a_share = a - a0;

    out.party[0].b_share = b0;
    out.party[1].b_share = b - b0;

    out.party[0].c_share = c0;
    out.party[1].c_share = c - c0;

    return out;
}

Ring sec_mul(
        RealTwoPartyPlayer* player,
        int playerno,
        const Ring& x_share,
        const Ring& y_share,
        const BeaverTripleParty& triple,
        ProtocolStats* stats)
{
    validate_player(playerno);

    const Ring e_local = x_share - triple.a_share;
    const Ring f_local = y_share - triple.b_share;

    std::vector<Ring> local = {e_local, f_local};
    std::vector<Ring> peer;
    exchange_ring_vector(player, local, peer, stats);

    if (stats)
        ++stats->logical_rounds;

    const Ring e = e_local + peer[0];
    const Ring f = f_local + peer[1];

    Ring z = triple.b_share * e
           + triple.a_share * f
           + triple.c_share;

    if (playerno == 0)
        z += e * f;

    return z;
}

std::vector<Ring> sec_mul_vector_reuse(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        const std::vector<Ring>& y_share,
        const BeaverTripleParty& triple,
        ProtocolStats* stats)
{
    validate_player(playerno);

    if (x_share.size() != y_share.size())
        throw std::invalid_argument(
                "ConstantPP SecMul vector reuse: size mismatch");

    const std::size_t n = x_share.size();
    std::vector<Ring> local(2 * n);

    for (std::size_t i = 0; i < n; ++i)
    {
        local[2 * i] = x_share[i] - triple.a_share;
        local[2 * i + 1] = y_share[i] - triple.b_share;
    }

    std::vector<Ring> peer;
    exchange_ring_vector(player, local, peer, stats);

    if (stats)
        ++stats->logical_rounds;

    std::vector<Ring> out(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const Ring e = local[2 * i] + peer[2 * i];
        const Ring f = local[2 * i + 1] + peer[2 * i + 1];

        Ring z = triple.b_share * e
               + triple.a_share * f
               + triple.c_share;

        if (playerno == 0)
            z += e * f;

        out[i] = z;
    }

    return out;
}

std::vector<Ring> sec_mul_vector_fresh(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        const std::vector<Ring>& y_share,
        const std::vector<BeaverTripleParty>& triples,
        ProtocolStats* stats)
{
    validate_player(playerno);

    if (x_share.size() != y_share.size() ||
            x_share.size() != triples.size())
        throw std::invalid_argument(
                "ConstantPP SecMul vector fresh: size mismatch");

    const std::size_t n = x_share.size();
    std::vector<Ring> local(2 * n);

    for (std::size_t i = 0; i < n; ++i)
    {
        local[2 * i] = x_share[i] - triples[i].a_share;
        local[2 * i + 1] = y_share[i] - triples[i].b_share;
    }

    std::vector<Ring> peer;
    exchange_ring_vector(player, local, peer, stats);

    if (stats)
        ++stats->logical_rounds;

    std::vector<Ring> out(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const Ring e = local[2 * i] + peer[2 * i];
        const Ring f = local[2 * i + 1] + peer[2 * i + 1];

        Ring z = triples[i].b_share * e
               + triples[i].a_share * f
               + triples[i].c_share;

        if (playerno == 0)
            z += e * f;

        out[i] = z;
    }

    return out;
}

} // namespace ConstantPP
