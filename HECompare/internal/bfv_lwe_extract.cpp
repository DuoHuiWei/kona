#include "internal/bfv_lwe_extract.hpp"

#include <sstream>
#include <stdexcept>

namespace hecompare {

using namespace lbcrypto;

namespace {

NativeInteger negate_mod(const NativeInteger& value, const NativeInteger& modulus)
{
    if (value == NativeInteger(0))
        return value;
    return modulus - value;
}

LWEPrivateKey derive_lwe_secret_key_from_single_tower_bfv_secret(
        const PrivateKey<DCRTPoly>& secret_key,
        uint32_t* ring_dimension,
        uint64_t* modulus_out)
{
    auto secret_poly = secret_key->GetPrivateElement();
    secret_poly.SetFormat(Format::COEFFICIENT);

    if (secret_poly.GetNumOfElements() != 1)
        return nullptr;

    const auto& tower = secret_poly.GetElementAtIndex(0);
    const auto& values = tower.GetValues();
    const auto modulus = tower.GetModulus();

    NativeVector lwe_secret(values.GetLength(), modulus);
    for (uint32_t i = 0; i < values.GetLength(); ++i)
        lwe_secret[i] = values[i];

    if (ring_dimension)
        *ring_dimension = values.GetLength();
    if (modulus_out)
        *modulus_out = modulus.ConvertToInt<uint64_t>();

    return std::make_shared<LWEPrivateKeyImpl>(std::move(lwe_secret));
}

LWECiphertext extract_target_coefficient_single_tower(
        const Ciphertext<DCRTPoly>& ciphertext,
        uint32_t target_index,
        uint64_t plaintext_modulus,
        BfvLweExtractionVariant variant)
{
    const auto& elements = ciphertext->GetElements();
    if (elements.size() != 2)
        return nullptr;

    size_t b_index = 0;
    size_t a_index = 1;
    bool negacyclic_signs = true;

    switch (variant) {
    case BfvLweExtractionVariant::Element0WithNegacyclicSigns:
        b_index = 0;
        a_index = 1;
        negacyclic_signs = true;
        break;
    case BfvLweExtractionVariant::Element1WithNegacyclicSigns:
        b_index = 1;
        a_index = 0;
        negacyclic_signs = true;
        break;
    case BfvLweExtractionVariant::Element0WithOppositeSigns:
        b_index = 0;
        a_index = 1;
        negacyclic_signs = false;
        break;
    case BfvLweExtractionVariant::Element1WithOppositeSigns:
        b_index = 1;
        a_index = 0;
        negacyclic_signs = false;
        break;
    }

    auto c0 = elements[b_index];
    auto c1 = elements[a_index];
    c0.SetFormat(Format::COEFFICIENT);
    c1.SetFormat(Format::COEFFICIENT);

    if (c0.GetNumOfElements() != 1 || c1.GetNumOfElements() != 1)
        return nullptr;

    const auto& c0_tower = c0.GetElementAtIndex(0);
    const auto& c1_tower = c1.GetElementAtIndex(0);
    const auto& c0_values = c0_tower.GetValues();
    const auto& c1_values = c1_tower.GetValues();
    const auto modulus = c0_tower.GetModulus();

    const uint32_t ring_dimension = c0_values.GetLength();
    if (target_index >= ring_dimension)
        throw std::out_of_range("target index out of range");

    NativeVector a(ring_dimension, modulus);
    for (uint32_t j = 0; j < ring_dimension; ++j) {
        const uint32_t source = (target_index + ring_dimension - j) % ring_dimension;
        const bool negative =
                negacyclic_signs ? (j <= target_index) : (j > target_index);
        a[j] = negative ? negate_mod(c1_values[source], modulus) : c1_values[source];
    }

    return std::make_shared<LWECiphertextImpl>(
            std::move(a),
            c0_values[target_index],
            NativeInteger(plaintext_modulus));
}

} // namespace

BfvLweExtractionResult extract_target_coefficient_to_lwe(
        const Ciphertext<DCRTPoly>& ciphertext,
        const PrivateKey<DCRTPoly>& secret_key,
        uint32_t target_index,
        uint64_t plaintext_modulus)
{
    return extract_target_coefficient_to_lwe(
            ciphertext,
            secret_key,
            target_index,
            plaintext_modulus,
            BfvLweExtractionVariant::Element0WithNegacyclicSigns);
}

BfvLweExtractionResult extract_target_coefficient_to_lwe(
        const Ciphertext<DCRTPoly>& ciphertext,
        const PrivateKey<DCRTPoly>& secret_key,
        uint32_t target_index,
        uint64_t plaintext_modulus,
        BfvLweExtractionVariant variant)
{
    BfvLweExtractionResult result;

    auto secret_poly = secret_key->GetPrivateElement();
    result.tower_count = secret_poly.GetNumOfElements();

    uint32_t ring_dimension = 0;
    uint64_t modulus = 0;
    auto lwe_secret =
            derive_lwe_secret_key_from_single_tower_bfv_secret(
                    secret_key, &ring_dimension, &modulus);

    result.ring_dimension = ring_dimension;
    result.ciphertext_modulus = modulus;

    if (!lwe_secret) {
        result.status = BfvLweProbeStatus::RequiresOpenfheCorePatch;
        result.detail =
                "BFV secret key is multi-tower RNS; public single-tower extraction adapter is insufficient";
        return result;
    }

    auto lwe_ciphertext =
            extract_target_coefficient_single_tower(
                    ciphertext, target_index, plaintext_modulus, variant);

    if (!lwe_ciphertext) {
        result.status = BfvLweProbeStatus::RequiresOpenfheCorePatch;
        result.detail =
                "BFV ciphertext is not a public single-tower two-element form after coefficient conversion";
        return result;
    }

    result.lwe_params = std::make_shared<LWECryptoParams>(
            ring_dimension,
            ring_dimension,
            NativeInteger(modulus),
            NativeInteger(modulus),
            NativeInteger(modulus),
            3.19,
            2,
            UNIFORM_TERNARY);
    result.lwe_secret_key = std::move(lwe_secret);
    result.lwe_ciphertext = std::move(lwe_ciphertext);
    result.status = BfvLweProbeStatus::PossibleWithSmallAdapter;
    result.detail =
            "single-tower BFV target-coefficient extraction into public LWE types succeeded without OpenFHE patching";
    return result;
}

std::string to_string(BfvLweProbeStatus status)
{
    switch (status) {
    case BfvLweProbeStatus::Supported:
        return "supported";
    case BfvLweProbeStatus::PossibleWithSmallAdapter:
        return "possible_with_small_adapter";
    case BfvLweProbeStatus::RequiresOpenfheCorePatch:
        return "requires_openfhe_core_patch";
    }
    return "unknown";
}

std::string to_string(BfvLweExtractionVariant variant)
{
    switch (variant) {
    case BfvLweExtractionVariant::Element0WithNegacyclicSigns:
        return "element0_with_negacyclic_signs";
    case BfvLweExtractionVariant::Element1WithNegacyclicSigns:
        return "element1_with_negacyclic_signs";
    case BfvLweExtractionVariant::Element0WithOppositeSigns:
        return "element0_with_opposite_signs";
    case BfvLweExtractionVariant::Element1WithOppositeSigns:
        return "element1_with_opposite_signs";
    }
    return "unknown";
}

} // namespace hecompare
