#include "binfhecontext.h"

#include <cstdlib>
#include <exception>
#include <iostream>

using namespace lbcrypto;

int main()
{
    try {
        BinFHEContext context;

        // This is only a build/runtime smoke test for the OpenFHE environment.
        context.GenerateBinFHEContext(TOY);

        const auto secret_key = context.KeyGen();
        context.BTKeyGen(secret_key);

        const auto ciphertext_1 = context.Encrypt(secret_key, 1);
        const auto ciphertext_2 = context.Encrypt(secret_key, 1);
        const auto ciphertext_result =
                context.EvalBinGate(AND, ciphertext_1, ciphertext_2);

        LWEPlaintext result = 0;
        context.Decrypt(secret_key, ciphertext_result, &result);

        std::cout << "kona-he OpenFHE smoke result: " << result << '\n';

        if (result != 1) {
            std::cerr << "Unexpected decryption result." << '\n';
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }
    catch (const std::exception& error) {
        std::cerr << "kona-he smoke test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
