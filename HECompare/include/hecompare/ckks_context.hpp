#pragma once

#include "binfhecontext.h"
#include "openfhe.h"

#include <cstdint>
#include <memory>

namespace hecompare {

using CkksCiphertext = lbcrypto::Ciphertext<lbcrypto::DCRTPoly>;

struct CkksParameters {
    uint32_t multiplicative_depth = 17;
    uint32_t first_mod_size = 60;
    uint32_t scaling_mod_size = 50;
    uint32_t ring_dimension = 8192;
    uint32_t slot_count = 16;
    uint32_t switching_value_count = 1;
    uint32_t log_q_lwe = 25;

    double scale_sign = 1.0;
    lbcrypto::ScalingTechnique scaling_technique = lbcrypto::FLEXIBLEAUTO;

    lbcrypto::SecurityLevel ckks_security = lbcrypto::HEStd_NotSet;
    lbcrypto::BINFHE_PARAMSET fhew_parameter_set = lbcrypto::TOY;
};

struct CkksEnvironment {
    lbcrypto::CryptoContext<lbcrypto::DCRTPoly> context;
    lbcrypto::KeyPair<lbcrypto::DCRTPoly> key_pair;

    std::shared_ptr<lbcrypto::BinFHEContext> binfhe_context;
    lbcrypto::LWEPrivateKey fhew_secret_key;

    uint32_t p_lwe = 0;
    double scale_sign = 1.0;
    uint32_t slot_count = 0;
    uint32_t switching_value_count = 1;
};

CkksEnvironment make_ckks_environment(const CkksParameters& parameters = {});

} // namespace hecompare
