#include "tests/test_helpers.hpp"

#include <cmath>
#include <stdexcept>

namespace hecompare::test {

double decrypt_first_slot_for_test(
        const CkksEnvironment& environment,
        const CkksCiphertext& ciphertext)
{
    lbcrypto::Plaintext plaintext;
    environment.context->Decrypt(
            environment.key_pair.secretKey,
            ciphertext,
            &plaintext);
    plaintext->SetLength(1);

    const auto values = plaintext->GetRealPackedValue();
    if (values.empty())
        throw std::runtime_error("decrypted CKKS plaintext is empty");

    return values[0];
}

void expect_close(
        double actual,
        double expected,
        double tolerance,
        const std::string& label)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
                label + " mismatch: actual=" + std::to_string(actual) +
                " expected=" + std::to_string(expected));
    }
}

} // namespace hecompare::test
