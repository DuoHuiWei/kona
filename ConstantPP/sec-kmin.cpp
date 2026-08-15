#include "sec-kmin.hpp"

#include <limits>
#include <stdexcept>

#include "constantpp-comm.hpp"

namespace ConstantPP
{
namespace
{

inline Ring public_share(
        std::uint64_t value,
        int playerno)
{
    return playerno == 0
            ? Ring(static_cast<mp_limb_t>(value))
            : Ring(0);
}

std::vector<Ring> open_bits_to_both(
        RealTwoPartyPlayer* player,
        const std::vector<Ring>& local_bits,
        ProtocolStats* stats)
{
    std::vector<Ring> peer_bits;
    exchange_ring_vector(
            player,
            local_bits,
            peer_bits,
            stats);

    std::vector<Ring> clear(local_bits.size());
    for (std::size_t i = 0; i < local_bits.size(); ++i)
        clear[i] = local_bits[i] + peer_bits[i];

    if (stats)
        ++stats->logical_rounds;

    return clear;
}

} // namespace

std::vector<Ring> sec_kmin_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& distance_share,
        const std::vector<Ring>& label_share,
        std::size_t k,
        const ShufflePartyMaterial& shuffle_material,
        const SecBComUpperTrianglePlan& bcom_plan,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats,
        SecBComStats* bcom_stats,
        SecKMinStats* kmin_stats)
{
    if (!player)
        throw std::invalid_argument(
                "ConstantPP SecKMin: null RealTwoPartyPlayer");
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument(
                "ConstantPP SecKMin: playerno must be 0 or 1");
    if (distance_share.size() != label_share.size())
        throw std::invalid_argument(
                "ConstantPP SecKMin: distance/label size mismatch");

    const std::size_t n = distance_share.size();
    if (k > n)
        throw std::invalid_argument(
                "ConstantPP SecKMin: k exceeds n");

    if (kmin_stats)
        kmin_stats->reset();

    if (k == 0)
        return {};

    /*
     * Step 1: secure shuffle of aligned distance-label shares.
     * This contributes exactly two ConstantPP logical rounds.
     */
    auto shuffled =
            sec_shuffle_value_label(
                    player,
                    playerno,
                    distance_share,
                    label_share,
                    shuffle_material,
                    protocol_stats);

    if (shuffled.size() != 2)
        throw std::runtime_error(
                "ConstantPP SecKMin: shuffle returned invalid columns");

    const auto& shuffled_distance = shuffled[0];
    const auto& shuffled_label = shuffled[1];

    /*
     * Edge case k == n:
     * every shuffled item is selected.  Returning here avoids unsigned
     * underflow in n-k-1.  Normal ConstantPP benchmark cases use 1 <= k < n,
     * where the standard five-round SecKMin path below is executed.
     */
    if (k == n)
    {
        if (kmin_stats)
        {
            kmin_stats->selected_count = n;
            kmin_stats->threshold_compare_pairs = 0;
            kmin_stats->threshold_dcf_evaluate_calls = 0;
            kmin_stats->threshold_dcf_evaluate_ms = 0.0;
        }
        return shuffled_label;
    }

    /*
     * Step 2: source-like upper-triangle rank count.
     * For CompareOp::Less:
     *   su_i = #{j != i | x_i < x_j}
     *
     * One ConstantPP logical round.
     */
    const auto su_share =
            sec_bcom_kona_dcf(
                    player,
                    playerno,
                    shuffled_distance,
                    CompareOp::Less,
                    bcom_plan,
                    dcf_compare,
                    protocol_stats,
                    bcom_stats);

    /*
     * Repository-style threshold stage:
     *
     *   u_i = 1{su_i > n-k-1}
     *
     * The public ConstantPP repository reuses one GT-FSS key across all
     * candidate threshold tests rather than generating pair-specific keys.
     * We keep that engineering logic, but replace the repository's
     * single-process/shared-state evaluation with Kona Compare64 so the
     * comparison remains a real two-process secure DCF comparison.
     *
     * No su_i is reconstructed in plaintext here.
     */
    const std::uint64_t threshold =
            static_cast<std::uint64_t>(n - k - 1);

    std::vector<Ring> threshold_share(
            n, public_share(threshold, playerno));

    std::vector<int> threshold_idx(2 * n);
    for (std::size_t i = 0; i < n; ++i)
    {
        if (i > static_cast<std::size_t>(
                    std::numeric_limits<int>::max()))
            throw std::overflow_error(
                    "ConstantPP SecKMin: index exceeds int range");

        /*
         * Compare64 expects both operands inside one values vector.
         * We concatenate:
         *   [su_0,...,su_{n-1}, threshold_0,...,threshold_{n-1}]
         */
        threshold_idx[2 * i] =
                static_cast<int>(i);
        threshold_idx[2 * i + 1] =
                static_cast<int>(n + i);
    }

    std::vector<Ring> threshold_values;
    threshold_values.reserve(2 * n);
    threshold_values.insert(
            threshold_values.end(),
            su_share.begin(),
            su_share.end());
    threshold_values.insert(
            threshold_values.end(),
            threshold_share.begin(),
            threshold_share.end());

    std::vector<Ring> u_share(2 * n);

    const auto before = dcf_compare.get_stats();

    /*
     * greater_than=true gives [1{su_i > threshold}].
     * All n predicates are evaluated as one ConstantPP comparison stage.
     */
    dcf_compare.compare_in_vec(
            threshold_values,
            threshold_idx,
            u_share,
            true);

    const auto after = dcf_compare.get_stats();

    if (protocol_stats)
        ++protocol_stats->logical_rounds;

    /*
     * Step 9: open all indicator shares.
     * Compare64 duplicates each pair output in slots 2*i and 2*i+1;
     * consume 2*i only.
     */
    std::vector<Ring> u_local(n);
    for (std::size_t i = 0; i < n; ++i)
        u_local[i] = u_share[2 * i];

    const auto u_clear =
            open_bits_to_both(
                    player,
                    u_local,
                    protocol_stats);

    std::vector<Ring> selected_labels;
    selected_labels.reserve(k);

    for (std::size_t i = 0; i < n; ++i)
    {
        const std::uint64_t u =
                static_cast<std::uint64_t>(
                        u_clear[i].get_limb(0));

        if (u == 1)
            selected_labels.push_back(
                    shuffled_label[i]);
        else if (u != 0)
            throw std::runtime_error(
                    "ConstantPP SecKMin: opened indicator is not a bit");
    }

    /*
     * Under the source-like upper-triangle total-order semantics,
     * exactly k positions should be selected even in the presence of ties.
     */
    if (selected_labels.size() != k)
        throw std::runtime_error(
                "ConstantPP SecKMin: selected count is not exactly k");

    if (kmin_stats)
    {
        kmin_stats->selected_count =
                selected_labels.size();
        kmin_stats->threshold_compare_pairs = n;
        kmin_stats->threshold_dcf_evaluate_calls =
                static_cast<std::uint64_t>(
                        after.evaluate_calls -
                        before.evaluate_calls);
        kmin_stats->threshold_dcf_evaluate_ms =
                (after.evaluate_time -
                 before.evaluate_time).count() * 1000.0;
    }

    return selected_labels;
}

} // namespace ConstantPP
