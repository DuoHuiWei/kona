#ifndef MACHINES_KONA_ACTIVE_BITPACK_HPP_
#define MACHINES_KONA_ACTIVE_BITPACK_HPP_

#include <cassert>
#include <cstdint>

namespace KonaActiveBitpack
{

static const int BITPACK_WORD_BITS = 64;

inline int endpoint_count(int distance)
{
    assert(distance > 0 && distance <= 32 && (distance & (distance - 1)) == 0);
    return BITPACK_WORD_BITS / (2 * distance);
}

inline int endpoint_packed_bytes(int distance)
{
    return (endpoint_count(distance) + 7) / 8;
}

inline uint64_t endpoint_mask(int distance)
{
    assert(distance > 0 && distance <= 32 && (distance & (distance - 1)) == 0);
    uint64_t mask = 0;
    int step = distance * 2;
    for (int bit = step - 1; bit < BITPACK_WORD_BITS; bit += step)
        mask |= (uint64_t(1) << bit);
    return mask;
}

// Compress endpoint bits for the sparse PCR carry tree. For distance=1 this
// maps bits 1,3,5,...,63 into dense bits 0..31; for distance=32 it maps bit63
// into dense bit0. This avoids scanning all 64 mask positions.
inline uint64_t compact_endpoint_bits(uint64_t value, int distance)
{
    const int count = endpoint_count(distance);
    const int step = distance * 2;
    int source_bit = step - 1;
    uint64_t compact = 0;

    for (int out_bit = 0; out_bit < count; out_bit++, source_bit += step)
        compact |= ((value >> source_bit) & 1ULL) << out_bit;

    return compact;
}

inline uint64_t expand_endpoint_bits(uint64_t compact, int distance)
{
    const int count = endpoint_count(distance);
    const int step = distance * 2;
    int target_bit = step - 1;
    uint64_t value = 0;

    for (int in_bit = 0; in_bit < count; in_bit++, target_bit += step)
        value |= ((compact >> in_bit) & 1ULL) << target_bit;

    return value;
}

} // namespace KonaActiveBitpack

#endif /* MACHINES_KONA_ACTIVE_BITPACK_HPP_ */
