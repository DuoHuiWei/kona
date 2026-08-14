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
#include "kona-cong-kona-adapter.hpp"
#include "kona-cong-topk.hpp"
#include "kona-dcf-compare.hpp"
#include "kona-matrix-topk.hpp"
#include "kona-pcr-compare.hpp"
#include "kona-share-conversion.hpp"

using namespace std;

namespace
{

const int K = 64;
int playerno = 0;
ez::ezOptionParser opt;
string mode = "legacy";

template<int KK>
using SharePair = std::array<Z2<KK>, 2>;

struct BenchResult
{
    double total_seconds = 0;
    double comm_seconds = 0;
    double compute_seconds = 0;
    size_t sent_bytes = 0;
    size_t transport_rounds = 0;
    size_t logical_rounds = 0;
    uint64_t checksum = 0;
    uint64_t topk_label_checksum = 0;
    uint64_t winner_label_share = 0;
    double dcf_key_init_seconds = 0;
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

void parse_argv(int argc, const char** argv, int& n, int& k)
{
    opt.add("5000", 0, 1, 0, "Port number base", "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on", "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host", "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file", "-ip", "--ip-file-name");
    opt.add("1024", 0, 1, 0, "Number of items", "-n", "--size");
    opt.add("5", 0, 1, 0, "Top-k", "-k", "--topk");
    opt.add("legacy", 0, 1, 0,
            "Benchmark mode: legacy, kona-dcf, cong, cong-dcf, matrix",
            "-m", "--mode");
    opt.parse(argc, argv);
    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else
        sscanf(argv[1], "%d", &playerno);
    opt.get("--size")->getInt(n);
    opt.get("--topk")->getInt(k);
    opt.get("--mode")->getString(mode);
    if (mode == "legacy-dcf")
        mode = "kona-dcf";
    if (mode != "legacy" && mode != "kona-dcf" &&
            mode != "cong" && mode != "cong-dcf" && mode != "matrix")
        throw runtime_error("unknown mode: " + mode);
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

void legacy_top1_with_pcr(
        vector<SharePair<K>>& shares,
        int size_now,
        bool min_in_last,
        RealTwoPartyPlayer* player)
{
    vector<int> compare_idx_vec;
    for (int i = 0; i < size_now; i++)
        compare_idx_vec.push_back(i);
    int leftover = -1;

    while (compare_idx_vec.size() + (leftover == -1 ? 0 : 1) > 1)
    {
        if (compare_idx_vec.size() % 2 == 1)
        {
            if (leftover == -1)
            {
                leftover = compare_idx_vec.back();
                compare_idx_vec.pop_back();
            }
            else
            {
                compare_idx_vec.push_back(leftover);
                leftover = -1;
            }
        }

        vector<Z2<K>> compare_res(compare_idx_vec.size());
        KonaPcrCompare::pcr_compare_in_vec_l2(
                shares, compare_idx_vec, compare_res, !min_in_last, player);
        KonaCongKonaAdapter::ss_vec_kona_l2<K>(
                shares, compare_idx_vec, compare_res, player);

        vector<int> new_compare_idx_vec;
        for (size_t i = 1; i < compare_idx_vec.size(); i += 2)
            new_compare_idx_vec.push_back(compare_idx_vec[i]);
        compare_idx_vec = std::move(new_compare_idx_vec);
    }
}

void legacy_top1_with_dcf(
        vector<SharePair<K>>& shares,
        int size_now,
        bool min_in_last,
        KonaDcfCompare::Compare64<K>& dcf_compare,
        RealTwoPartyPlayer* player)
{
    vector<int> compare_idx_vec;
    for (int i = 0; i < size_now; i++)
        compare_idx_vec.push_back(i);
    int leftover = -1;

    while (compare_idx_vec.size() + (leftover == -1 ? 0 : 1) > 1)
    {
        if (compare_idx_vec.size() % 2 == 1)
        {
            if (leftover == -1)
            {
                leftover = compare_idx_vec.back();
                compare_idx_vec.pop_back();
            }
            else
            {
                compare_idx_vec.push_back(leftover);
                leftover = -1;
            }
        }

        vector<Z2<K>> compare_res(compare_idx_vec.size());
        dcf_compare.compare_in_vec(
                shares, compare_idx_vec, compare_res, !min_in_last);
        KonaCongKonaAdapter::ss_vec_kona_l2<K>(
                shares, compare_idx_vec, compare_res, player);

        vector<int> new_compare_idx_vec;
        for (size_t i = 1; i < compare_idx_vec.size(); i += 2)
            new_compare_idx_vec.push_back(compare_idx_vec[i]);
        compare_idx_vec = std::move(new_compare_idx_vec);
    }
}

vector<Z2<K>> extract_tail_labels(
        const vector<SharePair<K>>& shares,
        int k)
{
    vector<Z2<K>> labels;
    labels.reserve(k);
    for (int i = 0; i < k; i++)
        labels.push_back(shares[shares.size() - 1 - i][1]);
    return labels;
}

void record_tail_topk_labels(
        const vector<SharePair<K>>& shares,
        int k,
        BenchResult& res)
{
    for (int i = 0; i < k; i++)
    {
        const SharePair<K>& item = shares[shares.size() - 1 - i];
        res.checksum += item[0].get_limb(0);
        res.topk_label_checksum += item[1].get_limb(0);
    }
}

void label_vote_with_pcr(
        const vector<Z2<K>>& topk_labels,
        vector<SharePair<K>>& label_count_array,
        RealTwoPartyPlayer* player)
{
    const int k = (int)topk_labels.size();
    label_count_array.resize(k);

    vector<int> compare_idx_vec;
    compare_idx_vec.reserve(2 * k * k);
    for (int i = 0; i < k; i++)
        for (int j = 0; j < k; j++)
        {
            compare_idx_vec.push_back(i);
            compare_idx_vec.push_back(j);
        }

    vector<Z2<K>> compare_res(compare_idx_vec.size());
    KonaPcrCompare::pcr_compare_in_vec_l2(
            topk_labels, compare_idx_vec, compare_res, true, player);

    vector<Z2<K>> v1(k * k), v2(k * k), products;
    for (int i = 0; i < k; i++)
        for (int j = 0; j < k; j++)
        {
            const int idx = i * k + j;
            const Z2<K> left_leq_right =
                    Z2<K>(playerno) - compare_res[2 * idx];
            const Z2<K> right_leq_left =
                    Z2<K>(playerno) - compare_res[2 * (j * k + i)];
            v1[idx] = left_leq_right;
            v2[idx] = right_leq_left;
        }

    KonaShareConversion::mul_vector_additive_kona_l2_chunked<K>(
            v1, v2, products, player);

    for (int i = 0; i < k; i++)
    {
        Z2<K> count(0);
        for (int j = 0; j < k; j++)
            count += products[i * k + j];
        label_count_array[i] = {count, topk_labels[i]};
    }
}

void finalize_vote_and_winner_with_pcr(
        vector<SharePair<K>>& shares,
        int k,
        RealTwoPartyPlayer* player,
        BenchResult& res)
{
    vector<Z2<K>> topk_labels = extract_tail_labels(shares, k);
    vector<SharePair<K>> label_count_array;
    label_vote_with_pcr(topk_labels, label_count_array, player);
    legacy_top1_with_pcr(label_count_array, k, false, player);

    for (int i = 0; i < k; i++)
        res.checksum += shares[shares.size() - 1 - i][0].get_limb(0);
    res.winner_label_share =
            label_count_array[label_count_array.size() - 1][1].get_limb(0);
}

BenchResult benchmark_legacy_network(
        const vector<SharePair<K>>& input,
        int k,
        RealTwoPartyPlayer* player)
{
    auto shares = input;
    auto comm_before = player->total_comm();
    auto start = chrono::steady_clock::now();
    BenchResult res;
    for (int i = 0; i < k; i++)
        legacy_top1_with_pcr(shares, (int)shares.size() - i, true, player);
    record_tail_topk_labels(shares, k, res);
    auto end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto delta = comm_after - comm_before;

    res.total_seconds = chrono::duration<double>(end - start).count();
    res.comm_seconds = total_comm_seconds(delta);
    res.compute_seconds = res.total_seconds - res.comm_seconds;
    res.sent_bytes = delta.sent;
    res.transport_rounds = total_rounds(delta);
    res.logical_rounds = res.transport_rounds / 2;
    return res;
}

BenchResult benchmark_legacy_network_dcf(
        const vector<SharePair<K>>& input,
        int k,
        RealTwoPartyPlayer* player,
        KonaDcfCompare::Compare64<K>& dcf_compare)
{
    auto shares = input;
    auto comm_before = player->total_comm();
    auto start = chrono::steady_clock::now();
    BenchResult res;
    for (int i = 0; i < k; i++)
        legacy_top1_with_dcf(
                shares, (int)shares.size() - i, true, dcf_compare, player);
    record_tail_topk_labels(shares, k, res);
    auto end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto delta = comm_after - comm_before;

    res.total_seconds = chrono::duration<double>(end - start).count();
    res.comm_seconds = total_comm_seconds(delta);
    res.compute_seconds = res.total_seconds - res.comm_seconds;
    res.sent_bytes = delta.sent;
    res.transport_rounds = total_rounds(delta);
    res.logical_rounds = res.transport_rounds / 2;
    return res;
}

BenchResult benchmark_cong_network(
        const vector<SharePair<K>>& input,
        int k,
        RealTwoPartyPlayer* player)
{
    auto shares = input;
    auto comm_before = player->total_comm();
    auto start = chrono::steady_clock::now();
    BenchResult res;
    KonaCongKonaAdapter::cong_top_k_with_pcr<K>(shares, k, true, player);
    record_tail_topk_labels(shares, k, res);
    auto end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto delta = comm_after - comm_before;

    res.total_seconds = chrono::duration<double>(end - start).count();
    res.comm_seconds = total_comm_seconds(delta);
    res.compute_seconds = res.total_seconds - res.comm_seconds;
    res.sent_bytes = delta.sent;
    res.transport_rounds = total_rounds(delta);
    res.logical_rounds = res.transport_rounds / 2;
    return res;
}

BenchResult benchmark_cong_network_dcf(
        const vector<SharePair<K>>& input,
        int k,
        RealTwoPartyPlayer* player,
        KonaDcfCompare::Compare64<K>& dcf_compare)
{
    auto shares = input;
    auto comm_before = player->total_comm();
    auto start = chrono::steady_clock::now();
    BenchResult res;
    KonaCongKonaAdapter::cong_top_k_with_dcf<K>(
            shares, k, true, dcf_compare, player);
    record_tail_topk_labels(shares, k, res);
    auto end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto delta = comm_after - comm_before;

    res.total_seconds = chrono::duration<double>(end - start).count();
    res.comm_seconds = total_comm_seconds(delta);
    res.compute_seconds = res.total_seconds - res.comm_seconds;
    res.sent_bytes = delta.sent;
    res.transport_rounds = total_rounds(delta);
    res.logical_rounds = res.transport_rounds / 2;
    return res;
}

BenchResult benchmark_matrix_network(
        const vector<SharePair<K>>& input,
        int k,
        RealTwoPartyPlayer* player)
{
    vector<Z2<K>> rank_share;
    vector<Z2<K>> topk_mask_share;
    vector<SharePair<K>> label_count_array;

    auto comm_before = player->total_comm();
    auto start = chrono::steady_clock::now();
    KonaMatrixTopK::matrix_topk_vote_with_pcr<K>(
            input, k, 16, rank_share, topk_mask_share, label_count_array, player);
    auto end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto delta = comm_after - comm_before;

    BenchResult res;
    res.total_seconds = chrono::duration<double>(end - start).count();
    res.comm_seconds = total_comm_seconds(delta);
    res.compute_seconds = res.total_seconds - res.comm_seconds;
    res.sent_bytes = delta.sent;
    res.transport_rounds = total_rounds(delta);
    res.logical_rounds = res.transport_rounds / 2;
    for (size_t i = 0; i < rank_share.size(); i++)
        res.checksum += topk_mask_share[i].get_limb(0);
    return res;
}

void print_result(
        const string& name,
        int n,
        int k,
        const BenchResult& r)
{
    cout << name
         << " n=" << n
         << " k=" << k
         << " total_ms=" << r.total_seconds * 1000.0
         << " compute_ms=" << r.compute_seconds * 1000.0
         << " comm_ms=" << r.comm_seconds * 1000.0
         << " sent_bytes=" << r.sent_bytes
         << " transport_rounds=" << r.transport_rounds
         << " logical_rounds=" << r.logical_rounds
         << " dcf_key_init_ms=" << r.dcf_key_init_seconds * 1000.0
         << " output_scope=topk_labels_only"
         << " checksum=" << r.checksum
         << " topk_label_checksum=" << r.topk_label_checksum
         << " winner_label_share=" << r.winner_label_share
         << endl;
}

} // namespace

int main(int argc, const char** argv)
{
    int n = 0;
    int k = 0;
    parse_argv(argc, argv, n, k);
    RealTwoPartyPlayer* player = start_networking();

    try
    {
        auto shares = build_shares(n);
        if (playerno == 0)
            cout << "mode=" << mode << endl;

        if (mode == "legacy")
        {
            auto legacy = benchmark_legacy_network(shares, k, player);
            if (playerno == 0)
                print_result("LEGACY_PCR_TOPK", n, k, legacy);
        }
        else if (mode == "kona-dcf")
        {
            auto dcf_key_init_start = chrono::steady_clock::now();
            KonaDcfCompare::Compare64<K> dcf_compare(player, playerno);
            auto dcf_key_init_end = chrono::steady_clock::now();
            auto kona_dcf = benchmark_legacy_network_dcf(
                    shares, k, player, dcf_compare);
            kona_dcf.dcf_key_init_seconds =
                    chrono::duration<double>(
                            dcf_key_init_end - dcf_key_init_start).count();
            if (playerno == 0)
                print_result("KONA_DCF_TOPK_LABELS", n, k, kona_dcf);
        }
        else if (mode == "cong")
        {
            auto cong = benchmark_cong_network(shares, k, player);
            if (playerno == 0)
                print_result("CONG_PCR_TOPK", n, k, cong);
        }
        else if (mode == "cong-dcf")
        {
            auto dcf_key_init_start = chrono::steady_clock::now();
            KonaDcfCompare::Compare64<K> dcf_compare(player, playerno);
            auto dcf_key_init_end = chrono::steady_clock::now();
            auto cong_dcf = benchmark_cong_network_dcf(
                    shares, k, player, dcf_compare);
            cong_dcf.dcf_key_init_seconds =
                    chrono::duration<double>(
                            dcf_key_init_end - dcf_key_init_start).count();
            if (playerno == 0)
                print_result("CONG_DCF_TOPK", n, k, cong_dcf);
        }
        else
        {
            auto matrix = benchmark_matrix_network(shares, k, player);
            if (playerno == 0)
                print_result("MATRIX_PCR_TOPK", n, k, matrix);
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
