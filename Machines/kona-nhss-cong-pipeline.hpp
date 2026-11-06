#ifndef KONA_NHSS_CONG_PIPELINE_HPP
#define KONA_NHSS_CONG_PIPELINE_HPP

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "Math/Z2k.hpp"
#include "Networking/Player.h"

#include "kona-cong-rotated-msb-adapter.hpp"
#include "kona-nhss-distance.hpp"
#include "kona-mask-vote.hpp"

/*
 * End-to-end glue for the intended experiment:
 *
 *   NHSS / NISED squared Euclidean distance
 *             (zero online S0<->S1 communication)
 *                      |
 *                      v
 *       additive Z2<64> distance shares
 *                      |
 *                      v
 *              Cong Top-k network
 *                      |
 *                      v
 *          Rotated MSB comparison
 *                      |
 *                      v
 *   existing Kona compare-then-exchange / secret swap
 *
 * Each sortable wire is:
 *      pair[0] = distance share
 *      pair[1] = payload share (normally label or row id)
 */

namespace KonaNhssCongPipeline
{

using Share64 = Z2<64>;
using SharePair64 = std::array<Share64, 2>;

struct RecordShare
{
    std::vector<KonaNhss::DataShare> coordinates;
    Share64 payload_share;
};

inline std::vector<SharePair64> build_distance_payload_pairs(
    int party,
    const std::vector<RecordShare>& database,
    const std::vector<KonaNhss::QueryShare>& query,
    const KonaNhss::PublicKey& pub,
    std::size_t dimension_batch_size = 0)
{
    if (party != 0 && party != 1)
        throw std::runtime_error("nhss+cong: party must be 0 or 1");

    std::vector<SharePair64> wires;
    wires.reserve(database.size());

    for (std::size_t i = 0; i < database.size(); ++i)
    {
        if (database[i].coordinates.size() != query.size())
            throw std::runtime_error(
                "nhss+cong: database/query dimension mismatch");

        SharePair64 wire;
        wire[0] =
            KonaNhss::nised_squared_euclidean_additive_share_batched_l2(
                party, database[i].coordinates, query, pub,
                dimension_batch_size);
        wire[1] = database[i].payload_share;
        wires.push_back(wire);
    }

    return wires;
}

inline void top_k_in_place(
    std::vector<SharePair64>& distance_payload_wires,
    int k,
    RealTwoPartyPlayer* player,
    std::size_t compare_batch_size =
        KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK)
{
    KonaCongRotatedMsbAdapter::cong_top_k_with_rotated_msb<64>(
        distance_payload_wires,
        k,
        true, // KNN = k smallest distances
        player,
        compare_batch_size);
}

/*
 * Convenience one-shot wrapper.
 *
 * The Cong implementation follows the existing repository convention for
 * where the final k wires reside; this function intentionally does not open
 * or reveal them.
 */
inline std::vector<SharePair64> run(
    int party,
    const std::vector<RecordShare>& database,
    const std::vector<KonaNhss::QueryShare>& query,
    const KonaNhss::PublicKey& pub,
    int k,
    RealTwoPartyPlayer* player,
    std::size_t dimension_batch_size = 0,
    std::size_t compare_batch_size =
        KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK)
{
    std::vector<SharePair64> wires =
        build_distance_payload_pairs(
            party, database, query, pub, dimension_batch_size);
    top_k_in_place(wires, k, player, compare_batch_size);
    return wires;
}

// Complete Shao experiment variant, retaining the pure RMSB Top-k wrapper.
// Returns only a winning-label share; counts remain secret. Public label
// domain and smallest-label tie rule match the existing mask-vote module.
inline Share64 classify(
    int party,const std::vector<RecordShare>& database,
    const std::vector<KonaNhss::QueryShare>& query,const KonaNhss::PublicKey& pub,
    int k,int label_domain,RealTwoPartyPlayer* player,
    KonaDcfCompare::Compare64<64>& dcf,
    size_t dimension_batch_size=0)
{
    auto plan=KonaCongRotatedMsbAdapter::prepare_plan(database.size(),k);
    auto wires=build_distance_payload_pairs(party,database,query,pub,dimension_batch_size);
    KonaCongRotatedMsbAdapter::execute_prepared<64>(wires,plan,true,player,
        [](size_t,size_t,size_t,auto operation){operation();});
    std::vector<SharePair64> selected(wires.end()-k,wires.end());
    return KonaMaskVote::vote_with_dcf<64>(selected,
        KonaMaskVote::public_constants<64>(k,1,player),label_domain,player,dcf);
}

} // namespace KonaNhssCongPipeline

#endif
