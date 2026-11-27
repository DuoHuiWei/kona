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
    const auto plan_start = Clock::now();
    const auto plan = prepare(n, k, layout);
    const auto plan_ms = std::chrono::duration<double, std::milli>(Clock::now()-plan_start).count();
    vector<int> keeps;
    for (const auto& block : plan.blocks.plan.blocks)
        keeps.push_back(block.keep);
    std::cout << std::fixed << std::setprecision(9)
              << "n=" << n << "\nk=" << k << "\nparty=" << party
              << "\nprofile=" << profile << "\nthreshold=" << threshold
              << "\nchunking=" << chunking << "\ndcf_chunk=" << KonaBenchConfig::dcf_chunk()
              << "\nscs_mul_chunk=" << KonaBenchConfig::mul_chunk()
              << "\nplan_ms=" << plan_ms
              << "\nnetwork_structural_depth=" << plan.blocks.plan.block_depth + plan.terminal.depth
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

    const vector<string> block_steps={"block_compare","scs"};
    const vector<string> terminal_steps={"candidate_collect","matrix_compare","rank_local",
            "rank_mask_prepare","rank_mask"};
    std::map<string,Metric> totals;
    for(const auto& name:block_steps)totals.emplace(name,Metric{});
    for(const auto& name:terminal_steps)totals.emplace(name,Metric{});
    vector<Batch> batches;
    batches.reserve(2*plan.blocks.fused_block_levels.size()+2);
    auto record_batch=[&](const char* name,size_t layer,size_t width,Backend backend,auto operation) {
        auto metric=measure(operation);
        metric.items=width;
        batches.push_back({name,backend==Backend::AUTO?"SCS":KonaReducedDepthHybrid::backend_name(backend),
                layer,width,metric});
    };
    auto record_local=[&](const char* name,auto operation) {
        totals.at(name)+=measure(operation);
    };
    Metric block,terminal;
    auto record_parent=[&](const char* name,auto operation) {
        (std::string_view(name)=="block_cong"?block:terminal)=measure(operation);
    };

    Output<64> output;
    octetStream ready,peer;
    Ring(0).pack(ready);
    player.exchange_concurrent(ready,peer);
    auto online=measure([&] {
        output=KonaCutV3::execute(work,plan,profile,policy,dcf,&player,
                record_parent,record_batch,record_local);
    });

    bool accounting=true;
    for(auto& batch:batches) {
        auto& metric=batch.metric;
        const auto backend=batch.backend=="SCS"?Backend::AUTO:
                batch.backend=="DCF"?Backend::DCF:Backend::PCR;
        const auto shape=batch_shape(batch.width,backend);
        metric.rounds=shape.logical;
        accounting &= metric.bytes==shape.bytes && metric.actions==2*shape.exchanges &&
                metric.evaluations==shape.dcf_calls;
        totals.at(batch.phase)+=metric;
    }
    auto close=[&](Metric& parent,const vector<string>& steps) {
        Metric children;
        for(const auto& step:steps)children+=totals.at(step);
        accounting &= parent.bytes==children.bytes && parent.actions==children.actions &&
                parent.evaluations==children.evaluations && parent.ms>=children.ms;
        parent.rounds=children.rounds;
        parent.items=children.items;
        Metric other;
        other.ms=parent.ms-children.ms;
        return other;
    };
    totals["block_cong_other"]=close(block,block_steps);
    totals["terminal_other"]=close(terminal,terminal_steps);
    totals["block_cong"]=block;
    totals["terminal"]=terminal;
    totals["topk_other"]=close(online,{"block_cong","terminal"});
    online.items=n;
    accounting &= totals.at("block_compare").items==plan.blocks.plan.block_comparators &&
            totals.at("scs").items==totals.at("block_compare").items &&
            totals.at("matrix_compare").items==plan.terminal.matrix_comparators &&
            totals.at("rank_mask").items==plan.terminal.mask_comparators;

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
    size_t block_index = 0, position = 0;
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
        if (++position == size_t(keeps[block_index]))
        {
            ++block_index;
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
    for (const auto& [name,metric]:totals)
    {
        const bool executed=(name!="matrix_compare"||metric.items) &&
                (name!="block_compare"||metric.items) && (name!="scs"||metric.items) &&
                ((name!="rank_local"&&name!="rank_mask"&&name!="rank_mask_prepare")||!plan.terminal.direct);
        std::cout << "CUT_PHASE phase=" << name << " executed=" << executed;
        print_metric(metric);
        std::cout << '\n';
    }
    std::cout << "CUT_PHASE phase=topk executed=1";
    print_metric(online);
    std::cout << "\ntopk_online_ms=" << online.ms;
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
