#include "sec-eq.hpp"

#include <limits>
#include <stdexcept>

namespace ConstantPP
{
namespace
{

inline Ring public_one_share(int playerno)
{
    return playerno == 0 ? Ring(1) : Ring(0);
}

inline Ring public_value_share(
        std::uint64_t value,
        int playerno)
{
    return playerno == 0
            ? Ring(static_cast<mp_limb_t>(value))
            : Ring(0);
}

void validate_player(
        RealTwoPartyPlayer* player,
        int playerno)
{
    if (!player)
        throw std::invalid_argument(
                "ConstantPP SecEq: null RealTwoPartyPlayer");
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument(
                "ConstantPP SecEq: playerno must be 0 or 1");
}

void finish_stats(
        const KonaDcfCompare::Compare64<RING_BITS>::Stats& before,
        const KonaDcfCompare::Compare64<RING_BITS>::Stats& after,
        std::size_t predicates,
        SecEqStats* eq_stats)
{
    if (!eq_stats)
        return;

    eq_stats->equality_predicates = predicates;
    eq_stats->kona_secure_compare_pairs = 2 * predicates;
    eq_stats->kona_dcf_evaluate_calls =
            static_cast<std::uint64_t>(
                    after.evaluate_calls -
                    before.evaluate_calls);
    eq_stats->kona_dcf_evaluate_ms =
            (after.evaluate_time -
             before.evaluate_time).count() * 1000.0;
}

} // namespace

std::vector<Ring> sec_eq_pairs_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        const SecBComUpperTrianglePlan& plan,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats,
        SecEqStats* eq_stats)
{
    validate_player(player, playerno);

    if (plan.n != x_share.size())
        throw std::invalid_argument(
                "ConstantPP SecEq pairs: plan/input size mismatch");

    const std::size_t m = plan.pairs.size();
    if (eq_stats)
        eq_stats->reset();

    if (m == 0)
        return {};

    /*
     * Two strict comparisons per equality predicate:
     *
     *   q=2t:   x_i < x_j
     *   q=2t+1: x_j < x_i
     *
     * Compare64 index storage uses two ints per secure comparison.
     */
    std::vector<int> compare_idx(4 * m);

    for (std::size_t t = 0; t < m; ++t)
    {
        const auto i = plan.pairs[t].i;
        const auto j = plan.pairs[t].j;

        if (i >= x_share.size() ||
                j >= x_share.size() ||
                i >= j ||
                i > static_cast<std::size_t>(
                        std::numeric_limits<int>::max()) ||
                j > static_cast<std::size_t>(
                        std::numeric_limits<int>::max()))
            throw std::invalid_argument(
                    "ConstantPP SecEq pairs: malformed pair plan");

        compare_idx[4 * t] =
                static_cast<int>(i);
        compare_idx[4 * t + 1] =
                static_cast<int>(j);

        compare_idx[4 * t + 2] =
                static_cast<int>(j);
        compare_idx[4 * t + 3] =
                static_cast<int>(i);
    }

    std::vector<Ring> compare_res(compare_idx.size());

    const auto before = dcf_compare.get_stats();

    // false => strict less-than in the already-validated Kona comparator.
    dcf_compare.compare_in_vec(
            x_share,
            compare_idx,
            compare_res,
            false);

    const auto after = dcf_compare.get_stats();

    if (protocol_stats)
        ++protocol_stats->logical_rounds;

    const Ring one = public_one_share(playerno);
    std::vector<Ring> eq_share(m);

    for (std::size_t t = 0; t < m; ++t)
    {
        // Compare64 duplicates each secure-comparison output.
        const Ring lt_ij = compare_res[4 * t];
        const Ring lt_ji = compare_res[4 * t + 2];

        eq_share[t] = one - lt_ij - lt_ji;
    }

    finish_stats(
            before, after, m, eq_stats);

    return eq_share;
}

std::vector<Ring> sec_eq_public_scalar_kona_dcf(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& x_share,
        std::uint64_t y,
        KonaDcfCompare::Compare64<RING_BITS>& dcf_compare,
        ProtocolStats* protocol_stats,
        SecEqStats* eq_stats)
{
    validate_player(player, playerno);

    const std::size_t n = x_share.size();
    if (eq_stats)
        eq_stats->reset();

    if (n == 0)
        return {};

    if (2 * n > static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
        throw std::overflow_error(
                "ConstantPP SecEq scalar: input too large");

    /*
     * values:
     *   [x_0 ... x_{n-1}, y_0 ... y_{n-1}]
     *
     * y is additively shared as (y,0).
     */
    std::vector<Ring> values;
    values.reserve(2 * n);
    values.insert(
            values.end(),
            x_share.begin(),
            x_share.end());

    const Ring y_share =
            public_value_share(y, playerno);
    for (std::size_t i = 0; i < n; ++i)
        values.push_back(y_share);

    /*
     * For each i batch:
     *   x_i < y
     *   y < x_i
     */
    std::vector<int> compare_idx(4 * n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const int xi = static_cast<int>(i);
        const int yi = static_cast<int>(n + i);

        compare_idx[4 * i] = xi;
        compare_idx[4 * i + 1] = yi;
        compare_idx[4 * i + 2] = yi;
        compare_idx[4 * i + 3] = xi;
    }

    std::vector<Ring> compare_res(compare_idx.size());

    const auto before = dcf_compare.get_stats();

    dcf_compare.compare_in_vec(
            values,
            compare_idx,
            compare_res,
            false);

    const auto after = dcf_compare.get_stats();

    if (protocol_stats)
        ++protocol_stats->logical_rounds;

    const Ring one = public_one_share(playerno);
    std::vector<Ring> eq_share(n);

    for (std::size_t i = 0; i < n; ++i)
    {
        const Ring x_lt_y = compare_res[4 * i];
        const Ring y_lt_x = compare_res[4 * i + 2];
        eq_share[i] = one - x_lt_y - y_lt_x;
    }

    finish_stats(
            before, after, n, eq_stats);

    return eq_share;
}

} // namespace ConstantPP
