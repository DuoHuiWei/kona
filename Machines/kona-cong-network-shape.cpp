#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../Tools/ezOptionParser.h"
#include "kona-cong-topk.hpp"

using namespace std;

namespace
{

ez::ezOptionParser opt;
string size_list = "16,1024";
int topk_k = 3;
bool compare_reference = true;
string mode = "shape";
int merge_k_min = 3;
int merge_k_max = 8;

struct ComparatorRecord
{
    int left = 0;
    int right = 0;
    int original_depth = 0;
};

vector<int> parse_sizes(const string& text)
{
    vector<int> out;
    size_t start = 0;
    while (start < text.size())
    {
        size_t comma = text.find(',', start);
        string token = text.substr(start,
                comma == string::npos ? string::npos : comma - start);
        if (!token.empty())
            out.push_back(stoi(token));
        if (comma == string::npos)
            break;
        start = comma + 1;
    }
    if (out.empty())
        throw runtime_error("size list must not be empty");
    return out;
}

void parse_argv(int argc, const char** argv)
{
    opt.add("16,1024", 0, 1, 0, "Comma-separated N list", "-n", "--sizes");
    opt.add("3", 0, 1, 0, "Top-k", "-k", "--topk");
    opt.add("shape", 0, 1, 0, "Mode: shape or merge-delta", "-m", "--mode");
    opt.add("3", 0, 1, 0, "Minimum k for merge-delta mode", "-kmn", "--k-min");
    opt.add("8", 0, 1, 0, "Maximum k for merge-delta mode", "-kmx", "--k-max");
    opt.add("1", 0, 1, 0,
            "Check known N=16,k=3 paper target if present", "-r", "--reference");
    opt.parse(argc, argv);

    opt.get("--sizes")->getString(size_list);
    opt.get("--topk")->getInt(topk_k);
    opt.get("--mode")->getString(mode);
    opt.get("--k-min")->getInt(merge_k_min);
    opt.get("--k-max")->getInt(merge_k_max);
    int ref = 1;
    opt.get("--reference")->getInt(ref);
    compare_reference = (ref != 0);

    if (mode != "shape" && mode != "merge-delta" && mode != "mu-block")
        throw runtime_error("mode must be shape, merge-delta, or mu-block");
    if (merge_k_min <= 0 || merge_k_max < merge_k_min)
        throw runtime_error("invalid merge-delta k range");
}

size_t count_comparators(const vector<KonaCongTopK::CongLevel>& levels)
{
    size_t total = 0;
    for (const auto& level : levels)
        total += level.size();
    return total;
}

vector<ComparatorRecord> flatten_records(
        const KonaCongTopK::CongNetwork& network)
{
    vector<ComparatorRecord> records;
    for (size_t depth = 0; depth < network.levels.size(); ++depth)
    {
        for (const auto& pair : network.levels[depth])
        {
            ComparatorRecord r;
            r.left = pair[0];
            r.right = pair[1];
            r.original_depth = static_cast<int>(depth);
            records.push_back(r);
        }
    }
    return records;
}

vector<KonaCongTopK::CongLevel> reschedule_by_wire_dependencies(
        const vector<ComparatorRecord>& records,
        int n)
{
    vector<int> last_depth(n, -1);
    vector<KonaCongTopK::CongLevel> levels;

    for (const auto& r : records)
    {
        if (r.left < 0 || r.left >= n || r.right < 0 || r.right >= n)
            throw runtime_error("comparator wire index out of range");
        int depth = max(last_depth[r.left], last_depth[r.right]) + 1;
        if (depth >= static_cast<int>(levels.size()))
            levels.resize(depth + 1);

        levels[depth].push_back({r.left, r.right});
        last_depth[r.left] = depth;
        last_depth[r.right] = depth;
    }

    return levels;
}

bool levels_have_same_counts(
        const vector<KonaCongTopK::CongLevel>& a,
        const vector<KonaCongTopK::CongLevel>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].size() != b[i].size())
            return false;
    return true;
}

void print_depth_counts(
        const string& prefix,
        const vector<KonaCongTopK::CongLevel>& levels)
{
    for (size_t depth = 0; depth < levels.size(); ++depth)
    {
        cout << prefix
             << " depth=" << depth
             << " comparators=" << levels[depth].size()
             << endl;
    }
}

void print_network_shape(int n, int k)
{
    KonaCongTopK::CongNetwork network =
            KonaCongTopK::build_cong_network(n, k);
    vector<ComparatorRecord> records = flatten_records(network);
    vector<KonaCongTopK::CongLevel> dependency_levels =
            reschedule_by_wire_dependencies(records, n);

    const size_t total = count_comparators(network.levels);
    const size_t dependency_total = count_comparators(dependency_levels);
    const bool same_dependency_counts =
            levels_have_same_counts(network.levels, dependency_levels);

    cout << "CONG_NETWORK_SHAPE"
         << " N=" << n
         << " k=" << k
         << " total_comparators=" << total
         << " network_depth=" << network.levels.size()
         << " dependency_total_comparators=" << dependency_total
         << " dependency_depth=" << dependency_levels.size()
         << " level_counts_match_dependency_schedule="
         << (same_dependency_counts ? 1 : 0)
         << endl;

    print_depth_counts("CONG_NETWORK_LEVEL", network.levels);

    if (!same_dependency_counts)
    {
        cout << "CONG_DEPENDENCY_RESCHEDULE"
             << " N=" << n
             << " k=" << k
             << " note=earliest_depth_counts_differ_from_generated_levels"
             << endl;
        print_depth_counts("CONG_DEPENDENCY_LEVEL", dependency_levels);
    }

    if (compare_reference && n == 16 && k == 3)
    {
        const bool pass_total = (total == 35);
        const bool pass_depth = (network.levels.size() == 9);
        cout << "CONG_REFERENCE_CHECK"
             << " N=16"
             << " k=3"
             << " expected_total_comparators=35"
             << " actual_total_comparators=" << total
             << " expected_network_depth=9"
             << " actual_network_depth=" << network.levels.size()
             << " pass=" << (pass_total && pass_depth ? 1 : 0)
             << endl;
    }

    cout << "CONG_STAGE_INFO"
         << " N=" << n
         << " k=" << k
         << " available=0"
         << " reason=current_CongNetwork_stores_only_levels_and_output_wires"
         << endl;
}


vector<int> level_counts(const vector<KonaCongTopK::CongLevel>& levels)
{
    vector<int> counts;
    counts.reserve(levels.size());
    for (const auto& level : levels)
        counts.push_back(static_cast<int>(level.size()));
    return counts;
}

string format_vector(const vector<int>& values)
{
    string out = "[";
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i)
            out += ",";
        out += to_string(values[i]);
    }
    out += "]";
    return out;
}

vector<int> wires_from(int start, int count)
{
    vector<int> wires(count);
    for (int i = 0; i < count; ++i)
        wires[i] = start + i;
    return wires;
}

vector<int> merge_level_counts(int left_count, int right_count, int k)
{
    vector<int> left = wires_from(0, left_count);
    vector<int> right = wires_from(left_count, right_count);
    auto result = KonaCongTopK::build_merge_network(left, right, k);
    return level_counts(result.second);
}

vector<int> padded_delta(const vector<int>& levels, size_t width)
{
    vector<int> delta(width, 0);
    for (size_t i = 0; i < levels.size() && i < width; ++i)
        delta[i] = levels[i];
    return delta;
}

bool nondecreasing(const vector<int>& values)
{
    for (size_t i = 1; i < values.size(); ++i)
        if (values[i] < values[i - 1])
            return false;
    return true;
}

int candidate_value(const string& name, int c, int t)
{
    if (name == "min(c,t)")
        return min(c, t);
    if (name == "min(c,floor(t/2))")
        return min(c, t / 2);
    if (name == "min(c,ceil(t/2))")
        return min(c, (t + 1) / 2);
    if (name == "min(c,2*t)")
        return min(c, 2 * t);
    if (name == "min(c,2*floor(t/2))")
        return min(c, 2 * (t / 2));
    if (name == "min(c,2*ceil(t/2))")
        return min(c, 2 * ((t + 1) / 2));
    if (name == "2*min(c,t)")
        return 2 * min(c, t);
    if (name == "2*min(c,floor(t/2))")
        return 2 * min(c, t / 2);
    if (name == "2*min(c,ceil(t/2))")
        return 2 * min(c, (t + 1) / 2);
    return -1;
}

string identify_simple_formula(const vector<int>& values, int k)
{
    const vector<string> names = {
        "min(c,t)",
        "min(c,floor(t/2))",
        "min(c,ceil(t/2))",
        "min(c,2*t)",
        "min(c,2*floor(t/2))",
        "min(c,2*ceil(t/2))",
        "2*min(c,t)",
        "2*min(c,floor(t/2))",
        "2*min(c,ceil(t/2))",
    };

    for (const string& name : names)
    {
        for (int c = 0; c <= 2 * k; ++c)
        {
            bool ok = true;
            for (int t = 0; t <= k; ++t)
            {
                if (candidate_value(name, c, t) != values.at(t))
                {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return name + ",c=" + to_string(c);
        }
    }

    return "none";
}

void print_merge_delta_for_k(int k)
{
    vector<int> pattern = merge_level_counts(k, k, k);
    const size_t width = pattern.size();
    vector<vector<int>> deltas(k + 1);

    int pattern_total = 0;
    for (int value : pattern)
        pattern_total += value;

    cout << "MERGE_PATTERN"
         << " k=" << k
         << " P_k=" << format_vector(pattern)
         << " total_comparators=" << pattern_total
         << " network_depth=" << pattern.size()
         << endl;

    for (int t = 0; t <= k; ++t)
    {
        vector<int> levels = merge_level_counts(k, t, k);
        vector<int> delta = padded_delta(levels, width);
        deltas[t] = delta;
        int total = 0;
        for (int value : levels)
            total += value;

        cout << "MERGE_SHAPE"
             << " k=" << k
             << " t=" << t
             << " total_comparators=" << total
             << " network_depth=" << levels.size()
             << " levels=" << format_vector(levels)
             << endl;
        cout << "MERGE_DELTA"
             << " k=" << k
             << " t=" << t
             << " delta=" << format_vector(delta)
             << endl;
    }

    for (size_t j = 0; j < width; ++j)
    {
        vector<int> values;
        values.reserve(k + 1);
        for (int t = 0; t <= k; ++t)
            values.push_back(deltas[t][j]);

        const int saturation_value = values.back();
        int first_saturation_t = k;
        for (int t = 0; t <= k; ++t)
        {
            if (values[t] == saturation_value)
            {
                first_saturation_t = t;
                break;
            }
        }
        string formula = identify_simple_formula(values, k);

        cout << "MERGE_LAYER_ANALYSIS"
             << " k=" << k
             << " j=" << j
             << " values_by_t=" << format_vector(values)
             << " monotone=" << (nondecreasing(values) ? 1 : 0)
             << " saturation_value=" << saturation_value
             << " first_saturation_t=" << first_saturation_t
             << " simple_formula=" << formula
             << endl;

        if (formula == "none")
        {
            cout << "MERGE_SIMPLE_FORM_COUNTEREXAMPLE"
                 << " k=" << k
                 << " j=" << j
                 << " values_by_t=" << format_vector(values)
                 << endl;
        }
    }
}



struct MergeFormulaResult
{
    bool pass = true;
    int fail_k = 0;
    int fail_t = 0;
    int fail_layer = 0;
    int actual = 0;
    int predicted = 0;
    vector<int> fail_pattern;
    vector<int> fail_saturation;
};

int ceil_div_int(int numerator, int denominator)
{
    if (denominator <= 0)
        throw runtime_error("ceil_div_int denominator must be positive");
    return (numerator + denominator - 1) / denominator;
}

vector<int> saturation_times_from_deltas(const vector<vector<int>>& deltas)
{
    const int k = static_cast<int>(deltas.size()) - 1;
    const size_t width = deltas.back().size();
    vector<int> saturation(width, k);
    for (size_t j = 0; j < width; ++j)
    {
        const int target = deltas[k][j];
        for (int t = 0; t <= k; ++t)
        {
            if (deltas[t][j] == target)
            {
                saturation[j] = t;
                break;
            }
        }
    }
    return saturation;
}

int predicted_delta(int p_j, int s_j, int t)
{
    if (t == 0 || p_j == 0)
        return 0;
    return min(p_j, ceil_div_int(p_j * t, s_j));
}

MergeFormulaResult verify_unified_formula_for_k(
        int k,
        vector<int>* pattern_out = 0,
        vector<int>* saturation_out = 0)
{
    vector<int> pattern = merge_level_counts(k, k, k);
    const size_t width = pattern.size();
    vector<vector<int>> deltas(k + 1);
    for (int t = 0; t <= k; ++t)
        deltas[t] = padded_delta(merge_level_counts(k, t, k), width);

    vector<int> saturation = saturation_times_from_deltas(deltas);
    if (pattern_out)
        *pattern_out = pattern;
    if (saturation_out)
        *saturation_out = saturation;

    MergeFormulaResult result;
    for (int t = 0; t <= k; ++t)
    {
        for (size_t j = 0; j < width; ++j)
        {
            const int actual = deltas[t][j];
            const int predicted = predicted_delta(pattern[j], saturation[j], t);
            if (actual != predicted)
            {
                result.pass = false;
                result.fail_k = k;
                result.fail_t = t;
                result.fail_layer = static_cast<int>(j);
                result.actual = actual;
                result.predicted = predicted;
                result.fail_pattern = pattern;
                result.fail_saturation = saturation;
                return result;
            }
        }
    }
    return result;
}

void print_power_bucket_observation(int k, const vector<int>& pattern,
        const vector<int>& saturation)
{
    int r = 0;
    while ((1 << r) < k)
        ++r;
    int lower = r == 0 ? 0 : (1 << (r - 1));
    int upper = 1 << r;
    cout << "MERGE_POWER_BUCKET"
         << " k=" << k
         << " bucket=(" << lower << "," << upper << "]"
         << " offset_from_lower=" << (k - lower)
         << " P_k=" << format_vector(pattern)
         << " saturation_t=" << format_vector(saturation)
         << endl;
}

void verify_and_print_unified_formula_range()
{
    MergeFormulaResult first_failure;
    bool all_pass = true;
    vector<pair<vector<int>, vector<int>>> summaries;
    summaries.resize(merge_k_max + 1);

    for (int k = merge_k_min; k <= merge_k_max; ++k)
    {
        vector<int> pattern;
        vector<int> saturation;
        MergeFormulaResult result = verify_unified_formula_for_k(
                k, &pattern, &saturation);
        summaries[k] = make_pair(pattern, saturation);
        if (!result.pass && all_pass)
        {
            all_pass = false;
            first_failure = result;
        }
    }

    if (all_pass)
    {
        cout << "MERGE_UNIFIED_FORMULA_CHECK"
             << " k_min=" << merge_k_min
             << " k_max=" << merge_k_max
             << " result=PASS"
             << " formula=delta_j(t,k)=min(p_j(k),ceil(p_j(k)*t/s_j(k)))"
             << endl;
        for (int k = merge_k_min; k <= merge_k_max; ++k)
        {
            cout << "MERGE_UNIFIED_FORMULA_SUMMARY"
                 << " k=" << k
                 << " P_k=" << format_vector(summaries[k].first)
                 << " saturation_t=" << format_vector(summaries[k].second)
                 << endl;
        }
    }
    else
    {
        cout << "MERGE_UNIFIED_FORMULA_CHECK"
             << " k_min=" << merge_k_min
             << " k_max=" << merge_k_max
             << " result=FAIL"
             << endl;
        cout << "MERGE_UNIFIED_FORMULA_COUNTEREXAMPLE"
             << " k=" << first_failure.fail_k
             << " t=" << first_failure.fail_t
             << " layer=" << first_failure.fail_layer
             << " actual=" << first_failure.actual
             << " predicted=" << first_failure.predicted
             << " P_k=" << format_vector(first_failure.fail_pattern)
             << " saturation_t=" << format_vector(first_failure.fail_saturation)
             << endl;
    }

    cout << "MERGE_POWER_BUCKET_OBSERVATION"
         << " note=P_k_and_saturation_t_are_reported_per_k_for_manual_floor_ceil_min_inspection"
         << endl;
    for (int k = merge_k_min; k <= merge_k_max; ++k)
        print_power_bucket_observation(k, summaries[k].first, summaries[k].second);
}



int ceil_log2_power(int k)
{
    int mu = 1;
    while (mu < k)
        mu <<= 1;
    return mu;
}

vector<KonaCongTopK::CongLevel> sort_levels_for_size(int r, int k)
{
    if (r <= 1)
        return vector<KonaCongTopK::CongLevel>();
    vector<int> wires = wires_from(0, r);
    auto result = KonaCongTopK::build_sort_network(wires, min(k, r));
    return result.second;
}


vector<int> parallel_sum_counts(const vector<vector<int>>& pieces)
{
    size_t depth = 0;
    for (const auto& p : pieces)
        depth = max(depth, p.size());
    vector<int> result(depth, 0);
    for (const auto& p : pieces)
        for (size_t i = 0; i < p.size(); ++i)
            result[i] += p[i];
    return result;
}

struct MuNode
{
    int candidate_count = 0;
    bool is_tail = false;
};

struct MuStageInfo
{
    int stage = 0;
    int full_k_nodes = 0;
    bool tail_exists = false;
    int tail_candidate_count = 0;
    bool tail_participates = false;
};

struct MuBlockPrediction
{
    vector<int> predicted_levels;
    int mu = 0;
    int block_count = 0;
    int remainder = 0;
    int tail_candidate_count = 0;
    int tail_first_merge_stage = -1;
    vector<MuStageInfo> stages;
};

MuBlockPrediction predict_mu_block_model(int n, int k)
{
    MuBlockPrediction pred;
    pred.mu = ceil_log2_power(k);
    pred.block_count = n / pred.mu;
    pred.remainder = n % pred.mu;
    pred.tail_candidate_count = pred.remainder == 0 ? 0 : min(k, pred.remainder);

    vector<vector<int>> initial_pieces;
    vector<MuNode> nodes;
    vector<int> full_sort_counts = level_counts(sort_levels_for_size(pred.mu, k));
    for (int i = 0; i < pred.block_count; ++i)
    {
        initial_pieces.push_back(full_sort_counts);
        nodes.push_back(MuNode{k, false});
    }
    if (pred.remainder > 0)
    {
        initial_pieces.push_back(level_counts(sort_levels_for_size(pred.remainder, k)));
        nodes.push_back(MuNode{pred.tail_candidate_count, true});
    }

    vector<int> init_counts = parallel_sum_counts(initial_pieces);
    pred.predicted_levels.insert(pred.predicted_levels.end(),
            init_counts.begin(), init_counts.end());

    vector<int> p_k = merge_level_counts(k, k, k);
    vector<vector<int>> delta(k + 1);
    for (int t = 0; t <= k; ++t)
        delta[t] = merge_level_counts(k, t, k);

    int stage = 0;
    while (nodes.size() > 1)
    {
        MuStageInfo info;
        info.stage = stage;
        vector<vector<int>> merge_pieces;
        vector<MuNode> next;

        for (size_t i = 0; i < nodes.size(); i += 2)
        {
            if (i + 1 == nodes.size())
            {
                next.push_back(nodes[i]);
                continue;
            }

            MuNode a = nodes[i];
            MuNode b = nodes[i + 1];
            const bool has_tail = a.is_tail || b.is_tail;
            int t = 0;
            if (has_tail)
                t = a.is_tail ? a.candidate_count : b.candidate_count;

            if (has_tail)
            {
                merge_pieces.push_back(delta.at(t));
                info.tail_participates = true;
                if (pred.tail_first_merge_stage < 0)
                    pred.tail_first_merge_stage = stage;
            }
            else
            {
                merge_pieces.push_back(p_k);
            }

            next.push_back(MuNode{k, has_tail});
        }

        for (const auto& node : nodes)
        {
            if (node.is_tail)
            {
                info.tail_exists = true;
                info.tail_candidate_count = node.candidate_count;
            }
            else if (node.candidate_count == k)
                ++info.full_k_nodes;
        }
        pred.stages.push_back(info);

        vector<int> merged_counts = parallel_sum_counts(merge_pieces);
        pred.predicted_levels.insert(pred.predicted_levels.end(),
                merged_counts.begin(), merged_counts.end());
        nodes = next;
        ++stage;
    }

    return pred;
}

void print_mu_block_case(int n, int k, bool verbose)
{
    KonaCongTopK::CongNetwork network = KonaCongTopK::build_cong_network(n, k);
    vector<int> actual = level_counts(network.levels);
    MuBlockPrediction pred = predict_mu_block_model(n, k);
    bool match = actual == pred.predicted_levels;

    cout << "MU_BLOCK_MODEL"
         << " N=" << n
         << " k=" << k
         << " mu=" << pred.mu
         << " actual_levels=" << format_vector(actual)
         << " predicted_levels=" << format_vector(pred.predicted_levels)
         << " match=" << (match ? "YES" : "NO")
         << endl;

    if (!match || verbose)
    {
        int mismatch = -1;
        size_t width = max(actual.size(), pred.predicted_levels.size());
        for (size_t i = 0; i < width; ++i)
        {
            int a = i < actual.size() ? actual[i] : -1;
            int b = i < pred.predicted_levels.size() ? pred.predicted_levels[i] : -1;
            if (a != b)
            {
                mismatch = static_cast<int>(i);
                break;
            }
        }
        cout << "MU_BLOCK_MODEL_DETAIL"
             << " N=" << n
             << " k=" << k
             << " first_mismatch_layer=" << mismatch
             << " actual_value=" << (mismatch >= 0 && mismatch < (int)actual.size() ? actual[mismatch] : -1)
             << " predicted_value=" << (mismatch >= 0 && mismatch < (int)pred.predicted_levels.size() ? pred.predicted_levels[mismatch] : -1)
             << " block_count=" << pred.block_count
             << " remainder=" << pred.remainder
             << " tail_candidate_count=" << pred.tail_candidate_count
             << " tail_first_merge_stage=" << pred.tail_first_merge_stage
             << " actual_levels=" << format_vector(actual)
             << " predicted_levels=" << format_vector(pred.predicted_levels)
             << endl;
        for (const auto& st : pred.stages)
        {
            cout << "MU_BLOCK_STAGE"
                 << " N=" << n
                 << " k=" << k
                 << " stage=" << st.stage
                 << " full_k_nodes=" << st.full_k_nodes
                 << " tail_exists=" << (st.tail_exists ? 1 : 0)
                 << " tail_candidate_count=" << st.tail_candidate_count
                 << " tail_participates=" << (st.tail_participates ? 1 : 0)
                 << endl;
        }
    }
}

void run_mu_block_model_checks()
{
    struct Range { int k; int first_n; int last_n; };
    const vector<Range> ranges = {{3, 16, 32}, {5, 32, 64}, {8, 32, 80}};
    bool all_pass = true;
    int first_fail_n = 0;
    int first_fail_k = 0;

    for (const auto& range : ranges)
    {
        for (int n = range.first_n; n <= range.last_n; ++n)
        {
            KonaCongTopK::CongNetwork network = KonaCongTopK::build_cong_network(n, range.k);
            vector<int> actual = level_counts(network.levels);
            MuBlockPrediction pred = predict_mu_block_model(n, range.k);
            if (actual != pred.predicted_levels && all_pass)
            {
                all_pass = false;
                first_fail_n = n;
                first_fail_k = range.k;
            }
            print_mu_block_case(n, range.k, false);
            if (!all_pass && first_fail_n == n && first_fail_k == range.k)
                break;
        }
        if (!all_pass)
            break;
    }

    cout << "MU_BLOCK_MODEL_CHECK"
         << " result=" << (all_pass ? "PASS" : "FAIL")
         << endl;
    if (!all_pass)
    {
        print_mu_block_case(first_fail_n, first_fail_k, true);
        cout << "MU_BLOCK_MODEL_FAILURE_REASON_HINT"
             << " reason=likely_chunk_size_non_simple_mu_block_bisection_or_left_right_subtree_depth_alignment"
             << endl;
    }
}

void print_merge_delta_range()
{
    cout << "MERGE_DELTA_MAPPING"
         << " left_size=k"
         << " right_size=t"
         << " merge_k=k"
         << " meaning=direct_call_to_current_build_merge_network_left_full_block_right_tail"
         << endl;

    for (int k = merge_k_min; k <= merge_k_max; ++k)
        print_merge_delta_for_k(k);

    verify_and_print_unified_formula_range();
}

} // namespace

int main(int argc, const char** argv)
{
    parse_argv(argc, argv);
    if (mode == "merge-delta")
    {
        print_merge_delta_range();
        return 0;
    }
    if (mode == "mu-block")
    {
        run_mu_block_model_checks();
        return 0;
    }

    const vector<int> ns = parse_sizes(size_list);

    for (int n : ns)
    {
        if (topk_k > n)
            continue;
        print_network_shape(n, topk_k);
    }

    return 0;
}
