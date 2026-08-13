#ifndef MACHINES_KONA_SHARE_CONVERSION_HPP_
#define MACHINES_KONA_SHARE_CONVERSION_HPP_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

#include "Networking/Player.h"
#include "Tools/octetStream.h"
#include "Math/Z2k.hpp"

namespace KonaShareConversion
{

template<int K>
using RingShare = Z2<K>;

static const size_t KONA_DEFAULT_BATCH_CHUNK = 16348;

template<int K>
std::vector<RingShare<K>> open_additive_batch_l2(
        const std::vector<RingShare<K>>& local,
        RealTwoPartyPlayer* player);

template<int K>
std::vector<RingShare<K>> open_xor_batch_l2(
        const std::vector<RingShare<K>>& local,
        RealTwoPartyPlayer* player);

template<int K>
std::vector<RingShare<K>> open_subtractive_batch_l2(
        const std::vector<RingShare<K>>& local,
        RealTwoPartyPlayer* player);

template<int K>
std::vector<RingShare<K>> additive_to_xor_batch_l2(
        const std::vector<RingShare<K>>& additive_shares,
        RealTwoPartyPlayer* player);

template<int K>
std::vector<RingShare<K>> xor_to_additive_batch_l2(
        const std::vector<RingShare<K>>& xor_shares,
        RealTwoPartyPlayer* player);

template<int K>
std::vector<RingShare<K>> additive_to_subtractive_batch_l2(
        const std::vector<RingShare<K>>& additive_shares,
        RealTwoPartyPlayer* player);

template<int K>
std::vector<RingShare<K>> subtractive_to_additive_batch_l2(
        const std::vector<RingShare<K>>& subtractive_shares,
        RealTwoPartyPlayer* player);

template<int K>
std::vector<RingShare<K>> subtractive_to_xor_batch_l2(
        const std::vector<RingShare<K>>& subtractive_shares,
        RealTwoPartyPlayer* player);

template<int K>
std::vector<RingShare<K>> xor_to_subtractive_batch_l2(
        const std::vector<RingShare<K>>& xor_shares,
        RealTwoPartyPlayer* player);

template<int K>
void mul_additive_kona_l2(
        RingShare<K> x1,
        RingShare<K> x2,
        RingShare<K>& res,
        RealTwoPartyPlayer* player)
{
    RingShare<K> a(0), b(0), c(0);
    octetStream send_os, receive_os;
    (x1 - a).pack(send_os);
    (x2 - b).pack(send_os);
    player->send(send_os);
    player->receive(receive_os);

    RingShare<K> peer_e, peer_f;
    peer_e.unpack(receive_os);
    peer_f.unpack(receive_os);

    RingShare<K> e = peer_e + x1 - a;
    RingShare<K> f = peer_f + x2 - b;
    RingShare<K> r = f * a + e * b + c;
    if (player->my_num())
        r = r + e * f;
    res = r;
}

template<int K>
void mul_vector_additive_kona_l2(
        const std::vector<RingShare<K>>& v1,
        const std::vector<RingShare<K>>& v2,
        std::vector<RingShare<K>>& res,
        RealTwoPartyPlayer* player)
{
    assert(v1.size() == v2.size());
    res.resize(v1.size());

    RingShare<K> a(0), b(0), c(0);
    octetStream send_os, receive_os;
    for (size_t i = 0; i < v1.size(); i++)
    {
        (v1[i] - a).pack(send_os);
        (v2[i] - b).pack(send_os);
    }

    player->send(send_os);
    player->receive(receive_os);

    for (size_t i = 0; i < v1.size(); i++)
    {
        RingShare<K> peer_e, peer_f;
        peer_e.unpack(receive_os);
        peer_f.unpack(receive_os);

        RingShare<K> e = peer_e + v1[i] - a;
        RingShare<K> f = peer_f + v2[i] - b;
        RingShare<K> r = f * a + e * b + c;
        if (player->my_num())
            r = r + e * f;
        res[i] = r;
    }
}

template<int K>
void mul_vector_additive_kona_l2(
        const std::vector<RingShare<K>>& v1,
        const std::vector<RingShare<K>>& v2,
        std::vector<RingShare<K>>& res,
        bool double_res,
        RealTwoPartyPlayer* player)
{
    if (!double_res)
    {
        mul_vector_additive_kona_l2(v1, v2, res, player);
        return;
    }

    assert(v1.size() == v2.size() * 2);
    res.resize(v1.size());

    RingShare<K> a(0), b(0), c(0);
    octetStream send_os, receive_os;
    const size_t half_size = v2.size();

    for (size_t i = 0; i < half_size; i++)
    {
        (v1[i] - a).pack(send_os);
        (v2[i] - b).pack(send_os);
    }
    for (size_t i = 0; i < half_size; i++)
    {
        (v1[i + half_size] - a).pack(send_os);
        (v2[i] - b).pack(send_os);
    }

    player->send(send_os);
    player->receive(receive_os);

    std::vector<RingShare<K>> tmp(v1.size() * 2);
    for (size_t i = 0; i < half_size; i++)
    {
        tmp[2 * i].unpack(receive_os);
        tmp[2 * i + 1].unpack(receive_os);
        tmp[2 * i] = tmp[2 * i] + v1[i] - a;
        tmp[2 * i + 1] = tmp[2 * i + 1] + v2[i] - b;
    }
    for (size_t i = 0; i < half_size; i++)
    {
        RingShare<K> e = tmp[2 * i];
        RingShare<K> f = tmp[2 * i + 1];
        RingShare<K> r = f * a + e * b + c;
        if (player->my_num())
            r = r + e * f;
        res[i] = r;
    }

    for (size_t i = 0; i < half_size; i++)
    {
        tmp[2 * i].unpack(receive_os);
        tmp[2 * i + 1].unpack(receive_os);
        tmp[2 * i] = tmp[2 * i] + v1[i + half_size] - a;
        tmp[2 * i + 1] = tmp[2 * i + 1] + v2[i] - b;
    }
    for (size_t i = 0; i < half_size; i++)
    {
        RingShare<K> e = tmp[2 * i];
        RingShare<K> f = tmp[2 * i + 1];
        RingShare<K> r = f * a + e * b + c;
        if (player->my_num())
            r = r + e * f;
        res[i + half_size] = r;
    }
}

template<int K>
void mul_vector_additive_kona_l2_chunked(
        const std::vector<RingShare<K>>& v1,
        const std::vector<RingShare<K>>& v2,
        std::vector<RingShare<K>>& res,
        RealTwoPartyPlayer* player,
        size_t chunk_size = KONA_DEFAULT_BATCH_CHUNK)
{
    assert(v1.size() == v2.size());
    res.resize(v1.size());
    for (size_t offset = 0; offset < v1.size(); offset += chunk_size)
    {
        size_t len = std::min(chunk_size, v1.size() - offset);
        std::vector<RingShare<K>> v1_chunk(len), v2_chunk(len), out_chunk;
        for (size_t i = 0; i < len; i++)
        {
            v1_chunk[i] = v1[offset + i];
            v2_chunk[i] = v2[offset + i];
        }
        mul_vector_additive_kona_l2(v1_chunk, v2_chunk, out_chunk, player);
        for (size_t i = 0; i < len; i++)
            res[offset + i] = out_chunk[i];
    }
}

template<int K>
void mul_vector_additive_kona_l2_chunked(
        const std::vector<RingShare<K>>& v1,
        const std::vector<RingShare<K>>& v2,
        std::vector<RingShare<K>>& res,
        bool double_res,
        RealTwoPartyPlayer* player,
        size_t chunk_size = KONA_DEFAULT_BATCH_CHUNK)
{
    if (!double_res)
    {
        mul_vector_additive_kona_l2_chunked(v1, v2, res, player, chunk_size);
        return;
    }

    assert(v1.size() == v2.size() * 2);
    res.resize(v1.size());
    const size_t half = v2.size();
    for (size_t offset = 0; offset < half; offset += chunk_size)
    {
        size_t len = std::min(chunk_size, half - offset);
        std::vector<RingShare<K>> v1_chunk(len * 2), v2_chunk(len), out_chunk;
        for (size_t i = 0; i < len; i++)
        {
            v1_chunk[i] = v1[offset + i];
            v1_chunk[i + len] = v1[half + offset + i];
            v2_chunk[i] = v2[offset + i];
        }
        mul_vector_additive_kona_l2(
                v1_chunk, v2_chunk, out_chunk, true, player);
        for (size_t i = 0; i < len; i++)
        {
            res[offset + i] = out_chunk[i];
            res[half + offset + i] = out_chunk[len + i];
        }
    }
}

template<int K>
std::vector<RingShare<K>> exchange_ring_vector_l2(
        const std::vector<RingShare<K>>& local,
        RealTwoPartyPlayer* player)
{
    octetStream send_os, receive_os;
    for (auto& x : local)
        x.pack(send_os);

    player->send(send_os);
    player->receive(receive_os);

    std::vector<RingShare<K>> peer(local.size());
    for (auto& x : peer)
        x.unpack(receive_os);

    return peer;
}

template<int K>
RingShare<K> open_additive_l2(RingShare<K> local, RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> opened =
            open_additive_batch_l2<K>({local}, player);
    return opened[0];
}

template<int K>
std::vector<RingShare<K>> open_additive_batch_l2(
        const std::vector<RingShare<K>>& local,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> opened(local.size());
    for (size_t offset = 0; offset < local.size(); offset += KONA_DEFAULT_BATCH_CHUNK)
    {
        size_t len = std::min(KONA_DEFAULT_BATCH_CHUNK, local.size() - offset);
        std::vector<RingShare<K>> chunk(len);
        for (size_t i = 0; i < len; i++)
            chunk[i] = local[offset + i];
        auto peer = exchange_ring_vector_l2<K>(chunk, player);
        for (size_t i = 0; i < len; i++)
            opened[offset + i] = chunk[i] + peer[i];
    }
    return opened;
}

template<int K>
RingShare<K> open_xor_l2(RingShare<K> local, RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> opened = open_xor_batch_l2<K>({local}, player);
    return opened[0];
}

template<int K>
std::vector<RingShare<K>> open_xor_batch_l2(
        const std::vector<RingShare<K>>& local,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> opened(local.size());
    for (size_t offset = 0; offset < local.size(); offset += KONA_DEFAULT_BATCH_CHUNK)
    {
        size_t len = std::min(KONA_DEFAULT_BATCH_CHUNK, local.size() - offset);
        std::vector<RingShare<K>> chunk(len);
        for (size_t i = 0; i < len; i++)
            chunk[i] = local[offset + i];
        auto peer = exchange_ring_vector_l2<K>(chunk, player);
        for (size_t i = 0; i < len; i++)
            opened[offset + i] = chunk[i] ^ peer[i];
    }
    return opened;
}

template<int K>
RingShare<K> open_subtractive_l2(
        RingShare<K> local,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> opened =
            open_subtractive_batch_l2<K>({local}, player);
    return opened[0];
}

template<int K>
std::vector<RingShare<K>> open_subtractive_batch_l2(
        const std::vector<RingShare<K>>& local,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> opened(local.size());
    for (size_t offset = 0; offset < local.size(); offset += KONA_DEFAULT_BATCH_CHUNK)
    {
        size_t len = std::min(KONA_DEFAULT_BATCH_CHUNK, local.size() - offset);
        std::vector<RingShare<K>> chunk(len);
        for (size_t i = 0; i < len; i++)
            chunk[i] = local[offset + i];
        auto peer = exchange_ring_vector_l2<K>(chunk, player);
        for (size_t i = 0; i < len; i++)
        {
            if (player->my_num() == 0)
                opened[offset + i] = peer[i] - chunk[i];
            else
                opened[offset + i] = chunk[i] - peer[i];
        }
    }
    return opened;
}

template<int K>
RingShare<K> public_to_additive_l2(
        RingShare<K> value,
        RealTwoPartyPlayer* player)
{
    return player->my_num() == 0 ? RingShare<K>(0) : value;
}

template<int K>
std::vector<RingShare<K>> public_to_additive_batch_l2(
        const std::vector<RingShare<K>>& values,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> result(values.size());
    for (size_t i = 0; i < values.size(); i++)
        result[i] = public_to_additive_l2<K>(values[i], player);
    return result;
}

template<int K>
RingShare<K> public_to_xor_l2(
        RingShare<K> value,
        RealTwoPartyPlayer* player)
{
    return player->my_num() == 0 ? RingShare<K>(0) : value;
}

template<int K>
std::vector<RingShare<K>> public_to_xor_batch_l2(
        const std::vector<RingShare<K>>& values,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> result(values.size());
    for (size_t i = 0; i < values.size(); i++)
        result[i] = public_to_xor_l2<K>(values[i], player);
    return result;
}

template<int K>
RingShare<K> public_to_subtractive_l2(
        RingShare<K> value,
        RealTwoPartyPlayer* player)
{
    return player->my_num() == 0 ? RingShare<K>(0) : value;
}

template<int K>
std::vector<RingShare<K>> public_to_subtractive_batch_l2(
        const std::vector<RingShare<K>>& values,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> result(values.size());
    for (size_t i = 0; i < values.size(); i++)
        result[i] = public_to_subtractive_l2<K>(values[i], player);
    return result;
}

template<int K>
RingShare<K> additive_to_xor_l2(
        RingShare<K> additive_share,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> result =
            additive_to_xor_batch_l2<K>({additive_share}, player);
    return result[0];
}

template<int K>
std::vector<RingShare<K>> additive_to_xor_batch_l2(
        const std::vector<RingShare<K>>& additive_shares,
        RealTwoPartyPlayer* player)
{
    auto opened = open_additive_batch_l2<K>(additive_shares, player);
    return public_to_xor_batch_l2<K>(opened, player);
}

template<int K>
RingShare<K> xor_to_additive_l2(
        RingShare<K> xor_share,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> result =
            xor_to_additive_batch_l2<K>({xor_share}, player);
    return result[0];
}

template<int K>
std::vector<RingShare<K>> xor_to_additive_batch_l2(
        const std::vector<RingShare<K>>& xor_shares,
        RealTwoPartyPlayer* player)
{
    auto opened = open_xor_batch_l2<K>(xor_shares, player);
    return public_to_additive_batch_l2<K>(opened, player);
}

template<int K>
RingShare<K> additive_to_subtractive_l2(
        RingShare<K> additive_share,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> result =
            additive_to_subtractive_batch_l2<K>({additive_share}, player);
    return result[0];
}

template<int K>
std::vector<RingShare<K>> additive_to_subtractive_batch_l2(
        const std::vector<RingShare<K>>& additive_shares,
        RealTwoPartyPlayer* player)
{
    auto opened = open_additive_batch_l2<K>(additive_shares, player);
    return public_to_subtractive_batch_l2<K>(opened, player);
}

template<int K>
RingShare<K> subtractive_to_additive_l2(
        RingShare<K> subtractive_share,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> result =
            subtractive_to_additive_batch_l2<K>({subtractive_share}, player);
    return result[0];
}

template<int K>
std::vector<RingShare<K>> subtractive_to_additive_batch_l2(
        const std::vector<RingShare<K>>& subtractive_shares,
        RealTwoPartyPlayer* player)
{
    auto opened = open_subtractive_batch_l2<K>(subtractive_shares, player);
    return public_to_additive_batch_l2<K>(opened, player);
}

template<int K>
RingShare<K> subtractive_to_xor_l2(
        RingShare<K> subtractive_share,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> result =
            subtractive_to_xor_batch_l2<K>({subtractive_share}, player);
    return result[0];
}

template<int K>
std::vector<RingShare<K>> subtractive_to_xor_batch_l2(
        const std::vector<RingShare<K>>& subtractive_shares,
        RealTwoPartyPlayer* player)
{
    auto opened = open_subtractive_batch_l2<K>(subtractive_shares, player);
    return public_to_xor_batch_l2<K>(opened, player);
}

template<int K>
RingShare<K> xor_to_subtractive_l2(
        RingShare<K> xor_share,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> result =
            xor_to_subtractive_batch_l2<K>({xor_share}, player);
    return result[0];
}

template<int K>
std::vector<RingShare<K>> xor_to_subtractive_batch_l2(
        const std::vector<RingShare<K>>& xor_shares,
        RealTwoPartyPlayer* player)
{
    auto opened = open_xor_batch_l2<K>(xor_shares, player);
    return public_to_subtractive_batch_l2<K>(opened, player);
}

template<int K>
RingShare<K> arithmetic_bit_to_xor_bit_l2(
        RingShare<K> arithmetic_bit_share,
        RealTwoPartyPlayer* player)
{
    RingShare<K> opened = open_additive_l2<K>(arithmetic_bit_share, player);
    opened = RingShare<K>(opened.get_bit(0));
    return public_to_xor_l2<K>(opened, player);
}

template<int K>
std::vector<RingShare<K>> arithmetic_bit_to_xor_bit_batch_l2(
        const std::vector<RingShare<K>>& arithmetic_bit_shares,
        RealTwoPartyPlayer* player)
{
    auto opened = open_additive_batch_l2<K>(arithmetic_bit_shares, player);
    for (auto& x : opened)
        x = RingShare<K>(x.get_bit(0));
    return public_to_xor_batch_l2<K>(opened, player);
}

template<int K>
RingShare<K> xor_bit_to_arithmetic_bit_l2(
        RingShare<K> xor_bit_share,
        RealTwoPartyPlayer* player)
{
    RingShare<K> local_bit(xor_bit_share.get_bit(0));
    RingShare<K> b0 = player->my_num() == 0 ? local_bit : RingShare<K>(0);
    RingShare<K> b1 = player->my_num() == 1 ? local_bit : RingShare<K>(0);

    RingShare<K> product;
    mul_additive_kona_l2<K>(b0, b1, product, player);
    return b0 + b1 - RingShare<K>(2) * product;
}

template<int K>
std::vector<RingShare<K>> xor_bit_to_arithmetic_bit_batch_l2(
        const std::vector<RingShare<K>>& xor_bit_shares,
        RealTwoPartyPlayer* player)
{
    std::vector<RingShare<K>> b0(xor_bit_shares.size(), RingShare<K>(0));
    std::vector<RingShare<K>> b1(xor_bit_shares.size(), RingShare<K>(0));

    for (size_t i = 0; i < xor_bit_shares.size(); i++)
    {
        RingShare<K> local_bit(xor_bit_shares[i].get_bit(0));
        if (player->my_num() == 0)
            b0[i] = local_bit;
        else
            b1[i] = local_bit;
    }

    std::vector<RingShare<K>> products;
    mul_vector_additive_kona_l2_chunked<K>(b0, b1, products, player);

    std::vector<RingShare<K>> result(xor_bit_shares.size());
    for (size_t i = 0; i < xor_bit_shares.size(); i++)
        result[i] = b0[i] + b1[i] - RingShare<K>(2) * products[i];
    return result;
}

// Kona/PCR-facing names. A2B still uses the L2 open-and-reshare bridge for
// full ring values. B2A is the Boolean-bit conversion b=b0+b1-2*b0*b1.
template<int K>
RingShare<K> A2B_l2(RingShare<K> arithmetic_share, RealTwoPartyPlayer* player)
{
    return additive_to_xor_l2<K>(arithmetic_share, player);
}

template<int K>
std::vector<RingShare<K>> A2B_batch_l2(
        const std::vector<RingShare<K>>& arithmetic_shares,
        RealTwoPartyPlayer* player)
{
    return additive_to_xor_batch_l2<K>(arithmetic_shares, player);
}

template<int K>
RingShare<K> B2A_l2(RingShare<K> xor_share, RealTwoPartyPlayer* player)
{
    return xor_bit_to_arithmetic_bit_l2<K>(xor_share, player);
}

template<int K>
std::vector<RingShare<K>> B2A_batch_l2(
        const std::vector<RingShare<K>>& xor_shares,
        RealTwoPartyPlayer* player)
{
    return xor_bit_to_arithmetic_bit_batch_l2<K>(xor_shares, player);
}

template<int K>
RingShare<K> ABit2BBit_l2(
        RingShare<K> arithmetic_bit_share,
        RealTwoPartyPlayer* player)
{
    return arithmetic_bit_to_xor_bit_l2<K>(arithmetic_bit_share, player);
}

template<int K>
std::vector<RingShare<K>> ABit2BBit_batch_l2(
        const std::vector<RingShare<K>>& arithmetic_bit_shares,
        RealTwoPartyPlayer* player)
{
    return arithmetic_bit_to_xor_bit_batch_l2<K>(
            arithmetic_bit_shares, player);
}

template<int K>
RingShare<K> BBit2ABit_l2(
        RingShare<K> xor_bit_share,
        RealTwoPartyPlayer* player)
{
    return xor_bit_to_arithmetic_bit_l2<K>(xor_bit_share, player);
}

template<int K>
std::vector<RingShare<K>> BBit2ABit_batch_l2(
        const std::vector<RingShare<K>>& xor_bit_shares,
        RealTwoPartyPlayer* player)
{
    return xor_bit_to_arithmetic_bit_batch_l2<K>(xor_bit_shares, player);
}

} // namespace KonaShareConversion

#endif /* MACHINES_KONA_SHARE_CONVERSION_HPP_ */
