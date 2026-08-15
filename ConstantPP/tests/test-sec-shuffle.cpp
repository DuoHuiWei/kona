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
#include "../sec-shuffle.hpp"

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
    opt.add("5200", 0, 1, 0,
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
        throw runtime_error("usage: test-sec-shuffle.x -p 0|1");
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
                offset + 19 * static_cast<std::uint64_t>(i + 1));

        out[i] = (party == 0) ? share0 : clear - share0;
    }

    return out;
}

/*
 * Deterministic local material for Algorithm 5 correctness testing.
 *
 * Permutation convention used by sec-shuffle.cpp:
 *     output[i] = input[pi[i]]
 *
 * pi0 = [2,0,3,1]
 * pi1 = [3,2,0,1]
 *
 * Therefore the reconstructed output must equal pi0(pi1(input)).
 *
 * These constants are test fixtures only. The real offline path should use
 * dealer_generate_shuffle_material().
 */
ConstantPP::ShufflePartyMaterial make_test_shuffle_material(int party)
{
    ConstantPP::ShufflePartyMaterial material;

    const vector<std::size_t> pi0 = {2, 0, 3, 1};
    const vector<std::size_t> pi1 = {3, 2, 0, 1};

    material.permutation = (party == 0) ? pi0 : pi1;
    material.a_share.assign(
            2, vector<ConstantPP::Ring>(4));
    material.b_share.assign(
            2, vector<ConstantPP::Ring>(4));

    // Column 0 masks: values/distances.
    const std::uint64_t a0_col0[4] = {10, 20, 30, 40};
    const std::uint64_t a1_col0[4] = {5, 6, 7, 8};

    // Column 1 masks: labels.
    const std::uint64_t a0_col1[4] = {1, 2, 3, 4};
    const std::uint64_t a1_col1[4] = {9, 10, 11, 12};

    /*
     * Dealer correction plaintexts computed from:
     *
     * b = pi0(pi1(a0) + a1)
     *
     * col0: [17,45,28,36]
     * col1: [12,13,14,13]
     *
     * We choose fixed b0 shares and derive b1 = b-b0.
     */
    const std::uint64_t b_plain_col0[4] = {17, 45, 28, 36};
    const std::uint64_t b_plain_col1[4] = {12, 13, 14, 13};

    const std::uint64_t b0_col0[4] = {101, 102, 103, 104};
    const std::uint64_t b0_col1[4] = {201, 202, 203, 204};

    for (std::size_t i = 0; i < 4; ++i)
    {
        if (party == 0)
        {
            material.a_share[0][i] = plain_ring(a0_col0[i]);
            material.a_share[1][i] = plain_ring(a0_col1[i]);
            material.b_share[0][i] = plain_ring(b0_col0[i]);
            material.b_share[1][i] = plain_ring(b0_col1[i]);
        }
        else
        {
            material.a_share[0][i] = plain_ring(a1_col0[i]);
            material.a_share[1][i] = plain_ring(a1_col1[i]);

            material.b_share[0][i] =
                    plain_ring(b_plain_col0[i]) -
                    plain_ring(b0_col0[i]);
            material.b_share[1][i] =
                    plain_ring(b_plain_col1[i]) -
                    plain_ring(b0_col1[i]);
        }
    }

    return material;
}

vector<vector<ConstantPP::Ring>> reveal_columns_to_p0(
        RealTwoPartyPlayer* player,
        int party,
        const vector<vector<ConstantPP::Ring>>& local)
{
    if (local.empty())
        return {};

    const std::size_t num_columns = local.size();
    const std::size_t n = local[0].size();

    if (party == 0)
    {
        vector<vector<ConstantPP::Ring>> peer;
        ConstantPP::receive_columns(
                player, num_columns, n, peer, nullptr);

        vector<vector<ConstantPP::Ring>> clear = local;
        for (std::size_t c = 0; c < num_columns; ++c)
            for (std::size_t i = 0; i < n; ++i)
                clear[c][i] += peer[c][i];

        return clear;
    }
    else
    {
        ConstantPP::send_columns(player, local, nullptr);
        return local;
    }
}

vector<std::uint64_t> apply_test_permutation(
        const vector<std::uint64_t>& input,
        const vector<std::size_t>& pi)
{
    vector<std::uint64_t> output(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = input.at(pi.at(i));
    return output;
}

void print_vector(
        const char* name,
        const vector<ConstantPP::Ring>& v)
{
    cout << name << "=";
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        if (i)
            cout << ",";
        cout << v[i].get_limb(0);
    }
    cout << endl;
}

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);

    if (playerno != 0 && playerno != 1)
        throw runtime_error("player must be 0 or 1");

    RealTwoPartyPlayer* player = start_networking();

    /*
     * Row-aligned test data:
     *
     * value = [30,10,40,20]
     * label = [ 3, 1, 4, 2]
     *
     * Every value has a matching label that makes pairing errors obvious.
     */
    const vector<std::uint64_t> values_plain = {30, 10, 40, 20};
    const vector<std::uint64_t> labels_plain = {3, 1, 4, 2};

    const auto value_share =
            make_test_share(values_plain, playerno, 1000);
    const auto label_share =
            make_test_share(labels_plain, playerno, 2000);

    const auto material =
            make_test_shuffle_material(playerno);

    ConstantPP::ProtocolStats protocol_stats;

    auto shuffled_local =
            ConstantPP::sec_shuffle_value_label(
                    player,
                    playerno,
                    value_share,
                    label_share,
                    material,
                    &protocol_stats);

    /*
     * Correctness opening happens AFTER SecShuffle and is intentionally
     * excluded from protocol_stats.
     */
    auto revealed =
            reveal_columns_to_p0(
                    player, playerno, shuffled_local);

    bool ok = true;
    if (playerno == 0)
    {
        const vector<std::size_t> pi0 = {2, 0, 3, 1};
        const vector<std::size_t> pi1 = {3, 2, 0, 1};

        const auto expected_values =
                apply_test_permutation(
                        apply_test_permutation(values_plain, pi1),
                        pi0);
        const auto expected_labels =
                apply_test_permutation(
                        apply_test_permutation(labels_plain, pi1),
                        pi0);

        if (revealed.size() != 2 ||
                revealed[0].size() != expected_values.size() ||
                revealed[1].size() != expected_labels.size())
        {
            ok = false;
        }
        else
        {
            for (std::size_t i = 0; i < expected_values.size(); ++i)
            {
                if (revealed[0][i].get_limb(0) != expected_values[i])
                    ok = false;
                if (revealed[1][i].get_limb(0) != expected_labels[i])
                    ok = false;

                // Explicit distance/value-label pairing check.
                if (revealed[0][i].get_limb(0) !=
                        10 * revealed[1][i].get_limb(0))
                    ok = false;
            }
        }

        cout << "test=constantpp_sec_shuffle" << endl;
        if (revealed.size() >= 2)
        {
            print_vector("revealed_values", revealed[0]);
            print_vector("revealed_labels", revealed[1]);
        }
        else
        {
            cout << "revealed_columns=" << revealed.size() << endl;
        }
        cout << "sec_shuffle_logical_rounds="
             << protocol_stats.logical_rounds << endl;
        cout << "sec_shuffle_send_calls="
             << protocol_stats.send_calls << endl;
        cout << "sec_shuffle_receive_calls="
             << protocol_stats.receive_calls << endl;
        cout << "sec_shuffle_payload_bytes_sent="
             << protocol_stats.payload_bytes_sent << endl;
        cout << "pairing_preserved="
             << (ok ? "YES" : "NO") << endl;
        cout << "result=" << (ok ? "PASS" : "FAIL") << endl;
    }

    delete player;
    return ok ? 0 : 1;
}
