/* Run with: python tests/gl_backend/run.py
 *
 * The combiner constants as uniforms (src/nv2a_gl/nv2a_psh_consts.h). A
 * combiner state's shader, GLSL and Metal, is the same text whatever its
 * constants and alpha reference; its key keeps every other byte of the
 * state; and one program, built once, draws each set of constants it is
 * given: every stage's C0 and C1 and the final combiner's pair, each read
 * from where it sits in pshC[], on a headless EGL context (context.h).
 */
#include "context.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"
#include "nv2a_psh.h"
#include "nv2a_psh_consts.h"

#define W        16
#define SRC_MAX  (96 * 1024)   /* what the front half gives the emitters */
#define DECOY    0x7F7F7F7Fu   /* every constant the shader shouldn't read */

static const Nv2aGfx *G;
static char src_a[SRC_MAX], src_b[SRC_MAX];
static const int attr_at[16] = { 0, -1, -1, -1, -1, -1, -1, -1,
                                 -1, -1, -1, -1, -1, -1, -1, -1 };
static const float no_const[16][4];

static void check(int ok, const char *what)
{
    GLenum e = glGetError();
    if (!ok || e != GL_NO_ERROR) {
        fprintf(stderr, "FAIL %s%s (GL error 0x%x)\n", what, ok ? "" : ": wrong", e);
        exit(1);
    }
}

/* Two stages, each with a C0 and C1 of its own (COMBINER_CONTROL bits 12
 * and 16). Stage 0 puts its C0 in R0, stage 1 adds its C1 to R0, and the
 * final combiner adds its own C0 (as E times one) and takes its alpha from
 * its own C1: the colour is stage 0's C0 + stage 1's C1 + the final C0, the
 * alpha the final C1's. */
static void combiners(Nv2aPshState *ps)
{
    memset(ps, 0, sizeof *ps);
    ps->control    = 2u | (1u << 12) | (1u << 16);
    ps->rgb_icw[0] = 0x01200000u;   /* A = C0, B = one (zero, inverted) */
    ps->rgb_ocw[0] = 0x0C0u;        /* AB -> R0 */
    ps->rgb_icw[1] = 0x0C200220u;   /* A = R0, B = one, C = C1, D = one */
    ps->rgb_ocw[1] = 0xC00u;        /* AB + CD -> R0 */
    ps->final_abcd = 0x00000C0Fu;   /* A = zero, B = zero, C = R0, D = E*F */
    ps->final_efg  = 0x01201200u;   /* E = C0, F = one, G = C1's alpha */
}

/* The four constants the combiners read, A8R8G8B8, and every other one a
 * decoy, so that a constant read from the wrong place shows. */
static void constants(Nv2aPshState *ps, uint32_t c0_0, uint32_t c1_1, uint32_t fc0, uint32_t fc1)
{
    int i;
    for (i = 0; i < NV2A_PSH_STAGES; i++) ps->c0[i] = ps->c1[i] = DECOY;
    ps->c0[0] = c0_0;
    ps->c1[1] = c1_1;
    ps->final_c0 = fc0;
    ps->final_c1 = fc1;
}

/* The same source for both states, in both dialects, reading pshC[] in its
 * body. */
static void same_source(const Nv2aPshState *a, const Nv2aPshState *b, const char *what)
{
    char decl[64], msg[160];
    int msl;
    snprintf(decl, sizeof decl, "uniform vec4 pshC[%d];", NV2A_PSH_NCONSTS);
    for (msl = 0; msl < 2; msl++) {
        int na = msl ? nv2a_psh_emit_msl(a, src_a, SRC_MAX) : nv2a_psh_emit_glsl(a, src_a, SRC_MAX);
        int nb = msl ? nv2a_psh_emit_msl(b, src_b, SRC_MAX) : nv2a_psh_emit_glsl(b, src_b, SRC_MAX);
        const char *body = strstr(src_a, msl ? "// nv2a body" : "void main()");
        snprintf(msg, sizeof msg, "%s, %s: emitted", what, msl ? "Metal" : "GLSL");
        check(na > 0 && nb > 0, msg);
        snprintf(msg, sizeof msg, "%s, %s: the same source whatever the constants", what,
                 msl ? "Metal" : "GLSL");
        check(na == nb && !strcmp(src_a, src_b), msg);
        /* The Metal backend finds the uniforms a program reads by name in its
         * body (uniforms_used), so the body must name pshC itself. */
        snprintf(msg, sizeof msg, "%s, %s: pshC read in the body", what, msl ? "Metal" : "GLSL");
        check(body && strstr(body, "pshC[") != NULL, msg);
        if (!msl) check(strstr(src_a, decl) != NULL, "GLSL: pshC declared");
    }
}

/* Whether byte `at` of Nv2aPshState belongs to one of the fields the shader
 * reads as a uniform. */
static int in_uniform(size_t at)
{
    static const struct { size_t off, len; } f[] = {
        { offsetof(Nv2aPshState, c0), sizeof(((Nv2aPshState *)0)->c0) },
        { offsetof(Nv2aPshState, c1), sizeof(((Nv2aPshState *)0)->c1) },
        { offsetof(Nv2aPshState, final_c0), sizeof(uint32_t) },
        { offsetof(Nv2aPshState, final_c1), sizeof(uint32_t) },
        { offsetof(Nv2aPshState, alpha_ref), sizeof(float) } };
    size_t i;
    for (i = 0; i < sizeof f / sizeof f[0]; i++)
        if (at >= f[i].off && at < f[i].off + f[i].len) return 1;
    return 0;
}

/* The whole of target `rt`, drawn by `prog` with these constants. */
static void cover(uint32_t rt, uint32_t prog, const Nv2aPshState *ps)
{
    static const float v[6][4] = { { -1, -1, 0, 1 }, { 1, -1, 0, 1 }, { 1, 1, 0, 1 },
                                   { -1, -1, 0, 1 }, { 1, 1, 0, 1 }, { -1, 1, 0, 1 } };
    uint32_t w[NV2A_PSH_NCONSTS];
    float c[NV2A_PSH_NCONSTS * 4];
    Nv2aRenderState rs;
    memset(&rs, 0, sizeof rs);
    rs.stencil_func = 0x207;
    rs.stencil_rmask = rs.stencil_wmask = 0xFF;
    rs.stencil_op[0] = rs.stencil_op[1] = rs.stencil_op[2] = 0x1E00;   /* KEEP */
    nv2a_psh_const_words(w, ps);
    nv2a_psh_const_values(c, w);
    G->target_bind(rt);
    G->prog_use(prog);
    G->uniform_i(NV2A_U_ALPHAFUNC, 0);
    G->uniform_f(NV2A_U_PSHC, c, NV2A_PSH_NCONSTS);
    G->state(&rs);
    G->vertices(&v[0][0], 6, 4, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, 6);
}

/* Pixel (8, 8) of `rt`, RGBA. */
static const uint8_t *middle(uint32_t rt)
{
    static uint8_t px[W * W * 4];
    check(G->target_read(rt, 0, 0, W, W, 0, px), "target_read");
    return px + (8 * W + 8) * 4;
}

static int near(const uint8_t *p, int r, int g, int b, int a)
{
    return abs(p[0] - r) <= 1 && abs(p[1] - g) <= 1 && abs(p[2] - b) <= 1
        && abs(p[3] - a) <= 1;
}

int main(void)
{
    Nv2aPshState a, b;
    const uint8_t *p;
    uint32_t prog, rt;
    size_t at;
    int k;

    if (!gl_backend_context()) return 1;

    /* The source: the same text whatever the constants and the alpha
     * reference, with each stage's constants and with stage 0's shared, and
     * with a texture and the W-buffer, which add declarations of their own. */
    for (k = 0; k < 3; k++) {
        static const char *what[3] = { "constants per stage", "constants shared",
                                       "a texture and the W-buffer" };
        combiners(&a);
        if (k == 1) a.control &= ~((1u << 12) | (1u << 16));
        if (k == 2) { a.tex_bound[0] = 1; a.tex_mode[0] = 1; a.w_buffer = 1; }
        b = a;
        constants(&a, 0x00200000u, 0x00004000u, 0x00000060u, 0x80000000u);
        a.alpha_ref = 0.25f;
        constants(&b, 0x00500000u, 0x00003000u, 0x00000010u, 0xC0000000u);
        b.alpha_ref = 0.75f;
        same_source(&a, &b, what[k]);
    }

    /* The key: every byte of the state but the uniforms'. One byte at a time
     * changed, the padding included: the key follows it unless it is one of
     * theirs. */
    combiners(&a);
    constants(&a, 0x00200000u, 0x00004000u, 0x00000060u, 0x80000000u);
    for (at = 0; at < sizeof a; at++) {
        Nv2aPshState ka, kb;
        char msg[96];
        memcpy(&b, &a, sizeof b);
        ((unsigned char *)&b)[at] ^= 0x5A;
        nv2a_psh_key(&ka, &a);
        nv2a_psh_key(&kb, &b);
        snprintf(msg, sizeof msg, "the key, with byte %u of the state changed", (unsigned)at);
        check((memcmp(&ka, &kb, sizeof ka) == 0) == in_uniform(at), msg);
    }
    printf("source and key: the same whatever the constants, every other byte kept\n");

    /* One program, two sets of constants. */
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    combiners(&a);
    check(nv2a_psh_emit_glsl(&a, src_a, SRC_MAX) > 0, "emitted for drawing");
    prog = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\n"
        "out vec4 oD0;\nout vec4 oD1;\nout vec4 oT0;\nout vec4 oT1;\n"
        "out vec4 oT2;\nout vec4 oT3;\nout float oFogC;\n"
        "void main() { gl_Position = v0; oD0 = vec4(0.0); oD1 = vec4(0.0);\n"
        "  oT0 = vec4(0.0); oT1 = vec4(0.0); oT2 = vec4(0.0); oT3 = vec4(0.0);\n"
        "  oFogC = 0.0; }\n", src_a, "combiners");
    check(prog != 0, "prog_new");
    check(G->prog_uses(prog, NV2A_U_PSHC), "the program reads pshC");
    rt = G->target_new(W, W);
    check(rt != 0, "target_new");

    constants(&a, 0x00200000u, 0x00004000u, 0x00000060u, 0x80000000u);
    cover(rt, prog, &a);
    p = middle(rt);
    printf("first constants: %d %d %d %d\n", p[0], p[1], p[2], p[3]);
    check(near(p, 32, 64, 96, 128), "first constants: stage 0's C0 red, stage 1's C1 green, "
                                    "the final C0 blue, the final C1 alpha");
    constants(&a, 0x00500000u, 0x00003000u, 0x00000010u, 0xC0000000u);
    cover(rt, prog, &a);
    p = middle(rt);
    printf("second constants, the same program: %d %d %d %d\n", p[0], p[1], p[2], p[3]);
    check(near(p, 80, 48, 16, 192), "second constants, the same program");

    G->prog_free(prog);
    G->target_free(rt);
    check(1, "target_free");
    printf("gl_backend: combiner constants as uniforms pass\n");
    return 0;
}
