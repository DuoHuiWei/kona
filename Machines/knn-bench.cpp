// Unified KNN experiment body. One binary, four schemes, explicit scopes.
// Protocols remain in their original modules/baselines; benchmark engines are
// contained here rather than linked through other benchmark entry files.
// This file is maintained directly; no source generation occurs at build/run.
#include <iostream>
#include <random>
#include <chrono>
#include "Networking/Player.h"
#include "Tools/ezOptionParser.h"
#include "Networking/Server.h"
#include "Tools/octetStream.h"
#include "Networking/PlayerBuffer.h"
#include "Tools/PointerVector.h"
#include "Tools/int.h"
#include "Math/bigint.h"
#include "Math/Z2k.hpp"
#include "Tools/TimerWithComm.h"
#include "Math/FixedVec.h"
#include "Machines/kona-bench-affinity.hpp"
#include "Machines/kona-bench-config.hpp"
#include "ConstantPP/data-loader.hpp"
#include <iomanip>
#include <memory>
#include <sstream>
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <sys/resource.h>
#include "Machines/kona-cong-topk.hpp"
#include "Machines/kona-dcf-compare.hpp"
#include "Machines/kona-reduced-depth-cong-topk.hpp"
#include "Machines/kona-reduced-depth-hybrid-adapter.hpp"
#include "Machines/kona-mask-vote.hpp"
#include <limits>
#include <functional>
#include <cstdio>
#include <numeric>
#include "ConstantPP/sec-kc-batch.hpp"
#include "ConstantPP/secmax-local-fss-l2.hpp"
#include <cstring>
#include <cctype>
#include <set>
#include "Machines/kona-nhss-cong-pipeline.hpp"

namespace secknn_native {
#define SECKNN_STAGE_BENCH
#define main preserved_native_main
#include "Machines/secknn.cpp"
#undef main
#undef SECKNN_STAGE_BENCH
} // namespace secknn_native

namespace kona_native {
#define KONA_STAGE_BENCH
#define main preserved_native_main
#include "Machines/kona-upstream.cpp"
#undef main
#undef KONA_STAGE_BENCH
} // namespace kona_native

// ezOptionParser otherwise silently retains unknown flags. Reject them before
// networking, including trailing flags stored as positional arguments.
static void reject_unknown_options(const ez::ezOptionParser& parser) {
    for(const auto* arg:parser.unknownArgs)
        throw std::invalid_argument("unknown option/argument: "+*arg);
    for(const auto* list:{&parser.firstArgs,&parser.lastArgs})
        for(const auto* arg:*list)
            if(arg->size()>1 && (*arg)[0]=='-' && !std::isdigit(static_cast<unsigned char>((*arg)[1])))
                throw std::invalid_argument("unknown option: "+*arg);
}

namespace constantpp_engine {


using std::cout;
using std::endl;
using std::runtime_error;
using std::string;
using std::vector;

namespace
{

int playerno = 0;
ez::ezOptionParser opt;

struct TestOptions
{
    std::size_t limit_train = 0;
    std::size_t limit_features = 0;
    std::size_t k = 5;
    bool distance_only = false;
    bool pre_vote_only = false;
    bool vote_only = false;
    string secmax_mode = "paper-local-l2";
};

void parse_argv(int argc, const char** argv)
{
    opt.add("6000", 0, 1, 0, "Port number base",
            "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number",
            "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on",
            "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host",
            "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file",
            "-ip", "--ip-file-name");
    opt.add("0", 0, 1, 0, "Limit training rows (0 means all)",
            "--limit-train");
    opt.add("0", 0, 1, 0, "Limit features (0 means all)",
            "--limit-features");
    opt.add("5", 0, 1, 0, "KNN neighbor count",
            "-k", "--k");
    opt.add("", 0, 0, 0, "Run only Batch SecED",
            "--distance-only");
    opt.add("", 0, 0, 0, "Measure SecED and complete SecKMin, stop before vote",
            "--pre-vote-only");

    opt.add("", 0, 0, 0, "Measure only SecFre and SecMax on prepared Top-k labels", "--vote-only");
    opt.add("UNVERIFIED", 0, 1, 0, "Declared network profile; actual snapshot in log wrapper", "--network-profile");
    opt.add("paper-local-l2", 0, 1, 0, "SecMax only: paper-local-l2 (performance, NO privacy) or legacy", "--secmax-mode");
    opt.parse(argc, argv);
    reject_unknown_options(opt);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
}

RealTwoPartyPlayer* start_networking()
{
    string hostname;
    string ip_file_name;
    int pnbase = 0;
    int my_port = Names::DEFAULT_PORT;

    opt.get("--portnumbase")->getInt(pnbase);
    opt.get("--hostname")->getString(hostname);
    opt.get("--ip-file-name")->getString(ip_file_name);

    ez::OptionGroup* mp_opt = opt.get("--my-port");
    if (mp_opt->isSet)
        mp_opt->getInt(my_port);

    Names player_names;
    if (!ip_file_name.empty())
        player_names.init(playerno, pnbase, ip_file_name, 2);
    else
        Server::start_networking(
                player_names, playerno, 2,
                hostname, pnbase, my_port);

    return new RealTwoPartyPlayer(
            player_names, 1 - playerno, 0);
}

string dataset_name_from_argv(int argc, const char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const string arg(argv[i]);
        if (!arg.empty() && arg[0] != '-')
            return arg;
        if ((arg == "-p" || arg == "--player" ||
             arg == "-pn" || arg == "--portnumbase" ||
             arg == "-mp" || arg == "--my-port" ||
             arg == "-h" || arg == "--hostname" ||
             arg == "-ip" || arg == "--ip-file-name" ||
             arg == "--network-profile" || arg == "--secmax-mode" ||
             arg == "--limit-train" ||
             arg == "--limit-features" ||
             arg == "-k" || arg == "--k") && i + 1 < argc)
            ++i;
    }
    return "arcene";
}

std::size_t get_size_option(const char* name)
{
    long long value = 0;
    opt.get(name)->getLongLong(value);
    if (value < 0)
        throw runtime_error(string(name) + " must be non-negative");
    return static_cast<std::size_t>(value);
}

TestOptions get_test_options()
{
    TestOptions options;
    options.limit_train = get_size_option("--limit-train");
    options.limit_features = get_size_option("--limit-features");
    options.k = get_size_option("--k");
    options.distance_only = opt.isSet("--distance-only");
    options.pre_vote_only = opt.isSet("--pre-vote-only");
    options.vote_only = opt.isSet("--vote-only");
    if (options.distance_only && options.pre_vote_only)
        throw runtime_error("--distance-only and --pre-vote-only are mutually exclusive");
    opt.get("--secmax-mode")->getString(options.secmax_mode);
    if (options.secmax_mode != "paper-local-l2" && options.secmax_mode != "legacy")
        throw runtime_error("--secmax-mode must be paper-local-l2 or legacy");
    if (options.k == 0)
        throw runtime_error("--k must be positive");
    return options;
}

void apply_limits(
        ConstantPP::ConstantPPDataset& dataset,
        const TestOptions& options)
{
    const std::size_t train_count =
            options.limit_train == 0
                    ? dataset.database.size()
                    : std::min(options.limit_train, dataset.database.size());
    const std::size_t feature_count =
            options.limit_features == 0
                    ? dataset.num_features
                    : std::min(options.limit_features, dataset.num_features);

    if (train_count == 0 || feature_count == 0)
        throw runtime_error("effective dataset dimensions must be positive");

    dataset.database.resize(train_count);
    dataset.labels.resize(train_count);
    dataset.query.resize(feature_count);
    for (auto& row : dataset.database)
        row.resize(feature_count);
    dataset.num_features = feature_count;
}

std::uint64_t plaintext_expected_label(
        const ConstantPP::ConstantPPDataset& dataset,
        std::size_t k)
{
    const std::size_t n = dataset.database.size();
    vector<std::uint64_t> distance(n, 0);
    for (std::size_t i = 0; i < n; ++i)
    {
        unsigned __int128 sum = 0;
        for (std::size_t j = 0; j < dataset.num_features; ++j)
        {
            const std::uint64_t x = dataset.database[i][j];
            const std::uint64_t q = dataset.query[j];
            const std::uint64_t delta = x >= q ? x - q : q - x;
            sum += static_cast<unsigned __int128>(delta) * delta;
        }
        distance[i] = static_cast<std::uint64_t>(sum);
    }

    vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(
            order.begin(),
            order.end(),
            [&](std::size_t lhs, std::size_t rhs) {
                if (distance[lhs] != distance[rhs])
                    return distance[lhs] < distance[rhs];
                return lhs > rhs;
            });

    vector<bool> selected(n, false);
    for (std::size_t i = 0; i < k; ++i)
        selected[order[i]] = true;

    vector<std::uint64_t> selected_labels;
    selected_labels.reserve(k);
    for (std::size_t i = 0; i < n; ++i)
        if (selected[i])
            selected_labels.push_back(dataset.labels[i]);

    std::size_t best_pos = 0;
    std::size_t best_frequency = 0;
    for (std::size_t i = 0; i < selected_labels.size(); ++i)
    {
        const auto label = selected_labels[i];
        const auto frequency = static_cast<std::size_t>(
                std::count(
                        selected_labels.begin(),
                        selected_labels.end(),
                        label));
        if (frequency >= best_frequency)
        {
            best_frequency = frequency;
            best_pos = i;
        }
    }
    return selected_labels[best_pos];
}

double elapsed_ms(
        const std::chrono::steady_clock::time_point& start,
        const std::chrono::steady_clock::time_point& end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

string dataset_path_from_name(const string& name)
{
    if (name.find('/') != string::npos)
        return name;
    if (name.size() >= 5 && name.substr(name.size() - 5) == "-data")
        return "ConstantPP/data/" + name;
    return "ConstantPP/data/" + name + "-data";
}

ConstantPP::Ring ring(std::uint64_t x)
{
    return ConstantPP::Ring(static_cast<mp_limb_t>(x));
}

vector<ConstantPP::Ring> make_share(
        const vector<std::uint64_t>& plain,
        int party,
        std::uint64_t base)
{
    vector<ConstantPP::Ring> out(plain.size());
    for (std::size_t i = 0; i < plain.size(); ++i)
    {
        const auto clear = ring(plain[i]);
        const auto s0 = ring(base + 31 * i);
        out[i] = party == 0 ? s0 : clear - s0;
    }
    return out;
}

vector<vector<ConstantPP::Ring>> make_database_share(
        const vector<vector<std::uint64_t>>& plain,
        int party)
{
    vector<vector<ConstantPP::Ring>> out;
    out.reserve(plain.size());
    for (std::size_t i = 0; i < plain.size(); ++i)
        out.push_back(
                make_share(
                        plain[i],
                        party,
                        1000 + 211 * static_cast<std::uint64_t>(i)));
    return out;
}

ConstantPP::SecEdPartyMaterial make_ed_material(int party)
{
    ConstantPP::SecEdPartyMaterial material;
    if (party == 0)
    {
        material.a_share = ring(3);
        material.c_share = ring(20);
    }
    else
    {
        material.a_share = ring(4);
        material.c_share = ring(29);
    }
    return material;
}

ConstantPP::ShufflePartyMaterial make_identity_shuffle_material(
        int party,
        std::size_t n)
{
    ConstantPP::ShufflePartyMaterial material;
    material.permutation.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        material.permutation[i] = i;

    material.a_share.assign(2, vector<ConstantPP::Ring>(n, ring(0)));
    material.b_share.assign(2, vector<ConstantPP::Ring>(n, ring(0)));
    (void)party;
    return material;
}

ConstantPP::BeaverTripleParty make_triple(int party)
{
    ConstantPP::BeaverTripleParty triple;
    if (party == 0)
    {
        triple.a_share = ring(2);
        triple.b_share = ring(3);
        triple.c_share = ring(11);
    }
    else
    {
        triple.a_share = ring(3);
        triple.b_share = ring(4);
        triple.c_share = ring(24);
    }
    return triple;
}

std::uint64_t reveal_to_p0(
        RealTwoPartyPlayer* player,
        int party,
        const ConstantPP::Ring& local)
{
    if (party == 0)
    {
        octetStream os;
        player->receive(os);
        ConstantPP::Ring peer;
        peer.unpack(os);
        return (local + peer).get_limb(0);
    }

    octetStream os;
    local.pack(os);
    player->send(os);
    return 0;
}

std::uint64_t actions(const NamedCommStats& stats)
{
    std::uint64_t n = 0;
    for (const auto& entry : stats) n += entry.second.rounds;
    return n;
}
void barrier(RealTwoPartyPlayer* player)
{
    octetStream send, receive;
    ring(0).pack(send);
    if (playerno == 0) { player->send(send); player->receive(receive); }
    else { player->receive(receive); player->send(send); }
}
void print_phase(const char* name, double ms, std::uint64_t bytes,
        std::uint64_t rounds, std::uint64_t theoretical)
{
    if (rounds % 2) throw runtime_error("non-paired communication in ConstantPP stage");
    cout << "BENCH_PHASE phase=" << name << " party=" << playerno << " trial=1"
         << " online_ms=" << ms << " sent_bytes=" << bytes
         << " transport_actions=" << rounds << " exchange_rounds=" << rounds / 2
         << " theoretical_rounds=" << theoretical << '\n';
}

} // namespace

int run(int argc, const char** argv)
{
    parse_argv(argc, argv);
    if (playerno != 0 && playerno != 1)
        throw runtime_error("player must be 0 or 1");

    const string dataset_name = dataset_name_from_argv(argc, argv);
    const string dataset_path = dataset_path_from_name(dataset_name);
    const auto options = get_test_options();

    const auto load_start = std::chrono::steady_clock::now();
    auto dataset = ConstantPP::load_constantpp_dataset(dataset_path);
    const std::size_t source_features = dataset.num_features;
    const std::size_t source_train_count = dataset.database.size();
    apply_limits(dataset, options);
    const auto load_end = std::chrono::steady_clock::now();

    if (!options.distance_only && dataset.database.size() < options.k)
        throw runtime_error("dataset has fewer than k training rows");

    const auto expected_label = (options.distance_only || options.pre_vote_only)
            ? 0
            : plaintext_expected_label(dataset, options.k);

    std::unique_ptr<RealTwoPartyPlayer> connection(start_networking());
    auto* player = connection.get();
    string network_profile;
    opt.get("--network-profile")->getString(network_profile);
    cout << std::fixed << std::setprecision(9)
         << "metrics_version=reference-online-v1\nparty=" << playerno
         << "\ndataset=" << dataset_name << "\ndataset_path=" << dataset_path
         << "\nnetwork_env=" << network_profile
         << "\nbackend=constantpp-batch+kona-dcf\nmaterial_mode=existing-deterministic-test-material\n";


    if (options.vote_only)
    {
        if(options.secmax_mode != "paper-local-l2")
            throw runtime_error("isolated ConstantPP vote requires paper-local-l2");
        // Same identity-shuffled Top-k labels/order as SecKMin, prepared outside
        // online timing. Reuse real SecFre/SecMax without repeating distance/rank.
        const auto n=dataset.database.size(), k=options.k;
        vector<std::uint64_t> distances(n,0);
        vector<std::size_t> order(n);
        std::iota(order.begin(),order.end(),0);
        for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<dataset.num_features;++j){
            const auto delta=ring(dataset.database[i][j])-ring(dataset.query[j]);
            distances[i]+=(delta*delta).get_limb(0);
        }
        std::sort(order.begin(),order.end(),[&](std::size_t i,std::size_t j){
            return distances[i]<distances[j] || (distances[i]==distances[j] && i>j);
        });
        order.resize(k);std::sort(order.begin(),order.end());
        vector<std::uint64_t> labels;for(auto i:order)labels.push_back(dataset.labels[i]);
        const auto shares=make_share(labels,playerno,9000);
        const auto triple=make_triple(playerno);
        KonaDcfCompare::Compare64<64> dcf(player,playerno);
        ConstantPP::SecMaxLocalFssL2 local_max(playerno,k);
        ConstantPP::ProtocolStats protocol;
        ConstantPP::SecFreStats fre;
        ConstantPP::SecMaxStats maximum;
        barrier(player);
        const auto before=player->total_comm();
        const auto start=std::chrono::steady_clock::now();
        const auto frequency=ConstantPP::sec_fre_kona_dcf(player,playerno,shares,dcf,&protocol,&fre);
        const auto result=ConstantPP::sec_max_kona_dcf(player,playerno,frequency,shares,triple,dcf,&protocol,&maximum,&local_max);
        const auto end=std::chrono::steady_clock::now();
        const auto delta=player->total_comm()-before;
        const auto pairs=k*(k-1)/2;
        const bool accounting=delta.sent==(k>1?16*pairs+40*k:0) &&
                actions(delta)==(k>1?8u:0u) && protocol.logical_rounds==(k>1?4u:0u) &&
                fre.upper_triangle_eq_predicates==pairs &&
                maximum.local_fss_calls==k*(k-1);
        bool correct=accounting;
        const auto opened=reveal_to_p0(player,playerno,result);
        octetStream status;
        if(playerno==0){correct=correct && opened==expected_label;ring(correct?1:0).pack(status);player->send(status);}
        else {player->receive(status);ConstantPP::Ring flag;flag.unpack(status);correct=correct && flag.get_limb(0)==1;}
        print_phase("vote",elapsed_ms(start,end),delta.sent,actions(delta),k>1?4:0);
        cout<<"secmax_mode="<<options.secmax_mode
            <<"\nsecmax_security=NOT_PROVIDED_PERFORMANCE_ONLY\ninput_preparation=local_exact_topk_labels_outside_timer"
            <<"\ntrain_count="<<n<<"\nfeatures="<<dataset.num_features<<"\nquery_count=1\nk="<<k
            <<"\nvote_dcf_evaluate_calls="<<dcf.get_stats().evaluate_calls+maximum.local_fss_calls
            <<"\noutput_scope=winning_label_secret_share\naccounting_check="<<(accounting?"PASS":"FAIL")
            <<"\ncorrectness="<<(correct?"PASS":"FAIL")<<"\ntest_status="<<(correct?"COMPLETED":"FAILED")<<endl;
        return correct?0:1;
    }

    const auto setup_start = std::chrono::steady_clock::now();
    const auto query_share = make_share(dataset.query, playerno, 7000);
    const auto database_share = make_database_share(dataset.database, playerno);
    const auto ed_material = make_ed_material(playerno);

    if (options.distance_only)
    {
        barrier(player); // Outside online time and byte snapshots.
        const auto comm_before = player->total_comm();
        const auto distance_start = std::chrono::steady_clock::now();
        const auto distance_share =
                ConstantPP::sec_ed_batch_n_dim(
                        player,
                        playerno,
                        query_share,
                        database_share,
                        ed_material,
                        nullptr);
        const auto distance_end = std::chrono::steady_clock::now();
        const auto comm_delta = player->total_comm() - comm_before;
        // Validation is deliberately after every online snapshot.
        vector<ConstantPP::Ring> peer;
        ConstantPP::exchange_ring_vector(player, distance_share, peer, nullptr);
        bool distance_ok = peer.size() == dataset.database.size();
        for (std::size_t i = 0; i < dataset.database.size(); ++i) {
            ConstantPP::Ring expected(0);
            for (std::size_t j = 0; j < dataset.num_features; ++j) {
                const auto delta = ring(dataset.database[i][j]) - ring(dataset.query[j]);
                expected += delta * delta;
            }
            distance_ok = distance_ok && distance_share[i] + peer[i] == expected;
        }

        {
            std::uint64_t transport_rounds = 0;
            for (auto it = comm_delta.begin(); it != comm_delta.end(); ++it)
                transport_rounds +=
                        static_cast<std::uint64_t>(it->second.rounds);

            print_phase("distance", elapsed_ms(distance_start, distance_end),
                    comm_delta.sent, transport_rounds, 1);
            cout << "scope=distance_only\noutput_scope=distance_shares\nknn_online_e2e_ms=NOT_IN_SCOPE\n"
                 << "topk_status=NOT_IN_SCOPE\nvote_status=NOT_IN_SCOPE\naccounting_check=PASS\ntest_status=COMPLETED\n";
            cout << "test=constantpp_sec_kc_data" << endl;
            cout << "dataset=" << dataset_name << endl;
            cout << "source_features=" << source_features << endl;
            cout << "source_train_count=" << source_train_count << endl;
            cout << "features=" << dataset.num_features << endl;
            cout << "train_count=" << dataset.database.size() << endl;
            cout << "query_count=1" << endl;
            cout << "distance_only=YES" << endl;
            cout << "distance_count=" << distance_share.size() << endl;
            cout << "timer_scope=shares_ready_to_distance_shares_ready" << endl;
            cout << "communication_party=" << playerno << endl;
            cout << "distance_ms="
                 << elapsed_ms(distance_start, distance_end) << endl;
            cout << "online_total_ms="
                 << elapsed_ms(distance_start, distance_end) << endl;
            cout << "distance_sent_bytes=" << comm_delta.sent << endl;
            cout << "total_sent_bytes=" << comm_delta.sent << endl;
            cout << "distance_transport_actions="
                 << transport_rounds << endl;
            cout << "total_transport_actions="
                 << transport_rounds << endl;
            cout << "distance_logical_rounds=1" << endl;
            cout << "total_logical_rounds=1" << endl;
            cout << "correctness=" << (distance_ok ? "PASS" : "FAIL") << endl;
            cout << "result=" << (distance_ok ? "PASS" : "FAIL") << endl;
        }

        return distance_ok ? 0 : 1;
    }

    if (options.pre_vote_only)
    {
        // Original distance + complete SecKMin; only optional boundary snapshots.
        // k changes the threshold and output length, not the normal-path work count.
        const auto labels = make_share(dataset.labels, playerno, 9000);
        const auto shuffle = make_identity_shuffle_material(playerno, dataset.database.size());
        const auto plan = ConstantPP::make_sec_bcom_upper_triangle_plan(dataset.database.size());
        KonaDcfCompare::Compare64<64> dcf(player, playerno);
        ConstantPP::ProtocolStats protocol;
        ConstantPP::SecBComStats rank_stats;
        ConstantPP::SecKMinStats kmin_stats;
        ConstantPP::SecKMinTrace trace;
        if (options.k >= dataset.database.size())
            throw runtime_error("pre-vote measurement requires 1 <= k < N");
        barrier(player);
        const auto before = player->total_comm();
        const auto start = std::chrono::steady_clock::now();
        const auto distance = ConstantPP::sec_ed_batch_n_dim(player, playerno,
                query_share, database_share, ed_material, &protocol);
        const auto distance_end = std::chrono::steady_clock::now();
        const auto middle = player->total_comm();
        const auto kmin_start = std::chrono::steady_clock::now();
        const auto selected = ConstantPP::sec_kmin_kona_dcf(player, playerno,
                distance, labels, options.k, shuffle, plan, dcf, &protocol,
                &rank_stats, &kmin_stats, &trace);
        const auto end = std::chrono::steady_clock::now();
        const auto after = player->total_comm();
        // Derive all detailed metrics after the independent outer end snapshot.
        const auto distance_delta = middle - before;
        const auto total_delta = after - before;
        const auto n = dataset.database.size();
        const auto pairs = n * (n - 1) / 2;
        const auto chunks = KonaBenchConfig::chunks(pairs,KonaBenchConfig::dcf_chunk());
        const auto rank_delta = trace.boundaries[1].comm - trace.boundaries[0].comm;
        const auto threshold_delta = trace.boundaries[2].comm - trace.boundaries[1].comm;
        const bool accounting_ok = rank_stats.dcf_compare_pairs == pairs &&
                rank_stats.kona_dcf_evaluate_calls == 2 * pairs &&
                rank_delta.sent == 8 * pairs && actions(rank_delta) == 2 * chunks &&
                threshold_delta.sent == 8 * n && actions(threshold_delta) == 2 * KonaBenchConfig::chunks(n,KonaBenchConfig::dcf_chunk()) &&
                kmin_stats.threshold_compare_pairs == n &&
                kmin_stats.threshold_dcf_evaluate_calls == 2 * n &&
                protocol.logical_rounds == 6;
        // Reveal only to validate, after online snapshots; tie order matches SecBCom.
        vector<ConstantPP::Ring> peer;
        ConstantPP::exchange_ring_vector(player, selected, peer, nullptr);
        vector<std::uint64_t> clear_distance(n, 0);
        vector<std::size_t> order(n);
        std::iota(order.begin(), order.end(), 0);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < dataset.num_features; ++j) {
                const auto d = ring(dataset.database[i][j]) - ring(dataset.query[j]);
                clear_distance[i] += (d * d).get_limb(0);
            }
        std::sort(order.begin(), order.end(), [&](std::size_t i, std::size_t j) {
            return clear_distance[i] < clear_distance[j] ||
                    (clear_distance[i] == clear_distance[j] && i > j);
        });
        order.resize(options.k);
        std::sort(order.begin(), order.end()); // SecKMin collects in shuffled input order.
        bool correct = accounting_ok && selected.size() == options.k;
        for (std::size_t i = 0; i < selected.size(); ++i)
            correct = correct && (selected[i] + peer[i]).get_limb(0) == dataset.labels[order[i]];
        print_phase("distance", elapsed_ms(start, distance_end), distance_delta.sent,
                actions(distance_delta), 1);
        const char* names[] = {"shuffle", "rank_matrix", "threshold", "indicator_select"};
        const std::uint64_t theoretical[] = {2, 1, 1, 1};
        for (int i = 0; i < 4; ++i) {
            const auto prev_comm = i == 0 ? middle : trace.boundaries[i-1].comm;
            const auto prev_time = i == 0 ? kmin_start : trace.boundaries[i-1].time;
            const auto delta = trace.boundaries[i].comm - prev_comm;
            print_phase(names[i], elapsed_ms(prev_time, trace.boundaries[i].time),
                    delta.sent, actions(delta), theoretical[i]);
        }
        const auto tail = after - trace.boundaries[3].comm;
        print_phase("online_other", elapsed_ms(distance_end, kmin_start) +
                elapsed_ms(trace.boundaries[3].time, end), tail.sent, actions(tail), 0);
        print_phase("pre_vote_online", elapsed_ms(start, end), total_delta.sent,
                actions(total_delta), 6);
        cout << "scope=pre_vote_online\noutput_scope=selected_label_secret_shares\n"
             << "knn_online_e2e_ms=NOT_IN_SCOPE\nvote_status=NOT_IN_SCOPE\n"
             << "train_count=" << n << "\nfeatures=" << dataset.num_features
             << "\nquery_count=1\nk=" << options.k << "\nrank_compare_pairs=" << pairs
             << "\nrank_compare_chunk_size=" << KonaBenchConfig::dcf_chunk() << "\nrank_compare_chunks=" << chunks
             << "\nrank_dcf_evaluate_calls=" << rank_stats.kona_dcf_evaluate_calls
             << "\nthreshold_compare_pairs=" << kmin_stats.threshold_compare_pairs
             << "\nthreshold_dcf_evaluate_calls=" << kmin_stats.threshold_dcf_evaluate_calls
             << "\naccounting_check=" << (accounting_ok ? "PASS" : "FAIL")
             << "\ncorrectness=" << (correct ? "PASS" : "FAIL")
             << "\ntest_status=" << (correct ? "COMPLETED" : "FAILED") << endl;
        return correct ? 0 : 1;
    }

    const auto label_share = make_share(dataset.labels, playerno, 9000);
    const auto shuffle_material =
            make_identity_shuffle_material(
                    playerno,
                    dataset.database.size());
    const auto bcom_plan =
            ConstantPP::make_sec_bcom_upper_triangle_plan(
                    dataset.database.size());
    const auto triple = make_triple(playerno);
    const auto setup_end = std::chrono::steady_clock::now();

    const auto dcf_init_start = std::chrono::steady_clock::now();
    KonaDcfCompare::Compare64<64> dcf_compare(player, playerno);
    const auto dcf_init_end = std::chrono::steady_clock::now();
    const auto secmax_setup_start = std::chrono::steady_clock::now();
    std::unique_ptr<ConstantPP::SecMaxLocalFssL2> secmax_l2;
    if (options.secmax_mode == "paper-local-l2")
        secmax_l2.reset(new ConstantPP::SecMaxLocalFssL2(playerno, options.k));
    const auto secmax_setup_end = std::chrono::steady_clock::now();
    ConstantPP::ProtocolStats protocol_stats;
    ConstantPP::SecKCBatchStats kc_stats;

    ConstantPP::SecKCBatchTrace trace;
    barrier(player);
    const auto outer_comm_before = player->total_comm();
    const auto protocol_start = std::chrono::steady_clock::now();
    const auto result_share =
            ConstantPP::sec_kc_batch_kona(
                    player,
                    playerno,
                    query_share,
                    database_share,
                    label_share,
                    options.k,
                    ed_material,
                    shuffle_material,
                    bcom_plan,
                    triple,
                    dcf_compare,
                    &protocol_stats,
                    &kc_stats,
                    &trace,
                    secmax_l2.get());
    const auto protocol_end = std::chrono::steady_clock::now();
    const auto outer_delta = player->total_comm() - outer_comm_before;
    // Deferred stage accounting is intentionally after the outer end snapshot.
    ConstantPP::finalize_sec_kc_batch_stats(trace, kc_stats);
    const auto outer_actions = actions(outer_delta);
    const auto rank_delta = kc_stats.max.rank_comm_after - kc_stats.max.rank_comm_before;
    const auto rank_expected_bytes = options.k > 1 ? options.k * sizeof(ConstantPP::Ring) : 0;
    const bool local_rank_ok = !secmax_l2 || (rank_delta.sent == rank_expected_bytes &&
            actions(rank_delta) == (options.k > 1 ? 2u : 0u) &&
            kc_stats.max.local_fss_calls == options.k * (options.k - 1));
    const bool accounting_ok = local_rank_ok && kc_stats.online.sent_bytes == outer_delta.sent &&
            kc_stats.online.transport_rounds == outer_actions && outer_actions % 2 == 0;


    const auto revealed_label =
            reveal_to_p0(player, playerno, result_share);

    bool ok = accounting_ok;
    if (playerno == 0)
    {
        ok = ok && revealed_label == expected_label &&
             kc_stats.distance_count == dataset.database.size();
        octetStream status;
        ring(ok ? 1 : 0).pack(status);
        player->send(status);
    }
    else
    {
        octetStream status;
        player->receive(status);
        ConstantPP::Ring flag;
        flag.unpack(status);
        ok = ok && flag.get_limb(0) == 1;
    }
    {
        if (!accounting_ok) throw runtime_error("ConstantPP stage/outer byte or action mismatch");
        for (const auto& phase : {std::make_pair("distance", &kc_stats.distance),
                std::make_pair("topk", &kc_stats.topk), std::make_pair("vote", &kc_stats.vote)})
            print_phase(phase.first, phase.second->online_ms, phase.second->sent_bytes,
                    phase.second->transport_rounds, phase.second->logical_rounds);
        print_phase("online_other", elapsed_ms(protocol_start, protocol_end) - kc_stats.online.online_ms,
                outer_delta.sent - kc_stats.online.sent_bytes,
                outer_actions - kc_stats.online.transport_rounds, 0);
        print_phase("knn_online", elapsed_ms(protocol_start, protocol_end),
                outer_delta.sent, outer_actions, protocol_stats.logical_rounds);
        cout << "secmax_mode=" << options.secmax_mode << '\n'
             << "secmax_security=" << (secmax_l2 ? "NOT_PROVIDED_PERFORMANCE_ONLY" : "EXISTING_L2_TEST_SETTINGS") << '\n'
             << "secmax_rank_sent_bytes=" << rank_delta.sent << '\n'
             << "secmax_rank_transport_actions=" << actions(rank_delta) << '\n'
             << "secmax_rank_exchange_rounds=" << actions(rank_delta) / 2 << '\n'
             << "secmax_rank_local_fss_calls=" << kc_stats.max.local_fss_calls << '\n'
             << "secmax_rank_local_fss_ms=" << kc_stats.max.local_fss_ms << '\n'
             << "secmax_l2_setup_ms=" << elapsed_ms(secmax_setup_start, secmax_setup_end) << '\n';
        cout << "scope=knn_online\noutput_scope=winning_label_secret_share\n"
             << "knn_online_e2e_ms=" << elapsed_ms(protocol_start, protocol_end) << '\n'
             << "accounting_check=" << (accounting_ok ? "PASS" : "FAIL") << '\n'
             << "correctness=" << (ok ? "PASS" : "FAIL") << '\n'
             << "test_status=" << (ok ? "COMPLETED" : "FAILED") << '\n';
        cout << "test=constantpp_sec_kc_data" << endl;
        cout << "dataset=" << dataset_name << endl;
        cout << "source_features=" << source_features << endl;
        cout << "source_train_count=" << source_train_count << endl;
        cout << "features=" << dataset.num_features << endl;
        cout << "train_count=" << dataset.database.size() << endl;
        cout << "query_count=1" << endl;
        cout << "k=" << options.k << endl;
        cout << "distance_count=" << kc_stats.distance_count << endl;
        cout << "topk_rank_compare_pairs=" << kc_stats.kmin_bcom.dcf_compare_pairs << endl;
        cout << "topk_threshold_compare_pairs=" << kc_stats.kmin.threshold_compare_pairs << endl;
        cout << "vote_frequency_eq_predicates=" << kc_stats.fre.upper_triangle_eq_predicates << endl;
        cout << "vote_max_compare_pairs=" << kc_stats.max.bcom.dcf_compare_pairs << endl;
        cout << "vote_secure_mul_pairs=" << kc_stats.max.secure_mul_pairs << endl;
        cout << "chunk_limit=" << KonaBenchConfig::dcf_chunk() << "\nstructural_depth=NOT_APPLICABLE\n";
        if (playerno == 0) cout << "revealed_label=" << revealed_label << endl;
        cout << "expected_label=" << expected_label << endl;
        cout << "query_label=" << dataset.query_label << endl;
        cout << "timer_scope=shares_ready_to_result_share_ready" << endl;
        cout << "communication_party=" << playerno << endl;
        cout << "online_ms=" << elapsed_ms(protocol_start, protocol_end) << endl;
        cout << "stage_sum_ms=" << kc_stats.online.online_ms << endl;
        cout << "comm_ms=" << kc_stats.online.comm_ms << endl;
        cout << "sent_bytes=" << outer_delta.sent << endl;
        cout << "transport_actions=" << outer_actions << endl;
        cout << "logical_rounds=" << protocol_stats.logical_rounds << endl;
        cout << "dcf_evaluate_calls="
             << kc_stats.online.dcf_evaluate_calls << endl;
        cout << "dcf_evaluate_ms="
             << kc_stats.online.dcf_evaluate_ms << endl;
        cout << "distance_ms=" << kc_stats.distance.online_ms << endl;
        cout << "distance_comm_ms=" << kc_stats.distance.comm_ms << endl;
        cout << "distance_sent_bytes=" << kc_stats.distance.sent_bytes << endl;
        cout << "distance_transport_actions="
             << kc_stats.distance.transport_rounds << endl;
        cout << "distance_logical_rounds="
             << kc_stats.distance.logical_rounds << endl;
        cout << "distance_dcf_evaluate_calls="
             << kc_stats.distance.dcf_evaluate_calls << endl;
        cout << "topk_ms=" << kc_stats.topk.online_ms << endl;
        cout << "topk_comm_ms=" << kc_stats.topk.comm_ms << endl;
        cout << "topk_sent_bytes=" << kc_stats.topk.sent_bytes << endl;
        cout << "topk_transport_actions="
             << kc_stats.topk.transport_rounds << endl;
        cout << "topk_logical_rounds="
             << kc_stats.topk.logical_rounds << endl;
        cout << "topk_dcf_evaluate_calls="
             << kc_stats.topk.dcf_evaluate_calls << endl;
        cout << "vote_ms=" << kc_stats.vote.online_ms << endl;
        cout << "vote_comm_ms=" << kc_stats.vote.comm_ms << endl;
        cout << "vote_sent_bytes=" << kc_stats.vote.sent_bytes << endl;
        cout << "vote_transport_actions="
             << kc_stats.vote.transport_rounds << endl;
        cout << "vote_logical_rounds="
             << kc_stats.vote.logical_rounds << endl;
        cout << "vote_dcf_evaluate_calls="
             << kc_stats.vote.dcf_evaluate_calls << endl;
        cout << "load_ms=" << elapsed_ms(load_start, load_end) << endl;
        cout << "share_material_setup_ms="
             << elapsed_ms(setup_start, setup_end) << endl;
        cout << "dcf_key_init_ms="
             << elapsed_ms(dcf_init_start, dcf_init_end) << endl;
        cout << "result=" << (ok ? "PASS" : "FAIL") << endl;
    }

    return ok ? 0 : 1;
}
}

namespace secknn_engine {
using namespace secknn_native;
// Reuse the ORIGINAL SecKNN implementation, including its scalar comparison,
// swap, frequency logic, zero auxiliary values and per-evaluation key-file I/O.
// Compile this as a separate translation unit; secknn.x keeps its old entry.

namespace {
using Clock = std::chrono::steady_clock;
struct Snapshot {
    NamedCommStats comm;
    long long calls;
    double eval_ms;
    Clock::time_point time;
};
Snapshot snap() {
    Snapshot s;
    s.comm = player->total_comm(); s.calls = call_evaluate_time;
    s.eval_ms = total_duration.count() * 1000; s.time = Clock::now();
    return s;
}
uint64_t actions(const NamedCommStats& c) {
    uint64_t r=0; for (const auto& e:c) r+=e.second.rounds; return r;
}
void phase(const char* name, const Snapshot& a, const Snapshot& b, uint64_t rounds, int checkpoint=0) {
    const auto c=b.comm-a.comm;
    if (actions(c)%2) throw runtime_error("unpaired communication");
    cout << "BENCH_PHASE phase=" << name << " party=" << playerno << " trial=1"
         << " online_ms=" << std::chrono::duration<double,std::milli>(b.time-a.time).count()
         << " sent_bytes=" << c.sent << " transport_actions=" << actions(c)
         << " exchange_rounds=" << actions(c)/2 << " theoretical_rounds=" << rounds
         << " dcf_evaluate_calls=" << b.calls-a.calls
         << " dcf_evaluate_ms=" << b.eval_ms-a.eval_ms;
    if(checkpoint)cout<<" k="<<checkpoint;
    cout<<'\n';
}
void barrier() {
    octetStream a,b; Z2<K>(0).pack(a);
    if(playerno==0){player->send(a);player->receive(b);}
    else{player->receive(b);player->send(a);}
}
vector<uint64_t> open_for_validation(const vector<Z2<K>>& local) {
    octetStream a,b; for(const auto& v:local)v.pack(a);
    player->send(a);player->receive(b);
    vector<uint64_t> out(local.size());
    for(size_t i=0;i<out.size();++i){Z2<K> peer;peer.unpack(b);out[i]=(local[i]+peer).get_limb(0);}
    return out;
}
Z2<K> test_share(uint64_t value, size_t i) {
    const Z2<K> mask((mp_limb_t)(7001+37*i));
    return playerno==0 ? mask : Z2<K>((mp_limb_t)value)-mask;
}
vector<int> parse_ks(const string& text) {
    vector<int> out;std::istringstream in(text);string token;
    while(std::getline(in,token,',')){
        size_t used=0;int k=std::stoi(token,&used);
        if(used!=token.size()||k<1||std::find(out.begin(),out.end(),k)!=out.end())throw runtime_error("invalid checkpoint list");
        out.push_back(k);
    }
    if(out.empty())throw runtime_error("empty checkpoint list");
    return out;
}
uint64_t network_layers(size_t size) {
    // Native SecKNN scans (i,last) sequentially. These comparisons share the
    // last wire and cannot be combined into independent tournament layers.
    return size ? size-1 : 0;
}
uint64_t topk_layers(size_t n,size_t k) {
    uint64_t result=0;for(size_t i=0;i<k;++i)result+=network_layers(n-i);return result;
}
void plain_top_one(vector<array<uint64_t,2>>& rows,int size,bool minimum) {
    for(int i=0;i<size-1;++i)
        if(minimum?rows[i][0]<rows[size-1][0]:rows[i][0]>rows[size-1][0])std::swap(rows[i],rows[size-1]);
}
uint64_t plain_winner(const vector<array<uint64_t,2>>& reference, int k) {
    vector<array<uint64_t,2>> frequency(k);
    for(int i=0;i<k;++i){
        const auto label=reference[reference.size()-1-i][1];uint64_t count=0;
        for(int j=0;j<k;++j)count+=label==reference[reference.size()-1-j][1];
        frequency[i]={count,label};
    }
    plain_top_one(frequency,k,false);
    return frequency[k-1][1];
}
}

int run(int argc,const char** argv) {
    opt.add("5",0,1,0,"Neighbors","-k","--k");
    opt.add("/usr/src/Garnet/ConstantPP/data/arcene-data",0,1,0,"Dataset path","--dataset");
    opt.add("topk-vote",0,1,0,"distance, topk-vote or checkpoint-vote","--scope");
    opt.add("UNVERIFIED",0,1,0,"Network label; actual snapshot in wrapper","--network-profile");
    opt.add("1,2,3,5,20,100",0,1,0,"Cumulative Top-k checkpoints","--checkpoints");
    opt.add("",0,1,0,"Vote order; permutation of checkpoints","--vote-order");
    opt.add("scalar",0,1,0,"Native serial scan only; historical layered changes the algorithm and is rejected","--topk-mode");
    parse_argv(argc,argv);
    reject_unknown_options(opt);
    opt.get("--k")->getInt(k_const);
    string path,scope,network,topk_mode;
    opt.get("--topk-mode")->getString(topk_mode);
    if(topk_mode!="scalar")throw runtime_error("SecKNN requires --topk-mode scalar: layered/tournament changes the original algorithm and is disabled");
    opt.get("--dataset")->getString(path);opt.get("--scope")->getString(scope);
    opt.get("--network-profile")->getString(network);

    const auto setup_start=Clock::now();
    const auto data=ConstantPP::load_constantpp_dataset(path);
    const size_t n=data.database.size(), d=data.num_features;
    vector<int> checkpoints,vote_order;
    if(scope=="checkpoint-vote"){
        string text,order;opt.get("--checkpoints")->getString(text);opt.get("--vote-order")->getString(order);
        checkpoints=parse_ks(text);std::sort(checkpoints.begin(),checkpoints.end());
        vote_order=order.empty()?checkpoints:parse_ks(order);
        auto sorted=vote_order;std::sort(sorted.begin(),sorted.end());
        if(sorted!=checkpoints||checkpoints.back()!=k_const)throw runtime_error("checkpoints must end at max k; vote order must be their permutation");
    }
    if(k_const<1 || size_t(k_const)>n || (scope!="distance"&&scope!="full"&&scope!="topk-vote"&&scope!="checkpoint-vote"))
        throw runtime_error("invalid k/scope");
    // Plaintext reference and input preparation are outside online snapshots.
    vector<uint64_t> clear_dist(n,0);
    vector<array<uint64_t,2>> reference(n);
    for(size_t i=0;i<n;++i){
        for(size_t j=0;j<d;++j){
            const auto diff=Z2<K>((mp_limb_t)data.database[i][j])-Z2<K>((mp_limb_t)data.query[j]);
            clear_dist[i]+=(diff*diff).get_limb(0);
        }
        reference[i]={clear_dist[i],data.labels[i]};
    }
    for(int step=0;step<k_const;++step){
        plain_top_one(reference,n-step,true);
    }
    vector<uint64_t> candidate(k_const);
    vector<array<uint64_t,2>> frequencies(k_const);
    for(int i=0;i<k_const;++i)candidate[i]=reference[n-1-i][1];
    for(int i=0;i<k_const;++i){
        uint64_t count=0;for(auto label:candidate)count+=label==candidate[i];
        frequencies[i]={count,candidate[i]};
    }
    plain_top_one(frequencies,k_const,false);
    const auto expected=frequencies[k_const-1][1];
    KNN_party_SecKNN party(playerno);
    party.num_train_data=n;party.num_features=d;party.num_test_data=1;
    party.start_networking(opt);
    std::unique_ptr<RealTwoPartyPlayer> connection(party.m_player);
    // Preserve the ORIGINAL SecKNN call graph, scan order, swap and tie rules.
    // The compile-time transport adapter only overlaps independent messages.
    const auto select_one=[&](vector<array<Z2<K>,2>>& shares,int size,bool minimum) {
        party.top_1(shares,size,minimum);
    };
    party.m_ESD_vec.resize(n);
    if(scope=="distance"||scope=="full"){
        // Keep original samples for reference; prepare offline inputs locally.
        if(playerno==0)for(size_t i=0;i<n;++i){
            auto* s=new Sample(d);s->label=data.labels[i];
            for(size_t j=0;j<d;++j)s->features[j]=data.database[i][j];
            party.m_sample.push_back(s);
        }
        else{
            auto* s=new Sample(d);s->label=data.query_label;
            for(size_t j=0;j<d;++j)s->features[j]=data.query[j];
            party.m_test.push_back(s);
        }
        // Performance input preparation only: local additive shares avoid
        // repeating offline transfers over WAN. Original online ESD is unchanged.
        party.m_train_additive_share_vec.resize(n);
        party.m_test_additive_share_vec.resize(1);
        party.m_test_additive_share_vec[0].resize(d);
        party.m_train_label_additive_share_vec.resize(n);
        for(size_t i=0;i<n;++i){
            party.m_train_additive_share_vec[i].resize(d);
            for(size_t j=0;j<d;++j)
                party.m_train_additive_share_vec[i][j]=test_share(data.database[i][j],i*d+j);
            party.m_train_label_additive_share_vec[i]=test_share(data.labels[i],n*d+d+i);
            party.m_ESD_vec[i][1]=party.m_train_label_additive_share_vec[i];
        }
        for(size_t j=0;j<d;++j)
            party.m_test_additive_share_vec[0][j]=test_share(data.query[j],n*d+j);
    }else{
        // Real Arcene distance/label values in deterministic test shares.
        // This scope deliberately does not execute secure distance first.
        for(size_t i=0;i<n;++i)
            party.m_ESD_vec[i]={test_share(clear_dist[i],2*i),test_share(data.labels[i],2*i+1)};
        for(const auto& prefix:{"k","r"}) {
            std::ifstream f("Player-Data/2-fss/"+string(prefix)+std::to_string(playerno));
            if(!f.good())throw runtime_error("missing original SecKNN DCF test material");
        }
    }
    const double setup_ms=std::chrono::duration<double,std::milli>(Clock::now()-setup_start).count();
    cout<<std::fixed<<std::setprecision(9)
        <<"party="<<playerno<<"\ndataset="<<path<<"\ntrain_count="<<n<<"\nfeatures="<<d
        <<"\nquery_count=1\nk="<<k_const<<"\nnetwork_env="<<network
        <<"\nbackend=original-secknn-scalar\ntopk_mode="<<topk_mode<<"\nscope="<<scope
        <<"\nalgorithm_identity=secknn-native-serial-scan\nselection_schedule=scan-i-against-last\ntie_rule=strict-comparison-no-swap-on-equality"
        <<"\nmaterial_mode=original-zero-auxiliary-test-settings"
        <<"\ninput_preparation="<<(scope=="distance"||scope=="full"?"local-deterministic-additive-input-shares":"plaintext-arcene-distances-to-deterministic-test-shares")
        <<"\nsetup_ms="<<setup_ms<<"\ncore_exclusivity=NOT_VERIFIED\n";
    if(scope=="checkpoint-vote"){
        // Only boundary snapshots interrupt selection. Already selected suffix
        // entries are never touched by subsequent top_1 calls, so extract ALL
        // candidate vectors after max-k, without copying candidates mid-timer.
        vector<Snapshot> ends(checkpoints.size()),vote_begin(checkpoints.size()),
                frequency_ends(checkpoints.size()),vote_ends(checkpoints.size());
        vector<vector<Z2<K>>> selected(checkpoints.size());
        vector<Z2<K>> results(checkpoints.size());
        for(size_t i=0;i<checkpoints.size();++i)selected[i].resize(checkpoints[i]);
        barrier();const auto begin=snap();size_t next=0;
        for(int step=0;step<k_const;++step){
            select_one(party.m_ESD_vec,n-step,true);
            if(step+1==checkpoints[next]){
                ends[next]=snap();++next;
                if(next==checkpoints.size())break;
            }
        }
        for(size_t i=0;i<checkpoints.size();++i)
            for(int j=0;j<checkpoints[i];++j)selected[i][j]=party.m_ESD_vec[n-1-j][1];
        // Votes are independent measured calls, AFTER all Top-k work. Barriers,
        // candidate copies, validation and printing are outside vote timers.
        for(int k:vote_order){
            cout<<"VOTE_EXECUTION k="<<k<<" order_source=explicit_or_checkpoint_default\n";
            const size_t i=std::find(checkpoints.begin(),checkpoints.end(),k)-checkpoints.begin();
            k_const=k;barrier();vote_begin[i]=snap();
            party.label_compute(selected[i],party.m_shared_label_list_count_array);
            frequency_ends[i]=snap();
            select_one(party.m_shared_label_list_count_array,k,false);
            results[i]=party.m_shared_label_list_count_array[k-1][1];
            vote_ends[i]=snap();
        }
        const auto clear=open_for_validation(results);
        vector<Z2<K>> neighbor_labels;
        for(const auto& labels:selected)neighbor_labels.insert(neighbor_labels.end(),labels.begin(),labels.end());
        const auto clear_neighbors=open_for_validation(neighbor_labels);
        bool accounting=true,correct=true;
        size_t neighbor_cursor=0;
        for(size_t i=0;i<checkpoints.size();++i){
            const uint64_t k=checkpoints[i],c=k*n-k*(k+1)/2;
            const auto top=ends[i].comm-begin.comm;
            const auto frequency=frequency_ends[i].comm-vote_begin[i].comm;
            const auto maximum=vote_ends[i].comm-frequency_ends[i].comm;
            accounting=accounting&&top.sent==72*c&&actions(top)==4*topk_layers(n,k)&&uint64_t(ends[i].calls-begin.calls)==2*c&&
                frequency.sent==24*k*k&&actions(frequency)==4&&uint64_t(frequency_ends[i].calls-vote_begin[i].calls)==2*k*k&&
                maximum.sent==72*(k-1)&&actions(maximum)==4*network_layers(k)&&uint64_t(vote_ends[i].calls-frequency_ends[i].calls)==2*(k-1);
            correct=correct&&clear[i]==plain_winner(reference,k);
            for(size_t j=0;j<k;++j)
                correct=correct&&clear_neighbors[neighbor_cursor+j]==reference[n-1-j][1];
            neighbor_cursor+=k;
            phase("topk",begin,ends[i],2*topk_layers(n,k),k);
            phase("vote_frequency",vote_begin[i],frequency_ends[i],2,k);
            phase("vote_max",frequency_ends[i],vote_ends[i],2*network_layers(k),k);
            phase("vote",vote_begin[i],vote_ends[i],2+2*network_layers(k),k);
            cout<<"CHECKPOINT_RESULT k="<<k<<" revealed_label="<<clear[i]
                <<" expected_label="<<plain_winner(reference,k)<<" compare_swaps="<<c<<" network_layers="<<topk_layers(n,k)<<'\n';
        }
        cout<<"topk_execution_count=1\nmax_k="<<checkpoints.back()
            <<"\nneighbor_order_check="<<(correct?"PASS":"FAIL")
            <<"\nvote_execution_count="<<checkpoints.size()
            <<"\nmeasurement_mode=cumulative_topk_then_separate_votes"
            <<"\noutput_scope=winning_label_secret_shares_per_k\ndistance_in_scope=0"
            <<"\nknn_online_e2e_ms=NOT_IN_SCOPE\ntopk_vote_online_ms=NOT_MEASURED"
            <<"\naccounting_check="<<(accounting?"PASS":"FAIL")
            <<"\ncorrectness="<<(correct?"PASS":"FAIL")
            <<"\ntest_status="<<(accounting&&correct?"COMPLETED":"FAILED")<<endl;
        return accounting&&correct?0:1;
    }
    barrier();
    const auto start=snap();
    Snapshot selection_begin=start;
    if(scope=="full"){
        party.compute_ESD_for_one_query(0);
        selection_begin=snap();
    }
    Snapshot topk_end=selection_begin, frequency_end=selection_begin, max_end=selection_begin;
    Z2<K> result(0);
    if(scope=="distance"){
        party.compute_ESD_for_one_query(0);
    }else{
        for(int i=0;i<k_const;++i)select_one(party.m_ESD_vec,n-i,true);
        vector<Z2<K>> selected;selected.reserve(k_const);
        for(int i=0;i<k_const;++i)selected.push_back(party.m_ESD_vec[n-1-i][1]);
        topk_end=snap();
        party.label_compute(selected,party.m_shared_label_list_count_array);
        frequency_end=snap();
        select_one(party.m_shared_label_list_count_array,k_const,false);
        result=party.m_shared_label_list_count_array[k_const-1][1];
        max_end=snap();
    }
    // Independent outer end, before summaries, reconstruction or validation.
    const auto end_time=Clock::now();
    auto end=snap();end.time=end_time;
    const uint64_t comparisons=uint64_t(k_const)*n-uint64_t(k_const)*(k_const+1)/2;
    const uint64_t vote_bytes=24ULL*k_const*k_const+72ULL*(k_const-1);
    const auto delta=end.comm-start.comm;
    bool accounting;
    bool correct;
    if(scope=="distance"){
        accounting=delta.sent==8*n*d && actions(delta)==2 && end.calls==start.calls;
        vector<Z2<K>> local(n);for(size_t i=0;i<n;++i)local[i]=party.m_ESD_vec[i][0];
        correct=open_for_validation(local)==clear_dist;
        phase("distance",start,end,1);
    }else{
        accounting=(topk_end.comm-selection_begin.comm).sent==72*comparisons &&
            (max_end.comm-topk_end.comm).sent==vote_bytes &&
            actions(delta)==4*topk_layers(n,k_const)+4+4*network_layers(k_const)+2*(scope=="full") &&
            uint64_t(end.calls-start.calls)==2*(comparisons+uint64_t(k_const)*k_const+k_const-1);
        correct=open_for_validation({result})[0]==expected;
        vector<Z2<K>> selected_labels;
        for(int i=0;i<k_const;++i)selected_labels.push_back(party.m_ESD_vec[n-1-i][1]);
        const auto clear_labels=open_for_validation(selected_labels);
        for(int i=0;i<k_const;++i)correct=correct&&clear_labels[i]==reference[n-1-i][1];
        phase("topk",selection_begin,topk_end,2*topk_layers(n,k_const));
        phase("vote_frequency",topk_end,frequency_end,2);
        phase("vote_max",frequency_end,max_end,2*network_layers(k_const));
        phase("vote",topk_end,max_end,2+2*network_layers(k_const));
        phase("online_other",max_end,end,0);
        if(scope=="full"){
            phase("distance",start,selection_begin,1);
            phase("knn_online",start,end,1+2*topk_layers(n,k_const)+2+2*network_layers(k_const));
        }
        phase("topk_vote_online",selection_begin,end,2*topk_layers(n,k_const)+2+2*network_layers(k_const));
    }
    cout<<"topk_compare_swaps="<<(scope=="distance"?0:comparisons)
        <<"\nneighbor_order_check="<<(scope=="distance"?"NOT_IN_SCOPE":(correct?"PASS":"FAIL"))
        <<"\nvote_frequency_compare_pairs="<<(scope=="distance"?0:k_const*k_const)
        <<"\nfull_knn_measured="<<(scope=="full")<<"\noutput_scope="<<(scope=="distance"?"distance_secret_shares":"winning_label_secret_share")
        <<"\ndistance_in_scope="<<(scope=="distance"||scope=="full")<<"\nknn_online_e2e_ms="<<(scope=="full"?"SEE_BENCH_PHASE":"NOT_IN_SCOPE")
        <<"\naccounting_check="<<(accounting?"PASS":"FAIL")
        <<"\ncorrectness="<<(correct?"PASS":"FAIL")
        <<"\ntest_status="<<(accounting&&correct?"COMPLETED":"FAILED")<<endl;
    for(auto* s:party.m_sample)delete s;
    for(auto* s:party.m_test)delete s;
    return accounting&&correct?0:1;
}
}

namespace kona_engine {
using namespace kona_native;
// Reuse the ORIGINAL Kona implementation, including its per-layer vector comparison,
// swap, frequency logic, zero auxiliary values and per-evaluation key-file I/O.
// Compile this as a separate translation unit; kona-upstream.x keeps its old entry.

namespace {
using Clock = std::chrono::steady_clock;
// Same original KONA tournament/leftover graph and pairwise frequency logic,
// using the repository cached batch DCF and chunked batch swap/multiplication.
// Reference: kona-mainpath-esd-topk-dcf-bench.cpp prepare_top_1/execute_topk.
// This opt-in batched baseline never replaces the native baseline silently.
KonaDcfCompare::Compare64<K>* cached_dcf=nullptr;
template<class Dataset>
void prepare_local_aby2_inputs(KNN_party_optimized& party,const Dataset& data){
    const size_t n=data.database.size(),d=data.num_features;
            party.m_train_aby2_share_vec.resize(n);
            party.m_test_aby2_share_vec.resize(1);
            party.m_test_aby2_share_vec[0].resize(d);
            for(size_t i=0;i<n;++i){
                party.m_train_aby2_share_vec[i].resize(d);
                for(size_t j=0;j<d;++j){
                    party.m_train_aby2_share_vec[i][j][0]=Z2<K>((mp_limb_t)data.database[i][j]);
                    party.m_train_aby2_share_vec[i][j][1]=Z2<K>(0);
                }
            }
            for(size_t j=0;j<d;++j){
                party.m_test_aby2_share_vec[0][j][0]=Z2<K>((mp_limb_t)data.query[j]);
                party.m_test_aby2_share_vec[0][j][1]=Z2<K>(0);
            }
}
struct CachedLevel {vector<int> idx;vector<Z2<K>> bits;};
using CachedPlan=vector<CachedLevel>;
CachedPlan prepare_cached_top_one(int size){
    vector<int> idx(size);iota(idx.begin(),idx.end(),0);int leftover=-1;CachedPlan plan;
    while(idx.size()+(leftover!=-1)>1){
        if(idx.size()%2){if(leftover==-1){leftover=idx.back();idx.pop_back();}else{idx.push_back(leftover);leftover=-1;}}
        plan.push_back({idx,vector<Z2<K>>(idx.size())});vector<int> next;
        for(size_t i=1;i<idx.size();i+=2)next.push_back(idx[i]);
        idx.swap(next);
    }
    return plan;
}
uint64_t cached_rounds(const CachedPlan& plan){
    uint64_t total=0;for(const auto& l:plan){size_t pairs=l.idx.size()/2;total+=KonaBenchConfig::chunks(pairs,KonaBenchConfig::dcf_chunk())+KonaBenchConfig::chunks(2*pairs,KonaBenchConfig::mul_chunk());}return total;
}
void cached_top_one(vector<array<Z2<K>,2>>& rows,CachedPlan& plan,bool minimum){
    for(auto& l:plan){cached_dcf->compare_in_vec(rows,l.idx,l.bits,!minimum);KonaCongKonaAdapter::ss_vec_kona_l2<K>(rows,l.idx,l.bits,player);}
}
void cached_label_counts(const vector<Z2<K>>& labels,vector<array<Z2<K>,2>>& counts){
    const size_t k=labels.size();vector<int> idx;idx.reserve(2*k*k);
    for(size_t i=0;i<k;++i)for(size_t j=0;j<k;++j){idx.push_back(i);idx.push_back(j);}
    vector<Z2<K>> bits(idx.size()),left(k*k),right(k*k),equal;
    cached_dcf->compare_in_vec(labels,idx,bits,true);
    const Z2<K> one(static_cast<mp_limb_t>(playerno));
    for(size_t i=0;i<k;++i)for(size_t j=0;j<k;++j){left[i*k+j]=one-bits[2*(i*k+j)];right[i*k+j]=one-bits[2*(j*k+i)];}
    KonaShareConversion::mul_vector_additive_kona_l2_chunked<K>(left,right,equal,player);
    counts.assign(k,array<Z2<K>,2>{Z2<K>(0),Z2<K>(0)});
    for(size_t i=0;i<k;++i){for(size_t j=0;j<k;++j)counts[i][0]+=equal[i*k+j];counts[i][1]=labels[i];}
}

struct Snapshot {
    NamedCommStats comm;
    long long calls;
    double eval_ms;
    Clock::time_point time;
};
Snapshot snap() {
    Snapshot s;
    s.comm = player->total_comm(); s.calls = cached_dcf?cached_dcf->get_stats().evaluate_calls:call_evaluate_time;
    s.eval_ms = cached_dcf?cached_dcf->get_stats().evaluate_time.count()*1000:total_duration.count()*1000; s.time = Clock::now();
    return s;
}
uint64_t actions(const NamedCommStats& c) {
    uint64_t r=0; for (const auto& e:c) r+=e.second.rounds; return r;
}
void phase(const char* name, const Snapshot& a, const Snapshot& b, uint64_t rounds, int checkpoint=0) {
    const auto c=b.comm-a.comm;
    if (actions(c)%2) throw runtime_error("unpaired communication");
    cout << "BENCH_PHASE phase=" << name << " party=" << playerno << " trial=1"
         << " online_ms=" << std::chrono::duration<double,std::milli>(b.time-a.time).count()
         << " sent_bytes=" << c.sent << " transport_actions=" << actions(c)
         << " exchange_rounds=" << actions(c)/2 << " theoretical_rounds=" << rounds
         << " dcf_evaluate_calls=" << b.calls-a.calls
         << " dcf_evaluate_ms=" << b.eval_ms-a.eval_ms;
    if(checkpoint)cout<<" k="<<checkpoint;
    cout<<'\n';
}
void barrier() {
    octetStream a,b; Z2<K>(0).pack(a);
    if(playerno==0){player->send(a);player->receive(b);}
    else{player->receive(b);player->send(a);}
}
vector<uint64_t> open_for_validation(const vector<Z2<K>>& local) {
    octetStream a,b; for(const auto& v:local)v.pack(a);
    player->send(a);player->receive(b);
    vector<uint64_t> out(local.size());
    for(size_t i=0;i<out.size();++i){Z2<K> peer;peer.unpack(b);out[i]=(local[i]+peer).get_limb(0);}
    return out;
}
Z2<K> test_share(uint64_t value, size_t i) {
    const Z2<K> mask((mp_limb_t)(7001+37*i));
    return playerno==0 ? mask : Z2<K>((mp_limb_t)value)-mask;
}
vector<int> parse_ks(const string& text) {
    vector<int> out;std::istringstream in(text);string token;
    while(std::getline(in,token,',')){
        size_t used=0;int k=std::stoi(token,&used);
        if(used!=token.size()||k<1||std::find(out.begin(),out.end(),k)!=out.end())throw runtime_error("invalid checkpoint list");
        out.push_back(k);
    }
    if(out.empty())throw runtime_error("empty checkpoint list");
    return out;
}
uint64_t network_layers(size_t size) {
    uint64_t depth=0;size_t width=1;
    while(width<size){width*=2;++depth;}
    return depth;
}
uint64_t topk_layers(size_t n,size_t k) {
    uint64_t result=0;for(size_t i=0;i<k;++i)result+=network_layers(n-i);return result;
}
void plain_top_one(vector<array<uint64_t,2>>& rows,int size,bool minimum) {
    vector<int> indices;for(int i=0;i<size;++i)indices.push_back(i);
    int leftover=-1;
    while(indices.size()+(leftover!=-1)>1){
        if(indices.size()%2){
            if(leftover==-1){leftover=indices.back();indices.pop_back();}
            else{indices.push_back(leftover);leftover=-1;}
        }
        vector<int> next;
        for(size_t i=0;i<indices.size();i+=2){
            auto& a=rows[indices[i]];auto& b=rows[indices[i+1]];
            if(minimum?a[0]<b[0]:a[0]>b[0])std::swap(a,b);
            next.push_back(indices[i+1]);
        }
        indices=std::move(next);
    }
}
uint64_t plain_winner(const vector<array<uint64_t,2>>& reference, int k) {
    vector<array<uint64_t,2>> frequency(k);
    for(int i=0;i<k;++i){
        const auto label=reference[reference.size()-1-i][1];uint64_t count=0;
        for(int j=0;j<k;++j)count+=label==reference[reference.size()-1-j][1];
        frequency[i]={count,label};
    }
    plain_top_one(frequency,k,false);
    return frequency[k-1][1];
}
}

int run(int argc,const char** argv) {
    opt.add("5",0,1,0,"Neighbors","-k","--k");
    opt.add("/usr/src/Garnet/ConstantPP/data/arcene-data",0,1,0,"Dataset path","--dataset");
    opt.add("topk-vote",0,1,0,"distance, topk-vote or checkpoint-vote","--scope");
    opt.add("UNVERIFIED",0,1,0,"Network label; actual snapshot in wrapper","--network-profile");
    opt.add("1,2,3,5,20,100",0,1,0,"Cumulative Top-k checkpoints","--checkpoints");
    opt.add("",0,1,0,"Vote order; permutation of checkpoints","--vote-order");
    opt.add("original",0,1,0,"original or historical cached batched-dcf components","--kona-backend");
    parse_argv(argc,argv);
    reject_unknown_options(opt);
    opt.get("--k")->getInt(k_const);
    string path,scope,network;
    opt.get("--dataset")->getString(path);opt.get("--scope")->getString(scope);
    opt.get("--network-profile")->getString(network);

    string implementation;opt.get("--kona-backend")->getString(implementation);
    const bool optimized=implementation=="batched-dcf";
    if(implementation!="original"&&!optimized)throw runtime_error("invalid KONA backend");
    if(scope=="topk-only"&&!optimized)throw runtime_error("KONA topk-only requires explicit batched-dcf backend");
    const auto setup_start=Clock::now();
    const auto data=ConstantPP::load_constantpp_dataset(path);
    const size_t n=data.database.size(), d=data.num_features;
    vector<int> checkpoints,vote_order;
    if(scope=="checkpoint-vote"){
        string text,order;opt.get("--checkpoints")->getString(text);opt.get("--vote-order")->getString(order);
        checkpoints=parse_ks(text);std::sort(checkpoints.begin(),checkpoints.end());
        vote_order=order.empty()?checkpoints:parse_ks(order);
        auto sorted=vote_order;std::sort(sorted.begin(),sorted.end());
        if(sorted!=checkpoints||checkpoints.back()!=k_const)throw runtime_error("checkpoints must end at max k; vote order must be their permutation");
    }
    if(k_const<1 || size_t(k_const)>n || (scope!="distance"&&scope!="full"&&scope!="topk-vote"&&scope!="checkpoint-vote"&&scope!="topk-only"))
        throw runtime_error("invalid k/scope");
    // Plaintext reference and input preparation are outside online snapshots.
    vector<uint64_t> clear_dist(n,0);
    vector<array<uint64_t,2>> reference(n);
    for(size_t i=0;i<n;++i){
        for(size_t j=0;j<d;++j){
            const auto diff=Z2<K>((mp_limb_t)data.database[i][j])-Z2<K>((mp_limb_t)data.query[j]);
            clear_dist[i]+=(diff*diff).get_limb(0);
        }
        reference[i]={clear_dist[i],data.labels[i]};
    }
    for(int step=0;step<k_const;++step){
        plain_top_one(reference,n-step,true);
    }
    vector<uint64_t> candidate(k_const);
    vector<array<uint64_t,2>> frequencies(k_const);
    for(int i=0;i<k_const;++i)candidate[i]=reference[n-1-i][1];
    for(int i=0;i<k_const;++i){
        uint64_t count=0;for(auto label:candidate)count+=label==candidate[i];
        frequencies[i]={count,candidate[i]};
    }
    plain_top_one(frequencies,k_const,false);
    const auto expected=frequencies[k_const-1][1];
    KNN_party_optimized party(playerno);
    party.num_train_data=n;party.num_features=d;party.num_test_data=1;
    party.start_networking(opt);
    std::unique_ptr<RealTwoPartyPlayer> connection(party.m_player);
    party.m_ESD_vec.resize(n);
    if(scope=="distance"||scope=="full"){
        // Reuse the original real input-sharing path outside online timing.
        if(playerno==0)for(size_t i=0;i<n;++i){
            auto* s=new Sample(d);s->label=data.labels[i];
            for(size_t j=0;j<d;++j)s->features[j]=data.database[i][j];
            party.m_sample.push_back(s);
        }
        else{
            auto* s=new Sample(d);s->label=data.query_label;
            for(size_t j=0;j<d;++j)s->features[j]=data.query[j];
            party.m_test.push_back(s);
        }
        party.fake_load_triples();
        if(optimized){
            prepare_local_aby2_inputs(party,data);
        }else party.aby2_share_data_and_additive_share_label_list();
    }else{
        // Real Arcene distance/label values in deterministic test shares.
        // This scope deliberately does not execute secure distance first.
        for(size_t i=0;i<n;++i)
            party.m_ESD_vec[i]={test_share(clear_dist[i],2*i),test_share(data.labels[i],2*i+1)};
        for(const auto& prefix:{"k","r"}) {
            std::ifstream f("Player-Data/2-fss/"+string(prefix)+std::to_string(playerno));
            if(!f.good())throw runtime_error("missing original Kona DCF test material");
        }
    }
    unique_ptr<KonaDcfCompare::Compare64<K>> optimized_backend;
    vector<CachedPlan> topk_plans,checkpoint_vote_plans;CachedPlan vote_plan;
    uint64_t selected_rounds=2*topk_layers(n,k_const),frequency_rounds=2,max_rounds=2*network_layers(k_const);
    if(optimized){
        optimized_backend.reset(new KonaDcfCompare::Compare64<K>(party.m_player,playerno));cached_dcf=optimized_backend.get();
        selected_rounds=0;
        for(int i=0;i<k_const;++i){topk_plans.push_back(prepare_cached_top_one(n-i));selected_rounds+=cached_rounds(topk_plans.back());}
        for(int checkpoint:checkpoints)checkpoint_vote_plans.push_back(prepare_cached_top_one(checkpoint));
        vote_plan=prepare_cached_top_one(k_const);max_rounds=cached_rounds(vote_plan);
        frequency_rounds=KonaBenchConfig::chunks(uint64_t(k_const)*k_const,KonaBenchConfig::dcf_chunk())+KonaBenchConfig::chunks(uint64_t(k_const)*k_const,KonaBenchConfig::mul_chunk());
    }
    const double setup_ms=std::chrono::duration<double,std::milli>(Clock::now()-setup_start).count();
    cout<<std::fixed<<std::setprecision(9)
        <<"party="<<playerno<<"\ndataset="<<path<<"\ntrain_count="<<n<<"\nfeatures="<<d
        <<"\nquery_count=1\nk="<<k_const<<"\nnetwork_env="<<network
        <<"\nbackend="<<(optimized?"kona-batched-cached-dcf":"original-kona")<<"\nkona_backend="<<implementation<<"\nscope="<<scope
        <<"\nmaterial_mode=original-zero-auxiliary-test-settings"
        <<"\ninput_preparation="<<(scope=="distance"||scope=="full"?(optimized?"local-zero-auxiliary-aby2-inputs":"original-real-sharing"):"plaintext-dataset-distances-to-deterministic-test-shares")
        <<"\nsetup_ms="<<setup_ms<<"\ncore_exclusivity=NOT_VERIFIED\n";
    if(scope=="topk-only"){
        // Ready distance/label shares and cached network plans. Measure only
        // the original repeated Top-1 graph using cached DCF and Kona swap.
        barrier();const auto begin=snap();
        for(int i=0;i<k_const;++i)cached_top_one(party.m_ESD_vec,topk_plans[i],true);
        const auto end=snap();
        const auto delta=end.comm-begin.comm;
        const uint64_t comparisons=uint64_t(k_const)*n-uint64_t(k_const)*(k_const+1)/2;
        uint64_t compare_chunks=0,swap_chunks=0,depth=0;
        for(const auto& plan:topk_plans)for(const auto& level:plan){
            ++depth;compare_chunks+=KonaBenchConfig::chunks(level.idx.size()/2,KonaBenchConfig::dcf_chunk());
            swap_chunks+=KonaBenchConfig::chunks(level.idx.size(),KonaBenchConfig::mul_chunk());
        }
        const uint64_t logical_rounds=2*depth;
        const bool accounting=delta.sent==72*comparisons &&
            actions(delta)==2*selected_rounds && selected_rounds==compare_chunks+swap_chunks &&
            uint64_t(end.calls-begin.calls)==2*comparisons;
        // Candidate extraction and reconstruction are outside online timing.
        vector<Z2<K>> selected;
        for(int i=0;i<k_const;++i){selected.push_back(party.m_ESD_vec[n-1-i][0]);selected.push_back(party.m_ESD_vec[n-1-i][1]);}
        const auto clear=open_for_validation(selected);bool correct=true;
        for(int i=0;i<k_const;++i)correct=correct && clear[2*i]==reference[n-1-i][0] && clear[2*i+1]==reference[n-1-i][1];
        phase("topk",begin,end,logical_rounds);
        cout<<"measurement_mode=topk_only_shares_ready_to_selected_shares_ready"
            <<"\ntopk_share_check="<<(correct?"PASS":"FAIL")
            <<"\ntopk_compare_swaps="<<comparisons<<"\nnetwork_layers="<<depth
            <<"\ncompare_chunk_size="<<KonaBenchConfig::dcf_chunk()<<"\nswap_chunk_size="<<KonaBenchConfig::mul_chunk()
            <<"\ntopk_compare_chunks="<<compare_chunks<<"\ntopk_swap_chunks="<<swap_chunks
            <<"\ntopk_expected_exchange_rounds="<<selected_rounds
            <<"\ntopk_theoretical_logical_rounds="<<logical_rounds
            <<"\ndistance_in_scope=0\nvote_in_scope=0\nvote_execution_count=0"
            <<"\nfull_knn_measured=0\nknn_online_e2e_ms=NOT_IN_SCOPE"
            <<"\noutput_scope=topk_distance_label_secret_shares"
            <<"\naccounting_check="<<(accounting?"PASS":"FAIL")
            <<"\ncorrectness="<<(correct?"PASS":"FAIL")
            <<"\ntest_status="<<(accounting&&correct?"COMPLETED":"FAILED")<<endl;
        cached_dcf=nullptr;return accounting&&correct?0:1;
    }
    if(scope=="checkpoint-vote"){
        // Only boundary snapshots interrupt selection. Already selected suffix
        // entries are never touched by subsequent top_1 calls, so extract ALL
        // candidate vectors after max-k, without copying candidates mid-timer.
        vector<Snapshot> ends(checkpoints.size()),vote_begin(checkpoints.size()),
                frequency_ends(checkpoints.size()),vote_ends(checkpoints.size());
        vector<vector<Z2<K>>> selected(checkpoints.size());
        vector<Z2<K>> results(checkpoints.size());
        for(size_t i=0;i<checkpoints.size();++i)selected[i].resize(checkpoints[i]);
        barrier();const auto begin=snap();size_t next=0;
        for(int step=0;step<k_const;++step){
            if(optimized)cached_top_one(party.m_ESD_vec,topk_plans[step],true);else party.top_1(party.m_ESD_vec,n-step,true);
            if(step+1==checkpoints[next]){
                ends[next]=snap();++next;
                if(next==checkpoints.size())break;
            }
        }
        for(size_t i=0;i<checkpoints.size();++i)
            for(int j=0;j<checkpoints[i];++j)selected[i][j]=party.m_ESD_vec[n-1-j][1];
        // Votes are independent measured calls, AFTER all Top-k work. Barriers,
        // candidate copies, validation and printing are outside vote timers.
        for(int k:vote_order){
            cout<<"VOTE_EXECUTION k="<<k<<" order_source=explicit_or_checkpoint_default\n";
            const size_t i=std::find(checkpoints.begin(),checkpoints.end(),k)-checkpoints.begin();
            k_const=k;barrier();vote_begin[i]=snap();
            if(optimized)cached_label_counts(selected[i],party.m_shared_label_list_count_array);else party.label_compute(selected[i],party.m_shared_label_list_count_array);
            frequency_ends[i]=snap();
            if(optimized)cached_top_one(party.m_shared_label_list_count_array,checkpoint_vote_plans[i],false);else party.top_1(party.m_shared_label_list_count_array,k,false);
            results[i]=party.m_shared_label_list_count_array[k-1][1];
            vote_ends[i]=snap();
        }
        const auto clear=open_for_validation(results);
        bool accounting=true,correct=true;
        for(size_t i=0;i<checkpoints.size();++i){
            const uint64_t k=checkpoints[i],c=k*n-k*(k+1)/2;
            const auto top=ends[i].comm-begin.comm;
            const auto frequency=frequency_ends[i].comm-vote_begin[i].comm;
            const auto maximum=vote_ends[i].comm-frequency_ends[i].comm;
            uint64_t top_exchanges=2*topk_layers(n,k);
            uint64_t freq_exchanges=2,max_exchanges=2*network_layers(k);
            if(optimized){
                top_exchanges=0;
                for(size_t j=0;j<k;++j)top_exchanges+=cached_rounds(topk_plans[j]);
                freq_exchanges=KonaBenchConfig::chunks(k*k,KonaBenchConfig::dcf_chunk())+KonaBenchConfig::chunks(k*k,KonaBenchConfig::mul_chunk());
                max_exchanges=cached_rounds(checkpoint_vote_plans[i]);
            }
            accounting=accounting&&top.sent==72*c&&actions(top)==2*top_exchanges&&uint64_t(ends[i].calls-begin.calls)==2*c&&
                frequency.sent==24*k*k&&actions(frequency)==2*freq_exchanges&&uint64_t(frequency_ends[i].calls-vote_begin[i].calls)==2*k*k&&
                maximum.sent==72*(k-1)&&actions(maximum)==2*max_exchanges&&uint64_t(vote_ends[i].calls-frequency_ends[i].calls)==2*(k-1);
            correct=correct&&clear[i]==plain_winner(reference,k);
            phase("topk",begin,ends[i],2*topk_layers(n,k),k);
            phase("vote_frequency",vote_begin[i],frequency_ends[i],2,k);
            phase("vote_max",frequency_ends[i],vote_ends[i],2*network_layers(k),k);
            phase("vote",vote_begin[i],vote_ends[i],2+2*network_layers(k),k);
            cout<<"CHECKPOINT_RESULT k="<<k<<" revealed_label="<<clear[i]
                <<" expected_label="<<plain_winner(reference,k)<<" compare_swaps="<<c<<" network_layers="<<topk_layers(n,k)<<'\n';
        }
        cout<<"topk_execution_count=1\nmax_k="<<checkpoints.back()
            <<"\nvote_execution_count="<<checkpoints.size()
            <<"\nmeasurement_mode=cumulative_topk_then_separate_votes"
            <<"\noutput_scope=winning_label_secret_shares_per_k\ndistance_in_scope=0"
            <<"\nknn_online_e2e_ms=NOT_IN_SCOPE\ntopk_vote_online_ms=NOT_MEASURED"
            <<"\naccounting_check="<<(accounting?"PASS":"FAIL")
            <<"\ncorrectness="<<(correct?"PASS":"FAIL")
            <<"\ntest_status="<<(accounting&&correct?"COMPLETED":"FAILED")<<endl;
        return accounting&&correct?0:1;
    }
    barrier();
    const auto start=snap();
    Snapshot selection_begin=start;
    if(scope=="full"){
        party.compute_ESD_for_one_query(0);
        selection_begin=snap();
    }
    Snapshot topk_end=selection_begin, frequency_end=selection_begin, max_end=selection_begin;
    Z2<K> result(0);
    if(scope=="distance"){
        party.compute_ESD_for_one_query(0);
    }else{
        for(int i=0;i<k_const;++i){if(optimized)cached_top_one(party.m_ESD_vec,topk_plans[i],true);else party.top_1(party.m_ESD_vec,n-i,true);}
        vector<Z2<K>> selected;selected.reserve(k_const);
        for(int i=0;i<k_const;++i)selected.push_back(party.m_ESD_vec[n-1-i][1]);
        topk_end=snap();
        if(optimized)cached_label_counts(selected,party.m_shared_label_list_count_array);else party.label_compute(selected,party.m_shared_label_list_count_array);
        frequency_end=snap();
        if(optimized)cached_top_one(party.m_shared_label_list_count_array,vote_plan,false);else party.top_1(party.m_shared_label_list_count_array,k_const,false);
        result=party.m_shared_label_list_count_array[k_const-1][1];
        max_end=snap();
    }
    // Independent outer end, before summaries, reconstruction or validation.
    const auto end_time=Clock::now();
    auto end=snap();end.time=end_time;
    const uint64_t comparisons=uint64_t(k_const)*n-uint64_t(k_const)*(k_const+1)/2;
    const uint64_t vote_bytes=24ULL*k_const*k_const+72ULL*(k_const-1);
    const auto delta=end.comm-start.comm;
    bool accounting;
    bool correct;
    if(scope=="distance"){
        accounting=delta.sent==0 && actions(delta)==0 && end.calls==start.calls;
        vector<Z2<K>> local(n);for(size_t i=0;i<n;++i)local[i]=party.m_ESD_vec[i][0];
        correct=open_for_validation(local)==clear_dist;
        phase("distance",start,end,0);
    }else{
        accounting=(topk_end.comm-selection_begin.comm).sent==72*comparisons &&
            (max_end.comm-topk_end.comm).sent==vote_bytes &&
            actions(delta)==2*(selected_rounds+frequency_rounds+max_rounds) &&
            uint64_t(end.calls-start.calls)==2*(comparisons+uint64_t(k_const)*k_const+k_const-1);
        correct=open_for_validation({result})[0]==expected;
        if(optimized){
            vector<Z2<K>> selected_check;
            for(int i=0;i<k_const;++i){selected_check.push_back(party.m_ESD_vec[n-1-i][0]);selected_check.push_back(party.m_ESD_vec[n-1-i][1]);}
            const auto clear=open_for_validation(selected_check);bool topk_ok=true;
            for(int i=0;i<k_const;++i)topk_ok=topk_ok&&clear[2*i]==reference[n-1-i][0]&&clear[2*i+1]==reference[n-1-i][1];
            correct=correct&&topk_ok;cout<<"topk_share_check="<<(topk_ok?"PASS":"FAIL")<<'\n';
        }
        phase("topk",selection_begin,topk_end,2*topk_layers(n,k_const));
        phase("vote_frequency",topk_end,frequency_end,2);
        phase("vote_max",frequency_end,max_end,2*network_layers(k_const));
        phase("vote",topk_end,max_end,2+2*network_layers(k_const));
        phase("online_other",max_end,end,0);
        if(scope=="full"){
            phase("distance",start,selection_begin,0);
            phase("knn_online",start,end,2*topk_layers(n,k_const)+2+2*network_layers(k_const));
        }
        phase("topk_vote_online",selection_begin,end,2*topk_layers(n,k_const)+2+2*network_layers(k_const));
    }
    cout<<"topk_compare_swaps="<<(scope=="distance"?0:comparisons)
        <<"\nvote_frequency_compare_pairs="<<(scope=="distance"?0:k_const*k_const)
        <<"\nfull_knn_measured="<<(scope=="full")<<"\noutput_scope="<<(scope=="distance"?"distance_secret_shares":"winning_label_secret_share")
        <<"\ndistance_in_scope="<<(scope=="distance"||scope=="full")<<"\nknn_online_e2e_ms="<<(scope=="full"?"SEE_BENCH_PHASE":"NOT_IN_SCOPE")
        <<"\naccounting_check="<<(accounting?"PASS":"FAIL")
        <<"\ncorrectness="<<(correct?"PASS":"FAIL")
        <<"\ntest_status="<<(accounting&&correct?"COMPLETED":"FAILED")<<endl;
    for(auto* s:party.m_sample)delete s;
    for(auto* s:party.m_test)delete s;
    cached_dcf=nullptr;
    return accounting&&correct?0:1;
}
}

namespace matrix_engine {



using namespace std;

namespace
{

const int K = 64;
int playerno = 0;
ez::ezOptionParser opt;

template<int KK>
using SharePair = std::array<Z2<KK>, 2>;

struct PhaseStats
{
    double total_seconds = 0.0;
    double comm_seconds = 0.0;
    size_t sent_bytes = 0;
    size_t transport_events = 0;
    size_t transport_actions = 0;
};

struct ResourceStats
{
    double user_seconds = 0.0;
    double system_seconds = 0.0;
    long voluntary_context_switches = 0;
    long involuntary_context_switches = 0;
    long minor_page_faults = 0;
    long major_page_faults = 0;
};

ResourceStats resource_snapshot()
{
    rusage u{};
    if (getrusage(RUSAGE_SELF, &u) != 0)
        throw runtime_error("getrusage failed");
    ResourceStats r;
    r.user_seconds = u.ru_utime.tv_sec + u.ru_utime.tv_usec / 1e6;
    r.system_seconds = u.ru_stime.tv_sec + u.ru_stime.tv_usec / 1e6;
    r.voluntary_context_switches = u.ru_nvcsw;
    r.involuntary_context_switches = u.ru_nivcsw;
    r.minor_page_faults = u.ru_minflt;
    r.major_page_faults = u.ru_majflt;
    return r;
}

ResourceStats resource_delta(const ResourceStats& a, const ResourceStats& b)
{
    ResourceStats r;
    r.user_seconds = b.user_seconds - a.user_seconds;
    r.system_seconds = b.system_seconds - a.system_seconds;
    r.voluntary_context_switches = b.voluntary_context_switches - a.voluntary_context_switches;
    r.involuntary_context_switches = b.involuntary_context_switches - a.involuntary_context_switches;
    r.minor_page_faults = b.minor_page_faults - a.minor_page_faults;
    r.major_page_faults = b.major_page_faults - a.major_page_faults;
    return r;
}

struct BackendTimeStats
{
    double pcr_seconds = 0.0;
    double dcf_seconds = 0.0;
    double pcr_comm_seconds = 0.0;
    double dcf_comm_seconds = 0.0;
};

struct TrialResult
{
    string mode;
    string profile;
    string scope;
    int party = 0;
    int n = 0;
    int k = 0;
    int trial = 0;
    uint64_t seed = 0;
    size_t threshold = 0;

    uint64_t theoretical_rounds = 0;
    size_t comparator_count = 0;
    size_t structural_depth = 0;

    size_t block_comparators = 0;
    size_t matrix_cross_comparators = 0;
    size_t reverse_prefilter_comparators = 0;
    size_t prefilter_candidate_count = 0;
    size_t prefilter_matrix_comparators = 0;
    size_t rank_mask_comparators = 0;
    size_t candidate_count = 0;
    size_t block_depth = 0;
    size_t lmax = 0;
    bool outer_partition_cancelled = false;
    size_t q = 0;
    size_t r = 0;
    size_t block_count = 0;
    size_t tail_real_size = 0;
    size_t compare_count = 0;
    size_t swap_count = 0;
    size_t max_layer_width = 0;
    double mean_layer_width = 0.0;
    string comparators_per_layer;

    PhaseStats online;

    KonaReducedDepthHybrid::SelectionStats selection;
    BackendTimeStats backend_time;

    double compute_seconds = 0.0;
    ResourceStats resources;
    string matrix_backend = "AUTO";
    bool reverse_path_used = false;

    long long dcf_evaluate_calls = 0;
    double dcf_evaluate_seconds = 0.0;
    double dcf_key_init_seconds = 0.0;

    uint64_t checksum = 0;
    string output_scope;
    string correctness = "NOT_CHECKED";
    size_t revealed_selected_count = 0;
};

struct RevealedPair
{
    uint64_t value = 0;
    uint64_t label = 0;
};

struct StageRecord
{
    string phase;
    size_t stage_id = 0;
    size_t comparators = 0;
    KonaReducedDepthHybrid::Backend backend =
            KonaReducedDepthHybrid::Backend::DCF;
    PhaseStats stats;
};

using PreparedOldPlan = KonaReducedDepthCongTopK::PreparedCongPlan;
using PreparedReducedPlan = KonaReducedDepthCongTopK::PreparedReducedPlan;
string requested_mode;
string dataset_path, timing_scope = "topk", input_pattern = "random", comparator_name = "hybrid";
int vote_labels = 0;
vector<RevealedPair> clear_input;

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

Z2<K> additive_share_for(int who, size_t i, Z2<K> value)
{
    Z2<K> mask = ring(mix64(i + 0xabcddcbaULL));
    return who == 0 ? mask : value - mask;
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
    double total = 0.0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        total += it->second.timer.elapsed();
    return total;
}

PhaseStats phase_delta(
        chrono::steady_clock::time_point start,
        chrono::steady_clock::time_point end,
        const NamedCommStats& before,
        const NamedCommStats& after)
{
    auto delta = after - before;
    PhaseStats p;
    p.total_seconds = chrono::duration<double>(end - start).count();
    p.comm_seconds = total_comm_seconds(delta);
    p.sent_bytes = delta.sent;
    p.transport_events = total_rounds(delta);
    if (p.transport_events % 2 != 0)
        throw runtime_error("odd send/receive transport event count");
    p.transport_actions = p.transport_events / 2;
    return p;
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
    if (!ip_file_name.empty())
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

void parse_argv(
        int argc,
        const char** argv,
        int& n,
        int& k,
        string& mode,
        string& profile,
        int& repeats,
        int& warmup,
        string& matrix_backend_name,
        uint64_t& seed,
        KonaReducedDepthHybrid::Policy& policy)
{
    opt.add("5000", 0, 1, 0, "Port number base", "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on", "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host", "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file", "-ip", "--ip-file-name");

    opt.add("1024", 0, 1, 0, "Number of items", "-n", "--size");
    opt.add("5", 0, 1, 0, "Top-k", "-k", "--topk");
    opt.add("old-hybrid", 0, 1, 0,
            "Mode: old-hybrid, full-matrix-hybrid, or pruned-matrix-hybrid",
            "-m", "--mode");
    opt.add("lan", 0, 1, 0,
            "Hybrid network profile: lan or wan", "", "--network-profile");
    opt.add("3", 0, 1, 0, "Repeated online trials", "-r", "--repeats");
    opt.add("0", 0, 1, 0, "Online warmup trials excluded from results", "", "--warmup");
    opt.add("20260819", 0, 1, 0, "Deterministic input seed",
            "-s", "--seed");

    opt.add(to_string(KonaBenchConfig::LAN_THRESHOLD).c_str(), 0, 1, 0,
            "LAN comparator threshold; <= threshold uses DCF",
            "", "--lan-threshold");
    opt.add(to_string(KonaBenchConfig::WAN_THRESHOLD).c_str(), 0, 1, 0,
            "WAN comparator threshold; <= threshold uses DCF",
            "", "--wan-threshold");
    opt.add("0", 0, 1, 0,
            "Hard threshold override; 0 uses profile threshold",
            "", "--threshold");
    opt.add("auto", 0, 1, 0,
            "Matrix rank backend override: auto, dcf, or pcr",
            "", "--matrix-backend");

    opt.add("", 0, 1, 0, "Knn five-file dataset directory; fixture distances outside timer", "", "--dataset");
    opt.add("topk", 0, 1, 0, "topk or terminal (constructed ordered blocks, no Cong)", "", "--scope");
    opt.add("hybrid", 0, 1, 0, "hybrid, pcr, or dcf for all Top-k stages", "", "--comparator");
    opt.add("random", 0, 1, 0, "random, duplicates, interleaved, concentrated", "", "--input-pattern");
    opt.add("0", 0, 1, 0, "Optional public label-domain size; vote separately timed; 0 skips", "", "--vote-labels");
    opt.parse(argc, argv);
    reject_unknown_options(opt);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else
        sscanf(argv[1], "%d", &playerno);

    opt.get("--size")->getInt(n);
    opt.get("--topk")->getInt(k);
    opt.get("--mode")->getString(mode);
    opt.get("--network-profile")->getString(profile);
    opt.get("--repeats")->getInt(repeats);
    opt.get("--warmup")->getInt(warmup);
    opt.get("--matrix-backend")->getString(matrix_backend_name);

    opt.get("--dataset")->getString(dataset_path);
    opt.get("--scope")->getString(timing_scope);
    opt.get("--comparator")->getString(comparator_name);
    opt.get("--input-pattern")->getString(input_pattern);
    opt.get("--vote-labels")->getInt(vote_labels);
    if (timing_scope != "topk" && timing_scope != "terminal") throw runtime_error("scope must be topk or terminal");
    if (vote_labels < 0) throw runtime_error("vote-labels must be nonnegative");
    if (comparator_name != "hybrid" && comparator_name != "pcr" && comparator_name != "dcf") throw runtime_error("bad comparator");
    if (input_pattern != "random" && input_pattern != "duplicates" && input_pattern != "interleaved" && input_pattern != "concentrated") throw runtime_error("bad input-pattern");
    string seed_s;
    opt.get("--seed")->getString(seed_s);
    seed = static_cast<uint64_t>(stoull(seed_s));

    int lan_threshold = 0;
    int wan_threshold = 0;
    int threshold_override = 0;
    opt.get("--lan-threshold")->getInt(lan_threshold);
    opt.get("--wan-threshold")->getInt(wan_threshold);
    opt.get("--threshold")->getInt(threshold_override);

    if (lan_threshold <= 0 || wan_threshold <= 0 || threshold_override < 0)
        throw runtime_error("thresholds must be positive; override may be zero");

    policy.lan_threshold = static_cast<size_t>(lan_threshold);
    policy.wan_threshold = static_cast<size_t>(wan_threshold);
    policy.threshold_override = static_cast<size_t>(threshold_override);

    if (matrix_backend_name == "auto")
        policy.matrix_backend_override = KonaReducedDepthHybrid::Backend::AUTO;
    else if (matrix_backend_name == "dcf")
        policy.matrix_backend_override = KonaReducedDepthHybrid::Backend::DCF;
    else if (matrix_backend_name == "pcr")
        policy.matrix_backend_override = KonaReducedDepthHybrid::Backend::PCR;
    else
        throw runtime_error("matrix-backend must be auto, dcf, or pcr");

    requested_mode = mode;
    if (mode == "old-pcr" || mode == "old-dcf" || mode == "new-pcr" || mode == "new-dcf") {
        comparator_name = mode.find("pcr") != string::npos ? "pcr" : "dcf";
        mode = mode.find("old") == 0 ? "old-hybrid" : "full-matrix-hybrid";
    }
    if (mode == "full") mode = "full-matrix-hybrid";
    if (mode == "pruned") mode = "pruned-matrix-hybrid";
    if (n <= 0)
        throw runtime_error("n must be > 0");
    if (k <= 0 || k > n)
        throw runtime_error("k must satisfy 1 <= k <= n");
    if (repeats <= 0)
        throw runtime_error("repeats must be > 0");
    if (warmup < 0)
        throw runtime_error("warmup must be >= 0");
    if (mode != "old-hybrid" &&
            mode != "new-hybrid" &&
            mode != "full-matrix-hybrid" &&
            mode != "pruned-hybrid" &&
            mode != "pruned-matrix-hybrid" &&
            mode != "two-block-reverse-hybrid" &&
            mode != "pruned-two-block-reverse-hybrid" &&
            mode != "reverse-prefilter-matrix-hybrid" &&
            mode != "reverse-prefilter-matrix-only" &&
            mode != "reverse-prefilter-matrix-after-block" &&
            mode != "obssn-matrix-only" &&
            mode != "octm-matrix-only")
        throw runtime_error("unknown mode: " + mode);
    if (profile != "lan" && profile != "wan")
        throw runtime_error("network-profile must be lan or wan");
    // Resolve once for every entry, after legacy mode aliases are normalized.
    policy.comparator_override = comparator_name == "pcr" ? KonaReducedDepthHybrid::Backend::PCR :
        comparator_name == "dcf" ? KonaReducedDepthHybrid::Backend::DCF : KonaReducedDepthHybrid::Backend::AUTO;
}

void record_stage(
        const char* phase,
        size_t stage_id,
        size_t comparators,
        KonaReducedDepthHybrid::Backend backend,
        const PhaseStats& s,
        KonaReducedDepthHybrid::SelectionStats& selection,
        BackendTimeStats& backend_time,
        vector<StageRecord>& stage_records)
{
    selection.record(backend, comparators);

    if (backend == KonaReducedDepthHybrid::Backend::PCR)
    {
        backend_time.pcr_seconds += s.total_seconds;
        backend_time.pcr_comm_seconds += s.comm_seconds;
    }
    else
    {
        backend_time.dcf_seconds += s.total_seconds;
        backend_time.dcf_comm_seconds += s.comm_seconds;
    }

    stage_records.push_back({
            phase, stage_id, comparators, backend, s});
}

vector<RevealedPair> reveal_pairs(
        const vector<SharePair<K>>& shares,
        RealTwoPartyPlayer* player)
{
    octetStream send_os, receive_os;
    for (const auto& x : shares)
    {
        x[0].pack(send_os);
        x[1].pack(send_os);
    }
    player->send(send_os);
    player->receive(receive_os);

    vector<RevealedPair> revealed;
    revealed.reserve(shares.size());
    for (const auto& x : shares)
    {
        Z2<K> peer_value, peer_label;
        peer_value.unpack(receive_os);
        peer_label.unpack(receive_os);
        revealed.push_back({
                (x[0] + peer_value).get_limb(0),
                (x[1] + peer_label).get_limb(0)});
    }
    return revealed;
}

vector<uint64_t> reveal_values(
        const vector<Z2<K>>& shares,
        RealTwoPartyPlayer* player)
{
    octetStream send_os, receive_os;
    for (const auto& x : shares)
        x.pack(send_os);
    player->send(send_os);
    player->receive(receive_os);

    vector<uint64_t> revealed;
    revealed.reserve(shares.size());
    for (const auto& x : shares)
    {
        Z2<K> peer;
        peer.unpack(receive_os);
        revealed.push_back((x + peer).get_limb(0));
    }
    return revealed;
}

vector<RevealedPair> make_plaintext_items(int n, int k, uint64_t seed)
{
    vector<RevealedPair> items;
    if (!dataset_path.empty()) {
        const auto data = ConstantPP::load_constantpp_dataset(dataset_path);
        for (size_t i=0; i<data.database.size(); ++i) {
            unsigned __int128 sum=0;
            for (size_t d=0; d<data.num_features; ++d) {
                const __int128 diff=__int128(data.database[i][d])-__int128(data.query[d]);
                sum += static_cast<unsigned __int128>(diff*diff);
                if (sum >= (static_cast<unsigned __int128>(1)<<63)) throw runtime_error("distance fixture exceeds PCR half-ring range");
            }
            items.push_back({uint64_t(sum), data.labels[i]});
        }
        return items;
    }
    int interleave_lmax=1, interleave_blocks=1;
    if (input_pattern == "interleaved") {
        const auto p=KonaReducedDepthCongTopK::build_reduced_depth_cong_plan(n,k);
        interleave_lmax=p.lmax; interleave_blocks=p.blocks.size();
    }
    for (int i=0; i<n; ++i) {
        uint64_t value = mix64(seed + uint64_t(i) + 1000) & ((uint64_t(1)<<20)-1);
        if (i%17==0 && i>0) value=mix64(seed+uint64_t(i-1)+1000)&((uint64_t(1)<<20)-1);
        if (input_pattern == "duplicates") value=uint64_t(i%7);
        if (input_pattern == "interleaved") value=uint64_t(i%interleave_lmax)*interleave_blocks+uint64_t(i/interleave_lmax);
        if (input_pattern == "concentrated") value=uint64_t(i);
        items.push_back({value, mix64(seed+uint64_t(i)+5000)&0xf});
    }
    return items;
}

vector<SharePair<K>> share_items(const vector<RevealedPair>& items)
{
    vector<SharePair<K>> shares(items.size());
    for (size_t i=0;i<items.size();++i) {
        shares[i][0]=additive_share_for(playerno,i,ring(items[i].value));
        shares[i][1]=additive_share_for(playerno,i+100000,ring(items[i].label));
    }
    return shares;
}

vector<SharePair<K>> construct_ordered_candidates(const PreparedReducedPlan& p)
{
    vector<RevealedPair> candidates;
    for (const auto& block : p.plan.blocks) {
        vector<RevealedPair> local(clear_input.begin()+block.real_begin,
                clear_input.begin()+block.real_begin+block.real_size);
        stable_sort(local.begin(),local.end(),[](const RevealedPair& a,const RevealedPair& b){return a.value<b.value;});
        candidates.insert(candidates.end(),local.begin(),local.begin()+block.keep);
    }
    return share_items(candidates);
}

bool validate_selected_multiset(
        const vector<RevealedPair>& selected,
        int n,
        int k,
        uint64_t seed)
{
    if ((int)selected.size() != k)
        return false;

    (void)n; (void)seed;
    auto items = clear_input;
    auto by_value_label = items;
    sort(by_value_label.begin(), by_value_label.end(),
            [](const RevealedPair& a, const RevealedPair& b) {
                if (a.value != b.value)
                    return a.value < b.value;
                return a.label < b.label;
            });

    vector<uint64_t> expected_values;
    expected_values.reserve(k);
    for (int i = 0; i < k; ++i)
        expected_values.push_back(by_value_label[i].value);

    vector<uint64_t> got_values;
    got_values.reserve(selected.size());
    for (const auto& x : selected)
        got_values.push_back(x.value);
    sort(expected_values.begin(), expected_values.end());
    sort(got_values.begin(), got_values.end());
    if (expected_values != got_values)
        return false;

    vector<RevealedPair> remaining = items;
    for (const auto& x : selected)
    {
        auto it = find_if(remaining.begin(), remaining.end(),
                [&](const RevealedPair& y) {
                    return x.value == y.value && x.label == y.label;
                });
        if (it == remaining.end())
            return false;
        remaining.erase(it);
    }
    return true;
}

void sync_outside_timer(RealTwoPartyPlayer* player)
{
    octetStream send_os, receive_os;
    send_os.store_int(1, 1);
    player->send(send_os);
    player->receive(receive_os);
}

TrialResult run_protocol(
        const vector<SharePair<K>>& input, const PreparedOldPlan& old,
        const PreparedReducedPlan& prepared, const string& mode, int k,
        int trial, uint64_t seed, const string& profile,
        KonaReducedDepthHybrid::Policy policy, RealTwoPartyPlayer* player,
        KonaDcfCompare::Compare64<K>& dcf, double key_init,
        vector<StageRecord>& records)
{
    using KonaReducedDepthCongTopK::Backend;
    using KonaReducedDepthCongTopK::Terminal;
    using KonaReducedDepthCongTopK::TerminalPlan;
    using KonaReducedDepthCongTopK::prepare_terminal;
    using KonaReducedDepthCongTopK::execute_blocks;
    using KonaReducedDepthCongTopK::collect_block_candidates;
    using KonaReducedDepthCongTopK::NoObserver;
    using KonaReducedDepthCongTopK::Output;
    using KonaReducedDepthCongTopK::execute_cong;
    using KonaReducedDepthCongTopK::execute_terminal;
    using KonaReducedDepthCongTopK::execute_topk;

    const bool original = mode == "old-hybrid";
    const bool after_block = mode == "reverse-prefilter-matrix-after-block";
    const bool terminal_only = timing_scope == "terminal" || after_block ||
            mode.find("matrix-only") != string::npos;
    if (original && terminal_only) throw runtime_error("old Cong has no matrix-only phase");
    Terminal kind = Terminal::Full;
    if (mode == "pruned-hybrid" || mode == "pruned-matrix-hybrid") kind = Terminal::Pruned;
    if (mode == "two-block-reverse-hybrid" || mode == "pruned-two-block-reverse-hybrid") kind = Terminal::ReverseTwo;
    if (mode.find("reverse-prefilter") == 0) kind = Terminal::ReversePrefilter;
    if (mode == "obssn-matrix-only") kind = Terminal::Obssn;
    if (mode == "octm-matrix-only") kind = Terminal::Octm;
    const TerminalPlan terminal = original ? TerminalPlan() : prepare_terminal(prepared, kind);

    // Restoration and synthetic candidates are prepared before sync/timer.
    auto work = input;
    vector<SharePair<K>> candidates;
    if (terminal_only) {
        if (after_block) {
            sync_outside_timer(player);
            execute_blocks<K>(work, prepared, profile, policy, dcf, player, NoObserver());
            candidates = collect_block_candidates<K>(work, prepared.plan, prepared.block_offsets);
        } else candidates = construct_ordered_candidates(prepared);
    }
    records.clear();
    records.reserve(original ? old.flattened_levels.size() : prepared.plan.block_depth + terminal.depth);
    TrialResult r;
    r.mode = mode == "new-hybrid" ? "full-matrix-hybrid" : mode == "pruned-hybrid" ? "pruned-matrix-hybrid" : mode;
    r.scope=terminal_only?"terminal":"topk";
    if (requested_mode == "old-pcr" || requested_mode == "old-dcf" ||
            requested_mode == "new-pcr" || requested_mode == "new-dcf") r.mode=requested_mode;
    r.profile=profile; r.party=playerno; r.n=input.size(); r.k=k; r.trial=trial; r.seed=seed;
    r.threshold=policy.threshold_for(profile); r.dcf_key_init_seconds=key_init;
    r.matrix_backend=KonaReducedDepthHybrid::backend_name(policy.matrix_backend_override);
    if (original) {
        r.comparator_count=old.comparators; r.structural_depth=old.network.levels.size();
        r.output_scope="materialized_topk_tail";
    } else {
        const auto& p=prepared.plan;
        r.block_comparators=terminal_only?0:p.block_comparators;
        r.block_depth=terminal_only?0:p.block_depth;
        r.reverse_prefilter_comparators=terminal.reverse_comparators;
        r.matrix_cross_comparators=terminal.matrix_comparators;
        r.rank_mask_comparators=terminal.mask_comparators;
        r.comparator_count=r.block_comparators+terminal.comparators;
        r.structural_depth=r.block_depth+terminal.depth;
        r.candidate_count=terminal.candidate_count;
        r.prefilter_candidate_count=terminal.reverse_used?terminal.candidate_count:0;
        r.prefilter_matrix_comparators=terminal.reverse_used?terminal.matrix_comparators:0;
        r.lmax=p.lmax; r.q=p.q; r.r=p.r; r.block_count=p.blocks.size(); r.tail_real_size=p.r;
        r.outer_partition_cancelled=p.outer_partition_cancelled;
        if (p.outer_partition_cancelled && !terminal_only)
            r.compare_count=r.swap_count=p.block_comparators;
        r.output_scope=terminal.output_scope; r.reverse_path_used=terminal.reverse_used;
        if (terminal.network) {
            r.compare_count=r.swap_count=terminal.comparators;
            ostringstream widths; size_t total=0;
            for (size_t d=0; d<terminal.layers.size(); ++d) {
                const size_t width=terminal.layers[d].size()/2;
                if(d) widths << ';';
                widths << width;
                r.max_layer_width=max(r.max_layer_width,width); total+=width;
            }
            r.comparators_per_layer=widths.str();
            r.mean_layer_width=terminal.layers.empty()?0.0:double(total)/terminal.layers.size();
        }
    }
    auto observe = [&](const char* phase,size_t stage,size_t count,auto operation) {
        const auto comm0=player->total_comm(); const auto start=chrono::steady_clock::now();
        const auto backend=operation();
        const auto end=chrono::steady_clock::now(); const auto comm1=player->total_comm();
        record_stage(phase,stage,count,backend,phase_delta(start,end,comm0,comm1),
                r.selection,r.backend_time,records);
    };
    dcf.reset_stats();
    sync_outside_timer(player);
    const auto comm0=player->total_comm(); const auto res0=resource_snapshot();
    const auto start=chrono::steady_clock::now();
    Output<K> out;
    if (original) {
        execute_cong<K>(work,old,profile,policy,dcf,player,observe);
        out.candidates.assign(work.end()-k,work.end());
    } else if (terminal_only) {
        out=execute_terminal<K>(std::move(candidates),terminal,k,profile,policy,dcf,player,observe);
    } else {
        out=execute_topk<K>(work,prepared,terminal,profile,policy,dcf,player,observe);
    }
    const auto end=chrono::steady_clock::now();
    const auto comm1=player->total_comm(); const auto res1=resource_snapshot();
    r.online=phase_delta(start,end,comm0,comm1);
    // Derived after online timing; empty/local stages contribute zero.
    for(const auto& stage:records)if(stage.comparators)
        r.theoretical_rounds+=(stage.backend==Backend::PCR?8:1)+
            (stage.phase=="matrix_rank"||stage.phase=="rank_mask"?0:1);
    r.compute_seconds=r.online.total_seconds-r.online.comm_seconds;
    r.resources=resource_delta(res0,res1);
    r.dcf_evaluate_calls=dcf.get_stats().evaluate_calls;
    r.dcf_evaluate_seconds=dcf.get_stats().evaluate_time.count();

    // Optional voting is a different stage and NEVER enters Top-k deltas.
    Z2<K> winner(0); PhaseStats vote_stats;
    KonaMaskVote::DcfVoteStats vote_shape;long long vote_calls=0;
    if (vote_labels > 0) {
        sync_outside_timer(player);
        const auto vc0=player->total_comm(); const auto v0=chrono::steady_clock::now();
        const auto calls0=dcf.get_stats().evaluate_calls;
        winner=KonaMaskVote::vote_dcf_optimized<K>(out.candidates,out.mask,vote_labels,player,dcf,out.mask.empty(),&vote_shape,k);
        vote_calls=dcf.get_stats().evaluate_calls-calls0;
        const auto v1=chrono::steady_clock::now(); const auto vc1=player->total_comm();
        vote_stats=phase_delta(v0,v1,vc0,vc1);
    }
    // Reveal/validation, checksums and all printing are outside both timers.
    const auto opened=reveal_pairs(out.candidates,player);
    vector<RevealedPair> selected;
    bool valid=true;
    if (out.mask.empty()) selected=opened;
    else {
        const auto mask=reveal_values(out.mask,player);
        for(size_t i=0;i<mask.size();++i) {
            if(mask[i]==1) selected.push_back(opened[i]);
            else if(mask[i]!=0) valid=false;
        }
    }
    valid=valid && validate_selected_multiset(selected,r.n,k,seed);
    if (kind==Terminal::Octm && !original)
        valid=valid && is_sorted(selected.begin(),selected.end(),[](const RevealedPair& a,const RevealedPair& b){return a.value<b.value;});
    r.revealed_selected_count=selected.size();
    for (const auto& x:selected) r.checksum+=x.value+x.label;
    if (vote_labels>0) {
        const auto result=reveal_values({winner},player);
        vector<size_t> counts(vote_labels,0);
        for(const auto& x:selected) ++counts.at(x.label);
        const size_t expected=distance(counts.begin(),max_element(counts.begin(),counts.end()));
        const bool pass=result[0]==expected;
        const bool vote_accounting=vote_calls==2*static_cast<long long>(vote_shape.comparators())&&
            vote_stats.sent_bytes==8*vote_shape.comparators()+16*vote_shape.mul_items&&
            vote_stats.transport_actions==vote_shape.exchanges();
        valid=valid && pass && vote_accounting;
        cout << "VOTE party=" << playerno << " trial=" << trial << " mode=" << mode
             << " backend=DCF_OPTIMIZED output_scope=winner_label_share tie_rule=smallest_public_label"
             << " compare_predicates=" << vote_shape.comparators() << " mul_items=" << vote_shape.mul_items
             << " public_all_selected=" << vote_shape.public_all_selected
             << " theoretical_rounds=" << vote_shape.logical_rounds()
             << " dcf_evaluate_calls=" << vote_calls
             << " accounting_check=" << (vote_accounting?"PASS":"FAIL")
             << " online_ms=" << vote_stats.total_seconds*1000
             << " sent_bytes=" << vote_stats.sent_bytes
             << " transport_actions=" << vote_stats.transport_events
             << " exchange_rounds=" << vote_stats.transport_actions
             << " correctness=" << (pass?"PASS":"FAIL") << endl;
    }
    r.correctness=valid?"PASS":"FAIL";
    return r;
}


void print_result(const TrialResult& r, const char* record_name = "RESULT")
{
    cout << fixed << setprecision(6);

    cout << record_name
         << " mode=" << r.mode
         << " profile=" << r.profile
         << " party=" << r.party
         << " n=" << r.n
         << " k=" << r.k
         << " scope=" << r.scope
         << " dataset=" << (dataset_path.empty() ? "synthetic" : dataset_path)
         << " trial=" << r.trial
         << " seed=" << r.seed
         << " comparator=" << comparator_name
         << " threshold=" << r.threshold

         << " total_comparators=" << r.comparator_count
         << " comparator_count=" << r.comparator_count
         << " structural_depth=" << r.structural_depth

         << " block_comparators=" << r.block_comparators
         << " matrix_cross_comparators=" << r.matrix_cross_comparators
         << " matrix_comparators=" << r.matrix_cross_comparators
         << " reverse_prefilter_comparators="
         << r.reverse_prefilter_comparators
         << " prefilter_candidate_count=" << r.prefilter_candidate_count
         << " prefilter_matrix_comparators="
         << r.prefilter_matrix_comparators
         << " rank_mask_comparators=" << r.rank_mask_comparators
         << " candidate_count=" << r.candidate_count
         << " block_depth=" << r.block_depth
         << " lmax=" << r.lmax
         << " candidate_lmax=" << r.lmax
         << " outer_partition_cancelled=" << int(r.outer_partition_cancelled)
         << " execution_path=" << (r.mode.find("old")==0 ? "original_cong" :
                 r.outer_partition_cancelled ? "single_cong" : "block_matrix")
         << " q=" << r.q
         << " r=" << r.r
         << " block_count=" << r.block_count
         << " tail_real_size=" << r.tail_real_size
         << " compare_count=" << r.compare_count
         << " swap_count=" << r.swap_count
         << " max_layer_width=" << r.max_layer_width
         << " mean_layer_width=" << r.mean_layer_width
         << " comparators_per_layer=" << r.comparators_per_layer

         << " pcr_stages=" << r.selection.pcr_stages
         << " dcf_stages=" << r.selection.dcf_stages
         << " pcr_comparators=" << r.selection.pcr_comparators
         << " dcf_comparators=" << r.selection.dcf_comparators
         << " backend_switches=" << r.selection.backend_switches

         << " pcr_stage_ms=" << r.backend_time.pcr_seconds * 1000.0
         << " dcf_stage_ms=" << r.backend_time.dcf_seconds * 1000.0
         << " pcr_stage_comm_ms="
         << r.backend_time.pcr_comm_seconds * 1000.0
         << " dcf_stage_comm_ms="
         << r.backend_time.dcf_comm_seconds * 1000.0

         << " online_ms=" << r.online.total_seconds * 1000.0
         << " noncomm_api_ms=" << r.compute_seconds * 1000.0
         << " compute_ms=" << r.compute_seconds * 1000.0
         << " comm_ms=" << r.online.comm_seconds * 1000.0
         << " sent_bytes=" << r.online.sent_bytes
         << " transport_actions=" << r.online.transport_events
         << " exchange_rounds=" << r.online.transport_actions
         << " theoretical_rounds=" << r.theoretical_rounds

         << " dcf_evaluate_calls=" << r.dcf_evaluate_calls
         << " dcf_evaluate_ms=" << r.dcf_evaluate_seconds * 1000.0
         << " dcf_key_init_ms=" << r.dcf_key_init_seconds * 1000.0
         << " matrix_backend=" << r.matrix_backend
         << " reverse_path_used=" << (r.reverse_path_used ? 1 : 0)
         << " user_cpu_ms=" << r.resources.user_seconds * 1000.0
         << " system_cpu_ms=" << r.resources.system_seconds * 1000.0
         << " voluntary_ctx_switches=" << r.resources.voluntary_context_switches
         << " involuntary_ctx_switches=" << r.resources.involuntary_context_switches
         << " minor_page_faults=" << r.resources.minor_page_faults
         << " major_page_faults=" << r.resources.major_page_faults

         << " output_scope=" << r.output_scope
         << " correctness=" << r.correctness
         << " revealed_selected_count=" << r.revealed_selected_count
         << " checksum=" << r.checksum
         << endl;
}

void print_stage_records(
        const TrialResult& r,
        const vector<StageRecord>& stage_records,
        const char* record_name = "LEVEL")
{
    cout << fixed << setprecision(6);
    for (const auto& s : stage_records)
    {
        cout << record_name
             << " mode=" << r.mode
             << " profile=" << r.profile
             << " party=" << r.party
             << " n=" << r.n
             << " k=" << r.k
             << " trial=" << r.trial
             << " phase=" << s.phase
             << " stage=" << s.stage_id
             << " comparators=" << s.comparators
             << " backend="
             << KonaReducedDepthHybrid::backend_name(s.backend)
             << " stage_ms=" << s.stats.total_seconds * 1000.0
             << " stage_comm_ms=" << s.stats.comm_seconds * 1000.0
             << " sent_bytes=" << s.stats.sent_bytes
             << " transport_actions=" << s.stats.transport_events
             << " exchange_rounds=" << s.stats.transport_actions
             << " theoretical_rounds=" << (s.comparators ?
                    (s.backend==KonaReducedDepthHybrid::Backend::PCR?8:1)+
                    (s.phase=="matrix_rank"||s.phase=="rank_mask"?0:1) : 0)
             << endl;
    }
}

} // namespace

int run(int argc, const char** argv)
{
    int n = 0;
    int k = 0;
    int repeats = 0;
    int warmup = 0;
    uint64_t seed = 0;
    string mode;
    string profile;
    string matrix_backend_name;
    KonaReducedDepthHybrid::Policy policy;

    parse_argv(
            argc, argv,
            n, k, mode, profile, repeats, warmup, matrix_backend_name,
            seed, policy);

    RealTwoPartyPlayer* player = start_networking();

    try
    {
        clear_input = make_plaintext_items(n, k, seed);
        n = int(clear_input.size());
        if (k > n) throw runtime_error("k exceeds actual dataset size");
        if (vote_labels > 0)
            for (const auto& item : clear_input)
                if (item.label >= uint64_t(vote_labels)) throw runtime_error("label outside public voting domain");
        const auto input = share_items(clear_input);

        const auto dcf_init_start = chrono::steady_clock::now();
        unique_ptr<KonaDcfCompare::Compare64<K>> dcf(
                new KonaDcfCompare::Compare64<K>(player, playerno));
        const auto dcf_init_end = chrono::steady_clock::now();

        const double dcf_key_init_seconds =
                chrono::duration<double>(
                        dcf_init_end - dcf_init_start).count();

        PreparedOldPlan old_plan;
        PreparedReducedPlan reduced_plan;
        if (mode == "old-hybrid")
            old_plan = KonaReducedDepthCongTopK::prepare_cong(n, k);
        else
            reduced_plan = KonaReducedDepthCongTopK::prepare_reduced_plan(n, k);

        cout << "EXPERIMENT"
             << " mode=" << mode
             << " profile=" << profile
             << " party=" << playerno
             << " n=" << n
             << " k=" << k
             << " repeats=" << repeats
             << " warmup=" << warmup
             << " seed=" << seed
             << " lan_threshold=" << policy.lan_threshold
             << " wan_threshold=" << policy.wan_threshold
             << " threshold_override=" << policy.threshold_override
             << " matrix_backend=" << matrix_backend_name
             << " effective_threshold="
             << policy.threshold_for(profile)
             << " selection_rule=DCF_if_width_le_threshold_else_PCR"
             << " scope=" << timing_scope
             << " dataset=" << (dataset_path.empty() ? "synthetic" : dataset_path)
             << " comparator=" << comparator_name
             << " vote_labels=" << vote_labels
             << " input_source=" << (dataset_path.empty() ? "constructed_shares" : "plaintext_distance_fixture")
             << " online_scope=post_sync_shares_ready_to_topk_output_shares_ready"
             << " offline_excluded=input_generation,network_plan,index_plan,dcf_key_init,pre_timer_sync,logging"
             << endl;

        auto execute_trial = [&](int trial, vector<StageRecord>& records) {
            return run_protocol(input, old_plan, reduced_plan, mode, k, trial, seed,
                    profile, policy, player, *dcf, dcf_key_init_seconds, records);
        };

        // Same-process warmups are explicitly tagged and excluded from RESULT summaries.
        for (int w = 0; w < warmup; ++w)
        {
            vector<StageRecord> warmup_records;
            TrialResult warmup_result = execute_trial(0, warmup_records);
            if (warmup_result.correctness != "PASS") throw runtime_error("warmup correctness failed");
            print_result(warmup_result, "WARMUP");
            print_stage_records(warmup_result, warmup_records, "WARMUP_LEVEL");
        }

        for (int trial = 1; trial <= repeats; ++trial)
        {
            vector<StageRecord> stage_records;
            TrialResult r = execute_trial(trial, stage_records);

            print_result(r);
            if (r.correctness != "PASS") throw runtime_error("Top-k correctness failed");
            print_stage_records(r, stage_records);
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
}

namespace hybrid_shared_engine {
using namespace matrix_engine;
// Independent bench: reuse the existing fixture, protocol, validation helpers.
// The original matrix bench entry and all protocol components remain intact.

namespace {
using SharedClock = std::chrono::steady_clock;
struct SharedMetric {
    PhaseStats comm;
    uint64_t dcf_calls=0,comparators=0,pcr=0,dcf=0,logical=0;
    double dcf_ms=0;
};
SharedMetric add_metric(SharedMetric a,const SharedMetric& b) {
    a.comm.total_seconds+=b.comm.total_seconds;a.comm.comm_seconds+=b.comm.comm_seconds;
    a.comm.sent_bytes+=b.comm.sent_bytes;a.comm.transport_events+=b.comm.transport_events;
    a.comm.transport_actions+=b.comm.transport_actions;
    a.dcf_calls+=b.dcf_calls;a.dcf_ms+=b.dcf_ms;a.comparators+=b.comparators;
    a.pcr+=b.pcr;a.dcf+=b.dcf;a.logical+=b.logical;return a;
}
template<class F>
SharedMetric measure_shared(RealTwoPartyPlayer* p,KonaDcfCompare::Compare64<64>& dcf,F operation) {
    const auto c0=p->total_comm();const auto d0=dcf.get_stats();const auto t0=SharedClock::now();
    operation();
    const auto t1=SharedClock::now();const auto c1=p->total_comm();const auto d1=dcf.get_stats();
    SharedMetric m;m.comm=phase_delta(t0,t1,c0,c1);
    m.dcf_calls=d1.evaluate_calls-d0.evaluate_calls;
    m.dcf_ms=(d1.evaluate_time-d0.evaluate_time).count()*1000;return m;
}
void print_shared_phase(const char* name,int k,const SharedMetric& m,bool composed=true,long long theoretical_rounds=-1) {
    cout<<"BENCH_PHASE phase="<<name<<" k="<<k<<" party="<<playerno<<" trial=1"
        <<" online_ms="<<m.comm.total_seconds*1000<<" sent_bytes="<<m.comm.sent_bytes
        <<" transport_actions="<<m.comm.transport_events<<" exchange_rounds="<<m.comm.transport_actions
        <<" theoretical_rounds="<<(theoretical_rounds<0?m.logical:theoretical_rounds)<<" dcf_evaluate_calls="<<m.dcf_calls<<" dcf_evaluate_ms="<<m.dcf_ms
        <<" comparators="<<m.comparators<<" pcr_comparators="<<m.pcr<<" dcf_comparators="<<m.dcf
        <<" timing_kind="<<(composed&&(string(name)=="topk"||string(name)=="block_cong")?"composed_shared_stages":"measured_stage")<<'\n';
}
struct SharedNode {
    vector<int> indices,children,leaves;
    int parent=-1;
    SharedMetric metric;
};
struct SharedCase {
    int k=0,leaf=0;
    PreparedReducedPlan plan;
    KonaReducedDepthCongTopK::TerminalPlan terminal;
    vector<SharePair<64>> finished_work;
    KonaReducedDepthCongTopK::Output<64> output;
    SharedMetric block,tail,vote;
    KonaMaskVote::DcfVoteStats vote_shape;
    vector<StageRecord> terminal_records;
    Z2<64> winner;
};
}

int run(int argc,const char** argv) {
    opt.add("1,2,3,5,20,100",0,1,0,"Ordered k branches; exact layer prefixes are shared","--ks");
    int n,k,repeats,warmup;uint64_t seed;string mode,profile,matrix_backend;
    KonaReducedDepthHybrid::Policy policy;
    parse_argv(argc,argv,n,k,mode,profile,repeats,warmup,matrix_backend,seed,policy);

    if(mode!="pruned-matrix-hybrid"||timing_scope!="topk"||comparator_name!="hybrid"||
            matrix_backend!="auto"||repeats!=1||warmup!=0||vote_labels<=0||input_pattern=="interleaved")
        throw runtime_error("shared bench requires pruned/topk/hybrid/auto, one trial, voting, k-independent fixture");
    string text,token;opt.get("--ks")->getString(text);istringstream input_ks(text);vector<int> ks;
    while(getline(input_ks,token,',')) {
        size_t used=0;int value=stoi(token,&used);
        if(used!=token.size()||value<1||find(ks.begin(),ks.end(),value)!=ks.end())throw runtime_error("bad k list");
        ks.push_back(value);
    }
    if(ks.empty())throw runtime_error("empty k list");
    const auto setup0=SharedClock::now();
    clear_input=make_plaintext_items(n,*max_element(ks.begin(),ks.end()),seed);n=clear_input.size();
    for(const auto& item:clear_input)if(item.label>=uint64_t(vote_labels))throw runtime_error("label domain mismatch");
    const auto input=share_items(clear_input);
    size_t features=0;
    if(!dataset_path.empty()){ifstream meta(dataset_path+"/Knn-meta");meta>>features;}
    vector<SharedNode> nodes(1);vector<SharedCase> cases(ks.size());
    size_t independent_layers=0,independent_comparators=0;
    for(size_t i=0;i<ks.size();++i) {
        auto& c=cases[i];c.k=ks[i];
        c.plan=KonaReducedDepthCongTopK::prepare_reduced_plan(n,c.k);
        c.terminal=KonaReducedDepthCongTopK::prepare_terminal(c.plan,KonaReducedDepthCongTopK::Terminal::Pruned);
        independent_layers+=c.plan.fused_block_levels.size();independent_comparators+=c.plan.plan.block_comparators;
        int parent=0;
        for(const auto& layer:c.plan.fused_block_levels) {
            int child=-1;
            for(int id:nodes[parent].children)if(nodes[id].indices==layer){child=id;break;}
            if(child<0){child=nodes.size();nodes.emplace_back();nodes[child].indices=layer;nodes[child].parent=parent;nodes[parent].children.push_back(child);}
            parent=child;
        }
        c.leaf=parent;nodes[parent].leaves.push_back(i);
    }
    std::unique_ptr<RealTwoPartyPlayer> connection(start_networking());auto* player=connection.get();
    KonaDcfCompare::Compare64<64> dcf(player,playerno);
    cout<<fixed<<setprecision(9)<<"party="<<playerno<<"\nbackend=pruned-matrix-hybrid-shared"
        <<"\ntrain_count="<<n<<"\nfeatures="<<features<<"\nquery_count=1\nk="<<*max_element(ks.begin(),ks.end())
        <<"\nnetwork_env="<<profile<<"\nmeasurement_mode=shared_exact_layer_prefixes"
        <<"\nlan_threshold="<<policy.lan_threshold<<"\nwan_threshold="<<policy.wan_threshold
        <<"\ncomparator_override="<<comparator_name<<"\nmatrix_backend_override="<<matrix_backend
        <<"\neffective_threshold="<<policy.threshold_for(profile)
        <<"\nsetup_ms="<<chrono::duration<double,milli>(SharedClock::now()-setup0).count()
        <<"\nrounds_definition=send_receive_actions_div_2\ntheoretical_rounds_status=DEPENDENCY_STAGES_WITHOUT_CHUNKS"
        <<"\nknn_online_e2e_ms=NOT_MEASURED\ntopk_vote_online_ms=NOT_MEASURED\n";
    SharedMetric unique;
    std::function<void(int,const vector<SharePair<64>>&,SharedMetric)> visit;
    visit=[&](int id,const vector<SharePair<64>>& state,SharedMetric cumulative) {
        // Fork copies are measurement infrastructure, outside operation timers.
        for(int leaf:nodes[id].leaves){cases[leaf].finished_work=state;cases[leaf].block=cumulative;}
        for(int child:nodes[id].children) {
            auto work=state;
            if(id==0 || nodes[id].children.size()>1)sync_outside_timer(player);
            KonaReducedDepthHybrid::Backend backend;
            auto m=measure_shared(player,dcf,[&] {
                backend=KonaReducedDepthHybrid::compare_and_swap_level<64>(work,nodes[child].indices,true,profile,policy,dcf,player);
            });
            m.comparators=nodes[child].indices.size()/2;
            if(backend==KonaReducedDepthHybrid::Backend::PCR)m.pcr=m.comparators;else m.dcf=m.comparators;
            m.logical=m.comparators?(m.pcr?8:1)+1:0;
            nodes[child].metric=m;unique=add_metric(unique,m);
            visit(child,work,add_metric(cumulative,m));
        }
    };
    visit(0,input,SharedMetric()); // Every node executes exactly once for ALL k.
    bool valid=true,accounting=true;
    for(auto& c:cases) {
        SharedMetric observed;
        auto observe=[&](const char* phase,size_t stage,size_t count,auto operation) {
            KonaReducedDepthHybrid::Backend backend;
            auto m=measure_shared(player,dcf,[&]{backend=operation();});m.comparators=count;
            if(backend==KonaReducedDepthHybrid::Backend::PCR)m.pcr=count;else m.dcf=count;
            m.logical=count?(m.pcr?8:1)+(string(phase)=="matrix_rank"||string(phase)=="rank_mask"?0:1):0;
            observed=add_metric(observed,m);
            c.terminal_records.push_back(StageRecord{phase,stage,count,backend,m.comm});
        };
        sync_outside_timer(player);
        c.tail=measure_shared(player,dcf,[&] {
            auto candidates=KonaReducedDepthCongTopK::collect_block_candidates<64>(c.finished_work,c.plan.plan,c.plan.block_offsets);
            c.output=KonaReducedDepthCongTopK::execute_terminal<64>(std::move(candidates),c.terminal,c.k,profile,policy,dcf,player,observe);
        });
        c.tail.logical=observed.logical;c.tail.comparators=observed.comparators;c.tail.pcr=observed.pcr;c.tail.dcf=observed.dcf;
        accounting=accounting&&c.block.comparators==c.plan.plan.block_comparators&&
            observed.comparators==c.terminal.comparators&&observed.comm.sent_bytes==c.tail.comm.sent_bytes&&
            observed.comm.transport_events==c.tail.comm.transport_events&&observed.dcf_calls==c.tail.dcf_calls;
        sync_outside_timer(player);
        c.vote=measure_shared(player,dcf,[&]{c.winner=KonaMaskVote::vote_dcf_optimized<64>(c.output.candidates,c.output.mask,vote_labels,player,dcf,c.output.mask.empty(),&c.vote_shape,c.k);});
        c.vote.comparators=c.vote_shape.comparators();c.vote.dcf=c.vote.comparators;
        accounting=accounting&&c.vote.dcf_calls==2*c.vote.comparators&&
            c.vote.comm.sent_bytes==8*c.vote.comparators+16*c.vote_shape.mul_items&&
            c.vote.comm.transport_actions==c.vote_shape.exchanges();
    }
    // Reveal/check/log only after ALL measured operations. Winner stays a share
    // until this validation block, exactly like the existing matrix benchmark.
    for(auto& c:cases) {
        const auto opened=reveal_pairs(c.output.candidates,player);vector<RevealedPair> selected;
        if(c.output.mask.empty())selected=opened;
        else {
            const auto mask=reveal_values(c.output.mask,player);
            for(size_t i=0;i<mask.size();++i){if(mask[i]==1)selected.push_back(opened[i]);else if(mask[i]!=0)valid=false;}
        }
        valid=valid&&validate_selected_multiset(selected,n,c.k,seed);
        vector<size_t> counts(vote_labels,0);uint64_t checksum=0;
        for(const auto& item:selected){++counts.at(item.label);checksum+=item.value+item.label;}
        const auto clear=reveal_values({c.winner},player);
        valid=valid&&clear[0]==uint64_t(distance(counts.begin(),max_element(counts.begin(),counts.end())));
        print_shared_phase("block_cong",c.k,c.block);print_shared_phase("terminal",c.k,c.tail);
        print_shared_phase("topk",c.k,add_metric(c.block,c.tail));print_shared_phase("vote",c.k,c.vote,false,c.vote_shape.logical_rounds());
        cout<<"CASE k="<<c.k<<" vote_backend=DCF_OPTIMIZED vote_mul_items="<<c.vote_shape.mul_items
            <<" vote_public_all_selected="<<c.vote_shape.public_all_selected
            <<" vote_single_selected="<<c.vote_shape.single_selected<<" vote_direct_label="<<c.vote_shape.direct_label<<" candidate_lmax="<<c.plan.plan.lmax
            <<" outer_partition_cancelled="<<c.plan.plan.outer_partition_cancelled
            <<" execution_path="<<(c.plan.plan.outer_partition_cancelled?"single_cong":"block_pruned_matrix")
            <<" block_count="<<c.plan.plan.blocks.size()<<" matrix_comparators="<<c.terminal.matrix_comparators
            <<" rank_mask_comparators="<<c.terminal.mask_comparators<<" structural_depth="<<c.plan.plan.block_depth+c.terminal.depth
            <<" selected_count="<<selected.size()<<" checksum="<<checksum<<" revealed_winner="<<clear[0]<<" prefix_node_ids=";
        vector<int> path;for(int id=c.leaf;id>0;id=nodes[id].parent)path.push_back(id);
        reverse(path.begin(),path.end());for(size_t i=0;i<path.size();++i)cout<<(i?":":"")<<path[i];cout<<'\n';
        for(const auto& r:c.terminal_records)
            cout<<"TERMINAL_LEVEL k="<<c.k<<" phase="<<r.phase<<" stage="<<r.stage_id
                <<" comparators="<<r.comparators<<" backend="<<KonaReducedDepthHybrid::backend_name(r.backend)
                <<" online_ms="<<r.stats.total_seconds*1000<<" sent_bytes="<<r.stats.sent_bytes
                <<" transport_actions="<<r.stats.transport_events<<" exchange_rounds="<<r.stats.transport_actions<<'\n';
    }
    for(size_t id=1;id<nodes.size();++id) {
        const auto& m=nodes[id].metric;
        cout<<"SHARED_NODE id="<<id<<" parent="<<nodes[id].parent<<" execution_count=1 comparators="<<m.comparators
            <<" backend="<<(m.pcr?"PCR":"DCF")<<" online_ms="<<m.comm.total_seconds*1000
            <<" sent_bytes="<<m.comm.sent_bytes<<" transport_actions="<<m.comm.transport_events
            <<" exchange_rounds="<<m.comm.transport_actions<<'\n';
    }
    accounting=accounting&&nodes.size()-1<=independent_layers&&unique.comparators<=independent_comparators;
    cout<<"independent_layer_count="<<independent_layers<<"\nexecuted_unique_layer_count="<<nodes.size()-1
        <<"\nsaved_layer_executions="<<independent_layers-(nodes.size()-1)
        <<"\nindependent_block_comparators="<<independent_comparators
        <<"\nexecuted_unique_block_comparators="<<unique.comparators
        <<"\naccounting_check="<<(accounting?"PASS":"FAIL")<<"\ncorrectness="<<(valid?"PASS":"FAIL")
        <<"\ntest_status="<<(accounting&&valid?"COMPLETED":"FAILED")<<endl;
    return accounting&&valid?0:1;
}
}

// Continuous full KNN for the hybrid scheme. Shared-prefix mode above remains
// a staged experiment; this mode measures one uninterrupted query for one k.
namespace hybrid_vote_engine {
using namespace matrix_engine;
int run(int argc,const char** argv) {
    int n,k,repeats,warmup;uint64_t seed;string mode,profile,matrix_backend;
    KonaReducedDepthHybrid::Policy policy;
    parse_argv(argc,argv,n,k,mode,profile,repeats,warmup,matrix_backend,seed,policy);
    if(dataset_path.empty()||mode!="pruned-matrix-hybrid"||repeats!=1||warmup||vote_labels<1)
        throw runtime_error("vote-only requires dataset/pruned/one trial/label domain");
    const auto setup0=chrono::steady_clock::now();
    clear_input=make_plaintext_items(n,k,seed);n=clear_input.size();
    set<uint64_t> unique;
    for(const auto& item:clear_input){
        if(item.label>=uint64_t(vote_labels))throw runtime_error("label outside public domain");
        unique.insert(item.value);
    }
    // Distinct distances make the plaintext preparation exactly equivalent to
    // all original comparison-network tie choices. Refuse ambiguous fixtures.
    if(unique.size()!=clear_input.size())throw runtime_error("vote-only fixture requires distinct distances");
    const auto prepared=KonaReducedDepthCongTopK::prepare_reduced_plan(n,k);
    vector<RevealedPair> candidates;
    for(const auto& block:prepared.plan.blocks){
        vector<RevealedPair> local(clear_input.begin()+block.real_begin,clear_input.begin()+block.real_begin+block.real_size);
        sort(local.begin(),local.end(),[](const auto& a,const auto& b){return a.value<b.value;});
        candidates.insert(candidates.end(),local.begin(),local.begin()+block.keep);
    }
    auto shares=construct_ordered_candidates(prepared);
    vector<size_t> order(candidates.size());iota(order.begin(),order.end(),0);
    sort(order.begin(),order.end(),[&](size_t a,size_t b){return candidates[a].value<candidates[b].value;});
    vector<uint64_t> clear_mask(candidates.size(),0);vector<size_t> counts(vote_labels,0);uint64_t checksum=0;
    for(int i=0;i<k;++i){clear_mask[order[i]]=1;++counts[candidates[order[i]].label];checksum+=candidates[order[i]].value+candidates[order[i]].label;}
    const bool all_selected=candidates.size()==size_t(k);
    vector<Z2<64>> mask;
    if(!all_selected)for(size_t i=0;i<clear_mask.size();++i)mask.push_back(additive_share_for(playerno,i+200000,ring(clear_mask[i])));
    unique_ptr<RealTwoPartyPlayer> connection(start_networking());auto* player=connection.get();
    KonaDcfCompare::Compare64<64> dcf(player,playerno);
    sync_outside_timer(player);KonaMaskVote::DcfVoteStats shape;Z2<64> winner;
    auto metric=hybrid_shared_engine::measure_shared(player,dcf,[&]{
        winner=KonaMaskVote::vote_dcf_optimized<64>(shares,mask,vote_labels,player,dcf,all_selected,&shape,k);
    });
    metric.comparators=shape.comparators();metric.dcf=metric.comparators;
    const bool accounting=metric.comm.sent_bytes==8*shape.comparators()+16*shape.mul_items&&
        metric.comm.transport_actions==shape.exchanges()&&metric.dcf_calls==2*shape.comparators();
    const auto clear=reveal_values({winner},player)[0];
    const auto expected=uint64_t(distance(counts.begin(),max_element(counts.begin(),counts.end())));
    const bool valid=clear==expected;
    size_t features=0;{ifstream meta(dataset_path+"/Knn-meta");meta>>features;}
    cout<<fixed<<setprecision(9)<<"party="<<playerno<<"\ntrain_count="<<n<<"\nfeatures="<<features<<"\nquery_count=1\nk="<<k
        <<"\nnetwork_env="<<profile<<"\nbackend=hybrid-dcf-vote-optimized\nvote_backend=DCF_OPTIMIZED"
        <<"\nmeasurement_mode=vote_only_from_prepared_exact_plan_fixture\ndistance_execution_count=0\ntopk_execution_count=0"
        <<"\nknn_online_e2e_ms=NOT_IN_SCOPE\ninput_preparation=plaintext_candidates_and_mask_outside_timer"
        <<"\ncandidate_count="<<candidates.size()<<"\npublic_label_domain="<<vote_labels<<"\nselected_checksum="<<checksum
        <<"\nouter_partition_cancelled="<<prepared.plan.outer_partition_cancelled<<"\nvote_public_all_selected="<<all_selected
        <<"\nvote_mul_items="<<shape.mul_items<<"\nvote_single_selected="<<shape.single_selected
        <<"\nsetup_and_validation_ms="<<chrono::duration<double,milli>(chrono::steady_clock::now()-setup0).count()-metric.comm.total_seconds*1000<<'\n';
    hybrid_shared_engine::print_shared_phase("vote",k,metric,false,shape.logical_rounds());
    cout<<"output_scope=winning_label_secret_share\nrevealed_winner="<<clear<<"\nexpected_label="<<expected
        <<"\naccounting_check="<<(accounting?"PASS":"FAIL")<<"\ncorrectness="<<(valid?"PASS":"FAIL")
        <<"\ntest_status="<<(accounting&&valid?"COMPLETED":"FAILED")<<endl;
    return accounting&&valid?0:1;
}
}

namespace hybrid_full_engine {
using namespace matrix_engine;
int run(int argc,const char** argv) {
    int n,k,repeats,warmup;uint64_t seed;string mode,profile,matrix_backend;
    KonaReducedDepthHybrid::Policy policy;
    parse_argv(argc,argv,n,k,mode,profile,repeats,warmup,matrix_backend,seed,policy);
    if(dataset_path.empty()||mode!="pruned-matrix-hybrid"||repeats!=1||warmup!=0||vote_labels<1)
        throw runtime_error("hybrid full requires a dataset, pruned matrix, one trial and label domain");
    const auto setup0=chrono::steady_clock::now();
    const auto data=ConstantPP::load_constantpp_dataset(dataset_path);
    n=data.database.size();clear_input=make_plaintext_items(n,k,seed);
    for(const auto& item:clear_input)if(item.label>=uint64_t(vote_labels))throw runtime_error("label domain mismatch");
    auto prepared=KonaReducedDepthCongTopK::prepare_reduced_plan(n,k);
    auto terminal=KonaReducedDepthCongTopK::prepare_terminal(prepared,KonaReducedDepthCongTopK::Terminal::Pruned);
    unique_ptr<RealTwoPartyPlayer> connection(start_networking());auto* player=connection.get();
    KonaDcfCompare::Compare64<64> dcf(player,playerno);
    // Original online KONA distance; prepare the existing zero-auxiliary inputs locally.
    kona_native::playerno=playerno;kona_native::player=player;
    kona_native::KNN_party_optimized distance_party(playerno);
    distance_party.m_player=player;distance_party.num_train_data=n;
    distance_party.num_features=data.num_features;distance_party.num_test_data=1;
    distance_party.m_ESD_vec.resize(n);
    if(playerno==0){
        for(int i=0;i<n;++i){auto* sample=new kona_native::Sample(data.num_features);
            sample->label=data.labels[i];for(size_t j=0;j<data.num_features;++j)sample->features[j]=data.database[i][j];
            distance_party.m_sample.push_back(sample);}
    }else{auto* sample=new kona_native::Sample(data.num_features);sample->label=data.query_label;
        for(size_t j=0;j<data.num_features;++j)sample->features[j]=data.query[j];
        distance_party.m_test.push_back(sample);}
    distance_party.fake_load_triples();kona_engine::prepare_local_aby2_inputs(distance_party,data);
    using hybrid_shared_engine::SharedMetric;using hybrid_shared_engine::measure_shared;
    using hybrid_shared_engine::print_shared_phase;
    KonaReducedDepthCongTopK::Output<64> output;Z2<64> winner;
    SharedMetric dist,topk,vote,block,tail,topk_other;
    struct Shape {uint64_t comparators=0,pcr=0,dcf=0,bytes=0,exchanges=0,logical=0;};
    Shape block_shape,tail_shape;
    auto observer=[&](const char* phase,size_t,size_t count,auto operation){
        const auto backend=operation();
        const bool is_block=string(phase)=="block"||string(phase)=="single_cong";
        const bool swaps=string(phase)!="matrix_rank"&&string(phase)!="rank_mask";
        auto& shape=is_block?block_shape:tail_shape;
        const bool pcr=backend==KonaReducedDepthHybrid::Backend::PCR;
        shape.comparators+=count;if(pcr)shape.pcr+=count;else shape.dcf+=count;
        shape.bytes+=(pcr?70:8)*count+(swaps?64*count:0);
        shape.exchanges+=(pcr?7:0)+KonaBenchConfig::chunks(count,pcr?KonaBenchConfig::mul_chunk():KonaBenchConfig::dcf_chunk())+(swaps?KonaBenchConfig::chunks(2*count,KonaBenchConfig::mul_chunk()):0);
        shape.logical+=(pcr?8:1)+(swaps?1:0);
    };
    sync_outside_timer(player);
    const auto c0=player->total_comm();const auto t0=chrono::steady_clock::now();
    dist=measure_shared(player,dcf,[&]{distance_party.compute_ESD_for_one_query(0);});
    topk=measure_shared(player,dcf,[&]{
        block=measure_shared(player,dcf,[&]{
            KonaReducedDepthCongTopK::execute_blocks<64>(distance_party.m_ESD_vec,prepared,profile,policy,dcf,player,observer);
        });
        tail=measure_shared(player,dcf,[&]{
            auto candidates=KonaReducedDepthCongTopK::collect_block_candidates<64>(distance_party.m_ESD_vec,prepared.plan,prepared.block_offsets);
            output=KonaReducedDepthCongTopK::execute_terminal<64>(std::move(candidates),terminal,k,profile,policy,dcf,player,observer);
        });
    });
    KonaMaskVote::DcfVoteStats vote_shape;
    vote=measure_shared(player,dcf,[&]{winner=KonaMaskVote::vote_dcf_optimized<64>(output.candidates,output.mask,vote_labels,player,dcf,output.mask.empty(),&vote_shape,k);});
    const auto t1=chrono::steady_clock::now();const auto c1=player->total_comm();
    // All detailed accounting is deferred until the independent full end.
    block.comparators=block_shape.comparators;block.pcr=block_shape.pcr;block.dcf=block_shape.dcf;
    tail.comparators=tail_shape.comparators;tail.pcr=tail_shape.pcr;tail.dcf=tail_shape.dcf;
    topk.comparators=block.comparators+tail.comparators;topk.pcr=block.pcr+tail.pcr;topk.dcf=block.dcf+tail.dcf;
    topk_other.comm=topk.comm;
    for(auto* m:{&block,&tail}){topk_other.comm.total_seconds-=m->comm.total_seconds;
        topk_other.comm.comm_seconds-=m->comm.comm_seconds;topk_other.comm.sent_bytes-=m->comm.sent_bytes;
        topk_other.comm.transport_events-=m->comm.transport_events;topk_other.comm.transport_actions-=m->comm.transport_actions;}
    const uint64_t vote_logical=vote_shape.logical_rounds();
    vote.comparators=vote_shape.comparators();vote.dcf=vote.comparators;
    SharedMetric total;total.comm=phase_delta(t0,t1,c0,c1);
    total.dcf_calls=dist.dcf_calls+topk.dcf_calls+vote.dcf_calls;
    total.dcf_ms=dist.dcf_ms+topk.dcf_ms+vote.dcf_ms;
    SharedMetric other;other.comm=total.comm;
    for(auto* m:{&dist,&topk,&vote}){other.comm.total_seconds-=m->comm.total_seconds;
        other.comm.comm_seconds-=m->comm.comm_seconds;other.comm.sent_bytes-=m->comm.sent_bytes;
        other.comm.transport_events-=m->comm.transport_events;other.comm.transport_actions-=m->comm.transport_actions;}
    const auto opened=reveal_pairs(output.candidates,player);vector<RevealedPair> chosen;
    bool valid=true;
    if(output.mask.empty())chosen=opened;
    else{const auto clear_mask=reveal_values(output.mask,player);for(size_t i=0;i<opened.size();++i){
        if(clear_mask[i]==1)chosen.push_back(opened[i]);else if(clear_mask[i]!=0)valid=false;}}
    valid=valid&&validate_selected_multiset(chosen,n,k,seed);
    vector<size_t> counts(vote_labels,0);for(const auto& item:chosen)++counts.at(item.label);
    valid=valid&&reveal_values({winner},player)[0]==uint64_t(distance(counts.begin(),max_element(counts.begin(),counts.end())));
    const bool accounting=dist.comm.sent_bytes==0&&dist.comm.transport_events==0&&
        block.comm.sent_bytes==block_shape.bytes&&block.comm.transport_actions==block_shape.exchanges&&block.dcf_calls==2*block_shape.dcf&&
        tail.comm.sent_bytes==tail_shape.bytes&&tail.comm.transport_actions==tail_shape.exchanges&&tail.dcf_calls==2*tail_shape.dcf&&
        topk_other.comm.sent_bytes==0&&topk_other.comm.transport_events==0&&
        other.comm.sent_bytes==0&&other.comm.transport_events==0&&
        topk.comparators==prepared.plan.block_comparators+terminal.comparators&&
        vote.dcf_calls==2*vote_shape.comparators()&&
        vote.comm.sent_bytes==8*vote_shape.comparators()+16*vote_shape.mul_items&&
        vote.comm.transport_actions==vote_shape.exchanges();
    cout<<fixed<<setprecision(9)<<"party="<<playerno<<"\nbackend=pruned-matrix-hybrid\ntrain_count="<<n
        <<"\nfeatures="<<data.num_features<<"\nquery_count=1\nk="<<k<<"\nnetwork_env="<<profile
        <<"\nmeasurement_mode=measured_full_knn\nsetup_ms="<<chrono::duration<double,milli>(t0-setup0).count()
        <<"\nvote_backend=DCF_OPTIMIZED\nvote_mul_items="<<vote_shape.mul_items
        <<"\nvote_public_all_selected="<<vote_shape.public_all_selected<<"\nvote_direct_label="<<vote_shape.direct_label
        <<"\nouter_partition_cancelled="<<prepared.plan.outer_partition_cancelled
        <<"\nblock_count="<<prepared.plan.blocks.size()<<"\ncandidate_lmax="<<prepared.plan.lmax
        <<"\nvote_candidate_count="<<output.candidates.size()<<"\nvote_class_count="<<vote_labels
        <<"\nlan_threshold="<<policy.lan_threshold<<"\nwan_threshold="<<policy.wan_threshold
        <<"\ncomparator_override="<<comparator_name<<"\nmatrix_backend_override="<<matrix_backend
        <<"\neffective_threshold="<<policy.threshold_for(profile)
        <<"\ninput_preparation=local-zero-auxiliary-aby2-inputs"
        <<"\npartition_rule=original-formula-and-4k-gt-Lmax-cutoff"
        <<"\nnetwork_structural_depth="<<prepared.plan.block_depth+terminal.depth<<'\n';
    print_shared_phase("distance",k,dist,false,0);
    print_shared_phase("block_cong",k,block,false,block_shape.logical);
    print_shared_phase("terminal",k,tail,false,tail_shape.logical);
    print_shared_phase("topk_other",k,topk_other,false,0);
    print_shared_phase("topk",k,topk,false,block_shape.logical+tail_shape.logical);
    print_shared_phase("vote",k,vote,false,vote_logical);
    print_shared_phase("online_other",k,other,false,0);
    print_shared_phase("knn_online",k,total,false,block_shape.logical+tail_shape.logical+vote_logical);
    cout<<"full_knn_measured=1\noutput_scope=winning_label_secret_share\naccounting_check="<<(accounting?"PASS":"FAIL")
        <<"\ncorrectness="<<(valid?"PASS":"FAIL")<<"\ntest_status="<<(valid&&accounting?"COMPLETED":"FAILED")<<endl;
    for(auto* sample:distance_party.m_sample)delete sample;
    for(auto* sample:distance_party.m_test)delete sample;
    return valid&&accounting?0:1;
}
}

namespace shao_engine {
using namespace std;
using hybrid_shared_engine::SharedMetric;
using hybrid_shared_engine::measure_shared;
using hybrid_shared_engine::print_shared_phase;
int run(int argc,const char** argv) {
    ez::ezOptionParser opt;
    opt.add("0",0,1,0,"Party","-p","--player");
    opt.add("34000",0,1,0,"Port","-pn","--portnumbase");
    opt.add("localhost",0,1,0,"Host","-h","--hostname");
    opt.add("",0,1,0,"Dataset path","--dataset");
    opt.add("full",0,1,0,"full/distance/topk-vote","--scope");
    opt.add("wan",0,1,0,"Profile","--network-profile");
    opt.add("5",0,1,0,"K","-k","--k");
    opt.add("0",0,1,0,"Label domain;0=infer","--vote-labels");
    opt.add("512",0,1,0,"NHSS modulus bits","--lambda");
    opt.parse(argc,argv);
    reject_unknown_options(opt);
    int party,port,k,domain,lambda;string path,scope,profile,host;
    opt.get("-p")->getInt(party);opt.get("-pn")->getInt(port);opt.get("-k")->getInt(k);
    opt.get("--vote-labels")->getInt(domain);
    opt.get("--lambda")->getInt(lambda);opt.get("--dataset")->getString(path);
    opt.get("--scope")->getString(scope);opt.get("--network-profile")->getString(profile);opt.get("-h")->getString(host);
    if(profile!="lan"&&profile!="wan")throw runtime_error("shao profile must be lan/wan");
    if(lambda<128)throw runtime_error("invalid NHSS lambda");
    if(scope!="full"&&scope!="distance"&&scope!="topk-vote")throw runtime_error("invalid Shao scope");
    const auto setup_start=chrono::steady_clock::now();
    auto data=ConstantPP::load_constantpp_dataset(path);const size_t n=data.database.size();
    if(k<1||size_t(k)>n)throw runtime_error("invalid Shao k");
    if(!domain)domain=*max_element(data.labels.begin(),data.labels.end())+1;
    for(auto label:data.labels)if(label>=uint64_t(domain))throw runtime_error("Shao label outside public domain");
    vector<uint64_t> expected(n,0);
    for(size_t i=0;i<n;++i)for(size_t j=0;j<data.num_features;++j){
        const uint64_t d=data.database[i][j]-data.query[j];expected[i]+=d*d;
    }
    const auto plan=KonaCongRotatedMsbAdapter::prepare_plan(n,k);
    uint64_t expected_top_bytes=0,expected_top_exchanges=0,top_logical=0;
    if(scope!="distance")for(const auto& layer:plan.layers){
        const size_t pairs=layer.size()/2;if(!pairs)continue;
        top_logical+=189; // 187 packed AND stages + B2A + swap, before chunk expansion.
        for(size_t offset=0;offset<pairs;offset+=KonaBenchConfig::mul_chunk()){
            const size_t count=std::min<size_t>(KonaBenchConfig::mul_chunk(),pairs-offset);
            expected_top_bytes+=2992*((count+63)/64)+80*count;
            expected_top_exchanges+=188+KonaBenchConfig::chunks(2*count,KonaBenchConfig::mul_chunk());
        }
    }
    const uint64_t frequency_predicates=2*uint64_t(domain)*k;
    const uint64_t expected_frequency_bytes=32*uint64_t(domain)*k;
    const uint64_t expected_frequency_exchanges=KonaBenchConfig::chunks(frequency_predicates,KonaBenchConfig::dcf_chunk())+KonaBenchConfig::chunks(uint64_t(domain)*k,KonaBenchConfig::mul_chunk());
    uint64_t expected_max_exchanges=0,max_logical=0;
    for(size_t count=domain;count>1;count=(count+1)/2){
        const size_t pairs=count/2;max_logical+=2;
        expected_max_exchanges+=KonaBenchConfig::chunks(pairs,KonaBenchConfig::dcf_chunk())+KonaBenchConfig::chunks(2*pairs,KonaBenchConfig::mul_chunk());
    }

    vector<KonaNhssCongPipeline::RecordShare> database;
    vector<KonaNhss::QueryShare> query;KonaNhss::KeyPair keys;
    if(scope!="topk-vote") {
        // Existing deterministic dealer/client setup, outside online time.
        // Reuses NHSS implementation/security limits, no plaintext distance substitution.
        keys=KonaNhss::keygen(lambda,20260829UL);gmp_randclass rng(gmp_randinit_mt);rng.seed(20260830UL);
        database.resize(n);
        for(size_t i=0;i<n;++i){
            for(auto coordinate:data.database[i])database[i].coordinates.push_back(KonaNhss::share_data(coordinate,keys.sec,rng).party[party]);
            const Z2<64> mask(static_cast<mp_limb_t>(7001+37*i));
            database[i].payload_share=party==0?mask:Z2<64>(data.labels[i])-mask;
        }
        for(auto coordinate:data.query)query.push_back(KonaNhss::share_query(coordinate,keys.pub,rng).party[party]);
    }
    Names names;Server::start_networking(names,party,2,host,port,Names::DEFAULT_PORT);
    unique_ptr<RealTwoPartyPlayer> connection(new RealTwoPartyPlayer(names,1-party,0));auto* player=connection.get();
    KonaDcfCompare::Compare64<64> dcf(player,party);matrix_engine::playerno=party;
    vector<array<Z2<64>,2>> wires(n);
    if(scope=="topk-vote")for(size_t i=0;i<n;++i){
        Z2<64> m(static_cast<mp_limb_t>(7001+37*i));
        wires[i]={party==0?m:Z2<64>(expected[i])-m,party==0?m:Z2<64>(data.labels[i])-m};
    }
    vector<array<Z2<64>,2>> selected;vector<array<Z2<64>,2>> counts;
    SharedMetric dist,topk,freq,maximum,total,other;Z2<64> winner;
    size_t rmsb_pairs=0;
    matrix_engine::sync_outside_timer(player);
    const auto c0=player->total_comm();const auto t0=chrono::steady_clock::now();
    if(scope!="topk-vote")dist=measure_shared(player,dcf,[&]{
        wires=KonaNhssCongPipeline::build_distance_payload_pairs(party,database,query,keys.pub);
    });
    if(scope!="distance") {
        topk=measure_shared(player,dcf,[&]{
            KonaCongRotatedMsbAdapter::execute_prepared<64>(wires,plan,true,player,
                [&](size_t,size_t,size_t count,auto operation){
                    rmsb_pairs+=count;operation();
                });
        });
        // Candidate materialization is part of online total, separately residual.
        selected.assign(wires.end()-k,wires.end());const auto mask=KonaMaskVote::public_constants<64>(k,1,player);
        freq=measure_shared(player,dcf,[&]{KonaMaskVote::label_counts_with_dcf<64>(selected,mask,domain,counts,player,dcf);});
        maximum=measure_shared(player,dcf,[&]{winner=KonaMaskVote::winner_from_counts_with_dcf<64>(std::move(counts),player,dcf);});
    }
    const auto t1=chrono::steady_clock::now();const auto c1=player->total_comm();
    total.comm=matrix_engine::phase_delta(t0,t1,c0,c1);total.dcf_calls=dist.dcf_calls+topk.dcf_calls+freq.dcf_calls+maximum.dcf_calls;
    other.comm=total.comm;
    for(auto* m:{&dist,&topk,&freq,&maximum}){
        other.comm.total_seconds-=m->comm.total_seconds;other.comm.sent_bytes-=m->comm.sent_bytes;
        other.comm.transport_events-=m->comm.transport_events;other.comm.transport_actions-=m->comm.transport_actions;
    }
    topk.comparators=rmsb_pairs;
    freq.comparators=2*size_t(domain)*size_t(k);freq.dcf=freq.comparators;
    maximum.comparators=domain-1;maximum.dcf=maximum.comparators;
    bool valid=true;
    if(scope=="distance") {
        const auto opened=matrix_engine::reveal_pairs(wires,player);
        for(size_t i=0;i<n;++i)valid=valid&&opened[i].value==expected[i]&&opened[i].label==data.labels[i];
    }else {
        const auto opened=matrix_engine::reveal_pairs(selected,player);auto sorted=expected;sort(sorted.begin(),sorted.end());
        vector<size_t> histogram(domain,0);multiset<pair<uint64_t,uint64_t>> available;
        for(size_t i=0;i<n;++i)available.insert({expected[i],data.labels[i]});
        for(size_t i=0;i<opened.size();++i){
            auto pos=available.find({opened[i].value,opened[i].label});
            valid=valid&&opened[i].value==sorted[i]&&pos!=available.end();
            if(pos!=available.end())available.erase(pos);
            if(opened[i].label>=uint64_t(domain))valid=false;else ++histogram[opened[i].label];
        }
        valid=valid&&matrix_engine::reveal_values({winner},player)[0]==uint64_t(distance(histogram.begin(),max_element(histogram.begin(),histogram.end())));
    }
    const bool accounting=other.comm.sent_bytes==0&&other.comm.transport_events==0&&dist.comm.sent_bytes==0&&dist.comm.transport_events==0&&
        (scope=="distance" || (topk.comm.sent_bytes==expected_top_bytes&&topk.comm.transport_actions==expected_top_exchanges&&topk.dcf_calls==0&&
        freq.comm.sent_bytes==expected_frequency_bytes&&freq.comm.transport_actions==expected_frequency_exchanges&&freq.dcf_calls==2*frequency_predicates&&
        maximum.comm.sent_bytes==72*uint64_t(domain-1)&&maximum.comm.transport_actions==expected_max_exchanges&&maximum.dcf_calls==2*uint64_t(domain-1)));
    cout<<fixed<<setprecision(9)<<"party="<<party<<"\ndataset="<<path<<"\ntrain_count="<<n<<"\nfeatures="<<data.num_features
        <<"\nquery_count=1\nk="<<k<<"\nnetwork_env="<<profile<<"\nbackend=nhss+cong+rotated-msb+dcf-vote"
        <<"\nnetwork_comparator=rotated-msb-all-layers\nvote_comparator=batched-dcf"
        <<"\nmaterial_mode=existing-deterministic-NHSS-and-L2"
        <<"\nsetup_ms="<<chrono::duration<double,milli>(t0-setup_start).count()
        <<"\nfull_knn_measured="<<(scope=="full")<<"\nknn_online_e2e_ms="<<(scope=="full"?"SEE_BENCH_PHASE":"NOT_IN_SCOPE")
        <<"\ninput_preparation="<<(scope=="topk-vote"?"plaintext_distance_fixture":"NHSS_offline_shares")
        <<"\ntopk_rmsb_comparators="<<rmsb_pairs<<"\ntopk_dcf_comparators=0"
        <<"\ntopk_expected_payload_bytes="<<expected_top_bytes<<"\ntopk_expected_exchange_rounds="<<expected_top_exchanges
        <<"\ntopk_theoretical_logical_rounds="<<top_logical<<"\ncompare_chunk_size="<<KonaBenchConfig::dcf_chunk()<<"\nswap_chunk_size="<<KonaBenchConfig::mul_chunk()<<'\n';
    if(scope!="topk-vote")print_shared_phase("distance",k,dist,false,0);
    if(scope!="distance") {
        for(size_t l=0;l<plan.layers.size();++l)cout<<"SHAO_LAYER layer="<<l<<" pairs="<<plan.layers[l].size()/2
            <<" backend="<<"ROTATED_MSB"<<'\n';
        print_shared_phase("topk",k,topk,false,top_logical);print_shared_phase("vote_frequency",k,freq,false,2);print_shared_phase("vote_max",k,maximum,false,max_logical);
        print_shared_phase("vote",k,hybrid_shared_engine::add_metric(freq,maximum),false,2+max_logical);
        print_shared_phase("online_other",k,other,false,0);
        print_shared_phase(scope=="full"?"knn_online":"topk_vote_online",k,total,false,top_logical+2+max_logical);
    }
    cout<<"output_scope="<<(scope=="distance"?"distance_label_shares":"winning_label_secret_share")
        <<"\naccounting_check="<<(accounting?"PASS":"FAIL")<<"\ncorrectness="<<(valid?"PASS":"FAIL")
        <<"\ntest_status="<<(valid&&accounting?"COMPLETED":"FAILED")<<endl;
    return valid&&accounting?0:1;
}
}

using namespace std;
int main(int argc,char** argv) {
    try{
        string scheme,scope="full",dataset;vector<string> args;
        for(int i=1;i<argc;++i){string a=argv[i];
            if(a=="--help"||a=="-?"){
                cout<<"knn-bench.x --scheme constantpp|secknn|kona|hybrid|shao --scope full|distance|checkpoints|shared|pre-vote|topk|terminal|vote [options]\n"
                    <<"Hybrid: --chunking off|on (default off)\nCommon: -p 0|1 -pn PORT --dataset PATH --k K --ks 1,2,3,5,20,100 --network-profile lan|wan\n"
                    <<"full measures real continuous distance+Top-k+vote; checkpoints/shared retain explicit staged/composed semantics.\n";return 0;}
            if(a=="--scheme"||a=="--scope"||a=="--dataset"){
                if(++i>=argc)throw runtime_error("missing value for "+a);
                if(a=="--scheme")scheme=argv[i];else if(a=="--scope")scope=argv[i];else dataset=argv[i];
            }else{args.push_back(a=="--k"?"-k":a);}
        }
        if(scheme.empty())throw runtime_error("--scheme is required; use --help");
        // Applicability is validated once, before dispatch/networking. Merely
        // recognizing a flag in a downstream parser does not make it meaningful.
        for(const auto& flag:args){
            const bool selection_option=flag=="--comparator"||flag=="--matrix-backend"||
                flag=="--threshold"||flag=="--lan-threshold"||flag=="--wan-threshold";
            if(selection_option&&(scheme!="hybrid"||scope=="vote"))
                throw runtime_error(flag+" is not applicable to "+scheme+"/"+scope);
            if((flag=="--kona-backend"&&scheme!="kona")||
               (flag=="--topk-mode"&&scheme!="secknn")||
               (flag=="--secmax-mode"&&scheme!="constantpp"))
                throw runtime_error(flag+" is not applicable to "+scheme);
            if((flag=="--vote-order"||flag=="--checkpoints"||flag=="--ks")&&
               ((scheme=="kona"||scheme=="secknn")?scope!="checkpoints":!(scheme=="hybrid"&&scope=="shared"&&flag=="--ks")))
                throw runtime_error(flag+" is not applicable to "+scheme+"/"+scope);
        }
        bool chunking = scheme != "hybrid" || KonaBenchConfig::MATRIX_CHUNKING;
        bool chunking_seen = false;
        for (size_t i=0;i<args.size();) {
            if (args[i]!="--chunking") { ++i; continue; }
            if (scheme!="hybrid") throw runtime_error("--chunking is only applicable to hybrid");
            if (i+1>=args.size() || (args[i+1]!="on" && args[i+1]!="off"))
                throw runtime_error("--chunking requires on or off");
            if (chunking_seen) throw runtime_error("duplicate --chunking");
            chunking_seen = true;
            chunking=args[i+1]=="on";
            args.erase(args.begin()+i,args.begin()+i+2);
        }
        KonaBenchConfig::ScopedChunking chunk_scope(chunking);
        cout<<"chunking="<<(chunking?"on":"off")
            <<"\nchunk_policy="<<(chunking?"bounded":"whole-logical-batch")
            <<"\neffective_dcf_chunk_limit="<<KonaBenchConfig::dcf_chunk()
            <<"\neffective_mul_chunk_limit="<<KonaBenchConfig::mul_chunk()<<'\n';
        int party=-1;for(size_t i=0;i+1<args.size();++i)if(args[i]=="-p"||args[i]=="--player")party=stoi(args[i+1]);
        if(party<0&&!args.empty()&&(args[0]=="0"||args[0]=="1"))party=stoi(args[0]);
        KonaBenchAffinity::bind_fixed_cpu(party);
        cout<<"benchmark_scheme="<<scheme<<"\nbenchmark_scope="<<scope<<'\n';
        const auto backend_arg=find(args.begin(),args.end(),"--kona-backend");
        const bool native_kona_control=scheme=="kona" && (backend_arg==args.end() ||
                backend_arg+1==args.end() || *(backend_arg+1)!="batched-dcf");
        cout<<"independent_exchange_transport="<<(native_kona_control?
                "native-kona-control":"framed-full-duplex-v1")<<'\n';
        if(scheme=="constantpp"){
            if(scope=="distance")args.push_back("--distance-only");
            else if(scope=="pre-vote")args.push_back("--pre-vote-only");
            else if(scope=="vote")args.push_back("--vote-only");
            else if(scope!="full")throw runtime_error("ConstantPP scope must be full/distance/pre-vote/vote");
            if(!dataset.empty())args.push_back(dataset);
        }else{
            if(!dataset.empty()){args.push_back("--dataset");args.push_back(dataset);}
            if(scheme=="secknn"||scheme=="kona"){
                string native_scope=scope=="checkpoints"?"checkpoint-vote":scope;
                if(scheme=="kona"&&scope=="topk")native_scope="topk-only";
                if(native_scope!="distance"&&native_scope!="full"&&native_scope!="checkpoint-vote"&&native_scope!="topk-vote"&&native_scope!="topk-only")
                    throw runtime_error("classic scope must be full/distance/checkpoints/topk-vote");
                for(auto& a:args)if(a=="--ks")a="--checkpoints";
                args.push_back("--scope");args.push_back(native_scope);
            }else if(scheme=="shao"){
                args.push_back("--scope");args.push_back(scope);
            }else if(scheme=="hybrid"){
                if(scope!="shared"&&scope!="full"&&scope!="topk"&&scope!="terminal"&&scope!="vote")throw runtime_error("hybrid scope must be full/shared/topk/terminal/vote");
                auto has=[&](const string& a,const string& b){return find(args.begin(),args.end(),a)!=args.end()||find(args.begin(),args.end(),b)!=args.end();};
                if(!has("-m","--mode")){args.push_back("-m");args.push_back("pruned");}
                if((scope=="full"||scope=="shared"||scope=="vote")&&!has("--vote-labels","--vote-labels")){args.push_back("--vote-labels");args.push_back("2");}
                args.push_back("--scope");args.push_back(scope=="terminal"?"terminal":"topk");
            }else throw runtime_error("unknown scheme "+scheme);
        }
        vector<const char*> native;native.push_back(argv[0]);for(const auto& a:args)native.push_back(a.c_str());
        if(scheme=="constantpp")return constantpp_engine::run(native.size(),native.data());
        if(scheme=="secknn")return secknn_engine::run(native.size(),native.data());
        if(scheme=="kona")return kona_engine::run(native.size(),native.data());
        if(scheme=="shao")return shao_engine::run(native.size(),native.data());
        if(scope=="vote")return hybrid_vote_engine::run(native.size(),native.data());
        if(scope=="shared")return hybrid_shared_engine::run(native.size(),native.data());
        if(scope=="full")return hybrid_full_engine::run(native.size(),native.data());
        return matrix_engine::run(native.size(),native.data());
    }catch(const exception& e){cerr<<"test_status=FAILED\nerror="<<e.what()<<endl;return 1;}
}
