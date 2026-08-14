#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sched.h>
#include <sstream>
#include <time.h>
#include <stdexcept>
#include <string>
#include <thread>
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
const int BACKGROUND_CALIBRATION_MS = 100;

int playerno = 0;
ez::ezOptionParser opt;
string backend_mode = "pcr";
int comparator_count = 160;
bool with_background = true;

template<int KK>
using SharePair = std::array<Z2<KK>, 2>;

struct BenchResult
{
    string backend;
    int comparator_count = 0;
    bool with_background = true;
    string process_affinity;
    double foreground_thread_cpu_ms = 0;
    double background_thread_cpu_ms = 0;
    double background_calibration_ms = 0;
    double background_iters_per_ms = 0;
    uint64_t background_iters = 0;
    double background_ms_equiv = 0;
    double dcf_key_init_seconds = 0;
    double wall_seconds = 0;
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

uint64_t background_compute_until(
        const atomic<bool>& stop,
        double* cpu_ms_out = 0)
{
    const double cpu_start_ms = thread_cpu_ms_now();
    uint64_t x = 0x123456789abcdef0ULL ^ static_cast<uint64_t>(playerno);
    uint64_t count = 0;
    while (!stop.load(memory_order_relaxed))
    {
        for (int i = 0; i < 1024; ++i)
        {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            ++count;
        }
    }
    if (cpu_ms_out)
        *cpu_ms_out = thread_cpu_ms_now() - cpu_start_ms;
    return count ^ (x & 1ULL);
}

uint64_t run_background_for_ms(int millis)
{
    atomic<bool> stop(false);
    uint64_t count = 0;
    thread worker([&]() { count = background_compute_until(stop); });
    this_thread::sleep_for(chrono::milliseconds(millis));
    stop.store(true, memory_order_relaxed);
    worker.join();
    return count;
}

void parse_argv(int argc, const char** argv)
{
    opt.add("5000", 0, 1, 0, "Port number base", "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on", "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host", "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file", "-ip", "--ip-file-name");
    opt.add("pcr", 0, 1, 0, "Backend: pcr or dcf", "-m", "--mode");
    opt.add("160", 0, 1, 0, "Comparator count in this single layer", "-c", "--comparators");
    opt.add("1", 0, 1, 0, "Run same-core background worker during online phase", "-bg", "--background");
    opt.parse(argc, argv);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else
        sscanf(argv[1], "%d", &playerno);

    opt.get("--mode")->getString(backend_mode);
    opt.get("--comparators")->getInt(comparator_count);
    int bg = 0;
    opt.get("--background")->getInt(bg);
    with_background = (bg != 0);

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
    cout << "KONA_LAYER_MULTITASK_CROSSOVER"
         << " backend=" << r.backend
         << " comparator_count=" << r.comparator_count
         << " with_background=" << (r.with_background ? 1 : 0)
         << " process_affinity=" << r.process_affinity
         << " foreground_wall_ms=" << r.wall_seconds * 1000.0
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
         << " foreground_thread_cpu_ms=" << r.foreground_thread_cpu_ms
         << " background_thread_cpu_ms=" << r.background_thread_cpu_ms
         << " background_calibration_ms=" << r.background_calibration_ms
         << " background_iters_per_ms=" << r.background_iters_per_ms
         << " background_completed_iters=" << r.background_iters
         << " background_ms_equiv=" << r.background_ms_equiv
         << " background_throughput_iters_per_ms=" << (r.wall_seconds > 0 ? static_cast<double>(r.background_iters) / (r.wall_seconds * 1000.0) : 0.0)
         << " checksum=" << r.checksum
         << endl;
}

BenchResult run_layer_bench(
        const string& backend,
        int m,
        bool enable_background,
        RealTwoPartyPlayer* player)
{
    BenchResult res;
    res.backend = backend;
    res.comparator_count = m;
    res.with_background = enable_background;
    res.process_affinity = current_process_affinity();

    auto shares = build_shares(m);
    vector<int> compare_idx_vec = build_disjoint_pairs(m);
    vector<Z2<K>> compare_res(compare_idx_vec.size());

    if (enable_background)
    {
        auto calib_start = chrono::steady_clock::now();
        uint64_t calib_iters = run_background_for_ms(BACKGROUND_CALIBRATION_MS);
        auto calib_end = chrono::steady_clock::now();
        res.background_calibration_ms =
                chrono::duration<double, milli>(calib_end - calib_start).count();
        res.background_iters_per_ms =
                static_cast<double>(calib_iters) / res.background_calibration_ms;
    }

    auto dcf_init_start = chrono::steady_clock::now();
    KonaDcfCompare::Compare64<K> dcf_compare(player, playerno);
    auto dcf_init_end = chrono::steady_clock::now();
    res.dcf_key_init_seconds =
            chrono::duration<double>(dcf_init_end - dcf_init_start).count();

    atomic<bool> stop_background(false);
    uint64_t background_count = 0;
    double background_cpu_ms = 0.0;
    thread background_thread;
    if (enable_background)
        background_thread = thread([&]() {
            background_count = background_compute_until(
                    stop_background, &background_cpu_ms);
        });

    const double foreground_cpu_start_ms = thread_cpu_ms_now();
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
    res.foreground_thread_cpu_ms =
            thread_cpu_ms_now() - foreground_cpu_start_ms;

    if (enable_background)
    {
        stop_background.store(true, memory_order_relaxed);
        background_thread.join();
        res.background_thread_cpu_ms = background_cpu_ms;
        res.background_iters = background_count;
        if (res.background_iters_per_ms > 0)
            res.background_ms_equiv =
                    static_cast<double>(background_count) / res.background_iters_per_ms;
    }

    auto delta = comm_after - comm_before;
    res.wall_seconds = chrono::duration<double>(online_end - online_start).count();
    res.comm_seconds = total_comm_seconds(delta);
    res.compute_seconds = max(0.0, res.wall_seconds - res.comm_seconds);
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
                backend_mode, comparator_count, with_background, player);
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
