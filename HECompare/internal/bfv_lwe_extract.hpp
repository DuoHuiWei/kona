#pragma once

#include "openfhe.h"

#include <memory>
#include <string>
#include <vector>

namespace hecompare {

enum class BfvLweProbeStatus {
    Supported,
    PossibleWithSmallAdapter,
    RequiresOpenfheCorePatch,
};

struct BfvLweExtractionResult {
    BfvLweProbeStatus status = BfvLweProbeStatus::RequiresOpenfheCorePatch;
    std::string detail;
    lbcrypto::LWECiphertext lwe_ciphertext{nullptr};
    lbcrypto::LWEPrivateKey lwe_secret_key{nullptr};
    std::shared_ptr<lbcrypto::LWECryptoParams> lwe_params;
    uint64_t ciphertext_modulus = 0;
    uint32_t ring_dimension = 0;
    uint32_t tower_count = 0;
};

enum class BfvLweExtractionVariant {
    Element0WithNegacyclicSigns,
    Element1WithNegacyclicSigns,
    Element0WithOppositeSigns,
    Element1WithOppositeSigns,
};

BfvLweExtractionResult extract_target_coefficient_to_lwe(
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ciphertext,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly>& secret_key,
        uint32_t target_index,
        uint64_t plaintext_modulus);

BfvLweExtractionResult extract_target_coefficient_to_lwe(
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ciphertext,
        const lbcrypto::PrivateKey<lbcrypto::DCRTPoly>& secret_key,
        uint32_t target_index,
        uint64_t plaintext_modulus,
        BfvLweExtractionVariant variant);

std::string to_string(BfvLweProbeStatus status);
std::string to_string(BfvLweExtractionVariant variant);

} // namespace hecompare
