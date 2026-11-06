#ifndef KONA_DCF_TEST_MATERIAL_HPP
#define KONA_DCF_TEST_MATERIAL_HPP
#include <fstream>
#include "Math/Z2k.hpp"
#include "Tools/random.h"
// Test/dealer setup only. Verbatim existing gen_fake_dcf from the preprocess
// bench, with only std qualification supplied by a local using directive.
// Reused fixed masks match the historical L2 tests; not production key lifecycle.
namespace KonaDcfTestMaterial {
using namespace std;
inline void gen_fake_dcf(int beta, int n)
{
    int lambda_bytes = 16;
    PRNG prng;
    prng.InitSeed();
    fstream k0, k1, r0, r1, r2;
    k0.open("Player-Data/k0", ios::out);
    k1.open("Player-Data/k1", ios::out);
    r0.open("Player-Data/r0", ios::out);
    r1.open("Player-Data/r1", ios::out);
    r2.open("Player-Data/r2", ios::out);
    octet seed[2][16];
    bigint s[2][2], v[2][2], t[2][2], tmp_t[2], convert[2], tcw[2];
    bigint a, scw, vcw, va, tmp, tmp1, tmp_out;
    prng.InitSeed();
    prng.get(tmp, n);
    bytesFromBigint(&seed[0][0], tmp, lambda_bytes);
    k0 << tmp << " ";
    prng.get(tmp1, n);
    bytesFromBigint(&seed[1][0], tmp1, lambda_bytes);
    k1 << tmp1 << " ";
    prng.get(a, n);
    prng.get(tmp, n);
    r1 << a - tmp << " ";
    r0 << tmp << " ";
    r2 << tmp << " ";
    r0.close();
    r1.close();
    r2.close();
    tmp_t[0] = 0;
    tmp_t[1] = 1;
    int keep, lose;
    va = 0;
    for (int i = 0; i < n - 1; i++)
    {
        keep = bigint(a >> (n - i - 1)).get_ui() & 1;
        lose = 1 ^ keep;
        for (int j = 0; j < 2; j++)
        {
            prng.SetSeed(seed[j]);
            for (int k = 0; k < 2; k++)
            {
                prng.get(t[k][j], 1);
                prng.get(v[k][j], n);
                prng.get(s[k][j], n);
            }
        }
        scw = s[lose][0] ^ s[lose][1];
        bytesFromBigint(&seed[0][0], v[lose][0], lambda_bytes);
        prng.SetSeed(seed[0]);
        prng.get(convert[0], n);
        bytesFromBigint(&seed[0][0], v[lose][1], lambda_bytes);
        prng.SetSeed(seed[0]);
        prng.get(convert[1], n);
        if (tmp_t[1])
            vcw = convert[0] + va - convert[1];
        else
            vcw = convert[1] - convert[0] - va;
        if (keep)
            vcw = vcw + tmp_t[1] * (-beta) + (1 - tmp_t[1]) * beta;
        bytesFromBigint(&seed[0][0], v[keep][0], lambda_bytes);
        prng.SetSeed(seed[0]);
        prng.get(convert[0], n);
        bytesFromBigint(&seed[0][0], v[keep][1], lambda_bytes);
        prng.SetSeed(seed[0]);
        prng.get(convert[1], n);
        va = va - convert[1] + convert[0] + tmp_t[1] * (-vcw) +
                (1 - tmp_t[1]) * vcw;
        tcw[0] = t[0][0] ^ t[0][1] ^ keep ^ 1;
        tcw[1] = t[1][0] ^ t[1][1] ^ keep;
        k0 << scw << " " << vcw << " " << tcw[0] << " " << tcw[1] << " ";
        k1 << scw << " " << vcw << " " << tcw[0] << " " << tcw[1] << " ";
        bytesFromBigint(&seed[0][0], s[keep][0] ^ (tmp_t[0] * scw), lambda_bytes);
        bytesFromBigint(&seed[1][0], s[keep][1] ^ (tmp_t[1] * scw), lambda_bytes);
        bigintFromBytes(tmp_out, &seed[0][0], lambda_bytes);
        bigintFromBytes(tmp_out, &seed[1][0], lambda_bytes);
        tmp_t[0] = t[keep][0] ^ (tmp_t[0] * tcw[keep]);
        tmp_t[1] = t[keep][1] ^ (tmp_t[1] * tcw[keep]);
    }

    prng.SetSeed(seed[0]);
    prng.get(convert[0], n);
    prng.SetSeed(seed[1]);
    prng.get(convert[1], n);
    k0 << tmp_t[1] * (-1 * (convert[1] - convert[0] - va)) +
            (1 - tmp_t[1]) * (convert[1] - convert[0] - va) << " ";
    k1 << tmp_t[1] * (-1 * (convert[1] - convert[0] - va)) +
            (1 - tmp_t[1]) * (convert[1] - convert[0] - va) << " ";
    k0.close();
    k1.close();
}
} // namespace KonaDcfTestMaterial
#endif
