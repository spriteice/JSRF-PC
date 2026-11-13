/* Run with: python tests/gl_backend/run.py
 *
 * One texture sampled two ways in one draw (texture and its samplers in
 * src/nv2a_gl/nv2a_gfx_gl.c), driven through the device interface the way
 * the front half drives it, on a headless EGL context (context.h). On the
 * console how a texture is sampled is each stage's, set for each draw, so a
 * title can bind one picture to two stages clamped on one and repeated on
 * the other, or filtered two ways, and get both. The garage's graffiti
 * picker samples its sheet two ways, which made the front half hold it, and
 * its graffiti copy, twice while a texture carried its own sampling.
 *
 * The texture is 2 x 1, red then green, and is made once. Bound to stage 0
 * clamped and stage 1 repeated, it is read at u = 1.25: clamped that is the
 * green texel, repeated the red one. Bound nearest on one and linear on the
 * other, at u = 0.45: the red texel, and a blend of the two. Then a target
 * bound without a sampling (NULL) is sampled as the target is, and the same
 * way of sampling asked for twice is one sampler object.
 */
#include "context.h"
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"

#define W 8
#define H 8

static const Nv2aGfx *G;
static uint32_t rt, prog;
static const int attr_at[16] = { 0, 4, -1, -1, -1, -1, -1, -1,
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

/* The whole target drawn at one coordinate (u, 0.5), with `tex` on stages 0
 * and 1 sampled as `s0` and `s1` say; pixel (4, 4) back, RGBA. */
static const uint8_t *draw_at(float u, uint32_t tex, const Nv2aSampler *s0,
                              const Nv2aSampler *s1)
{
    static uint8_t px[W * H * 4];
    static const float black[4] = { 0, 0, 0, 1 };
    float v[6][8];
    static const float corner[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 },
                                        { -1, -1 }, { 1, 1 }, { -1, 1 } };
    Nv2aRenderState rs;
    int i;
    for (i = 0; i < 6; i++) {
        v[i][0] = corner[i][0]; v[i][1] = corner[i][1]; v[i][2] = 0; v[i][3] = 1;
        v[i][4] = u; v[i][5] = 0.5f; v[i][6] = 0; v[i][7] = 1;
    }
    memset(&rs, 0, sizeof rs);
    rs.stencil_func = 0x207;
    rs.stencil_rmask = rs.stencil_wmask = 0xFF;
    rs.stencil_op[0] = rs.stencil_op[1] = rs.stencil_op[2] = 0x1E00;   /* KEEP */
    G->target_bind(rt);
    G->clear(7, black, 1.0f, 0, NULL);
    G->prog_use(prog);
    G->texture(0, tex, s0);
    G->texture(1, tex, s1);
    G->uniform_i(NV2A_U_TEX0, 0);
    G->uniform_i(NV2A_U_TEX0 + 1, 1);
    G->state(&rs);
    G->vertices(&v[0][0], 6, 8, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, 6);
    check(G->target_read(rt, 0, 0, W, H, 0, px), "target_read");
    return px + (4 * W + 4) * 4;
}

static GLint bound_sampler(int unit)
{
    GLint smp = -1;
    glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
    glGetIntegerv(GL_SAMPLER_BINDING, &smp);
    return smp;
}

int main(void)
{
    static const uint8_t red_green[2 * 4] = { 255, 0, 0, 255,  0, 255, 0, 255 };
    /* min_linear, mag_linear, mip, wrap_s, wrap_t, aniso, levels, above */
    Nv2aSampler clamped = { 0, 0, 0, 0, 0, 1, 0, 0 }, repeated = { 0, 0, 0, 1, 1, 1, 0, 0 },
                linear = { 1, 1, 0, 0, 0, 1, 0, 0 };
    const uint8_t *p;
    uint32_t t, other;
    GLint a, b;

    if (!gl_backend_context()) return 1;
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    /* Stage 0 into red and stage 1 into green, each the channel that is 1
     * only when that stage samples as it was asked; blue is stage 0's red. */
    prog = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\nlayout(location = 1) in vec4 v1;\n"
        "out vec2 uv;\nvoid main() { gl_Position = v0; uv = v1.xy; }\n",
        NV2A_GLSL_VERSION "uniform sampler2D tex0;\nuniform sampler2D tex1;\n"
        "in vec2 uv;\nout vec4 frag;\n"
        "void main() { vec4 a = texture(tex0, uv), b = texture(tex1, uv);\n"
        "              frag = vec4(a.g, b.r, a.r, 1.0); }\n", "two stages");
    check(prog != 0, "prog_new");
    rt = G->target_new(W, H);
    check(rt != 0, "target_new");
    t = G->tex_new(2, 1, red_green, &clamped);
    check(t != 0, "tex_new");

    /* Clamped on stage 0 (green at u = 1.25), repeated on stage 1 (red). */
    p = draw_at(1.25f, t, &clamped, &repeated);
    printf("clamped and repeated: %d %d %d\n", p[0], p[1], p[2]);
    check(p[0] > 250, "stage 0 samples clamped: the green texel");
    check(p[1] > 250, "stage 1 samples repeated: the red texel");
    check(p[2] < 5, "stage 0 is not sampled repeated");
    /* The other way round: each stage as it is bound now. */
    p = draw_at(1.25f, t, &repeated, &clamped);
    printf("repeated and clamped: %d %d %d\n", p[0], p[1], p[2]);
    check(p[0] < 5 && p[2] > 250, "stage 0 samples repeated: the red texel");
    check(p[1] < 5, "stage 1 samples clamped: not the red texel");

    /* Nearest on stage 0, linear on stage 1, at u = 0.45 (texel 0.9): the
     * red texel, and 0.6 red with 0.4 green. */
    p = draw_at(0.45f, t, &clamped, &linear);
    printf("nearest and linear: %d %d %d\n", p[0], p[1], p[2]);
    check(p[0] < 5 && p[2] > 250, "stage 0 samples nearest: the red texel");
    check(abs(p[1] - 153) <= 8, "stage 1 samples linear: 0.6 red");

    /* A target bound without a sampling: no sampler object on the stage, so
     * it is sampled as the target is made (linear and clamped). */
    other = G->target_new(W, H);
    check(other != 0, "target_new");
    G->texture(2, G->target_texture(other), NULL);
    check(bound_sampler(2) == 0, "a target is bound with no sampler object");
    G->texture(2, t, &clamped);
    check(bound_sampler(2) != 0, "a texture is bound with one");

    /* One sampler object for one way of sampling, however often asked. */
    G->texture(0, t, &repeated);
    G->texture(1, t, &repeated);
    a = bound_sampler(0);
    b = bound_sampler(1);
    check(a > 0 && a == b, "the same way of sampling is one sampler object");
    G->texture(1, t, &clamped);
    check(bound_sampler(1) != a, "another way is another");
    G->texture(1, t, &repeated);
    check(bound_sampler(1) == a, "and the first is kept");

    /* State set behind the backend (the menus' draws) is set again after
     * state_invalidate, the sampler with it. */
    glBindSampler(0, 0);
    G->state_invalidate();
    G->texture(0, t, &repeated);
    check(bound_sampler(0) == a, "bound again after state_invalidate");

    G->tex_free(t);
    G->target_free(other);
    G->target_free(rt);
    G->prog_free(prog);
    check(1, "tex_free");
    printf("gl_backend: one texture sampled two ways passes\n");
    return 0;
}
