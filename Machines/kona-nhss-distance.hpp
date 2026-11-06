#ifndef KONA_NHSS_DISTANCE_HPP
#define KONA_NHSS_DISTANCE_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <gmpxx.h>

#ifndef mpz_probable_prime_p
#define mpz_probable_prime_p(n, state, reps, trial_division) \
    mpz_probab_prime_p((n), (reps))
#define KONA_NHSS_UNDEF_MPZ_PROBABLE_PRIME_P
#endif
#include "Math/Z2k.hpp"
#ifdef KONA_NHSS_UNDEF_MPZ_PROBABLE_PRIME_P
#undef mpz_probable_prime_p
#undef KONA_NHSS_UNDEF_MPZ_PROBABLE_PRIME_P
#endif

/*
 * NHSS / NISED reference implementation for the Kona KNN pipeline.
 *
 * Paper construction:
 *   NHSS.KeyGen(lambda)
 *   NHSS.Share(x, sk)
 *   NHSS.TokenGen(y, pk)
 *   NHSS.LocalCalc(...)
 *   NHSS.DDlog(...)
 *
 * followed by
 *
 *   <delta>_b = <x^2>_b + <y^2>_b - 2<xy>_b.
 *
 * The paper uses SUBTRACTIVE shares:
 *
 *      v = <v>_1 - <v>_0.
 *
 * Kona's Cong path uses additive Z_2^64 shares.  The online helper
 * nised_distance_additive_share_u64() therefore returns
 *
 *      party 1 :  +<delta>_1
 *      party 0 :  -<delta>_0
 *
 * so the two returned values add to delta in Z_2^64.
 *
 * This file deliberately separates:
 *   - dealer/client preprocessing: keygen, data sharing, token generation
 *   - server online work: LocalCalc, DDlog, local distance aggregation
 *
 * No server-to-server communication occurs in the NISED online path.
 *
 * SECURITY NOTE:
 *   gmp_randclass with an explicit seed is used here for a reproducible
 *   research/benchmark implementation.  It is NOT a cryptographic RNG.
 *   Replace it with system CSPRNG-backed sampling before any production use.
 */

namespace KonaNhss
{

static const unsigned RING_BITS = 64;
using Share64 = Z2<RING_BITS>;

struct PublicKey
{
    mpz_class N;
    mpz_class N2;
    mpz_class g;
    mpz_class pk;
};

struct SecretKey
{
    mpz_class e;
};

struct KeyPair
{
    PublicKey pub;
    SecretKey sec;
};

struct Token
{
    // a = g^r mod N^2
    mpz_class a;
    // b = pk^r * (1+N)^y mod N^2
    mpz_class b;
};

struct DataShare
{
    // subtractive share: x = x_1 - x_0
    uint64_t x_share = 0;
    // integer subtractive share of s = e*x
    mpz_class s_share = 0;
    // subtractive share of x^2 in Z_2^64
    uint64_t x2_share = 0;
};

struct QueryShare
{
    // subtractive share of y^2 in Z_2^64
    uint64_t y2_share = 0;
    Token token;
};

struct SharedDataPair
{
    std::array<DataShare, 2> party;
};

struct SharedQueryPair
{
    std::array<QueryShare, 2> party;
};

inline mpz_class mpz_from_u64(uint64_t v)
{
    mpz_class z;
    mpz_import(z.get_mpz_t(), 1, -1, sizeof(v), 0, 0, &v);
    return z;
}

inline uint64_t u64_from_mpz_mod_2_64(const mpz_class& z)
{
    mpz_class r;
    mpz_fdiv_r_2exp(r.get_mpz_t(), z.get_mpz_t(), RING_BITS);
    uint64_t out = 0;
    std::size_t count = 0;
    mpz_export(&out, &count, -1, sizeof(out), 0, 0, r.get_mpz_t());
    return out;
}

inline Share64 u64_to_z2(uint64_t x)
{
    return Share64(static_cast<mp_limb_t>(x));
}

inline uint64_t random_u64(gmp_randclass& rng)
{
    return u64_from_mpz_mod_2_64(rng.get_z_bits(64));
}

inline mpz_class random_unit_mod(const mpz_class& N, gmp_randclass& rng)
{
    mpz_class x, gcd;
    do
    {
        x = rng.get_z_range(N - 1) + 1; // 1..N-1
        mpz_gcd(gcd.get_mpz_t(), x.get_mpz_t(), N.get_mpz_t());
    } while (gcd != 1);
    return x;
}

inline mpz_class mod_inverse(const mpz_class& a, const mpz_class& mod)
{
    mpz_class inv;
    if (mpz_invert(inv.get_mpz_t(), a.get_mpz_t(), mod.get_mpz_t()) == 0)
        throw std::runtime_error("NHSS: modular inverse does not exist");
    return inv;
}

inline mpz_class powm_nonnegative(const mpz_class& base,
                                  const mpz_class& exponent,
                                  const mpz_class& mod)
{
    if (exponent < 0)
        throw std::runtime_error("NHSS: negative exponent in powm_nonnegative");
    mpz_class out;
    mpz_powm(out.get_mpz_t(), base.get_mpz_t(),
             exponent.get_mpz_t(), mod.get_mpz_t());
    return out;
}

inline mpz_class powm_signed(const mpz_class& base,
                             const mpz_class& exponent,
                             const mpz_class& mod)
{
    if (exponent >= 0)
        return powm_nonnegative(base, exponent, mod);

    const mpz_class inv = mod_inverse(base, mod);
    return powm_nonnegative(inv, -exponent, mod);
}

/*
 * Exact NHSS.KeyGen from the paper:
 *   choose lambda-bit PRIME N,
 *   sample g' in Z_N^*,
 *   g = (g')^(N-1) mod N^2,
 *   sk=e in Z_N^*,
 *   pk=g^e mod N^2.
 */
inline KeyPair keygen(unsigned lambda = 1024,
                      unsigned long seed = 0x4b4f4e41UL)
{
    if (lambda < 128)
        throw std::runtime_error("NHSS: lambda is unreasonably small");

    gmp_randclass rng(gmp_randinit_mt);
    rng.seed(seed);

    mpz_class candidate;
    mpz_class N;
    for (;;)
    {
        candidate = rng.get_z_bits(lambda);
        mpz_setbit(candidate.get_mpz_t(), lambda - 1);
        mpz_setbit(candidate.get_mpz_t(), 0);
        mpz_nextprime(N.get_mpz_t(), candidate.get_mpz_t());

        if (mpz_sizeinbase(N.get_mpz_t(), 2) == lambda)
            break;
    }

    const mpz_class N2 = N * N;
    const mpz_class g_prime = random_unit_mod(N, rng);

    mpz_class exponent = N - 1;
    const mpz_class g = powm_nonnegative(g_prime, exponent, N2);

    const mpz_class e = random_unit_mod(N, rng);
    const mpz_class pk = powm_nonnegative(g, e, N2);

    KeyPair kp;
    kp.pub.N = N;
    kp.pub.N2 = N2;
    kp.pub.g = g;
    kp.pub.pk = pk;
    kp.sec.e = e;
    return kp;
}

/*
 * NHSS.Share(x, sk), with x1 - x0 = x and s1 - s0 = e*x.
 *
 * We keep x in [0, 2^32-1] so x^2 is representable in Z_2^64 without
 * ambiguity.  This is stricter than the paper's half-domain statement but
 * is convenient for the KNN benchmark path.
 */
inline SharedDataPair share_data(uint64_t x,
                                 const SecretKey& sk,
                                 gmp_randclass& rng)
{
    if (x > 0xffffffffULL)
        throw std::runtime_error(
            "NHSS: reference share_data expects x <= 2^32-1");

    const mpz_class two64 = mpz_class(1) << 64;
    const mpz_class max_x0 = two64 - 1 - mpz_from_u64(x);
    const mpz_class x0z = rng.get_z_range(max_x0 + 1);
    const mpz_class x1z = x0z + mpz_from_u64(x);

    const uint64_t x0 = u64_from_mpz_mod_2_64(x0z);
    const uint64_t x1 = u64_from_mpz_mod_2_64(x1z);

    const mpz_class s = sk.e * mpz_from_u64(x);
    const mpz_class slack = rng.get_z_bits(
        static_cast<unsigned long>(mpz_sizeinbase(sk.e.get_mpz_t(), 2) + 64));
    const mpz_class s0 = slack;
    const mpz_class s1 = slack + s;

    const uint64_t x2 = x * x;
    const uint64_t x2_0 = random_u64(rng);
    const uint64_t x2_1 = x2_0 + x2; // wrap = subtractive share in Z_2^64

    SharedDataPair out;
    out.party[0].x_share = x0;
    out.party[0].s_share = s0;
    out.party[0].x2_share = x2_0;
    out.party[1].x_share = x1;
    out.party[1].s_share = s1;
    out.party[1].x2_share = x2_1;
    return out;
}

/*
 * NHSS.TokenGen(y, pk):
 *   [[y]] = (g^r, pk^r (1+N)^y) mod N^2.
 */
inline Token token_gen(uint64_t y,
                       const PublicKey& pub,
                       gmp_randclass& rng)
{
    const mpz_class r = rng.get_z_range(pub.N);
    Token t;
    t.a = powm_nonnegative(pub.g, r, pub.N2);

    const mpz_class pkr = powm_nonnegative(pub.pk, r, pub.N2);
    const mpz_class one_plus_N = pub.N + 1;
    const mpz_class enc_y =
        powm_nonnegative(one_plus_N, mpz_from_u64(y), pub.N2);
    t.b = (pkr * enc_y) % pub.N2;
    return t;
}

inline SharedQueryPair share_query(uint64_t y,
                                   const PublicKey& pub,
                                   gmp_randclass& rng)
{
    if (y > 0xffffffffULL)
        throw std::runtime_error(
            "NHSS: reference share_query expects y <= 2^32-1");

    const uint64_t y2 = y * y;
    const uint64_t y2_0 = random_u64(rng);
    const uint64_t y2_1 = y2_0 + y2;

    const Token token = token_gen(y, pub, rng);

    SharedQueryPair out;
    out.party[0].y2_share = y2_0;
    out.party[0].token = token;
    out.party[1].y2_share = y2_1;
    out.party[1].token = token;
    return out;
}

/*
 * NHSS.LocalCalc:
 *   g_b = token.b^(x_b) * token.a^(-s_b) mod N^2.
 */
inline mpz_class local_calc(int party,
                            const DataShare& x,
                            const Token& token,
                            const PublicKey& pub)
{
    if (party != 0 && party != 1)
        throw std::runtime_error("NHSS: party must be 0 or 1");

    const mpz_class term1 =
        powm_nonnegative(token.b, mpz_from_u64(x.x_share), pub.N2);
    const mpz_class term2 =
        powm_signed(token.a, -x.s_share, pub.N2);
    return (term1 * term2) % pub.N2;
}

/*
 * NHSS.DDlog:
 *   z_b = floor(g_b/N) mod N.
 *
 * The simplified NHSS construction proves g_b mod N = 1, so the inverse
 * correction in the older HSS.DDlog disappears.
 */
inline mpz_class ddlog(int party,
                       const mpz_class& g_b,
                       const PublicKey& pub)
{
    if (party != 0 && party != 1)
        throw std::runtime_error("NHSS: party must be 0 or 1");

    mpz_class h_prime = g_b / pub.N;
    return h_prime % pub.N;
}

/*
 * Return the paper's subtractive distance share delta_b:
 *   delta_b = x2_b + y2_b - 2*z_b  (mod 2^64)
 * with delta = delta_1 - delta_0.
 */
inline uint64_t nised_distance_subtractive_share_u64(
    int party,
    const DataShare& x,
    const QueryShare& y,
    const PublicKey& pub)
{
    const mpz_class gb = local_calc(party, x, y.token, pub);
    const mpz_class zb = ddlog(party, gb, pub);
    const uint64_t z = u64_from_mpz_mod_2_64(zb);

    return x.x2_share + y.y2_share - uint64_t(2) * z;
}

/*
 * Convert paper-style subtractive output to Kona-style additive Z2<64>.
 */
inline Share64 nised_distance_additive_share_u64(
    int party,
    const DataShare& x,
    const QueryShare& y,
    const PublicKey& pub)
{
    const uint64_t delta_b =
        nised_distance_subtractive_share_u64(party, x, y, pub);

    if (party == 1)
        return u64_to_z2(delta_b);
    else if (party == 0)
        return u64_to_z2(uint64_t(0) - delta_b);

    throw std::runtime_error("NHSS: party must be 0 or 1");
}

/*
 * Multi-dimensional squared Euclidean distance.  Each coordinate is fully
 * local at the server; there are still zero server-to-server online rounds.
 */
inline Share64 nised_squared_euclidean_additive_share_batched_l2(
    int party,
    const std::vector<DataShare>& x,
    const std::vector<QueryShare>& y,
    const PublicKey& pub,
    std::size_t dimension_batch_size);

inline Share64 nised_squared_euclidean_additive_share_l2(
    int party,
    const std::vector<DataShare>& x,
    const std::vector<QueryShare>& y,
    const PublicKey& pub)
{
    return nised_squared_euclidean_additive_share_batched_l2(
        party, x, y, pub, 0);
}

/*
 * Process one sample's dimensions in explicit local batches. NHSS still has
 * zero server-to-server communication; this parameter controls only local
 * scheduling/cache footprint. A value of zero means all dimensions at once.
 */
inline Share64 nised_squared_euclidean_additive_share_batched_l2(
    int party,
    const std::vector<DataShare>& x,
    const std::vector<QueryShare>& y,
    const PublicKey& pub,
    std::size_t dimension_batch_size)
{
    if (x.size() != y.size())
        throw std::runtime_error("NHSS: dimension mismatch");

    Share64 total(0);
    const std::size_t batch_size = dimension_batch_size == 0 ?
        (x.empty() ? 1 : x.size()) : dimension_batch_size;
    for (std::size_t offset = 0; offset < x.size(); offset += batch_size)
    {
        const std::size_t end = std::min(x.size(), offset + batch_size);
        for (std::size_t j = offset; j < end; ++j)
            total += nised_distance_additive_share_u64(
                party, x[j], y[j], pub);
    }
    return total;
}

} // namespace KonaNhss

#endif
