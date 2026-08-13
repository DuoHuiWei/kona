#include "hecompare/ckks_compare.hpp"
#include "hecompare/ckks_context.hpp"
#include "hecompare/ckks_distance.hpp"
#include "tests/test_helpers.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using hecompare::CkksCiphertext;
using hecompare::CkksEnvironment;
using hecompare::EncryptedCandidate;
using hecompare::RealVector;

double minimum_correct_compare_gap(
        const CkksEnvironment& environment,
        const CkksCiphertext& base_score,
        double base_value,
        const std::vector<double>& gaps)
{
    for (double gap : gaps) {
        auto close_score = hecompare::make_public_constant_like(
                environment,
                base_score,
                base_value + gap);
        auto cmp = hecompare::encrypted_less_than(
                environment,
                base_score,
                close_score);
        const double cmp_plain =
                hecompare::test::decrypt_first_slot_for_test(environment, cmp);
        if (std::abs(cmp_plain - 1.0) <= 0.25)
            return gap;
    }

    return -1.0;
}

} // namespace

int main()
{
    try {
        auto env = hecompare::make_ckks_environment();

        const RealVector query = {1, 2, 3, 4, 0, 1, 2, 3};
        const std::vector<RealVector> trains = {
                {1, 2, 3, 4, 0, 1, 2, 3},
                {3, 2, 3, 4, 0, 1, 2, 3},
                {2, 2, 2, 2, 2, 2, 2, 2},
                {0, 0, 0, 0, 0, 0, 0, 0},
        };
        const std::vector<double> labels = {10, 20, 30, 40};
        double detected_small_gap = -1.0;

        auto encrypted_query = hecompare::encrypt_query(env, query);

        std::vector<CkksCiphertext> scores;
        scores.reserve(trains.size());

        double max_score_error = 0.0;
        for (size_t i = 0; i < trains.size(); ++i) {
            auto encrypted_score = hecompare::evaluate_reduced_score(
                    env,
                    encrypted_query,
                    trains[i]);

            const double expected =
                    hecompare::plaintext_reduced_score(query, trains[i]);
            const double obtained =
                    hecompare::test::decrypt_first_slot_for_test(env, encrypted_score);

            max_score_error =
                    std::max(max_score_error, std::abs(expected - obtained));

            hecompare::test::expect_close(
                    obtained,
                    expected,
                    1e-2,
                    "encrypted score " + std::to_string(i));

            scores.push_back(std::move(encrypted_score));
        }

        {
            auto cmp = hecompare::encrypted_less_than(env, scores[0], scores[1]);
            hecompare::test::expect_close(
                    hecompare::test::decrypt_first_slot_for_test(env, cmp),
                    1.0,
                    0.25,
                    "cmp score0 < score1");
        }

        {
            auto cmp = hecompare::encrypted_less_than(env, scores[1], scores[0]);
            hecompare::test::expect_close(
                    hecompare::test::decrypt_first_slot_for_test(env, cmp),
                    0.0,
                    0.25,
                    "cmp score1 < score0");
        }

        {
            auto cmp = hecompare::encrypted_less_than(env, scores[3], scores[2]);
            hecompare::test::expect_close(
                    hecompare::test::decrypt_first_slot_for_test(env, cmp),
                    0.0,
                    0.25,
                    "cmp score3 < score2");
        }

        {
            auto cmp = hecompare::encrypted_less_than(env, scores[0], scores[0]);
            const double value =
                    hecompare::test::decrypt_first_slot_for_test(env, cmp);
            if (value < -0.25 || value > 1.25) {
                throw std::runtime_error("cmp equal scores produced invalid value");
            }
        }

        EncryptedCandidate left{
                scores[1],
                hecompare::make_public_constant_like(env, scores[1], labels[1]),
        };
        EncryptedCandidate right{
                scores[0],
                hecompare::make_public_constant_like(env, scores[0], labels[0]),
        };

        auto [minimum, maximum] =
                hecompare::compare_and_swap_ascending(env, left, right);

        hecompare::test::expect_close(
                hecompare::test::decrypt_first_slot_for_test(env, minimum.score),
                -44.0,
                0.1,
                "minimum score");
        hecompare::test::expect_close(
                hecompare::test::decrypt_first_slot_for_test(env, minimum.label),
                10.0,
                0.1,
                "minimum label");
        hecompare::test::expect_close(
                hecompare::test::decrypt_first_slot_for_test(env, maximum.score),
                -40.0,
                0.1,
                "maximum score");
        hecompare::test::expect_close(
                hecompare::test::decrypt_first_slot_for_test(env, maximum.label),
                20.0,
                0.1,
                "maximum label");

        {
            const std::vector<double> gaps = {0.1, 0.01, 0.001};
            detected_small_gap =
                    minimum_correct_compare_gap(env, scores[0], -44.0, gaps);
        }

        {
            for (int round = 0; round < 8; ++round) {
                EncryptedCandidate lhs{
                        scores[1],
                        hecompare::make_public_constant_like(
                                env,
                                scores[1],
                                labels[1]),
                };
                EncryptedCandidate rhs{
                        scores[0],
                        hecompare::make_public_constant_like(
                                env,
                                scores[0],
                                labels[0]),
                };

                auto swapped =
                        hecompare::compare_and_swap_ascending(
                                env, lhs, rhs);

                hecompare::test::expect_close(
                        hecompare::test::decrypt_first_slot_for_test(
                                env, swapped.first.score),
                        -44.0,
                        0.2,
                        "repeated fresh min score");
                hecompare::test::expect_close(
                        hecompare::test::decrypt_first_slot_for_test(
                                env, swapped.first.label),
                        10.0,
                        0.2,
                        "repeated fresh min label");
                hecompare::test::expect_close(
                        hecompare::test::decrypt_first_slot_for_test(
                                env, swapped.second.score),
                        -40.0,
                        0.2,
                        "repeated fresh max score");
                hecompare::test::expect_close(
                        hecompare::test::decrypt_first_slot_for_test(
                                env, swapped.second.label),
                        20.0,
                        0.2,
                        "repeated fresh max label");
            }
        }

        std::cout << "openfhe-ckks-distance-compare-test passed" << '\n';
        std::cout << "score_max_abs_error=" << max_score_error << '\n';
        std::cout << "cmp_semantics=1_iff_lhs_less_than_rhs" << '\n';
        std::cout << "minimum_correct_compare_gap=" << detected_small_gap << '\n';
        std::cout << "validated_equal_score_case=true" << '\n';
        std::cout << "validated_repeated_compare_and_swap_on_fresh_inputs=true" << '\n';
        std::cout << "validated_path=encrypted_query -> ckks_score -> scheme_switch_compare -> encrypted_compare_swap" << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr << "openfhe-ckks-distance-compare-test failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
