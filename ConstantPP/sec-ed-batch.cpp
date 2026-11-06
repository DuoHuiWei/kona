#include "sec-ed-batch.hpp"

#include <stdexcept>

#include "constantpp-comm.hpp"

namespace ConstantPP
{

std::vector<Ring> sec_ed_batch_n_dim(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& query_share,
        const std::vector<std::vector<Ring>>& database_share,
        const SecEdPartyMaterial& material,
        ProtocolStats* stats)
{
    if (!player)
        throw std::invalid_argument("ConstantPP Batch SecED: null RealTwoPartyPlayer");
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument("ConstantPP Batch SecED: playerno must be 0 or 1");

    const std::size_t n = database_share.size();
    const std::size_t m = query_share.size();

    for (const auto& row : database_share)
    {
        if (row.size() != m)
            throw std::invalid_argument("ConstantPP Batch SecED: dimension mismatch");
    }

    std::vector<Ring> e_local;
    e_local.reserve(n * m);
    for (std::size_t i = 0; i < n; ++i)
    {
        for (std::size_t j = 0; j < m; ++j)
            e_local.push_back(
                    database_share[i][j] - query_share[j] - material.a_share);
    }

    std::vector<Ring> e_peer;
    // Both local vectors are ready; the shared helper sends and receives
    // concurrently and preserves one logical exchange and all payload bytes.
    exchange_ring_vector(player, e_local, e_peer, stats);
    if (stats)
        ++stats->logical_rounds;

    std::vector<Ring> distance_share(n, Ring(0));
    const Ring two(2);

    for (std::size_t i = 0; i < n; ++i)
    {
        Ring acc(0);
        for (std::size_t j = 0; j < m; ++j)
        {
            const std::size_t flat = i * m + j;
            const Ring e = e_local[flat] + e_peer[flat];
            const Ring cross = two * e * material.a_share;

            if (playerno == 0)
                acc += e * e + cross + material.c_share;
            else
                acc += cross + material.c_share;
        }
        distance_share[i] = acc;
    }

    return distance_share;
}

} // namespace ConstantPP
