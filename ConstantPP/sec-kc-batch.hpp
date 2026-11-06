#ifndef CONSTANTPP_SEC_KC_BATCH_HPP_
#define CONSTANTPP_SEC_KC_BATCH_HPP_

#include <cstddef>
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

#include "../Machines/kona-dcf-compare.hpp"
#include "../Networking/Player.h"

#include "constantpp-types.hpp"
#include "sec-bcom.hpp"
#include "sec-ed-batch.hpp"
#include "sec-fre.hpp"
#include "sec-kmin.hpp"
#include "sec-max.hpp"
#include "sec-mul.hpp"
#include "sec-shuffle.hpp"

namespace ConstantPP
{

struct SecKCStageStats
{
    double online_ms = 0.0;
    double comm_ms = 0.0;
    std::uint64_t sent_bytes = 0;
    std::uint64_t transport_rounds = 0;
    std::uint64_t logical_rounds = 0;
    std::uint64_t dcf_evaluate_calls = 0;
    double dcf_evaluate_ms = 0.0;

    void reset()
    {
        online_ms = 0.0;
        comm_ms = 0.0;
        sent_bytes = 0;
        transport_rounds = 0;
        logical_rounds = 0;
        dcf_evaluate_calls = 0;
        dcf_evaluate_ms = 0.0;
    }
};

struct SecKCBatchStats
{
    std::uint64_t distance_count = 0;
    SecKCStageStats online;
    SecKCStageStats distance;
    SecKCStageStats topk;
    SecKCStageStats vote;
    SecBComStats kmin_bcom;
    SecKMinStats kmin;
    SecFreStats fre;
    SecMaxStats max;
    std::uint64_t logical_rounds = 0;

    void reset()
    {
        distance_count = 0;
        online.reset();
        distance.reset();
        topk.reset();
        vote.reset();
        kmin_bcom.reset();
        kmin.reset();
        fre.reset();
        max.reset();
        logical_rounds = 0;
    }
};

// Optional deferred instrumentation: capture only at boundaries during the call.
// The caller finalizes AFTER its independent outer online end snapshot.
struct SecKCStageSnapshot
{
    std::chrono::steady_clock::time_point start;
    NamedCommStats comm;
    std::uint64_t logical_rounds = 0;
    KonaDcfCompare::Compare64<RING_BITS>::Stats dcf;
};
struct SecKCBatchTrace
{
    std::array<SecKCStageSnapshot, 6> snapshots;
};
void finalize_sec_kc_batch_stats(const SecKCBatchTrace& trace,
        SecKCBatchStats& stats);

Ring sec_kc_batch_kona(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& query_share,
        const std::vector<std::vector<Ring>>& database_share,
        const std::vector<Ring>& label_share,
        std::size_t k,
        const SecEdPartyMaterial& ed_material,
        const ShufflePartyMaterial& shuffle_material,
        const SecBComUpperTrianglePlan& bcom_plan,
        const BeaverTripleParty& reused_triple,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats = nullptr,
        SecKCBatchStats* kc_stats = nullptr,
        SecKCBatchTrace* deferred_trace = nullptr,
        SecMaxLocalFssL2* secmax_performance_l2 = nullptr);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_KC_BATCH_HPP_
