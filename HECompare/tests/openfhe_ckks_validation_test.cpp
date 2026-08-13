#include "hecompare/ckks_compare.hpp"
#include "hecompare/ckks_context.hpp"
#include "hecompare/ckks_distance.hpp"
#include "tests/test_helpers.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using hecompare::CkksParameters;
using hecompare::EncryptedQueryChunks;
using hecompare::RealVector;

void expect_throws(
        const std::function<void()>& fn,
        const std::string& expected_substring,
        const std::string& label)
{
    try {
        fn();
    }
    catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find(expected_substring) == std::string::npos) {
            throw std::runtime_error(
                    label + " threw unexpected message: " + message);
        }
        return;
    }

    throw std::runtime_error(label + " did not throw");
}

} // namespace

int main()
{
    try {
        {
            CkksParameters params;
            params.slot_count = 0;
            expect_throws(
                    [&] { (void)hecompare::make_ckks_environment(params); },
                    "slot_count must be positive",
                    "slot_count=0");
        }
        {
            CkksParameters params;
            params.ring_dimension = 0;
            expect_throws(
                    [&] { (void)hecompare::make_ckks_environment(params); },
                    "ring_dimension must be positive",
                    "ring_dimension=0");
        }
        {
            CkksParameters params;
            params.slot_count = 4097;
            params.ring_dimension = 8192;
            expect_throws(
                    [&] { (void)hecompare::make_ckks_environment(params); },
                    "slot_count exceeds CKKS ring capacity",
                    "slot_count exceeds ring capacity");
        }
        {
            CkksParameters params;
            params.switching_value_count = 0;
            expect_throws(
                    [&] { (void)hecompare::make_ckks_environment(params); },
                    "switching_value_count must be positive",
                    "switching_value_count=0");
        }
        {
            CkksParameters params;
            params.switching_value_count = params.slot_count + 1;
            expect_throws(
                    [&] { (void)hecompare::make_ckks_environment(params); },
                    "switching_value_count exceeds slot_count",
                    "switching_value_count exceeds slot_count");
        }
        {
            CkksParameters params;
            params.scale_sign = 0.0;
            expect_throws(
                    [&] { (void)hecompare::make_ckks_environment(params); },
                    "scale_sign must be finite and positive",
                    "scale_sign=0");
        }
        {
            CkksParameters params;
            params.scale_sign = std::numeric_limits<double>::quiet_NaN();
            expect_throws(
                    [&] { (void)hecompare::make_ckks_environment(params); },
                    "scale_sign must be finite and positive",
                    "scale_sign=NaN");
        }
        {
            CkksParameters params;
            params.log_q_lwe = 64;
            expect_throws(
                    [&] { (void)hecompare::make_ckks_environment(params); },
                    "log_q_lwe must be in [1, 63]",
                    "log_q_lwe=64");
        }
        {
            CkksParameters params;
            params.multiplicative_depth = 0;
            expect_throws(
                    [&] { (void)hecompare::make_ckks_environment(params); },
                    "multiplicative_depth must be positive",
                    "multiplicative_depth=0");
        }

        CkksParameters params;
        params.multiplicative_depth = 20;
        params.scale_sign = 64.0;
        params.scaling_technique = lbcrypto::FLEXIBLEAUTOEXT;
        params.fhew_parameter_set = lbcrypto::STD128;
        params.switching_value_count = 1;
        auto env = hecompare::make_ckks_environment(params);

        auto lhs = hecompare::encrypt_query(env, {1.0});
        auto rhs = hecompare::encrypt_query(env, {2.0});

        expect_throws(
                [&] { (void)hecompare::encrypted_less_than(env, nullptr, rhs); },
                "comparison ciphertext is null",
                "null lhs");
        expect_throws(
                [&] { (void)hecompare::encrypted_less_than(env, lhs, nullptr); },
                "comparison ciphertext is null",
                "null rhs");
        expect_throws(
                [&] { (void)hecompare::encrypted_less_than(env, lhs, rhs, env.switching_value_count + 1); },
                "comparison value count exceeds scheme-switch setup",
                "count exceeds environment setup");

        CkksParameters params2 = params;
        params2.ring_dimension = 16384;
        params2.slot_count = 16;
        auto env2 = hecompare::make_ckks_environment(params2);
        auto rhs_other_context = hecompare::encrypt_query(env2, {2.0});
        expect_throws(
                [&] { (void)hecompare::encrypted_less_than(env, lhs, rhs_other_context); },
                "comparison ciphertext context mismatch",
                "context mismatch");

        auto other_key_pair = env.context->KeyGen();
        auto other_plaintext = env.context->MakeCKKSPackedPlaintext(
                std::vector<double>{2.0}, 1, 0, nullptr, env.slot_count);
        auto rhs_other_key = env.context->Encrypt(other_key_pair.publicKey, other_plaintext);
        expect_throws(
                [&] { (void)hecompare::encrypted_less_than(env, lhs, rhs_other_key); },
                "comparison ciphertext key does not match environment",
                "key mismatch");

        auto encrypted_query = hecompare::encrypt_query_chunks(env, {1.0, 2.0, 3.0});
        auto query_good = encrypted_query;
        query_good.squared_norms[0] = std::numeric_limits<double>::quiet_NaN();
        expect_throws(
                [&] { (void)hecompare::evaluate_chunked_squared_l2_distance(env, query_good, {1.0, 2.0, 3.0}); },
                "encrypted query squared norm is invalid",
                "squared_norm NaN");

        query_good = encrypted_query;
        query_good.squared_norms[0] = -1.0;
        expect_throws(
                [&] { (void)hecompare::evaluate_chunked_squared_l2_distance(env, query_good, {1.0, 2.0, 3.0}); },
                "encrypted query squared norm is invalid",
                "squared_norm negative");

        expect_throws(
                [&] { (void)hecompare::evaluate_chunked_squared_l2_distance(env, encrypted_query, {1.0, 2.0}); },
                "plaintext_train size must match encrypted_query.feature_count",
                "train size mismatch");

        auto broken_counts = encrypted_query;
        broken_counts.valid_counts[0] -= 1;
        expect_throws(
                [&] { (void)hecompare::evaluate_chunked_squared_l2_distance(env, broken_counts, {1.0, 2.0, 3.0}); },
                "valid_counts do not sum to feature_count",
                "valid_counts sum mismatch");

        auto broken_context = encrypted_query;
        auto foreign_env = hecompare::make_ckks_environment(params2);
        broken_context.ciphertexts[0] = hecompare::encrypt_query(foreign_env, {1.0});
        expect_throws(
                [&] { (void)hecompare::evaluate_chunked_squared_l2_distance(env, broken_context, {1.0, 2.0, 3.0}); },
                "encrypted_query chunk ciphertext context mismatch",
                "chunk context mismatch");

        auto broken_key = encrypted_query;
        auto foreign_key_pair = env.context->KeyGen();
        auto foreign_plain = env.context->MakeCKKSPackedPlaintext(
                std::vector<double>{1.0}, 1, 0, nullptr, env.slot_count);
        broken_key.ciphertexts[0] = env.context->Encrypt(foreign_key_pair.publicKey, foreign_plain);
        expect_throws(
                [&] { (void)hecompare::evaluate_chunked_squared_l2_distance(env, broken_key, {1.0, 2.0, 3.0}); },
                "encrypted query chunk key does not match environment",
                "chunk key mismatch");

        std::cout << "CONTEXT_VALIDATION = PASS" << '\n';
        std::cout << "COMPARE_INPUT_VALIDATION = PASS" << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr << "CONTEXT_VALIDATION = FAIL" << '\n';
        std::cerr << "COMPARE_INPUT_VALIDATION = FAIL" << '\n';
        std::cerr << "openfhe-ckks-validation-test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
