#include "hecompare/ckks_distance.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
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

void validate_real_vector(
        const RealVector& values,
        const char* label)
{
    for (double value : values) {
        if (!std::isfinite(value))
            throw std::invalid_argument(std::string(label) + " contains non-finite value");
    }
}

} // namespace

double plaintext_dot(const RealVector& lhs, const RealVector& rhs)
{
    if (lhs.size() != rhs.size())
        throw std::invalid_argument("dot size mismatch");

    double sum = 0.0;
    for (size_t i = 0; i < lhs.size(); ++i)
        sum += lhs[i] * rhs[i];
    return sum;
}

double plaintext_squared_norm(const RealVector& values)
{
    return plaintext_dot(values, values);
}

double plaintext_reduced_score(
        const RealVector& query,
        const RealVector& train)
{
    return plaintext_squared_norm(train) - 2.0 * plaintext_dot(query, train);
}

std::vector<double> pad_to_slots(
        const RealVector& input,
        uint32_t slots)
{
    if (input.size() > slots)
        throw std::invalid_argument("input size exceeds slot count");

    std::vector<double> padded(slots, 0.0);
    std::copy(input.begin(), input.end(), padded.begin());
    return padded;
}

CkksCiphertext encrypt_query(
        const CkksEnvironment& environment,
        const RealVector& query)
{
    validate_environment_has_key(environment, "query encryption");
    if (query.empty())
        throw std::invalid_argument("query must not be empty");
    validate_real_vector(query, "query");
    const auto padded = pad_to_slots(query, environment.slot_count);
    auto plaintext = environment.context->MakeCKKSPackedPlaintext(
            padded,
            1,
            0,
            nullptr,
            environment.slot_count);
    return environment.context->Encrypt(
            environment.key_pair.publicKey,
            plaintext);
}

CkksCiphertext evaluate_reduced_score(
        const CkksEnvironment& environment,
        const CkksCiphertext& encrypted_query,
        const RealVector& plaintext_train)
{
    validate_environment_has_key(environment, "distance");
    if (!encrypted_query)
        throw std::invalid_argument("encrypted query ciphertext is null");
    if (encrypted_query->GetCryptoContext().get() != environment.context.get())
        throw std::invalid_argument("encrypted query ciphertext context mismatch");
    if (encrypted_query->GetKeyTag() != environment.key_pair.publicKey->GetKeyTag())
        throw std::invalid_argument("encrypted query ciphertext key does not match environment");
    if (plaintext_train.empty())
        throw std::invalid_argument("empty train vector");
    validate_real_vector(plaintext_train, "plaintext_train");

    const auto padded_train =
            pad_to_slots(plaintext_train, environment.slot_count);
    auto train_plaintext = environment.context->MakeCKKSPackedPlaintext(
            padded_train,
            1,
            0,
            nullptr,
            environment.slot_count);

    auto encrypted_products =
            environment.context->EvalMult(encrypted_query, train_plaintext);
    auto encrypted_dot = environment.context->EvalSum(
            encrypted_products,
            static_cast<uint32_t>(plaintext_train.size()));

    auto encrypted_score = environment.context->EvalMult(encrypted_dot, -2.0);
    encrypted_score = environment.context->EvalAdd(
            encrypted_score,
            plaintext_squared_norm(plaintext_train));
    return encrypted_score;
}

EncryptedQueryChunks encrypt_query_chunks(
        const CkksEnvironment& environment,
        const RealVector& query)
{
    if (query.empty())
        throw std::invalid_argument("query must not be empty");
    if (environment.slot_count == 0)
        throw std::invalid_argument("environment slot_count must be positive");

    EncryptedQueryChunks result;
    result.feature_count = query.size();
    result.slot_count = environment.slot_count;

    for (std::size_t offset = 0; offset < query.size(); offset += environment.slot_count) {
        const std::size_t take =
                std::min<std::size_t>(environment.slot_count, query.size() - offset);
        RealVector chunk(
                query.begin() + static_cast<std::ptrdiff_t>(offset),
                query.begin() + static_cast<std::ptrdiff_t>(offset + take));

        result.ciphertexts.push_back(encrypt_query(environment, chunk));
        result.squared_norms.push_back(plaintext_squared_norm(chunk));
        result.valid_counts.push_back(static_cast<uint32_t>(take));
    }

    return result;
}

CkksCiphertext evaluate_chunked_squared_l2_distance(
        const CkksEnvironment& environment,
        const EncryptedQueryChunks& encrypted_query,
        const RealVector& plaintext_train)
{
    validate_environment_has_key(environment, "distance");
    if (plaintext_train.empty())
        throw std::invalid_argument("plaintext_train must not be empty");
    if (plaintext_train.size() != encrypted_query.feature_count)
        throw std::invalid_argument("plaintext_train size must match encrypted_query.feature_count");
    if (encrypted_query.ciphertexts.empty())
        throw std::invalid_argument("encrypted_query.ciphertexts must not be empty");
    if (encrypted_query.ciphertexts.size() != encrypted_query.squared_norms.size() ||
        encrypted_query.ciphertexts.size() != encrypted_query.valid_counts.size())
        throw std::invalid_argument("encrypted_query vectors size mismatch");
    if (encrypted_query.slot_count != environment.slot_count)
        throw std::invalid_argument("encrypted_query slot_count must match environment slot_count");
    validate_real_vector(plaintext_train, "plaintext_train");

    const uint64_t valid_sum =
            std::accumulate(
                    encrypted_query.valid_counts.begin(),
                    encrypted_query.valid_counts.end(),
                    uint64_t{0});
    if (valid_sum != encrypted_query.feature_count)
        throw std::invalid_argument("encrypted_query valid_counts do not sum to feature_count");

    CkksCiphertext total;
    std::size_t offset = 0;
    for (std::size_t i = 0; i < encrypted_query.ciphertexts.size(); ++i) {
        const uint32_t valid_count = encrypted_query.valid_counts[i];
        if (valid_count == 0 || valid_count > environment.slot_count)
            throw std::invalid_argument("encrypted_query valid_count out of range");
        if (!encrypted_query.ciphertexts[i])
            throw std::invalid_argument("encrypted_query chunk ciphertext is null");
        if (encrypted_query.ciphertexts[i]->GetCryptoContext().get() != environment.context.get())
            throw std::invalid_argument("encrypted_query chunk ciphertext context mismatch");
        if (encrypted_query.ciphertexts[i]->GetKeyTag() != environment.key_pair.publicKey->GetKeyTag())
            throw std::invalid_argument("encrypted query chunk key does not match environment");
        const double query_norm = encrypted_query.squared_norms[i];
        if (!std::isfinite(query_norm) || query_norm < 0.0)
            throw std::invalid_argument("encrypted query squared norm is invalid");

        RealVector train_chunk(
                plaintext_train.begin() + static_cast<std::ptrdiff_t>(offset),
                plaintext_train.begin() + static_cast<std::ptrdiff_t>(offset + valid_count));
        offset += valid_count;

        auto encrypted_reduced_score = evaluate_reduced_score(
                environment,
                encrypted_query.ciphertexts[i],
                train_chunk);
        auto encrypted_squared_distance_chunk = environment.context->EvalAdd(
                encrypted_reduced_score,
                query_norm);

        if (!total)
            total = encrypted_squared_distance_chunk;
        else
            total = environment.context->EvalAdd(total, encrypted_squared_distance_chunk);
    }

    return total;
}

CkksCiphertext evaluate_chunked_mean_squared_l2_distance(
        const CkksEnvironment& environment,
        const EncryptedQueryChunks& encrypted_query,
        const RealVector& plaintext_train)
{
    auto squared_distance = evaluate_chunked_squared_l2_distance(
            environment,
            encrypted_query,
            plaintext_train);
    return environment.context->EvalMult(
            squared_distance,
            1.0 / static_cast<double>(encrypted_query.feature_count));
}

} // namespace hecompare
