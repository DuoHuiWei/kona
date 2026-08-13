#include <chrono>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "../Math/Z2k.hpp"
#include "kona-active-bitpack.hpp"
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
    Z2<K> mask = ring(mix64(i + 0x33445566ULL));
    return playerno == 0 ? mask : value - mask;
}

vector<Z2<K>> make_additive_shares(
        const vector<uint64_t>& values,
        int playerno,
        uint64_t domain)
{
    vector<Z2<K>> shares(values.size());
    for (size_t i = 0; i < values.size(); i++)
        shares[i] = additive_share_for(playerno, i + domain, ring(values[i]));
    return shares;
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
    opt.add("65536", 0, 1, 0, "Large benchmark comparison count",
            "-l", "--large-n");
    opt.add("5", 0, 1, 0, "Large benchmark repeats",
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

void test_active_bitpack(int playerno)
{
    bool ok = true;
    for (int distance = 1; distance <= 32; distance <<= 1)
    {
        uint64_t mask = KonaActiveBitpack::endpoint_mask(distance);
        for (int i = 0; i < 256; i++)
        {
            uint64_t value = mix64(uint64_t(i) + uint64_t(distance) * 1000);
            uint64_t dense =
                    KonaActiveBitpack::compact_endpoint_bits(value, distance);
            uint64_t expanded =
                    KonaActiveBitpack::expand_endpoint_bits(dense, distance);
            if (expanded != (value & mask))
            {
                ok = false;
                if (playerno == 0)
                    cerr << "bitpack mismatch distance=" << distance
                         << " i=" << i << endl;
                break;
            }
        }
    }

    if (playerno == 0)
        cout << "ACTIVE_BITPACK " << (ok ? "PASS" : "FAIL") << endl;
    if (!ok)
        throw runtime_error("active bitpack failed");
}

void test_uint64_adder(RealTwoPartyPlayer* player, int playerno)
{
    const size_t n = 1000;
    vector<uint64_t> u_share(n, 0);
    vector<uint64_t> v_share(n, 0);
    vector<uint64_t> expected(n);

    for (size_t i = 0; i < n; i++)
    {
        uint64_t u = mix64(i + 100);
        uint64_t v = mix64(i + 200);
        expected[i] = u + v;
        if (playerno == 0)
            u_share[i] = u;
        else
            v_share[i] = v;
    }

    vector<uint64_t> sum_share = KonaPcrCompare::pcr_add_uint64_xor_batch_l2(
            u_share, v_share, player);

    vector<Z2<K>> sum_z2(sum_share.size());
    for (size_t i = 0; i < sum_share.size(); i++)
        sum_z2[i] = ring(sum_share[i]);

    vector<Z2<K>> opened =
            KonaShareConversion::open_xor_batch_l2<K>(sum_z2, player);

    bool ok = true;
    for (size_t i = 0; i < n; i++)
    {
        if (KonaPcrCompare::z2_to_u64(opened[i]) != expected[i])
        {
            ok = false;
            if (playerno == 0)
                cerr << "adder mismatch at " << i << endl;
            break;
        }
    }

    if (playerno == 0)
        cout << "PCR_ADDER_UINT64 " << (ok ? "PASS" : "FAIL") << endl;
    if (!ok)
        throw runtime_error("PCR uint64 adder failed");
}

void test_compare_correctness(
        RealTwoPartyPlayer* player,
        int playerno,
        int small_n)
{
    // Values are intentionally bounded below 2^50. The current PCR comparison
    // uses the sign bit of x-y, so correctness is tested within |x-y| < 2^63.
    vector<uint64_t> x(small_n);
    vector<uint64_t> y(small_n);
    for (int i = 0; i < small_n; i++)
    {
        x[i] = bounded_value(i);
        y[i] = bounded_value(i + 100000);
        if (i % 97 == 0)
            y[i] = x[i];
    }

    vector<Z2<K>> xs = make_additive_shares(x, playerno, 0);
    vector<Z2<K>> ys = make_additive_shares(y, playerno, 1000000);

    vector<Z2<K>> gt = KonaPcrCompare::pcr_compare_gt_batch_l2(xs, ys, player);
    vector<Z2<K>> lt = KonaPcrCompare::pcr_compare_lt_batch_l2(xs, ys, player);

    vector<uint64_t> gt_open = open_additive_u64(gt, player);
    vector<uint64_t> lt_open = open_additive_u64(lt, player);

    vector<Z2<K>> flat_shares(2 * small_n);
    vector<array<Z2<K>, 2>> pair_shares(2 * small_n);
    vector<int> compare_idx(2 * small_n);
    vector<Z2<K>> flat_compare_res(2 * small_n);
    vector<Z2<K>> pair_compare_res(2 * small_n);

    for (int i = 0; i < small_n; i++)
    {
        flat_shares[i] = xs[i];
        flat_shares[i + small_n] = ys[i];
        pair_shares[i] = {xs[i], ring(uint64_t(i))};
        pair_shares[i + small_n] = {ys[i], ring(uint64_t(i + small_n))};
        compare_idx[2 * i] = i;
        compare_idx[2 * i + 1] = i + small_n;
    }

    KonaPcrCompare::pcr_compare_in_vec_l2(
            flat_shares, compare_idx, flat_compare_res, true, player);
    KonaPcrCompare::pcr_compare_in_vec_l2(
            pair_shares, compare_idx, pair_compare_res, true, player);

    vector<uint64_t> flat_open =
            open_additive_u64(flat_compare_res, player);
    vector<uint64_t> pair_open =
            open_additive_u64(pair_compare_res, player);

    bool ok = true;
    for (int i = 0; i < small_n; i++)
    {
        uint64_t expected_gt = x[i] > y[i] ? 1 : 0;
        uint64_t expected_lt = x[i] < y[i] ? 1 : 0;
        if (gt_open[i] != expected_gt || lt_open[i] != expected_lt ||
                flat_open[2 * i] != expected_gt ||
                flat_open[2 * i + 1] != expected_gt ||
                pair_open[2 * i] != expected_gt ||
                pair_open[2 * i + 1] != expected_gt)
        {
            ok = false;
            if (playerno == 0)
                cerr << "compare mismatch at " << i
                     << " x=" << x[i]
                     << " y=" << y[i]
                     << " gt=" << gt_open[i]
                     << " expected_gt=" << expected_gt
                     << " lt=" << lt_open[i]
                     << " expected_lt=" << expected_lt
                     << " flat=" << flat_open[2 * i]
                     << "/" << flat_open[2 * i + 1]
                     << " pair=" << pair_open[2 * i]
                     << "/" << pair_open[2 * i + 1]
                     << endl;
            break;
        }
    }

    if (playerno == 0)
        cout << "PCR_COMPARE_SMALL n=" << small_n << " "
             << (ok ? "PASS" : "FAIL") << endl;
    if (!ok)
        throw runtime_error("PCR compare correctness failed");
}

void benchmark_compare(
        RealTwoPartyPlayer* player,
        int playerno,
        int n,
        int repeats)
{
    vector<uint64_t> x(n);
    vector<uint64_t> y(n);
    for (int i = 0; i < n; i++)
    {
        x[i] = bounded_value(i + 200000);
        y[i] = bounded_value(i + 500000);
    }

    vector<Z2<K>> xs = make_additive_shares(x, playerno, 2000000);
    vector<Z2<K>> ys = make_additive_shares(y, playerno, 3000000);

    size_t sent_before = player->total_comm().sent;
    auto start = chrono::steady_clock::now();
    for (int r = 0; r < repeats; r++)
    {
        volatile size_t sink =
                KonaPcrCompare::pcr_compare_gt_batch_l2(xs, ys, player).size();
        (void)sink;
    }
    auto end = chrono::steady_clock::now();
    size_t sent_after = player->total_comm().sent;

    chrono::duration<double> elapsed = end - start;
    double ms = elapsed.count() * 1000.0;
    if (playerno == 0)
    {
        cout << "PCR_COMPARE_BENCH n=" << n
             << " repeats=" << repeats
             << " total_ms=" << ms
             << " avg_ms=" << (ms / repeats)
             << " ns_per_compare=" << (elapsed.count() * 1e9 / n / repeats)
             << " sent_bytes=" << (sent_after - sent_before)
             << endl;
    }
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
        test_active_bitpack(playerno);
        test_uint64_adder(player, playerno);
        test_compare_correctness(player, playerno, small_n);
        benchmark_compare(player, playerno, small_n, 1);
        benchmark_compare(player, playerno, large_n, repeats);
    }
    catch (...)
    {
        delete player;
        throw;
    }

    delete player;
    return 0;
}
