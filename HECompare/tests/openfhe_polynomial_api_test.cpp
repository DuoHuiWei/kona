#include "openfhe.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
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

Vec negacyclic_convolution(const Vec& lhs, const Vec& rhs, size_t ring_degree)
{
    if (ring_degree == 0)
        throw std::invalid_argument("ring_degree must be greater than zero");

    Vec result(ring_degree, 0);
    for (size_t i = 0; i < lhs.size(); ++i) {
        for (size_t j = 0; j < rhs.size(); ++j) {
            const size_t degree = i + j;
            const int64_t value = lhs[i] * rhs[j];

            if (degree < ring_degree)
                result[degree] += value;
            else
                result[degree - ring_degree] -= value;
        }
    }

    return result;
}

void expect_equal(int64_t lhs, int64_t rhs, const std::string& label)
{
    if (lhs != rhs)
        throw std::runtime_error(
                label + " mismatch: lhs=" + std::to_string(lhs) +
                " rhs=" + std::to_string(rhs));
}

void expect_equal(const Vec& lhs, const Vec& rhs, const std::string& label)
{
    if (lhs.size() != rhs.size())
        throw std::runtime_error(label + " size mismatch");

    for (size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i] != rhs[i]) {
            throw std::runtime_error(
                    label + " mismatch at index=" + std::to_string(i) +
                    " lhs=" + std::to_string(lhs[i]) +
                    " rhs=" + std::to_string(rhs[i]));
        }
    }
}

CryptoContext<DCRTPoly> build_bfv_context(uint32_t ring_dim, uint64_t plaintext_modulus)
{
    CCParams<CryptoContextBFVRNS> parameters;
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(ring_dim);
    parameters.SetPlaintextModulus(plaintext_modulus);
    parameters.SetMultiplicativeDepth(1);

    auto cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    return cc;
}

Vec decrypt_coef_vector(
        const CryptoContext<DCRTPoly>& cc,
        const PrivateKey<DCRTPoly>& secret_key,
        ConstCiphertext<DCRTPoly> ciphertext,
        size_t output_length)
{
    Plaintext plaintext;
    cc->Decrypt(secret_key, ciphertext, &plaintext);
    plaintext->SetLength(output_length);
    return plaintext->GetCoefPackedValue();
}

void run_public_polynomial_eval_test(
        const Vec& query,
        const Vec& train,
        uint32_t ring_dim,
        uint64_t plaintext_modulus)
{
    if (query.size() != train.size())
        throw std::runtime_error("query/train size mismatch");

    const Vec query_poly = encode_query_polynomial(query);
    const Vec train_poly = encode_reversed_train_polynomial(train);
    const Vec expected_product =
            negacyclic_convolution(query_poly, train_poly, ring_dim);
    const int64_t expected_dot = ordinary_dot(query, train);

    auto cc = build_bfv_context(ring_dim, plaintext_modulus);
    const auto key_pair = cc->KeyGen();

    const Plaintext query_plain = cc->MakeCoefPackedPlaintext(query_poly);
    const Plaintext train_plain = cc->MakeCoefPackedPlaintext(train_poly);

    const auto encrypted_query = cc->Encrypt(key_pair.publicKey, query_plain);
    const auto encrypted_product = cc->EvalMult(encrypted_query, train_plain);

    const Vec decrypted_product =
            decrypt_coef_vector(cc, key_pair.secretKey, encrypted_product, ring_dim);

    expect_equal(decrypted_product, expected_product, "decrypted negacyclic product");
    expect_equal(decrypted_product[query.size() - 1], expected_dot, "target coefficient dot");
}

} // namespace

int main()
{
    try {
        const Vec query8 = {3, -2, 5, 1, -4, 2, 0, 3};
        const Vec train8 = {7, 4, -1, 2, 3, -2, 1, 5};
        run_public_polynomial_eval_test(query8, train8, 16, 65537);

        const Vec query16 = {2, -1, 0, 4, -3, 1, 5, -2, 3, 0, -4, 2, 1, -1, 2, 3};
        const Vec train16 = {1, 3, -2, 0, 4, -1, 2, 5, -3, 1, 0, 2, -2, 4, 1, -1};
        run_public_polynomial_eval_test(query16, train16, 32, 65537);

        std::cout << "openfhe-polynomial-api-test passed" << '\n';
        std::cout << "validated_path=encrypted_query_poly * plaintext_train_poly -> decrypt_full_poly -> inspect_target_coefficient" << '\n';
        std::cout << "note=public RLWE-style ciphertext-plaintext polynomial multiplication works; direct SampleExtract/LWE path still needs separate confirmation" << '\n';
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "openfhe-polynomial-api-test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
