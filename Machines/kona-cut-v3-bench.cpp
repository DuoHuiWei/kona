#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include "Machines/kona-bench-affinity.hpp"
#include "Machines/kona-cut-v3.hpp"
#include "Networking/Server.h"

namespace
{
using namespace KonaCutV3;
using Clock = std::chrono::steady_clock;
using Ring = Z2<64>;
using Pair = SharePair<64>;

struct Metric
{
    double ms = 0;
    uint64_t bytes = 0, actions = 0, rounds = 0, evaluations = 0, items = 0;

    Metric& operator+=(const Metric& rhs)
    {
        ms += rhs.ms;
        bytes += rhs.bytes;
        actions += rhs.actions;
        rounds += rhs.rounds;
        evaluations += rhs.evaluations;
        items += rhs.items;
        return *this;
    }
};

struct Batch
{
    string phase, backend;
    size_t layer, width;
    Metric metric;
};

void print_metric(const Metric& metric)
{
    std::cout << " online_ms=" << metric.ms
              << " sent_bytes=" << metric.bytes
              << " theoretical_rounds=" << metric.rounds
              << " exchange_rounds=" << metric.actions / 2
              << " transport_actions=" << metric.actions
              << " dcf_evaluate_calls=" << metric.evaluations
              << " items=" << metric.items
              << " ms_per_item=" << (metric.items ? metric.ms / metric.items : 0);
}

int run(int argc, char** argv)
{
    const auto setup_start = Clock::now();
    const std::set<string> keys = {"--n", "--k", "--blocks", "--block-sizes",
            "--profile", "--chunking", "-p", "-pn", "-h"};
    std::map<string, string> args;
    bool describe = false;
    for (int i = 1; i < argc; ++i)
    {
        string key = argv[i];
        if (key == "--describe" && !describe)
        {
            describe = true;
            continue;
        }
        if (!keys.count(key) || i + 1 == argc || args.count(key))
            throw std::invalid_argument("unknown, duplicate or incomplete argument: " + key);
        args.emplace(key, argv[++i]);
    }
    if (args.count("--blocks") + args.count("--block-sizes") != 1)
        throw std::invalid_argument("specify exactly one of --blocks and --block-sizes");
    const int n = positive(args.at("--n")), k = positive(args.at("--k"));
    const auto layout = args.count("--blocks") ?
            blocks_layout(n, positive(args.at("--blocks"))) : explicit_layout(args.at("--block-sizes"));
    const string profile = args.count("--profile") ? args.at("--profile") : "wan";
    const string chunking = args.count("--chunking") ? args.at("--chunking") : "off";
    if (chunking != "off" && chunking != "on")
        throw std::invalid_argument("--chunking must be on or off");
    const string party_text = args.count("-p") ? args.at("-p") : "0";
    if (party_text != "0" && party_text != "1")
        throw std::invalid_argument("party must be 0 or 1");
    const int party = party_text == "0" ? 0 : 1;
    const int port = args.count("-pn") ? positive(args.at("-pn")) : 37000;
    if (port < 1024 || port > 64000)
        throw std::invalid_argument("port must be between 1024 and 64000");

    const Policy policy;
    const size_t threshold = policy.threshold_for(profile);
    KonaBenchAffinity::bind_fixed_cpu(describe ? 0 : party);
    KonaBenchConfig::ScopedChunking chunks(chunking == "on");
    const auto plan = prepare(n, k, layout);
    vector<int> keeps;
    for (const auto& block : plan.blocks.plan.blocks)
        keeps.push_back(block.keep);
    std::cout << std::fixed << std::setprecision(9)
              << "n=" << n << "\nk=" << k << "\nparty=" << party
              << "\nprofile=" << profile << "\nthreshold=" << threshold
              << "\nchunking=" << chunking << "\ndcf_chunk=" << KonaBenchConfig::dcf_chunk()
              << "\nscs_mul_chunk=" << KonaBenchConfig::mul_chunk()
              << "\nlayout=" << layout_string(layout) << "\nblock_keeps=" << layout_string(keeps)
              << "\ncandidate_count=" << plan.terminal.input_count
              << "\nblock_comparators=" << plan.blocks.plan.block_comparators
              << "\nmatrix_comparators=" << plan.terminal.matrix_comparators
              << "\nrank_mask_comparators=" << plan.terminal.mask_comparators
              << "\noutput_scope=" << (plan.terminal.direct ? "ordered_topk" : "candidate_mask")
              << "\nfixture=cut-v3-permuted-unique-distance-v1\n";
    for (size_t i = 0; i < layout.size(); ++i)
    {
        const auto& block = plan.blocks.plan.blocks[i];
        std::cout << "CUT_BLOCK block=" << i << " begin=" << block.real_begin
                  << " length=" << block.real_size << " candidates=" << block.keep << '\n';
    }
    for (size_t depth = 0; depth < plan.blocks.fused_block_levels.size(); ++depth)
        std::cout << "CUT_PLAN layer=" << depth << " width="
                  << plan.blocks.fused_block_levels[depth].size() / 2 << '\n';
    if (describe)
        return 0;

    // Public synthetic fixture with unique distances and row IDs as labels.
    // The existing DCF material and PCR/SCS benchmark paths are used unchanged.
    vector<int> distances(n);
    std::iota(distances.begin(), distances.end(), 0);
    std::mt19937_64 fixture_rng(0x6375747633ULL);
    std::shuffle(distances.begin(), distances.end(), fixture_rng);
    vector<Pair> work(n), reference(k);
    uint64_t fixture_checksum = 1469598103934665603ULL;
    auto share = [&](uint64_t value) {
        Ring mask(static_cast<mp_limb_t>(fixture_rng()));
        return party == 0 ? mask : Ring(static_cast<mp_limb_t>(value)) - mask;
    };
    for (int i = 0; i < n; ++i)
    {
        work[i] = {share(distances[i]), share(i)};
        if (distances[i] < k)
            reference[distances[i]] = {Ring(distances[i]), Ring(i)};
        fixture_checksum = (fixture_checksum ^ uint64_t(distances[i])) * 1099511628211ULL;
    }

    Names names;
    Server::start_networking(names, party, 2, args.count("-h") ? args.at("-h") : "localhost",
            port, Names::DEFAULT_PORT);
    RealTwoPartyPlayer player(names, 1 - party, 0);
    KonaDcfCompare::Compare64<64> dcf(&player, party);
    std::cout << "fixture_checksum=" << fixture_checksum << "\nsetup_ms="
              << std::chrono::duration<double, std::milli>(Clock::now() - setup_start).count()
              << '\n';
    auto measure = [&](auto&& operation) {
        const auto before = player.total_comm();
        const auto evaluations = dcf.get_stats().evaluate_calls;
        const auto start = Clock::now();
        operation();
        const auto end = Clock::now();
        const auto comm = player.total_comm() - before;
        Metric metric;
        metric.ms = std::chrono::duration<double, std::milli>(end - start).count();
        metric.bytes = comm.sent;
        for (const auto& entry : comm)
            metric.actions += entry.second.rounds;
        metric.evaluations = dcf.get_stats().evaluate_calls - evaluations;
        return metric;
    };

    constexpr const char* phases[] = {
        "block_compare", "scs", "matrix_compare", "rank_mask", "local"
    };
    enum { Block, Swap, Matrix, Mask, Local };
    std::array<Metric, 5> totals;
    vector<Batch> batches;
    batches.reserve(2 * plan.blocks.fused_block_levels.size() + 2);
    auto record = [&](size_t phase, size_t layer, size_t width, Backend backend, Metric metric) {
        metric.rounds = phase == Swap ? 1 : backend == Backend::PCR ? 8 : 1;
        metric.items = width;
        totals[phase] += metric;
        batches.push_back({phases[phase], phase == Swap ? "SCS" :
                KonaReducedDepthHybrid::backend_name(backend), layer, width, metric});
    };

    size_t block_layer = 0;
    auto block_observer = [&](const char*, size_t layer, size_t, auto operation) {
        block_layer = layer;
        operation();
    };
    auto split_layer = [&](auto& shares, const auto& indices, const auto&, const auto&,
            auto&, auto*) {
        vector<Ring> bits(indices.size());
        Backend backend = Backend::AUTO;
        const auto compare = measure([&] {
            backend = KonaReducedDepthHybrid::compare_pairs<64>(
                    shares, indices, bits, true, profile, policy, dcf, &player);
        });
        const auto swap = measure([&] {
            KonaCongKonaAdapter::ss_vec_kona_l2<64>(shares, indices, bits, &player);
        });
        record(Block, block_layer, indices.size() / 2, backend, compare);
        record(Swap, block_layer, indices.size() / 2, backend, swap);
        return backend;
    };

    size_t terminal_width = 0, terminal_phase = Matrix;
    auto terminal_observer = [&](const char* phase, size_t, size_t width, auto operation) {
        terminal_phase = std::string_view(phase) == "rank_mask" ? Mask : Matrix;
        terminal_width = width;
        operation();
    };
    auto secure_compare = [&](auto operation) {
        Backend backend = Backend::AUTO;
        const auto metric = measure([&] { backend = operation(); });
        record(terminal_phase, 0, terminal_width, backend, metric);
        return backend;
    };

    Output<64> output;
    octetStream ready, peer;
    Ring(0).pack(ready);
    player.exchange_concurrent(ready, peer);
    auto online = measure([&] {
        execute_blocks<64>(work, plan.blocks, profile, policy, dcf, &player,
                block_observer, split_layer);
        auto candidates = collect_block_candidates<64>(
                work, plan.blocks.plan, plan.blocks.block_offsets);
        output = execute_terminal<64>(std::move(candidates), plan.terminal, k, profile,
                policy, dcf, &player, terminal_observer, secure_compare);
    });

    Metric protocol_sum;
    bool accounting = true;
    for (const auto& batch : batches)
    {
        const auto& metric = batch.metric;
        const uint64_t width = batch.width;
        const bool swap = batch.backend == "SCS", dcf_batch = batch.backend == "DCF";
        const auto exchanges = swap ? KonaBenchConfig::chunks(2 * width, KonaBenchConfig::mul_chunk()) :
                dcf_batch ? KonaBenchConfig::chunks(width, KonaBenchConfig::dcf_chunk()) : 8;
        accounting &= metric.bytes == width * (swap ? 64 : dcf_batch ? 8 : 70)
                && metric.actions == 2 * exchanges && metric.evaluations == (dcf_batch ? 2 * width : 0);
        protocol_sum += metric;
    }
    totals[Local].ms = online.ms - protocol_sum.ms;
    online.rounds = protocol_sum.rounds;
    online.items = n;
    accounting &= totals[Local].ms >= 0 && online.bytes == protocol_sum.bytes &&
            online.actions == protocol_sum.actions && online.evaluations == protocol_sum.evaluations &&
            totals[Block].items == plan.blocks.plan.block_comparators &&
            totals[Swap].items == totals[Block].items &&
            totals[Matrix].items == plan.terminal.matrix_comparators &&
            totals[Mask].items == plan.terminal.mask_comparators;

    // Reveal synthetic outputs only after timing. Also check ordered block survivors.
    octetStream check_out, check_in;
    for (size_t i = 0; i < output.candidates.size(); ++i)
    {
        output.candidates[i][0].pack(check_out);
        output.candidates[i][1].pack(check_out);
        (output.mask.empty() ? Ring(party) : output.mask[i]).pack(check_out);
    }
    player.exchange_concurrent(check_out, check_in);
    vector<Pair> selected;
    bool valid = true;
    size_t block = 0, position = 0;
    uint64_t previous = 0;
    for (size_t i = 0; i < output.candidates.size(); ++i)
    {
        Ring distance, label, bit;
        distance.unpack(check_in);
        label.unpack(check_in);
        bit.unpack(check_in);
        distance += output.candidates[i][0];
        label += output.candidates[i][1];
        bit += output.mask.empty() ? Ring(party) : output.mask[i];
        valid &= bit.get_limb(0) <= 1;
        valid &= !position || previous < distance.get_limb(0);
        previous = distance.get_limb(0);
        if (++position == size_t(keeps[block]))
        {
            ++block;
            position = 0;
        }
        if (bit.get_limb(0) == 1)
            selected.push_back({distance, label});
    }
    std::sort(selected.begin(), selected.end(), [](const Pair& a, const Pair& b) {
        return a[0].get_limb(0) < b[0].get_limb(0);
    });
    valid &= selected == reference;

    for (const auto& batch : batches)
    {
        std::cout << "CUT_BATCH phase=" << batch.phase << " layer=" << batch.layer
                  << " width=" << batch.width << " backend=" << batch.backend;
        print_metric(batch.metric);
        std::cout << '\n';
    }
    for (size_t i = 0; i < totals.size(); ++i)
    {
        std::cout << "CUT_PHASE phase=" << phases[i];
        print_metric(totals[i]);
        std::cout << '\n';
    }
    std::cout << "CUT_PHASE phase=online";
    print_metric(online);
    std::cout << "\naccounting_check=" << (accounting ? "PASS" : "FAIL")
              << "\ncorrectness=" << (valid ? "PASS" : "FAIL") << std::endl;
    return accounting && valid ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    try { return run(argc, argv); }
    catch (const std::exception& error)
    {
        std::cerr << "error=" << error.what() << '\n';
        return 1;
    }
}
