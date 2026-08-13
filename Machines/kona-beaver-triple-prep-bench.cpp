#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "../Tools/random.h"
#include "../Math/Z2k.hpp"

using namespace std;

namespace
{

const int K = 64;
string dataset_name = "tcga-pancan";
string dir = "knn-1/";
int playerno = 0;
ez::ezOptionParser opt;

size_t total_rounds(const NamedCommStats& stats)
{
    size_t rounds = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        rounds += it->second.rounds;
    return rounds;
}

void parse_argv(int argc, const char** argv)
{
    opt.add("5000", 0, 1, 0,
            "Port number base to attempt to start connections from",
            "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on", "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host", "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file", "-ip", "--ip-file-name");
    opt.parse(argc, argv);
    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else
        sscanf(argv[1], "%d", &playerno);
}

class BeaverTriplePrepBench
{
public:
    int num_features = 0;
    int num_train_data = 0;
    int num_test_data = 0;
    RealTwoPartyPlayer* player = 0;

    explicit BeaverTriplePrepBench(int playerno)
    {
        string hostname, ipFileName;
        int pnbase = 0;
        int my_port = Names::DEFAULT_PORT;
        opt.get("--portnumbase")->getInt(pnbase);
        opt.get("--hostname")->getString(hostname);
        opt.get("--ip-file-name")->getString(ipFileName);

        ez::OptionGroup* mp_opt = opt.get("--my-port");
        if (mp_opt->isSet)
            mp_opt->getInt(my_port);

        Names playerNames;
        if (ipFileName.size() > 0)
        {
            if (my_port != Names::DEFAULT_PORT)
                throw runtime_error("cannot set port number when using IP file");
            playerNames.init(playerno, pnbase, ipFileName, 2);
        }
        else
        {
            Server::start_networking(playerNames, playerno, 2,
                    hostname, pnbase, my_port);
        }
        player = new RealTwoPartyPlayer(playerNames, 1 - playerno, 0);
    }

    ~BeaverTriplePrepBench()
    {
        delete player;
    }

    void read_meta()
    {
        ifstream meta_file(
                "Player-Data/Knn-Data/" + dir + dataset_name + "-data/Knn-meta");
        if (!meta_file.good())
            throw runtime_error("missing meta file for " + dataset_name);
        meta_file >> num_features;
        meta_file >> num_train_data;
        meta_file >> num_test_data;
        meta_file.close();
    }

    // Pair-by-pair Beaver triple generation + file materialization benchmark.
    // Logic is real in the sense that additive shares [a], [b], [c] satisfy
    // c = a * b mod 2^64. Communication is real and counted. Random sharing of
    // c is simplified: party 0 keeps c0=0 and party 1 keeps c1=ab.
    Z2<K> generate_one_pair_triples_and_write(
            int triples_per_pair,
            ofstream& file_a,
            ofstream& file_b,
            ofstream& file_c)
    {
        PRNG prng;
        prng.ReSeed();

        vector<Z2<K>> a_local(triples_per_pair);
        vector<Z2<K>> b_local(triples_per_pair);
        for (int i = 0; i < triples_per_pair; i++)
        {
            a_local[i].randomize(prng);
            b_local[i].randomize(prng);
        }

        octetStream send_os, receive_os;
        for (int i = 0; i < triples_per_pair; i++)
        {
            a_local[i].pack(send_os);
            b_local[i].pack(send_os);
        }
        player->send(send_os);
        player->receive(receive_os);

        Z2<K> checksum(0);
        for (int i = 0; i < triples_per_pair; i++)
        {
            Z2<K> peer_a, peer_b;
            peer_a.unpack(receive_os);
            peer_b.unpack(receive_os);
            Z2<K> a_share = a_local[i];
            Z2<K> b_share = b_local[i];
            Z2<K> a = a_local[i] + peer_a;
            Z2<K> b = b_local[i] + peer_b;
            Z2<K> c_share = player->my_num() ? (a * b) : Z2<K>(0);
            file_a.write(reinterpret_cast<char*>(&a_share), sizeof(Z2<K>));
            file_b.write(reinterpret_cast<char*>(&b_share), sizeof(Z2<K>));
            file_c.write(reinterpret_cast<char*>(&c_share), sizeof(Z2<K>));
            checksum += c_share;
        }
        return checksum;
    }
};

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    BeaverTriplePrepBench bench(playerno);
    bench.read_meta();

    const int triples_per_pair = bench.num_features;
    const int num_pairs = bench.num_train_data;
    const long long total_triples =
            (long long)triples_per_pair * (long long)num_pairs;
    const long long file_bytes_written =
            total_triples * 3LL * (long long)sizeof(Z2<K>);

    string prefix = "Player-Data/Knn-Data/" + dir + dataset_name + "-data/P" +
            to_string(playerno) + "-Beaver-";
    ofstream file_a((prefix + "A-Triples").c_str(), ios::binary);
    ofstream file_b((prefix + "B-Triples").c_str(), ios::binary);
    ofstream file_c((prefix + "C-Triples").c_str(), ios::binary);

    auto comm_before = bench.player->total_comm();
    auto total_start = chrono::steady_clock::now();

    Z2<K> checksum(0);
    auto pair_start = chrono::steady_clock::now();
    checksum += bench.generate_one_pair_triples_and_write(
            triples_per_pair, file_a, file_b, file_c);
    auto pair_end = chrono::steady_clock::now();

    for (int i = 1; i < num_pairs; i++)
        checksum += bench.generate_one_pair_triples_and_write(
                triples_per_pair, file_a, file_b, file_c);

    auto total_end = chrono::steady_clock::now();
    auto comm_after = bench.player->total_comm();
    auto comm_delta = comm_after - comm_before;

    file_a.close();
    file_b.close();
    file_c.close();

    chrono::duration<double> pair_time = pair_end - pair_start;
    chrono::duration<double> total_time = total_end - total_start;

    cout << "dataset=" << dataset_name << endl;
    cout << "player=" << playerno << endl;
    cout << "triples_per_pair=" << triples_per_pair << endl;
    cout << "num_pairs=" << num_pairs << endl;
    cout << "total_triples=" << total_triples << endl;
    cout << "single_pair_offline_gen_seconds=" << pair_time.count() << endl;
    cout << "all_pairs_offline_gen_and_write_seconds=" << total_time.count() << endl;
    cout << "triples_per_second=" << (double)total_triples / total_time.count() << endl;
    cout << "sent_bytes=" << comm_delta.sent << endl;
    cout << "rounds=" << total_rounds(comm_delta) << endl;
    cout << "file_bytes_written=" << file_bytes_written << endl;
    cout << "checksum_limb0=" << checksum.get_limb(0) << endl;
    return 0;
}
