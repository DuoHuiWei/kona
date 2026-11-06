#include "kona-bench-affinity.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "../ConstantPP/data-loader.hpp"
#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "kona-nhss-distance.hpp"
#include "kona-share-conversion.hpp"

namespace
{

struct Options
{
    int party = -1;
    std::string dataset = "spambase";
    std::size_t limit_train = 32;
    std::size_t limit_features = 16;
    std::vector<std::size_t> dimension_batch_sizes = {1, 8, 16, 0};
    int repeats = 3;
    unsigned lambda = 512;
    int port_base = 15000;
    std::string hostname = "localhost";
};

struct Trial
{
    std::size_t batch_size = 0;
    int repeat = 0;
    int order = 0;
    double milliseconds = 0;
    std::size_t sent_bytes = 0;
    std::size_t transport_rounds = 0;
};

double elapsed_ms(const std::chrono::steady_clock::time_point& begin,
        const std::chrono::steady_clock::time_point& end)
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::size_t total_rounds(const NamedCommStats& stats)
{
    std::size_t rounds = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        rounds += it->second.rounds;
    return rounds;
}

std::vector<std::size_t> parse_batch_sizes(const std::string& text)
{
    std::vector<std::size_t> out;
    std::size_t begin = 0;
    while (begin <= text.size())
    {
        const std::size_t comma = text.find(',', begin);
        const std::string token = text.substr(
                begin, comma == std::string::npos ? comma : comma - begin);
        if (token.empty())
            throw std::runtime_error("empty dimension batch size");
        const long long value = std::stoll(token);
        if (value < 0)
            throw std::runtime_error("dimension batch size must be non-negative");
        out.push_back(static_cast<std::size_t>(value));
        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }
    if (out.empty())
        throw std::runtime_error("at least one dimension batch size is required");
    return out;
}

std::string dataset_path(const std::string& name)
{
    if (name.find('/') != std::string::npos)
        return name;
    if (name.size() >= 5 && name.substr(name.size() - 5) == "-data")
        return "ConstantPP/data/" + name;
    return "ConstantPP/data/" + name + "-data";
}

Options parse_options(int argc, const char** argv)
{
    ez::ezOptionParser parser;
    parser.add("", 0, 1, 0, "Party number", "-p", "--player");
    parser.add("spambase", 0, 1, 0, "Dataset name/path", "-d", "--dataset");
    parser.add("32", 0, 1, 0, "Training row limit", "--limit-train");
    parser.add("16", 0, 1, 0, "Feature limit", "--limit-features");
    parser.add("1,8,16,0", 0, 1, 0, "Comma-separated dimension batches",
            "-b", "--dimension-batch-sizes");
    parser.add("3", 0, 1, 0, "Interleaved repeats", "-r", "--repeats");
    parser.add("512", 0, 1, 0, "NHSS modulus bits", "--lambda");
    parser.add("15000", 0, 1, 0, "Port base", "-pn", "--portnumbase");
    parser.add("localhost", 0, 1, 0, "Hostname", "-h", "--hostname");
    parser.parse(argc, argv);

    Options options;
    if (parser.isSet("--player"))
        parser.get("--player")->getInt(options.party);
    else if (argc > 1)
        options.party = std::stoi(argv[1]);
    if (options.party != 0 && options.party != 1)
        throw std::runtime_error("party must be 0 or 1");

    long long limit_train = 0, limit_features = 0;
    int lambda = 0;
    std::string batches;
    parser.get("--dataset")->getString(options.dataset);
    parser.get("--limit-train")->getLongLong(limit_train);
    parser.get("--limit-features")->getLongLong(limit_features);
    parser.get("--dimension-batch-sizes")->getString(batches);
    parser.get("--repeats")->getInt(options.repeats);
    parser.get("--lambda")->getInt(lambda);
    parser.get("--portnumbase")->getInt(options.port_base);
    parser.get("--hostname")->getString(options.hostname);
    if (limit_train < 0 || limit_features < 0 || options.repeats <= 0 ||
            lambda < 128)
        throw std::runtime_error("invalid benchmark option");
    options.limit_train = static_cast<std::size_t>(limit_train);
    options.limit_features = static_cast<std::size_t>(limit_features);
    options.lambda = static_cast<unsigned>(lambda);
    options.dimension_batch_sizes = parse_batch_sizes(batches);
    return options;
}

void apply_limits(ConstantPP::ConstantPPDataset& dataset,
        const Options& options)
{
    const std::size_t rows = options.limit_train == 0 ?
            dataset.database.size() :
            std::min(options.limit_train, dataset.database.size());
    const std::size_t features = options.limit_features == 0 ?
            dataset.num_features :
            std::min(options.limit_features, dataset.num_features);
    if (rows == 0 || features == 0)
        throw std::runtime_error("effective dataset shape must be non-empty");
    dataset.database.resize(rows);
    dataset.labels.resize(rows);
    dataset.query.resize(features);
    for (auto& row : dataset.database)
        row.resize(features);
    dataset.num_features = features;
}

RealTwoPartyPlayer* start_networking(const Options& options)
{
    Names names;
    Server::start_networking(names, options.party, 2, options.hostname,
            options.port_base, Names::DEFAULT_PORT);
    return new RealTwoPartyPlayer(names, 1 - options.party, 0);
}

std::vector<std::uint64_t> clear_distances(
        const ConstantPP::ConstantPPDataset& dataset)
{
    std::vector<std::uint64_t> out(dataset.database.size());
    for (std::size_t i = 0; i < dataset.database.size(); ++i)
    {
        unsigned __int128 sum = 0;
        for (std::size_t j = 0; j < dataset.num_features; ++j)
        {
            const std::uint64_t x = dataset.database[i][j];
            const std::uint64_t y = dataset.query[j];
            const std::uint64_t difference = x >= y ? x - y : y - x;
            sum += static_cast<unsigned __int128>(difference) * difference;
        }
        if (sum >= (static_cast<unsigned __int128>(1) << 63))
            throw std::runtime_error("distance violates signed-MSB <2^63 bound");
        out[i] = static_cast<std::uint64_t>(sum);
    }
    return out;
}

void print_summary(const Options& options, std::size_t rows,
        std::size_t features, std::size_t batch_size,
        const std::vector<Trial>& trials)
{
    std::vector<double> values;
    for (const auto& trial : trials)
        if (trial.batch_size == batch_size)
            values.push_back(trial.milliseconds);
    std::sort(values.begin(), values.end());
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    const double mean = sum / values.size();
    const double median = values.size() % 2 ? values[values.size() / 2] :
            (values[values.size() / 2 - 1] + values[values.size() / 2]) / 2;
    double variance = 0;
    for (double value : values)
        variance += (value - mean) * (value - mean);
    variance /= values.size();
    const double stddev = std::sqrt(variance);
    std::cout << "NHSS_DISTANCE_BATCH_SUMMARY"
              << " party=" << options.party
              << " dataset=" << options.dataset
              << " rows=" << rows
              << " features=" << features
              << " dimension_batch_size=" << batch_size
              << " repeats=" << values.size()
              << " mean_ms=" << mean
              << " median_ms=" << median
              << " std_ms=" << stddev
              << " cv=" << (mean == 0 ? 0 : stddev / mean)
              << " min_ms=" << values.front()
              << " max_ms=" << values.back()
              << " mean_per_sample_ms=" << mean / rows
              << " mean_per_coordinate_ms=" << mean / (rows * features)
              << " distance_sent_bytes=0"
              << " distance_transport_rounds=0"
              << " distance_logical_rounds=0"
              << std::endl;
}

} // namespace

int main(int argc, const char** argv)
{
    const Options options = parse_options(argc, argv);
    KonaBenchAffinity::bind_fixed_cpu(options.party);

    const auto load_begin = std::chrono::steady_clock::now();
    auto dataset = ConstantPP::load_constantpp_dataset(
            dataset_path(options.dataset));
    const std::size_t source_rows = dataset.database.size();
    const std::size_t source_features = dataset.num_features;
    apply_limits(dataset, options);
    const std::vector<std::uint64_t> expected = clear_distances(dataset);
    const auto load_end = std::chrono::steady_clock::now();

    const auto keygen_begin = std::chrono::steady_clock::now();
    const KonaNhss::KeyPair keys = KonaNhss::keygen(
            options.lambda, 20260829UL);
    const auto keygen_end = std::chrono::steady_clock::now();

    gmp_randclass rng(gmp_randinit_mt);
    rng.seed(20260830UL);
    const auto data_setup_begin = std::chrono::steady_clock::now();
    std::vector<std::vector<KonaNhss::DataShare> > database(
            dataset.database.size());
    for (std::size_t i = 0; i < dataset.database.size(); ++i)
        for (std::uint64_t coordinate : dataset.database[i])
        {
            const auto pair = KonaNhss::share_data(coordinate, keys.sec, rng);
            database[i].push_back(pair.party[options.party]);
        }
    const auto data_setup_end = std::chrono::steady_clock::now();

    const auto query_setup_begin = std::chrono::steady_clock::now();
    std::vector<KonaNhss::QueryShare> query;
    for (std::uint64_t coordinate : dataset.query)
    {
        const auto pair = KonaNhss::share_query(coordinate, keys.pub, rng);
        query.push_back(pair.party[options.party]);
    }
    const auto query_setup_end = std::chrono::steady_clock::now();

    RealTwoPartyPlayer* player = start_networking(options);
    std::vector<Trial> trials;
    std::vector<KonaNhss::Share64> reference;
    bool local_batch_match = true;
    for (int repeat = 0; repeat < options.repeats; ++repeat)
    {
        for (std::size_t position = 0;
                position < options.dimension_batch_sizes.size(); ++position)
        {
            const std::size_t index =
                    (position + static_cast<std::size_t>(repeat)) %
                    options.dimension_batch_sizes.size();
            const std::size_t batch_size = options.dimension_batch_sizes[index];
            const auto comm_before = player->total_comm();
            const auto begin = std::chrono::steady_clock::now();
            std::vector<KonaNhss::Share64> distances(database.size());
            for (std::size_t i = 0; i < database.size(); ++i)
                distances[i] =
                    KonaNhss::nised_squared_euclidean_additive_share_batched_l2(
                        options.party, database[i], query, keys.pub, batch_size);
            const auto end = std::chrono::steady_clock::now();
            const auto comm_delta = player->total_comm() - comm_before;
            Trial trial;
            trial.batch_size = batch_size;
            trial.repeat = repeat;
            trial.order = static_cast<int>(position);
            trial.milliseconds = elapsed_ms(begin, end);
            trial.sent_bytes = comm_delta.sent;
            trial.transport_rounds = total_rounds(comm_delta);
            trials.push_back(trial);
            if (reference.empty())
                reference = distances;
            else if (distances != reference)
                local_batch_match = false;
            std::cout << "NHSS_DISTANCE_BATCH_TRIAL"
                      << " party=" << options.party
                      << " dataset=" << options.dataset
                      << " rows=" << database.size()
                      << " features=" << query.size()
                      << " repeat=" << repeat
                      << " order=" << position
                      << " dimension_batch_size=" << batch_size
                      << " online_nhss_distance_ms=" << trial.milliseconds
                      << " per_sample_ms=" << trial.milliseconds / database.size()
                      << " per_coordinate_ms="
                      << trial.milliseconds / (database.size() * query.size())
                      << " distance_sent_bytes=" << trial.sent_bytes
                      << " distance_transport_rounds=" << trial.transport_rounds
                      << " distance_logical_rounds=0"
                      << std::endl;
        }
    }

    const auto opened =
            KonaShareConversion::open_additive_batch_l2<64>(reference, player);
    bool correctness = local_batch_match;
    for (std::size_t i = 0; i < opened.size(); ++i)
        correctness = correctness &&
                static_cast<std::uint64_t>(opened[i].get_limb(0)) == expected[i];

    std::cout << "NHSS_DISTANCE_DATA_SETUP"
              << " party=" << options.party
              << " dataset=" << options.dataset
              << " source_rows=" << source_rows
              << " source_features=" << source_features
              << " rows=" << database.size()
              << " features=" << query.size()
              << " lambda=" << options.lambda
              << " load_ms=" << elapsed_ms(load_begin, load_end)
              << " keygen_ms=" << elapsed_ms(keygen_begin, keygen_end)
              << " data_share_setup_ms="
              << elapsed_ms(data_setup_begin, data_setup_end)
              << " query_token_setup_ms="
              << elapsed_ms(query_setup_begin, query_setup_end)
              << " setup_in_online_timer=0"
              << std::endl;
    for (std::size_t batch_size : options.dimension_batch_sizes)
        print_summary(options, database.size(), query.size(), batch_size, trials);
    std::cout << "NHSS_DISTANCE_DATA_RESULT party=" << options.party
              << " correctness=" << (correctness ? "PASS" : "FAIL")
              << " local_batch_match="
              << (local_batch_match ? "PASS" : "FAIL") << std::endl;

    delete player;
    return correctness ? 0 : 1;
}
