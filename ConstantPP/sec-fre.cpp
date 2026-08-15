#include "sec-fre.hpp"

#include <stdexcept>

#include "sec-bcom-material.hpp"

namespace ConstantPP
{
namespace
{

inline Ring public_one_share(int playerno)
{
    return playerno == 0 ? Ring(1) : Ring(0);
}

} // namespace

std::vector<Ring> sec_fre_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& label_share,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats,
        SecFreStats* fre_stats)
{
    if (!player)
        throw std::invalid_argument(
                "ConstantPP SecFre: null RealTwoPartyPlayer");
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument(
                "ConstantPP SecFre: playerno must be 0 or 1");

    const std::size_t k = label_share.size();

    if (fre_stats)
        fre_stats->reset();

    std::vector<Ring> frequency_share(
            k, public_one_share(playerno));

    if (k < 2)
        return frequency_share;

    const auto plan =
            make_sec_bcom_upper_triangle_plan(k);

    SecEqStats eq_stats;
    const auto eq_share =
            sec_eq_pairs_kona_dcf(
                    player,
                    playerno,
                    label_share,
                    plan,
                    dcf_compare,
                    protocol_stats,
                    &eq_stats);

    if (eq_share.size() != plan.pairs.size())
        throw std::runtime_error(
                "ConstantPP SecFre: equality result size mismatch");

    for (std::size_t t = 0;
            t < plan.pairs.size(); ++t)
    {
        const auto i = plan.pairs[t].i;
        const auto j = plan.pairs[t].j;

        frequency_share[i] += eq_share[t];
        frequency_share[j] += eq_share[t];
    }

    if (fre_stats)
    {
        fre_stats->upper_triangle_eq_predicates =
                plan.pairs.size();
        fre_stats->eq = eq_stats;
    }

    return frequency_share;
}

} // namespace ConstantPP
