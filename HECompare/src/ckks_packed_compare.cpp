#include "hecompare/ckks_packed_compare.hpp"

#include <stdexcept>
#include <string>

namespace hecompare {
namespace {

void validate_ciphertext(
        const CkksEnvironment& environment,
        const CkksCiphertext& ciphertext,
        const char* label)
{
    if (!environment.context)
        throw std::invalid_argument(
                "packed compare environment has no context");

    if (!environment.key_pair.good() ||
        !environment.key_pair.publicKey) {
        throw std::invalid_argument(
                "packed compare environment has no valid key pair");
    }

    if (!ciphertext)
        throw std::invalid_argument(
                std::string(label) + " ciphertext is null");

    if (ciphertext->GetCryptoContext().get() !=
        environment.context.get()) {
        throw std::invalid_argument(
                std::string(label) +
                " ciphertext context mismatch");
    }

    if (ciphertext->GetKeyTag() !=
        environment.key_pair.publicKey->GetKeyTag()) {
        throw std::invalid_argument(
                std::string(label) +
                " ciphertext key does not match environment");
    }
}

void validate_active_count(
        const CkksEnvironment& environment,
        uint32_t active_count)
{
    if (active_count == 0)
        throw std::invalid_argument(
                "packed compare active_count must be positive");

    if (active_count > environment.slot_count)
        throw std::invalid_argument(
                "packed compare active_count exceeds slot_count");

    if (active_count >
        environment.switching_value_count) {
        throw std::invalid_argument(
                "packed compare active_count exceeds "
                "scheme-switch setup");
    }
}

void validate_binary_candidate(
        const CkksEnvironment& environment,
        const PackedBinaryLabelCandidate& candidate,
        const char* name)
{
    validate_ciphertext(
            environment,
            candidate.score,
            (std::string(name) + " score").c_str());

    if (candidate.label_bits.empty()) {
        throw std::invalid_argument(
                std::string(name) +
                " has no binary label planes");
    }

    for (std::size_t bit = 0;
         bit < candidate.label_bits.size();
         ++bit) {
        validate_ciphertext(
                environment,
                candidate.label_bits[bit],
                (std::string(name) +
                 " label bit " +
                 std::to_string(bit)).c_str());
    }
}

CkksCiphertext select_lhs_when_one(
        const CkksEnvironment& environment,
        const CkksCiphertext& selector,
        const CkksCiphertext& lhs,
        const CkksCiphertext& rhs)
{
    auto delta =
            environment.context->EvalSub(
                    lhs,
                    rhs);
    auto selected_delta =
            environment.context->EvalMult(
                    selector,
                    delta);
    return environment.context->EvalAdd(
            rhs,
            selected_delta);
}

CkksCiphertext complementary_selection(
        const CkksEnvironment& environment,
        const CkksCiphertext& lhs,
        const CkksCiphertext& rhs,
        const CkksCiphertext& selected)
{
    return environment.context->EvalSub(
            environment.context->EvalAdd(
                    lhs,
                    rhs),
            selected);
}

} // namespace

CkksCiphertext encrypted_less_than_packed(
        const CkksEnvironment& environment,
        const CkksCiphertext& lhs,
        const CkksCiphertext& rhs,
        uint32_t active_count)
{
    validate_active_count(
            environment,
            active_count);
    validate_ciphertext(
            environment,
            lhs,
            "packed lhs");
    validate_ciphertext(
            environment,
            rhs,
            "packed rhs");

    return encrypted_less_than(
            environment,
            lhs,
            rhs,
            active_count);
}

std::pair<EncryptedCandidate, EncryptedCandidate>
packed_compare_and_swap_ascending(
        const CkksEnvironment& environment,
        const EncryptedCandidate& lhs,
        const EncryptedCandidate& rhs,
        uint32_t active_count)
{
    validate_active_count(
            environment,
            active_count);
    validate_ciphertext(
            environment,
            lhs.score,
            "packed lhs score");
    validate_ciphertext(
            environment,
            rhs.score,
            "packed rhs score");
    validate_ciphertext(
            environment,
            lhs.label,
            "packed lhs label");
    validate_ciphertext(
            environment,
            rhs.label,
            "packed rhs label");

    auto compare =
            encrypted_less_than_packed(
                    environment,
                    lhs.score,
                    rhs.score,
                    active_count);

    auto minimum_score =
            select_lhs_when_one(
                    environment,
                    compare,
                    lhs.score,
                    rhs.score);
    auto maximum_score =
            complementary_selection(
                    environment,
                    lhs.score,
                    rhs.score,
                    minimum_score);

    auto minimum_label =
            select_lhs_when_one(
                    environment,
                    compare,
                    lhs.label,
                    rhs.label);
    auto maximum_label =
            complementary_selection(
                    environment,
                    lhs.label,
                    rhs.label,
                    minimum_label);

    return {
            EncryptedCandidate{
                    minimum_score,
                    minimum_label},
            EncryptedCandidate{
                    maximum_score,
                    maximum_label},
    };
}

std::pair<PackedBinaryLabelCandidate, PackedBinaryLabelCandidate>
packed_compare_and_swap_binary_labels_ascending(
        const CkksEnvironment& environment,
        const PackedBinaryLabelCandidate& lhs,
        const PackedBinaryLabelCandidate& rhs,
        uint32_t active_count)
{
    validate_active_count(
            environment,
            active_count);
    validate_binary_candidate(
            environment,
            lhs,
            "packed binary lhs");
    validate_binary_candidate(
            environment,
            rhs,
            "packed binary rhs");

    if (lhs.label_bits.size() !=
        rhs.label_bits.size()) {
        throw std::invalid_argument(
                "binary label plane counts do not match");
    }

    auto compare =
            encrypted_less_than_packed(
                    environment,
                    lhs.score,
                    rhs.score,
                    active_count);

    PackedBinaryLabelCandidate minimum;
    PackedBinaryLabelCandidate maximum;

    minimum.score =
            select_lhs_when_one(
                    environment,
                    compare,
                    lhs.score,
                    rhs.score);
    maximum.score =
            complementary_selection(
                    environment,
                    lhs.score,
                    rhs.score,
                    minimum.score);

    minimum.label_bits.reserve(
            lhs.label_bits.size());
    maximum.label_bits.reserve(
            lhs.label_bits.size());

    for (std::size_t bit = 0;
         bit < lhs.label_bits.size();
         ++bit) {
        auto minimum_bit =
                select_lhs_when_one(
                        environment,
                        compare,
                        lhs.label_bits[bit],
                        rhs.label_bits[bit]);
        auto maximum_bit =
                complementary_selection(
                        environment,
                        lhs.label_bits[bit],
                        rhs.label_bits[bit],
                        minimum_bit);

        minimum.label_bits.push_back(
                std::move(minimum_bit));
        maximum.label_bits.push_back(
                std::move(maximum_bit));
    }

    return {
            std::move(minimum),
            std::move(maximum),
    };
}

} // namespace hecompare
