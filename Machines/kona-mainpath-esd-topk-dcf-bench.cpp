#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "../Tools/ezOptionParser.h"
#include "../Math/Z2k.hpp"
#include "../Tools/random.h"
#include "Machines/kona-cong-kona-adapter.hpp"

using namespace std;

namespace
{

const int K = 64;
int playerno = 0;
ez::ezOptionParser opt;
string dataset_name = "arcene";
string data_dir = "knn-1/";
int topk_k = 5;
int query_limit = -1;
long long call_evaluate_time = 0;
chrono::duration<double> total_duration(0);

using AdditiveShare = Z2<K>;
using Aby2Share = array<Z2<K>, 2>;
using EsdEntry = array<Z2<K>, 2>;

struct Sample
{
    vector<int> features;
    int label = 0;
    explicit Sample(int n) : features(n) {}
};

struct CommSnapshot
{
    NamedCommStats stats;
};

size_t total_rounds(const NamedCommStats& stats)
{
    size_t rounds = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        rounds += it->second.rounds;
    return rounds;
}

double total_comm_seconds(NamedCommStats stats)
{
    double total = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        total += it->second.timer.elapsed();
    return total;
}


bigint evaluate_original_dcf(Z2<K> x, int n, int playerID)
{
    call_evaluate_time++;
    auto start = chrono::high_resolution_clock::now();

    fstream k_in;
    PRNG prng;
    int b = playerID, xi;
    const int lambda_bytes = 16;
    k_in.open("Player-Data/2-fss/k" + to_string(playerID), ios::in);
    if (!k_in.good())
        throw runtime_error("missing original DCF key file");

    octet seed[lambda_bytes], tmp_seed[lambda_bytes];
    bigint s_hat[2], v_hat[2], t_hat[2], s[2], v[2], t[2];
    bigint scw, vcw, tcw[2], convert[2], cw, tmp_t, tmp_v, tmp_out;
    k_in >> tmp_t;
    bytesFromBigint(&seed[0], tmp_t, lambda_bytes);
    tmp_t = b;
    tmp_v = 0;
    for (int i = 0; i < n - 1; i++)
    {
        xi = x.get_bit(n - i - 1);
        bigintFromBytes(tmp_out, &seed[0], lambda_bytes);
        k_in >> scw >> vcw >> tcw[0] >> tcw[1];
        prng.SetSeed(seed);
        for (int j = 0; j < 2; j++)
        {
            prng.get(t_hat[j], 1);
            prng.get(v_hat[j], n);
            prng.get(s_hat[j], n);
            s[j] = s_hat[j] ^ (tmp_t * scw);
            t[j] = t_hat[j] ^ (tmp_t * tcw[j]);
        }
        bytesFromBigint(&tmp_seed[0], v_hat[0], lambda_bytes);
        prng.SetSeed(tmp_seed);
        prng.get(convert[0], n);
        bytesFromBigint(&tmp_seed[0], v_hat[1], lambda_bytes);
        prng.SetSeed(tmp_seed);
        prng.get(convert[1], n);
        tmp_v = tmp_v + b * (-1) * (convert[xi] + tmp_t * vcw) +
                (1 ^ b) * (convert[xi] + tmp_t * vcw);
        bytesFromBigint(&seed[0], s[xi], lambda_bytes);
        tmp_t = t[xi];
    }
    k_in >> cw;
    k_in.close();
    prng.SetSeed(seed);
    prng.get(convert[0], n);
    tmp_v = tmp_v + b * (-1) * (convert[0] + tmp_t * cw) +
            (1 ^ b) * (convert[0] + tmp_t * cw);

    auto end = chrono::high_resolution_clock::now();
    total_duration += chrono::duration<double>(end - start);
    return tmp_v;
}

int get_int_option(ez::ezOptionParser& parser, const char* name)
{
    int value = 0;
    parser.get(name)->getInt(value);
    return value;
}

void parse_argv(int argc, const char** argv)
{
    opt.add("5000", 0, 1, 0, "Port number base", "-pn", "--portnumbase");
    opt.add("", 0, 1, 0, "Player number", "-p", "--player");
    opt.add("", 0, 1, 0, "Port to listen on", "-mp", "--my-port");
    opt.add("localhost", 0, 1, 0, "Startup host", "-h", "--hostname");
    opt.add("", 0, 1, 0, "Party hostname file", "-ip", "--ip-file-name");
    opt.add("arcene", 0, 1, 0, "Dataset name under Player-Data/Knn-Data/<dir>",
            "-d", "--dataset");
    opt.add("knn-1/", 0, 1, 0, "Dataset directory prefix", "--data-dir");
    opt.add("5", 0, 1, 0, "Top-k", "-k", "--topk");
    opt.add("-1", 0, 1, 0, "Limit number of test queries; -1 means all",
            "-q", "--query-limit");
    opt.parse(argc, argv);

    if (opt.isSet("-p"))
        opt.get("-p")->getInt(playerno);
    else if (argc > 1)
        sscanf(argv[1], "%d", &playerno);
    else
        throw runtime_error("missing player number");

    opt.get("--dataset")->getString(dataset_name);
    opt.get("--data-dir")->getString(data_dir);
    topk_k = get_int_option(opt, "--topk");
    query_limit = get_int_option(opt, "--query-limit");
    if (topk_k <= 0)
        throw runtime_error("top-k must be positive");
}

RealTwoPartyPlayer* start_networking()
{
    string hostname, ip_file_name;
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
            throw runtime_error("cannot set my-port with ip-file-name");
        player_names.init(playerno, pnbase, ip_file_name, 2);
    }
    else
    {
        Server::start_networking(
                player_names, playerno, 2, hostname, pnbase, my_port);
    }
    return new RealTwoPartyPlayer(player_names, 1 - playerno, 0);
}

class KonaMainpathEsdTopkDcfBench
{
public:
    int num_features = 0;
    int num_train_data = 0;
    int num_test_data = 0;
    RealTwoPartyPlayer* player = 0;

    vector<Sample*> train_samples;
    vector<Sample*> test_samples;
    vector<vector<Aby2Share>> train_aby2_shares;
    vector<vector<Aby2Share>> test_aby2_shares;
    vector<vector<Z2<K>>> train_triples_0;
    vector<vector<Z2<K>>> train_triples_1;
    vector<vector<Z2<K>>> test_triples;
    vector<Z2<K>> test_triples_0;
    vector<Z2<K>> test_triples_1;
    vector<EsdEntry> esd_vec;

    explicit KonaMainpathEsdTopkDcfBench(RealTwoPartyPlayer* p) : player(p) {}

    ~KonaMainpathEsdTopkDcfBench()
    {
        for (auto* s : train_samples)
            delete s;
        for (auto* s : test_samples)
            delete s;
    }

    string dataset_path() const
    {
        return "Player-Data/Knn-Data/" + data_dir + dataset_name + "-data/";
    }

    void read_meta_and_P0_sample_P1_query()
    {
        ifstream meta_file(dataset_path() + "Knn-meta");
        if (!meta_file.good())
            throw runtime_error("missing Knn-meta for dataset " + dataset_name);
        meta_file >> num_features;
        meta_file >> num_train_data;
        meta_file >> num_test_data;
        meta_file.close();

        if (playerno == 0)
        {
            ifstream sample_file(dataset_path() + "P0-0-X-Train");
            if (!sample_file.good())
                throw runtime_error("missing P0 train file");
            for (int i = 0; i < num_train_data; i++)
            {
                Sample* sample = new Sample(num_features);
                for (int j = 0; j < num_features; j++)
                    sample_file >> sample->features[j];
                train_samples.push_back(sample);
            }
            sample_file.close();

            ifstream label_file(dataset_path() + "P0-0-Y-Train");
            if (!label_file.good())
                throw runtime_error("missing P0 train label file");
            for (int i = 0; i < num_train_data; i++)
                label_file >> train_samples[i]->label;
            label_file.close();
        }
        else
        {
            ifstream test_file(dataset_path() + "P1-0-X-Test");
            if (!test_file.good())
                throw runtime_error("missing P1 test file");
            for (int i = 0; i < num_test_data; i++)
            {
                Sample* sample = new Sample(num_features);
                for (int j = 0; j < num_features; j++)
                    test_file >> sample->features[j];
                test_samples.push_back(sample);
            }
            test_file.close();

            ifstream label_file(dataset_path() + "P1-0-Y-Test");
            if (!label_file.good())
                throw runtime_error("missing P1 test label file");
            for (int i = 0; i < num_test_data; i++)
                label_file >> test_samples[i]->label;
            label_file.close();
        }
    }

    void fake_load_triples()
    {
        train_triples_0.assign(num_train_data, vector<Z2<K>>(num_features, Z2<K>(0)));
        train_triples_1.assign(num_train_data, vector<Z2<K>>(num_features, Z2<K>(0)));
        test_triples.assign(num_train_data, vector<Z2<K>>(num_features, Z2<K>(0)));
        test_triples_0.assign(num_features, Z2<K>(0));
        test_triples_1.assign(num_features, Z2<K>(0));
    }

    void aby2_share_data_and_additive_share_label_list()
    {
        train_aby2_shares.assign(num_train_data, vector<Aby2Share>(num_features));
        test_aby2_shares.assign(num_test_data, vector<Aby2Share>(num_features));

        if (playerno == 0)
        {
            octetStream os;
            for (int i = 0; i < num_train_data; i++)
            {
                for (int j = 0; j < num_features; j++)
                {
                    train_aby2_shares[i][j][1] = train_triples_0[i][j];
                    train_aby2_shares[i][j][0] =
                            Z2<K>(train_samples[i]->features[j]) +
                            train_triples_0[i][j] + train_triples_1[i][j];
                    train_aby2_shares[i][j][0].pack(os);
                }
            }
            player->send(os);

            os.clear();
            player->receive(os);
            for (int i = 0; i < num_test_data; i++)
            {
                for (int j = 0; j < num_features; j++)
                {
                    test_aby2_shares[i][j][1] = test_triples_0[j];
                    test_aby2_shares[i][j][0].unpack(os);
                }
            }
        }
        else
        {
            octetStream os;
            player->receive(os);
            for (int i = 0; i < num_train_data; i++)
            {
                for (int j = 0; j < num_features; j++)
                {
                    train_aby2_shares[i][j][1] = train_triples_1[i][j];
                    train_aby2_shares[i][j][0].unpack(os);
                }
            }

            os.clear();
            for (int i = 0; i < num_test_data; i++)
            {
                for (int j = 0; j < num_features; j++)
                {
                    test_aby2_shares[i][j][1] = test_triples_1[j];
                    test_aby2_shares[i][j][0] =
                            Z2<K>(test_samples[i]->features[j]) +
                            test_triples_0[j] + test_triples_1[j];
                    test_aby2_shares[i][j][0].pack(os);
                }
            }
            player->send(os);
        }
    }

    Z2<K> compute_ESD_two_sample(int train_idx, int query_idx)
    {
        Z2<K> res(0);
        Z2<K> tmp_1(0);
        for (int j = 0; j < num_features; j++)
        {
            Aby2Share diff;
            diff[0] = train_aby2_shares[train_idx][j][0] -
                    test_aby2_shares[query_idx][j][0];
            diff[1] = train_aby2_shares[train_idx][j][1] -
                    test_aby2_shares[query_idx][j][1];
            tmp_1 += diff[0] * diff[0];
            res = res - Z2<K>(2) * diff[0] * diff[1] +
                    test_triples[train_idx][j];
        }
        if (playerno == 1)
            res += tmp_1;
        return res;
    }

    void compute_ESD_for_one_query(int query_idx)
    {
        if ((int)esd_vec.size() != num_train_data)
            esd_vec.resize(num_train_data);
        for (int i = 0; i < num_train_data; i++)
        {
            esd_vec[i][0] = compute_ESD_two_sample(i, query_idx);
            if (playerno == 0)
                esd_vec[i][1] = Z2<K>(train_samples[i]->label) - train_triples_1[i][0];
            else
                esd_vec[i][1] = train_triples_1[i][0];
        }
    }

    void compare_in_vec_original_dcf(
            const vector<int>& compare_idx_vec,
            vector<Z2<K>>& compare_res,
            bool greater_than)
    {
        if (compare_idx_vec.empty() || compare_idx_vec.size() != compare_res.size())
            throw runtime_error("invalid compare vector size");

        bigint r_tmp;
        fstream r;
        r.open("Player-Data/2-fss/r" + to_string(playerno), ios::in);
        if (!r.good())
            throw runtime_error("missing original DCF r file");
        r >> r_tmp;
        r.close();
        SignedZ2<K> alpha_share = (SignedZ2<K>)r_tmp;
        int size_res = compare_idx_vec.size() / 2;

        vector<SignedZ2<K>> compare_res_t(compare_res.size());
        if (greater_than)
        {
            for (int i = 0; i < size_res; i++)
                compare_res_t[i] =
                        SignedZ2<K>(esd_vec[compare_idx_vec[2 * i + 1]][0]) -
                        SignedZ2<K>(esd_vec[compare_idx_vec[2 * i]][0]) +
                        alpha_share;
        }
        else
        {
            for (int i = 0; i < size_res; i++)
                compare_res_t[i] =
                        SignedZ2<K>(esd_vec[compare_idx_vec[2 * i]][0]) -
                        SignedZ2<K>(esd_vec[compare_idx_vec[2 * i + 1]][0]) +
                        alpha_share;
        }

        vector<SignedZ2<K>> tmp_res(size_res);
        octetStream send_os, receive_os;
        for (int i = 0; i < size_res; i++)
            compare_res_t[i].pack(send_os);
        player->send(send_os);
        player->receive(receive_os);
        for (int i = 0; i < size_res; i++)
        {
            SignedZ2<K> tmp;
            tmp.unpack(receive_os);
            tmp_res[i] = compare_res_t[i] + tmp;
        }

        for (int i = 0; i < size_res; i++)
        {
            bigint dcf_res_u, dcf_res_v;
            SignedZ2<K> dcf_u, dcf_v;
            dcf_res_u = evaluate_original_dcf(tmp_res[i], K, playerno);
            tmp_res[i] += 1LL << (K - 1);
            dcf_res_v = evaluate_original_dcf(tmp_res[i], K, playerno);

            auto size = dcf_res_u.get_mpz_t()->_mp_size;
            mpn_copyi((mp_limb_t*)dcf_u.get_ptr(),
                    dcf_res_u.get_mpz_t()->_mp_d, abs(size));
            if (size < 0)
                dcf_u = -dcf_u;
            size = dcf_res_v.get_mpz_t()->_mp_size;
            mpn_copyi((mp_limb_t*)dcf_v.get_ptr(),
                    dcf_res_v.get_mpz_t()->_mp_d, abs(size));
            if (size < 0)
                dcf_v = -dcf_v;

            if (tmp_res[i].get_bit(K - 1))
                r_tmp = dcf_v - dcf_u + playerno;
            else
                r_tmp = dcf_v - dcf_u;
            compare_res[2 * i] = SignedZ2<K>(playerno) - r_tmp;
            compare_res[2 * i + 1] = compare_res[2 * i];
        }
    }

    void top_1_dcf(
            int size_now,
            bool min_in_last)
    {
        vector<int> compare_idx_vec;
        for (int i = 0; i < size_now; i++)
            compare_idx_vec.push_back(i);
        int leftover = -1;

        while (compare_idx_vec.size() + (leftover == -1 ? 0 : 1) > 1)
        {
            if (compare_idx_vec.size() % 2 == 1)
            {
                if (leftover == -1)
                {
                    leftover = compare_idx_vec.back();
                    compare_idx_vec.pop_back();
                }
                else
                {
                    compare_idx_vec.push_back(leftover);
                    leftover = -1;
                }
            }

            vector<Z2<K>> compare_res(compare_idx_vec.size());
            compare_in_vec_original_dcf(
                    compare_idx_vec, compare_res, !min_in_last);
            KonaCongKonaAdapter::ss_vec_kona_l2<K>(
                    esd_vec, compare_idx_vec, compare_res, player);

            vector<int> next;
            for (size_t i = 1; i < compare_idx_vec.size(); i += 2)
                next.push_back(compare_idx_vec[i]);
            compare_idx_vec = std::move(next);
        }
    }

    uint64_t topk_label_checksum() const
    {
        uint64_t checksum = 0;
        for (int i = 0; i < topk_k; i++)
            checksum += esd_vec[esd_vec.size() - 1 - i][1].get_limb(0);
        return checksum;
    }
};

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    RealTwoPartyPlayer* player = start_networking();

    using Clock = chrono::steady_clock;

    try
    {
        KonaMainpathEsdTopkDcfBench bench(player);

        auto read_start = Clock::now();
        bench.read_meta_and_P0_sample_P1_query();
        auto read_end = Clock::now();

        if (topk_k > bench.num_train_data)
            throw runtime_error("top-k larger than train size");

        auto triple_start = Clock::now();
        bench.fake_load_triples();
        auto triple_end = Clock::now();

        auto share_start = Clock::now();
        bench.aby2_share_data_and_additive_share_label_list();
        auto share_end = Clock::now();

        int queries = bench.num_test_data;
        if (query_limit >= 0)
            queries = min(queries, query_limit);
        if (queries <= 0)
            throw runtime_error("no query to benchmark");

        auto comm_before = player->total_comm();
        auto online_start = Clock::now();

        double esd_seconds = 0;
        double topk_seconds = 0;
        uint64_t label_checksum = 0;
        for (int q = 0; q < queries; q++)
        {
            auto esd_start = Clock::now();
            bench.compute_ESD_for_one_query(q);
            auto esd_end = Clock::now();
            esd_seconds += chrono::duration<double>(esd_end - esd_start).count();

            auto topk_start = Clock::now();
            for (int i = 0; i < topk_k; i++)
                bench.top_1_dcf(bench.num_train_data - i, true);
            label_checksum += bench.topk_label_checksum();
            auto topk_end = Clock::now();
            topk_seconds += chrono::duration<double>(topk_end - topk_start).count();
        }

        auto online_end = Clock::now();
        auto comm_after = player->total_comm();
        auto delta = comm_after - comm_before;

        chrono::duration<double> read_time = read_end - read_start;
        chrono::duration<double> triple_time = triple_end - triple_start;
        chrono::duration<double> share_time = share_end - share_start;
        chrono::duration<double> online_time = online_end - online_start;

        if (playerno == 0)
        {
            cout << "KONA_MAINPATH_ESD_TOPK_DCF"
                 << " dataset=" << dataset_name
                 << " n=" << bench.num_train_data
                 << " features=" << bench.num_features
                 << " queries=" << queries
                 << " k=" << topk_k
                 << " timer_scope=mainpath_after_share_setup_to_topk_labels"
                 << " read_ms=" << read_time.count() * 1000.0
                 << " fake_triple_setup_ms=" << triple_time.count() * 1000.0
                 << " share_setup_ms=" << share_time.count() * 1000.0
                 << " dcf_key_init_ms=0"
                 << " online_esd_ms=" << esd_seconds * 1000.0
                 << " online_topk_ms=" << topk_seconds * 1000.0
                 << " online_total_ms=" << online_time.count() * 1000.0
                 << " comm_ms=" << total_comm_seconds(delta) * 1000.0
                 << " sent_bytes=" << delta.sent
                 << " transport_rounds=" << total_rounds(delta)
                 << " logical_rounds=" << total_rounds(delta) / 2
                 << " call_evaluate_nums=" << call_evaluate_time
                 << " dcf_eval_total_ms=" << total_duration.count() * 1000.0
                 << " topk_label_checksum=" << label_checksum
                 << endl;
        }
    }
    catch (...)
    {
        delete player;
        throw;
    }

    delete player;
    return 0;
}
