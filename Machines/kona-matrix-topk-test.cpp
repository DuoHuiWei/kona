#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "../Math/Z2k.hpp"
#include "kona-matrix-topk.hpp"
#include "kona-share-conversion.hpp"

using namespace std;

namespace
{

const int K = 64;
int playerno = 0;
ez::ezOptionParser opt;

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
    Z2<K> mask = ring(mix64(i + 0x99aabbccULL));
    return playerno == 0 ? mask : value - mask;
}

void parse_argv(int argc, const char** argv)
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
    opt.parse(argc, argv);
    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else
        sscanf(argv[1], "%d", &playerno);
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

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    RealTwoPartyPlayer* player = start_networking();

    const int n = 32;
    const int k = 5;
    const int num_label = 4;

    vector<KonaMatrixTopK::SharePair<K>> shares(n);
    vector<pair<uint64_t, int>> plain(n);
    for (int i = 0; i < n; i++)
    {
        uint64_t value = mix64(i + 1000) & ((uint64_t(1) << 20) - 1);
        if (i % 7 == 0 && i > 0)
            value = plain[i - 1].first;
        int label = int(mix64(i + 5000) % num_label);
        plain[i] = {value, label};
        shares[i][0] = additive_share_for(playerno, i, ring(value));
        shares[i][1] = additive_share_for(playerno, i + 100000, ring(label));
    }

    vector<Z2<K>> rank_share;
    vector<Z2<K>> topk_mask_share;
    vector<KonaMatrixTopK::SharePair<K>> label_count_array;
    KonaMatrixTopK::matrix_topk_vote_with_pcr<K>(
            shares, k, num_label, rank_share, topk_mask_share,
            label_count_array, player);

    vector<Z2<K>> opened_ranks =
            KonaShareConversion::open_additive_batch_l2<K>(rank_share, player);
    vector<Z2<K>> opened_mask =
            KonaShareConversion::open_additive_batch_l2<K>(topk_mask_share, player);

    vector<Z2<K>> count_local(num_label);
    vector<Z2<K>> label_local(num_label);
    for (int i = 0; i < num_label; i++)
    {
        count_local[i] = label_count_array[i][0];
        label_local[i] = label_count_array[i][1];
    }
    vector<Z2<K>> opened_counts =
            KonaShareConversion::open_additive_batch_l2<K>(count_local, player);
    vector<Z2<K>> opened_labels =
            KonaShareConversion::open_additive_batch_l2<K>(label_local, player);

    vector<uint64_t> expected_rank(n);
    vector<uint64_t> expected_mask(n, 0);
    map<int, int> expected_counts;
    vector<int> order(n);
    for (int i = 0; i < n; i++)
        order[i] = i;
    stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return plain[a].first < plain[b].first;
    });
    for (int rank = 0; rank < n; rank++)
    {
        expected_rank[order[rank]] = rank;
        if (rank < k)
        {
            expected_mask[order[rank]] = 1;
            expected_counts[plain[order[rank]].second]++;
        }
    }

    bool ok = true;
    for (int i = 0; i < n; i++)
    {
        uint64_t got_rank = opened_ranks[i].get_limb(0);
        uint64_t got_mask = opened_mask[i].get_limb(0);
        if (got_rank != expected_rank[i] || got_mask != expected_mask[i])
        {
            ok = false;
            if (playerno == 0)
                cerr << "rank/mask mismatch at " << i << endl;
            break;
        }
    }
    for (int label = 0; label < num_label && ok; label++)
    {
        uint64_t got_label = opened_labels[label].get_limb(0);
        uint64_t got_count = opened_counts[label].get_limb(0);
        uint64_t expected = expected_counts[label];
        if (got_label != (uint64_t)label || got_count != expected)
        {
            ok = false;
            if (playerno == 0)
                cerr << "count mismatch for label " << label << endl;
        }
    }

    if (playerno == 0)
        cout << "MATRIX_TOPK_PCR " << (ok ? "PASS" : "FAIL") << endl;

    delete player;
    return ok ? 0 : 1;
}
