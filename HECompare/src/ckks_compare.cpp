#include "hecompare/ckks_compare.hpp"

#include <cmath>
#include <stdexcept>

namespace hecompare {

namespace {

void validate_environment_has_key(
        const CkksEnvironment& environment,
        const char* prefix)
{
    if (!environment.context)
        throw std::invalid_argument(std::string(prefix) + " environment has no context");
    if (!environment.key_pair.good() || !environment.key_pair.publicKey)
        throw std::invalid_argument(std::string(prefix) + " environment has no valid key pair");
}

} // namespace

CkksCiphertext make_public_constant_like(
        const CkksEnvironment& environment,
        const CkksCiphertext& reference,
        double value)
{
    validate_environment_has_key(environment, "constant");
    if (!reference)
        throw std::invalid_argument("constant reference ciphertext is null");
    if (!std::isfinite(value))
        throw std::invalid_argument("constant value must be finite");
    if (reference->GetCryptoContext().get() != environment.context.get())
        throw std::invalid_argument("constant reference context mismatch");

    auto zero = environment.context->EvalSub(reference, reference);
    return environment.context->EvalAdd(zero, value);
}

CkksCiphertext encrypted_less_than(
        const CkksEnvironment& environment,
        const CkksCiphertext& lhs,
        const CkksCiphertext& rhs,
        uint32_t switching_value_count)
{
    constexpr bool unit_circle_input = false;
    validate_environment_has_key(environment, "comparison");
    if (!lhs || !rhs)
        throw std::invalid_argument("comparison ciphertext is null");
    const uint32_t num_values_to_compare =
            switching_value_count == 0
                    ? environment.switching_value_count
                    : switching_value_count;
    if (num_values_to_compare == 0)
        throw std::invalid_argument("comparison value count must be positive");
    if (num_values_to_compare > environment.switching_value_count)
        throw std::invalid_argument("comparison value count exceeds scheme-switch setup");
    if (num_values_to_compare > environment.slot_count)
        throw std::invalid_argument("comparison value count exceeds slot count");
    if (lhs->GetCryptoContext().get() != environment.context.get() ||
        rhs->GetCryptoContext().get() != environment.context.get()) {
        throw std::invalid_argument("comparison ciphertext context mismatch");
    }
    const auto& expected_key_tag = environment.key_pair.publicKey->GetKeyTag();
    if (lhs->GetKeyTag() != expected_key_tag || rhs->GetKeyTag() != expected_key_tag)
        throw std::invalid_argument("comparison ciphertext key does not match environment");
    if (lhs->GetElements().empty() || rhs->GetElements().empty())
        throw std::invalid_argument("comparison ciphertext has no elements");
    if (lhs->GetLevel() != rhs->GetLevel())
        throw std::invalid_argument("comparison ciphertext level mismatch");
    const double lhs_scale = lhs->GetScalingFactor();
    const double rhs_scale = rhs->GetScalingFactor();
    if (!std::isfinite(lhs_scale) || !std::isfinite(rhs_scale) ||
        lhs_scale <= 0.0 || rhs_scale <= 0.0) {
        throw std::invalid_argument("comparison ciphertext has invalid scale");
    }

    return environment.context->EvalCompareSchemeSwitching(
            lhs,
            rhs,
            num_values_to_compare,
            environment.slot_count,
            environment.p_lwe,
            environment.scale_sign,
            unit_circle_input);
}

std::pair<EncryptedCandidate, EncryptedCandidate>
compare_and_swap_ascending(
        const CkksEnvironment& environment,
        const EncryptedCandidate& lhs,
        const EncryptedCandidate& rhs)
{
    auto cmp = encrypted_less_than(environment, lhs.score, rhs.score);

    auto score_delta = environment.context->EvalSub(lhs.score, rhs.score);
    auto selected_score_delta =
            environment.context->EvalMult(cmp, score_delta);
    auto min_score =
            environment.context->EvalAdd(rhs.score, selected_score_delta);
    auto max_score = environment.context->EvalSub(
            environment.context->EvalAdd(lhs.score, rhs.score),
            min_score);

    auto label_delta = environment.context->EvalSub(lhs.label, rhs.label);
    auto selected_label_delta =
            environment.context->EvalMult(cmp, label_delta);
    auto min_label =
            environment.context->EvalAdd(rhs.label, selected_label_delta);
    auto max_label = environment.context->EvalSub(
            environment.context->EvalAdd(lhs.label, rhs.label),
            min_label);

    return {
            EncryptedCandidate{min_score, min_label},
            EncryptedCandidate{max_score, max_label},
    };
}

} // namespace hecompare
