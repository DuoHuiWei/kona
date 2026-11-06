#include "kona-bench-affinity.hpp"
#include <algorithm>
#include <array>
#include <chrono>
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
#include "kona-pcr-compare.hpp"

using namespace std;

namespace
{

const int K = 64;

int playerno = 0;
int comparator_count = 200;
int layer_count = 10;
ez::ezOptionParser opt;

template<int KK>
using SharePair = array<Z2<KK>, 2>;

struct LayerResult
{
    int layer = 0;
    int comparator_count = 0;
    double total_seconds = 0;
    double compare_seconds = 0;
    double swap_seconds = 0;
    double comm_seconds = 0;
    double compute_seconds = 0;
    size_t sent_bytes = 0;
    size_t transport_rounds = 0;
    size_t logical_rounds = 0;
    uint64_t checksum = 0;
};

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

Z2<K> additive_share_for(int p, size_t i, Z2<K> value)
{
    Z2<K> mask = ring(mix64(i + 0xabcddcbaULL));
    return p == 0 ? mask : value - mask;
}

void parse_argv(int argc, const char** argv)
{
    opt.add("5000", 0, 1, 0, "Port number base", "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on", "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host", "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file", "-ip", "--ip-file-name");
    opt.add("200", 0, 1, 0, "Comparator count per layer", "-c", "--comparators");
    opt.add("10", 0, 1, 0, "Number of sequential PCR layers", "-l", "--layers");
    opt.parse(argc, argv);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else
        sscanf(argv[1], "%d", &playerno);

    opt.get("--comparators")->getInt(comparator_count);
    opt.get("--layers")->getInt(layer_count);

    if (comparator_count <= 0)
        throw runtime_error("comparator count must be positive");
    if (layer_count <= 0)
        throw runtime_error("layer count must be positive");
}

RealTwoPartyPlayer* start_networking()
{
    string hostname, ip_file_name;
    int pnbase = 0;
    int my_port = Names::DEFAULT_PORT;

    opt.get("--portnumbase")->getInt(pnbase);
    opt.get("--hostname")->getString(hostname);
    opt.get("--ip-file-name")->getString(ip_file_name);

    ez::OptionGroup* mp_opt = opt.get("--my-port");
    if (mp_opt->isSet)
        mp_opt->getInt(my_port);

    Names player_names;
    if (ip_file_name.size() > 0)
    {
        if (my_port != Names::DEFAULT_PORT)
            throw runtime_error("cannot set my-port with ip-file-name");
        player_names.init(playerno, pnbase, ip_file_name, 2);
    }
    else
    {
        Server::start_networking(
                player_names, playerno, 2, hostname, pnbase, my_port);
    }

    return new RealTwoPartyPlayer(player_names, 1 - playerno, 0);
}

size_t total_rounds(const NamedCommStats& stats)
{
    size_t rounds = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        rounds += it->second.rounds;
    return rounds;
}

double total_comm_seconds(NamedCommStats stats)
{
    double total = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        total += it->second.timer.elapsed();
    return total;
}

vector<SharePair<K>> build_shares(int m)
{
    vector<SharePair<K>> shares(2 * m);
    for (int i = 0; i < 2 * m; ++i)
    {
        uint64_t value = mix64(uint64_t(i) + 1000) & ((uint64_t(1) << 20) - 1);
        uint64_t label = mix64(uint64_t(i) + 5000) & 0xf;
        shares[i][0] = additive_share_for(playerno, i, ring(value));
        shares[i][1] = additive_share_for(playerno, i + 100000, ring(label));
    }
    return shares;
}

vector<int> build_disjoint_pairs(int m)
{
    vector<int> compare_idx_vec;
    compare_idx_vec.reserve(2 * m);
    for (int i = 0; i < m; ++i)
    {
        compare_idx_vec.push_back(2 * i);
        compare_idx_vec.push_back(2 * i + 1);
    }
    return compare_idx_vec;
}

uint64_t checksum_shares(const vector<SharePair<K>>& shares)
{
    uint64_t checksum = 0;
    for (size_t i = 0; i < shares.size(); ++i)
        checksum += shares[i][0].get_limb(0) + shares[i][1].get_limb(0);
    return checksum;
}

LayerResult run_one_layer(
        int layer,
        vector<SharePair<K>>& shares,
        const vector<int>& compare_idx_vec,
        RealTwoPartyPlayer* player)
{
    LayerResult res;
    res.layer = layer;
    res.comparator_count = static_cast<int>(compare_idx_vec.size() / 2);
    vector<Z2<K>> compare_res(compare_idx_vec.size());

    auto comm_before = player->total_comm();
    auto total_start = chrono::steady_clock::now();

    auto compare_start = chrono::steady_clock::now();
    KonaPcrCompare::pcr_compare_in_vec_l2(
            shares, compare_idx_vec, compare_res, true, player);
    auto compare_end = chrono::steady_clock::now();

    auto swap_start = chrono::steady_clock::now();
    KonaCongKonaAdapter::ss_vec_kona_l2<K>(
            shares, compare_idx_vec, compare_res, player);
    auto swap_end = chrono::steady_clock::now();

    auto total_end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto delta = comm_after - comm_before;

    res.compare_seconds =
            chrono::duration<double>(compare_end - compare_start).count();
    res.swap_seconds = chrono::duration<double>(swap_end - swap_start).count();
    res.total_seconds =
            chrono::duration<double>(total_end - total_start).count();
    res.comm_seconds = total_comm_seconds(delta);
    res.compute_seconds = max(0.0, res.total_seconds - res.comm_seconds);
    res.sent_bytes = delta.sent;
    res.transport_rounds = total_rounds(delta);
    res.logical_rounds = res.transport_rounds / 2;
    res.checksum = checksum_shares(shares);
    return res;
}

void print_layer_result(const LayerResult& r)
{
    cout << "KONA_PCR_LAYER_DEPTH_LAYER"
         << " layer=" << r.layer
         << " comparator_count=" << r.comparator_count
         << " total_ms=" << r.total_seconds * 1000.0
         << " compare_ms=" << r.compare_seconds * 1000.0
         << " swap_ms=" << r.swap_seconds * 1000.0
         << " compute_ms=" << r.compute_seconds * 1000.0
         << " comm_ms=" << r.comm_seconds * 1000.0
         << " sent_bytes=" << r.sent_bytes
         << " transport_rounds=" << r.transport_rounds
         << " logical_rounds=" << r.logical_rounds
         << " checksum=" << r.checksum
         << endl;
}

void print_summary(
        const vector<LayerResult>& results,
        double overall_wall_seconds)
{
    LayerResult sum;
    sum.comparator_count = comparator_count;
    for (const auto& r : results)
    {
        sum.total_seconds += r.total_seconds;
        sum.compare_seconds += r.compare_seconds;
        sum.swap_seconds += r.swap_seconds;
        sum.compute_seconds += r.compute_seconds;
        sum.comm_seconds += r.comm_seconds;
        sum.sent_bytes += r.sent_bytes;
        sum.transport_rounds += r.transport_rounds;
        sum.logical_rounds += r.logical_rounds;
        sum.checksum += r.checksum;
    }

    const double denom = static_cast<double>(results.size());
    const double orchestration_seconds =
            max(0.0, overall_wall_seconds - sum.total_seconds);
    cout << "KONA_PCR_LAYER_DEPTH_SUMMARY"
         << " layers=" << results.size()
         << " comparator_count=" << comparator_count
         << " overall_wall_ms=" << overall_wall_seconds * 1000.0
         << " total_ms=" << sum.total_seconds * 1000.0
         << " orchestration_ms=" << orchestration_seconds * 1000.0
         << " avg_layer_total_ms=" << sum.total_seconds * 1000.0 / denom
         << " compare_ms=" << sum.compare_seconds * 1000.0
         << " avg_layer_compare_ms=" << sum.compare_seconds * 1000.0 / denom
         << " swap_ms=" << sum.swap_seconds * 1000.0
         << " avg_layer_swap_ms=" << sum.swap_seconds * 1000.0 / denom
         << " compute_ms=" << sum.compute_seconds * 1000.0
         << " comm_ms=" << sum.comm_seconds * 1000.0
         << " sent_bytes=" << sum.sent_bytes
         << " transport_rounds=" << sum.transport_rounds
         << " logical_rounds=" << sum.logical_rounds
         << " checksum=" << sum.checksum
         << endl;
}

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    KonaBenchAffinity::bind_fixed_cpu(playerno);
    RealTwoPartyPlayer* player = start_networking();

    try
    {
        auto shares = build_shares(comparator_count);
        auto compare_idx_vec = build_disjoint_pairs(comparator_count);
        vector<LayerResult> results;
        results.reserve(layer_count);

        auto overall_start = chrono::steady_clock::now();
        for (int layer = 0; layer < layer_count; ++layer)
        {
            auto res = run_one_layer(layer, shares, compare_idx_vec, player);
            results.push_back(res);
            if (playerno == 0)
                print_layer_result(res);
        }

        auto overall_end = chrono::steady_clock::now();
        double overall_wall_seconds =
                chrono::duration<double>(overall_end - overall_start).count();

        if (playerno == 0)
            print_summary(results, overall_wall_seconds);
    }
    catch (...)
    {
        delete player;
        throw;
    }

    delete player;
    return 0;
}
