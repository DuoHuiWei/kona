#include "sec-kc.hpp"

#include <stdexcept>

namespace ConstantPP
{

Ring sec_kc_kona(
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
        SecKCStats* kc_stats)
{
    if (!player)
        throw std::invalid_argument("ConstantPP SecKC: null RealTwoPartyPlayer");
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument("ConstantPP SecKC: playerno must be 0 or 1");
    if (database_share.size() != label_share.size())
        throw std::invalid_argument("ConstantPP SecKC: database/label size mismatch");
    if (k > label_share.size())
        throw std::invalid_argument("ConstantPP SecKC: k exceeds database size");

    if (kc_stats)
        kc_stats->reset();

    const std::uint64_t rounds_before =
            protocol_stats ? protocol_stats->logical_rounds : 0;

    std::vector<Ring> distance_share;
    distance_share.reserve(database_share.size());

    for (const auto& row_share : database_share)
    {
        if (row_share.size() != query_share.size())
            throw std::invalid_argument("ConstantPP SecKC: feature dimension mismatch");

        distance_share.push_back(
                sec_ed_n_dim(
                        player,
                        playerno,
                        row_share,
                        query_share,
                        ed_material,
                        protocol_stats));
    }

    SecBComStats kmin_bcom_stats;
    SecKMinStats kmin_stats;
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
                    &max_stats);

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

    return result;
}

} // namespace ConstantPP
