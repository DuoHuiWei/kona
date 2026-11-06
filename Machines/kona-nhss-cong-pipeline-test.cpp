#include "kona-bench-affinity.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../Networking/Player.h"
#include "../Networking/Server.h"
#include "kona-nhss-cong-pipeline.hpp"

namespace
{
using RingShare64 = Z2<64>;

uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

RingShare64 ring(uint64_t x)
{
    return RingShare64(static_cast<mp_limb_t>(x));
}

RingShare64 additive_share(int party, uint64_t domain, RingShare64 value)
{
    const RingShare64 mask = ring(mix64(domain));
    return party == 0 ? mask : value - mask;
}

RealTwoPartyPlayer* start_networking(int party, int port_base)
{
    Names names;
    Server::start_networking(
            names, party, 2, "localhost", port_base, Names::DEFAULT_PORT);
    return new RealTwoPartyPlayer(names, 1 - party, 0);
}
}

int main(int argc, const char** argv)
{
    if (argc < 2)
        throw std::runtime_error(
                "usage: kona-nhss-cong-pipeline-test.x PARTY [-pn PORT]");
    const int party = std::stoi(argv[1]);
    int port_base = 14000;
    for (int i = 2; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "-pn")
            port_base = std::stoi(argv[++i]);
    KonaBenchAffinity::bind_fixed_cpu(party);
    RealTwoPartyPlayer* player = start_networking(party, port_base);

    const std::vector<std::vector<uint64_t> > database_clear = {
        {3, 8, 12}, {5, 9, 15}, {12, 2, 7}, {7, 7, 7},
        {20, 4, 3}, {1, 1, 1}, {8, 11, 14}, {6, 10, 13},
        {13, 9, 2}, {4, 6, 11}, {15, 15, 15}, {2, 12, 9}};
    const std::vector<uint64_t> query_clear = {6, 9, 13};
    const int k = 4;

    // Deterministic dealer/client preprocessing. Both test processes derive
    // identical offline material and retain only their own party share.
    const KonaNhss::KeyPair keys = KonaNhss::keygen(512, 20260829UL);
    gmp_randclass rng(gmp_randinit_mt);
    rng.seed(20260830UL);
    std::vector<KonaNhssCongPipeline::RecordShare> database(
            database_clear.size());
    for (std::size_t i = 0; i < database_clear.size(); ++i)
    {
        for (uint64_t coordinate : database_clear[i])
        {
            const auto pair = KonaNhss::share_data(coordinate, keys.sec, rng);
            database[i].coordinates.push_back(pair.party[party]);
        }
        database[i].payload_share =
                additive_share(party, 500000 + i, ring(1000 + i));
    }
    std::vector<KonaNhss::QueryShare> query;
    for (uint64_t coordinate : query_clear)
    {
        const auto pair = KonaNhss::share_query(coordinate, keys.pub, rng);
        query.push_back(pair.party[party]);
    }

    const auto result = KonaNhssCongPipeline::run(
            party, database, query, keys.pub, k, player,
            2, // dimensions per local NHSS batch
            3  // comparator pairs per RMSB/swap batch
    );
    std::vector<RingShare64> distance_shares(k), payload_shares(k);
    for (int i = 0; i < k; ++i)
    {
        distance_shares[i] = result[result.size() - k + i][0];
        payload_shares[i] = result[result.size() - k + i][1];
    }
    const auto distances =
            KonaShareConversion::open_additive_batch_l2<64>(distance_shares, player);
    const auto payloads =
            KonaShareConversion::open_additive_batch_l2<64>(payload_shares, player);

    std::vector<std::pair<uint64_t, uint64_t> > expected;
    for (std::size_t i = 0; i < database_clear.size(); ++i)
    {
        uint64_t distance = 0;
        for (std::size_t j = 0; j < query_clear.size(); ++j)
        {
            const int64_t difference =
                    static_cast<int64_t>(database_clear[i][j]) -
                    static_cast<int64_t>(query_clear[j]);
            distance += static_cast<uint64_t>(difference * difference);
        }
        expected.push_back({distance, 1000 + i});
    }
    std::sort(expected.begin(), expected.end());

    bool ok = true;
    for (int i = 0; i < k; ++i)
    {
        const uint64_t distance =
                static_cast<uint64_t>(distances[i].get_limb(0));
        const uint64_t payload =
                static_cast<uint64_t>(payloads[i].get_limb(0));
        if (std::make_pair(distance, payload) != expected[i])
        {
            ok = false;
            if (party == 0)
                std::cerr << "pipeline mismatch i=" << i
                          << " got=(" << distance << "," << payload << ")"
                          << " expected=(" << expected[i].first << ","
                          << expected[i].second << ")" << std::endl;
            break;
        }
    }
    if (party == 0)
        std::cout << "NHSS_CONG_RMSB_PIPELINE n=" << database.size()
                  << " dim=" << query.size() << " k=" << k << " "
                  << (ok ? "PASS" : "FAIL") << std::endl;
    // Complete classifier extension: pure RMSB Cong and batch DCF vote.
    for(size_t i=0;i<database.size();++i)
        database[i].payload_share=additive_share(party,600000+i,ring(i%2));
    KonaDcfCompare::Compare64<64> dcf(player,party);
    const auto winning_share=KonaNhssCongPipeline::classify(
        party,database,query,keys.pub,k,2,player,dcf);
    const auto winning=KonaShareConversion::open_additive_batch_l2<64>({winning_share},player);
    size_t counts[2]={0,0};
    for(int i=0;i<k;++i)++counts[(expected[i].second-1000)%2];
    ok=ok&&winning[0].get_limb(0)==uint64_t(counts[1]>counts[0]);
    if(party==0)std::cout<<"NHSS_CONG_RMSB_DCF_VOTE "<<(ok?"PASS":"FAIL")<<std::endl;
    delete player;
    return ok ? 0 : 1;
}
