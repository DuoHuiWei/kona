#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "../Tools/random.h"
#include "../Math/Z2k.hpp"
#include "Machines/kona-share-conversion.hpp"

using namespace std;

namespace
{

const int K = 64;
string dataset_name = "tcga-pancan";
string dir = "knn-1/";
int playerno = 0;
ez::ezOptionParser opt;
RealTwoPartyPlayer* player = 0;

class Sample
{
public:
    vector<int> features;
    int label;
    Sample(int n) : features(n) {}
};

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

class BeaverEsdBenchParty
{
public:
    typedef Z2<K> additive_share;

    int num_features = 0;
    int num_train_data = 0;
    int num_test_data = 0;
    int m_playerno = 0;
    RealTwoPartyPlayer* m_player = 0;

    vector<Sample*> m_sample;
    vector<Sample*> m_test;
    vector<vector<additive_share>> m_train_additive_share_vec;
    vector<vector<additive_share>> m_test_additive_share_vec;

    explicit BeaverEsdBenchParty(int playerNo) : m_playerno(playerNo) {}

    void start_networking(ez::ezOptionParser& opt)
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
        this->m_player = new RealTwoPartyPlayer(playerNames, 1 - playerno, 0);
        player = this->m_player;
    }

    void read_meta_and_P0_sample_P1_query()
    {
        ifstream meta_file(
                "Player-Data/Knn-Data/" + dir + dataset_name + "-data/Knn-meta");
        meta_file >> num_features;
        meta_file >> num_train_data;
        meta_file >> num_test_data;
        meta_file.close();

        if (playerno == 0)
        {
            ifstream sample_file(
                    "Player-Data/Knn-Data/" + dir + dataset_name + "-data/P0-0-X-Train");
            for (int i = 0; i < num_train_data; i++)
            {
                Sample* sample_ptr = new Sample(num_features);
                for (int j = 0; j < num_features; j++)
                    sample_file >> sample_ptr->features[j];
                m_sample.push_back(sample_ptr);
            }
            sample_file.close();

            ifstream label_file(
                    "Player-Data/Knn-Data/" + dir + dataset_name + "-data/P0-0-Y-Train");
            for (int i = 0; i < num_train_data; i++)
                label_file >> m_sample[i]->label;
            label_file.close();
        }
        else
        {
            ifstream test_file(
                    "Player-Data/Knn-Data/" + dir + dataset_name + "-data/P1-0-X-Test");
            for (int i = 0; i < num_test_data; i++)
            {
                Sample* test_ptr = new Sample(num_features);
                for (int j = 0; j < num_features; j++)
                    test_file >> test_ptr->features[j];
                m_test.push_back(test_ptr);
            }
            test_file.close();

            ifstream label_file(
                    "Player-Data/Knn-Data/" + dir + dataset_name + "-data/P1-0-Y-Test");
            for (int i = 0; i < num_test_data; i++)
                label_file >> m_test[i]->label;
            label_file.close();
        }
    }

    void additive_share_all_data()
    {
        m_train_additive_share_vec.resize(num_train_data);
        for (int i = 0; i < num_train_data; i++)
            m_train_additive_share_vec[i].resize(num_features);

        m_test_additive_share_vec.resize(num_test_data);
        for (int i = 0; i < num_test_data; i++)
            m_test_additive_share_vec[i].resize(num_features);

        if (playerno == 0)
        {
            octetStream os;
            PRNG prng;
            prng.ReSeed();
            Z2<K> random_data;
            for (int i = 0; i < num_train_data; i++)
            {
                for (int j = 0; j < num_features; j++)
                {
                    random_data.randomize(prng);
                    m_train_additive_share_vec[i][j] =
                            Z2<K>(m_sample[i]->features[j]) - random_data;
                    random_data.pack(os);
                }
            }
            m_player->send(os);

            os.clear();
            m_player->receive(os);
            for (int i = 0; i < num_test_data; i++)
                for (int j = 0; j < num_features; j++)
                    m_test_additive_share_vec[i][j].unpack(os);
        }
        else
        {
            octetStream os;
            m_player->receive(os);
            for (int i = 0; i < num_train_data; i++)
                for (int j = 0; j < num_features; j++)
                    m_train_additive_share_vec[i][j].unpack(os);

            os.clear();
            PRNG prng;
            prng.ReSeed();
            Z2<K> random_data;
            for (int i = 0; i < num_test_data; i++)
            {
                for (int j = 0; j < num_features; j++)
                {
                    random_data.randomize(prng);
                    m_test_additive_share_vec[i][j] =
                            Z2<K>(m_test[i]->features[j]) - random_data;
                    random_data.pack(os);
                }
            }
            m_player->send(os);
        }
    }

    Z2<K> compute_beaver_esd_two_sample(int train_idx, int query_idx)
    {
        vector<Z2<K>> diffs(num_features);
        for (int j = 0; j < num_features; j++)
            diffs[j] =
                    m_train_additive_share_vec[train_idx][j] -
                    m_test_additive_share_vec[query_idx][j];

        vector<Z2<K>> squares;
        KonaShareConversion::mul_vector_additive_kona_l2<K>(
                diffs, diffs, squares, m_player);

        Z2<K> result(0);
        for (int j = 0; j < num_features; j++)
            result += squares[j];
        return result;
    }

    void compute_beaver_esd_for_one_query(
            int query_idx,
            vector<Z2<K>>& esd_vec)
    {
        esd_vec.resize(num_train_data);
        for (int i = 0; i < num_train_data; i++)
            esd_vec[i] = compute_beaver_esd_two_sample(i, query_idx);
    }

    void compute_beaver_esd_for_one_query_query_batched(
            int query_idx,
            vector<Z2<K>>& esd_vec)
    {
        esd_vec.assign(num_train_data, Z2<K>(0));

        Z2<K> a(0), b(0), c(0);
        octetStream send_os, receive_os;
        for (int i = 0; i < num_train_data; i++)
        {
            for (int j = 0; j < num_features; j++)
            {
                Z2<K> diff =
                        m_train_additive_share_vec[i][j] -
                        m_test_additive_share_vec[query_idx][j];
                (diff - a).pack(send_os);
                (diff - b).pack(send_os);
            }
        }

        m_player->send(send_os);
        m_player->receive(receive_os);

        for (int i = 0; i < num_train_data; i++)
        {
            Z2<K> sum(0);
            for (int j = 0; j < num_features; j++)
            {
                Z2<K> diff =
                        m_train_additive_share_vec[i][j] -
                        m_test_additive_share_vec[query_idx][j];
                Z2<K> peer_e, peer_f;
                peer_e.unpack(receive_os);
                peer_f.unpack(receive_os);
                Z2<K> e = peer_e + diff - a;
                Z2<K> f = peer_f + diff - b;
                Z2<K> r = f * a + e * b + c;
                if (m_player->my_num())
                    r = r + e * f;
                sum += r;
            }
            esd_vec[i] = sum;
        }
    }
};

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    BeaverEsdBenchParty party(playerno);
    party.start_networking(opt);

    using Clock = chrono::steady_clock;
    auto total_start = Clock::now();

    auto read_start = Clock::now();
    party.read_meta_and_P0_sample_P1_query();
    auto read_end = Clock::now();

    auto share_start = Clock::now();
    party.additive_share_all_data();
    auto share_end = Clock::now();

    vector<Z2<K>> esd_vec;
    auto pair_batched_start = Clock::now();
    party.compute_beaver_esd_for_one_query(0, esd_vec);
    auto pair_batched_end = Clock::now();

    auto query_batched_start = Clock::now();
    vector<Z2<K>> query_batched_esd;
    party.compute_beaver_esd_for_one_query_query_batched(0, query_batched_esd);
    auto query_batched_end = Clock::now();

    auto pair_loop_start = Clock::now();
    Z2<K> checksum_pair_batched(0);
    for (int i = 0; i < party.num_train_data; i++)
        checksum_pair_batched += party.compute_beaver_esd_two_sample(i, 0);
    auto pair_loop_end = Clock::now();

    Z2<K> checksum_query_batched(0);
    for (int i = 0; i < party.num_train_data; i++)
        checksum_query_batched += query_batched_esd[i];

    auto total_end = Clock::now();

    chrono::duration<double> read_time = read_end - read_start;
    chrono::duration<double> share_time = share_end - share_start;
    chrono::duration<double> pair_batched_time =
            pair_batched_end - pair_batched_start;
    chrono::duration<double> query_batched_time =
            query_batched_end - query_batched_start;
    chrono::duration<double> pair_loop_time = pair_loop_end - pair_loop_start;
    chrono::duration<double> total_time = total_end - total_start;

    cout << "dataset=" << dataset_name << endl;
    cout << "player=" << playerno << endl;
    cout << "num_features=" << party.num_features << endl;
    cout << "num_train_data=" << party.num_train_data << endl;
    cout << "num_test_data=" << party.num_test_data << endl;
    cout << "read_seconds=" << read_time.count() << endl;
    cout << "share_setup_seconds=" << share_time.count() << endl;
    cout << "beaver_pair_batched_one_query_seconds=" << pair_batched_time.count() << endl;
    cout << "beaver_pair_comm_coalesced_one_query_seconds=" << query_batched_time.count() << endl;
    cout << "beaver_pair_batched_all_pairs_loop_seconds=" << pair_loop_time.count() << endl;
    cout << "beaver_pair_batched_pair_ns=" <<
            pair_loop_time.count() * 1e9 / party.num_train_data << endl;
    cout << "checksum_pair_batched_limb0=" << checksum_pair_batched.get_limb(0) << endl;
    cout << "checksum_pair_comm_coalesced_limb0=" << checksum_query_batched.get_limb(0) << endl;
    cout << "total_seconds=" << total_time.count() << endl;
    return 0;
}
