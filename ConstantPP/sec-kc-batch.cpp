#include "sec-kc-batch.hpp"

#include <chrono>
#include <stdexcept>

namespace ConstantPP
{
namespace
{

using Clock = std::chrono::steady_clock;

using StageSnapshot = SecKCStageSnapshot;

std::uint64_t total_transport_rounds(const NamedCommStats& stats)
{
    std::uint64_t rounds = 0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        rounds += static_cast<std::uint64_t>(it->second.rounds);
    return rounds;
}

double total_comm_ms(NamedCommStats stats)
{
    double seconds = 0.0;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        seconds += it->second.timer.elapsed();
    return seconds * 1000.0;
}

StageSnapshot begin_stage(
        RealTwoPartyPlayer* player,
        const ProtocolStats* protocol_stats,
        const KonaDcfCompare::Compare64<RING_BITS>& dcf_compare)
{
    StageSnapshot snapshot;
    snapshot.comm = player->total_comm();
    snapshot.logical_rounds =
            protocol_stats ? protocol_stats->logical_rounds : 0;
    snapshot.dcf = dcf_compare.get_stats();
    snapshot.start = Clock::now();
    return snapshot;
}

void finish_stage(const StageSnapshot& before, const StageSnapshot& after,
        SecKCStageStats* stage)
{
    const auto comm_delta = after.comm - before.comm;
    stage->online_ms = std::chrono::duration<double, std::milli>(
            after.start - before.start).count();
    stage->comm_ms = total_comm_ms(comm_delta);
    stage->sent_bytes = comm_delta.sent;
    stage->transport_rounds = total_transport_rounds(comm_delta);
    stage->logical_rounds = after.logical_rounds - before.logical_rounds;
    stage->dcf_evaluate_calls = after.dcf.evaluate_calls - before.dcf.evaluate_calls;
    stage->dcf_evaluate_ms =
            (after.dcf.evaluate_time - before.dcf.evaluate_time).count() * 1000;
}

} // namespace

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
        ProtocolStats* protocol_stats,
        SecKCBatchStats* kc_stats,
        SecKCBatchTrace* deferred_trace,
        SecMaxLocalFssL2* secmax_performance_l2)
{
    if (!player)
        throw std::invalid_argument("ConstantPP Batch SecKC: null RealTwoPartyPlayer");
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument("ConstantPP Batch SecKC: playerno must be 0 or 1");
    if (database_share.size() != label_share.size())
        throw std::invalid_argument("ConstantPP Batch SecKC: database/label size mismatch");
    if (k > label_share.size())
        throw std::invalid_argument("ConstantPP Batch SecKC: k exceeds database size");

    if (kc_stats)
        kc_stats->reset();

    const std::uint64_t rounds_before =
            protocol_stats ? protocol_stats->logical_rounds : 0;

    SecKCBatchTrace local_trace;
    auto& trace = deferred_trace ? *deferred_trace : local_trace;
    trace.snapshots[0] = begin_stage(player, protocol_stats, dcf_compare);
    const auto distance_share =
            sec_ed_batch_n_dim(
                    player,
                    playerno,
                    query_share,
                    database_share,
                    ed_material,
                    protocol_stats);
    trace.snapshots[1] = begin_stage(player, protocol_stats, dcf_compare);

    SecBComStats kmin_bcom_stats;
    SecKMinStats kmin_stats;
    trace.snapshots[2] = begin_stage(player, protocol_stats, dcf_compare);
    const auto selected_labels =
            sec_kmin_kona_dcf(
                    player,
                    playerno,
                    distance_share,
                    label_share,
                    k,
                    shuffle_material,
                    bcom_plan,
                    dcf_compare,
                    protocol_stats,
                    &kmin_bcom_stats,
                    &kmin_stats);
    trace.snapshots[3] = begin_stage(player, protocol_stats, dcf_compare);

    trace.snapshots[4] = begin_stage(player, protocol_stats, dcf_compare);
    SecFreStats fre_stats;
    const auto frequency_share =
            sec_fre_kona_dcf(
                    player,
                    playerno,
                    selected_labels,
                    dcf_compare,
                    protocol_stats,
                    &fre_stats);

    SecMaxStats max_stats;
    Ring result =
            sec_max_kona_dcf(
                    player,
                    playerno,
                    frequency_share,
                    selected_labels,
                    reused_triple,
                    dcf_compare,
                    protocol_stats,
                    &max_stats,
                    secmax_performance_l2);
    trace.snapshots[5] = begin_stage(player, protocol_stats, dcf_compare);

    if (kc_stats)
    {
        kc_stats->distance_count = distance_share.size();
        kc_stats->kmin_bcom = kmin_bcom_stats;
        kc_stats->kmin = kmin_stats;
        kc_stats->fre = fre_stats;
        kc_stats->max = max_stats;
        kc_stats->logical_rounds = protocol_stats ?
                protocol_stats->logical_rounds - rounds_before : 0;
    }

    if (kc_stats && !deferred_trace)
        finalize_sec_kc_batch_stats(trace, *kc_stats);
    return result;
}

void finalize_sec_kc_batch_stats(const SecKCBatchTrace& trace,
        SecKCBatchStats& stats)
{
    SecKCStageStats distance_stage, topk_stage, vote_stage;
    finish_stage(trace.snapshots[0], trace.snapshots[1], &distance_stage);
    finish_stage(trace.snapshots[2], trace.snapshots[3], &topk_stage);
    finish_stage(trace.snapshots[4], trace.snapshots[5], &vote_stage);
    // Local-only SecMax evaluations are not part of the shared comparator cache.
    // Include their REAL work in the deferred vote/total evaluation accounting.
    vote_stage.dcf_evaluate_calls += stats.max.local_fss_calls;
    vote_stage.dcf_evaluate_ms += stats.max.local_fss_ms;
    stats.distance = distance_stage;
    stats.topk = topk_stage;
    stats.vote = vote_stage;
    stats.online.online_ms =
            distance_stage.online_ms +
            topk_stage.online_ms +
            vote_stage.online_ms;
    stats.online.comm_ms =
            distance_stage.comm_ms +
            topk_stage.comm_ms +
            vote_stage.comm_ms;
    stats.online.sent_bytes =
            distance_stage.sent_bytes +
            topk_stage.sent_bytes +
            vote_stage.sent_bytes;
    stats.online.transport_rounds =
            distance_stage.transport_rounds +
            topk_stage.transport_rounds +
            vote_stage.transport_rounds;
    stats.online.logical_rounds =
            distance_stage.logical_rounds +
            topk_stage.logical_rounds +
            vote_stage.logical_rounds;
    stats.online.dcf_evaluate_calls =
            distance_stage.dcf_evaluate_calls +
            topk_stage.dcf_evaluate_calls +
            vote_stage.dcf_evaluate_calls;
    stats.online.dcf_evaluate_ms =
            distance_stage.dcf_evaluate_ms +
            topk_stage.dcf_evaluate_ms +
            vote_stage.dcf_evaluate_ms;
    stats.logical_rounds = stats.online.logical_rounds;
}

} // namespace ConstantPP
