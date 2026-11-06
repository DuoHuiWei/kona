#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "../Math/FixedVec.h"
#include "../Math/Z2k.hpp"
#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"

using namespace std;

namespace
{
const int K = 64;
using Ring = Z2<K>;
using Aby2Share = FixedVec<Ring, 2>;

int playerno = -1;
ez::ezOptionParser opt;

struct KonaDistanceInputs
{
    size_t features = 0;
    size_t n = 0;
    vector<vector<Aby2Share>> database;
    vector<Aby2Share> query;
};

ifstream input_file(const string& path)
{
    ifstream in(path);
    if (!in)
        throw runtime_error("cannot open " + path);
    return in;
}

Ring read_ring(istream& in, const string& field)
{
    long long value = 0;
    if (!(in >> value) || value < 0)
        throw runtime_error("invalid value in " + field);
    return Ring(static_cast<mp_limb_t>(value));
}

string dataset_path(string name)
{
    if (name.find('/') != string::npos)
        return name;
    if (name.size() < 5 || name.substr(name.size() - 5) != "-data")
        name += "-data";
    return "Player-Data/Knn-Data/knn-1/" + name;
}

void parse_options(int argc, const char** argv, string& dataset, int& trials,
        string& network_env)
{
    opt.add("5000", 0, 1, 0, "Port number base", "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("localhost", 0, 1, 0, "Host", "-h", "--hostname");
    opt.add("arcene", 0, 1, 0, "Dataset", "-d", "--dataset");
    opt.add("3", 0, 1, 0, "Trials", "-t", "--trials");
    opt.add("localhost-no-delay", 0, 1, 0, "Network label", "--network-env");
    opt.parse(argc, argv);
    opt.get("--player")->getInt(playerno);
    opt.get("--dataset")->getString(dataset);
    opt.get("--trials")->getInt(trials);
    opt.get("--network-env")->getString(network_env);
    if ((playerno != 0 && playerno != 1) || trials < 1)
        throw runtime_error("invalid player or trial count");
}

RealTwoPartyPlayer* start_networking()
{
    int port = 0;
    string hostname;
    opt.get("--portnumbase")->getInt(port);
    opt.get("--hostname")->getString(hostname);
    Names names;
    Server::start_networking(names, playerno, 2, hostname, port,
            Names::DEFAULT_PORT);
    return new RealTwoPartyPlayer(names, 1 - playerno, 0);
}

/* Reproduce Kona's zero-material preprocessing and ABY2 share setup. */
KonaDistanceInputs prepare_kona_inputs(
        const string& path, int party, RealTwoPartyPlayer* player)
{
    KonaDistanceInputs result;
    size_t query_count = 0;
    auto meta = input_file(path + "/Knn-meta");
    if (!(meta >> result.features >> result.n >> query_count) || query_count != 1)
        throw runtime_error("benchmark requires query_count=1");

    result.database.assign(result.n, vector<Aby2Share>(result.features));
    result.query.resize(result.features);

    if (party == 0)
    {
        auto train = input_file(path + "/P0-0-X-Train");
        octetStream os;
        for (size_t i = 0; i < result.n; ++i)
            for (size_t j = 0; j < result.features; ++j)
            {
                result.database[i][j][0] = read_ring(train, "P0-0-X-Train");
                result.database[i][j][1] = Ring(0);
                result.database[i][j][0].pack(os);
            }
        player->send(os);

        os.clear();
        player->receive(os);
        for (size_t j = 0; j < result.features; ++j)
        {
            result.query[j][0].unpack(os);
            result.query[j][1] = Ring(0);
        }
    }
    else
    {
        octetStream os;
        player->receive(os);
        for (size_t i = 0; i < result.n; ++i)
            for (size_t j = 0; j < result.features; ++j)
            {
                result.database[i][j][0].unpack(os);
                result.database[i][j][1] = Ring(0);
            }

        auto query = input_file(path + "/P1-0-X-Test");
        os.clear();
        for (size_t j = 0; j < result.features; ++j)
        {
            result.query[j][0] = read_ring(query, "P1-0-X-Test");
            result.query[j][1] = Ring(0);
            result.query[j][0].pack(os);
        }
        player->send(os);
    }
    return result;
}

/* Exact local arithmetic from Kona's compute_ESD_two_sample(). */
Ring compute_esd_two_sample(
        const vector<Aby2Share>& train,
        const vector<Aby2Share>& query,
        int party)
{
    Ring result(0);
    Ring square_of_public_component(0);
    for (size_t j = 0; j < train.size(); ++j)
    {
        const Aby2Share delta = train[j] - query[j];
        square_of_public_component += delta[0] * delta[0];
        result -= Ring(2) * delta[0] * delta[1];
        // Kona's per-coordinate correction triple is zero in this path.
    }
    if (party == 1)
        result += square_of_public_component;
    return result;
}

void compute_esd_for_one_query(
        const KonaDistanceInputs& inputs,
        int party,
        vector<Ring>& distance_shares)
{
    distance_shares.resize(inputs.n);
    for (size_t i = 0; i < inputs.n; ++i)
        distance_shares[i] =
                compute_esd_two_sample(inputs.database[i], inputs.query, party);
}

void barrier(RealTwoPartyPlayer* player, int party)
{
    octetStream os;
    Ring(0).pack(os);
    if (party == 0)
    {
        player->send(os);
        os.clear();
        player->receive(os);
    }
    else
    {
        octetStream received;
        player->receive(received);
        player->send(os);
    }
}

uint64_t transport_actions(const NamedCommStats& stats)
{
    uint64_t actions = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        actions += static_cast<uint64_t>(it->second.rounds);
    return actions;
}
} // namespace

int main(int argc, const char** argv)
{
    string dataset;
    string network_env;
    int trials = 0;
    parse_options(argc, argv, dataset, trials, network_env);
    RealTwoPartyPlayer* player = start_networking();

    // IO, material setup, and share distribution are outside online timing.
    const auto inputs =
            prepare_kona_inputs(dataset_path(dataset), playerno, player);
    vector<Ring> distance_shares;
    vector<double> times;
    times.reserve(trials);

    for (int trial = 1; trial <= trials; ++trial)
    {
        barrier(player, playerno); // excluded from timer and communication delta
        const auto comm_before = player->total_comm();
        const auto start = chrono::steady_clock::now();
        compute_esd_for_one_query(inputs, playerno, distance_shares);
        const auto end = chrono::steady_clock::now();
        const auto comm_delta = player->total_comm() - comm_before;
        const double ms = chrono::duration<double, milli>(end - start).count();

        if (distance_shares.size() != inputs.n || comm_delta.sent != 0 ||
                transport_actions(comm_delta) != 0)
            throw runtime_error("Kona Distance unexpectedly communicated");

        if (playerno == 0)
        {
            times.push_back(ms);
            cout << fixed << setprecision(6)
                 << "dataset=" << dataset << '\n'
                 << "N=" << inputs.n << '\n'
                 << "features=" << inputs.features << '\n'
                 << "query_count=1\n"
                 << "trial=" << trial << '\n'
                 << "distance_ms=" << ms << '\n'
                 << "distance_sent_bytes=0\n"
                 << "distance_rtt_rounds=0\n"
                 << "distance_logical_rounds=0\n"
                 << "timer_scope=shares_ready_to_distance_shares_ready\n"
                 << "network_env=" << network_env << '\n'
                 << "correctness=NOT_CHECKED\n\n";
        }
    }

    if (playerno == 0)
    {
        const double mean = accumulate(times.begin(), times.end(), 0.0) /
                times.size();
        double variance = 0;
        for (double value : times)
            variance += (value - mean) * (value - mean);
        const double sample_sd = times.size() > 1
                ? sqrt(variance / (times.size() - 1)) : 0;
        cout << fixed << setprecision(6)
             << "dataset=" << dataset << '\n'
             << "trials=" << trials << '\n'
             << "distance_mean_ms=" << mean << '\n'
             << "distance_sample_sd_ms=" << sample_sd << '\n'
             << "distance_mean_sent_bytes=0.000000\n"
             << "distance_mean_rtt_rounds=0.000000\n"
             << "distance_mean_logical_rounds=0.000000\n";
    }

    delete player;
    return 0;
}
