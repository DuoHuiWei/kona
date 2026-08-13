#include "openfhe.h"

#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace lbcrypto;

namespace {

CryptoContext<DCRTPoly> build_ckks_context(uint32_t slots)
{
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(3);
    parameters.SetScalingModSize(50);
    parameters.SetScalingTechnique(FLEXIBLEAUTOEXT);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(4096);
    parameters.SetBatchSize(slots);

    auto cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(SCHEMESWITCH);
    return cc;
}

void expect_equal(
        const std::vector<int64_t>& lhs,
        const std::vector<int64_t>& rhs,
        const std::string& label)
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

} // namespace

int main()
{
    try {
        const uint32_t slots = 8;
        auto cc = build_ckks_context(slots);
        const auto keys = cc->KeyGen();

        SchSwchParams params;
        params.SetSecurityLevelCKKS(HEStd_NotSet);
        params.SetSecurityLevelFHEW(STD128);
        params.SetCtxtModSizeFHEWLargePrec(25);
        params.SetNumSlotsCKKS(slots);

        auto private_key_fhew = cc->EvalCKKStoFHEWSetup(params);
        auto ccLWE = cc->GetBinCCForSchemeSwitch();
        cc->EvalCKKStoFHEWKeyGen(keys, private_key_fhew);

        const auto pLWE = ccLWE->GetMaxPlaintextSpace().ConvertToInt();
        const double scale = 1.0 / pLWE;
        cc->EvalCKKStoFHEWPrecompute(scale);

        const std::vector<double> input = {0.0, 1.0, 2.0, 3.0, 0.0, 1.0, 2.0, 3.0};
        Plaintext plaintext = cc->MakeCKKSPackedPlaintext(input, 1, 0, nullptr);
        auto ciphertext = cc->Encrypt(keys.publicKey, plaintext);

        auto lwe_ciphertexts = cc->EvalCKKStoFHEW(ciphertext, slots);

        std::vector<int64_t> expected(slots);
        for (size_t i = 0; i < input.size(); ++i)
            expected[i] = static_cast<int64_t>(std::llround(input[i])) % pLWE;

        std::vector<int64_t> decrypted(slots);
        LWEPlaintext lwe_plain = 0;
        for (size_t i = 0; i < lwe_ciphertexts.size(); ++i) {
            ccLWE->Decrypt(private_key_fhew, lwe_ciphertexts[i], &lwe_plain, pLWE);
            decrypted[i] = lwe_plain;
        }

        expect_equal(decrypted, expected, "ckks to fhew public schemeswitch");

        std::cout << "openfhe-public-schemeswitch-test passed" << '\n';
        std::cout << "validated_path=CKKS ciphertext -> EvalCKKStoFHEW -> vector<LWECiphertext> -> BinFHE decrypt" << '\n';
        std::cout << "note=this confirms the public slot-switching route, not BFV coefficient extraction" << '\n';
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "openfhe-public-schemeswitch-test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
