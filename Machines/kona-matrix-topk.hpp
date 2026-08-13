#ifndef MACHINES_KONA_MATRIX_TOPK_HPP_
#define MACHINES_KONA_MATRIX_TOPK_HPP_

#include <array>
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Math/Z2k.hpp"
#include "Networking/Player.h"
#include "Machines/kona-pcr-compare.hpp"
#include "Machines/kona-share-conversion.hpp"

namespace KonaMatrixTopK
{

template<int K>
using SharePair = std::array<Z2<K>, 2>;

inline std::pair<std::vector<int>, std::vector<int>> build_upper_triangle_pairs(int n)
{
    if (n <= 0)
        throw std::invalid_argument("n must be positive");

    std::vector<int> left_idx;
    std::vector<int> right_idx;
    left_idx.reserve(n * (n - 1) / 2);
    right_idx.reserve(n * (n - 1) / 2);

    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
        {
            left_idx.push_back(i);
            right_idx.push_back(j);
        }

    return {left_idx, right_idx};
}

template<int K>
inline void add_local_rank_to_global(
        std::vector<Z2<K>>& global_rank,
        int offset,
        const std::vector<Z2<K>>& local_rank)
{
    for (size_t i = 0; i < local_rank.size(); i++)
        global_rank[offset + i] += local_rank[i];
}

template<int K>
inline std::vector<Z2<K>> public_constants_batch(
        size_t n,
        uint64_t value,
        RealTwoPartyPlayer* player)
{
    std::vector<Z2<K>> values(n, Z2<K>(static_cast<mp_limb_t>(value)));
    return KonaShareConversion::public_to_additive_batch_l2<K>(values, player);
}

template<int K>
inline void secure_matrix_rank_with_pcr(
        const std::vector<SharePair<K>>& shares,
        std::vector<Z2<K>>& rank_share,
        RealTwoPartyPlayer* player,
        size_t tile_size = 128)
{
    const int n = (int)shares.size();
    rank_share.assign(n, Z2<K>(0));
    const size_t chunk_size = KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK;

    for (int block_i = 0; block_i < n; block_i += (int)tile_size)
    {
        const int a = std::min<int>((int)tile_size, n - block_i);

        // Diagonal tile: only compare the strict upper triangle inside the tile.
        {
            std::vector<int> local_left;
            std::vector<int> local_right;
            local_left.reserve(a * (a - 1) / 2);
            local_right.reserve(a * (a - 1) / 2);

            for (int p = 0; p < a; p++)
                for (int q = p + 1; q < a; q++)
                {
                    local_left.push_back(p);
                    local_right.push_back(q);
                }

            std::vector<Z2<K>> local_rank(a, Z2<K>(0));
            const size_t pair_count = local_left.size();
            for (size_t offset = 0; offset < pair_count; offset += chunk_size)
            {
                const size_t len = std::min(chunk_size, pair_count - offset);
                std::vector<Z2<K>> left_values(len);
                std::vector<Z2<K>> right_values(len);
                for (size_t i = 0; i < len; i++)
                {
                    const size_t p = offset + i;
                    left_values[i] = shares[block_i + local_left[p]][0];
                    right_values[i] = shares[block_i + local_right[p]][0];
                }

                // Stable tie-breaking: compare [right < left]. Equality yields 0,
                // so the smaller public index (left) wins.
                std::vector<Z2<K>> c = KonaPcrCompare::pcr_compare_gt_batch_l2(
                        left_values, right_values, player);
                std::vector<Z2<K>> ones = public_constants_batch<K>(len, 1, player);
                for (size_t i = 0; i < len; i++)
                {
                    const size_t p = offset + i;
                    local_rank[local_left[p]] += c[i];
                    local_rank[local_right[p]] += ones[i] - c[i];
                }
            }

            add_local_rank_to_global(rank_share, block_i, local_rank);
        }

        // Off-diagonal tiles: process one block pair at a time and reduce
        // contributions into two tile-local rank vectors.
        for (int block_j = block_i + a; block_j < n; block_j += (int)tile_size)
        {
            const int b = std::min<int>((int)tile_size, n - block_j);
            std::vector<Z2<K>> rank_a(a, Z2<K>(0));
            std::vector<Z2<K>> rank_b(b, Z2<K>(0));
            const size_t pair_count = (size_t)a * (size_t)b;

            for (size_t offset = 0; offset < pair_count; offset += chunk_size)
            {
                const size_t len = std::min(chunk_size, pair_count - offset);
                std::vector<Z2<K>> left_values(len);
                std::vector<Z2<K>> right_values(len);

                for (size_t i = 0; i < len; i++)
                {
                    const size_t flat = offset + i;
                    const int p = (int)(flat / b);
                    const int q = (int)(flat % b);
                    left_values[i] = shares[block_i + p][0];
                    right_values[i] = shares[block_j + q][0];
                }

                // Stable tie-breaking: block_i indices are always smaller than
                // block_j indices, so equality should favor the left tile.
                std::vector<Z2<K>> c = KonaPcrCompare::pcr_compare_gt_batch_l2(
                        left_values, right_values, player);
                std::vector<Z2<K>> ones = public_constants_batch<K>(len, 1, player);

                for (size_t i = 0; i < len; i++)
                {
                    const size_t flat = offset + i;
                    const int p = (int)(flat / b);
                    const int q = (int)(flat % b);
                    rank_a[p] += c[i];
                    rank_b[q] += ones[i] - c[i];
                }
            }

            add_local_rank_to_global(rank_share, block_i, rank_a);
            add_local_rank_to_global(rank_share, block_j, rank_b);
        }
    }
}

template<int K>
inline void secure_topk_mask_from_rank_with_pcr(
        const std::vector<Z2<K>>& rank_share,
        int k,
        std::vector<Z2<K>>& topk_mask_share,
        RealTwoPartyPlayer* player)
{
    assert(k > 0);
    std::vector<Z2<K>> public_k =
            public_constants_batch<K>(rank_share.size(), (uint64_t)k, player);
    topk_mask_share =
            KonaPcrCompare::pcr_compare_lt_batch_l2(rank_share, public_k, player);
}

template<int K>
inline void matrix_topk_vote_with_pcr(
        const std::vector<SharePair<K>>& shares,
        int k,
        int num_label,
        std::vector<Z2<K>>& rank_share,
        std::vector<Z2<K>>& topk_mask_share,
        std::vector<SharePair<K>>& label_count_array,
        RealTwoPartyPlayer* player,
        size_t tile_size = 128)
{
    if (num_label <= 0)
        throw std::invalid_argument("num_label must be positive");

    secure_matrix_rank_with_pcr<K>(shares, rank_share, player, tile_size);
    secure_topk_mask_from_rank_with_pcr<K>(
            rank_share, k, topk_mask_share, player);

    const size_t n = shares.size();
    std::vector<Z2<K>> labels(n);
    for (size_t i = 0; i < n; i++)
        labels[i] = shares[i][1];

    label_count_array.assign(num_label, SharePair<K>{Z2<K>(0), Z2<K>(0)});
    std::vector<Z2<K>> ones = public_constants_batch<K>(n, 1, player);

    for (int label = 0; label < num_label; label++)
    {
        std::vector<Z2<K>> public_label =
                public_constants_batch<K>(n, (uint64_t)label, player);

        std::vector<Z2<K>> lt =
                KonaPcrCompare::pcr_compare_lt_batch_l2(labels, public_label, player);
        std::vector<Z2<K>> gt =
                KonaPcrCompare::pcr_compare_gt_batch_l2(labels, public_label, player);

        std::vector<Z2<K>> eq(n);
        for (size_t i = 0; i < n; i++)
            eq[i] = ones[i] - lt[i] - gt[i];

        std::vector<Z2<K>> selected_eq;
        KonaShareConversion::mul_vector_additive_kona_l2_chunked<K>(
                topk_mask_share, eq, selected_eq, player);

        Z2<K> count(0);
        for (size_t i = 0; i < n; i++)
            count += selected_eq[i];

        label_count_array[label][0] = count;
        label_count_array[label][1] =
                KonaShareConversion::public_to_additive_l2<K>(
                        Z2<K>(static_cast<mp_limb_t>(label)), player);
    }
}

} // namespace KonaMatrixTopK

#endif /* MACHINES_KONA_MATRIX_TOPK_HPP_ */
