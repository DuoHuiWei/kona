#pragma once

#include "hecompare/ckks_context.hpp"

#include <utility>

namespace hecompare {

struct EncryptedCandidate {
    CkksCiphertext score;
    CkksCiphertext label;
};

CkksCiphertext make_public_constant_like(
        const CkksEnvironment& environment,
        const CkksCiphertext& reference,
        double value);

CkksCiphertext encrypted_less_than(
        const CkksEnvironment& environment,
        const CkksCiphertext& lhs,
        const CkksCiphertext& rhs,
        uint32_t switching_value_count = 0);

std::pair<EncryptedCandidate, EncryptedCandidate>
compare_and_swap_ascending(
        const CkksEnvironment& environment,
        const EncryptedCandidate& lhs,
        const EncryptedCandidate& rhs);

} // namespace hecompare
