#include "internal/bfv_lwe_extract.hpp"
#include "openfhe.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using namespace lbcrypto;

namespace {

using Vec = std::vector<int64_t>;

int64_t ordinary_dot(const Vec& lhs, const Vec& rhs)
{
    if (lhs.size() != rhs.size())
        throw std::runtime_error("ordinary_dot size mismatch");

    int64_t sum = 0;
    for (size_t i = 0; i < lhs.size(); ++i)
        sum += lhs[i] * rhs[i];
    return sum;
}

Vec encode_query_polynomial(const Vec& query)
{
    return query;
}

Vec encode_reversed_train_polynomial(const Vec& train)
{
    return Vec(train.rbegin(), train.rend());
}

CryptoContext<DCRTPoly> build_bfv_probe_context(uint32_t ring_dim, uint64_t plaintext_modulus)
{
    CCParams<CryptoContextBFVRNS> parameters;
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ring_dim);
    parameters.SetPlaintextModulus(plaintext_modulus);
    parameters.SetMultiplicativeDepth(0);

    auto cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    return cc;
}

int64_t decrypt_lwe_plaintext(
        const hecompare::BfvLweExtractionResult& extraction,
        uint64_t plaintext_modulus)
{
    LWEEncryptionScheme lwe_scheme;
    LWEPlaintext result = 0;
    lwe_scheme.Decrypt(
            extraction.lwe_params,
            extraction.lwe_secret_key,
            extraction.lwe_ciphertext,
            &result,
            plaintext_modulus);
    return result;
}

int64_t mod_plain(int64_t value, uint64_t modulus)
{
    int64_t reduced = value % static_cast<int64_t>(modulus);
    if (reduced < 0)
        reduced += static_cast<int64_t>(modulus);
    return reduced;
}

hecompare::BfvLweExtractionVariant find_working_variant(
        const Ciphertext<DCRTPoly>& encrypted_product,
        const PrivateKey<DCRTPoly>& secret_key,
        uint32_t target_index,
        uint64_t plaintext_modulus,
        int64_t expected)
{
    const std::vector<hecompare::BfvLweExtractionVariant> variants = {
            hecompare::BfvLweExtractionVariant::Element0WithNegacyclicSigns,
            hecompare::BfvLweExtractionVariant::Element1WithNegacyclicSigns,
            hecompare::BfvLweExtractionVariant::Element0WithOppositeSigns,
            hecompare::BfvLweExtractionVariant::Element1WithOppositeSigns};

    for (const auto variant : variants) {
        const auto extraction = hecompare::extract_target_coefficient_to_lwe(
                encrypted_product,
                secret_key,
                target_index,
                plaintext_modulus,
                variant);

        if (extraction.status == hecompare::BfvLweProbeStatus::RequiresOpenfheCorePatch)
            continue;

        const int64_t decrypted = decrypt_lwe_plaintext(extraction, plaintext_modulus);
        if (decrypted == expected)
            return variant;
    }

    throw std::runtime_error("no public extraction variant matched the expected target coefficient");
}

void run_probe_for_dimension(
        uint32_t dimension,
        uint64_t plaintext_modulus,
        std::mt19937_64& rng,
        int trials)
{
    auto cc = build_bfv_probe_context(dimension * 2, plaintext_modulus);
    const auto keys = cc->KeyGen();
    hecompare::BfvLweExtractionVariant working_variant =
            hecompare::BfvLweExtractionVariant::Element0WithNegacyclicSigns;
    bool variant_initialized = false;

    std::uniform_int_distribution<int64_t> dist(0, 5);

    for (int trial = 0; trial < trials; ++trial) {
        Vec query(dimension);
        Vec train(dimension);
        for (uint32_t i = 0; i < dimension; ++i) {
            query[i] = dist(rng);
            train[i] = dist(rng);
        }

        const Vec query_poly = encode_query_polynomial(query);
        const Vec train_poly = encode_reversed_train_polynomial(train);
        const Plaintext query_plain = cc->MakeCoefPackedPlaintext(query_poly);
        const Plaintext train_plain = cc->MakeCoefPackedPlaintext(train_poly);

        const auto encrypted_query = cc->Encrypt(keys.publicKey, query_plain);
        const auto encrypted_product = cc->EvalMult(encrypted_query, train_plain);
        const int64_t expected = mod_plain(ordinary_dot(query, train), plaintext_modulus);

        if (!variant_initialized) {
            working_variant = find_working_variant(
                    encrypted_product,
                    keys.secretKey,
                    dimension - 1,
                    plaintext_modulus,
                    expected);
            variant_initialized = true;
        }

        const auto extraction = hecompare::extract_target_coefficient_to_lwe(
                encrypted_product,
                keys.secretKey,
                dimension - 1,
                plaintext_modulus,
                working_variant);

        if (extraction.status == hecompare::BfvLweProbeStatus::RequiresOpenfheCorePatch) {
            std::cout << "classification=" << hecompare::to_string(extraction.status) << '\n';
            std::cout << "detail=" << extraction.detail << '\n';
            std::cout << "ring_dimension=" << extraction.ring_dimension
                      << " tower_count=" << extraction.tower_count
                      << " ciphertext_modulus=" << extraction.ciphertext_modulus << '\n';
            return;
        }

        const int64_t decrypted = decrypt_lwe_plaintext(extraction, plaintext_modulus);

        if (decrypted != expected) {
            throw std::runtime_error(
                    "LWE decrypted target coefficient mismatch at trial=" +
                    std::to_string(trial));
        }
    }

    std::cout << "classification=" << hecompare::to_string(
            hecompare::BfvLweProbeStatus::PossibleWithSmallAdapter) << '\n';
    std::cout << "detail=public single-tower BFV target coefficient can be converted into public LWE types across random trials without BFV decrypt" << '\n';
    std::cout << "working_variant=" << hecompare::to_string(working_variant) << '\n';
    std::cout << "tested_dimensions=" << dimension << '\n';
    std::cout << "trials=" << trials << '\n';
}

} // namespace

int main()
{
    try {
        constexpr uint64_t plaintext_modulus = 65537;
        constexpr int trials_per_dimension = 100;
        std::mt19937_64 rng(20260731);

        run_probe_for_dimension(8, plaintext_modulus, rng, trials_per_dimension);
        run_probe_for_dimension(16, plaintext_modulus, rng, trials_per_dimension);

        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "openfhe-bfv-to-lwe-test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
