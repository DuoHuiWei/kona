#include "sec-bcom.hpp"

#include <limits>
#include <stdexcept>

namespace ConstantPP
{
namespace
{

inline Ring public_one_share(int playerno)
{
    /*
     * Additive sharing of public 1:
     *   P0 owns 1
     *   P1 owns 0
     */
    return playerno == 0 ? Ring(1) : Ring(0);
}

void validate_plan(
        const SecBComUpperTrianglePlan& plan,
        std::size_t n)
{
    if (plan.n != n)
        throw std::invalid_argument(
                "ConstantPP SecBCom: plan/input size mismatch");

    const std::size_t expected =
            n > 0 ? n * (n - 1) / 2 : 0;

    if (plan.pairs.size() != expected)
        throw std::invalid_argument(
                "ConstantPP SecBCom: incomplete upper-triangle plan");

    std::size_t t = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        for (std::size_t j = i + 1; j < n; ++j, ++t)
        {
            if (plan.pairs[t].i != i ||
                    plan.pairs[t].j != j)
                throw std::invalid_argument(
                        "ConstantPP SecBCom: malformed pair order");
        }
    }
}

} // namespace

std::vector<Ring> sec_bcom_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        CompareOp op,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats,
        SecBComStats* bcom_stats)
{
    /*
     * Convenience overload. Do not use this overload inside the formal
     * online timer if topology construction is intended to be setup/offline.
     */
    const auto plan =
            make_sec_bcom_upper_triangle_plan(x_share.size());

    return sec_bcom_kona_dcf(
            player,
            playerno,
            x_share,
            op,
            plan,
            dcf_compare,
            protocol_stats,
            bcom_stats);
}

std::vector<Ring> sec_bcom_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        CompareOp op,
        const SecBComUpperTrianglePlan& plan,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats,
        SecBComStats* bcom_stats)
{
    if (!player)
        throw std::invalid_argument(
                "ConstantPP SecBCom: null RealTwoPartyPlayer");
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument(
                "ConstantPP SecBCom: playerno must be 0 or 1");

    const std::size_t n = x_share.size();
    validate_plan(plan, n);

    std::vector<Ring> su_share(n, Ring(0));
    if (plan.pairs.empty())
    {
        /*
         * No secure comparison occurs for n <= 1, so do not claim a logical
         * communication round that did not actually happen.
         */
        if (bcom_stats)
            bcom_stats->reset();
        return su_share;
    }

    /*
     * Kona Compare64 consumes a flat pair index vector:
     *   [i0,j0, i1,j1, ...]
     *
     * compare_res uses the same flat length and duplicates each pair output
     * in positions 2*t and 2*t+1. We consume only 2*t.
     */
    std::vector<int> compare_idx(2 * plan.pairs.size());
    for (std::size_t t = 0; t < plan.pairs.size(); ++t)
    {
        if (plan.pairs[t].i > static_cast<std::size_t>(
                    std::numeric_limits<int>::max()) ||
                plan.pairs[t].j > static_cast<std::size_t>(
                    std::numeric_limits<int>::max()))
            throw std::overflow_error(
                    "ConstantPP SecBCom: pair index exceeds int range");

        compare_idx[2 * t] =
                static_cast<int>(plan.pairs[t].i);
        compare_idx[2 * t + 1] =
                static_cast<int>(plan.pairs[t].j);
    }

    std::vector<Ring> compare_res(compare_idx.size());

    const auto dcf_before = dcf_compare.get_stats();

    /*
     * Compare64 semantics verified by Kona's own PCR/DCF correctness bench:
     *   greater_than=true  -> share of 1{x_i > x_j}
     *   greater_than=false -> share of 1{x_i < x_j}
     *
     * Compare64 performs the real masked opening and local DCF evaluation.
     * No x_i or x_j is reconstructed here.
     */
    const bool greater_than = (op == CompareOp::Greater);
    dcf_compare.compare_in_vec(
            x_share,
            compare_idx,
            compare_res,
            greater_than);

    const auto dcf_after = dcf_compare.get_stats();

    /*
     * ConstantPP logical level: all pair comparisons belong to one
     * stage, independent of Compare64's internal transport chunking.
     */
    if (protocol_stats)
        ++protocol_stats->logical_rounds;

    const Ring one_share = public_one_share(playerno);

    for (std::size_t t = 0; t < plan.pairs.size(); ++t)
    {
        const std::size_t i = plan.pairs[t].i;
        const std::size_t j = plan.pairs[t].j;

        const Ring b_share = compare_res[2 * t];

        /*
         * For both modes:
         *
         *   Less:
         *     b = 1{x_i < x_j}
         *
         *   Greater:
         *     b = 1{x_i > x_j}
         *
         * The forward count gets b.
         * The reverse count gets 1-b, using a public-one additive share:
         *
         *   [1-b]_0 = 1 - [b]_0
         *   [1-b]_1 =   - [b]_1
         *
         * This reproduces the source-like upper-triangle total ordering,
         * including deterministic handling of equality.
         */
        su_share[i] += b_share;
        su_share[j] += one_share - b_share;
    }

    if (bcom_stats)
    {
        bcom_stats->unordered_pairs = plan.pairs.size();
        bcom_stats->dcf_compare_pairs = plan.pairs.size();
        bcom_stats->kona_dcf_evaluate_calls =
                static_cast<std::uint64_t>(
                        dcf_after.evaluate_calls -
                        dcf_before.evaluate_calls);
        bcom_stats->kona_dcf_evaluate_ms =
                (dcf_after.evaluate_time -
                 dcf_before.evaluate_time).count() * 1000.0;
    }

    return su_share;
}

} // namespace ConstantPP
