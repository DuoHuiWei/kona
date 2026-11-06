#include "kona-bench-affinity.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "kona-rotated-msb-compare.hpp"

namespace
{
using Share = Z2<64>;

uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

Share ring(uint64_t x)
{
    return Share(static_cast<mp_limb_t>(x));
}

std::vector<Share> make_shares(const std::vector<uint64_t>& values,
        int playerno, uint64_t domain)
{
    std::vector<Share> out(values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        const Share mask = ring(mix64(domain + i));
        out[i] = playerno == 0 ? mask : ring(values[i]) - mask;
    }
    return out;
}

RealTwoPartyPlayer* start_networking(int playerno, int port_base)
{
    Names names;
    Server::start_networking(
            names, playerno, 2, "localhost", port_base, Names::DEFAULT_PORT);
    return new RealTwoPartyPlayer(names, 1 - playerno, 0);
}

bool equal_without_opening_flags(const std::vector<Share>& left,
        const std::vector<Share>& right, RealTwoPartyPlayer* player)
{
    if (left.size() != right.size())
        return false;
    std::vector<Share> differences(left.size());
    for (std::size_t i = 0; i < left.size(); ++i)
        differences[i] = left[i] - right[i];
    const auto opened =
            KonaShareConversion::open_additive_batch_l2<64>(differences, player);
    for (const auto& value : opened)
        if (static_cast<uint64_t>(value.get_limb(0)) != 0)
            return false;
    return true;
}
}

int main(int argc, const char** argv)
{
    if (argc < 2)
        throw std::runtime_error("usage: kona-rotated-msb-compare-test.x PARTY [-pn PORT]");
    const int playerno = std::stoi(argv[1]);
    int port_base = 10000;
    for (int i = 2; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "-pn")
            port_base = std::stoi(argv[++i]);
    KonaBenchAffinity::bind_fixed_cpu(playerno);
    RealTwoPartyPlayer* player = start_networking(playerno, port_base);

    // 0/1, 1/0, adjacent values in both directions, and equality.
    std::vector<uint64_t> x = {0, 1, 7, 8, 100, 101, 42, 42};
    std::vector<uint64_t> y = {1, 0, 8, 7, 101, 100, 42, 42};
    for (std::size_t i = 0; i < 256; ++i)
    {
        x.push_back(mix64(i + 1000) & ((uint64_t(1) << 50) - 1));
        y.push_back(mix64(i + 2000) & ((uint64_t(1) << 50) - 1));
    }

    const auto xs = make_shares(x, playerno, 0x100000);
    const auto ys = make_shares(y, playerno, 0x200000);
    const auto rmsb_lt = KonaRotatedMsbCompare::rotated_msb_compare_lt_batch_l2(xs, ys, player);
    const auto rmsb_gt = KonaRotatedMsbCompare::rotated_msb_compare_gt_batch_l2(xs, ys, player);
    const auto pcr_lt = KonaPcrCompare::pcr_compare_lt_batch_l2(xs, ys, player);
    const auto pcr_gt = KonaPcrCompare::pcr_compare_gt_batch_l2(xs, ys, player);

    // Open only RMSB-PCR differences, never either comparison flag.
    const bool ok = equal_without_opening_flags(rmsb_lt, pcr_lt, player) &&
            equal_without_opening_flags(rmsb_gt, pcr_gt, player);
    if (playerno == 0)
        std::cout << "ROTATED_MSB_VS_PCR n=" << x.size() << " "
                  << (ok ? "PASS" : "FAIL") << std::endl;
    delete player;
    return ok ? 0 : 1;
}
