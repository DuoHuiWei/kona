#include "kona-bench-affinity.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "kona-cong-kona-adapter.hpp"
#include "kona-cong-rotated-msb-adapter.hpp"

namespace
{
const int K = 64;
using Share = Z2<K>;
using SharePair = std::array<Share, 2>;

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

Share additive_share(int party, uint64_t domain, Share value)
{
    const Share mask = ring(mix64(domain));
    return party == 0 ? mask : value - mask;
}

RealTwoPartyPlayer* start_networking(int party, int port_base)
{
    Names names;
    Server::start_networking(
            names, party, 2, "localhost", port_base, Names::DEFAULT_PORT);
    return new RealTwoPartyPlayer(names, 1 - party, 0);
}

std::vector<std::pair<uint64_t, uint64_t> > open_top_k(
        const std::vector<SharePair>& shares,
        int k,
        RealTwoPartyPlayer* player)
{
    const std::size_t n = shares.size();
    std::vector<Share> distances(k), payloads(k);
    for (int i = 0; i < k; ++i)
    {
        distances[i] = shares[n - k + i][0];
        payloads[i] = shares[n - k + i][1];
    }
    const auto opened_distances =
            KonaShareConversion::open_additive_batch_l2<K>(distances, player);
    const auto opened_payloads =
            KonaShareConversion::open_additive_batch_l2<K>(payloads, player);
    std::vector<std::pair<uint64_t, uint64_t> > out(k);
    for (int i = 0; i < k; ++i)
        out[i] = {
            static_cast<uint64_t>(opened_distances[i].get_limb(0)),
            static_cast<uint64_t>(opened_payloads[i].get_limb(0))};
    return out;
}
}

int main(int argc, const char** argv)
{
    if (argc < 2)
        throw std::runtime_error(
                "usage: kona-cong-rotated-msb-test.x PARTY [-pn PORT]");
    const int party = std::stoi(argv[1]);
    int port_base = 13000;
    for (int i = 2; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "-pn")
            port_base = std::stoi(argv[++i]);
    KonaBenchAffinity::bind_fixed_cpu(party);
    RealTwoPartyPlayer* player = start_networking(party, port_base);

    const int n = 32;
    const int k = 7;
    std::vector<SharePair> original(n);
    std::vector<std::pair<uint64_t, uint64_t> > clear(n);
    for (int i = 0; i < n; ++i)
    {
        // Unique, bounded keys keep signed-difference comparison valid and
        // make payload association unambiguous.
        const uint64_t distance =
                (mix64(1000 + i) & ((uint64_t(1) << 45) - 1)) * n + i;
        const uint64_t payload = 100000 + i;
        clear[i] = {distance, payload};
        original[i][0] = additive_share(party, i, ring(distance));
        original[i][1] = additive_share(party, 100000 + i, ring(payload));
    }

    std::vector<SharePair> pcr = original;
    std::vector<SharePair> rmsb = original;
    KonaCongKonaAdapter::cong_top_k_with_pcr<K>(pcr, k, true, player);
    KonaCongRotatedMsbAdapter::cong_top_k_with_rotated_msb<K>(
            rmsb, k, true, player, 3);

    const auto pcr_top_k = open_top_k(pcr, k, player);
    const auto rmsb_top_k = open_top_k(rmsb, k, player);
    std::sort(clear.begin(), clear.end());
    const std::vector<std::pair<uint64_t, uint64_t> > expected(
            clear.begin(), clear.begin() + k);
    const bool ok = pcr_top_k == expected && rmsb_top_k == expected &&
            rmsb_top_k == pcr_top_k;
    if (party == 0)
        std::cout << "CONG_RMSB_VS_PCR n=" << n << " k=" << k << " "
                  << (ok ? "PASS" : "FAIL") << std::endl;
    delete player;
    return ok ? 0 : 1;
}
