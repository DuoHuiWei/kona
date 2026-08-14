#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../Networking/Player.h"
#include "../../Networking/Server.h"
#include "../../Tools/ezOptionParser.h"
#include "../constantpp-comm.hpp"
#include "../sec-ed.hpp"

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
    opt.add("5100", 0, 1, 0,
            "Port number base to attempt to start connections from",
            "-pn", "--portnumbase");
    opt.add("", 0, 1, 0,
            "Player number",
            "-p", "--player");
    opt.add("", 0, 1, 0,
            "Port to listen on",
            "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0,
            "Startup host",
            "-h", "--hostname");
    opt.add("", 0, 1, 0,
            "Party hostname file",
            "-ip", "--ip-file-name");

    opt.parse(argc, argv);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else if (argc > 1)
        std::sscanf(argv[1], "%d", &playerno);
    else
        throw runtime_error("usage: test-sec-ed.x -p 0|1");
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
    {
        if (my_port != Names::DEFAULT_PORT)
            throw runtime_error(
                    "cannot set port number when using IP file");
        player_names.init(playerno, pnbase, ip_file_name, 2);
    }
    else
    {
        Server::start_networking(
                player_names, playerno, 2,
                hostname, pnbase, my_port);
    }

    return new RealTwoPartyPlayer(
            player_names, 1 - playerno, 0);
}

ConstantPP::Ring plain_ring(std::uint64_t x)
{
    return ConstantPP::Ring(static_cast<mp_limb_t>(x));
}

/*
 * Deterministic additive sharing for a correctness test only.
 *
 * Both test processes know the public test vector. This helper is NOT
 * protocol code and must not be reused by benchmark or production paths.
 */
vector<ConstantPP::Ring> make_test_share(
        const vector<std::uint64_t>& plain,
        int party,
        std::uint64_t offset)
{
    vector<ConstantPP::Ring> out(plain.size());

    for (std::size_t i = 0; i < plain.size(); ++i)
    {
        ConstantPP::Ring clear = plain_ring(plain[i]);
        ConstantPP::Ring share0 = plain_ring(
                offset + 17 * static_cast<std::uint64_t>(i + 1));

        out[i] = (party == 0) ? share0 : clear - share0;
    }

    return out;
}

/*
 * The first ConstantPP baseline intentionally reuses the same SecED
 * preprocessing mask across dimensions, matching upstream secED_nDim().
 *
 * Global material:
 *     a = 7
 *     c = a^2 = 49
 *
 * Additive shares:
 *     a0=3, a1=4
 *     c0=20, c1=29
 */
ConstantPP::SecEdPartyMaterial make_test_ed_material(int party)
{
    ConstantPP::SecEdPartyMaterial material;
    if (party == 0)
    {
        material.a_share = plain_ring(3);
        material.c_share = plain_ring(20);
    }
    else
    {
        material.a_share = plain_ring(4);
        material.c_share = plain_ring(29);
    }
    return material;
}

ConstantPP::Ring reveal_to_p0(
        RealTwoPartyPlayer* player,
        int party,
        const ConstantPP::Ring& local_share)
{
    if (party == 0)
    {
        octetStream os;
        player->receive(os);
        ConstantPP::Ring peer_share;
        peer_share.unpack(os);
        return local_share + peer_share;
    }
    else
    {
        octetStream os;
        local_share.pack(os);
        player->send(os);
        return local_share;
    }
}

std::uint64_t plaintext_squared_distance(
        const vector<std::uint64_t>& x,
        const vector<std::uint64_t>& y)
{
    if (x.size() != y.size())
        throw runtime_error("plaintext dimension mismatch");

    std::uint64_t acc = 0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const std::int64_t diff =
                static_cast<std::int64_t>(x[i]) -
                static_cast<std::int64_t>(y[i]);
        acc += static_cast<std::uint64_t>(diff * diff);
    }
    return acc;
}

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);

    if (playerno != 0 && playerno != 1)
        throw runtime_error("player must be 0 or 1");

    RealTwoPartyPlayer* player = start_networking();

    /*
     * Chosen so the expected result is easy to inspect:
     *
     * x = [10, 20, 30, 40]
     * y = [ 7, 25, 28, 35]
     * diff = [3, -5, 2, 5]
     * d^2 = 9 + 25 + 4 + 25 = 63
     */
    const vector<std::uint64_t> x_plain = {10, 20, 30, 40};
    const vector<std::uint64_t> y_plain = {7, 25, 28, 35};

    const auto x_share =
            make_test_share(x_plain, playerno, 100);
    const auto y_share =
            make_test_share(y_plain, playerno, 700);
    const auto material =
            make_test_ed_material(playerno);

    ConstantPP::ProtocolStats protocol_stats;
    ConstantPP::Ring distance_share =
            ConstantPP::sec_ed_n_dim(
                    player,
                    playerno,
                    x_share,
                    y_share,
                    material,
                    &protocol_stats);

    /*
     * Correctness opening happens AFTER SecED and is intentionally
     * excluded from protocol_stats.
     */
    ConstantPP::Ring revealed =
            reveal_to_p0(player, playerno, distance_share);

    bool ok = true;
    if (playerno == 0)
    {
        const std::uint64_t expected =
                plaintext_squared_distance(x_plain, y_plain);
        const std::uint64_t actual = revealed.get_limb(0);

        ok = (actual == expected);

        cout << "test=constantpp_sec_ed" << endl;
        cout << "expected_distance=" << expected << endl;
        cout << "revealed_distance=" << actual << endl;
        cout << "sec_ed_logical_rounds="
             << protocol_stats.logical_rounds << endl;
        cout << "sec_ed_send_calls="
             << protocol_stats.send_calls << endl;
        cout << "sec_ed_receive_calls="
             << protocol_stats.receive_calls << endl;
        cout << "sec_ed_payload_bytes_sent="
             << protocol_stats.payload_bytes_sent << endl;
        cout << "result=" << (ok ? "PASS" : "FAIL") << endl;
    }

    delete player;
    return ok ? 0 : 1;
}
