#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "../Math/Z2k.hpp"
#include "kona-cong-kona-adapter.hpp"
#include "kona-pcr-compare.hpp"
#include "kona-dcf-compare.hpp"

using namespace std;

namespace
{

const int K = 64;

int playerno = 0;
ez::ezOptionParser opt;
string backend_mode = "pcr";
int comparator_count = 64;

template<int KK>
using SharePair = std::array<Z2<KK>, 2>;

struct BenchResult
{
    string backend;
    int comparator_count = 0;
    double dcf_key_init_seconds = 0;
    double online_seconds = 0;
    double compute_seconds = 0;
    double comm_seconds = 0;
    double compare_seconds = 0;
    double swap_seconds = 0;
    size_t sent_bytes = 0;
    size_t transport_rounds = 0;
    size_t logical_rounds = 0;
    long long dcf_evaluate_calls = 0;
    double dcf_evaluate_seconds = 0;
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
    opt.add("pcr", 0, 1, 0, "Backend: pcr or dcf", "-m", "--mode");
    opt.add("64", 0, 1, 0, "Comparator count in this single layer", "-c", "--comparators");
    opt.parse(argc, argv);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else
        sscanf(argv[1], "%d", &playerno);

    opt.get("--mode")->getString(backend_mode);
    opt.get("--comparators")->getInt(comparator_count);

    if (backend_mode != "pcr" && backend_mode != "dcf")
        throw runtime_error("backend mode must be pcr or dcf");
    if (comparator_count <= 0)
        throw runtime_error("comparator count must be positive");
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
        if (i % 17 == 0 && i > 0)
            value = mix64(uint64_t(i - 1) + 1000) & ((uint64_t(1) << 20) - 1);
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

void print_result(const BenchResult& r)
{
    cout << "KONA_LAYER_COST_CALIBRATION"
         << " backend=" << r.backend
         << " comparator_count=" << r.comparator_count
         << " online_ms=" << r.online_seconds * 1000.0
         << " compute_ms=" << r.compute_seconds * 1000.0
         << " comm_ms=" << r.comm_seconds * 1000.0
         << " sent_bytes=" << r.sent_bytes
         << " transport_rounds=" << r.transport_rounds
         << " logical_rounds=" << r.logical_rounds
         << " compare_ms=" << r.compare_seconds * 1000.0
         << " swap_ms=" << r.swap_seconds * 1000.0
         << " dcf_key_init_ms=" << r.dcf_key_init_seconds * 1000.0
         << " dcf_evaluate_calls=" << r.dcf_evaluate_calls
         << " dcf_evaluate_ms=" << r.dcf_evaluate_seconds * 1000.0
         << " checksum=" << r.checksum
         << endl;
}

BenchResult run_layer_bench(
        const string& backend,
        int m,
        RealTwoPartyPlayer* player)
{
    BenchResult res;
    res.backend = backend;
    res.comparator_count = m;

    auto shares = build_shares(m);
    vector<int> compare_idx_vec = build_disjoint_pairs(m);
    vector<Z2<K>> compare_res(compare_idx_vec.size());

    auto dcf_init_start = chrono::steady_clock::now();
    KonaDcfCompare::Compare64<K> dcf_compare(player, playerno);
    auto dcf_init_end = chrono::steady_clock::now();
    res.dcf_key_init_seconds =
            chrono::duration<double>(dcf_init_end - dcf_init_start).count();

    auto comm_before = player->total_comm();
    auto online_start = chrono::steady_clock::now();

    auto compare_start = chrono::steady_clock::now();
    if (backend == "pcr")
    {
        KonaPcrCompare::pcr_compare_in_vec_l2(
                shares, compare_idx_vec, compare_res, true, player);
    }
    else
    {
        dcf_compare.compare_in_vec(shares, compare_idx_vec, compare_res, true);
    }
    auto compare_end = chrono::steady_clock::now();
    res.compare_seconds =
            chrono::duration<double>(compare_end - compare_start).count();

    auto swap_start = chrono::steady_clock::now();
    KonaCongKonaAdapter::ss_vec_kona_l2<K>(
            shares, compare_idx_vec, compare_res, player);
    auto swap_end = chrono::steady_clock::now();
    res.swap_seconds = chrono::duration<double>(swap_end - swap_start).count();

    auto online_end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto delta = comm_after - comm_before;

    res.online_seconds =
            chrono::duration<double>(online_end - online_start).count();
    res.comm_seconds = total_comm_seconds(delta);
    res.compute_seconds = max(0.0, res.online_seconds - res.comm_seconds);
    res.sent_bytes = delta.sent;
    res.transport_rounds = total_rounds(delta);
    res.logical_rounds = res.transport_rounds / 2;

    if (backend == "dcf")
    {
        res.dcf_evaluate_calls = dcf_compare.get_stats().evaluate_calls;
        res.dcf_evaluate_seconds =
                dcf_compare.get_stats().evaluate_time.count();
    }

    for (size_t i = 0; i < shares.size(); ++i)
        res.checksum += shares[i][0].get_limb(0) + shares[i][1].get_limb(0);

    return res;
}

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    RealTwoPartyPlayer* player = start_networking();

    try
    {
        BenchResult result = run_layer_bench(
                backend_mode, comparator_count, player);
        if (playerno == 0)
            print_result(result);
    }
    catch (...)
    {
        delete player;
        throw;
    }

    delete player;
    return 0;
}
