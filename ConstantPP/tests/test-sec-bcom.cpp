#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../Machines/kona-dcf-compare.hpp"
#include "../../Networking/Player.h"
#include "../../Networking/Server.h"
#include "../../Tools/ezOptionParser.h"
#include "../sec-bcom.hpp"

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
    opt.add("5300", 0, 1, 0, "Port number base",
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
        throw runtime_error("usage: test-sec-bcom.x -p 0|1");
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
        int party)
{
    vector<ConstantPP::Ring> out(plain.size());
    for (std::size_t i = 0; i < plain.size(); ++i)
    {
        const auto clear = ring(plain[i]);
        const auto s0 = ring(1000 + 37 * i);
        out[i] = party == 0 ? s0 : clear - s0;
    }
    return out;
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
        for (std::size_t i = 0; i < local.size(); ++i)
        {
            ConstantPP::Ring peer;
            peer.unpack(os);
            out[i] = (local[i] + peer).get_limb(0);
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
        const vector<std::uint64_t>& v)
{
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
     * Unique-value case:
     * x = [3,1,4,2]
     *
     * less-count  = #{j != i | x_i < x_j}
     *             = [1,3,0,2]
     *
     * greater-count
     *             = [2,0,3,1]
     */
    const vector<std::uint64_t> plain = {3, 1, 4, 2};
    const auto shares = make_share(plain, playerno);
    const auto plan =
            ConstantPP::make_sec_bcom_upper_triangle_plan(
                    plain.size());

    KonaDcfCompare::Compare64<64> dcf_compare(
            player, playerno);

    ConstantPP::ProtocolStats less_protocol;
    ConstantPP::SecBComStats less_stats;
    const auto less_share =
            ConstantPP::sec_bcom_kona_dcf(
                    player,
                    playerno,
                    shares,
                    ConstantPP::CompareOp::Less,
                    plan,
                    dcf_compare,
                    &less_protocol,
                    &less_stats);

    const auto less_open =
            reveal_to_p0(player, playerno, less_share);

    ConstantPP::ProtocolStats greater_protocol;
    ConstantPP::SecBComStats greater_stats;
    const auto greater_share =
            ConstantPP::sec_bcom_kona_dcf(
                    player,
                    playerno,
                    shares,
                    ConstantPP::CompareOp::Greater,
                    plan,
                    dcf_compare,
                    &greater_protocol,
                    &greater_stats);

    const auto greater_open =
            reveal_to_p0(player, playerno, greater_share);

    bool ok = true;
    if (playerno == 0)
    {
        const vector<std::uint64_t> expected_less =
                {1, 3, 0, 2};
        const vector<std::uint64_t> expected_greater =
                {2, 0, 3, 1};

        ok = less_open == expected_less &&
             greater_open == expected_greater &&
             less_protocol.logical_rounds == 1 &&
             greater_protocol.logical_rounds == 1 &&
             less_stats.unordered_pairs == 6 &&
             greater_stats.unordered_pairs == 6;

        cout << "test=constantpp_sec_bcom_kona_dcf" << endl;
        print_u64_vector("less_counts", less_open);
        print_u64_vector("greater_counts", greater_open);
        cout << "upper_triangle_pairs="
             << less_stats.unordered_pairs << endl;
        cout << "less_logical_rounds="
             << less_protocol.logical_rounds << endl;
        cout << "greater_logical_rounds="
             << greater_protocol.logical_rounds << endl;
        cout << "less_kona_dcf_evaluate_calls="
             << less_stats.kona_dcf_evaluate_calls << endl;
        cout << "greater_kona_dcf_evaluate_calls="
             << greater_stats.kona_dcf_evaluate_calls << endl;
        cout << "result=" << (ok ? "PASS" : "FAIL") << endl;
    }

    delete player;
    return ok ? 0 : 1;
}
