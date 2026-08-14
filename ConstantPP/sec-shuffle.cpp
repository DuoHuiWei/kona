#include "sec-shuffle.hpp"

#include <algorithm>
#include <stdexcept>

#include "constantpp-comm.hpp"

namespace ConstantPP
{
namespace
{

inline Ring random_ring(PRNG& prng)
{
    return Ring(static_cast<mp_limb_t>(prng.get_word()));
}

std::vector<std::size_t> random_permutation(std::size_t n, PRNG& prng)
{
    std::vector<std::size_t> pi(n);
    for (std::size_t i = 0; i < n; ++i)
        pi[i] = i;

    for (std::size_t i = n; i > 1; --i)
    {
        const std::size_t j =
                static_cast<std::size_t>(prng.get_word() % i);
        std::swap(pi[i - 1], pi[j]);
    }
    return pi;
}

void validate_permutation(
        const std::vector<std::size_t>& pi,
        std::size_t n)
{
    if (pi.size() != n)
        throw std::invalid_argument(
                "ConstantPP SecShuffle: permutation size mismatch");

    std::vector<bool> seen(n, false);
    for (std::size_t idx : pi)
    {
        if (idx >= n)
            throw std::invalid_argument(
                    "ConstantPP SecShuffle: invalid permutation index");
        if (seen[idx])
            throw std::invalid_argument(
                    "ConstantPP SecShuffle: duplicate permutation index");
        seen[idx] = true;
    }
}

std::vector<Ring> apply_permutation(
        const std::vector<Ring>& input,
        const std::vector<std::size_t>& pi)
{
    validate_permutation(pi, input.size());

    std::vector<Ring> output(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = input[pi[i]];
    return output;
}

void validate_local_material(
        const ShufflePartyMaterial& material,
        std::size_t num_columns,
        std::size_t n)
{
    validate_permutation(material.permutation, n);
    if (material.a_share.size() != num_columns ||
            material.b_share.size() != num_columns)
        throw std::invalid_argument(
                "ConstantPP SecShuffle: material column count mismatch");

    for (std::size_t c = 0; c < num_columns; ++c)
    {
        if (material.a_share[c].size() != n ||
                material.b_share[c].size() != n)
            throw std::invalid_argument(
                    "ConstantPP SecShuffle: material row count mismatch");
    }
}

} // namespace

ShuffleDealerMaterial dealer_generate_shuffle_material(
        std::size_t n,
        std::size_t num_columns,
        PRNG& prng)
{
    ShuffleDealerMaterial out;

    const auto pi0 = random_permutation(n, prng);
    const auto pi1 = random_permutation(n, prng);
    out.party[0].permutation = pi0;
    out.party[1].permutation = pi1;

    for (int p = 0; p < 2; ++p)
    {
        out.party[p].a_share.assign(
                num_columns, std::vector<Ring>(n));
        out.party[p].b_share.assign(
                num_columns, std::vector<Ring>(n));
    }

    for (std::size_t c = 0; c < num_columns; ++c)
    {
        // The paper treats [a]_0 and [a]_1 as random masking shares.
        for (std::size_t i = 0; i < n; ++i)
        {
            out.party[0].a_share[c][i] = random_ring(prng);
            out.party[1].a_share[c][i] = random_ring(prng);
        }

        // b0 + b1 = pi0(pi1(a0) + a1)
        const auto pi1_a0 =
                apply_permutation(out.party[0].a_share[c], pi1);

        std::vector<Ring> tmp(n);
        for (std::size_t i = 0; i < n; ++i)
            tmp[i] = pi1_a0[i] + out.party[1].a_share[c][i];

        const auto b_plain = apply_permutation(tmp, pi0);

        for (std::size_t i = 0; i < n; ++i)
        {
            const Ring b0 = random_ring(prng);
            out.party[0].b_share[c][i] = b0;
            out.party[1].b_share[c][i] = b_plain[i] - b0;
        }
    }

    return out;
}

std::vector<std::vector<Ring>> sec_shuffle_columns(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<std::vector<Ring>>& local_columns,
        const ShufflePartyMaterial& material,
        ProtocolStats* stats)
{
    if (!player)
        throw std::invalid_argument(
                "ConstantPP SecShuffle: null RealTwoPartyPlayer");
    if (playerno != 0 && playerno != 1)
        throw std::invalid_argument(
                "ConstantPP SecShuffle: playerno must be 0 or 1");

    const std::size_t num_columns = local_columns.size();
    if (num_columns == 0)
        return {};

    const std::size_t n = local_columns[0].size();
    for (const auto& column : local_columns)
    {
        if (column.size() != n)
            throw std::invalid_argument(
                    "ConstantPP SecShuffle: input columns have unequal length");
    }
    validate_local_material(material, num_columns, n);

    std::vector<std::vector<Ring>> output(
            num_columns, std::vector<Ring>(n));

    if (playerno == 0)
    {
        // Round 1: S0 sends [x']_0 = [x]_0 + [a]_0 to S1.
        std::vector<std::vector<Ring>> x_prime0(
                num_columns, std::vector<Ring>(n));
        for (std::size_t c = 0; c < num_columns; ++c)
            for (std::size_t i = 0; i < n; ++i)
                x_prime0[c][i] =
                        local_columns[c][i] + material.a_share[c][i];

        send_columns(player, x_prime0, stats);

        // Round 2: receive x'' from S1.
        std::vector<std::vector<Ring>> x_double;
        receive_columns(player, num_columns, n, x_double, stats);

        // [y]_0 = pi0(x'') - [b]_0
        for (std::size_t c = 0; c < num_columns; ++c)
        {
            const auto permuted =
                    apply_permutation(x_double[c], material.permutation);
            for (std::size_t i = 0; i < n; ++i)
                output[c][i] =
                        permuted[i] - material.b_share[c][i];
        }
    }
    else
    {
        // Round 1: S1 receives [x']_0 from S0.
        std::vector<std::vector<Ring>> x_prime0;
        receive_columns(player, num_columns, n, x_prime0, stats);

        // x'' = pi1([x']_0 + [x]_1) + [a]_1
        std::vector<std::vector<Ring>> x_double(
                num_columns, std::vector<Ring>(n));
        for (std::size_t c = 0; c < num_columns; ++c)
        {
            std::vector<Ring> tmp(n);
            for (std::size_t i = 0; i < n; ++i)
                tmp[i] = x_prime0[c][i] + local_columns[c][i];

            const auto permuted =
                    apply_permutation(tmp, material.permutation);
            for (std::size_t i = 0; i < n; ++i)
                x_double[c][i] =
                        permuted[i] + material.a_share[c][i];
        }

        // Round 2: S1 sends x'' to S0.
        send_columns(player, x_double, stats);

        // [y]_1 = -[b]_1
        for (std::size_t c = 0; c < num_columns; ++c)
            for (std::size_t i = 0; i < n; ++i)
                output[c][i] = Ring(0) - material.b_share[c][i];
    }

    if (stats)
        stats->logical_rounds += 2;

    return output;
}

std::vector<Ring> sec_shuffle_vector(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& local_share,
        const ShufflePartyMaterial& material,
        ProtocolStats* stats)
{
    std::vector<std::vector<Ring>> columns(1);
    columns[0] = local_share;
    auto result =
            sec_shuffle_columns(player, playerno, columns, material, stats);
    return result.empty() ? std::vector<Ring>() : result[0];
}

std::vector<std::vector<Ring>> sec_shuffle_value_label(
        RealTwoPartyPlayer* player,
        int playerno,
        const std::vector<Ring>& value_share,
        const std::vector<Ring>& label_share,
        const ShufflePartyMaterial& material,
        ProtocolStats* stats)
{
    if (value_share.size() != label_share.size())
        throw std::invalid_argument(
                "ConstantPP SecShuffle: value/label size mismatch");

    std::vector<std::vector<Ring>> columns(2);
    columns[0] = value_share;
    columns[1] = label_share;
    return sec_shuffle_columns(
            player, playerno, columns, material, stats);
}

} // namespace ConstantPP
