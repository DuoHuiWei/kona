#ifndef MACHINES_KONA_DCF_COMPARE_HPP_
#define MACHINES_KONA_DCF_COMPARE_HPP_

#include <cassert>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Networking/Player.h"
#include "Tools/random.h"
#include "Math/bigint.h"
#include "Math/Z2k.hpp"

namespace KonaDcfCompare
{

template<int K>
class Compare64
{
    static_assert(K == 64, "KonaDcfCompare::Compare64 currently targets Z2<64>");

    static const int LAMBDA_BYTES = 16;
    static const size_t MAX_COMPARE_CHUNK = 16348;

public:
    struct Stats
    {
        long long evaluate_calls = 0;
        std::chrono::duration<double> evaluate_time =
                std::chrono::duration<double>::zero();
    };

private:
    struct Node
    {
        uint64_t scw = 0;
        uint64_t vcw = 0;
        uint8_t tcw0 = 0;
        uint8_t tcw1 = 0;
    };

    struct Cache
    {
        std::array<octet, LAMBDA_BYTES> initial_seed;
        uint64_t cw = 0;
        SignedZ2<K> alpha_share;
        std::vector<Node> nodes;
    };

    RealTwoPartyPlayer* player;
    int playerno;
    Cache cache;
    Stats stats;

    std::vector<SignedZ2<K>> compare_local;
    std::vector<SignedZ2<K>> compare_revealed;
    std::vector<SignedZ2<K>> compare_eval_inputs;
    std::vector<Z2<K>> compare_eval_outputs;
    std::vector<std::array<octet, LAMBDA_BYTES>> batch_seeds;
    std::vector<uint8_t> batch_tmp_t;
    std::vector<uint64_t> batch_tmp_v;

    PRNG prng;
    octet tmp_seed[LAMBDA_BYTES];

    static std::string file_path(const std::string& prefix, int playerID)
    {
        std::string nested = "Player-Data/2-fss/" + prefix + std::to_string(playerID);
        std::ifstream nested_file(nested.c_str());
        if (nested_file.good())
            return nested;

        std::string root = "Player-Data/" + prefix + std::to_string(playerID);
        std::ifstream root_file(root.c_str());
        if (root_file.good())
            return root;

        return nested;
    }

    static SignedZ2<K> bigint_to_signed_z2(const bigint& x)
    {
        SignedZ2<K> result;
        auto size = x.get_mpz_t()->_mp_size;
        mpn_copyi((mp_limb_t*)result.get_ptr(), x.get_mpz_t()->_mp_d, abs(size));
        if (size < 0)
            result = -result;
        return result;
    }

    static uint64_t bigint_to_u64_ring(const bigint& x)
    {
        Z2<K> tmp(x);
        return static_cast<uint64_t>(tmp.get_limb(0));
    }

    static void seed_from_u64(octet out[LAMBDA_BYTES], uint64_t x)
    {
        std::memset(out, 0, LAMBDA_BYTES);
        for (int i = 0; i < 8; i++)
        {
            out[LAMBDA_BYTES - 1 - i] = octet(x & 0xff);
            x >>= 8;
        }
    }

    void evaluate_batch(
            const std::vector<SignedZ2<K>>& xs,
            std::vector<Z2<K>>& outputs)
    {
        auto start = std::chrono::high_resolution_clock::now();
        const size_t batch_size = xs.size();
        stats.evaluate_calls += batch_size;

        outputs.resize(batch_size);
        batch_seeds.resize(batch_size);
        batch_tmp_t.resize(batch_size);
        batch_tmp_v.resize(batch_size);

        int b = playerno;
        for (size_t idx = 0; idx < batch_size; idx++)
        {
            batch_seeds[idx] = cache.initial_seed;
            batch_tmp_t[idx] = uint8_t(b);
            batch_tmp_v[idx] = 0;
        }

        for (int i = 0; i < K - 1; i++)
        {
            const Node& node = cache.nodes.at(i);
            for (size_t idx = 0; idx < batch_size; idx++)
            {
                int xi = xs[idx].get_bit(K - i - 1);
                prng.SetSeed(batch_seeds[idx].data());
                uint8_t t_hat0 = prng.get_uchar() & 1;
                uint64_t v_hat0 = prng.get_word();
                uint64_t s_hat0 = prng.get_word();
                uint8_t t_hat1 = prng.get_uchar() & 1;
                uint64_t v_hat1 = prng.get_word();
                uint64_t s_hat1 = prng.get_word();

                uint64_t s0 = s_hat0 ^ (batch_tmp_t[idx] ? node.scw : 0);
                uint64_t s1 = s_hat1 ^ (batch_tmp_t[idx] ? node.scw : 0);
                uint8_t t0 = t_hat0 ^ (batch_tmp_t[idx] ? node.tcw0 : 0);
                uint8_t t1 = t_hat1 ^ (batch_tmp_t[idx] ? node.tcw1 : 0);

                seed_from_u64(&tmp_seed[0], v_hat0);
                prng.SetSeed(tmp_seed);
                uint64_t convert0 = prng.get_word();
                seed_from_u64(&tmp_seed[0], v_hat1);
                prng.SetSeed(tmp_seed);
                uint64_t convert1 = prng.get_word();

                uint64_t chosen_convert = xi ? convert1 : convert0;
                uint64_t term = chosen_convert + (batch_tmp_t[idx] ? node.vcw : 0);
                batch_tmp_v[idx] = b ? (batch_tmp_v[idx] - term) :
                        (batch_tmp_v[idx] + term);

                seed_from_u64(batch_seeds[idx].data(), xi ? s1 : s0);
                batch_tmp_t[idx] = xi ? t1 : t0;
            }
        }

        for (size_t idx = 0; idx < batch_size; idx++)
        {
            prng.SetSeed(batch_seeds[idx].data());
            uint64_t convert0 = prng.get_word();
            uint64_t term = convert0 + (batch_tmp_t[idx] ? cache.cw : 0);
            uint64_t out = b ? (batch_tmp_v[idx] - term) :
                    (batch_tmp_v[idx] + term);
            outputs[idx] = Z2<K>(static_cast<mp_limb_t>(out));
        }

        auto end = std::chrono::high_resolution_clock::now();
        stats.evaluate_time += end - start;
    }

public:
    Compare64(RealTwoPartyPlayer* player, int playerno) :
            player(player), playerno(playerno)
    {
        std::string k_path = file_path("k", playerno);
        std::ifstream k_in(k_path.c_str());
        if (!k_in.good())
            throw std::runtime_error("missing DCF key file for player " +
                    std::to_string(playerno));

        bigint tmp_bigint;
        k_in >> tmp_bigint;
        seed_from_u64(cache.initial_seed.data(), bigint_to_u64_ring(tmp_bigint));
        cache.nodes.resize(K - 1);
        for (int i = 0; i < K - 1; i++)
        {
            k_in >> tmp_bigint;
            cache.nodes[i].scw = bigint_to_u64_ring(tmp_bigint);
            k_in >> tmp_bigint;
            cache.nodes[i].vcw = bigint_to_u64_ring(tmp_bigint);
            k_in >> tmp_bigint;
            cache.nodes[i].tcw0 = uint8_t(tmp_bigint.get_ui() & 1);
            k_in >> tmp_bigint;
            cache.nodes[i].tcw1 = uint8_t(tmp_bigint.get_ui() & 1);
        }
        k_in >> tmp_bigint;
        cache.cw = bigint_to_u64_ring(tmp_bigint);
        k_in.close();

        std::string r_path = file_path("r", playerno);
        std::ifstream r_in(r_path.c_str());
        if (!r_in.good())
            throw std::runtime_error("missing DCF r file for player " +
                    std::to_string(playerno));
        r_in >> tmp_bigint;
        cache.alpha_share = SignedZ2<K>(tmp_bigint);
        r_in.close();
    }

    const Stats& get_stats() const
    {
        return stats;
    }

    void reset_stats()
    {
        stats = Stats();
    }

    void compare_in_vec(
            const std::vector<Z2<K>>& shares,
            const std::vector<int>& compare_idx_vec,
            std::vector<Z2<K>>& compare_res,
            bool greater_than)
    {
        assert(compare_idx_vec.size() && compare_idx_vec.size() == compare_res.size());
        const int size_res = int(compare_idx_vec.size() / 2);

        compare_res.resize(compare_idx_vec.size());
        SignedZ2<K> alpha_share = cache.alpha_share;

        for (int offset = 0; offset < size_res; offset += (int)MAX_COMPARE_CHUNK)
        {
            const int len = std::min<int>((int)MAX_COMPARE_CHUNK, size_res - offset);
            compare_local.resize(len);
            compare_revealed.resize(len);
            compare_eval_inputs.resize(2 * len);

            for (int i = 0; i < len; i++)
            {
                int first = compare_idx_vec[2 * (offset + i)];
                int second = compare_idx_vec[2 * (offset + i) + 1];
                if (greater_than)
                    compare_local[i] = SignedZ2<K>(shares[second]) -
                            SignedZ2<K>(shares[first]) + alpha_share;
                else
                    compare_local[i] = SignedZ2<K>(shares[first]) -
                            SignedZ2<K>(shares[second]) + alpha_share;
            }

            octetStream send_os, receive_os;
            for (int i = 0; i < len; i++)
                compare_local[i].pack(send_os);
            player->send(send_os);
            player->receive(receive_os);

            for (int i = 0; i < len; i++)
            {
                SignedZ2<K> peer;
                peer.unpack(receive_os);
                compare_revealed[i] = compare_local[i] + peer;
                compare_eval_inputs[2 * i] = compare_revealed[i];
                compare_eval_inputs[2 * i + 1] = compare_revealed[i];
                compare_eval_inputs[2 * i + 1] += 1LL << (K - 1);
            }

            evaluate_batch(compare_eval_inputs, compare_eval_outputs);

            bigint r_tmp;
            for (int i = 0; i < len; i++)
            {
                SignedZ2<K> dcf_u = compare_eval_outputs[2 * i];
                SignedZ2<K> dcf_v = compare_eval_outputs[2 * i + 1];
                SignedZ2<K> revealed_shifted = compare_eval_inputs[2 * i + 1];

                if (revealed_shifted.get_bit(K - 1))
                    r_tmp = bigint(dcf_v - dcf_u + playerno);
                else
                    r_tmp = bigint(dcf_v - dcf_u);

                compare_res[2 * (offset + i)] =
                        Z2<K>(SignedZ2<K>(playerno) - r_tmp);
                compare_res[2 * (offset + i) + 1] =
                        compare_res[2 * (offset + i)];
            }
        }
    }

    void compare_in_vec(
            const std::vector<std::array<Z2<K>, 2>>& shares,
            const std::vector<int>& compare_idx_vec,
            std::vector<Z2<K>>& compare_res,
            bool greater_than)
    {
        assert(compare_idx_vec.size() && compare_idx_vec.size() == compare_res.size());
        const int size_res = int(compare_idx_vec.size() / 2);
        std::vector<Z2<K>> first_component(2 * size_res);
        for (int i = 0; i < 2 * size_res; i++)
            first_component[i] = shares[compare_idx_vec[i]][0];

        std::vector<int> identity_idx(2 * size_res);
        for (int i = 0; i < 2 * size_res; i++)
            identity_idx[i] = i;
        compare_in_vec(first_component, identity_idx, compare_res, greater_than);
    }
};

} // namespace KonaDcfCompare

#endif /* MACHINES_KONA_DCF_COMPARE_HPP_ */
