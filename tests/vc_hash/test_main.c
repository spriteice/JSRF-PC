/* Run with: python tests/vc_hash/run.py
 *
 * vc_hash hands 256 bytes and more to nv2a_hash_wide (nv2a_hash.h), which
 * hashes 64 bytes a step in eight lanes with SSE2, or AVX2 where the
 * processor has it. Both ways must give what the same steps give one lane at
 * a time in plain C, at every length from 64 bytes to over 4 KB, at any
 * alignment and with any seed; vc_hash must give its own loop's hash under
 * 256 bytes and the wider one from there; a changed bit, 64 bytes moved within
 * a kilobyte and two words swapped within a step must each change the hash;
 * and nothing may be read past either end of the bytes it is given. It says
 * how fast each way is too, which is not checked.
 */
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) fails++;
    printf("  %s: %s\n", ok ? "ok" : "FAIL", what);
}

/* The data: the same every run. */
static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return g_rng;
}
static void fill(uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) p[i] = (uint8_t)(rnd() >> 24);
}

/* The steps one lane at a time. */
static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}
static void ref_step(uint64_t acc[8], const uint8_t *p, const uint64_t *k)
{
    int j;
    for (j = 0; j < 8; j++) {
        uint64_t d = le64(p + 8 * j), dk = d ^ k[j];
        acc[j] += (dk & 0xFFFFFFFFull) * (dk >> 32);
        acc[j ^ 1] += d;
    }
}
static void ref_scramble(uint64_t acc[8], const uint64_t *k)
{
    int j;
    for (j = 0; j < 8; j++)
        acc[j] = (acc[j] ^ (acc[j] >> 47) ^ k[j]) * 0x9E3779B1ull;
}
/* The 128-bit product from 32-bit halves, its halves xored. */
static uint64_t ref_fold(uint64_t a, uint64_t b)
{
    const uint64_t m = 0xFFFFFFFFull;
    uint64_t ll = (a & m) * (b & m), lh = (a & m) * (b >> 32);
    uint64_t hl = (a >> 32) * (b & m), hh = (a >> 32) * (b >> 32);
    uint64_t mid = (ll >> 32) + (lh & m) + (hl & m);
    uint64_t lo = (ll & m) | (mid << 32), hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    return lo ^ hi;
}
static uint64_t ref_hash(const uint8_t *data, size_t n, uint64_t seed)
{
    const uint64_t *k = nv2a_hash_key;
    const uint8_t *p = data;
    size_t blocks = (n - 1) / 1024, steps = (n - 1) % 1024 / 64, b, s;
    uint64_t acc[8], h;
    int j;
    for (j = 0; j < 8; j++) acc[j] = seed;
    for (b = 0; b < blocks; b++, p += 1024) {
        for (s = 0; s < 16; s++) ref_step(acc, p + 64 * s, k + s);
        ref_scramble(acc, k + 23);
    }
    for (s = 0; s < steps; s++) ref_step(acc, p + 64 * s, k + s);
    ref_step(acc, data + n - 64, k + 31);
    h = (uint64_t)n * 0x9E3779B185EBCA87ull;
    for (j = 0; j < 8; j += 2) h += ref_fold(acc[j] ^ k[39 + j], acc[j + 1] ^ k[39 + j + 1]);
    h ^= h >> 33; h *= 0xC2B2AE3D27D4EB4Full; h ^= h >> 29; h *= 0x165667B19E3779F9ull; h ^= h >> 32;
    return h;
}

static int have_avx2(void)
{
    return __builtin_cpu_supports("avx2") ? 1 : 0;
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
/* GB/s hashing n bytes at a time through 32 KB, the best of five. */
static double speed(uint64_t (*f)(const void *, size_t, uint64_t), const uint8_t *buf, size_t n)
{
    size_t span = n < 32768 ? 32768 : n, reps = (64u << 20) / n, off, i;
    double best = 0.0, t0, r;
    uint64_t h = 0;
    int t;
    for (t = 0; t < 5; t++) {
        t0 = now_s();
        for (i = 0, off = 0; i < reps; i++) {
            h = f(buf + off, n, h);
            off += n;
            if (off + n > span) off = 0;
        }
        r = (double)(reps * n) / (now_s() - t0) / 1e9;
        if (r > best) best = r;
    }
    if (h == 1) puts("");
    return best;
}
static uint64_t avx2_or_sse2(const void *d, size_t n, uint64_t s)
{
    return have_avx2() ? nv2a_hash_avx2(d, n, s) : nv2a_hash_sse2(d, n, s);
}

int main(void)
{
    enum { BIG = (1 << 20) + 64 };
    uint8_t *buf = malloc(BIG + 64), *copy = malloc(BIG + 64);
    int avx2 = have_avx2(), ok, i;
    size_t n, a, j;

    if (!buf || !copy) return 2;
    fill(buf, BIG + 64);
    printf("vc_hash: the wider hash %s\n", avx2 ? "with SSE2 and AVX2 (this processor has both)"
                                                : "with SSE2 (this processor has no AVX2: that way is not checked)");

    printf("the key words\n");
    {
        uint64_t x = 0, z;
        ok = 1;
        for (i = 0; i < NV2A_HASH_KEYS; i++) {
            x += 0x9E3779B97F4A7C15ull;
            z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            ok &= nv2a_hash_key[i] == (z ^ (z >> 31));
        }
        check(ok, "splitmix64's first 47 from 0");
    }

    printf("the same as one lane at a time\n");
    {
        int sse = 1, wide = 1, avx = 1;
        for (n = 64; n <= 4200; n++)
            for (a = 0; a < 3; a++) {
                const uint8_t *p = buf + (a == 0 ? 0 : a == 1 ? 1 : 7) + (n * 13) % 4096;
                uint64_t seed = rnd(), want = ref_hash(p, n, seed);
                sse &= nv2a_hash_sse2(p, n, seed) == want;
                if (avx2) avx &= nv2a_hash_avx2(p, n, seed) == want;
                if (n >= NV2A_HASH_MIN) wide &= nv2a_hash_wide(p, n, seed) == want;
            }
        {
            static const size_t big[] = { 16384, 16385, 65536 + 9, 1 << 20, (1 << 20) + 63 };
            for (j = 0; j < sizeof big / sizeof *big; j++) {
                uint64_t seed = rnd(), want = ref_hash(buf + 1, big[j], seed);
                sse &= nv2a_hash_sse2(buf + 1, big[j], seed) == want;
                if (avx2) avx &= nv2a_hash_avx2(buf + 1, big[j], seed) == want;
                wide &= nv2a_hash_wide(buf + 1, big[j], seed) == want;
            }
        }
        check(sse, "SSE2: every length from 64 bytes to 4200, at three alignments, and up to 1 MB");
        if (avx2) check(avx, "AVX2: the same");
        check(wide, "nv2a_hash_wide: from 256 bytes");
    }

    printf("vc_hash\n");
    {
        int loop = 1, wide = 1;
        for (n = 0; n < 2048; n++) {
            uint64_t seed = rnd();
            if (n < NV2A_HASH_MIN) loop &= vc_hash(buf + 3, n, seed) == vc_hash_loop(buf + 3, n, seed);
            else wide &= vc_hash(buf + 3, n, seed) == ref_hash(buf + 3, n, seed);
        }
        check(loop, "under 256 bytes, its own loop's hash, as before");
        check(wide, "from 256 bytes, the wider hash");
    }

    printf("what changes the hash\n");
    {
        static const size_t lens[] = { 256, 257, 1000, 1024, 1025, 3000 };
        int bits = 1, moved = 1, swapped = 1, seeds = 1;
        for (j = 0; j < sizeof lens / sizeof *lens; j++) {
            uint64_t h0;
            n = lens[j];
            memcpy(copy, buf, n);
            h0 = nv2a_hash_wide(copy, n, 0);
            for (a = 0; a < n * 8; a++) {
                copy[a / 8] ^= (uint8_t)(1u << (a % 8));
                bits &= nv2a_hash_wide(copy, n, 0) != h0;
                copy[a / 8] ^= (uint8_t)(1u << (a % 8));
            }
        }
        check(bits, "every bit, one at a time, at 256, 257, 1000, 1024, 1025 and 3000 bytes");
        /* 64 bytes moved: two steps swapped, within a kilobyte and across. */
        n = 2048;
        {
            uint64_t h0 = nv2a_hash_wide(buf, n, 0);
            size_t s, t;
            for (s = 0; s < n / 64; s++)
                for (t = s + 1; t < n / 64; t++) {
                    memcpy(copy, buf, n);
                    memcpy(copy + 64 * s, buf + 64 * t, 64);
                    memcpy(copy + 64 * t, buf + 64 * s, 64);
                    moved &= nv2a_hash_wide(copy, n, 0) != h0;
                }
            /* Two words of one step swapped: the lane each is added to as well. */
            for (s = 0; s + 8 <= n; s += 64)
                for (t = 0; t < 8; t++) {
                    size_t u;
                    for (u = t + 1; u < 8; u++) {
                        memcpy(copy, buf, n);
                        memcpy(copy + s + 8 * t, buf + s + 8 * u, 8);
                        memcpy(copy + s + 8 * u, buf + s + 8 * t, 8);
                        swapped &= nv2a_hash_wide(copy, n, 0) != h0;
                    }
                }
        }
        check(moved, "every two of the 32 steps of 2 KB swapped");
        check(swapped, "every two words of a step swapped, in every step of 2 KB");
        /* The seed: vc_hash's chain over a draw's arrays. */
        {
            static uint64_t seen[4096];
            size_t m = 0, x, y;
            memset(copy, 0, 4096);
            for (x = 0; x < 4096; x++) seen[m++] = nv2a_hash_wide(copy, 1024, x * 0x9E3779B97F4A7C15ull);
            for (x = 0; x < m; x++)
                for (y = x + 1; y < m; y++) seeds &= seen[x] != seen[y];
        }
        check(seeds, "4096 seeds over the same kilobyte of zeros, all different");
    }

    printf("no two the same\n");
    {
        enum { N = 1 << 19 };
        uint64_t *h = malloc(sizeof(uint64_t) * N);
        int unique = 1;
        size_t k;
        if (!h) return 2;
        /* Each from its own byte, so no two are the same bytes. */
        for (k = 0; k < N; k++)
            h[k] = nv2a_hash_wide(buf + k, NV2A_HASH_MIN + (size_t)(rnd() % 1793), 0);
        /* Sorted, then side by side. */
        {
            size_t gap, x;
            for (gap = N / 2; gap > 0; gap /= 2)          /* Shell's sort */
                for (x = gap; x < N; x++) {
                    uint64_t v = h[x];
                    size_t y = x;
                    while (y >= gap && h[y - gap] > v) { h[y] = h[y - gap]; y -= gap; }
                    h[y] = v;
                }
            for (x = 1; x < N; x++) unique &= h[x] != h[x - 1];
        }
        check(unique, "2^19 pieces of 256 bytes to 2 KB, each from its own byte, every hash different");
        free(h);
    }

    printf("nothing read past either end\n");
    {
        long page = sysconf(_SC_PAGESIZE);
        uint8_t *m = mmap(NULL, (size_t)page * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) return 2;
        /* A guard page, two pages to read, a guard page. */
        mprotect(m, (size_t)page, PROT_NONE);
        mprotect(m + 3 * page, (size_t)page, PROT_NONE);
        fill(m + page, (size_t)page * 2);
        for (n = 64; n <= (size_t)page * 2; n++) {
            uint64_t h1 = nv2a_hash_sse2(m + page, n, 0), h2 = nv2a_hash_sse2(m + 3 * page - n, n, 0);
            if (avx2) { h1 ^= nv2a_hash_avx2(m + page, n, 0); h2 ^= nv2a_hash_avx2(m + 3 * page - n, n, 0); }
            if (h1 == 1 && h2 == 1) puts("");
        }
        check(1, "every length from 64 bytes to two pages, at the start and at the end of them");
        munmap(m, (size_t)page * 4);
    }

    printf("how fast (not checked)\n");
    {
        static const size_t sizes[] = { 256, 1024, 12288, 1 << 20 };
        for (j = 0; j < sizeof sizes / sizeof *sizes; j++) {
            double l = speed(vc_hash_loop, buf, sizes[j]), s = speed(nv2a_hash_sse2, buf, sizes[j]);
            if (avx2) {
                double v = speed(avx2_or_sse2, buf, sizes[j]);
                printf("  %7zu bytes: vc_hash's loop %.1f GB/s, SSE2 %.1f (x%.2f), AVX2 %.1f (x%.2f)\n",
                       sizes[j], l, s, s / l, v, v / l);
            } else {
                printf("  %7zu bytes: vc_hash's loop %.1f GB/s, SSE2 %.1f (x%.2f)\n", sizes[j], l, s, s / l);
            }
        }
    }

    free(buf);
    free(copy);
    if (fails) {
        printf("vc_hash: %d FAILED\n", fails);
        return 1;
    }
    printf("vc_hash: all cases pass\n");
    return 0;
}
