#include "sec-max.hpp"

#include <stdexcept>

#include "sec-bcom-material.hpp"

namespace ConstantPP
{

Ring sec_max_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& frequency_share,
        const std::vector<Ring>& label_share,
        const BeaverTripleParty& reused_mul_triple,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats,
        SecMaxStats* max_stats)
{
    if (!player)
        throw std::invalid_argument(
                "ConstantPP SecMax: null RealTwoPartyPlayer");
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument(
                "ConstantPP SecMax: playerno must be 0 or 1");
    if (frequency_share.size() != label_share.size())
        throw std::invalid_argument(
                "ConstantPP SecMax: frequency/label size mismatch");

    const std::size_t k = frequency_share.size();

    if (max_stats)
        max_stats->reset();

    if (k == 0)
        return Ring(0);

    /*
     * k==1 has no BCom pair and therefore no normal three-round max path.
     * Functionally the sole label is the maximum-frequency label.
     */
    if (k == 1)
        return label_share[0];

    /*
     * Step 1: repository-style upper-triangle greater-than rank counting.
     * The complement direction gives a deterministic total order on ties,
     * matching the source engineering behavior.
     */
    const auto plan =
            make_sec_bcom_upper_triangle_plan(k);

    SecBComStats bcom_stats;
    const auto su_share =
            sec_bcom_kona_dcf(
                    player,
                    playerno,
                    frequency_share,
                    CompareOp::Greater,
                    plan,
                    dcf_compare,
                    protocol_stats,
                    &bcom_stats);

    /*
     * Step 2:
     * exactly one position in the source-like total order has su_i == k-1.
     */
    SecEqStats eq_stats;
    const auto indicator_share =
            sec_eq_public_scalar_kona_dcf(
                    player,
                    playerno,
                    su_share,
                    static_cast<std::uint64_t>(k - 1),
                    dcf_compare,
                    protocol_stats,
                    &eq_stats);

    /*
     * Step 3:
     * multiply every label share by its max-indicator share.
     * Reuse one triple across the vector to match the public repository.
     */
    const auto masked_label_share =
            sec_mul_vector_reuse(
                    player,
                    playerno,
                    label_share,
                    indicator_share,
                    reused_mul_triple,
                    protocol_stats);

    Ring result_share(0);
    for (const auto& x : masked_label_share)
        result_share += x;

    if (max_stats)
    {
        max_stats->bcom = bcom_stats;
        max_stats->eq = eq_stats;
        max_stats->secure_mul_pairs = k;
    }

    return result_share;
}

} // namespace ConstantPP
