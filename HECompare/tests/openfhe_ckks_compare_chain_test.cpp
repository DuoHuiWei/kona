#include "hecompare/ckks_compare.hpp"
#include "hecompare/ckks_context.hpp"
#include "tests/test_helpers.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <vector>

namespace {

using hecompare::CkksParameters;
using hecompare::EncryptedCandidate;

void print_levels(const char* prefix, const std::vector<EncryptedCandidate>& candidates)
{
    std::cout << prefix;
    for (size_t i = 0; i < candidates.size(); ++i) {
        std::cout << " c" << i << "_score=" << candidates[i].score->GetLevel()
                  << " c" << i << "_label=" << candidates[i].label->GetLevel();
    }
    std::cout << '\n';
}

} // namespace

int main()
{
    try {
        CkksParameters params;
        params.scaling_technique = lbcrypto::FLEXIBLEAUTOEXT;
        params.multiplicative_depth = 20;
        params.switching_value_count = 1;

        auto env = hecompare::make_ckks_environment(params);

        std::vector<double> scores_plain = {7.0, 1.0, 5.0, 3.0};
        std::vector<double> labels_plain = {70.0, 10.0, 50.0, 30.0};

        std::vector<EncryptedCandidate> candidates;
        candidates.reserve(scores_plain.size());
        auto zero_ref = hecompare::make_public_constant_like(
                env,
                env.context->Encrypt(
                        env.key_pair.publicKey,
                        env.context->MakeCKKSPackedPlaintext(
                                std::vector<double>(env.slot_count, 0.0),
                                1,
                                0,
                                nullptr,
                                env.slot_count)),
                0.0);

        for (size_t i = 0; i < scores_plain.size(); ++i) {
            candidates.push_back(EncryptedCandidate{
                    hecompare::make_public_constant_like(env, zero_ref, scores_plain[i]),
                    hecompare::make_public_constant_like(env, zero_ref, labels_plain[i]),
            });
        }

        print_levels("level_0_input", candidates);

        auto swap01 = hecompare::compare_and_swap_ascending(env, candidates[0], candidates[1]);
        auto swap23 = hecompare::compare_and_swap_ascending(env, candidates[2], candidates[3]);
        candidates[0] = swap01.first;
        candidates[1] = swap01.second;
        candidates[2] = swap23.first;
        candidates[3] = swap23.second;
        print_levels("level_0_output", candidates);

        auto swap02 = hecompare::compare_and_swap_ascending(env, candidates[0], candidates[2]);
        auto swap13 = hecompare::compare_and_swap_ascending(env, candidates[1], candidates[3]);
        candidates[0] = swap02.first;
        candidates[2] = swap02.second;
        candidates[1] = swap13.first;
        candidates[3] = swap13.second;
        print_levels("level_1_output", candidates);

        auto swap12 = hecompare::compare_and_swap_ascending(env, candidates[1], candidates[2]);
        candidates[1] = swap12.first;
        candidates[2] = swap12.second;
        print_levels("level_2_output", candidates);

        const std::vector<double> expected_scores = {1.0, 3.0, 5.0, 7.0};
        const std::vector<double> expected_labels = {10.0, 30.0, 50.0, 70.0};

        for (size_t i = 0; i < candidates.size(); ++i) {
            hecompare::test::expect_close(
                    hecompare::test::decrypt_first_slot_for_test(env, candidates[i].score),
                    expected_scores[i],
                    0.25,
                    "chain score " + std::to_string(i));
            hecompare::test::expect_close(
                    hecompare::test::decrypt_first_slot_for_test(env, candidates[i].label),
                    expected_labels[i],
                    0.25,
                    "chain label " + std::to_string(i));
        }

        std::cout << "openfhe-ckks-compare-chain-test passed" << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr << "openfhe-ckks-compare-chain-test failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
