#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "../Math/Z2k.hpp"
#include "Tools/TimerWithComm.h"
#include "Math/FixedVec.h"

using namespace std;

namespace
{

const int K = 64;
string dataset_name = "tcga-pancan";
string dir = "knn-1/";
string distance_record_path = "KNN-experiment-res/codex_esd_distances.csv";
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
    else
        sscanf(argv[1], "%d", &playerno);
}

class EsdBenchParty
{
public:
    typedef Z2<K> additive_share;
    typedef FixedVec<Z2<K>, 2> aby2_share;

    int num_features = 0;
    int num_train_data = 0;
    int num_test_data = 0;
    int m_playerno = 0;
    RealTwoPartyPlayer* m_player = 0;

    vector<Sample*> m_sample;
    vector<Sample*> m_test;
    vector<vector<aby2_share>> m_train_aby2_share_vec;
    vector<vector<aby2_share>> m_test_aby2_share_vec;
    vector<vector<Z2<K>>> m_Train_Triples_0;
    vector<vector<Z2<K>>> m_Train_Triples_1;
    vector<vector<Z2<K>>> m_Test_Triples;
    vector<Z2<K>> m_Test_Triples_0;
    vector<Z2<K>> m_Test_Triples_1;
    vector<array<additive_share, 2>> m_ESD_vec;

    explicit EsdBenchParty(int playerNo) : m_playerno(playerNo) {}

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

    void fake_load_triples()
    {
        m_Train_Triples_0.resize(num_train_data, vector<Z2<K>>(num_features, Z2<K>(0)));
        m_Train_Triples_1.resize(num_train_data, vector<Z2<K>>(num_features, Z2<K>(0)));
        m_Test_Triples.resize(num_train_data, vector<Z2<K>>(num_features, Z2<K>(0)));
        m_Test_Triples_0.resize(num_features, Z2<K>(0));
        m_Test_Triples_1.resize(num_features, Z2<K>(0));
    }

    void aby2_share_data_and_additive_share_label_list()
    {
        m_train_aby2_share_vec.resize(num_train_data);
        for (int i = 0; i < num_train_data; i++)
            m_train_aby2_share_vec[i].resize(num_features);

        m_test_aby2_share_vec.resize(num_test_data);
        for (int i = 0; i < num_test_data; i++)
            m_test_aby2_share_vec[i].resize(num_features);

        if (m_playerno == 0)
        {
            octetStream os;
            for (int i = 0; i < num_train_data; i++)
            {
                for (int j = 0; j < num_features; j++)
                {
                    m_train_aby2_share_vec[i][j][1] = m_Train_Triples_0[i][j];
                    m_train_aby2_share_vec[i][j][0] =
                            Z2<K>(m_sample[i]->features[j]) +
                            m_Train_Triples_0[i][j] + m_Train_Triples_1[i][j];
                    m_train_aby2_share_vec[i][j][0].pack(os);
                }
            }
            m_player->send(os);

            os.clear();
            m_player->receive(os);
            for (int i = 0; i < num_test_data; i++)
            {
                for (int j = 0; j < num_features; j++)
                {
                    m_test_aby2_share_vec[i][j][1] = m_Test_Triples_0[j];
                    m_test_aby2_share_vec[i][j][0].unpack(os);
                }
            }
        }
        else
        {
            octetStream os;
            m_player->receive(os);
            for (int i = 0; i < num_train_data; i++)
            {
                for (int j = 0; j < num_features; j++)
                {
                    m_train_aby2_share_vec[i][j][1] = m_Train_Triples_1[i][j];
                    m_train_aby2_share_vec[i][j][0].unpack(os);
                }
            }

            os.clear();
            for (int i = 0; i < num_test_data; i++)
            {
                for (int j = 0; j < num_features; j++)
                {
                    m_test_aby2_share_vec[i][j][1] = m_Test_Triples_1[j];
                    m_test_aby2_share_vec[i][j][0] =
                            Z2<K>(m_test[i]->features[j]) +
                            m_Test_Triples_0[j] + m_Test_Triples_1[j];
                    m_test_aby2_share_vec[i][j][0].pack(os);
                }
            }
            m_player->send(os);
        }
    }

    Z2<K> compute_ESD_two_sample(int train_idx, int query_idx)
    {
        Z2<K> res(0);
        Z2<K> tmp_1(0);
        for (int j = 0; j < num_features; j++)
        {
            aby2_share x_min_y_aby2_share =
                    m_train_aby2_share_vec[train_idx][j] -
                    m_test_aby2_share_vec[query_idx][j];
            tmp_1 = tmp_1 + x_min_y_aby2_share[0] * x_min_y_aby2_share[0];
            res = res - Z2<K>(2) * x_min_y_aby2_share[0] *
                    x_min_y_aby2_share[1] + m_Test_Triples[train_idx][j];
        }
        if (playerno == 1)
            res = res + tmp_1;
        return res;
    }

    Z2<K> reveal_one_num_to(Z2<K> x, int playerID)
    {
        octetStream os;
        if (m_playerno == playerID)
        {
            m_player->receive(os);
            Z2<K> tmp;
            tmp.unpack(os);
            return tmp + x;
        }
        else
        {
            x.pack(os);
            m_player->send(os);
            return x;
        }
    }

    void compute_ESD_for_one_query(int idx_of_test)
    {
        if ((int)m_ESD_vec.size() != num_train_data)
            m_ESD_vec.resize(num_train_data);
        for (int i = 0; i < num_train_data; i++)
            m_ESD_vec[i][0] = compute_ESD_two_sample(i, idx_of_test);
    }

    void record_revealed_distances_and_labels(
            int idx_of_test, const string& output_path)
    {
        if (idx_of_test < 0 || idx_of_test >= num_test_data)
            throw runtime_error("query index out of range");

        ofstream out;
        if (m_playerno == 0)
        {
            out.open(output_path);
            if (!out)
                throw runtime_error("failed to open distance record output");
            out << "query_idx,train_idx,label,distance_limb0\n";
        }

        for (int i = 0; i < num_train_data; i++)
        {
            Z2<K> revealed = reveal_one_num_to(m_ESD_vec[i][0], 0);
            if (m_playerno == 0)
            {
                out << idx_of_test << "," << i << "," << m_sample[i]->label << ","
                    << revealed.get_limb(0) << "\n";
            }
        }
    }
};

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    EsdBenchParty party(playerno);
    party.start_networking(opt);

    using Clock = chrono::steady_clock;
    auto total_start = Clock::now();

    auto read_start = Clock::now();
    party.read_meta_and_P0_sample_P1_query();
    auto read_end = Clock::now();

    auto triple_start = Clock::now();
    party.fake_load_triples();
    auto triple_end = Clock::now();

    auto share_start = Clock::now();
    party.aby2_share_data_and_additive_share_label_list();
    auto share_end = Clock::now();

    auto one_query_start = Clock::now();
    party.compute_ESD_for_one_query(0);
    auto one_query_end = Clock::now();

    auto record_start = Clock::now();
    party.record_revealed_distances_and_labels(0, distance_record_path);
    auto record_end = Clock::now();

    auto pair_loop_start = Clock::now();
    Z2<K> checksum(0);
    for (int i = 0; i < party.num_train_data; i++)
        checksum += party.compute_ESD_two_sample(i, 0);
    auto pair_loop_end = Clock::now();

    auto total_end = Clock::now();

    chrono::duration<double> read_time = read_end - read_start;
    chrono::duration<double> triple_time = triple_end - triple_start;
    chrono::duration<double> share_time = share_end - share_start;
    chrono::duration<double> one_query_time = one_query_end - one_query_start;
    chrono::duration<double> record_time = record_end - record_start;
    chrono::duration<double> pair_loop_time = pair_loop_end - pair_loop_start;
    chrono::duration<double> benchmark_wall_time = total_end - total_start;
    chrono::duration<double> benchmark_wall_excluding_record_time =
            benchmark_wall_time - record_time;

    cout << "dataset=" << dataset_name << endl;
    cout << "player=" << playerno << endl;
    cout << "num_features=" << party.num_features << endl;
    cout << "num_train_data=" << party.num_train_data << endl;
    cout << "num_test_data=" << party.num_test_data << endl;
    cout << "distance_scalar_count=" << party.num_train_data << endl;
    cout << "benchmark_case=1_query_to_all_train_samples" << endl;
    cout << "read_seconds=" << read_time.count() << endl;
    cout << "triple_load_setup_seconds=" << triple_time.count() << endl;
    cout << "share_setup_seconds=" << share_time.count() << endl;
    cout << "online_distance_seconds=" << one_query_time.count() << endl;
    cout << "correctness_output_record_seconds=" << record_time.count() << endl;
    cout << "distance_record_path=" << distance_record_path << endl;
    cout << "all_pairs_microbenchmark_seconds=" << pair_loop_time.count() << endl;
    cout << "pair_ns=" << pair_loop_time.count() * 1e9 / party.num_train_data << endl;
    cout << "checksum_limb0=" << checksum.get_limb(0) << endl;
    cout << "benchmark_wall_seconds_excluding_record="
         << benchmark_wall_excluding_record_time.count() << endl;
    cout << "benchmark_wall_seconds=" << benchmark_wall_time.count() << endl;
    return 0;
}
