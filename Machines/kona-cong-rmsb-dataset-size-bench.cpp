#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "kona-cong-kona-adapter.hpp"
#include "kona-bench-affinity.hpp"
#include "kona-cong-topk.hpp"
#include "kona-rotated-msb-compare.hpp"
#include "kona-share-conversion.hpp"

namespace
{

const int K = 64;
const std::size_t RMSB_COMPARE_LOGICAL_INTERACTIONS = 188;
const std::size_t SWAP_LOGICAL_INTERACTIONS = 1;
using Share = Z2<K>;
using SharePair = std::array<Share, 2>;

struct Options
{
    int party = -1;
    std::string dataset = "arcene";
    std::string network_profile = "UNVERIFIED";
    int n = 0;
    int k = 5;
    int repeats = 3;
    std::size_t compare_batch_size =
            KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK;
    int port_base = 16000;
    std::string hostname = "localhost";
};

struct PhaseStats
{
    double milliseconds = 0;
    double comm_milliseconds = 0;
    std::size_t sent_bytes = 0;
    std::size_t transport_rounds = 0;
    std::size_t logical_interactions = 0;

    PhaseStats& operator+=(const PhaseStats& other)
    {
        milliseconds += other.milliseconds;
        comm_milliseconds += other.comm_milliseconds;
        sent_bytes += other.sent_bytes;
        transport_rounds += other.transport_rounds;
        logical_interactions += other.logical_interactions;
        return *this;
    }
};

struct TrialStats
{
    int trial = 0;
    double online_ms = 0;
    PhaseStats compare;
    PhaseStats swap;
    PhaseStats materialize;
    std::size_t sent_bytes = 0, transport_rounds = 0;
    std::size_t comparator_count = 0;
    std::size_t chunk_count = 0;
    bool communication_closed = false;
    bool correctness = false;
};

double elapsed_ms(const std::chrono::steady_clock::time_point& begin,
        const std::chrono::steady_clock::time_point& end)
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::size_t total_rounds(const NamedCommStats& stats)
{
    std::size_t result = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        result += it->second.rounds;
    return result;
}

double total_comm_ms(NamedCommStats stats)
{
    double seconds = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        seconds += it->second.timer.elapsed();
    return seconds * 1000;
}

int dataset_size(const std::string& name)
{
    if (name == "arcene") return 199;
    if (name == "tcga" || name == "tcga-pancan") return 800;
    if (name == "spambase") return 4600;
    if (name == "dota2" || name == "dota2-games-results") return 102943;
    throw std::runtime_error("unknown dataset name: " + name);
}

Options parse_options(int argc, const char** argv)
{
    ez::ezOptionParser parser;
    parser.add("", 0, 1, 0, "Party", "-p", "--player");
    parser.add("arcene", 0, 1, 0, "Dataset", "-d", "--dataset");
    parser.add("0", 0, 1, 0, "Override N", "-n", "--size");
    parser.add("5", 0, 1, 0, "Top-k", "-k", "--topk");
    parser.add("3", 0, 1, 0, "Repeats", "-r", "--repeats");
    parser.add("16384", 0, 1, 0, "Comparator pairs per chunk",
            "-b", "--compare-batch-size");
    parser.add("16000", 0, 1, 0, "Port base", "-pn", "--portnumbase");
    parser.add("localhost", 0, 1, 0, "Hostname", "-h", "--hostname");
    parser.add("UNVERIFIED", 0, 1, 0, "Network profile",
            "--network-profile");
    parser.parse(argc, argv);

    Options options;
    if (parser.isSet("--player"))
        parser.get("--player")->getInt(options.party);
    else if (argc > 1)
        options.party = std::stoi(argv[1]);
    parser.get("--dataset")->getString(options.dataset);
    parser.get("--size")->getInt(options.n);
    parser.get("--topk")->getInt(options.k);
    parser.get("--repeats")->getInt(options.repeats);
    long long batch = 0;
    parser.get("--compare-batch-size")->getLongLong(batch);
    parser.get("--portnumbase")->getInt(options.port_base);
    parser.get("--hostname")->getString(options.hostname);
    parser.get("--network-profile")->getString(options.network_profile);
    if (options.party != 0 && options.party != 1)
        throw std::runtime_error("party must be 0 or 1");
    if (options.n == 0)
        options.n = dataset_size(options.dataset);
    if (options.n <= 0 || options.k <= 0 || options.k > options.n ||
            options.repeats <= 0 || batch < 0)
        throw std::runtime_error("invalid benchmark options");
    options.compare_batch_size = batch == 0 ?
            KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK :
            std::min<std::size_t>(
                static_cast<std::size_t>(batch),
                KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK);
    return options;
}

RealTwoPartyPlayer* start_networking(const Options& options)
{
    Names names;
    Server::start_networking(names, options.party, 2, options.hostname,
            options.port_base, Names::DEFAULT_PORT);
    return new RealTwoPartyPlayer(names, 1 - options.party, 0);
}

uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

Share ring(uint64_t value)
{
    return Share(static_cast<mp_limb_t>(value));
}

Share additive_share(int party, uint64_t domain, Share value)
{
    const Share mask = ring(mix64(domain));
    return party == 0 ? mask : value - mask;
}

std::vector<SharePair> make_input(const Options& options,
        std::vector<std::pair<uint64_t, uint64_t> >& clear)
{
    std::vector<SharePair> shares(options.n);
    clear.resize(options.n);
    for (int i = 0; i < options.n; ++i)
    {
        // Deterministic fabricated bounded distance and payload. The distance
        // is prepared before the online timer; no NHSS work occurs here.
        const uint64_t distance =
                (mix64(1000 + i) & ((uint64_t(1) << 45) - 1)) *
                static_cast<uint64_t>(options.n) + static_cast<uint64_t>(i);
        const uint64_t payload = static_cast<uint64_t>(i);
        clear[i] = {distance, payload};
        shares[i][0] = additive_share(
                options.party, static_cast<uint64_t>(i), ring(distance));
        shares[i][1] = additive_share(
                options.party, 1000000ULL + static_cast<uint64_t>(i),
                ring(payload));
    }
    return shares;
}

PhaseStats measure_phase(const NamedCommStats& before,
        const NamedCommStats& after, double milliseconds,
        std::size_t logical_interactions)
{
    const NamedCommStats delta = after - before;
    PhaseStats result;
    result.milliseconds = milliseconds;
    result.comm_milliseconds = total_comm_ms(delta);
    result.sent_bytes = delta.sent;
    result.transport_rounds = total_rounds(delta);
    result.logical_interactions = logical_interactions;
    return result;
}

bool verify_top_k(const Options& options,
        const std::vector<SharePair>& shares,
        std::vector<std::pair<uint64_t, uint64_t> > clear,
        RealTwoPartyPlayer* player)
{
    std::vector<Share> distances(options.k), payloads(options.k);
    for (int i = 0; i < options.k; ++i)
    {
        distances[i] = shares[options.n - options.k + i][0];
        payloads[i] = shares[options.n - options.k + i][1];
    }
    const auto opened_distances =
            KonaShareConversion::open_additive_batch_l2<K>(distances, player);
    const auto opened_payloads =
            KonaShareConversion::open_additive_batch_l2<K>(payloads, player);
    std::sort(clear.begin(), clear.end());
    for (int i = 0; i < options.k; ++i)
        if (static_cast<uint64_t>(opened_distances[i].get_limb(0)) !=
                    clear[i].first ||
            static_cast<uint64_t>(opened_payloads[i].get_limb(0)) !=
                    clear[i].second)
            return false;
    return true;
}

void print_layer(const Options& options, int trial, std::size_t layer,
        std::size_t comparators, std::size_t chunks,
        const PhaseStats& compare, const PhaseStats& swap)
{
    std::cout << "CONG_RMSB_DATASET_LAYER"
              << " party=" << options.party
              << " dataset=" << options.dataset
              << " n=" << options.n << " k=" << options.k
              << " trial=" << trial << " layer=" << layer
              << " comparator_count=" << comparators
              << " chunk_count=" << chunks
              << " compare_ms=" << compare.milliseconds
              << " swap_ms=" << swap.milliseconds
              << " stage_total_ms="
              << compare.milliseconds + swap.milliseconds
              << " compare_comm_ms=" << compare.comm_milliseconds
              << " swap_comm_ms=" << swap.comm_milliseconds
              << " compare_sent_bytes=" << compare.sent_bytes
              << " swap_sent_bytes=" << swap.sent_bytes
              << " stage_sent_bytes="
              << compare.sent_bytes + swap.sent_bytes
              << " compare_transport_rounds=" << compare.transport_rounds
              << " swap_transport_rounds=" << swap.transport_rounds
              << " stage_transport_rounds="
              << compare.transport_rounds + swap.transport_rounds
              << " stage_transport_actions="
              << compare.transport_rounds + swap.transport_rounds
              << " compare_logical_interactions="
              << compare.logical_interactions
              << " swap_logical_interactions=" << swap.logical_interactions
              << " stage_logical_interactions="
              << compare.logical_interactions + swap.logical_interactions
              << std::endl;
}

struct PreparedChunk
{
    std::vector<int> indices;
    std::vector<Share> comparison;
    NamedCommStats compare_before, compare_after, swap_before, swap_after;
    std::chrono::steady_clock::time_point compare_begin, compare_end, swap_begin, swap_end;
};
using PreparedPlan = std::vector<std::vector<PreparedChunk>>;

PreparedPlan prepare_plan(const KonaCongTopK::CongNetwork& network, std::size_t batch)
{
    PreparedPlan plan(network.levels.size());
    for (std::size_t layer = 0; layer < plan.size(); ++layer)
    {
        const auto indices = KonaCongTopK::flatten_level(network.levels[layer]);
        for (std::size_t offset = 0; offset < indices.size() / 2; offset += batch)
        {
            const auto count = std::min(batch, indices.size() / 2 - offset);
            PreparedChunk chunk;
            chunk.indices.assign(indices.begin() + 2 * offset, indices.begin() + 2 * (offset + count));
            chunk.comparison.resize(chunk.indices.size());
            plan[layer].push_back(std::move(chunk));
        }
    }
    return plan;
}

void barrier(RealTwoPartyPlayer* player, int party)
{
    octetStream send, receive;
    ring(0).pack(send);
    if (party == 0) { player->send(send); player->receive(receive); }
    else { player->receive(receive); player->send(send); }
}

void print_phase(const Options& options, int trial, const char* name, const PhaseStats& phase)
{
    if (phase.transport_rounds % 2)
        throw std::runtime_error("non-paired RMSB communication");
    std::cout << "BENCH_PHASE phase=" << name << " party=" << options.party << " trial=" << trial
              << " online_ms=" << phase.milliseconds << " sent_bytes=" << phase.sent_bytes
              << " transport_rounds=" << phase.transport_rounds
              << " exchange_rounds=" << phase.transport_rounds / 2
              << " theoretical_rounds=" << phase.logical_interactions << '\n';
}

TrialStats run_trial(const Options& options,
        const KonaCongTopK::CongNetwork& network, PreparedPlan& plan,
        RealTwoPartyPlayer* player, int trial)
{
    std::vector<std::pair<uint64_t, uint64_t>> clear;
    auto shares = make_input(options, clear);
    TrialStats result;
    result.trial = trial;
    barrier(player, options.party); // Excluded from all online snapshots.
    const auto online_comm_before = player->total_comm();
    const auto online_begin = std::chrono::steady_clock::now();
    for (auto& layer : plan)
        for (auto& chunk : layer)
        {
            chunk.compare_before = player->total_comm();
            chunk.compare_begin = std::chrono::steady_clock::now();
            KonaRotatedMsbCompare::rotated_msb_compare_in_vec_l2(
                    shares, chunk.indices, chunk.comparison, true, player);
            chunk.compare_end = std::chrono::steady_clock::now();
            chunk.compare_after = player->total_comm();
            chunk.swap_before = player->total_comm();
            chunk.swap_begin = std::chrono::steady_clock::now();
            KonaCongKonaAdapter::ss_vec_kona_l2<K>(
                    shares, chunk.indices, chunk.comparison, player);
            chunk.swap_end = std::chrono::steady_clock::now();
            chunk.swap_after = player->total_comm();
        }
    const auto materialize_before = player->total_comm();
    const auto materialize_begin = std::chrono::steady_clock::now();
    KonaCongTopK::move_output_wires_to_tail(shares, network.output_wires);
    const auto online_end = std::chrono::steady_clock::now();
    const auto online_comm_after = player->total_comm();

    // No logging, phase subtraction/aggregation or checks above this boundary.
    const auto online_delta = online_comm_after - online_comm_before;
    result.online_ms = elapsed_ms(online_begin, online_end);
    result.sent_bytes = online_delta.sent;
    result.transport_rounds = total_rounds(online_delta);
    result.materialize = measure_phase(materialize_before, online_comm_after,
            elapsed_ms(materialize_begin, online_end), 0);
    bool chunk_accounting = true;
    for (std::size_t layer = 0; layer < plan.size(); ++layer)
    {
        PhaseStats compare, swap;
        std::size_t pairs = 0;
        for (const auto& chunk : plan[layer])
        {
            const auto c = measure_phase(chunk.compare_before, chunk.compare_after,
                    elapsed_ms(chunk.compare_begin, chunk.compare_end), RMSB_COMPARE_LOGICAL_INTERACTIONS);
            const auto w = measure_phase(chunk.swap_before, chunk.swap_after,
                    elapsed_ms(chunk.swap_begin, chunk.swap_end), SWAP_LOGICAL_INTERACTIONS);
            const auto swap_chunks = (chunk.indices.size() +
                    KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK - 1) /
                    KonaShareConversion::KONA_DEFAULT_BATCH_CHUNK;
            chunk_accounting = chunk_accounting &&
                    c.transport_rounds == 2 * RMSB_COMPARE_LOGICAL_INTERACTIONS &&
                    w.transport_rounds == 2 * swap_chunks;
            compare += c; swap += w;
            pairs += chunk.indices.size() / 2;
        }
        result.comparator_count += pairs;
        result.chunk_count += plan[layer].size();
        result.compare += compare; result.swap += swap;
        print_layer(options, trial, layer, pairs, plan[layer].size(), compare, swap);
    }
    result.communication_closed = chunk_accounting &&
            result.sent_bytes == result.compare.sent_bytes + result.swap.sent_bytes + result.materialize.sent_bytes &&
            result.transport_rounds == result.compare.transport_rounds + result.swap.transport_rounds + result.materialize.transport_rounds;
    result.correctness = verify_top_k(options, shares, clear, player); // Outside timer and byte delta.
    print_phase(options, trial, "compare", result.compare);
    print_phase(options, trial, "swap", result.swap);
    print_phase(options, trial, "output_materialize", result.materialize);
    PhaseStats other;
    other.milliseconds = result.online_ms - result.compare.milliseconds - result.swap.milliseconds - result.materialize.milliseconds;
    print_phase(options, trial, "online_other", other);
    PhaseStats total;
    total.milliseconds = result.online_ms;
    total.sent_bytes = result.sent_bytes; total.transport_rounds = result.transport_rounds;
    total.logical_interactions = result.compare.logical_interactions + result.swap.logical_interactions;
    print_phase(options, trial, "topk", total);
    return result;
}

double sample_std(const std::vector<double>& values, double mean)
{
    if (values.size() < 2)
        return 0;
    double sum = 0;
    for (double value : values)
        sum += (value - mean) * (value - mean);
    return std::sqrt(sum / (values.size() - 1));
}

double quantile(const std::vector<double>& sorted, double q)
{
    if (sorted.size() == 1)
        return sorted[0];
    const double position = q * static_cast<double>(sorted.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(position);
    const std::size_t upper = std::min(lower + 1, sorted.size() - 1);
    const double fraction = position - static_cast<double>(lower);
    return sorted[lower] * (1 - fraction) + sorted[upper] * fraction;
}

void print_trial(const Options& options,
        const KonaCongTopK::CongNetwork& network,
        const TrialStats& result)
{
    std::cout << "CONG_RMSB_DATASET_TRIAL"
              << " party=" << options.party
              << " dataset=" << options.dataset
              << " network_env=" << options.network_profile
              << " n=" << options.n << " k=" << options.k
              << " trial=" << result.trial
              << " online_ms=" << result.online_ms
              << " topk_ms=" << result.online_ms
              << " topk_online_ms=" << result.online_ms
              << " exchange_rounds=" << result.transport_rounds / 2
              << " scope=topk_only input_kind=synthetic_distances_real_N"
              << " features=NOT_APPLICABLE distance_status=NOT_IN_SCOPE vote_status=NOT_IN_SCOPE"
              << " knn_online_e2e_ms=NOT_IN_SCOPE output_scope=ordered_k_distance_payload_shares"
              << " metrics_version=reference-online-v1 backend=cong+rotated-msb material_mode=L2"
              << " accounting_check=" << (result.communication_closed ? "PASS" : "FAIL")
              << " test_status=" << (result.communication_closed && result.correctness ? "COMPLETED" : "FAILED")
              << " compare_ms=" << result.compare.milliseconds
              << " swap_ms=" << result.swap.milliseconds
              << " sent_bytes=" << result.sent_bytes
              << " transport_rounds=" << result.transport_rounds
              << " transport_actions=" << result.transport_rounds
              << " logical_interactions="
              << result.compare.logical_interactions +
                    result.swap.logical_interactions
              << " compare_sent_bytes=" << result.compare.sent_bytes
              << " swap_sent_bytes=" << result.swap.sent_bytes
              << " compare_transport_rounds="
              << result.compare.transport_rounds
              << " swap_transport_rounds=" << result.swap.transport_rounds
              << " compare_logical_interactions="
              << result.compare.logical_interactions
              << " swap_logical_interactions="
              << result.swap.logical_interactions
              << " comparator_count=" << result.comparator_count
              << " network_depth=" << network.levels.size()
              << " chunk_count=" << result.chunk_count
              << " compare_batch_size=" << options.compare_batch_size
              << " communication_closed="
              << (result.communication_closed ? 1 : 0)
              << " correctness=" << (result.correctness ? "PASS" : "FAIL")
              << std::endl;
}

} // namespace

int main(int argc, const char** argv)
{
    const Options options = parse_options(argc, argv);
    KonaBenchAffinity::bind_fixed_cpu(options.party);
    const auto network_begin = std::chrono::steady_clock::now();
    const KonaCongTopK::CongNetwork network =
            KonaCongTopK::build_cong_network(options.n, options.k);
    auto plan = prepare_plan(network, options.compare_batch_size);
    const auto network_end = std::chrono::steady_clock::now();
    std::unique_ptr<RealTwoPartyPlayer> connection(start_networking(options));
    auto* player = connection.get();
    std::cout << std::fixed << std::setprecision(9);

    std::vector<TrialStats> results;
    bool ok = true;
    for (int trial = 0; trial < options.repeats; ++trial)
    {
        TrialStats result = run_trial(options, network, plan, player, trial);
        print_trial(options, network, result);
        ok = ok && result.correctness && result.communication_closed;
        results.push_back(result);
    }

    std::vector<double> online;
    for (const auto& result : results)
        online.push_back(result.online_ms);
    std::sort(online.begin(), online.end());
    const double mean =
            std::accumulate(online.begin(), online.end(), 0.0) / online.size();
    const double median = online.size() % 2 ? online[online.size() / 2] :
            (online[online.size() / 2 - 1] + online[online.size() / 2]) / 2;
    const double stddev = sample_std(online, mean);
    std::cout << "CONG_RMSB_DATASET_SUMMARY"
              << " party=" << options.party
              << " dataset=" << options.dataset
              << " network_env=" << options.network_profile
              << " n=" << options.n << " k=" << options.k
              << " repeats=" << results.size()
              << " mean_online_ms=" << mean
              << " median_online_ms=" << median
              << " sample_std_ms=" << (online.size() > 1 ? std::to_string(stddev) : "NA")
              << " cv=" << (online.size() > 1 && mean > 0 ? std::to_string(stddev / mean) : "NA")
              << " aggregation=local_party_only formal_summary=paired_runner"
              << " min_online_ms=" << online.front()
              << " max_online_ms=" << online.back()
              << " p25_online_ms=" << quantile(online, 0.25)
              << " p75_online_ms=" << quantile(online, 0.75)
              << " comparator_count=" << results[0].comparator_count
              << " network_depth=" << network.levels.size()
              << " chunk_count=" << results[0].chunk_count
              << " sent_bytes="
              << results[0].compare.sent_bytes + results[0].swap.sent_bytes
              << " transport_rounds="
              << results[0].compare.transport_rounds +
                    results[0].swap.transport_rounds
              << " transport_actions="
              << results[0].compare.transport_rounds +
                    results[0].swap.transport_rounds
              << " logical_interactions="
              << results[0].compare.logical_interactions +
                    results[0].swap.logical_interactions
              << " network_build_ms="
              << elapsed_ms(network_begin, network_end)
              << " network_build_in_online_timer=0"
              << " input_share_setup_in_online_timer=0"
              << " correctness_open_in_online_timer=0"
              << " result=" << (ok ? "PASS" : "FAIL")
              << std::endl;
    return ok ? 0 : 1;
}
