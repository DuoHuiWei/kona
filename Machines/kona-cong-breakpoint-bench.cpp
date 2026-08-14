#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "../Math/Z2k.hpp"
#include "kona-cong-topk.hpp"
#include "kona-cong-kona-adapter.hpp"
#include "kona-pcr-compare.hpp"
#include "kona-dcf-compare.hpp"

using namespace std;

namespace
{

const int K = 64;
int playerno = 0;
ez::ezOptionParser opt;
string backend_mode = "both";
string size_list = "64,128,256,512";
int topk_k = -1;

template<int KK>
using SharePair = std::array<Z2<KK>, 2>;

struct TopkBenchResult
{
    int n = 0;
    int k = 0;
    string backend;
    double total_seconds = 0;
    double comm_seconds = 0;
    double compute_seconds = 0;
    double compare_seconds = 0;
    double swap_seconds = 0;
    double reorder_seconds = 0;
    double network_build_seconds = 0;
    size_t sent_bytes = 0;
    size_t transport_rounds = 0;
    size_t logical_rounds = 0;
    uint64_t checksum = 0;
    double dcf_key_init_seconds = 0;
    long long dcf_evaluate_calls = 0;
    double dcf_evaluate_seconds = 0;
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

Z2<K> additive_share_for(int playerno, size_t i, Z2<K> value)
{
    Z2<K> mask = ring(mix64(i + 0xabcddcbaULL));
    return playerno == 0 ? mask : value - mask;
}

void parse_argv(int argc, const char** argv)
{
    opt.add("5000", 0, 1, 0, "Port number base", "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on", "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host", "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file", "-ip", "--ip-file-name");
    opt.add("both", 0, 1, 0, "Backend: pcr, dcf, both", "-m", "--mode");
    opt.add("64,128,256,512", 0, 1, 0, "Comma-separated n list",
            "-n", "--sizes");
    opt.add("-1", 0, 1, 0, "Top-k; if negative, run defaults 5 and 16",
            "-k", "--topk");
    opt.parse(argc, argv);
    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else
        sscanf(argv[1], "%d", &playerno);
    opt.get("--mode")->getString(backend_mode);
    opt.get("--sizes")->getString(size_list);
    opt.get("--topk")->getInt(topk_k);

    if (backend_mode != "pcr" && backend_mode != "dcf" && backend_mode != "both")
        throw runtime_error("backend mode must be pcr, dcf, or both");
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

vector<SharePair<K>> build_shares(int n)
{
    vector<SharePair<K>> shares(n);
    for (int i = 0; i < n; i++)
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

void print_result(const TopkBenchResult& r)
{
    cout << "CONG_TOPK_ONLY"
         << " backend=" << r.backend
         << " n=" << r.n
         << " k=" << r.k
         << " total_ms=" << r.total_seconds * 1000.0
         << " compute_ms=" << r.compute_seconds * 1000.0
         << " comm_ms=" << r.comm_seconds * 1000.0
         << " compare_ms=" << r.compare_seconds * 1000.0
         << " swap_ms=" << r.swap_seconds * 1000.0
         << " reorder_ms=" << r.reorder_seconds * 1000.0
         << " network_build_ms=" << r.network_build_seconds * 1000.0
         << " sent_bytes=" << r.sent_bytes
         << " transport_rounds=" << r.transport_rounds
         << " logical_rounds=" << r.logical_rounds
         << " checksum=" << r.checksum;

    if (r.backend == "cong-dcf")
    {
        cout << " dcf_key_init_ms=" << r.dcf_key_init_seconds * 1000.0
             << " dcf_evaluate_calls=" << r.dcf_evaluate_calls
             << " dcf_evaluate_ms=" << r.dcf_evaluate_seconds * 1000.0;
    }

    cout << endl;
}

vector<int> parse_sizes(const string& text)
{
    vector<int> out;
    size_t start = 0;
    while (start < text.size())
    {
        size_t comma = text.find(',', start);
        string token = text.substr(start,
                comma == string::npos ? string::npos : comma - start);
        if (!token.empty())
            out.push_back(stoi(token));
        if (comma == string::npos)
            break;
        start = comma + 1;
    }
    if (out.empty())
        throw runtime_error("size list must not be empty");
    return out;
}

TopkBenchResult benchmark_cong_pcr_topk_only(
        const vector<SharePair<K>>& input,
        int k,
        RealTwoPartyPlayer* player)
{
    auto shares = input;
    TopkBenchResult res;
    res.backend = "cong-pcr";
    res.n = shares.size();
    res.k = k;

    auto build_start = chrono::steady_clock::now();
    KonaCongTopK::CongNetwork network =
            KonaCongTopK::build_cong_network((int)shares.size(), k);
    auto build_end = chrono::steady_clock::now();
    res.network_build_seconds =
            chrono::duration<double>(build_end - build_start).count();

    auto comm_before = player->total_comm();
    auto total_start = chrono::steady_clock::now();

    for (size_t depth = 0; depth < network.levels.size(); depth++)
    {
        vector<int> compare_idx_vec =
                KonaCongTopK::flatten_level(network.levels[depth]);

        auto compare_start = chrono::steady_clock::now();
        vector<Z2<K>> compare_res(compare_idx_vec.size());
        KonaPcrCompare::pcr_compare_in_vec_l2(
                shares, compare_idx_vec, compare_res, true, player);
        auto compare_end = chrono::steady_clock::now();
        res.compare_seconds +=
                chrono::duration<double>(compare_end - compare_start).count();

        auto swap_start = chrono::steady_clock::now();
        KonaCongKonaAdapter::ss_vec_kona_l2<K>(
                shares, compare_idx_vec, compare_res, player);
        auto swap_end = chrono::steady_clock::now();
        res.swap_seconds +=
                chrono::duration<double>(swap_end - swap_start).count();
    }

    auto reorder_start = chrono::steady_clock::now();
    KonaCongTopK::move_output_wires_to_tail(shares, network.output_wires);
    auto reorder_end = chrono::steady_clock::now();
    res.reorder_seconds =
            chrono::duration<double>(reorder_end - reorder_start).count();

    auto total_end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto delta = comm_after - comm_before;

    res.total_seconds = chrono::duration<double>(total_end - total_start).count();
    res.comm_seconds = total_comm_seconds(delta);
    res.compute_seconds = res.total_seconds - res.comm_seconds;
    res.sent_bytes = delta.sent;
    res.transport_rounds = total_rounds(delta);
    res.logical_rounds = res.transport_rounds / 2;
    for (int i = 0; i < k; i++)
        res.checksum += shares[shares.size() - 1 - i][0].get_limb(0);
    return res;
}

TopkBenchResult benchmark_cong_dcf_topk_only(
        const vector<SharePair<K>>& input,
        int k,
        RealTwoPartyPlayer* player)
{
    auto dcf_init_start = chrono::steady_clock::now();
    KonaDcfCompare::Compare64<K> dcf_compare(player, playerno);
    auto dcf_init_end = chrono::steady_clock::now();

    auto shares = input;
    TopkBenchResult res;
    res.backend = "cong-dcf";
    res.n = shares.size();
    res.k = k;
    res.dcf_key_init_seconds =
            chrono::duration<double>(dcf_init_end - dcf_init_start).count();

    auto build_start = chrono::steady_clock::now();
    KonaCongTopK::CongNetwork network =
            KonaCongTopK::build_cong_network((int)shares.size(), k);
    auto build_end = chrono::steady_clock::now();
    res.network_build_seconds =
            chrono::duration<double>(build_end - build_start).count();

    auto comm_before = player->total_comm();
    auto total_start = chrono::steady_clock::now();

    for (size_t depth = 0; depth < network.levels.size(); depth++)
    {
        vector<int> compare_idx_vec =
                KonaCongTopK::flatten_level(network.levels[depth]);

        auto compare_start = chrono::steady_clock::now();
        vector<Z2<K>> compare_res(compare_idx_vec.size());
        dcf_compare.compare_in_vec(shares, compare_idx_vec, compare_res, true);
        auto compare_end = chrono::steady_clock::now();
        res.compare_seconds +=
                chrono::duration<double>(compare_end - compare_start).count();

        auto swap_start = chrono::steady_clock::now();
        KonaCongKonaAdapter::ss_vec_kona_l2<K>(
                shares, compare_idx_vec, compare_res, player);
        auto swap_end = chrono::steady_clock::now();
        res.swap_seconds +=
                chrono::duration<double>(swap_end - swap_start).count();
    }

    auto reorder_start = chrono::steady_clock::now();
    KonaCongTopK::move_output_wires_to_tail(shares, network.output_wires);
    auto reorder_end = chrono::steady_clock::now();
    res.reorder_seconds =
            chrono::duration<double>(reorder_end - reorder_start).count();

    auto total_end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto delta = comm_after - comm_before;

    res.total_seconds = chrono::duration<double>(total_end - total_start).count();
    res.comm_seconds = total_comm_seconds(delta);
    res.compute_seconds = res.total_seconds - res.comm_seconds;
    res.sent_bytes = delta.sent;
    res.transport_rounds = total_rounds(delta);
    res.logical_rounds = res.transport_rounds / 2;
    res.dcf_evaluate_calls = dcf_compare.get_stats().evaluate_calls;
    res.dcf_evaluate_seconds =
            dcf_compare.get_stats().evaluate_time.count();
    for (int i = 0; i < k; i++)
        res.checksum += shares[shares.size() - 1 - i][0].get_limb(0);
    return res;
}

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    RealTwoPartyPlayer* player = start_networking();

    try
    {
        const vector<int> ns = parse_sizes(size_list);
        const vector<int> ks = topk_k > 0 ? vector<int>{topk_k} : vector<int>{5, 16};

        for (int k : ks)
        {
            for (int n : ns)
            {
                if (k > n)
                    continue;

                auto shares = build_shares(n);
                if (backend_mode == "pcr" || backend_mode == "both")
                {
                    auto pcr = benchmark_cong_pcr_topk_only(shares, k, player);
                    if (playerno == 0)
                        print_result(pcr);
                }

                if (backend_mode == "dcf" || backend_mode == "both")
                {
                    shares = build_shares(n);
                    auto dcf = benchmark_cong_dcf_topk_only(shares, k, player);
                    if (playerno == 0)
                        print_result(dcf);
                }
            }
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
