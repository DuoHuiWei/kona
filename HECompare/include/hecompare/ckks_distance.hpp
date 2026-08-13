#pragma once

#include "hecompare/ckks_context.hpp"

#include <cstddef>
#include <vector>

namespace hecompare {

using RealVector = std::vector<double>;

struct EncryptedQueryChunks {
    std::vector<CkksCiphertext> ciphertexts;
    std::vector<double> squared_norms;
    std::vector<uint32_t> valid_counts;

    std::size_t feature_count = 0;
    uint32_t slot_count = 0;
};

double plaintext_dot(const RealVector& lhs, const RealVector& rhs);

double plaintext_squared_norm(const RealVector& values);

double plaintext_reduced_score(
        const RealVector& query,
        const RealVector& train);

std::vector<double> pad_to_slots(
        const RealVector& input,
        uint32_t slots);

CkksCiphertext encrypt_query(
        const CkksEnvironment& environment,
        const RealVector& query);

CkksCiphertext evaluate_reduced_score(
        const CkksEnvironment& environment,
        const CkksCiphertext& encrypted_query,
        const RealVector& plaintext_train);

EncryptedQueryChunks encrypt_query_chunks(
        const CkksEnvironment& environment,
        const RealVector& query);

CkksCiphertext evaluate_chunked_squared_l2_distance(
        const CkksEnvironment& environment,
        const EncryptedQueryChunks& encrypted_query,
        const RealVector& plaintext_train);

CkksCiphertext evaluate_chunked_mean_squared_l2_distance(
        const CkksEnvironment& environment,
        const EncryptedQueryChunks& encrypted_query,
        const RealVector& plaintext_train);

} // namespace hecompare
