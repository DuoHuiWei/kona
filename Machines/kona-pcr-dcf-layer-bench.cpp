#include "kona-bench-affinity.hpp"
#include <algorithm>
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
#include "kona-dcf-compare.hpp"
#include "kona-pcr-compare.hpp"
#include "kona-share-conversion.hpp"

using namespace std;

namespace
{

const int K = 64;

int playerno = 0;
ez::ezOptionParser opt;
string mode = "pcr";
int batch_size = 64;
int repeats = 1;
int inner_repeats = 1;
uint64_t seed = 20260819;
string check_mode = "every";

template<int KK>
using RingShare = Z2<KK>;

struct TrialResult
{
    string mode;
    int batch_size = 0;
    int trial = 0;
    int inner_repeats = 1;
    uint64_t seed = 0;
    double dcf_key_init_seconds = 0;
    double online_seconds = 0;
    double avg_online_seconds = 0;
    double compute_seconds = 0;
    double avg_compute_seconds = 0;
    double comm_seconds = 0;
    double avg_comm_seconds = 0;
    size_t sent_bytes = 0;
    double avg_sent_bytes = 0;
    size_t transport_rounds = 0;
    double avg_transport_rounds = 0;
    size_t logical_rounds = 0;
    double avg_logical_rounds = 0;
    long long dcf_evaluate_calls = 0;
    double avg_dcf_evaluate_calls = 0;
    double dcf_evaluate_seconds = 0;
    double avg_dcf_evaluate_seconds = 0;
    bool correctness = false;
    bool correctness_checked = false;
};

uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

RingShare<K> ring(uint64_t x)
{
    return RingShare<K>(static_cast<mp_limb_t>(x));
}

RingShare<K> additive_share_for(
        int p,
        size_t share_id,
        RingShare<K> value,
        uint64_t input_seed)
{
    RingShare<K> mask = ring(mix64(input_seed ^ (share_id + 0xabcddcbaULL)));
    return p == 0 ? mask : value - mask;
}

void parse_argv(int argc, const char** argv)
{
    opt.add("5000", 0, 1, 0, "Port number base", "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on", "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host", "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file", "-ip", "--ip-file-name");
    opt.add("pcr", 0, 1, 0, "Comparison backend: pcr or dcf", "-m", "--mode");
    opt.add("64", 0, 1, 0, "Independent comparator count in this layer", "-b", "--batch-size");
    opt.add("1", 0, 1, 0, "Repeated online compare trials", "-r", "--repeats");
    opt.add("1", 0, 1, 0, "Compare calls inside each timed trial", "-ir", "--inner-repeats");
    opt.add("20260819", 0, 1, 0, "Deterministic input seed", "-s", "--seed");
    opt.add("every", 0, 1, 0,
            "Correctness mode: every, once, or skip", "", "--check-mode");
    opt.parse(argc, argv);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else if (argc > 1)
        sscanf(argv[1], "%d", &playerno);
    else
        throw runtime_error("missing player number");

    opt.get("--mode")->getString(mode);
    opt.get("--batch-size")->getInt(batch_size);
    opt.get("--repeats")->getInt(repeats);
    opt.get("--inner-repeats")->getInt(inner_repeats);
    opt.get("--check-mode")->getString(check_mode);

    string seed_text;
    opt.get("--seed")->getString(seed_text);
    seed = static_cast<uint64_t>(stoull(seed_text));

    if (mode != "pcr" && mode != "dcf")
        throw runtime_error("mode must be pcr or dcf");
    if (batch_size <= 0)
        throw runtime_error("batch-size must be positive");
    if (repeats <= 0)
        throw runtime_error("repeats must be positive");
    if (inner_repeats <= 0)
        throw runtime_error("inner-repeats must be positive");
    if (check_mode != "every" && check_mode != "once" && check_mode != "skip")
        throw runtime_error("check-mode must be every, once, or skip");
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

uint64_t clear_value(size_t i, uint64_t input_seed)
{
    // Keep values far below 2^63 so PCR's sign-bit compare precondition holds.
    return mix64(input_seed + i * 1315423911ULL) & ((uint64_t(1) << 40) - 1);
}

void build_inputs(
        int m,
        int p,
        uint64_t input_seed,
        vector<RingShare<K>>& shares,
        vector<int>& compare_idx_vec,
        vector<uint64_t>& left_plain,
        vector<uint64_t>& right_plain)
{
    shares.resize(2 * m);
    compare_idx_vec.resize(2 * m);
    left_plain.resize(m);
    right_plain.resize(m);

    for (int i = 0; i < m; ++i)
    {
        uint64_t left = clear_value(2 * size_t(i), input_seed);
        uint64_t right = clear_value(2 * size_t(i) + 1, input_seed);
        if (i % 97 == 0)
            right = left;

        left_plain[i] = left;
        right_plain[i] = right;

        shares[2 * i] = additive_share_for(
                p, 2 * size_t(i), ring(left), input_seed);
        shares[2 * i + 1] = additive_share_for(
                p, 2 * size_t(i) + 1, ring(right), input_seed);
        compare_idx_vec[2 * i] = 2 * i;
        compare_idx_vec[2 * i + 1] = 2 * i + 1;
    }
}

bool check_correctness(
        const vector<RingShare<K>>& compare_res,
        const vector<uint64_t>& left_plain,
        const vector<uint64_t>& right_plain,
        RealTwoPartyPlayer* player)
{
    vector<RingShare<K>> opened =
            KonaShareConversion::open_additive_batch_l2<K>(compare_res, player);

    for (size_t i = 0; i < left_plain.size(); ++i)
    {
        uint64_t expected = left_plain[i] > right_plain[i] ? 1 : 0;
        uint64_t got0 = KonaPcrCompare::z2_to_u64(opened[2 * i]);
        uint64_t got1 = KonaPcrCompare::z2_to_u64(opened[2 * i + 1]);
        if (got0 != expected || got1 != expected)
        {
            if (playerno == 0)
            {
                cerr << "correctness mismatch i=" << i
                     << " left=" << left_plain[i]
                     << " right=" << right_plain[i]
                     << " expected_gt=" << expected
                     << " got=" << got0 << "/" << got1 << endl;
            }
            return false;
        }
    }

    return true;
}

TrialResult run_one_trial(
        int trial,
        const vector<RingShare<K>>& shares,
        const vector<int>& compare_idx_vec,
        const vector<uint64_t>& left_plain,
        const vector<uint64_t>& right_plain,
        KonaDcfCompare::Compare64<K>& dcf_compare,
        double dcf_key_init_seconds,
        RealTwoPartyPlayer* player,
        bool do_correctness)
{
    vector<RingShare<K>> compare_res(compare_idx_vec.size());
    compare_res.reserve(compare_idx_vec.size());

    dcf_compare.reset_stats();
    auto comm_before = player->total_comm();
    auto online_start = chrono::steady_clock::now();

    for (int inner = 0; inner < inner_repeats; ++inner)
    {
        if (mode == "pcr")
        {
            KonaPcrCompare::pcr_compare_in_vec_l2(
                    shares, compare_idx_vec, compare_res, true, player);
        }
        else
        {
            dcf_compare.compare_in_vec(
                    shares, compare_idx_vec, compare_res, true);
        }
    }

    auto online_end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto comm_delta = comm_after - comm_before;

    TrialResult result;
    result.mode = mode;
    result.batch_size = batch_size;
    result.trial = trial;
    result.inner_repeats = inner_repeats;
    result.seed = seed;
    result.dcf_key_init_seconds = mode == "dcf" ? dcf_key_init_seconds : 0.0;
    result.online_seconds = chrono::duration<double>(
            online_end - online_start).count();
    result.avg_online_seconds = result.online_seconds / inner_repeats;
    result.comm_seconds = total_comm_seconds(comm_delta);
    result.avg_comm_seconds = result.comm_seconds / inner_repeats;
    result.compute_seconds = max(0.0, result.online_seconds - result.comm_seconds);
    result.avg_compute_seconds = result.compute_seconds / inner_repeats;
    result.sent_bytes = comm_delta.sent;
    result.avg_sent_bytes = double(result.sent_bytes) / inner_repeats;
    result.transport_rounds = total_rounds(comm_delta);
    result.avg_transport_rounds = double(result.transport_rounds) / inner_repeats;
    result.logical_rounds = result.transport_rounds / 2;
    result.avg_logical_rounds = double(result.logical_rounds) / inner_repeats;

    if (mode == "dcf")
    {
        result.dcf_evaluate_calls = dcf_compare.get_stats().evaluate_calls;
        result.avg_dcf_evaluate_calls =
                double(result.dcf_evaluate_calls) / inner_repeats;
        result.dcf_evaluate_seconds =
                dcf_compare.get_stats().evaluate_time.count();
        result.avg_dcf_evaluate_seconds =
                result.dcf_evaluate_seconds / inner_repeats;
    }

    result.correctness_checked = do_correctness;
    result.correctness = true;
    if (do_correctness)
    {
        result.correctness = check_correctness(
                compare_res, left_plain, right_plain, player);
    }
    return result;
}

void print_result(const TrialResult& r)
{
    cout << "KONA_PCR_DCF_LAYER_BENCH"
         << " mode=" << r.mode
         << " batch_size=" << r.batch_size
         << " trial=" << r.trial
         << " inner_repeats=" << r.inner_repeats
         << " seed=" << r.seed
         << " online_ms=" << r.online_seconds * 1000.0
         << " avg_online_ms=" << r.avg_online_seconds * 1000.0
         << " compute_ms=" << r.compute_seconds * 1000.0
         << " avg_compute_ms=" << r.avg_compute_seconds * 1000.0
         << " comm_ms=" << r.comm_seconds * 1000.0
         << " avg_comm_ms=" << r.avg_comm_seconds * 1000.0
         << " sent_bytes=" << r.sent_bytes
         << " avg_sent_bytes=" << r.avg_sent_bytes
         << " transport_rounds=" << r.transport_rounds
         << " avg_transport_rounds=" << r.avg_transport_rounds
         << " logical_rounds=" << r.logical_rounds
         << " avg_logical_rounds=" << r.avg_logical_rounds
         << " dcf_evaluate_calls=" << r.dcf_evaluate_calls
         << " avg_dcf_evaluate_calls=" << r.avg_dcf_evaluate_calls
         << " dcf_evaluate_ms=" << r.dcf_evaluate_seconds * 1000.0
         << " avg_dcf_evaluate_ms=" << r.avg_dcf_evaluate_seconds * 1000.0
         << " dcf_key_init_ms=" << r.dcf_key_init_seconds * 1000.0
         << " correctness_checked=" << (r.correctness_checked ? "YES" : "NO")
         << " correctness=" << (r.correctness ? "PASS" : "FAIL")
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
        vector<RingShare<K>> shares;
        vector<int> compare_idx_vec;
        vector<uint64_t> left_plain;
        vector<uint64_t> right_plain;
        build_inputs(
                batch_size, playerno, seed, shares, compare_idx_vec,
                left_plain, right_plain);

        auto dcf_init_start = chrono::steady_clock::now();
        KonaDcfCompare::Compare64<K> dcf_compare(player, playerno);
        auto dcf_init_end = chrono::steady_clock::now();
        double dcf_key_init_seconds = chrono::duration<double>(
                dcf_init_end - dcf_init_start).count();

        vector<TrialResult> results;
        results.reserve(repeats);
        for (int trial = 1; trial <= repeats; ++trial)
        {
            bool do_correctness = check_mode == "every" ||
                    (check_mode == "once" && trial == 1);
            TrialResult result = run_one_trial(
                    trial, shares, compare_idx_vec, left_plain, right_plain,
                    dcf_compare, dcf_key_init_seconds, player,
                    do_correctness);
            if (!result.correctness)
                throw runtime_error("layer compare correctness failed");
            results.push_back(result);
        }

        if (playerno == 0)
        {
            for (size_t i = 0; i < results.size(); ++i)
                print_result(results[i]);
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
