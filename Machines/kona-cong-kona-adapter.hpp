#ifndef MACHINES_KONA_CONG_KONA_ADAPTER_HPP_
#define MACHINES_KONA_CONG_KONA_ADAPTER_HPP_

#include <array>
#include <vector>

#include "Math/Z2k.hpp"
#include "Networking/Player.h"
#include "Machines/kona-cong-topk.hpp"
#include "Machines/kona-dcf-compare.hpp"
#include "Machines/kona-pcr-compare.hpp"
#include "Machines/kona-share-conversion.hpp"

namespace KonaCongKonaAdapter
{

template<int K>
using SharePair = std::array<Z2<K>, 2>;

// Standalone Kona-style SS_vec implementation so Cong can reuse the same
// conditional exchange rule without depending on KNN_party_base methods.
template<int K>
inline void ss_vec_kona_l2(
        std::vector<SharePair<K>>& shares,
        const std::vector<int>& compare_idx_vec,
        const std::vector<Z2<K>>& compare_res,
        RealTwoPartyPlayer* player)
{
    assert(compare_idx_vec.size() && compare_idx_vec.size() == compare_res.size());
    const int size_of_cur_cmp = (int)compare_idx_vec.size();

    std::vector<Z2<K>> tmp_ss;
    tmp_ss.reserve(size_of_cur_cmp * 2);
    for (int i = 0; i < size_of_cur_cmp; i++)
        tmp_ss.push_back(shares[compare_idx_vec[i]][0]);
    for (int i = 0; i < size_of_cur_cmp; i++)
        tmp_ss.push_back(shares[compare_idx_vec[i]][1]);

    std::vector<Z2<K>> tmp_res(size_of_cur_cmp * 2);
    KonaShareConversion::mul_vector_additive_kona_l2_chunked<K>(
            tmp_ss, compare_res, tmp_res, true, player);

    for (int i = 0; i < size_of_cur_cmp / 2; i++)
    {
        tmp_ss[2 * i] =
                tmp_ss[2 * i] - tmp_res[2 * i] + tmp_res[2 * i + 1];
        tmp_ss[2 * i + 1] =
                tmp_ss[2 * i + 1] + tmp_res[2 * i] - tmp_res[2 * i + 1];
    }
    for (int i = 0; i < size_of_cur_cmp; i++)
        shares[compare_idx_vec[i]][0] = tmp_ss[i];

    for (int i = 0; i < size_of_cur_cmp / 2; i++)
    {
        tmp_ss[2 * i + size_of_cur_cmp] =
                tmp_ss[2 * i + size_of_cur_cmp] -
                tmp_res[2 * i + size_of_cur_cmp] +
                tmp_res[2 * i + 1 + size_of_cur_cmp];
        tmp_ss[2 * i + 1 + size_of_cur_cmp] =
                tmp_ss[2 * i + 1 + size_of_cur_cmp] +
                tmp_res[2 * i + size_of_cur_cmp] -
                tmp_res[2 * i + 1 + size_of_cur_cmp];
    }
    for (int i = 0; i < size_of_cur_cmp; i++)
        shares[compare_idx_vec[i]][1] = tmp_ss[i + size_of_cur_cmp];
}

template<int K>
inline void cong_top_k_with_pcr(
        std::vector<SharePair<K>>& shares,
        int k,
        bool min_k,
        RealTwoPartyPlayer* player)
{
    KonaCongTopK::cong_top_k<SharePair<K>>(
            shares,
            (int)shares.size(),
            k,
            [&](std::vector<SharePair<K>>& current_shares,
                    const std::vector<int>& compare_idx_vec) {
                std::vector<Z2<K>> compare_res(compare_idx_vec.size());
                KonaPcrCompare::pcr_compare_in_vec_l2(
                        current_shares,
                        compare_idx_vec,
                        compare_res,
                        min_k,
                        player);
                ss_vec_kona_l2<K>(
                        current_shares, compare_idx_vec, compare_res, player);
            },
            [&](std::vector<SharePair<K>>&,
                    const std::vector<int>&) {});
}

template<int K>
inline void cong_top_k_with_dcf(
        std::vector<SharePair<K>>& shares,
        int k,
        bool min_k,
        KonaDcfCompare::Compare64<K>& dcf_compare,
        RealTwoPartyPlayer* player)
{
    KonaCongTopK::cong_top_k<SharePair<K>>(
            shares,
            (int)shares.size(),
            k,
            [&](std::vector<SharePair<K>>& current_shares,
                    const std::vector<int>& compare_idx_vec) {
                std::vector<Z2<K>> compare_res(compare_idx_vec.size());
                dcf_compare.compare_in_vec(
                        current_shares,
                        compare_idx_vec,
                        compare_res,
                        min_k);
                ss_vec_kona_l2<K>(
                        current_shares, compare_idx_vec, compare_res, player);
            },
            [&](std::vector<SharePair<K>>&,
                    const std::vector<int>&) {});
}

} // namespace KonaCongKonaAdapter

#endif /* MACHINES_KONA_CONG_KONA_ADAPTER_HPP_ */
