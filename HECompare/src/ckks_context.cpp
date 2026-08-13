#include "hecompare/ckks_context.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace hecompare {

using namespace lbcrypto;

CkksEnvironment make_ckks_environment(const CkksParameters& parameters)
{
    if (parameters.slot_count == 0)
        throw std::invalid_argument("slot_count must be positive");
    if (parameters.ring_dimension == 0)
        throw std::invalid_argument("ring_dimension must be positive");
    if (parameters.slot_count > parameters.ring_dimension / 2)
        throw std::invalid_argument("slot_count exceeds CKKS ring capacity");
    if (parameters.switching_value_count == 0)
        throw std::invalid_argument("switching_value_count must be positive");
    if (parameters.switching_value_count > parameters.slot_count)
        throw std::invalid_argument("switching_value_count exceeds slot_count");
    if (!std::isfinite(parameters.scale_sign) || parameters.scale_sign <= 0.0)
        throw std::invalid_argument("scale_sign must be finite and positive");
    if (parameters.log_q_lwe == 0 || parameters.log_q_lwe >= 64)
        throw std::invalid_argument("log_q_lwe must be in [1, 63]");
    if (parameters.multiplicative_depth == 0)
        throw std::invalid_argument("multiplicative_depth must be positive");

    CkksEnvironment environment;

    CCParams<CryptoContextCKKSRNS> params;
    params.SetMultiplicativeDepth(parameters.multiplicative_depth);
    params.SetFirstModSize(parameters.first_mod_size);
    params.SetScalingModSize(parameters.scaling_mod_size);
    params.SetScalingTechnique(parameters.scaling_technique);
    params.SetSecurityLevel(parameters.ckks_security);
    params.SetRingDim(parameters.ring_dimension);
    params.SetBatchSize(parameters.slot_count);
    params.SetSecretKeyDist(UNIFORM_TERNARY);
    params.SetKeySwitchTechnique(HYBRID);
    params.SetNumLargeDigits(3);

    environment.context = GenCryptoContext(params);
    if (!environment.context)
        throw std::runtime_error("CKKS context generation failed");
    environment.context->Enable(PKE);
    environment.context->Enable(KEYSWITCH);
    environment.context->Enable(LEVELEDSHE);
    environment.context->Enable(ADVANCEDSHE);
    environment.context->Enable(SCHEMESWITCH);

    environment.key_pair = environment.context->KeyGen();
    if (!environment.key_pair.good())
        throw std::runtime_error("CKKS key generation failed");

    environment.context->EvalSumKeyGen(environment.key_pair.secretKey);
    environment.context->EvalMultKeyGen(environment.key_pair.secretKey);

    SchSwchParams switch_params;
    switch_params.SetSecurityLevelCKKS(parameters.ckks_security);
    switch_params.SetSecurityLevelFHEW(parameters.fhew_parameter_set);
    switch_params.SetCtxtModSizeFHEWLargePrec(parameters.log_q_lwe);
    switch_params.SetNumSlotsCKKS(parameters.slot_count);
    switch_params.SetNumValues(parameters.switching_value_count);

    environment.fhew_secret_key =
            environment.context->EvalSchemeSwitchingSetup(switch_params);
    environment.binfhe_context =
            environment.context->GetBinCCForSchemeSwitch();
    if (!environment.binfhe_context)
        throw std::runtime_error("missing BinFHE context for scheme switching");

    environment.binfhe_context->BTKeyGen(environment.fhew_secret_key);
    environment.context->EvalSchemeSwitchingKeyGen(
            environment.key_pair,
            environment.fhew_secret_key);

    const uint64_t modulus_lwe = uint64_t{1} << parameters.log_q_lwe;
    const uint64_t beta =
            environment.binfhe_context->GetBeta().ConvertToInt<uint64_t>();
    if (beta == 0)
        throw std::runtime_error("BinFHE beta must be positive");
    if (beta > std::numeric_limits<uint64_t>::max() / 2)
        throw std::runtime_error("BinFHE beta is too large");
    const uint64_t denominator = 2 * beta;
    const uint64_t p_lwe_64 = modulus_lwe / denominator;
    if (p_lwe_64 == 0 || p_lwe_64 > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("derived pLWE is outside uint32_t range");
    environment.p_lwe = static_cast<uint32_t>(p_lwe_64);
    environment.scale_sign = parameters.scale_sign;
    environment.slot_count = parameters.slot_count;
    environment.switching_value_count = parameters.switching_value_count;

    environment.context->EvalCompareSwitchPrecompute(
            environment.p_lwe,
            environment.scale_sign);

    return environment;
}

} // namespace hecompare
