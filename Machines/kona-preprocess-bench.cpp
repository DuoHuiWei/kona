#include "kona-bench-affinity.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../Math/Z2k.hpp"
#include "../Tools/random.h"
#include "kona-dcf-test-material.hpp"

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

using KonaDcfTestMaterial::gen_fake_dcf;

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
    KonaBenchAffinity::bind_fixed_cpu(0); // Single-process setup/structure experiment.
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
