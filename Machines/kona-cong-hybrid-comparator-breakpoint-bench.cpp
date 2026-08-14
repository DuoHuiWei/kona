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
const int DEFAULT_COMPARATOR_THRESHOLD = 128;

int playerno = 0;
ez::ezOptionParser opt;
string backend_mode = "hybrid";
string size_list = "1024,2048,4096";
int topk_k = 5;
int comparator_threshold = DEFAULT_COMPARATOR_THRESHOLD;
bool print_levels = false;

template<int KK>
using SharePair = std::array<Z2<KK>, 2>;

struct LevelPlan
{
    size_t depth = 0;
    size_t comparators = 0;
    bool use_dcf = false;
};

struct TopkBenchResult
{
    int n = 0;
    int k = 0;
    string backend;

    // Setup/preprocessing (outside online timer)
    double network_build_seconds = 0;
    double switch_plan_seconds = 0;
    double dcf_key_init_seconds = 0;

    // Online Top-k only: compare + swap + final reorder
    double online_seconds = 0;
    double comm_seconds = 0;
    double compute_seconds = 0;

    size_t sent_bytes = 0;
    size_t transport_rounds = 0;
    size_t logical_rounds = 0;
    uint64_t checksum = 0;

    long long dcf_evaluate_calls = 0;
    double dcf_evaluate_seconds = 0;

    int pcr_layers = 0;
    int dcf_layers = 0;
    size_t pcr_comparators = 0;
    size_t dcf_comparators = 0;
    int switch_threshold = DEFAULT_COMPARATOR_THRESHOLD;

    vector<LevelPlan> plan;
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
    opt.add("hybrid", 0, 1, 0, "Backend: pcr, dcf, hybrid", "-m", "--mode");
    opt.add("1024,2048,4096", 0, 1, 0, "Comma-separated n list", "-n", "--sizes");
    opt.add("5", 0, 1, 0, "Top-k", "-k", "--topk");
    opt.add("128", 0, 1, 0,
            "Hybrid threshold in CURRENT-LEVEL comparator count",
            "-t", "--comparator-threshold");
    opt.add("0", 0, 1, 0,
            "Print per-level comparator/backend plan after online run (0/1)",
            "-pl", "--print-levels");
    opt.parse(argc, argv);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else
        sscanf(argv[1], "%d", &playerno);

    opt.get("--mode")->getString(backend_mode);
    opt.get("--sizes")->getString(size_list);
    opt.get("--topk")->getInt(topk_k);
    opt.get("--comparator-threshold")->getInt(comparator_threshold);

    int pl = 0;
    opt.get("--print-levels")->getInt(pl);
    print_levels = (pl != 0);

    if (backend_mode != "pcr" && backend_mode != "dcf" &&
            backend_mode != "hybrid")
        throw runtime_error("backend mode must be pcr, dcf, or hybrid");

    if (topk_k <= 0)
        throw runtime_error("top-k must be positive");

    if (comparator_threshold < 0)
        throw runtime_error("comparator threshold must be non-negative");
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
    for (int i = 0; i < n; ++i)
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

// Current-level load metric:
//
//     m_depth = | network.levels[depth] |
//
// Each element of network.levels[depth] is one comparator pair, so m_depth
// is exactly the number of secure comparisons issued in this Cong level.
// Equivalently, after flatten_level(), m_depth = compare_idx_vec.size() / 2.
vector<LevelPlan> build_level_plan(
        const KonaCongTopK::CongNetwork& network,
        const string& mode,
        int threshold)
{
    vector<LevelPlan> plan;
    plan.reserve(network.levels.size());

    for (size_t depth = 0; depth < network.levels.size(); ++depth)
    {
        const size_t m = network.levels[depth].size();

        bool use_dcf = false;
        if (mode == "dcf")
            use_dcf = true;
        else if (mode == "pcr")
            use_dcf = false;
        else
            use_dcf = (m <= static_cast<size_t>(threshold));

        plan.push_back(LevelPlan{depth, m, use_dcf});
    }

    return plan;
}

void fill_plan_stats(TopkBenchResult& res)
{
    for (const auto& p : res.plan)
    {
        if (p.use_dcf)
        {
            ++res.dcf_layers;
            res.dcf_comparators += p.comparators;
        }
        else
        {
            ++res.pcr_layers;
            res.pcr_comparators += p.comparators;
        }
    }
}

void print_level_plan(const TopkBenchResult& r)
{
    if (!print_levels)
        return;

    for (const auto& p : r.plan)
    {
        cout << "CONG_LEVEL_PLAN"
             << " backend=" << r.backend
             << " n=" << r.n
             << " k=" << r.k
             << " depth=" << p.depth
             << " comparators=" << p.comparators
             << " selected=" << (p.use_dcf ? "dcf" : "pcr")
             << " threshold=" << r.switch_threshold
             << endl;
    }
}

void print_result(const TopkBenchResult& r)
{
    cout << "CONG_TOPK_COMPARATOR_BREAKPOINT"
         << " backend=" << r.backend
         << " n=" << r.n
         << " k=" << r.k
         << " comparator_threshold=" << r.switch_threshold
         << " network_build_ms=" << r.network_build_seconds * 1000.0
         << " switch_plan_ms=" << r.switch_plan_seconds * 1000.0
         << " dcf_key_init_ms=" << r.dcf_key_init_seconds * 1000.0
         << " online_ms=" << r.online_seconds * 1000.0
         << " compute_ms=" << r.compute_seconds * 1000.0
         << " comm_ms=" << r.comm_seconds * 1000.0
         << " sent_bytes=" << r.sent_bytes
         << " transport_rounds=" << r.transport_rounds
         << " logical_rounds=" << r.logical_rounds
         << " pcr_layers=" << r.pcr_layers
         << " dcf_layers=" << r.dcf_layers
         << " pcr_comparators=" << r.pcr_comparators
         << " dcf_comparators=" << r.dcf_comparators
         << " dcf_evaluate_calls=" << r.dcf_evaluate_calls
         << " dcf_evaluate_ms=" << r.dcf_evaluate_seconds * 1000.0
         << " checksum=" << r.checksum
         << endl;

    print_level_plan(r);
}

TopkBenchResult benchmark_cong_topk(
        const vector<SharePair<K>>& input,
        int k,
        const string& mode,
        RealTwoPartyPlayer* player)
{
    auto shares = input;

    TopkBenchResult res;
    res.backend = mode == "pcr" ? "cong-pcr" :
                  mode == "dcf" ? "cong-dcf" :
                                  "cong-hybrid-comparator";
    res.n = static_cast<int>(shares.size());
    res.k = k;
    res.switch_threshold = comparator_threshold;

    // -------- setup/public planning: OUTSIDE online timer --------
    auto build_start = chrono::steady_clock::now();
    KonaCongTopK::CongNetwork network =
            KonaCongTopK::build_cong_network(static_cast<int>(shares.size()), k);
    auto build_end = chrono::steady_clock::now();
    res.network_build_seconds =
            chrono::duration<double>(build_end - build_start).count();

    auto plan_start = chrono::steady_clock::now();
    res.plan = build_level_plan(network, mode, comparator_threshold);
    fill_plan_stats(res);
    auto plan_end = chrono::steady_clock::now();
    res.switch_plan_seconds =
            chrono::duration<double>(plan_end - plan_start).count();

    // DCF key/backend initialization is setup/preprocessing and is therefore
    // intentionally outside online timing.
    auto dcf_init_start = chrono::steady_clock::now();
    KonaDcfCompare::Compare64<K> dcf_compare(player, playerno);
    auto dcf_init_end = chrono::steady_clock::now();
    res.dcf_key_init_seconds =
            chrono::duration<double>(dcf_init_end - dcf_init_start).count();

    // -------- ONLINE TOP-K ONLY --------
    auto comm_before = player->total_comm();
    auto online_start = chrono::steady_clock::now();

    for (size_t depth = 0; depth < network.levels.size(); ++depth)
    {
        vector<int> compare_idx_vec =
                KonaCongTopK::flatten_level(network.levels[depth]);

        if (compare_idx_vec.size() != 2 * res.plan[depth].comparators)
            throw runtime_error(
                    "flatten_level size is inconsistent with comparator count");

        vector<Z2<K>> compare_res(compare_idx_vec.size());

        if (res.plan[depth].use_dcf)
        {
            dcf_compare.compare_in_vec(
                    shares, compare_idx_vec, compare_res, true);
        }
        else
        {
            KonaPcrCompare::pcr_compare_in_vec_l2(
                    shares, compare_idx_vec, compare_res, true, player);
        }

        KonaCongKonaAdapter::ss_vec_kona_l2<K>(
                shares, compare_idx_vec, compare_res, player);
    }

    KonaCongTopK::move_output_wires_to_tail(shares, network.output_wires);

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

    res.dcf_evaluate_calls = dcf_compare.get_stats().evaluate_calls;
    res.dcf_evaluate_seconds = dcf_compare.get_stats().evaluate_time.count();

    for (int i = 0; i < k; ++i)
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

        for (int n : ns)
        {
            if (topk_k > n)
                continue;

            auto shares = build_shares(n);
            auto result = benchmark_cong_topk(
                    shares, topk_k, backend_mode, player);

            if (playerno == 0)
                print_result(result);
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
