#include "kona-bench-affinity.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "kona-nhss-distance.hpp"

int main(int argc, char** argv)
{
    KonaBenchAffinity::bind_fixed_cpu(0);
    unsigned lambda = 512;
    if (argc > 1)
        lambda = static_cast<unsigned>(std::stoul(argv[1]));

    std::cout << "NHSS correctness test, lambda=" << lambda << std::endl;

    KonaNhss::KeyPair kp = KonaNhss::keygen(lambda, 20260829UL);

    gmp_randclass rng(gmp_randinit_mt);
    rng.seed(20260830UL);

    // One-dimensional check.
    const uint64_t x = 12345;
    const uint64_t y = 12001;

    KonaNhss::SharedDataPair xs =
        KonaNhss::share_data(x, kp.sec, rng);
    KonaNhss::SharedQueryPair ys =
        KonaNhss::share_query(y, kp.pub, rng);

    const uint64_t d0 =
        KonaNhss::nised_distance_subtractive_share_u64(
            0, xs.party[0], ys.party[0], kp.pub);
    const uint64_t d1 =
        KonaNhss::nised_distance_subtractive_share_u64(
            1, xs.party[1], ys.party[1], kp.pub);

    const uint64_t reconstructed = d1 - d0;
    const uint64_t clear =
        (x >= y ? (x - y) * (x - y) : (y - x) * (y - x));

    std::cout << "1D clear=" << clear
              << " nhss=" << reconstructed << std::endl;
    assert(reconstructed == clear);

    // Multi-dimensional check.
    const std::vector<uint64_t> xv = {10, 20, 30, 40, 50, 60};
    const std::vector<uint64_t> yv = {9, 22, 28, 45, 41, 63};

    std::vector<KonaNhss::DataShare> x0, x1;
    std::vector<KonaNhss::QueryShare> y0, y1;

    uint64_t clear_sum = 0;
    for (std::size_t i = 0; i < xv.size(); ++i)
    {
        KonaNhss::SharedDataPair xd =
            KonaNhss::share_data(xv[i], kp.sec, rng);
        KonaNhss::SharedQueryPair yq =
            KonaNhss::share_query(yv[i], kp.pub, rng);

        x0.push_back(xd.party[0]);
        x1.push_back(xd.party[1]);
        y0.push_back(yq.party[0]);
        y1.push_back(yq.party[1]);

        const int64_t diff =
            static_cast<int64_t>(xv[i]) - static_cast<int64_t>(yv[i]);
        clear_sum += static_cast<uint64_t>(diff * diff);
    }

    const Z2<64> a0 =
        KonaNhss::nised_squared_euclidean_additive_share_l2(
            0, x0, y0, kp.pub);
    const Z2<64> a1 =
        KonaNhss::nised_squared_euclidean_additive_share_l2(
            1, x1, y1, kp.pub);

    const uint64_t got =
        static_cast<uint64_t>((a0 + a1).get_limb(0));

    std::cout << "ND clear=" << clear_sum
              << " nhss=" << got << std::endl;
    assert(got == clear_sum);

    std::cout << "PASS" << std::endl;
    return 0;
}
