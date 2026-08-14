#ifndef CONSTANTPP_CONSTANTPP_TYPES_HPP_
#define CONSTANTPP_CONSTANTPP_TYPES_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../Math/Z2k.hpp"

namespace ConstantPP
{

static const int RING_BITS = 64;
using Ring = Z2<RING_BITS>;

/*
 * SecED preprocessing material held by one server.
 *
 * Compatibility choice:
 * The upstream constantPP-KNN implementation reuses one (a, c=a^2)
 * share pair across all dimensions in secED_nDim().  This reimplementation
 * intentionally keeps that behavior for the first baseline version.
 *
 * Do not interpret this as the stronger per-coordinate preprocessing
 * described in Algorithm 4 of the paper.
 */
struct SecEdPartyMaterial
{
    Ring a_share;
    Ring c_share;
};

struct SecEdDealerMaterial
{
    std::array<SecEdPartyMaterial, 2> party;
};

/*
 * One server's local SecShuffle preprocessing material.
 *
 * A single permutation is shared across all columns (for example distance
 * and label), while every column has its own masking/correction vector.
 */
struct ShufflePartyMaterial
{
    std::vector<std::size_t> permutation;
    std::vector<std::vector<Ring>> a_share; // [column][row]
    std::vector<std::vector<Ring>> b_share; // [column][row]
};

struct ShuffleDealerMaterial
{
    std::array<ShufflePartyMaterial, 2> party;
};

/*
 * Lightweight protocol-local counters.
 *
 * These counters are intentionally separate from Kona's player-wide
 * communication statistics.  They are useful for component tests, while
 * formal benchmark sent_bytes/transport_rounds should continue to use the
 * repository's existing communication-statistics machinery.
 */
struct ProtocolStats
{
    std::uint64_t payload_bytes_sent = 0;
    std::uint64_t send_calls = 0;
    std::uint64_t receive_calls = 0;
    std::uint64_t logical_rounds = 0;

    void reset()
    {
        payload_bytes_sent = 0;
        send_calls = 0;
        receive_calls = 0;
        logical_rounds = 0;
    }
};

} // namespace ConstantPP

#endif // CONSTANTPP_CONSTANTPP_TYPES_HPP_
