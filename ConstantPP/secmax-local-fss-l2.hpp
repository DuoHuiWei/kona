#ifndef CONSTANTPP_SECMAX_LOCAL_FSS_L2_HPP
#define CONSTANTPP_SECMAX_LOCAL_FSS_L2_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "sec-bcom-material.hpp"
#include "constantpp-comm.hpp"
#include "../Machines/kona-dcf-compare.hpp"

namespace ConstantPP {
// PERFORMANCE ONLY, explicitly authorized L2 simplification.
// Exchange k actual ring values once, then perform real local DCF evaluations.
// Zero masks and an offline-public alpha allow reusing one test key cache.
// Both r shares are read OFFLINE: there is deliberately NO privacy guarantee.
// This is not the paper's pair-specific key/mask preprocessing or its proof.
// The Node/Cache/parser/PRG evaluation core below is extracted verbatim from
// kona-dcf-compare.hpp at implementation time; that shared file is UNCHANGED.
// Native Compare64's private local API cannot be called without editing it.
// Keep this adapter confined to ConstantPP SecMax; never use for other phases.
class SecMaxLocalFssL2 {
    static const int K = 64;
    static const int LAMBDA_BYTES = 16;
    using Stats = KonaDcfCompare::Compare64<K>::Stats;
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
        auto start = std::chrono::steady_clock::now();
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

        auto end = std::chrono::steady_clock::now();
        stats.evaluate_time += end - start;
    }

public:
    SecMaxLocalFssL2(int party, size_t k) : playerno(party)
    {
        if (party != 0 && party != 1) throw std::invalid_argument("invalid SecMax L2 party");
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

        public_alpha = SignedZ2<K>(0);
        for (int p = 0; p < 2; ++p) {
            std::ifstream f(file_path("r", p));
            bigint value;
            if (!(f >> value)) throw std::runtime_error("SecMax L2 requires both offline test r shares");
            public_alpha += SignedZ2<K>(value);
        }
        plan = make_sec_bcom_upper_triangle_plan(k);
        peer.resize(k); opened.resize(k); ranks.resize(k);
        const size_t batch = std::min<size_t>(plan.pairs.size(), 16384);
        compare_eval_inputs.reserve(2 * batch); compare_eval_outputs.reserve(2 * batch);
        batch_seeds.reserve(2 * batch); batch_tmp_t.reserve(2 * batch); batch_tmp_v.reserve(2 * batch);
    }

    std::vector<Ring> rank_greater(const std::vector<Ring>& values,
            RealTwoPartyPlayer* player, ProtocolStats* protocol_stats)
    {
        if (values.size() != plan.n) throw std::invalid_argument("SecMax L2 plan size mismatch");
        stats = Stats();
        std::fill(ranks.begin(), ranks.end(), Ring(0));
        if (values.size() < 2) return ranks;
        // Real k*8-byte exchange per party. Zero mask: frequencies are opened.
        exchange_ring_vector(player, values, peer, protocol_stats);
        if (protocol_stats) ++protocol_stats->logical_rounds;
        for (size_t i = 0; i < values.size(); ++i) opened[i] = values[i] + peer[i];
        const Ring one = playerno == 0 ? Ring(1) : Ring(0);
        for (size_t offset = 0; offset < plan.pairs.size(); offset += 16384)
        {
            const size_t count = std::min<size_t>(16384, plan.pairs.size() - offset);
            compare_eval_inputs.resize(2 * count);
            for (size_t t = 0; t < count; ++t) {
                const auto& pair = plan.pairs[offset + t];
                const SignedZ2<K> x = SignedZ2<K>(opened[pair.j]) - SignedZ2<K>(opened[pair.i]) + public_alpha;
                compare_eval_inputs[2*t] = x;
                compare_eval_inputs[2*t+1] = x + SignedZ2<K>(Ring(1) << 63);
            }
            evaluate_batch(compare_eval_inputs, compare_eval_outputs);
            for (size_t t = 0; t < count; ++t) {
                Ring r = compare_eval_outputs[2*t+1] - compare_eval_outputs[2*t];
                if (compare_eval_inputs[2*t+1].get_bit(63)) r += Ring(playerno);
                const Ring bit = Ring(playerno) - r;
                const auto& pair = plan.pairs[offset + t];
                ranks[pair.i] += bit;
                ranks[pair.j] += one - bit; // Preserve existing later-index tie rule.
            }
        }
        return ranks;
    }
    const Stats& get_stats() const { return stats; }
private:
    SignedZ2<K> public_alpha;
    SecBComUpperTrianglePlan plan;
    std::vector<Ring> peer, opened, ranks;
};
} // namespace ConstantPP
#endif
