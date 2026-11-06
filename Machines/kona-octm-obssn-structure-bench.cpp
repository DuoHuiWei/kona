#include "kona-bench-affinity.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "kona-ordered-block-terminal-networks.hpp"
#include "kona-reduced-depth-cong-topk.hpp"

using namespace std;
using namespace KonaOrderedBlockTerminal;

namespace
{

struct Candidate
{
    uint64_t distance = 0;
    uint64_t label = 0;
    uint64_t stable_id = 0;
};

bool less_key(const Candidate& a, const Candidate& b)
{
    return tie(a.distance, a.stable_id) < tie(b.distance, b.stable_id);
}

vector<Candidate> execute(
        const TerminalNetwork& network, vector<Candidate> lanes)
{
    if (lanes.size() != network.input_count)
        throw runtime_error("network input count mismatch");
    for (const auto& layer : network.layers)
        for (const auto& c : layer)
            if (less_key(lanes[c.right], lanes[c.left]))
                swap(lanes[c.left], lanes[c.right]);
    vector<Candidate> result;
    for (auto lane : network.output_lanes)
        result.push_back(lanes[lane]);
    return result;
}

vector<Candidate> reference(vector<Candidate> lanes, size_t k)
{
    stable_sort(lanes.begin(), lanes.end(), less_key);
    lanes.resize(k);
    return lanes;
}

vector<tuple<uint64_t,uint64_t,uint64_t>> keys(vector<Candidate> values)
{
    vector<tuple<uint64_t,uint64_t,uint64_t>> result;
    for (const auto& x : values)
        result.emplace_back(x.distance, x.stable_id, x.label);
    sort(result.begin(), result.end());
    return result;
}

bool check_network(const TerminalNetwork& network,
        const vector<Candidate>& input, size_t k, bool require_sorted)
{
    auto got = execute(network, input);
    auto expected = reference(input, k);
    if (keys(got) != keys(expected))
        return false;
    if (require_sorted && !is_sorted(got.begin(), got.end(), less_key))
        return false;
    return true;
}

vector<Candidate> flatten(const vector<vector<uint64_t>>& blocks)
{
    vector<Candidate> result;
    uint64_t id = 0;
    for (const auto& block : blocks)
        for (auto value : block)
            result.push_back({value, id % 13, id++});
    return result;
}

void deterministic_tests()
{
    const vector<vector<vector<uint64_t>>> cases = {
        {{1,4,7,10,13},{2,3,8,9,12}},
        {{1,20,30,40,50},{2,3,60,70,80},
         {4,5,6,90,91},{7,8,9,10,11}},
        {{1,2,3,4,5},{100,101,102,103,104},
         {200,201,202,203,204},{300,301,302,303,304}},
        {{1,10,20,30,40},{2,11,21,31,41},
         {3,12,22,32,42},{4,13,23,33,43}},
        {{1,2,2,4,8},{1,2,3,4,8},{2,2,5,6,9}},
        {{1,5,9,12,20},{2,3,8,11,21},{4,7}}
    };
    for (const auto& blocks : cases)
    {
        vector<size_t> sizes;
        for (const auto& b : blocks) sizes.push_back(b.size());
        auto input = flatten(blocks);
        auto octm = build_octm(sizes, 5);
        auto obssn = build_obssn(sizes, 5);
        if (!check_network(octm, input, 5, true) ||
                !check_network(obssn, input, 5, false))
            throw runtime_error("deterministic correctness failed");
    }
}

uint64_t exhaustive_test(size_t block_count, size_t k,
        const TerminalNetwork& network, bool sorted)
{
    uint64_t cases = 1;
    for (size_t i = 0; i < block_count; ++i) cases *= (k + 1);
    for (uint64_t code = 0; code < cases; ++code)
    {
        uint64_t x = code, id = 0;
        vector<Candidate> input;
        for (size_t b = 0; b < block_count; ++b)
        {
            const size_t zeros = x % (k + 1); x /= (k + 1);
            for (size_t j = 0; j < k; ++j)
                input.push_back({j < zeros ? 0u : 1u, id % 7, id++});
        }
        if (!check_network(network, input, k, sorted))
            throw runtime_error("zero-one exhaustive correctness failed");
    }
    return cases;
}

uint64_t random_test(const vector<size_t>& sizes, size_t k,
        const TerminalNetwork& network, bool sorted,
        uint64_t seed, size_t count)
{
    mt19937_64 gen(seed);
    for (size_t trial = 0; trial < count; ++trial)
    {
        vector<Candidate> input;
        uint64_t id = 0;
        for (size_t size : sizes)
        {
            vector<Candidate> block;
            for (size_t j = 0; j < size; ++j)
                block.push_back({gen() % 128, gen() % 17, id++});
            stable_sort(block.begin(), block.end(), less_key);
            input.insert(input.end(), block.begin(), block.end());
        }
        if (!check_network(network, input, k, sorted))
            throw runtime_error("random correctness failed at trial " +
                    to_string(trial));
    }
    return count;
}

string join_sizes(const vector<size_t>& sizes)
{
    ostringstream out;
    for (size_t i = 0; i < sizes.size(); ++i)
    {
        if (i) out << '+';
        out << sizes[i];
    }
    return out.str();
}

string layer_counts(const TerminalNetwork& network)
{
    ostringstream out;
    for (size_t i = 0; i < network.layers.size(); ++i)
    {
        if (i) out << ';';
        out << network.layers[i].size();
    }
    return out.str();
}

uint64_t exhaustive_case_count(size_t block_count, size_t k)
{
    if (k != 5 || (block_count != 2 && block_count != 3 &&
            block_count != 4 && block_count != 6))
        return 0;
    uint64_t result = 1;
    for (size_t i = 0; i < block_count; ++i)
        result *= 6;
    return result;
}

pair<size_t,double> width_stats(const TerminalNetwork& network)
{
    size_t maximum = 0, total = 0;
    for (const auto& layer : network.layers)
    {
        maximum = max(maximum, layer.size());
        total += layer.size();
    }
    return {maximum, network.layers.empty() ? 0.0 :
            double(total) / network.layers.size()};
}

void verify_obssn_regressions()
{
    const vector<tuple<size_t,size_t,uint64_t,uint32_t>> expected = {
        {2,5,15,5},{2,17,153,17},{2,31,496,31},
        {3,5,30,10},{3,17,306,34},{3,31,992,62},
        {4,5,44,10},{4,17,420,34},{4,31,1344,62},
        {7,5,86,13},{7,17,781,43},{7,31,2472,77},
        {8,5,101,13},{8,17,871,43},{8,31,2727,77},
        {13,5,175,15},{13,17,1401,48},{13,31,4322,87},
        {16,5,225,15},{16,17,1657,48},{16,31,5024,87}
    };
    for (const auto& e : expected)
    {
        size_t b,k; uint64_t c; uint32_t d;
        tie(b,k,c,d)=e;
        auto n=build_obssn(vector<size_t>(b,k),k);
        if (n.comparator_count()!=c || n.depth()!=d)
            throw runtime_error("OB-SSN regression mismatch B="+
                    to_string(b)+" K="+to_string(k)+" got="+
                    to_string(n.comparator_count())+"/"+
                    to_string(n.depth()));
    }
}

} // namespace

int main(int argc, char** argv)
{
    KonaBenchAffinity::bind_fixed_cpu(0); // Single-process setup/structure experiment.
    const string output_dir = argc > 1 ? argv[1] :
            "KNN-experiment-res/octm-obssn-structure";
    const string mkdir_cmd = "mkdir -p '" + output_dir + "'";
    if (system(mkdir_cmd.c_str()) != 0)
        throw runtime_error("failed to create output directory");

    deterministic_tests();
    verify_obssn_regressions();
    uint64_t exhaustive_total = 0;
    for (size_t b : {2u,3u,4u,6u})
    {
        vector<size_t> sizes(b,5);
        exhaustive_total += exhaustive_test(b,5,build_octm(sizes,5),true);
        exhaustive_total += exhaustive_test(b,5,build_obssn(sizes,5),false);
    }

    ofstream summary(output_dir + "/octm_obssn_structure_48.csv");
    ofstream octm_layers(output_dir + "/octm_layers_48.csv");
    ofstream obssn_layers(output_dir + "/obssn_layers_48.csv");
    summary << "n,k,block_count,block_sizes,candidate_count,terminal_mode,"
            "cropped_matrix_comparators,rank_mask_comparators,"
            "cropped_terminal_total,cropped_terminal_depth,"
            "comparator_count,structural_depth,comparators_per_layer,"
            "max_layer_width,mean_layer_width,compare_count,swap_count,"
            "output_count,output_sorted,correctness_pass,exhaustive_cases,"
            "random_cases\n";
    octm_layers << "n,k,block_count,layer_index,layer_comparators\n";
    obssn_layers << "n,k,block_count,layer_index,layer_comparators\n";

    const vector<int> ns = {200,256,300,512,800,1024,2000,4096,
            5000,7000,8192,13000,32768,50000,65536,102944};
    const vector<int> ks = {5,17,31};
    size_t structures = 0;
    for (int k : ks) for (int n : ns)
    {
        auto plan=KonaReducedDepthCongTopK::build_reduced_depth_cong_plan(n,k);
        vector<size_t> sizes;
        for (const auto& block : plan.blocks) sizes.push_back(block.keep);
        auto octm=build_octm(sizes,k);
        auto obssn=build_obssn(sizes,k);
        random_test(sizes,k,octm,true,0x0c7a0000ULL+n*131ULL+k,200);
        random_test(sizes,k,obssn,false,0x0b550000ULL+n*131ULL+k,200);

        const uint64_t crop=plan.matrix.pruned_cross_pairs.size();
        const uint64_t mask=plan.matrix.candidate_count;
        for (const auto* net : {&octm,&obssn})
        {
            auto widths=width_stats(*net);
            summary << n << ',' << k << ',' << sizes.size() << ','
                    << join_sizes(sizes) << ',' << net->input_count << ','
                    << net->name << ',' << crop << ',' << mask << ','
                    << crop+mask << ",2," << net->comparator_count() << ','
                    << net->depth() << ',' << layer_counts(*net) << ','
                    << widths.first << ',' << fixed << setprecision(6)
                    << widths.second << ',' << net->comparator_count() << ','
                    << net->comparator_count() << ',' << net->output_count
                    << ',' << (net->name=="OCTM-Graph"?1:0)
                    << ",1," << exhaustive_case_count(sizes.size(), k)
                    << ",200\n";
        }
        for (size_t d=0;d<octm.layers.size();++d)
            octm_layers << n<<','<<k<<','<<sizes.size()<<','<<d<<','
                    <<octm.layers[d].size()<<'\n';
        for (size_t d=0;d<obssn.layers.size();++d)
            obssn_layers << n<<','<<k<<','<<sizes.size()<<','<<d<<','
                    <<obssn.layers[d].size()<<'\n';
        ++structures;
    }
    cout << "PASS structures=" << structures
         << " deterministic=6 exhaustive_network_cases=" << exhaustive_total
         << " random_cases_per_network=200 output_dir=" << output_dir << endl;
}
