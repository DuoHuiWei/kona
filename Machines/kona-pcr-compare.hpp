#ifndef MACHINES_KONA_PCR_COMPARE_HPP_
#define MACHINES_KONA_PCR_COMPARE_HPP_

#include <cassert>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Networking/Player.h"
#include "Tools/octetStream.h"
#include "Math/Z2k.hpp"
#include "Machines/kona-active-bitpack.hpp"
#include "Machines/kona-share-conversion.hpp"

namespace KonaPcrCompare
{

static const int PCR_K = 64;
static const uint64_t PCR_MSB_MASK = 0x8000000000000000ULL;
static const uint64_t PCR_LOW63_MASK = 0x7fffffffffffffffULL;

inline uint64_t z2_to_u64(const Z2<PCR_K>& x)
{
    return static_cast<uint64_t>(x.get_limb(0));
}

inline Z2<PCR_K> u64_to_z2(uint64_t x)
{
    return Z2<PCR_K>(static_cast<mp_limb_t>(x));
}

inline void pack_u64(octetStream& os, uint64_t x)
{
    os.store_int(static_cast<size_t>(x), 8);
}

inline uint64_t unpack_u64(octetStream& os)
{
    return static_cast<uint64_t>(os.get_int(8));
}

inline uint64_t endpoint_mask(int distance)
{
    return KonaActiveBitpack::endpoint_mask(distance);
}

inline uint64_t kogge_stone_active_mask(int distance)
{
    assert(distance > 0 && distance < PCR_K && (distance & (distance - 1)) == 0);
    return ~((uint64_t(1) << distance) - 1);
}

// Packed Boolean Beaver AND over XOR shares. The current L2 implementation
// uses a=b=c=0, matching Kona's existing "protocol-shaped" multiplication.
inline void xor_and_packed_batch_l2(
        const std::vector<uint64_t>& x,
        const std::vector<uint64_t>& y,
        std::vector<uint64_t>& z,
        RealTwoPartyPlayer* player)
{
    assert(x.size() == y.size());
    z.resize(x.size());

    octetStream send_os, receive_os;
    for (size_t i = 0; i < x.size(); i++)
    {
        pack_u64(send_os, x[i]);
        pack_u64(send_os, y[i]);
    }

    player->send(send_os);
    player->receive(receive_os);

    for (size_t i = 0; i < x.size(); i++)
    {
        uint64_t peer_d = unpack_u64(receive_os);
        uint64_t peer_e = unpack_u64(receive_os);
        uint64_t d = x[i] ^ peer_d;
        uint64_t e = y[i] ^ peer_e;
        z[i] = player->my_num() == 0 ? (d & e) : 0;
    }
}

inline std::vector<uint64_t> xor_and_packed_batch_l2(
        const std::vector<uint64_t>& x,
        const std::vector<uint64_t>& y,
        RealTwoPartyPlayer* player)
{
    std::vector<uint64_t> z;
    xor_and_packed_batch_l2(x, y, z, player);
    return z;
}

inline void xor_and_packed_batch_endpoint_l2(
        const std::vector<uint64_t>& x,
        const std::vector<uint64_t>& y,
        std::vector<uint64_t>& z,
        int distance,
        RealTwoPartyPlayer* player)
{
    assert(x.size() == y.size());
    z.resize(x.size());

    const int n_bytes = KonaActiveBitpack::endpoint_packed_bytes(distance);
    assert(n_bytes > 0 && n_bytes <= 8);

    octetStream send_os, receive_os;
    for (size_t i = 0; i < x.size(); i++)
    {
        uint64_t dense_x =
                KonaActiveBitpack::compact_endpoint_bits(x[i], distance);
        uint64_t dense_y =
                KonaActiveBitpack::compact_endpoint_bits(y[i], distance);
        send_os.store_int(static_cast<size_t>(dense_x), n_bytes);
        send_os.store_int(static_cast<size_t>(dense_y), n_bytes);
    }

    player->send(send_os);
    player->receive(receive_os);

    for (size_t i = 0; i < x.size(); i++)
    {
        uint64_t local_x =
                KonaActiveBitpack::compact_endpoint_bits(x[i], distance);
        uint64_t local_y =
                KonaActiveBitpack::compact_endpoint_bits(y[i], distance);
        uint64_t peer_x = static_cast<uint64_t>(receive_os.get_int(n_bytes));
        uint64_t peer_y = static_cast<uint64_t>(receive_os.get_int(n_bytes));
        uint64_t d = local_x ^ peer_x;
        uint64_t e = local_y ^ peer_y;
        uint64_t dense_z = player->my_num() == 0 ? (d & e) : 0;
        z[i] = KonaActiveBitpack::expand_endpoint_bits(dense_z, distance);
    }
}

inline std::vector<uint64_t> xor_and_packed_batch_endpoint_l2(
        const std::vector<uint64_t>& x,
        const std::vector<uint64_t>& y,
        int distance,
        RealTwoPartyPlayer* player)
{
    std::vector<uint64_t> z;
    xor_and_packed_batch_endpoint_l2(x, y, z, distance, player);
    return z;
}

inline std::vector<uint64_t> shift_left_masked(
        const std::vector<uint64_t>& values,
        int shift,
        uint64_t mask)
{
    std::vector<uint64_t> result(values.size());
    for (size_t i = 0; i < values.size(); i++)
        result[i] = (values[i] << shift) & mask;
    return result;
}

inline std::vector<uint64_t> mask_batch(
        const std::vector<uint64_t>& values,
        uint64_t mask)
{
    std::vector<uint64_t> result(values.size());
    for (size_t i = 0; i < values.size(); i++)
        result[i] = values[i] & mask;
    return result;
}

inline void shift_left_masked_into(
        const std::vector<uint64_t>& values,
        int shift,
        uint64_t mask,
        std::vector<uint64_t>& result)
{
    result.resize(values.size());
    for (size_t i = 0; i < values.size(); i++)
        result[i] = (values[i] << shift) & mask;
}

inline void mask_batch_into(
        const std::vector<uint64_t>& values,
        uint64_t mask,
        std::vector<uint64_t>& result)
{
    result.resize(values.size());
    for (size_t i = 0; i < values.size(); i++)
        result[i] = values[i] & mask;
}

struct PcrCarryWorkspace
{
    std::vector<uint64_t> p0;
    std::vector<uint64_t> p;
    std::vector<uint64_t> g;
    std::vector<uint64_t> high_g;
    std::vector<uint64_t> high_p;
    std::vector<uint64_t> low_g;
    std::vector<uint64_t> low_p;
    std::vector<uint64_t> and_left;
    std::vector<uint64_t> and_right;
    std::vector<uint64_t> and_result;
    std::vector<uint64_t> root_masked_g;
    std::vector<uint64_t> root_masked_p;
    std::vector<uint64_t> root_product;
    std::vector<uint64_t> carry_share;

    void resize(size_t n)
    {
        p0.resize(n);
        p.resize(n);
        g.resize(n);
        high_g.resize(n);
        high_p.resize(n);
        low_g.resize(n);
        low_p.resize(n);
        and_left.resize(2 * n);
        and_right.resize(2 * n);
        root_masked_g.resize(n);
        root_masked_p.resize(n);
        root_product.resize(n);
        carry_share.resize(n);
    }
};

// Sparse final-carry tree from the PCR reference. It computes only carry64
// for adding two XOR-shared uint64 words, not the full carry vector.
inline std::vector<uint64_t> pcr_final_carry64_xor_batch_l2(
        const std::vector<uint64_t>& u_share,
        const std::vector<uint64_t>& v_share,
        RealTwoPartyPlayer* player)
{
    assert(u_share.size() == v_share.size());
    const size_t n = u_share.size();
    PcrCarryWorkspace ws;
    ws.resize(n);
    for (size_t i = 0; i < n; i++)
    {
        ws.p0[i] = u_share[i] ^ v_share[i];
        ws.p[i] = ws.p0[i];
    }

    ws.g = xor_and_packed_batch_l2(u_share, v_share, player);

    for (int distance = 1; distance <= 16; distance <<= 1)
    {
        uint64_t active_mask = endpoint_mask(distance);
        mask_batch_into(ws.g, active_mask, ws.high_g);
        mask_batch_into(ws.p, active_mask, ws.high_p);
        shift_left_masked_into(ws.g, distance, active_mask, ws.low_g);
        shift_left_masked_into(ws.p, distance, active_mask, ws.low_p);

        for (size_t i = 0; i < n; i++)
        {
            ws.and_left[i] = ws.high_p[i];
            ws.and_right[i] = ws.low_g[i];
            ws.and_left[i + n] = ws.high_p[i];
            ws.and_right[i + n] = ws.low_p[i];
        }

        ws.and_result = xor_and_packed_batch_endpoint_l2(
                ws.and_left, ws.and_right, distance, player);

        for (size_t i = 0; i < n; i++)
        {
            ws.g[i] = (ws.high_g[i] ^ ws.and_result[i]) & active_mask;
            ws.p[i] = ws.and_result[i + n] & active_mask;
        }
    }

    mask_batch_into(ws.g, PCR_MSB_MASK, ws.root_masked_g);
    mask_batch_into(ws.p, PCR_MSB_MASK, ws.root_masked_p);
    shift_left_masked_into(ws.g, 32, PCR_MSB_MASK, ws.low_g);
    ws.root_product = xor_and_packed_batch_endpoint_l2(
            ws.root_masked_p, ws.low_g, 32, player);

    for (size_t i = 0; i < n; i++)
    {
        uint64_t root_g = ws.root_masked_g[i] ^ ws.root_product[i];
        ws.carry_share[i] = (root_g >> 63) & 1ULL;
    }

    return ws.carry_share;
}

// Adds two XOR-shared uint64 values using a full 6-layer packed Kogge-Stone
// prefix. Kept for unit testing the packed Boolean AND/carry machinery; the
// comparator below uses the sparse final-carry tree instead.
inline std::vector<uint64_t> pcr_add_uint64_xor_batch_l2(
        const std::vector<uint64_t>& u_share,
        const std::vector<uint64_t>& v_share,
        RealTwoPartyPlayer* player)
{
    assert(u_share.size() == v_share.size());
    const size_t n = u_share.size();

    std::vector<uint64_t> p0(n), p(n);
    for (size_t i = 0; i < n; i++)
    {
        p0[i] = u_share[i] ^ v_share[i];
        p[i] = p0[i];
    }

    std::vector<uint64_t> g = xor_and_packed_batch_l2(
            u_share, v_share, player);

    for (int distance = 1; distance < PCR_K; distance <<= 1)
    {
        uint64_t active_mask = kogge_stone_active_mask(distance);
        std::vector<uint64_t> high_g = mask_batch(g, active_mask);
        std::vector<uint64_t> high_p = mask_batch(p, active_mask);
        std::vector<uint64_t> low_g =
                shift_left_masked(g, distance, active_mask);
        std::vector<uint64_t> low_p =
                shift_left_masked(p, distance, active_mask);

        std::vector<uint64_t> and_left(2 * n);
        std::vector<uint64_t> and_right(2 * n);
        for (size_t i = 0; i < n; i++)
        {
            and_left[i] = high_p[i];
            and_right[i] = low_g[i];
            and_left[i + n] = high_p[i];
            and_right[i + n] = low_p[i];
        }

        std::vector<uint64_t> and_result =
                xor_and_packed_batch_l2(and_left, and_right, player);

        for (size_t i = 0; i < n; i++)
        {
            uint64_t inactive = ~active_mask;
            g[i] = (g[i] & inactive) |
                    ((high_g[i] ^ and_result[i]) & active_mask);
            p[i] = (p[i] & inactive) |
                    (and_result[i + n] & active_mask);
        }
    }

    std::vector<uint64_t> sum_share(n);
    for (size_t i = 0; i < n; i++)
        sum_share[i] = p0[i] ^ (g[i] << 1);

    return sum_share;
}

inline std::vector<Z2<PCR_K>> pcr_compare_lt_batch_l2(
        const std::vector<Z2<PCR_K>>& x_shares,
        const std::vector<Z2<PCR_K>>& y_shares,
        RealTwoPartyPlayer* player)
{
    // Correctness precondition: this L2 comparator interprets x-y via the
    // sign bit of (x-y) mod 2^64, so it matches unsigned x<y only when the
    // true distance between compared values does not cross the half ring:
    // |x-y| < 2^63. This is intended for Kona distance-ranking values with
    // bounded range, not arbitrary full-domain uint64_t ordering.
    assert(x_shares.size() == y_shares.size());
    const size_t n = x_shares.size();

    std::vector<uint64_t> low_shifted_u_share(n, 0);
    std::vector<uint64_t> low_shifted_v_share(n, 0);
    std::vector<uint64_t> local_msb_share(n, 0);
    for (size_t i = 0; i < n; i++)
    {
        uint64_t local_diff = z2_to_u64(x_shares[i] - y_shares[i]);
        if (player->my_num() == 0)
        {
            low_shifted_u_share[i] = (local_diff & PCR_LOW63_MASK) << 1;
        }
        else
        {
            low_shifted_v_share[i] = (local_diff & PCR_LOW63_MASK) << 1;
        }
        local_msb_share[i] = (local_diff >> 63) & 1ULL;
    }

    std::vector<uint64_t> carry_into_msb_share =
            pcr_final_carry64_xor_batch_l2(
                    low_shifted_u_share, low_shifted_v_share, player);

    std::vector<Z2<PCR_K>> sign_xor_bits(n);
    for (size_t i = 0; i < n; i++)
        sign_xor_bits[i] = u64_to_z2(
                local_msb_share[i] ^ carry_into_msb_share[i]);

    return KonaShareConversion::B2A_batch_l2<PCR_K>(sign_xor_bits, player);
}

inline std::vector<Z2<PCR_K>> pcr_compare_gt_batch_l2(
        const std::vector<Z2<PCR_K>>& x_shares,
        const std::vector<Z2<PCR_K>>& y_shares,
        RealTwoPartyPlayer* player)
{
    return pcr_compare_lt_batch_l2(y_shares, x_shares, player);
}

inline void pcr_compare_in_vec_l2(
        const std::vector<Z2<PCR_K>>& shares,
        const std::vector<int>& compare_idx_vec,
        std::vector<Z2<PCR_K>>& compare_res,
        bool greater_than,
        RealTwoPartyPlayer* player)
{
    // Drop-in replacement shape for Kona compare_in_vec(). The returned
    // compare_res entries are arithmetic Z2<64> bit shares duplicated as
    // compare_res[2*i] and compare_res[2*i+1], matching SS_vec().
    // See pcr_compare_lt_batch_l2() for the |x-y| < 2^63 precondition.
    assert(compare_idx_vec.size() && compare_idx_vec.size() == compare_res.size());
    const size_t n = compare_idx_vec.size() / 2;
    std::vector<Z2<PCR_K>> left(n);
    std::vector<Z2<PCR_K>> right(n);

    for (size_t i = 0; i < n; i++)
    {
        left[i] = shares[compare_idx_vec[2 * i]];
        right[i] = shares[compare_idx_vec[2 * i + 1]];
    }

    std::vector<Z2<PCR_K>> bits = greater_than ?
            pcr_compare_gt_batch_l2(left, right, player) :
            pcr_compare_lt_batch_l2(left, right, player);

    for (size_t i = 0; i < n; i++)
    {
        compare_res[2 * i] = bits[i];
        compare_res[2 * i + 1] = bits[i];
    }
}

inline void pcr_compare_in_vec_l2(
        const std::vector<std::array<Z2<PCR_K>, 2>>& shares,
        const std::vector<int>& compare_idx_vec,
        std::vector<Z2<PCR_K>>& compare_res,
        bool greater_than,
        RealTwoPartyPlayer* player)
{
    // Same interface as the array-valued Kona compare_in_vec(): compare only
    // shares[*][0] and leave labels to SS_vec(). Output is an arithmetic
    // comparison bit share duplicated per pair. Requires |x-y| < 2^63.
    assert(compare_idx_vec.size() && compare_idx_vec.size() == compare_res.size());
    const size_t n = compare_idx_vec.size() / 2;
    std::vector<Z2<PCR_K>> left(n);
    std::vector<Z2<PCR_K>> right(n);

    for (size_t i = 0; i < n; i++)
    {
        left[i] = shares[compare_idx_vec[2 * i]][0];
        right[i] = shares[compare_idx_vec[2 * i + 1]][0];
    }

    std::vector<Z2<PCR_K>> bits = greater_than ?
            pcr_compare_gt_batch_l2(left, right, player) :
            pcr_compare_lt_batch_l2(left, right, player);

    for (size_t i = 0; i < n; i++)
    {
        compare_res[2 * i] = bits[i];
        compare_res[2 * i + 1] = bits[i];
    }
}

} // namespace KonaPcrCompare

#endif /* MACHINES_KONA_PCR_COMPARE_HPP_ */
