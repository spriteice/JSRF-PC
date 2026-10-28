/*
 * A wider hash for the renderer's content checks, on x86-64.
 *
 * vc_hash (nv2a_gl.c) hashes every byte of what each kept draw's vertices
 * are made from, and of the textures the once-a-frame check compares: in
 * 99th Street on the Ally X over 300 MB a second, 7% of the GPU thread's
 * time. Its loop is xxHash64's: four 64-bit lanes, two 64-bit multiplies for
 * every 8 bytes.
 *
 * This one takes XXH3's approach (Yann Collet's xxHash, BSD 2-Clause; the
 * steps written here, not its code). Eight lanes take 64 bytes a step. Each
 * 8 bytes is xored with a key word and its two halves multiplied together, a
 * 32-by-32-bit multiply, of which SSE2 does two in one instruction and AVX2
 * four; the bytes themselves are added to the neighbouring lane, so none is
 * lost in a product of zero. Every 1 KB the lanes are scrambled. The key
 * words move on by one at each of the 16 steps in a kilobyte, so 64 bytes
 * moved within it change the hash. The last step takes the input's last 64
 * bytes, overlapping the step before, so nothing is read past the end. The
 * eight lanes are folded into one with 64-by-64-bit multiplies, then mixed
 * as vc_hash mixes. From 1 KB up it is 1.6 to 1.9 times as fast as vc_hash's
 * loop with SSE2 and 2 to 3 times with AVX2; under 256 bytes, the fold costs
 * more than the wider steps save, and vc_hash keeps its own loop.
 *
 * Where SSE2 is always there: x86-64. AVX2 on Linux and Windows where the
 * processor has it (the Ally X does) and the compiler is GCC or Clang.
 * Elsewhere NV2A_HASH_WIDE is not defined and vc_hash is as it was. The two
 * ways give the same hash, but a hash is only ever compared with others made
 * in the same run, so nothing depends on that. tests/vc_hash checks both
 * against the same steps in plain C.
 */
#ifndef NV2A_HASH_H
#define NV2A_HASH_H

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#define NV2A_HASH_WIDE 1
#endif

#if defined(NV2A_HASH_WIDE)

#include <stddef.h>
#include <stdint.h>
#include <emmintrin.h>
#if (defined(__GNUC__) || defined(__clang__)) && ((defined(__linux__) && !defined(__ANDROID__)) || defined(_WIN32))
#define NV2A_HASH_AVX2 1
#include <immintrin.h>
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#define NV2A_HASH_MIN      256     /* bytes: from here, the wider hash */
#define NV2A_HASH_STEP     64      /* bytes a step: eight lanes of 8 */
#define NV2A_HASH_STEPS    16      /* steps between scrambles: 1 KB */
#define NV2A_HASH_SCRAMBLE 23      /* key words: the scramble's eight... */
#define NV2A_HASH_LAST     31      /* ...the last step's... */
#define NV2A_HASH_FOLD     39      /* ...and the fold's; the steps' are 0 to 22 */
#define NV2A_HASH_KEYS     47
#define NV2A_HASH_MUL      0x9E3779B1u                /* the scramble's */
#define NV2A_HASH_LEN      0x9E3779B185EBCA87ull      /* the length's */
#define NV2A_HASH_MIX1     0xC2B2AE3D27D4EB4Full      /* the mix's, as vc_hash's */
#define NV2A_HASH_MIX2     0x165667B19E3779F9ull

/* splitmix64's first 47 outputs from 0. */
static const uint64_t nv2a_hash_key[NV2A_HASH_KEYS] = {
    0xE220A8397B1DCDAFull, 0x6E789E6AA1B965F4ull, 0x06C45D188009454Full, 0xF88BB8A8724C81ECull,
    0x1B39896A51A8749Bull, 0x53CB9F0C747EA2EAull, 0x2C829ABE1F4532E1ull, 0xC584133AC916AB3Cull,
    0x3EE5789041C98AC3ull, 0xF3B8488C368CB0A6ull, 0x657EECDD3CB13D09ull, 0xC2D326E0055BDEF6ull,
    0x8621A03FE0BBDB7Bull, 0x8E1F7555983AA92Full, 0xB54E0F1600CC4D19ull, 0x84BB3F97971D80ABull,
    0x7D29825C75521255ull, 0xC3CF17102B7F7F86ull, 0x3466E9A083914F64ull, 0xD81A8D2B5A4485ACull,
    0xDB01602B100B9ED7ull, 0xA9038A921825F10Dull, 0xEDF5F1D90DCA2F6Aull, 0x54496AD67BD2634Cull,
    0xDD7C01D4F5407269ull, 0x935E82F1DB4C4F7Bull, 0x69B82EBC92233300ull, 0x40D29EB57DE1D510ull,
    0xA2F09DABB45C6316ull, 0xEE521D7A0F4D3872ull, 0xF16952EE72F3454Full, 0x377D35DEA8E40225ull,
    0x0C7DE8064963BAB0ull, 0x05582D37111AC529ull, 0xD254741F599DC6F7ull, 0x69630F7593D108C3ull,
    0x417EF96181DAA383ull, 0x3C3C41A3B43343A1ull, 0x6E19905DCBE531DFull, 0x4FA9FA7324851729ull,
    0x84EB4454A792922Aull, 0x134F7096918175CEull, 0x07DC930B302278A8ull, 0x12C015A97019E937ull,
    0xCC06C31652EBF438ull, 0xECEE65630A691E37ull, 0x3E84ECB1763E79ADull,
};

/* The 128-bit product of a and b, its halves xored. */
static inline uint64_t nv2a_hash_fold(uint64_t a, uint64_t b)
{
#if defined(_MSC_VER) && !defined(__clang__)
    uint64_t hi, lo = _umul128(a, b, &hi);
    return lo ^ hi;
#else
    unsigned __int128 p = (unsigned __int128)a * b;
    return (uint64_t)p ^ (uint64_t)(p >> 64);
#endif
}

/* The eight lanes folded into one, with the length, and mixed. */
static inline uint64_t nv2a_hash_end(const uint64_t lane[8], size_t n)
{
    const uint64_t *k = nv2a_hash_key + NV2A_HASH_FOLD;
    uint64_t h = (uint64_t)n * NV2A_HASH_LEN;
    int i;
    for (i = 0; i < 8; i += 2)
        h += nv2a_hash_fold(lane[i] ^ k[i], lane[i + 1] ^ k[i + 1]);
    h ^= h >> 33; h *= NV2A_HASH_MIX1; h ^= h >> 29; h *= NV2A_HASH_MIX2; h ^= h >> 32;
    return h;
}

/* With SSE2, two lanes to a register, four registers. A step's 16 bytes
 * into a register's two lanes: */
static inline __m128i nv2a_hash_step(__m128i acc, const uint8_t *p, const uint64_t *k)
{
    __m128i d  = _mm_loadu_si128((const __m128i *)(const void *)p);
    __m128i dk = _mm_xor_si128(d, _mm_loadu_si128((const __m128i *)(const void *)k));
    __m128i hi = _mm_shuffle_epi32(dk, _MM_SHUFFLE(0, 3, 0, 1));   /* each lane's top half */
    __m128i sw = _mm_shuffle_epi32(d, _MM_SHUFFLE(1, 0, 3, 2));    /* the two lanes swapped */
    return _mm_add_epi64(acc, _mm_add_epi64(_mm_mul_epu32(dk, hi), sw));
}

/* Each lane: its top bits folded down, a key word, then multiplied. */
static inline __m128i nv2a_hash_scramble(__m128i acc, const uint64_t *k)
{
    const __m128i mul = _mm_set1_epi32((int)NV2A_HASH_MUL);
    __m128i a  = _mm_xor_si128(_mm_xor_si128(acc, _mm_srli_epi64(acc, 47)),
                               _mm_loadu_si128((const __m128i *)(const void *)k));
    __m128i lo = _mm_mul_epu32(a, mul);
    __m128i hi = _mm_mul_epu32(_mm_shuffle_epi32(a, _MM_SHUFFLE(2, 3, 0, 1)), mul);
    return _mm_add_epi64(lo, _mm_slli_epi64(hi, 32));
}

#define NV2A_HASH_STEP_SSE2(p, k) do { \
        a0 = nv2a_hash_step(a0, (p), (k));          a1 = nv2a_hash_step(a1, (p) + 16, (k) + 2); \
        a2 = nv2a_hash_step(a2, (p) + 32, (k) + 4); a3 = nv2a_hash_step(a3, (p) + 48, (k) + 6); \
    } while (0)

static inline uint64_t nv2a_hash_sse2(const void *data, size_t n, uint64_t seed)
{
    const uint8_t *p = (const uint8_t *)data;
    const uint64_t *k = nv2a_hash_key, *ks = k + NV2A_HASH_SCRAMBLE;
    const size_t block = (size_t)NV2A_HASH_STEP * NV2A_HASH_STEPS;
    /* Whole kilobytes, then whole steps, before the last 64 bytes: with n
     * less one, so the last step always has bytes of its own. */
    size_t blocks = (n - 1) / block, steps = (n - 1) % block / NV2A_HASH_STEP, b, s;
    __m128i a0 = _mm_set1_epi64x((long long)seed), a1 = a0, a2 = a0, a3 = a0;
    uint64_t lane[8];
    for (b = 0; b < blocks; b++, p += block) {
        for (s = 0; s < NV2A_HASH_STEPS; s++)
            NV2A_HASH_STEP_SSE2(p + s * NV2A_HASH_STEP, k + s);
        a0 = nv2a_hash_scramble(a0, ks);     a1 = nv2a_hash_scramble(a1, ks + 2);
        a2 = nv2a_hash_scramble(a2, ks + 4); a3 = nv2a_hash_scramble(a3, ks + 6);
    }
    for (s = 0; s < steps; s++)
        NV2A_HASH_STEP_SSE2(p + s * NV2A_HASH_STEP, k + s);
    NV2A_HASH_STEP_SSE2((const uint8_t *)data + n - NV2A_HASH_STEP, k + NV2A_HASH_LAST);
    _mm_storeu_si128((__m128i *)(void *)(lane + 0), a0);
    _mm_storeu_si128((__m128i *)(void *)(lane + 2), a1);
    _mm_storeu_si128((__m128i *)(void *)(lane + 4), a2);
    _mm_storeu_si128((__m128i *)(void *)(lane + 6), a3);
    return nv2a_hash_end(lane, n);
}

#if defined(NV2A_HASH_AVX2)
/* The same with AVX2, four lanes to a register, two registers. */
__attribute__((target("avx2")))
static inline __m256i nv2a_hash_step_avx2(__m256i acc, const uint8_t *p, const uint64_t *k)
{
    __m256i d  = _mm256_loadu_si256((const __m256i *)(const void *)p);
    __m256i dk = _mm256_xor_si256(d, _mm256_loadu_si256((const __m256i *)(const void *)k));
    __m256i hi = _mm256_shuffle_epi32(dk, _MM_SHUFFLE(0, 3, 0, 1));
    __m256i sw = _mm256_shuffle_epi32(d, _MM_SHUFFLE(1, 0, 3, 2));
    return _mm256_add_epi64(acc, _mm256_add_epi64(_mm256_mul_epu32(dk, hi), sw));
}

__attribute__((target("avx2")))
static inline __m256i nv2a_hash_scramble_avx2(__m256i acc, const uint64_t *k)
{
    const __m256i mul = _mm256_set1_epi32((int)NV2A_HASH_MUL);
    __m256i a  = _mm256_xor_si256(_mm256_xor_si256(acc, _mm256_srli_epi64(acc, 47)),
                                  _mm256_loadu_si256((const __m256i *)(const void *)k));
    __m256i lo = _mm256_mul_epu32(a, mul);
    __m256i hi = _mm256_mul_epu32(_mm256_shuffle_epi32(a, _MM_SHUFFLE(2, 3, 0, 1)), mul);
    return _mm256_add_epi64(lo, _mm256_slli_epi64(hi, 32));
}

__attribute__((target("avx2")))
static uint64_t nv2a_hash_avx2(const void *data, size_t n, uint64_t seed)
{
    const uint8_t *p = (const uint8_t *)data;
    const uint64_t *k = nv2a_hash_key, *ks = k + NV2A_HASH_SCRAMBLE;
    const size_t block = (size_t)NV2A_HASH_STEP * NV2A_HASH_STEPS;
    size_t blocks = (n - 1) / block, steps = (n - 1) % block / NV2A_HASH_STEP, b, s;
    __m256i a0 = _mm256_set1_epi64x((long long)seed), a1 = a0;
    const uint8_t *q;
    uint64_t lane[8];
    for (b = 0; b < blocks; b++, p += block) {
        for (s = 0; s < NV2A_HASH_STEPS; s++) {
            q = p + s * NV2A_HASH_STEP;
            a0 = nv2a_hash_step_avx2(a0, q, k + s);
            a1 = nv2a_hash_step_avx2(a1, q + 32, k + s + 4);
        }
        a0 = nv2a_hash_scramble_avx2(a0, ks);
        a1 = nv2a_hash_scramble_avx2(a1, ks + 4);
    }
    for (s = 0; s < steps; s++) {
        q = p + s * NV2A_HASH_STEP;
        a0 = nv2a_hash_step_avx2(a0, q, k + s);
        a1 = nv2a_hash_step_avx2(a1, q + 32, k + s + 4);
    }
    q = (const uint8_t *)data + n - NV2A_HASH_STEP;
    a0 = nv2a_hash_step_avx2(a0, q, k + NV2A_HASH_LAST);
    a1 = nv2a_hash_step_avx2(a1, q + 32, k + NV2A_HASH_LAST + 4);
    _mm256_storeu_si256((__m256i *)(void *)(lane + 0), a0);
    _mm256_storeu_si256((__m256i *)(void *)(lane + 4), a1);
    return nv2a_hash_end(lane, n);
}
#endif

/* A 64-bit hash of n bytes, n at least NV2A_HASH_MIN. */
static inline uint64_t nv2a_hash_wide(const void *data, size_t n, uint64_t seed)
{
#if defined(NV2A_HASH_AVX2)
    static int avx2 = -1;
    if (avx2 < 0) avx2 = __builtin_cpu_supports("avx2") ? 1 : 0;
    if (avx2) return nv2a_hash_avx2(data, n, seed);
#endif
    return nv2a_hash_sse2(data, n, seed);
}

#endif /* NV2A_HASH_WIDE */

#endif
