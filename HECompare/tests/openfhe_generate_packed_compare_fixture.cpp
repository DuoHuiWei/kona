#include "hecompare/ckks_context.hpp"
#include "hecompare/ckks_distance.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/utils/serial.h"

namespace {

using hecompare::CkksCiphertext;
using hecompare::CkksEnvironment;
using hecompare::CkksParameters;
using hecompare::RealVector;

struct FixtureCase {
    double lhs_score = 0.0;
    double rhs_score = 0.0;
    uint32_t lhs_label = 0;
    uint32_t rhs_label = 0;
    double expected_compare = 0.0;
    double expected_min_score = 0.0;
    double expected_max_score = 0.0;
    uint32_t expected_min_label = 0;
    uint32_t expected_max_label = 0;
};

std::vector<FixtureCase> build_fixture_cases()
{
    std::vector<FixtureCase> cases;
    cases.reserve(16);

    for (uint32_t i = 0; i < 16; ++i) {
        const double low = 0.20 + 0.019 * static_cast<double>(i);
        const double high = 0.80 - 0.019 * static_cast<double>(i);

        FixtureCase slot;
        slot.lhs_label = i + 1;
        slot.rhs_label = 101 + i;

        if (i % 2 == 0) {
            slot.lhs_score = low;
            slot.rhs_score = high;
            slot.expected_compare = 1.0;
            slot.expected_min_score = slot.lhs_score;
            slot.expected_max_score = slot.rhs_score;
            slot.expected_min_label = slot.lhs_label;
            slot.expected_max_label = slot.rhs_label;
        }
        else {
            slot.lhs_score = high;
            slot.rhs_score = low;
            slot.expected_compare = 0.0;
            slot.expected_min_score = slot.rhs_score;
            slot.expected_max_score = slot.lhs_score;
            slot.expected_min_label = slot.rhs_label;
            slot.expected_max_label = slot.lhs_label;
        }

        cases.push_back(slot);
    }

    return cases;
}

RealVector collect_scores(
        const std::vector<FixtureCase>& cases,
        bool lhs)
{
    RealVector values;
    values.reserve(cases.size());
    for (const auto& slot : cases)
        values.push_back(lhs ? slot.lhs_score : slot.rhs_score);
    return values;
}

std::vector<RealVector> collect_label_bit_planes(
        const std::vector<FixtureCase>& cases,
        uint32_t bit_count,
        bool lhs)
{
    std::vector<RealVector> planes(
            bit_count,
            RealVector(cases.size(), 0.0));

    for (std::size_t slot = 0; slot < cases.size(); ++slot) {
        const uint32_t label =
                lhs ? cases[slot].lhs_label : cases[slot].rhs_label;
        for (uint32_t bit = 0; bit < bit_count; ++bit)
            planes[bit][slot] = static_cast<double>((label >> bit) & 1U);
    }

    return planes;
}

std::vector<CkksCiphertext> encrypt_planes(
        const CkksEnvironment& environment,
        const std::vector<RealVector>& planes)
{
    std::vector<CkksCiphertext> encrypted;
    encrypted.reserve(planes.size());
    for (const auto& plane : planes)
        encrypted.push_back(hecompare::encrypt_query(environment, plane));
    return encrypted;
}

void write_plaintext_csv(
        const std::filesystem::path& path,
        const std::vector<FixtureCase>& cases)
{
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("failed to open plaintext csv");

    out << "slot,lhs_score,rhs_score,lhs_label,rhs_label,"
           "expected_compare,expected_min_score,expected_max_score,"
           "expected_min_label,expected_max_label\n";
    out << std::setprecision(17);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto& c = cases[i];
        out << i << ','
            << c.lhs_score << ','
            << c.rhs_score << ','
            << c.lhs_label << ','
            << c.rhs_label << ','
            << c.expected_compare << ','
            << c.expected_min_score << ','
            << c.expected_max_score << ','
            << c.expected_min_label << ','
            << c.expected_max_label << '\n';
    }
}

void save_ciphertext_binary(
        const std::filesystem::path& path,
        const CkksCiphertext& ciphertext)
{
    if (!lbcrypto::Serial::SerializeToFile(
                path.string(),
                ciphertext,
                lbcrypto::SerType::BINARY)) {
        throw std::runtime_error("failed to serialize ciphertext: " + path.string());
    }
}

void write_manifest_json(
        const std::filesystem::path& path,
        const std::filesystem::path& dir,
        uint32_t label_bit_count)
{
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("failed to open manifest json");

    out << "{\n";
    out << "  \"fixture_name\": \"packed_compare_fixture_16\",\n";
    out << "  \"active_count\": 16,\n";
    out << "  \"label_bit_count\": " << label_bit_count << ",\n";
    out << "  \"plaintext_csv\": \"plaintext_cases.csv\",\n";
    out << "  \"ciphertexts\": {\n";
    out << "    \"lhs_score\": \"lhs_score.bin\",\n";
    out << "    \"rhs_score\": \"rhs_score.bin\",\n";
    out << "    \"lhs_label_bits\": [\n";
    for (uint32_t bit = 0; bit < label_bit_count; ++bit) {
        out << "      \"lhs_label_bit_" << bit << ".bin\"";
        out << (bit + 1 == label_bit_count ? "\n" : ",\n");
    }
    out << "    ],\n";
    out << "    \"rhs_label_bits\": [\n";
    for (uint32_t bit = 0; bit < label_bit_count; ++bit) {
        out << "      \"rhs_label_bit_" << bit << ".bin\"";
        out << (bit + 1 == label_bit_count ? "\n" : ",\n");
    }
    out << "    ]\n";
    out << "  },\n";
    out << "  \"notes\": \"Even slots satisfy lhs<rhs, odd slots satisfy lhs>rhs. Distances are deliberately well-separated.\"\n";
    out << "}\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::filesystem::path output_dir =
                argc >= 2
                        ? std::filesystem::path(argv[1])
                        : std::filesystem::path(
                                  "/workspace/Kona/HECompare/testdata/packed_compare_fixture_16");
        std::filesystem::create_directories(output_dir);

        CkksParameters params;
        params.slot_count = 4096;
        params.ring_dimension = 8192;
        params.multiplicative_depth = 20;
        params.scale_sign = 262144.0;
        params.scaling_technique = lbcrypto::FLEXIBLEAUTOEXT;
        params.fhew_parameter_set = lbcrypto::STD128;
        params.switching_value_count = 16;
        const uint32_t label_bit_count = 8;

        auto environment = hecompare::make_ckks_environment(params);
        const auto cases = build_fixture_cases();

        write_plaintext_csv(output_dir / "plaintext_cases.csv", cases);

        const auto lhs_scores = collect_scores(cases, true);
        const auto rhs_scores = collect_scores(cases, false);
        const auto lhs_planes =
                collect_label_bit_planes(cases, label_bit_count, true);
        const auto rhs_planes =
                collect_label_bit_planes(cases, label_bit_count, false);

        save_ciphertext_binary(
                output_dir / "lhs_score.bin",
                hecompare::encrypt_query(environment, lhs_scores));
        save_ciphertext_binary(
                output_dir / "rhs_score.bin",
                hecompare::encrypt_query(environment, rhs_scores));

        const auto lhs_encrypted_planes = encrypt_planes(environment, lhs_planes);
        const auto rhs_encrypted_planes = encrypt_planes(environment, rhs_planes);
        for (uint32_t bit = 0; bit < label_bit_count; ++bit) {
            save_ciphertext_binary(
                    output_dir /
                            ("lhs_label_bit_" + std::to_string(bit) + ".bin"),
                    lhs_encrypted_planes[bit]);
            save_ciphertext_binary(
                    output_dir /
                            ("rhs_label_bit_" + std::to_string(bit) + ".bin"),
                    rhs_encrypted_planes[bit]);
        }

        write_manifest_json(
                output_dir / "fixture_manifest.json",
                output_dir,
                label_bit_count);

        std::cout << "PACKED_COMPARE_FIXTURE=PASS\n";
        std::cout << "OUTPUT_DIR=" << output_dir.string() << '\n';
        std::cout << "PLAINTEXT_CSV="
                  << (output_dir / "plaintext_cases.csv").string() << '\n';
        std::cout << "MANIFEST_JSON="
                  << (output_dir / "fixture_manifest.json").string() << '\n';
        std::cout << "ACTIVE_COUNT=16\n";
        std::cout << "LABEL_BIT_COUNT=" << label_bit_count << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr << "PACKED_COMPARE_FIXTURE=FAIL\n";
        std::cerr << "ERROR=" << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
