#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "../Math/Z2k.hpp"
#include "kona-cong-topk.hpp"

using namespace std;

namespace
{

const int K = 64;

uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

template<class T>
void local_compare_and_swap(
        std::vector<T>& shares,
        const std::vector<int>& compare_idx_vec)
{
    for (size_t i = 0; i < compare_idx_vec.size(); i += 2)
    {
        int left = compare_idx_vec[i];
        int right = compare_idx_vec[i + 1];
        uint64_t left_value = static_cast<uint64_t>(shares[left][0].get_limb(0));
        uint64_t right_value = static_cast<uint64_t>(shares[right][0].get_limb(0));
        if (left_value > right_value)
            std::swap(shares[left], shares[right]);
    }
}

} // namespace

int main()
{
    using Pair = std::array<Z2<K>, 2>;

    {
        KonaCongTopK::CongNetwork network =
                KonaCongTopK::build_cong_network(16, 3);
        int comparator_count = 0;
        for (size_t i = 0; i < network.levels.size(); i++)
            comparator_count += (int)network.levels[i].size();

        if (comparator_count != 35)
            throw runtime_error("expected 35 comparators for n=16,k=3");
        if ((int)network.levels.size() != 9)
            throw runtime_error("expected depth 9 for n=16,k=3");

        cout << "CONG_NETWORK_SHAPE PASS" << endl;
    }

    for (int n = 1; n <= 64; n++)
    {
        for (int k = 1; k <= n; k++)
        {
            vector<Pair> shares(n);
            vector<uint64_t> values(n);
            for (int i = 0; i < n; i++)
            {
                values[i] = mix64(uint64_t(n) * 1000 + uint64_t(k) * 100 + i)
                        & ((uint64_t(1) << 50) - 1);
                shares[i][0] = Z2<K>(static_cast<mp_limb_t>(values[i]));
                shares[i][1] = Z2<K>(static_cast<mp_limb_t>(i));
            }

            KonaCongTopK::cong_top_k<Pair>(
                    shares,
                    n,
                    k,
                    [&](std::vector<Pair>&, const std::vector<int>&) {},
                    [&](std::vector<Pair>& local_shares,
                            const std::vector<int>& compare_idx_vec) {
                        local_compare_and_swap(local_shares, compare_idx_vec);
                    });

            vector<uint64_t> expected = values;
            sort(expected.begin(), expected.end());

            for (int i = 0; i < k; i++)
            {
                uint64_t got =
                        static_cast<uint64_t>(shares[n - k + i][0].get_limb(0));
                if (got != expected[i])
                    throw runtime_error("top-k mismatch");
            }
        }
    }

    cout << "CONG_TOPK_CLEAR PASS" << endl;
    return 0;
}
