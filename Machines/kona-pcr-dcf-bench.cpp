#include <chrono>
#include <cstdlib>
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

uint64_t bounded_value(size_t i)
{
    return mix64(i) & ((uint64_t(1) << 50) - 1);
}

Z2<K> additive_share_for(int playerno, size_t i, Z2<K> value)
{
    Z2<K> mask = ring(mix64(i + 0x55667788ULL));
    return playerno == 0 ? mask : value - mask;
}

int get_int_option(ez::ezOptionParser& opt, const char* name)
{
    int value = 0;
    opt.get(name)->getInt(value);
    return value;
}

void parse_options(
        int argc,
        const char** argv,
        ez::ezOptionParser& opt,
        int& playerno,
        int& small_n,
        int& large_n,
        int& repeats)
{
    opt.add("5000", 0, 1, 0, "Port number base",
            "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number",
            "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on",
            "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host",
            "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file",
            "-ip", "--ip-file-name");
    opt.add("1000", 0, 1, 0, "Small correctness comparison count",
            "-n", "--small-n");
    opt.add("262144", 0, 1, 0, "Large benchmark comparison count",
            "-l", "--large-n");
    opt.add("3", 0, 1, 0, "Large benchmark repeats",
            "-r", "--repeats");

    opt.parse(argc, argv);
    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else if (argc > 1)
        sscanf(argv[1], "%d", &playerno);
    else
        throw runtime_error("missing player number");

    small_n = get_int_option(opt, "--small-n");
    large_n = get_int_option(opt, "--large-n");
    repeats = get_int_option(opt, "--repeats");
    if (small_n <= 0 || large_n <= 0 || repeats <= 0)
        throw runtime_error("n and repeats must be positive");
}

RealTwoPartyPlayer* start_networking(
        ez::ezOptionParser& opt,
        int playerno,
        int nplayers)
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
        player_names.init(playerno, pnbase, ip_file_name, nplayers);
    }
    else
    {
        Server::start_networking(
                player_names, playerno, nplayers, hostname, pnbase, my_port);
    }

    return new RealTwoPartyPlayer(player_names, 1 - playerno, 0);
}

void build_inputs(
        int n,
        int playerno,
        vector<Z2<K>>& shares,
        vector<int>& compare_idx,
        vector<uint64_t>* x_plain = 0,
        vector<uint64_t>* y_plain = 0)
{
    shares.resize(2 * n);
    compare_idx.resize(2 * n);
    if (x_plain)
        x_plain->resize(n);
    if (y_plain)
        y_plain->resize(n);

    for (int i = 0; i < n; i++)
    {
        uint64_t x = bounded_value(i);
        uint64_t y = bounded_value(i + 1000000);
        if (i % 97 == 0)
            y = x;

        shares[i] = additive_share_for(playerno, i, ring(x));
        shares[i + n] = additive_share_for(playerno, i + 1000000, ring(y));
        compare_idx[2 * i] = i;
        compare_idx[2 * i + 1] = i + n;
        if (x_plain)
            (*x_plain)[i] = x;
        if (y_plain)
            (*y_plain)[i] = y;
    }
}

vector<uint64_t> open_additive_u64(
        const vector<Z2<K>>& shares,
        RealTwoPartyPlayer* player)
{
    vector<Z2<K>> opened =
            KonaShareConversion::open_additive_batch_l2<K>(shares, player);
    vector<uint64_t> result(opened.size());
    for (size_t i = 0; i < opened.size(); i++)
        result[i] = KonaPcrCompare::z2_to_u64(opened[i]);
    return result;
}

void print_bench_line(
        const string& name,
        int n,
        int repeats,
        chrono::duration<double> elapsed,
        size_t sent_bytes,
        size_t transport_rounds,
        size_t logical_rounds,
        long long eval_calls,
        chrono::duration<double> eval_time)
{
    cout << name
         << " n=" << n
         << " repeats=" << repeats
         << " total_ms=" << elapsed.count() * 1000.0
         << " avg_ms=" << elapsed.count() * 1000.0 / repeats
         << " ns_per_compare=" << elapsed.count() * 1e9 / n / repeats
         << " sent_bytes=" << sent_bytes
         << " transport_rounds=" << transport_rounds
         << " logical_rounds=" << logical_rounds
         << " dcf_evaluate_calls=" << eval_calls
         << " dcf_eval_total_ms=" << eval_time.count() * 1000.0
         << endl;
}

size_t total_rounds(const NamedCommStats& stats)
{
    size_t rounds = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        rounds += it->second.rounds;
    return rounds;
}

void check_correctness(
        RealTwoPartyPlayer* player,
        int playerno,
        int n,
        KonaDcfCompare::Compare64<K>& dcf_compare)
{
    vector<Z2<K>> shares;
    vector<int> compare_idx;
    vector<uint64_t> x, y;
    build_inputs(n, playerno, shares, compare_idx, &x, &y);

    vector<Z2<K>> pcr_res(2 * n);
    vector<Z2<K>> dcf_res(2 * n);
    KonaPcrCompare::pcr_compare_in_vec_l2(
            shares, compare_idx, pcr_res, true, player);
    dcf_compare.compare_in_vec(shares, compare_idx, dcf_res, true);

    vector<uint64_t> pcr_open = open_additive_u64(pcr_res, player);
    vector<uint64_t> dcf_open = open_additive_u64(dcf_res, player);

    bool ok = true;
    for (int i = 0; i < n; i++)
    {
        uint64_t expected = x[i] > y[i] ? 1 : 0;
        if (pcr_open[2 * i] != expected ||
                pcr_open[2 * i + 1] != expected ||
                dcf_open[2 * i] != expected ||
                dcf_open[2 * i + 1] != expected)
        {
            ok = false;
            if (playerno == 0)
                cerr << "mismatch i=" << i
                     << " x=" << x[i]
                     << " y=" << y[i]
                     << " expected_gt=" << expected
                     << " pcr=" << pcr_open[2 * i]
                     << "/" << pcr_open[2 * i + 1]
                     << " dcf=" << dcf_open[2 * i]
                     << "/" << dcf_open[2 * i + 1]
                     << endl;
            break;
        }
    }

    if (playerno == 0)
        cout << "PCR_DCF_CORRECTNESS n=" << n << " "
             << (ok ? "PASS" : "FAIL") << endl;
    if (!ok)
        throw runtime_error("PCR/DCF correctness failed");
}

} // namespace

int main(int argc, const char** argv)
{
    ez::ezOptionParser opt;
    int playerno = 0;
    int small_n = 0;
    int large_n = 0;
    int repeats = 0;

    parse_options(argc, argv, opt, playerno, small_n, large_n, repeats);
    RealTwoPartyPlayer* player = start_networking(opt, playerno, 2);

    try
    {
        KonaDcfCompare::Compare64<K> dcf_compare(player, playerno);
        check_correctness(player, playerno, small_n, dcf_compare);

        vector<Z2<K>> shares;
        vector<int> compare_idx;
        build_inputs(large_n, playerno, shares, compare_idx);

        auto pcr_comm_before = player->total_comm();
        size_t pcr_sent_before = pcr_comm_before.sent;
        auto pcr_start = chrono::steady_clock::now();
        for (int r = 0; r < repeats; r++)
        {
            vector<Z2<K>> res(2 * large_n);
            KonaPcrCompare::pcr_compare_in_vec_l2(
                    shares, compare_idx, res, true, player);
        }
        auto pcr_end = chrono::steady_clock::now();
        auto pcr_comm_after = player->total_comm();
        size_t pcr_sent_after = pcr_comm_after.sent;
        if (playerno == 0)
        {
            print_bench_line(
                    "PCR_COMPARE_IN_VEC_WITH_B2A",
                    large_n,
                    repeats,
                    pcr_end - pcr_start,
                    pcr_sent_after - pcr_sent_before,
                    total_rounds(pcr_comm_after - pcr_comm_before),
                    total_rounds(pcr_comm_after - pcr_comm_before) / 2,
                    0,
                    chrono::duration<double>::zero());
        }

        auto dcf_start = chrono::steady_clock::now();
        KonaDcfCompare::Compare64<K> timed_dcf_compare(player, playerno);
        auto dcf_stats_before = timed_dcf_compare.get_stats();
        auto dcf_comm_before = player->total_comm();
        size_t dcf_sent_before = dcf_comm_before.sent;
        for (int r = 0; r < repeats; r++)
        {
            vector<Z2<K>> res(2 * large_n);
            timed_dcf_compare.compare_in_vec(shares, compare_idx, res, true);
        }
        auto dcf_end = chrono::steady_clock::now();
        auto dcf_comm_after = player->total_comm();
        size_t dcf_sent_after = dcf_comm_after.sent;
        auto dcf_stats_after = timed_dcf_compare.get_stats();

        if (playerno == 0)
        {
            print_bench_line(
                    "DCF_COMPARE_IN_VEC",
                    large_n,
                    repeats,
                    dcf_end - dcf_start,
                    dcf_sent_after - dcf_sent_before,
                    total_rounds(dcf_comm_after - dcf_comm_before),
                    total_rounds(dcf_comm_after - dcf_comm_before) / 2,
                    dcf_stats_after.evaluate_calls -
                            dcf_stats_before.evaluate_calls,
                    dcf_stats_after.evaluate_time -
                            dcf_stats_before.evaluate_time);
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
