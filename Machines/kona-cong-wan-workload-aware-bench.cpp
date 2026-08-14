#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sched.h>
#include <sstream>
#include <time.h>
#include <stdexcept>
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
const int WAN_WORKLOAD_AWARE_PCR_MIN_COMPARATORS = 2048;

int playerno = 0;
ez::ezOptionParser opt;
string backend_mode = "workload-aware";
string size_list = "1024";
int topk_k = 5;
int workload_pcr_min_comparators = WAN_WORKLOAD_AWARE_PCR_MIN_COMPARATORS;
bool print_levels = false;

template<int KK>
using SharePair = std::array<Z2<KK>, 2>;

enum class Backend
{
    PCR,
    DCF,
};

struct LevelPlan
{
    size_t depth = 0;
    size_t comparators = 0;
    Backend backend = Backend::PCR;
};

struct TopkBenchResult
{
    int n = 0;
    int k = 0;
    string backend;
    string process_affinity;

    // Setup/preprocessing (outside online timer)
    double network_build_seconds = 0;
    double switch_plan_seconds = 0;
    double dcf_key_init_seconds = 0;

    // Online Top-k only: compare + swap + final reorder
    double online_seconds = 0;
    double comm_seconds = 0;
    double compute_seconds = 0;
    double foreground_thread_cpu_ms = 0;

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
    int workload_pcr_min_comparators = WAN_WORKLOAD_AWARE_PCR_MIN_COMPARATORS;

    vector<LevelPlan> plan;
};


double thread_cpu_ms_now()
{
    timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
        return 0.0;
    return static_cast<double>(ts.tv_sec) * 1000.0 +
            static_cast<double>(ts.tv_nsec) / 1000000.0;
}

string current_process_affinity()
{
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof(mask), &mask) != 0)
        return "unknown";

    ostringstream os;
    bool first = true;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
    {
        if (CPU_ISSET(cpu, &mask))
        {
            if (!first)
                os << ",";
            os << cpu;
            first = false;
        }
    }
    return first ? "empty" : os.str();
}

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
    opt.add("workload-aware", 0, 1, 0,
            "Backend policy: pcr, dcf, workload-aware", "-m", "--mode");
    opt.add("1024", 0, 1, 0, "Comma-separated n list", "-n", "--sizes");
    opt.add("5", 0, 1, 0, "Top-k", "-k", "--topk");
    opt.add("2048", 0, 1, 0,
            "WAN workload-aware threshold: use PCR when comparator_count >= this value",
            "-t", "--workload-threshold");
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
    opt.get("--workload-threshold")->getInt(workload_pcr_min_comparators);

    int pl = 0;
    opt.get("--print-levels")->getInt(pl);
    print_levels = (pl != 0);

    if (backend_mode != "pcr" && backend_mode != "dcf" &&
            backend_mode != "workload-aware")
        throw runtime_error("backend mode must be pcr, dcf, or workload-aware");

    if (topk_k <= 0)
        throw runtime_error("top-k must be positive");

    if (workload_pcr_min_comparators < 0)
        throw runtime_error("workload threshold must be non-negative");
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

Backend select_backend(size_t comparator_count, int workload_threshold)
{
    // WAN workload-aware source: kona-cong-layer-wan-load-sweep-bench.x
    // found m=2048 as the first candidate region where PCR can become
    // resource/multitask-attractive. This is not a LAN latency threshold.
    return comparator_count >= static_cast<size_t>(workload_threshold) ?
            Backend::PCR : Backend::DCF;
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
        int workload_threshold)
{
    vector<LevelPlan> plan;
    plan.reserve(network.levels.size());

    for (size_t depth = 0; depth < network.levels.size(); ++depth)
    {
        const size_t m = network.levels[depth].size();

        Backend backend = Backend::PCR;
        if (mode == "dcf")
            backend = Backend::DCF;
        else if (mode == "pcr")
            backend = Backend::PCR;
        else
            backend = select_backend(m, workload_threshold);

        plan.push_back(LevelPlan{depth, m, backend});
    }

    return plan;
}

void fill_plan_stats(TopkBenchResult& res)
{
    for (const auto& p : res.plan)
    {
        if (p.backend == Backend::DCF)
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
             << " selected=" << (p.backend == Backend::DCF ? "dcf" : "pcr")
             << " threshold=" << r.workload_pcr_min_comparators
             << endl;
    }
}

void print_result(const TopkBenchResult& r)
{
    cout << "CONG_TOPK_WAN_WORKLOAD_AWARE"
         << " backend=" << r.backend
         << " n=" << r.n
         << " k=" << r.k
         << " workload_threshold=" << r.workload_pcr_min_comparators
         << " process_affinity=" << r.process_affinity
         << " network_build_ms=" << r.network_build_seconds * 1000.0
         << " switch_plan_ms=" << r.switch_plan_seconds * 1000.0
         << " dcf_key_init_ms=" << r.dcf_key_init_seconds * 1000.0
         << " online_ms=" << r.online_seconds * 1000.0
         << " compute_ms=" << r.compute_seconds * 1000.0
         << " foreground_thread_cpu_ms=" << r.foreground_thread_cpu_ms
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
                                          "cong-wan-workload-aware";
    res.n = static_cast<int>(shares.size());
    res.k = k;
    res.workload_pcr_min_comparators = workload_pcr_min_comparators;
    res.process_affinity = current_process_affinity();

    // -------- setup/public planning: OUTSIDE online timer --------
    auto build_start = chrono::steady_clock::now();
    KonaCongTopK::CongNetwork network =
            KonaCongTopK::build_cong_network(static_cast<int>(shares.size()), k);
    auto build_end = chrono::steady_clock::now();
    res.network_build_seconds =
            chrono::duration<double>(build_end - build_start).count();

    auto plan_start = chrono::steady_clock::now();
    res.plan = build_level_plan(network, mode, workload_pcr_min_comparators);
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
    const double foreground_cpu_start_ms = thread_cpu_ms_now();
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

        if (res.plan[depth].backend == Backend::DCF)
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
    res.foreground_thread_cpu_ms =
            thread_cpu_ms_now() - foreground_cpu_start_ms;
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
