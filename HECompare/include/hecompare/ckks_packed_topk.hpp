#pragma once

#include "hecompare/ckks_packed_compare.hpp"

#include <cstdint>
#include <vector>

namespace hecompare {

struct PackedBitonicSortResult {
    std::vector<PackedBinaryLabelCandidate> sorted_candidates;
    uint32_t comparator_count = 0;
    uint32_t network_depth = 0;
};

// Fully sort a power-of-two number of encrypted candidate wires in ascending
// score order.
//
// Layout:
// - candidates[w] is one sorting-network wire.
// - every ciphertext slot is an independent query/batch item.
// - active_count is therefore the number of queries sorted in parallel.
//
// The function uses the existing packed binary-label compare-and-swap.
// It performs a full Bitonic sort; callers may take the first k wires as Top-k.
PackedBitonicSortResult packed_bitonic_sort_ascending(
        const CkksEnvironment& environment,
        std::vector<PackedBinaryLabelCandidate> candidates,
        uint32_t active_count);

} // namespace hecompare
