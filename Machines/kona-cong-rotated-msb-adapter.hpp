#ifndef KONA_CONG_ROTATED_MSB_ADAPTER_HPP
#define KONA_CONG_ROTATED_MSB_ADAPTER_HPP

#include <array>
#include <stdexcept>
#include <vector>

#include "Math/Z2k.hpp"
#include "Networking/Player.h"

#include "kona-cong-kona-adapter.hpp"
#include "kona-cong-topk.hpp"
#include "kona-rotated-msb-compare.hpp"

/*
 * Thin adapter:
 *
 *   existing Cong network
 *       -> Rotated MSB comparison for each whole network level
 *       -> existing Kona compare-then-exchange / secret swap
 *
 * No Cong network construction code is duplicated here.
 */

namespace KonaCongRotatedMsbAdapter
{

template<int K>
using SharePair = std::array<Z2<K>, 2>;

template<int K>
inline void cong_top_k_with_rotated_msb(
    std::vector<SharePair<K> >& shares,
    int k,
    bool min_k,
    RealTwoPartyPlayer* player,
    std::size_t compare_batch_size =
        KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK)
{
    static_assert(
        K == 64,
        "Current rotated-MSB backend is defined for Z2<64> only");

    if (!player)
        throw std::runtime_error("cong+rmsb: null player");

    KonaCongTopK::cong_top_k<SharePair<K> >(
        shares,
        static_cast<int>(shares.size()),
        k,
        [player, min_k, compare_batch_size](
            std::vector<SharePair<K> >& level_shares,
            const std::vector<int>& compare_idx_vec)
        {
            const std::size_t pairs = compare_idx_vec.size() / 2;
            const std::size_t requested = compare_batch_size == 0 ?
                KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK :
                compare_batch_size;
            // Keep the existing 16384 common communication ceiling even if
            // the caller requests a larger batch.
            const std::size_t batch_size = std::min(
                requested,
                KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK);

            for (std::size_t offset = 0; offset < pairs; offset += batch_size)
            {
                const std::size_t count =
                    std::min(batch_size, pairs - offset);
                std::vector<int> chunk_idx(
                    compare_idx_vec.begin() + 2 * offset,
                    compare_idx_vec.begin() + 2 * (offset + count));
                std::vector<Z2<K> > compare_res(chunk_idx.size());
                KonaRotatedMsbCompare::rotated_msb_compare_in_vec_l2(
                    level_shares,
                    chunk_idx,
                    compare_res,
                    min_k,
                    player);
                KonaCongKonaAdapter::ss_vec_kona_l2<K>(
                    level_shares, chunk_idx, compare_res, player);
            }
        },
        [](std::vector<SharePair<K> >&,
           const std::vector<int>&) {});
}

// Prepared pure Rotated-MSB execution for phase-accounted Shao benchmarks.
// Network construction is outside the online timer. Every layer uses RMSB;
// this path has no DCF backend or width threshold. Existing chunk/swap stay.
struct PreparedPlan {
    KonaCongTopK::CongNetwork network;
    std::vector<std::vector<int>> layers;
};
inline PreparedPlan prepare_plan(int n,int k) {
    PreparedPlan plan;plan.network=KonaCongTopK::build_cong_network(n,k);
    for(const auto& level:plan.network.levels)
        plan.layers.push_back(KonaCongTopK::flatten_level(level));
    return plan;
}
template<int K,class Observer>
void execute_prepared(std::vector<SharePair<K>>& shares,const PreparedPlan& plan,
        bool min_k,RealTwoPartyPlayer* player,Observer observe,
        size_t batch_size=KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK)
{
    if(!batch_size)batch_size=KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK;
    batch_size=std::min(batch_size,KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK);
    for(size_t layer=0;layer<plan.layers.size();++layer) {
        const auto& idx=plan.layers[layer];const size_t pairs=idx.size()/2;
        for(size_t offset=0;offset<pairs;offset+=batch_size) {
            const size_t count=std::min(batch_size,pairs-offset);
            std::vector<int> chunk(idx.begin()+2*offset,idx.begin()+2*(offset+count));
            std::vector<Z2<K>> bits(chunk.size());
            observe(layer,pairs,count,[&]{
                KonaRotatedMsbCompare::rotated_msb_compare_in_vec_l2(shares,chunk,bits,min_k,player);
                KonaCongKonaAdapter::ss_vec_kona_l2<K>(shares,chunk,bits,player);
            });
        }
    }
    KonaCongTopK::move_output_wires_to_tail(shares,plan.network.output_wires);
}

} // namespace KonaCongRotatedMsbAdapter

#endif
