/* recomp_rcpss and recomp_rsqrtss (templates/runtime/recomp_types.h) give
 * Intel's answers, which are the Xbox's Pentium III's, on any processor
 * (known bug 60: AMD's own instructions answer differently, and the ending's
 * recorded skater went off course on the Ally X).
 *
 * Intel's answers were read from an Intel processor's own rcpss and rsqrtss
 * (a Xeon, 2026-11-16) and are held here: some inputs one by one, the
 * special cases among them, and two checksums, one over every input of
 * exponents 127 and 128 (every range of both instructions, each input in
 * it), and one over every 4,099th input of all 2^32 (every exponent).
 * The functions were also checked against that processor for all 2^32
 * inputs, with no difference (credits-sync/part7 in the shared folder).
 *
 * Nothing here runs the host's own instructions, so it fails on any
 * processor if the answers stop being Intel's: with the host's instructions
 * back in the runtime, it fails on an AMD processor and passes on an Intel
 * one, where those are Intel's answers. */
#include <stdio.h>
#include "recomp_types.h"

static const struct { uint32_t in, rcp, rsqrt; } cases[] = {
    { 0x00000000, 0x7F800000, 0x7F800000 },
    { 0x80000000, 0xFF800000, 0xFF800000 },
    { 0x00000001, 0x7F800000, 0x7F800000 },
    { 0x807FFFFF, 0xFF800000, 0xFF800000 },
    { 0x00800000, 0x7E7FF000, 0x5EFFF000 },
    { 0x80800000, 0xFE7FF000, 0xFFC00000 },
    { 0x3F800000, 0x3F7FF000, 0x3F7FF000 },
    { 0x40000000, 0x3EFFF000, 0x3F34F800 },
    { 0x40400000, 0x3EAAA000, 0x3F13C800 },
    { 0x3F000000, 0x3FFFF000, 0x3FB4F800 },
    { 0x3FC00000, 0x3F2AA000, 0x3F510000 },
    { 0x40490FDB, 0x3EA30000, 0x3F107000 },
    { 0x42FA004A, 0x3C031000, 0x3DB72800 },
    { 0x42F9FFAA, 0x3C031800, 0x3DB73000 },
    { 0x3F7FFFFF, 0x3F800800, 0x3F800800 },
    { 0x3FFFFFFF, 0x3F000800, 0x3F350800 },
    { 0x407FFFFF, 0x3E800800, 0x3F000800 },
    { 0x00FFFFFF, 0x7E000800, 0x5EB50800 },
    { 0x7E800000, 0x00000000, 0x1FFFF000 },
    { 0x7E7FFFFF, 0x00800800, 0x20000800 },
    { 0x7F000000, 0x00000000, 0x1FB4F800 },
    { 0x7F7FFFFF, 0x00000000, 0x1F800800 },
    { 0xFF7FFFFF, 0x80000000, 0xFFC00000 },
    { 0x7F800000, 0x00000000, 0x00000000 },
    { 0xFF800000, 0x80000000, 0xFFC00000 },
    { 0x7FC00000, 0x7FC00000, 0x7FC00000 },
    { 0x7F800001, 0x7FC00001, 0x7FC00001 },
    { 0xFFC00001, 0xFFC00001, 0xFFC00001 },
    { 0xBF800000, 0xBF7FF000, 0xFFC00000 },
    { 0xC0800000, 0xBE7FF000, 0xFFC00000 },
    { 0x3DCCCCCD, 0x41200000, 0x404A6000 },
    { 0x447A0000, 0x3A831000, 0x3D018000 },
};

#define FNV_START 2166136261u
static uint32_t fnv(uint32_t h, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++) { h ^= (v >> (8 * i)) & 0xFFu; h *= 16777619u; }
    return h;
}
static uint32_t rcp_bits(uint32_t x) { return recomp_f32_bits(recomp_rcpss(recomp_f32_from_bits(x))); }
static uint32_t rsqrt_bits(uint32_t x) { return recomp_f32_bits(recomp_rsqrtss(recomp_f32_from_bits(x))); }

static int check(const char *what, uint32_t got, uint32_t want)
{
    if (got == want) return 0;
    printf("sse_approx: %s: %08X, Intel's is %08X\n", what, got, want);
    return 1;
}

int main(void)
{
    char what[64];
    unsigned i, failed = 0;
    uint32_t e, m, x, rc = FNV_START, rs = FNV_START, sc = FNV_START, ss = FNV_START;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        snprintf(what, sizeof what, "rcpss %08X", cases[i].in);
        failed += check(what, rcp_bits(cases[i].in), cases[i].rcp);
        snprintf(what, sizeof what, "rsqrtss %08X", cases[i].in);
        failed += check(what, rsqrt_bits(cases[i].in), cases[i].rsqrt);
    }
    for (e = 127; e <= 128; e++)
        for (m = 0; m < 0x800000u; m++) {
            rc = fnv(rc, rcp_bits(e << 23 | m));
            rs = fnv(rs, rsqrt_bits(e << 23 | m));
        }
    failed += check("rcpss, checksum of every range", rc, 0x2777FDC5u);
    failed += check("rsqrtss, checksum of every range", rs, 0x75641DC5u);
    for (x = 0;; x += 4099u) {
        sc = fnv(sc, rcp_bits(x));
        ss = fnv(ss, rsqrt_bits(x));
        if (x > 0xFFFFFFFFu - 4099u) break;
    }
    failed += check("rcpss, checksum of every 4,099th input", sc, 0x912FEF7Cu);
    failed += check("rsqrtss, checksum of every 4,099th input", ss, 0x82664EA5u);

    if (failed) {
        printf("sse_approx: %u failed\n", failed);
        return 1;
    }
    printf("sse_approx: all cases pass\n");
    return 0;
}
