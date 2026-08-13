#pragma once

#include "hecompare/ckks_compare.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace hecompare {

// One packed candidate vector:
// - score: one CKKS ciphertext containing active_count independent scores.
// - label_bits[b]: one CKKS ciphertext containing bit b of every label.
//   Bits are little-endian. Each active slot contains approximately 0 or 1.
//
// Binary label encoding prevents the approximate comparison selector error
// from being multiplied by a large integer candidate-ID difference.
struct PackedBinaryLabelCandidate {
    CkksCiphertext score;
    std::vector<CkksCiphertext> label_bits;
};

// Compare active_count corresponding CKKS slots in one scheme-switch call.
// Slot i returns approximately 1 when lhs[i] < rhs[i], otherwise 0.
CkksCiphertext encrypted_less_than_packed(
        const CkksEnvironment& environment,
        const CkksCiphertext& lhs,
        const CkksCiphertext& rhs,
        uint32_t active_count);

// Legacy scalar-label version.
//
// This is only numerically reliable when the difference between labels being
// selected is small enough that selector_error * label_difference < 0.5.
// It is retained for experiments, but binary labels are recommended for IDs.
std::pair<EncryptedCandidate, EncryptedCandidate>
packed_compare_and_swap_ascending(
        const CkksEnvironment& environment,
        const EncryptedCandidate& lhs,
        const EncryptedCandidate& rhs,
        uint32_t active_count);

// Recommended packed compare-and-swap for integer candidate IDs.
//
// The score comparison is performed once. The same approximate 0/1 selector
// moves the score and every binary label plane. Since every label plane
// contains only 0/1, selector error is not amplified by the numerical ID gap.
std::pair<PackedBinaryLabelCandidate, PackedBinaryLabelCandidate>
packed_compare_and_swap_binary_labels_ascending(
        const CkksEnvironment& environment,
        const PackedBinaryLabelCandidate& lhs,
        const PackedBinaryLabelCandidate& rhs,
        uint32_t active_count);

} // namespace hecompare
