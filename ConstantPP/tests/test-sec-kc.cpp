#include "../../Machines/kona-bench-affinity.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../Machines/kona-dcf-compare.hpp"
#include "../../Networking/Player.h"
#include "../../Networking/Server.h"
#include "../../Tools/ezOptionParser.h"
#include "../sec-kc.hpp"

using std::cout;
using std::endl;
using std::runtime_error;
using std::string;
using std::vector;

namespace
{

int playerno = 0;
ez::ezOptionParser opt;

void parse_argv(int argc, const char** argv)
{
    opt.add("5800", 0, 1, 0, "Port number base",
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
    else if (argc > 1)
        std::sscanf(argv[1], "%d", &playerno);
    else
        throw runtime_error("usage: test-sec-kc.x -p 0|1");
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

ConstantPP::ShufflePartyMaterial make_shuffle_material(int party)
{
    ConstantPP::ShufflePartyMaterial material;

    const vector<std::size_t> pi0 = {2, 0, 3, 1};
    const vector<std::size_t> pi1 = {3, 2, 0, 1};

    material.permutation = party == 0 ? pi0 : pi1;
    material.a_share.assign(2, vector<ConstantPP::Ring>(4));
    material.b_share.assign(2, vector<ConstantPP::Ring>(4));

    const std::uint64_t a0_c0[4] = {10, 20, 30, 40};
    const std::uint64_t a1_c0[4] = {5, 6, 7, 8};
    const std::uint64_t a0_c1[4] = {1, 2, 3, 4};
    const std::uint64_t a1_c1[4] = {9, 10, 11, 12};

    const std::uint64_t b_plain_c0[4] = {17, 45, 28, 36};
    const std::uint64_t b_plain_c1[4] = {12, 13, 14, 13};
    const std::uint64_t b0_c0[4] = {101, 102, 103, 104};
    const std::uint64_t b0_c1[4] = {201, 202, 203, 204};

    for (std::size_t i = 0; i < 4; ++i)
    {
        if (party == 0)
        {
            material.a_share[0][i] = ring(a0_c0[i]);
            material.a_share[1][i] = ring(a0_c1[i]);
            material.b_share[0][i] = ring(b0_c0[i]);
            material.b_share[1][i] = ring(b0_c1[i]);
        }
        else
        {
            material.a_share[0][i] = ring(a1_c0[i]);
            material.a_share[1][i] = ring(a1_c1[i]);
            material.b_share[0][i] = ring(b_plain_c0[i]) - ring(b0_c0[i]);
            material.b_share[1][i] = ring(b_plain_c1[i]) - ring(b0_c1[i]);
        }
    }

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

vector<std::uint64_t> squared_distances(
        const vector<std::uint64_t>& query,
        const vector<vector<std::uint64_t>>& database)
{
    vector<std::uint64_t> out(database.size());
    for (std::size_t i = 0; i < database.size(); ++i)
    {
        std::uint64_t acc = 0;
        for (std::size_t j = 0; j < query.size(); ++j)
        {
            const std::int64_t diff =
                    static_cast<std::int64_t>(database[i][j]) -
                    static_cast<std::int64_t>(query[j]);
            acc += static_cast<std::uint64_t>(diff * diff);
        }
        out[i] = acc;
    }
    return out;
}

vector<std::uint64_t> expected_top_k_labels(
        const vector<std::uint64_t>& distances,
        const vector<std::uint64_t>& labels,
        std::size_t k)
{
    vector<std::size_t> idx(distances.size());
    for (std::size_t i = 0; i < idx.size(); ++i)
        idx[i] = i;

    std::stable_sort(
            idx.begin(),
            idx.end(),
            [&](std::size_t a, std::size_t b) {
                return distances[a] < distances[b];
            });

    vector<std::uint64_t> out;
    out.reserve(k);
    for (std::size_t i = 0; i < k; ++i)
        out.push_back(labels[idx[i]]);
    return out;
}

vector<std::uint64_t> expected_frequency_by_label(
        const vector<std::uint64_t>& labels)
{
    std::map<std::uint64_t, std::uint64_t> counts;
    for (auto label : labels)
        ++counts[label];

    vector<std::uint64_t> out;
    for (const auto& kv : counts)
        out.push_back(kv.second);
    return out;
}

void print_u64_vector(
        const char* name,
        const vector<std::uint64_t>& values)
{
    cout << name << "=";
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i)
            cout << ",";
        cout << values[i];
    }
    cout << endl;
}

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    KonaBenchAffinity::bind_fixed_cpu(playerno);
    RealTwoPartyPlayer* player = start_networking();

    const vector<std::uint64_t> query = {0, 0};
    const vector<vector<std::uint64_t>> database = {
        {0, 0},
        {1, 0},
        {8, 8},
        {9, 9},
    };
    const vector<std::uint64_t> labels = {1, 1, 2, 2};
    const std::size_t k = 3;

    const auto query_share = make_share(query, playerno, 7000);
    const auto database_share = make_database_share(database, playerno);
    const auto label_share = make_share(labels, playerno, 9000);

    const auto ed_material = make_ed_material(playerno);
    const auto shuffle_material = make_shuffle_material(playerno);
    const auto bcom_plan =
            ConstantPP::make_sec_bcom_upper_triangle_plan(labels.size());
    const auto triple = make_triple(playerno);

    KonaDcfCompare::Compare64<64> dcf_compare(player, playerno);
    ConstantPP::ProtocolStats protocol_stats;
    ConstantPP::SecKCStats kc_stats;

    const auto result_share =
            ConstantPP::sec_kc_kona(
                    player,
                    playerno,
                    query_share,
                    database_share,
                    label_share,
                    k,
                    ed_material,
                    shuffle_material,
                    bcom_plan,
                    triple,
                    dcf_compare,
                    &protocol_stats,
                    &kc_stats);

    const auto revealed_label =
            reveal_to_p0(player, playerno, result_share);

    bool ok = true;
    if (playerno == 0)
    {
        const auto distances = squared_distances(query, database);
        const auto selected_labels =
                expected_top_k_labels(distances, labels, k);
        const auto frequency =
                expected_frequency_by_label(selected_labels);

        ok = revealed_label == 1 &&
             kc_stats.distance_count == database.size() &&
             kc_stats.kmin.selected_count == k &&
             kc_stats.kmin_bcom.unordered_pairs == 6 &&
             kc_stats.fre.upper_triangle_eq_predicates == 3 &&
             kc_stats.max.secure_mul_pairs == k &&
             kc_stats.logical_rounds == protocol_stats.logical_rounds;

        cout << "test=constantpp_sec_kc" << endl;
        print_u64_vector("distances", distances);
        print_u64_vector("selected_labels", selected_labels);
        print_u64_vector("frequency", frequency);
        cout << "revealed_label=" << revealed_label << endl;
        cout << "logical_rounds="
             << protocol_stats.logical_rounds << endl;
        cout << "distance_count="
             << kc_stats.distance_count << endl;
        cout << "kmin_selected_count="
             << kc_stats.kmin.selected_count << endl;
        cout << "fre_upper_triangle_eq_predicates="
             << kc_stats.fre.upper_triangle_eq_predicates << endl;
        cout << "max_secure_mul_pairs="
             << kc_stats.max.secure_mul_pairs << endl;
        cout << "result=" << (ok ? "PASS" : "FAIL") << endl;
    }

    delete player;
    return ok ? 0 : 1;
}
