#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../../Machines/kona-dcf-compare.hpp"
#include "../../Networking/Player.h"
#include "../../Networking/Server.h"
#include "../../Tools/ezOptionParser.h"
#include "../sec-kmin.hpp"

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
    opt.add("5400", 0, 1, 0, "Port number base",
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
        throw runtime_error("usage: test-sec-kmin.x -p 0|1");
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
        player_names.init(
                playerno, pnbase, ip_file_name, 2);
    else
        Server::start_networking(
                player_names, playerno, 2,
                hostname, pnbase, my_port);

    return new RealTwoPartyPlayer(
            player_names, 1 - playerno, 0);
}

ConstantPP::Ring ring(std::uint64_t x)
{
    return ConstantPP::Ring(
            static_cast<mp_limb_t>(x));
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
        out[i] = party == 0
                ? s0
                : clear - s0;
    }
    return out;
}

/*
 * Non-identity deterministic shuffle fixture.
 * Same convention and corrected fixture as the strengthened shuffle test.
 */
ConstantPP::ShufflePartyMaterial
make_shuffle_material(int party)
{
    ConstantPP::ShufflePartyMaterial material;

    const vector<std::size_t> pi0 = {2, 0, 3, 1};
    const vector<std::size_t> pi1 = {3, 2, 0, 1};

    material.permutation =
            party == 0 ? pi0 : pi1;
    material.a_share.assign(
            2, vector<ConstantPP::Ring>(4));
    material.b_share.assign(
            2, vector<ConstantPP::Ring>(4));

    const std::uint64_t a0_c0[4] =
            {10, 20, 30, 40};
    const std::uint64_t a1_c0[4] =
            {5, 6, 7, 8};

    const std::uint64_t a0_c1[4] =
            {1, 2, 3, 4};
    const std::uint64_t a1_c1[4] =
            {9, 10, 11, 12};

    const std::uint64_t b_plain_c0[4] =
            {17, 45, 28, 36};
    const std::uint64_t b_plain_c1[4] =
            {12, 13, 14, 13};

    const std::uint64_t b0_c0[4] =
            {101, 102, 103, 104};
    const std::uint64_t b0_c1[4] =
            {201, 202, 203, 204};

    for (std::size_t i = 0; i < 4; ++i)
    {
        if (party == 0)
        {
            material.a_share[0][i] =
                    ring(a0_c0[i]);
            material.a_share[1][i] =
                    ring(a0_c1[i]);
            material.b_share[0][i] =
                    ring(b0_c0[i]);
            material.b_share[1][i] =
                    ring(b0_c1[i]);
        }
        else
        {
            material.a_share[0][i] =
                    ring(a1_c0[i]);
            material.a_share[1][i] =
                    ring(a1_c1[i]);
            material.b_share[0][i] =
                    ring(b_plain_c0[i]) -
                    ring(b0_c0[i]);
            material.b_share[1][i] =
                    ring(b_plain_c1[i]) -
                    ring(b0_c1[i]);
        }
    }

    return material;
}

vector<std::uint64_t> reveal_to_p0(
        RealTwoPartyPlayer* player,
        int party,
        const vector<ConstantPP::Ring>& local)
{
    if (party == 0)
    {
        octetStream os;
        player->receive(os);

        vector<std::uint64_t> out(local.size());
        for (std::size_t i = 0;
                i < local.size(); ++i)
        {
            ConstantPP::Ring peer;
            peer.unpack(os);
            out[i] =
                    (local[i] + peer).get_limb(0);
        }
        return out;
    }

    octetStream os;
    for (const auto& x : local)
        x.pack(os);
    player->send(os);
    return {};
}

void print_u64_vector(
        const char* name,
        vector<std::uint64_t> v)
{
    std::sort(v.begin(), v.end());

    cout << name << "=";
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        if (i)
            cout << ",";
        cout << v[i];
    }
    cout << endl;
}

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    RealTwoPartyPlayer* player = start_networking();

    /*
     * Distances and labels:
     *
     * distance = [30,10,40,20]
     * label    = [ 3, 1, 4, 2]
     *
     * k=2 => nearest labels are {1,2}.
     */
    const vector<std::uint64_t> distance =
            {30, 10, 40, 20};
    const vector<std::uint64_t> label =
            {3, 1, 4, 2};

    const auto distance_share =
            make_share(distance, playerno, 1000);
    const auto label_share =
            make_share(label, playerno, 2000);

    const auto shuffle_material =
            make_shuffle_material(playerno);

    const auto bcom_plan =
            ConstantPP::make_sec_bcom_upper_triangle_plan(
                    distance.size());

    KonaDcfCompare::Compare64<64> dcf_compare(
            player, playerno);

    ConstantPP::ProtocolStats protocol_stats;
    ConstantPP::SecBComStats bcom_stats;
    ConstantPP::SecKMinStats kmin_stats;

    const auto selected_share =
            ConstantPP::sec_kmin_kona_dcf(
                    player,
                    playerno,
                    distance_share,
                    label_share,
                    2,
                    shuffle_material,
                    bcom_plan,
                    dcf_compare,
                    &protocol_stats,
                    &bcom_stats,
                    &kmin_stats);

    const auto selected_open =
            reveal_to_p0(
                    player,
                    playerno,
                    selected_share);

    bool ok = true;
    if (playerno == 0)
    {
        vector<std::uint64_t> expected = {1, 2};
        vector<std::uint64_t> actual =
                selected_open;

        std::sort(expected.begin(), expected.end());
        std::sort(actual.begin(), actual.end());

        ok = actual == expected &&
             protocol_stats.logical_rounds == 5 &&
             kmin_stats.selected_count == 2 &&
             bcom_stats.unordered_pairs == 6 &&
             kmin_stats.threshold_compare_pairs == 4;

        cout << "test=constantpp_sec_kmin" << endl;
        print_u64_vector(
                "selected_labels_sorted",
                selected_open);
        cout << "logical_rounds="
             << protocol_stats.logical_rounds << endl;
        cout << "bcom_upper_triangle_pairs="
             << bcom_stats.unordered_pairs << endl;
        cout << "threshold_compare_pairs="
             << kmin_stats.threshold_compare_pairs << endl;
        cout << "threshold_dcf_evaluate_calls="
             << kmin_stats.threshold_dcf_evaluate_calls << endl;
        cout << "selected_count="
             << kmin_stats.selected_count << endl;
        cout << "result="
             << (ok ? "PASS" : "FAIL") << endl;
    }

    delete player;
    return ok ? 0 : 1;
}
