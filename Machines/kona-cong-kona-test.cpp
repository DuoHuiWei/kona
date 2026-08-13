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

Z2<K> additive_share_for(int playerno, size_t i, Z2<K> value)
{
    Z2<K> mask = ring(mix64(i + 0x778899aaULL));
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
        int& n,
        int& k)
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
    opt.add("64", 0, 1, 0, "Number of items",
            "-n", "--size");
    opt.add("5", 0, 1, 0, "Top-k",
            "-k", "--topk");

    opt.parse(argc, argv);
    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else if (argc > 1)
        sscanf(argv[1], "%d", &playerno);
    else
        throw runtime_error("missing player number");

    n = get_int_option(opt, "--size");
    k = get_int_option(opt, "--topk");
    if (n <= 0 || k <= 0 || k > n)
        throw runtime_error("invalid n/k");
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

template<class Runner>
void run_case(
        const string& name,
        Runner runner,
        RealTwoPartyPlayer* player,
        int playerno,
        int n,
        int k)
{
    using SharePair = KonaCongKonaAdapter::SharePair<K>;

    vector<SharePair> shares(n);
    vector<uint64_t> values(n);
    vector<uint64_t> labels(n);
    for (int i = 0; i < n; i++)
    {
        values[i] = mix64(1000 + i) & ((uint64_t(1) << 50) - 1);
        labels[i] = mix64(5000 + i) & ((uint64_t(1) << 32) - 1);
        shares[i][0] = additive_share_for(playerno, i, ring(values[i]));
        shares[i][1] = additive_share_for(playerno, i + 100000, ring(labels[i]));
    }

    auto start = chrono::steady_clock::now();
    runner(shares);
    auto end = chrono::steady_clock::now();

    vector<Z2<K>> tail_values_local(k), tail_labels_local(k);
    for (int i = 0; i < k; i++)
    {
        tail_values_local[i] = shares[n - k + i][0];
        tail_labels_local[i] = shares[n - k + i][1];
    }

    vector<Z2<K>> opened_values =
            KonaShareConversion::open_additive_batch_l2<K>(
                    tail_values_local, player);
    vector<Z2<K>> opened_labels =
            KonaShareConversion::open_additive_batch_l2<K>(
                    tail_labels_local, player);

    vector<pair<uint64_t, uint64_t>> plain(n);
    for (int i = 0; i < n; i++)
        plain[i] = {values[i], labels[i]};
    sort(plain.begin(), plain.end());

    bool ok = true;
    for (int i = 0; i < k; i++)
    {
        uint64_t got_value = static_cast<uint64_t>(opened_values[i].get_limb(0));
        uint64_t got_label = static_cast<uint64_t>(opened_labels[i].get_limb(0));
        if (got_value != plain[i].first || got_label != plain[i].second)
        {
            ok = false;
            if (playerno == 0)
                cerr << name << " mismatch at " << i << endl;
            break;
        }
    }

    if (playerno == 0)
    {
        chrono::duration<double> elapsed = end - start;
        cout << name << " " << (ok ? "PASS" : "FAIL")
             << " n=" << n
             << " k=" << k
             << " total_ms=" << elapsed.count() * 1000.0
             << endl;
    }
    if (!ok)
        throw runtime_error(name + " failed");
}

} // namespace

int main(int argc, const char** argv)
{
    ez::ezOptionParser opt;
    int playerno = 0;
    int n = 0;
    int k = 0;
    parse_options(argc, argv, opt, playerno, n, k);
    RealTwoPartyPlayer* player = start_networking(opt, playerno, 2);
    KonaDcfCompare::Compare64<K> dcf_compare(player, playerno);

    try
    {
        run_case(
                "CONG_PCR_TOPK",
                [&](std::vector<KonaCongKonaAdapter::SharePair<K>>& shares) {
                    KonaCongKonaAdapter::cong_top_k_with_pcr<K>(
                            shares, k, true, player);
                },
                player,
                playerno,
                n,
                k);

        run_case(
                "CONG_DCF_TOPK",
                [&](std::vector<KonaCongKonaAdapter::SharePair<K>>& shares) {
                    KonaCongKonaAdapter::cong_top_k_with_dcf<K>(
                            shares, k, true, dcf_compare, player);
                },
                player,
                playerno,
                n,
                k);
    }
    catch (...)
    {
        delete player;
        throw;
    }

    delete player;
    return 0;
}
