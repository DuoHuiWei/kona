#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../Math/Z2k.hpp"
#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "kona-cong-kona-adapter.hpp"
#include "kona-dcf-compare.hpp"
#include "kona-pcr-compare.hpp"

using namespace std;

namespace
{

const int K = 64;
int playerno = 0;
string mode = "pcr";
int comparator_count = 64;
int repeats = 1;
int inner_repeats = 1;
uint64_t seed = 20260902;
ez::ezOptionParser opt;

template<int KK>
using SharePair = array<Z2<KK>, 2>;

uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

Z2<K> ring(uint64_t x)
{
    return Z2<K>(static_cast<mp_limb_t>(x));
}

Z2<K> share_value(size_t index, Z2<K> value)
{
    Z2<K> mask = ring(mix64(seed ^ (index + 0xabcddcbaULL)));
    return playerno == 0 ? mask : value - mask;
}

void parse_argv(int argc, const char** argv)
{
    opt.add("5000", 0, 1, 0, "Port number base", "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("localhost", 0, 1, 0, "Startup host", "-h", "--hostname");
    opt.add("pcr", 0, 1, 0, "Backend: pcr or dcf", "-m", "--mode");
    opt.add("64", 0, 1, 0, "Independent comparator count", "-c", "--comparators");
    opt.add("1", 0, 1, 0, "Measured trials", "-r", "--repeats");
    opt.add("1", 0, 1, 0, "Compare+swap calls per trial", "-i", "--inner-repeats");
    opt.add("20260902", 0, 1, 0, "Deterministic input seed", "-s", "--seed");
    opt.parse(argc, argv);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else if (argc > 1)
        sscanf(argv[1], "%d", &playerno);
    else
        throw runtime_error("missing player number");

    opt.get("--mode")->getString(mode);
    opt.get("--comparators")->getInt(comparator_count);
    opt.get("--repeats")->getInt(repeats);
    opt.get("--inner-repeats")->getInt(inner_repeats);
    string seed_text;
    opt.get("--seed")->getString(seed_text);
    seed = stoull(seed_text);

    if (mode != "pcr" && mode != "dcf")
        throw runtime_error("mode must be pcr or dcf");
    if (comparator_count <= 0 || repeats <= 0 || inner_repeats <= 0)
        throw runtime_error("comparators, repeats, and inner-repeats must be positive");
}

RealTwoPartyPlayer* start_networking()
{
    string hostname;
    int port_base = 0;
    opt.get("--hostname")->getString(hostname);
    opt.get("--portnumbase")->getInt(port_base);
    Names names;
    Server::start_networking(names, playerno, 2, hostname, port_base);
    return new RealTwoPartyPlayer(names, 1 - playerno, 0);
}

vector<SharePair<K>> build_shares()
{
    vector<SharePair<K>> shares(2 * comparator_count);
    for (size_t i = 0; i < shares.size(); ++i)
    {
        uint64_t distance = mix64(seed + i) & ((uint64_t(1) << 40) - 1);
        uint64_t label = mix64(seed + 100000 + i) & 0xffffULL;
        shares[i][0] = share_value(i, ring(distance));
        shares[i][1] = share_value(i + 1000000, ring(label));
    }
    return shares;
}

vector<int> build_pairs()
{
    vector<int> pairs;
    pairs.reserve(2 * comparator_count);
    for (int i = 0; i < comparator_count; ++i)
    {
        pairs.push_back(2 * i);
        pairs.push_back(2 * i + 1);
    }
    return pairs;
}

size_t total_rounds(const NamedCommStats& stats)
{
    size_t result = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        result += it->second.rounds;
    return result;
}

double total_comm_ms(NamedCommStats stats)
{
    double result = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        result += it->second.timer.elapsed() * 1000.0;
    return result;
}

void print_result(
        int trial,
        double key_init_ms,
        double online_ms,
        double compare_ms,
        double swap_ms,
        double comm_ms,
        size_t sent_bytes,
        size_t transport_rounds,
        long long evaluate_calls,
        double evaluate_ms,
        uint64_t checksum)
{
    cout << "KONA_PCR_DCF_COMPARE_SWAP_BENCH"
         << " backend=" << mode
         << " comparator_count=" << comparator_count
         << " trial=" << trial
         << " inner_repeats=" << inner_repeats
         << " online_ms=" << online_ms
         << " avg_online_ms=" << online_ms / inner_repeats
         << " compare_ms=" << compare_ms / inner_repeats
         << " swap_ms=" << swap_ms / inner_repeats
         << " compute_ms=" << max(0.0, online_ms / inner_repeats - comm_ms)
         << " comm_ms=" << comm_ms
         << " sent_bytes=" << sent_bytes
         << " transport_rounds=" << transport_rounds
         << " logical_rounds=" << transport_rounds / 2
         << " dcf_evaluate_calls=" << evaluate_calls
         << " dcf_evaluate_ms=" << evaluate_ms
         << " dcf_key_init_ms=" << key_init_ms
         << " checksum=" << checksum
         << endl;
}

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    RealTwoPartyPlayer* player = start_networking();
    try
    {
        vector<SharePair<K>> shares = build_shares();
        vector<int> pairs = build_pairs();

        auto init_start = chrono::steady_clock::now();
        KonaDcfCompare::Compare64<K> dcf_compare(player, playerno);
        auto init_end = chrono::steady_clock::now();
        double key_init_ms = chrono::duration<double, milli>(
                init_end - init_start).count();

        for (int trial = 1; trial <= repeats; ++trial)
        {
            vector<Z2<K>> compare_res(pairs.size());
            dcf_compare.reset_stats();
            NamedCommStats comm_before = player->total_comm();
            auto online_start = chrono::steady_clock::now();
            double compare_ms = 0;
            double swap_ms = 0;

            for (int inner = 0; inner < inner_repeats; ++inner)
            {
                auto compare_start = chrono::steady_clock::now();
                if (mode == "pcr")
                {
                    // Only this benchmark requests a one-shot PCR B2A.
                    KonaPcrCompare::pcr_compare_in_vec_l2(
                            shares, pairs, compare_res, true, player,
                            static_cast<size_t>(comparator_count));
                }
                else
                {
                    dcf_compare.compare_in_vec(
                            shares, pairs, compare_res, true,
                            static_cast<size_t>(comparator_count));
                }
                auto compare_end = chrono::steady_clock::now();
                compare_ms += chrono::duration<double, milli>(
                        compare_end - compare_start).count();

                auto swap_start = chrono::steady_clock::now();
                // The explicit size keeps this benchmark's swap in one batch.
                KonaCongKonaAdapter::ss_vec_kona_l2<K>(
                        shares, pairs, compare_res, player,
                        pairs.size());
                auto swap_end = chrono::steady_clock::now();
                swap_ms += chrono::duration<double, milli>(
                        swap_end - swap_start).count();
            }

            auto online_end = chrono::steady_clock::now();
            NamedCommStats comm_after = player->total_comm();
            NamedCommStats delta = comm_after - comm_before;
            long long evaluate_calls =
                    dcf_compare.get_stats().evaluate_calls;
            double evaluate_ms = dcf_compare.get_stats().evaluate_time.count() * 1000.0;
            uint64_t checksum = 0;
            for (const auto& share : shares)
                checksum += share[0].get_limb(0) + share[1].get_limb(0);

            if (playerno == 0)
                print_result(
                        trial, key_init_ms,
                        chrono::duration<double, milli>(
                                online_end - online_start).count(),
                        compare_ms, swap_ms, total_comm_ms(delta) / inner_repeats,
                        delta.sent, total_rounds(delta), evaluate_calls,
                        evaluate_ms / inner_repeats, checksum);
        }
    }
    catch (...)
    {
        delete player;
        throw;
    }
    delete player;
    return 0;
}
