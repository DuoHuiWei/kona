#include "hecompare/ckks_compare.hpp"
#include "hecompare/ckks_context.hpp"
#include "tests/test_helpers.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using hecompare::CkksCiphertext;
using hecompare::CkksEnvironment;
using hecompare::CkksParameters;
using hecompare::EncryptedCandidate;

struct Config {
    int repeats = 5;
    double lhs_score = 7.0;
    double rhs_score = 3.0;
    double lhs_label = 70.0;
    double rhs_label = 30.0;
};

Config parse_args(int argc, char** argv)
{
    Config config;
    if (argc >= 2) {
        config.repeats = std::stoi(argv[1]);
        if (config.repeats <= 0)
            throw std::invalid_argument("repeats must be positive");
    }
    return config;
}

CkksCiphertext select_lhs_when_one(
        const CkksEnvironment& environment,
        const CkksCiphertext& selector,
        const CkksCiphertext& lhs,
        const CkksCiphertext& rhs)
{
    auto delta = environment.context->EvalSub(lhs, rhs);
    auto selected_delta = environment.context->EvalMult(selector, delta);
    return environment.context->EvalAdd(rhs, selected_delta);
}

CkksCiphertext complementary_selection(
        const CkksEnvironment& environment,
        const CkksCiphertext& lhs,
        const CkksCiphertext& rhs,
        const CkksCiphertext& selected)
{
    return environment.context->EvalSub(
            environment.context->EvalAdd(lhs, rhs),
            selected);
}

struct SwapResult {
    EncryptedCandidate minimum;
    EncryptedCandidate maximum;
};

SwapResult apply_compare_swap_with_selector(
        const CkksEnvironment& environment,
        const EncryptedCandidate& lhs,
        const EncryptedCandidate& rhs,
        const CkksCiphertext& cmp)
{
    auto min_score =
            select_lhs_when_one(environment, cmp, lhs.score, rhs.score);
    auto max_score =
            complementary_selection(environment, lhs.score, rhs.score, min_score);

    auto min_label =
            select_lhs_when_one(environment, cmp, lhs.label, rhs.label);
    auto max_label =
            complementary_selection(environment, lhs.label, rhs.label, min_label);

    return {
            EncryptedCandidate{min_score, min_label},
            EncryptedCandidate{max_score, max_label},
    };
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Config config = parse_args(argc, argv);

        CkksParameters params;
        params.scaling_technique = lbcrypto::FLEXIBLEAUTOEXT;
        params.multiplicative_depth = 20;
        params.switching_value_count = 1;

        const auto setup_begin = Clock::now();
        auto environment = hecompare::make_ckks_environment(params);
        const auto setup_end = Clock::now();
        const double setup_ms =
                std::chrono::duration<double, std::milli>(
                        setup_end - setup_begin).count();

        const auto encryption_begin = Clock::now();
        auto zero_ref = hecompare::make_public_constant_like(
                environment,
                environment.context->Encrypt(
                        environment.key_pair.publicKey,
                        environment.context->MakeCKKSPackedPlaintext(
                                std::vector<double>(
                                        environment.slot_count,
                                        0.0),
                                1,
                                0,
                                nullptr,
                                environment.slot_count)),
                0.0);

        EncryptedCandidate lhs{
                hecompare::make_public_constant_like(
                        environment,
                        zero_ref,
                        config.lhs_score),
                hecompare::make_public_constant_like(
                        environment,
                        zero_ref,
                        config.lhs_label),
        };
        EncryptedCandidate rhs{
                hecompare::make_public_constant_like(
                        environment,
                        zero_ref,
                        config.rhs_score),
                hecompare::make_public_constant_like(
                        environment,
                        zero_ref,
                        config.rhs_label),
        };
        const auto encryption_end = Clock::now();
        const double encryption_ms =
                std::chrono::duration<double, std::milli>(
                        encryption_end - encryption_begin).count();

        double compare_total_ms = 0.0;
        double swap_total_ms = 0.0;
        double decrypt_total_ms = 0.0;
        double total_online_ms = 0.0;

        double compare_plain = std::numeric_limits<double>::quiet_NaN();
        double min_score_plain = std::numeric_limits<double>::quiet_NaN();
        double max_score_plain = std::numeric_limits<double>::quiet_NaN();
        double min_label_plain = std::numeric_limits<double>::quiet_NaN();
        double max_label_plain = std::numeric_limits<double>::quiet_NaN();

        for (int i = 0; i < config.repeats; ++i) {
            const auto compare_begin = Clock::now();
            auto cmp = hecompare::encrypted_less_than(
                    environment,
                    lhs.score,
                    rhs.score);
            const auto compare_end = Clock::now();

            const auto swap_begin = Clock::now();
            auto swapped = apply_compare_swap_with_selector(
                    environment,
                    lhs,
                    rhs,
                    cmp);
            const auto swap_end = Clock::now();

            const auto decrypt_begin = Clock::now();
            compare_plain =
                    hecompare::test::decrypt_first_slot_for_test(
                            environment,
                            cmp);
            min_score_plain =
                    hecompare::test::decrypt_first_slot_for_test(
                            environment,
                            swapped.minimum.score);
            max_score_plain =
                    hecompare::test::decrypt_first_slot_for_test(
                            environment,
                            swapped.maximum.score);
            min_label_plain =
                    hecompare::test::decrypt_first_slot_for_test(
                            environment,
                            swapped.minimum.label);
            max_label_plain =
                    hecompare::test::decrypt_first_slot_for_test(
                            environment,
                            swapped.maximum.label);
            const auto decrypt_end = Clock::now();

            const double compare_ms =
                    std::chrono::duration<double, std::milli>(
                            compare_end - compare_begin).count();
            const double swap_ms =
                    std::chrono::duration<double, std::milli>(
                            swap_end - swap_begin).count();
            const double decrypt_ms =
                    std::chrono::duration<double, std::milli>(
                            decrypt_end - decrypt_begin).count();

            compare_total_ms += compare_ms;
            swap_total_ms += swap_ms;
            decrypt_total_ms += decrypt_ms;
            total_online_ms += compare_ms + swap_ms;

            hecompare::test::expect_close(
                    compare_plain,
                    0.0,
                    0.25,
                    "single compare result");
            hecompare::test::expect_close(
                    min_score_plain,
                    config.rhs_score,
                    0.25,
                    "single min score");
            hecompare::test::expect_close(
                    max_score_plain,
                    config.lhs_score,
                    0.25,
                    "single max score");
            hecompare::test::expect_close(
                    min_label_plain,
                    config.rhs_label,
                    0.25,
                    "single min label");
            hecompare::test::expect_close(
                    max_label_plain,
                    config.lhs_label,
                    0.25,
                    "single max label");

            std::cout << std::setprecision(17)
                      << "REPEAT " << (i + 1)
                      << " compare_ms=" << compare_ms
                      << " swap_ms=" << swap_ms
                      << " decrypt_ms=" << decrypt_ms
                      << " compare_plain=" << compare_plain
                      << " min_score=" << min_score_plain
                      << " max_score=" << max_score_plain
                      << " min_label=" << min_label_plain
                      << " max_label=" << max_label_plain
                      << '\n';
        }

        std::cout << std::setprecision(17)
                  << "BENCHMARK=CKKS_SINGLE_COMPARE_SWAP\n"
                  << "REPEATS=" << config.repeats << '\n'
                  << "SETUP_MS=" << setup_ms << '\n'
                  << "ENCRYPTION_MS=" << encryption_ms << '\n'
                  << "COMPARE_TOTAL_MS=" << compare_total_ms << '\n'
                  << "COMPARE_AVG_MS=" << (compare_total_ms / config.repeats) << '\n'
                  << "SWAP_TOTAL_MS=" << swap_total_ms << '\n'
                  << "SWAP_AVG_MS=" << (swap_total_ms / config.repeats) << '\n'
                  << "DECRYPT_TOTAL_MS=" << decrypt_total_ms << '\n'
                  << "DECRYPT_AVG_MS=" << (decrypt_total_ms / config.repeats) << '\n'
                  << "ONLINE_TOTAL_MS=" << total_online_ms << '\n'
                  << "ONLINE_AVG_MS=" << (total_online_ms / config.repeats) << '\n'
                  << "NOTE=SETUP_AND_ENCRYPTION_ARE_REPORTED_SEPARATELY_FROM_ONLINE_COMPARE_AND_SWAP\n"
                  << "CKKS_SINGLE_COMPARE_SWAP=PASS\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr << "CKKS_SINGLE_COMPARE_SWAP=FAIL\n"
                  << "ERROR=" << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
