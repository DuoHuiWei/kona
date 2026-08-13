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
    return Z2<K>((mp_limb_t)x);
}

uint64_t value_at(size_t i)
{
    return mix64(i) ^ (uint64_t(i) << 17) ^ 0x123456789abcdef0ULL;
}

Z2<K> additive_share_for(int playerno, size_t i, Z2<K> value)
{
    Z2<K> mask = ring(mix64(i + 0xabcdefULL));
    return playerno == 0 ? mask : value - mask;
}

bool check_opened(
        const vector<Z2<K>>& opened,
        const vector<Z2<K>>& expected,
        const string& name,
        int playerno)
{
    if (opened.size() != expected.size())
        throw runtime_error(name + " size mismatch");

    for (size_t i = 0; i < opened.size(); i++)
    {
        if (!(opened[i] == expected[i]))
        {
            if (playerno == 0)
                cerr << name << " mismatch at " << i << ": got "
                     << opened[i] << " expected " << expected[i] << endl;
            return false;
        }
    }
    return true;
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
        int& batch_size,
        int& repeats)
{
    opt.add("5000", 0, 1, 0,
            "Port number base to attempt to start connections from",
            "-pn", "--portnumbase");
    opt.add("", 0, 1, 0,
            "Player number, alternative to positional argv[1]",
            "-p", "--player");
    opt.add("", 0, 1, 0,
            "Port to listen on",
            "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0,
            "Host where party 0 coordinates startup",
            "-h", "--hostname");
    opt.add("", 0, 1, 0,
            "File containing party hostnames",
            "-ip", "--ip-file-name");
    opt.add("65536", 0, 1, 0,
            "Batch size for speed tests",
            "-b", "--batch-size");
    opt.add("20", 0, 1, 0,
            "Repeat count for speed tests",
            "-r", "--repeats");

    opt.parse(argc, argv);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else if (argc > 1)
        sscanf(argv[1], "%d", &playerno);
    else
        throw runtime_error("missing player number");

    batch_size = get_int_option(opt, "--batch-size");
    repeats = get_int_option(opt, "--repeats");
    if (batch_size <= 0 || repeats <= 0)
        throw runtime_error("batch size and repeats must be positive");
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

vector<Z2<K>> make_values(size_t n)
{
    vector<Z2<K>> values(n);
    for (size_t i = 0; i < n; i++)
        values[i] = ring(value_at(i));
    return values;
}

vector<Z2<K>> make_additive_shares(
        const vector<Z2<K>>& values,
        int playerno)
{
    vector<Z2<K>> shares(values.size());
    for (size_t i = 0; i < values.size(); i++)
        shares[i] = additive_share_for(playerno, i, values[i]);
    return shares;
}

vector<Z2<K>> make_xor_bit_shares(size_t n, int playerno)
{
    vector<Z2<K>> shares(n);
    for (size_t i = 0; i < n; i++)
    {
        bool b0 = (mix64(i + 11) & 1) != 0;
        bool b1 = (mix64(i + 29) & 1) != 0;
        shares[i] = ring(playerno == 0 ? b0 : b1);
    }
    return shares;
}

vector<Z2<K>> make_bit_values(size_t n)
{
    vector<Z2<K>> bits(n);
    for (size_t i = 0; i < n; i++)
    {
        bool b0 = (mix64(i + 11) & 1) != 0;
        bool b1 = (mix64(i + 29) & 1) != 0;
        bits[i] = ring(b0 ^ b1);
    }
    return bits;
}

void run_correctness_tests(RealTwoPartyPlayer* player, int playerno)
{
    const size_t n = 4096;
    auto values = make_values(n);
    auto additive = make_additive_shares(values, playerno);

    auto xor_shares = KonaShareConversion::A2B_batch_l2<K>(additive, player);
    auto opened_xor = KonaShareConversion::open_xor_batch_l2<K>(
            xor_shares, player);

    auto xor_bit_shares = make_xor_bit_shares(n, playerno);
    auto expected_bits = make_bit_values(n);
    auto arithmetic_bits = KonaShareConversion::B2A_batch_l2<K>(
            xor_bit_shares, player);
    auto opened_arithmetic_bits =
            KonaShareConversion::open_additive_batch_l2<K>(
                    arithmetic_bits, player);

    auto arithmetic_bit_shares = make_additive_shares(expected_bits, playerno);
    auto xor_bits = KonaShareConversion::ABit2BBit_batch_l2<K>(
            arithmetic_bit_shares, player);
    auto opened_xor_bits = KonaShareConversion::open_xor_batch_l2<K>(
            xor_bits, player);

    bool ok = true;
    ok = check_opened(opened_xor, values, "A2B", playerno) && ok;
    ok = check_opened(opened_arithmetic_bits, expected_bits, "B2A", playerno)
            && ok;
    ok = check_opened(opened_xor_bits, expected_bits, "ABit2BBit", playerno)
            && ok;

    if (playerno == 0)
        cout << "CORRECTNESS " << (ok ? "PASS" : "FAIL") << endl;

    if (!ok)
        throw runtime_error("share conversion correctness failed");
}

template<class Func>
void benchmark(
        const string& name,
        RealTwoPartyPlayer* player,
        int playerno,
        int batch_size,
        int repeats,
        Func func)
{
    size_t sent_before = player->total_comm().sent;
    auto start = chrono::steady_clock::now();
    for (int i = 0; i < repeats; i++)
        func();
    auto end = chrono::steady_clock::now();

    chrono::duration<double> elapsed = end - start;
    double ms = elapsed.count() * 1000.0;
    double ns_per_element = elapsed.count() * 1e9 / double(batch_size) /
            double(repeats);
    size_t sent_after = player->total_comm().sent;

    if (playerno == 0)
    {
        cout << name << " time_ms=" << ms
             << " avg_ms=" << (ms / repeats)
             << " ns_per_element=" << ns_per_element
             << " sent_bytes=" << (sent_after - sent_before)
             << endl;
    }
}

void run_speed_tests(
        RealTwoPartyPlayer* player,
        int playerno,
        int batch_size,
        int repeats)
{
    auto values = make_values(batch_size);
    auto additive = make_additive_shares(values, playerno);
    auto xor_bits = make_xor_bit_shares(batch_size, playerno);
    auto expected_bits = make_bit_values(batch_size);
    auto arithmetic_bits = make_additive_shares(expected_bits, playerno);

    if (playerno == 0)
        cout << "BENCH batch_size=" << batch_size
             << " repeats=" << repeats << endl;

    benchmark("A2B_batch_l2", player, playerno, batch_size, repeats, [&] {
        volatile size_t sink =
                KonaShareConversion::A2B_batch_l2<K>(additive, player).size();
        (void)sink;
    });

    benchmark("B2A_batch_l2", player, playerno, batch_size, repeats, [&] {
        volatile size_t sink =
                KonaShareConversion::B2A_batch_l2<K>(xor_bits, player).size();
        (void)sink;
    });

    benchmark("ABit2BBit_batch_l2", player, playerno, batch_size, repeats, [&] {
        volatile size_t sink =
                KonaShareConversion::ABit2BBit_batch_l2<K>(
                        arithmetic_bits, player).size();
        (void)sink;
    });
}

} // namespace

int main(int argc, const char** argv)
{
    ez::ezOptionParser opt;
    int playerno = 0;
    int batch_size = 0;
    int repeats = 0;

    parse_options(argc, argv, opt, playerno, batch_size, repeats);
    RealTwoPartyPlayer* player = start_networking(opt, playerno, 2);

    try
    {
        run_correctness_tests(player, playerno);
        run_speed_tests(player, playerno, batch_size, repeats);
    }
    catch (...)
    {
        delete player;
        throw;
    }

    delete player;
    return 0;
}
