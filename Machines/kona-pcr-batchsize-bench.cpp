#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "../Math/Z2k.hpp"
#include "kona-pcr-compare.hpp"

using namespace std;

namespace
{

const int K = 64;
const int REPEATS = 3;
int playerno = 0;
ez::ezOptionParser opt;
int total_compare = 16348;
int only_batch_size = 0;

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
    Z2<K> mask = ring(mix64(i + 0x13572468ULL));
    return playerno == 0 ? mask : value - mask;
}

void parse_argv(int argc, const char** argv)
{
    opt.add("5000", 0, 1, 0, "Port number base", "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on", "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host", "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file", "-ip", "--ip-file-name");
    opt.add("16348", 0, 1, 0, "Total number of comparisons",
            "-n", "--num-compare");
    opt.add("0", 0, 1, 0, "Run only this batch size (0 means run all defaults)",
            "-b", "--batch-size");
    opt.parse(argc, argv);
    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else
        sscanf(argv[1], "%d", &playerno);
    opt.get("--num-compare")->getInt(total_compare);
    opt.get("--batch-size")->getInt(only_batch_size);
    if (total_compare <= 0)
        throw runtime_error("num-compare must be positive");
    if (only_batch_size < 0)
        throw runtime_error("batch-size must be non-negative");
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

struct RepeatResult
{
    vector<double> chunk_ms;
    double total_ms = 0;
    double compute_ms = 0;
    double comm_ms = 0;
    size_t sent_bytes = 0;
    size_t transport_rounds = 0;
    size_t logical_rounds = 0;
    uint64_t checksum = 0;
};

void build_inputs(vector<Z2<K>>& x_shares, vector<Z2<K>>& y_shares)
{
    x_shares.resize(total_compare);
    y_shares.resize(total_compare);
    for (int i = 0; i < total_compare; i++)
    {
        uint64_t x = mix64(uint64_t(i) + 1000) & ((uint64_t(1) << 20) - 1);
        uint64_t y = mix64(uint64_t(i) + 5000) & ((uint64_t(1) << 20) - 1);
        if (i % 97 == 0)
            y = x;
        x_shares[i] = additive_share_for(playerno, i, ring(x));
        y_shares[i] = additive_share_for(playerno, i + 100000, ring(y));
    }
}

RepeatResult run_one_repeat(
        const vector<Z2<K>>& x_shares,
        const vector<Z2<K>>& y_shares,
        int batch_size,
        RealTwoPartyPlayer* player)
{
    RepeatResult out;
    auto comm_before = player->total_comm();
    auto total_start = chrono::steady_clock::now();

    vector<Z2<K>> all_bits;
    all_bits.reserve(total_compare);

    for (int offset = 0; offset < total_compare; offset += batch_size)
    {
        int len = min(batch_size, total_compare - offset);
        vector<Z2<K>> x_chunk(len), y_chunk(len);
        for (int i = 0; i < len; i++)
        {
            x_chunk[i] = x_shares[offset + i];
            y_chunk[i] = y_shares[offset + i];
        }

        auto chunk_start = chrono::steady_clock::now();
        vector<Z2<K>> bits =
                KonaPcrCompare::pcr_compare_gt_batch_l2(x_chunk, y_chunk, player);
        auto chunk_end = chrono::steady_clock::now();

        out.chunk_ms.push_back(
                chrono::duration<double>(chunk_end - chunk_start).count() * 1000.0);
        all_bits.insert(all_bits.end(), bits.begin(), bits.end());
    }

    auto total_end = chrono::steady_clock::now();
    auto comm_after = player->total_comm();
    auto comm_delta = comm_after - comm_before;

    out.total_ms = chrono::duration<double>(total_end - total_start).count() * 1000.0;
    out.comm_ms = total_comm_seconds(comm_delta) * 1000.0;
    out.compute_ms = out.total_ms - out.comm_ms;
    out.sent_bytes = comm_delta.sent;
    out.transport_rounds = total_rounds(comm_delta);
    out.logical_rounds = out.transport_rounds / 2;
    for (size_t i = 0; i < all_bits.size(); i++)
        out.checksum += all_bits[i].get_limb(0);
    return out;
}

void print_repeat(int batch_size, int repeat_idx, const RepeatResult& r)
{
    cout << "num_compare=" << total_compare
         << " batch_size=" << batch_size
         << " repeat=" << repeat_idx
         << " chunk_ms=[";
    for (size_t i = 0; i < r.chunk_ms.size(); i++)
    {
        if (i)
            cout << ",";
        cout << r.chunk_ms[i];
    }
    cout << "] total_ms=" << r.total_ms
         << " compute_ms=" << r.compute_ms
         << " comm_ms=" << r.comm_ms
         << " sent_bytes=" << r.sent_bytes
         << " transport_rounds=" << r.transport_rounds
         << " logical_rounds=" << r.logical_rounds
         << " checksum=" << r.checksum
         << endl
         << flush;
}

void print_average(int batch_size, const vector<RepeatResult>& results)
{
    double avg_total = 0;
    double avg_compute = 0;
    double avg_comm = 0;
    double avg_sent = 0;
    double avg_transport = 0;
    double avg_logical = 0;
    for (const auto& r : results)
    {
        avg_total += r.total_ms;
        avg_compute += r.compute_ms;
        avg_comm += r.comm_ms;
        avg_sent += r.sent_bytes;
        avg_transport += r.transport_rounds;
        avg_logical += r.logical_rounds;
    }
    avg_total /= results.size();
    avg_compute /= results.size();
    avg_comm /= results.size();
    avg_sent /= results.size();
    avg_transport /= results.size();
    avg_logical /= results.size();

    size_t max_chunks = 0;
    for (const auto& r : results)
        max_chunks = max(max_chunks, r.chunk_ms.size());
    vector<double> avg_chunk(max_chunks, 0);
    for (const auto& r : results)
        for (size_t i = 0; i < r.chunk_ms.size(); i++)
            avg_chunk[i] += r.chunk_ms[i];
    for (size_t i = 0; i < avg_chunk.size(); i++)
        avg_chunk[i] /= results.size();

    cout << "num_compare=" << total_compare
         << " batch_size=" << batch_size << " average_over=" << results.size()
         << " avg_chunk_ms=[";
    for (size_t i = 0; i < avg_chunk.size(); i++)
    {
        if (i)
            cout << ",";
        cout << avg_chunk[i];
    }
    cout << "] avg_total_ms=" << avg_total
         << " avg_compute_ms=" << avg_compute
         << " avg_comm_ms=" << avg_comm
         << " avg_sent_bytes=" << avg_sent
         << " avg_transport_rounds=" << avg_transport
         << " avg_logical_rounds=" << avg_logical
         << endl
         << flush;
}

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    RealTwoPartyPlayer* player = start_networking();

    try
    {
        vector<Z2<K>> x_shares;
        vector<Z2<K>> y_shares;
        build_inputs(x_shares, y_shares);

        std::vector<int> batch_sizes = {1024, 4096, 8193, 16348, 32696};
        for (int batch_size : batch_sizes)
        {
            if (only_batch_size > 0 && batch_size != only_batch_size)
                continue;
            if (batch_size > total_compare)
                continue;
            vector<RepeatResult> results;
            for (int r = 1; r <= REPEATS; r++)
            {
                auto result = run_one_repeat(x_shares, y_shares, batch_size, player);
                results.push_back(result);
                if (playerno == 0)
                    print_repeat(batch_size, r, result);
            }
            if (playerno == 0)
                print_average(batch_size, results);
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
