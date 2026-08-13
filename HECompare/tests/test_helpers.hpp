#pragma once

#include "hecompare/ckks_context.hpp"

#include <string>

namespace hecompare::test {

double decrypt_first_slot_for_test(
        const CkksEnvironment& environment,
        const CkksCiphertext& ciphertext);

void expect_close(
        double actual,
        double expected,
        double tolerance,
        const std::string& label);

} // namespace hecompare::test
