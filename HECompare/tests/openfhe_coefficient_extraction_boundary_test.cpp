#include "openfhe.h"

#include <iostream>
#include <type_traits>

using namespace lbcrypto;

namespace {

using CC = CryptoContext<DCRTPoly>;

template <class T, class = void>
struct has_eval_ckks_to_fhew_setup : std::false_type {};

template <class T>
struct has_eval_ckks_to_fhew_setup<
        T,
        std::void_t<decltype(std::declval<T>()->EvalCKKStoFHEWSetup(
                std::declval<SchSwchParams>()))>> : std::true_type {};

template <class T, class = void>
struct has_eval_ckks_to_fhew : std::false_type {};

template <class T>
struct has_eval_ckks_to_fhew<
        T,
        std::void_t<decltype(std::declval<T>()->EvalCKKStoFHEW(
                std::declval<ConstCiphertext<DCRTPoly>&>(), 1u))>> : std::true_type {};

template <class T, class = void>
struct has_get_bin_cc_for_scheme_switch : std::false_type {};

template <class T>
struct has_get_bin_cc_for_scheme_switch<
        T,
        std::void_t<decltype(std::declval<T>()->GetBinCCForSchemeSwitch())>>
    : std::true_type {};

template <class T, class = void>
struct has_get_swkfc : std::false_type {};

template <class T>
struct has_get_swkfc<
        T,
        std::void_t<decltype(std::declval<T>()->GetSwkFC())>> : std::true_type {};

template <class T, class = void>
struct has_sample_extract : std::false_type {};

template <class T>
struct has_sample_extract<
        T,
        std::void_t<decltype(std::declval<T>()->SampleExtract(
                std::declval<ConstCiphertext<DCRTPoly>&>(), 0u))>> : std::true_type {};

template <class T, class = void>
struct has_eval_sample_extract : std::false_type {};

template <class T>
struct has_eval_sample_extract<
        T,
        std::void_t<decltype(std::declval<T>()->EvalSampleExtract(
                std::declval<ConstCiphertext<DCRTPoly>&>(), 0u))>> : std::true_type {};

template <class T, class = void>
struct has_extract_ciphertext_coefficient : std::false_type {};

template <class T>
struct has_extract_ciphertext_coefficient<
        T,
        std::void_t<decltype(std::declval<T>()->ExtractCiphertextCoefficient(
                std::declval<ConstCiphertext<DCRTPoly>&>(), 0u))>> : std::true_type {};

void print_probe_result(const char* name, bool value)
{
    std::cout << name << '=' << (value ? "true" : "false") << '\n';
}

} // namespace

int main()
{
    const bool ckks_to_fhew_setup = has_eval_ckks_to_fhew_setup<CC>::value;
    const bool ckks_to_fhew = has_eval_ckks_to_fhew<CC>::value;
    const bool get_bin_cc = has_get_bin_cc_for_scheme_switch<CC>::value;
    const bool get_swkfc = has_get_swkfc<CC>::value;

    const bool sample_extract = has_sample_extract<CC>::value;
    const bool eval_sample_extract = has_eval_sample_extract<CC>::value;
    const bool extract_ciphertext_coefficient =
            has_extract_ciphertext_coefficient<CC>::value;

    print_probe_result("has_eval_ckks_to_fhew_setup", ckks_to_fhew_setup);
    print_probe_result("has_eval_ckks_to_fhew", ckks_to_fhew);
    print_probe_result("has_get_bin_cc_for_scheme_switch", get_bin_cc);
    print_probe_result("has_get_swkfc", get_swkfc);
    print_probe_result("has_sample_extract", sample_extract);
    print_probe_result("has_eval_sample_extract", eval_sample_extract);
    print_probe_result(
            "has_extract_ciphertext_coefficient",
            extract_ciphertext_coefficient);

    if (!ckks_to_fhew_setup || !ckks_to_fhew || !get_bin_cc || !get_swkfc) {
        std::cerr << "Expected public scheme-switching APIs are missing." << '\n';
        return 1;
    }

    if (sample_extract || eval_sample_extract || extract_ciphertext_coefficient) {
        std::cerr << "Unexpected direct coefficient extraction API appeared in public CryptoContext surface." << '\n';
        return 1;
    }

    std::cout << "direct_public_sample_extract_api=false" << '\n';
    std::cout << "public_scheme_switching_api=true" << '\n';
    std::cout << "internal_extraction_mechanism_exists=refer_to_openfhe_ckksrns_schemeswitching_internal_code" << '\n';
    std::cout << "bfv_to_binfhe_compatibility=unverified" << '\n';
    std::cout << "boundary_conclusion=public CryptoContext surface exposes CKKS/FHEW scheme-switching hooks but not a direct SampleExtract-style coefficient-extraction API" << '\n';
    return 0;
}
