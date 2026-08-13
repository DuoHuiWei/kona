#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../Math/Z2k.hpp"
#include "../Tools/random.h"

using namespace std;

namespace
{

const int K = 64;
string dataset_name = "knn-1/tcga-pancan";
int num_features = 0;
int num_train_data = 0;
int num_test_data = 0;
int num_label = 0;

bool file_exists(const string& path)
{
    ifstream file(path.c_str());
    return file.good();
}

void read_meta_data()
{
    string path = "Player-Data/Knn-Data/" + dataset_name + "-data/Knn-meta";
    if (!file_exists(path))
        throw runtime_error("missing meta file: " + path);

    ifstream meta_file(path.c_str());
    meta_file >> num_features;
    meta_file >> num_train_data;
    meta_file >> num_test_data;
    meta_file >> num_label;
    meta_file.close();
}

void gen_fake_dcf(int beta, int n)
{
    int lambda_bytes = 16;
    PRNG prng;
    prng.InitSeed();
    fstream k0, k1, r0, r1, r2;
    k0.open("Player-Data/k0", ios::out);
    k1.open("Player-Data/k1", ios::out);
    r0.open("Player-Data/r0", ios::out);
    r1.open("Player-Data/r1", ios::out);
    r2.open("Player-Data/r2", ios::out);
    octet seed[2][16];
    bigint s[2][2], v[2][2], t[2][2], tmp_t[2], convert[2], tcw[2];
    bigint a, scw, vcw, va, tmp, tmp1, tmp_out;
    prng.InitSeed();
    prng.get(tmp, n);
    bytesFromBigint(&seed[0][0], tmp, lambda_bytes);
    k0 << tmp << " ";
    prng.get(tmp1, n);
    bytesFromBigint(&seed[1][0], tmp1, lambda_bytes);
    k1 << tmp1 << " ";
    prng.get(a, n);
    prng.get(tmp, n);
    r1 << a - tmp << " ";
    r0 << tmp << " ";
    r2 << tmp << " ";
    r0.close();
    r1.close();
    r2.close();
    tmp_t[0] = 0;
    tmp_t[1] = 1;
    int keep, lose;
    va = 0;
    for (int i = 0; i < n - 1; i++)
    {
        keep = bigint(a >> (n - i - 1)).get_ui() & 1;
        lose = 1 ^ keep;
        for (int j = 0; j < 2; j++)
        {
            prng.SetSeed(seed[j]);
            for (int k = 0; k < 2; k++)
            {
                prng.get(t[k][j], 1);
                prng.get(v[k][j], n);
                prng.get(s[k][j], n);
            }
        }
        scw = s[lose][0] ^ s[lose][1];
        bytesFromBigint(&seed[0][0], v[lose][0], lambda_bytes);
        prng.SetSeed(seed[0]);
        prng.get(convert[0], n);
        bytesFromBigint(&seed[0][0], v[lose][1], lambda_bytes);
        prng.SetSeed(seed[0]);
        prng.get(convert[1], n);
        if (tmp_t[1])
            vcw = convert[0] + va - convert[1];
        else
            vcw = convert[1] - convert[0] - va;
        if (keep)
            vcw = vcw + tmp_t[1] * (-beta) + (1 - tmp_t[1]) * beta;
        bytesFromBigint(&seed[0][0], v[keep][0], lambda_bytes);
        prng.SetSeed(seed[0]);
        prng.get(convert[0], n);
        bytesFromBigint(&seed[0][0], v[keep][1], lambda_bytes);
        prng.SetSeed(seed[0]);
        prng.get(convert[1], n);
        va = va - convert[1] + convert[0] + tmp_t[1] * (-vcw) +
                (1 - tmp_t[1]) * vcw;
        tcw[0] = t[0][0] ^ t[0][1] ^ keep ^ 1;
        tcw[1] = t[1][0] ^ t[1][1] ^ keep;
        k0 << scw << " " << vcw << " " << tcw[0] << " " << tcw[1] << " ";
        k1 << scw << " " << vcw << " " << tcw[0] << " " << tcw[1] << " ";
        bytesFromBigint(&seed[0][0], s[keep][0] ^ (tmp_t[0] * scw), lambda_bytes);
        bytesFromBigint(&seed[1][0], s[keep][1] ^ (tmp_t[1] * scw), lambda_bytes);
        bigintFromBytes(tmp_out, &seed[0][0], lambda_bytes);
        bigintFromBytes(tmp_out, &seed[1][0], lambda_bytes);
        tmp_t[0] = t[keep][0] ^ (tmp_t[0] * tcw[keep]);
        tmp_t[1] = t[keep][1] ^ (tmp_t[1] * tcw[keep]);
    }

    prng.SetSeed(seed[0]);
    prng.get(convert[0], n);
    prng.SetSeed(seed[1]);
    prng.get(convert[1], n);
    k0 << tmp_t[1] * (-1 * (convert[1] - convert[0] - va)) +
            (1 - tmp_t[1]) * (convert[1] - convert[0] - va) << " ";
    k1 << tmp_t[1] * (-1 * (convert[1] - convert[0] - va)) +
            (1 - tmp_t[1]) * (convert[1] - convert[0] - va) << " ";
    k0.close();
    k1.close();
}

void generate_triples_save_file_optimized()
{
    PRNG seed;
    seed.ReSeed();
    vector<vector<Z2<K>>> train_triples_0(
            num_train_data, vector<Z2<K>>(num_features, Z2<K>(0)));
    vector<vector<Z2<K>>> test_triples_0(
            num_train_data + 1, vector<Z2<K>>(num_features, Z2<K>(0)));
    vector<vector<Z2<K>>> train_triples_1(
            num_train_data, vector<Z2<K>>(num_features, Z2<K>(0)));
    vector<vector<Z2<K>>> test_triples_1(
            num_train_data + 1, vector<Z2<K>>(num_features, Z2<K>(0)));

    ofstream file_train_triples_0(
            "./Player-Data/Knn-Data/" + dataset_name + "-data/P0-Train-Triples",
            ios::binary);
    ofstream file_test_triples_0(
            "./Player-Data/Knn-Data/" + dataset_name + "-data/P0-Test-Triples",
            ios::binary);
    ofstream file_train_triples_1(
            "./Player-Data/Knn-Data/" + dataset_name + "-data/P1-Train-Triples",
            ios::binary);
    ofstream file_test_triples_1(
            "./Player-Data/Knn-Data/" + dataset_name + "-data/P1-Test-Triples",
            ios::binary);

    for (int i = 0; i < num_features; i++)
    {
        test_triples_0[0][i].randomize(seed);
        test_triples_1[0][i].randomize(seed);
        file_test_triples_0.write(
                reinterpret_cast<char*>(&test_triples_0[0][i]), sizeof(Z2<K>));
        file_test_triples_1.write(
                reinterpret_cast<char*>(&test_triples_0[0][i]), sizeof(Z2<K>));
    }
    for (int i = 0; i < num_features; i++)
        file_test_triples_1.write(
                reinterpret_cast<char*>(&test_triples_1[0][i]), sizeof(Z2<K>));

    for (int i = 0; i < num_train_data; i++)
    {
        for (int j = 0; j < num_features; j++)
        {
            train_triples_0[i][j].randomize(seed);
            train_triples_1[i][j].randomize(seed);
            Z2<K> tmp;
            tmp.randomize(seed);
            test_triples_0[i + 1][j] =
                    (test_triples_0[0][j] + test_triples_1[0][j] -
                            train_triples_0[i][j] - train_triples_1[i][j]) *
                    (test_triples_0[0][j] + test_triples_1[0][j] -
                            train_triples_0[i][j] - train_triples_1[i][j]) - tmp;
            test_triples_1[i + 1][j] = tmp;
            file_train_triples_0.write(
                    reinterpret_cast<char*>(&train_triples_0[i][j]), sizeof(Z2<K>));
            file_train_triples_1.write(
                    reinterpret_cast<char*>(&train_triples_1[i][j]), sizeof(Z2<K>));
            file_test_triples_0.write(
                    reinterpret_cast<char*>(&test_triples_0[i + 1][j]), sizeof(Z2<K>));
            file_test_triples_1.write(
                    reinterpret_cast<char*>(&test_triples_1[i + 1][j]), sizeof(Z2<K>));
        }
    }
}

} // namespace

int main()
{
    using Clock = chrono::steady_clock;

    auto total_start = Clock::now();
    auto meta_start = Clock::now();
    read_meta_data();
    auto meta_end = Clock::now();

    auto dcf_start = Clock::now();
    gen_fake_dcf(1, K);
    auto dcf_end = Clock::now();

    auto triple_start = Clock::now();
    generate_triples_save_file_optimized();
    auto triple_end = Clock::now();
    auto total_end = Clock::now();

    chrono::duration<double> meta_time = meta_end - meta_start;
    chrono::duration<double> dcf_time = dcf_end - dcf_start;
    chrono::duration<double> triple_time = triple_end - triple_start;
    chrono::duration<double> total_time = total_end - total_start;

    cout << "dataset=" << dataset_name << endl;
    cout << "num_features=" << num_features << endl;
    cout << "num_train_data=" << num_train_data << endl;
    cout << "num_test_data=" << num_test_data << endl;
    cout << "meta_read_seconds=" << meta_time.count() << endl;
    cout << "dcf_prep_seconds=" << dcf_time.count() << endl;
    cout << "triple_prep_seconds=" << triple_time.count() << endl;
    cout << "offline_prep_total_seconds=" << total_time.count() << endl;
    return 0;
}
