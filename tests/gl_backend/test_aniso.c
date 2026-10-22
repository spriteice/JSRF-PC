/* Run with: python tests/gl_backend/run.py
 *
 * Anisotropic filtering in the OpenGL backend (aniso_max, tex_aniso and
 * tex_new in src/nv2a_gl/nv2a_gfx_gl.c), driven through the device interface
 * the way the front half drives it, on a headless EGL context (context.h).
 * The level is read back from each texture, which is what the backend sets;
 * the picture is only checked to change and to change back, since how a
 * driver spends its samples is its own business. The texture is 256x256 of
 * one-texel stripes across v, drawn on a plane leaning away from the eye, so
 * the far end squeezes many stripes into a pixel along v and few along u.
 */
#include "context.h"
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"

#define W 64
#define H 64
#define TW 256

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

/* The level a texture is sampled with, from the texture itself, through a
 * unit neither the combiner stages (0..3) nor the backend's scratch use. */
static int level(uint32_t tex)
{
    GLfloat a = 0;
    glActiveTexture(GL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexParameterfv(GL_TEXTURE_2D, 0x84FE, &a);   /* TEXTURE_MAX_ANISOTROPY */
    glBindTexture(GL_TEXTURE_2D, 0);
    return (int)a;
}

/* Whether a texture has a level 1, and what its GL_TEXTURE_MAX_LEVEL is. */
static int has_level1(uint32_t tex, int *max_level)
{
    GLint w = 0, ml = 0;
    glActiveTexture(GL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &w);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &ml);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (max_level) *max_level = (int)ml;
    return w > 0;
}

static int bound(int unit)
{
    GLint t = 0;
    glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &t);
    return (int)t;
}

/* The plane, its near edge along the bottom and its far edge along the top
 * eight times as far away, sampling `tex` on stage 0. */
static void plane(uint32_t tex, uint8_t *px)
{
    static const float black[4] = { 0, 0, 0, 1 };
    static const float v[6][8] = {
        { -1, -1, 0, 1,  0.00f, 0, 0, 1 }, { 1, -1, 0, 1,  0.25f, 0, 0, 1 },
        {  8,  8, 0, 8,  0.25f, 1, 0, 1 },
        { -1, -1, 0, 1,  0.00f, 0, 0, 1 }, { 8,  8, 0, 8,  0.25f, 1, 0, 1 },
        { -8,  8, 0, 8,  0.00f, 1, 0, 1 } };
    Nv2aRenderState rs;
    memset(&rs, 0, sizeof rs);
    rs.stencil_func = 0x207;
    rs.stencil_rmask = rs.stencil_wmask = 0xFF;
    rs.stencil_op[0] = rs.stencil_op[1] = rs.stencil_op[2] = 0x1E00;   /* KEEP */
    G->target_bind(rt);
    G->clear(7, black, 1.0f, 0, NULL);
    G->prog_use(prog);
    G->texture(0, tex);
    G->uniform_i(NV2A_U_TEX0, 0);
    G->state(&rs);
    G->vertices(&v[0][0], 6, 8, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, 6);
    check(G->target_read(rt, 0, 0, W, H, 0, px), "target_read");
}

static int differ(const uint8_t *a, const uint8_t *b)
{
    int i, n = 0;
    for (i = 0; i < W * H; i++) n += abs(a[i * 4] - b[i * 4]) > 8;
    return n;
}

int main(void)
{
    static uint8_t stripes[TW * TW * 4], one[W * H * 4], many[W * H * 4], again[W * H * 4];
    Nv2aSampler lin = { 1, 1, 0, 1, 1, 1 }, point = { 0, 0, 0, 1, 1, 1 };
    uint32_t t, p, m, other;
    int max, hi, i;
    char msg[96];

    if (!gl_backend_context()) return 1;
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    max = G->aniso_max();
    hi = max >= 16 ? 16 : max;
    printf("up to %dx here; testing %dx\n", max, hi);
    check(hi >= 2, "the device filters anisotropically");
    check(G->aniso_max() == max, "the most is asked once and kept");

    for (i = 0; i < TW * TW; i++) {
        uint8_t c = ((i / TW) & 1) ? 255 : 0;
        stripes[i * 4 + 0] = stripes[i * 4 + 1] = stripes[i * 4 + 2] = c;
        stripes[i * 4 + 3] = 255;
    }
    prog = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\nlayout(location = 1) in vec4 v1;\n"
        "out vec2 uv;\nvoid main() { gl_Position = v0; uv = v1.xy; }\n",
        NV2A_GLSL_VERSION "uniform sampler2D tex0;\nin vec2 uv;\nout vec4 frag;\n"
        "void main() { frag = texture(tex0, uv); }\n", "test");
    check(prog != 0, "prog_new");
    rt = G->target_new(W, H);
    check(rt != 0, "target_new");

    /* Made with the level the front half asks for: none, some, more than
     * the device has; and, as the front half asks, none on a point-sampled
     * texture whatever the setting. */
    t = G->tex_new(TW, TW, stripes, &lin);
    check(level(t) == 1, "linear, made at 1x: 1x");
    check(!has_level1(t, NULL), "made at 1x: one level, as before");
    lin.aniso = 8;
    m = G->tex_new(TW, TW, stripes, &lin);
    check(level(m) == (max < 8 ? max : 8), "linear, made at 8x: 8x");
    /* A texture whose base level is its last is not filtered
     * anisotropically on radeonsi (ANISO_OVERRIDE): it gets a second. */
    check(has_level1(m, &i) && i == 1, "made at 8x: a second level, the last");
    lin.mip = 2;
    lin.aniso = 4;
    G->tex_free(m);
    m = G->tex_new(TW, TW, stripes, &lin);
    check(level(m) == 4, "with a chain, made at 4x: 4x");
    check(has_level1(m, &i) && i > 1, "with a chain: its own levels, all of them");
    p = G->tex_new(TW, TW, stripes, &point);
    check(level(p) == 1, "point-sampled: 1x");

    /* Changed in place, every way, clamped to what the device has. */
    G->tex_aniso(t, hi);
    snprintf(msg, sizeof msg, "tex_aniso(%d): %dx", hi, hi);
    check(level(t) == hi, msg);
    check(has_level1(t, &i) && i == 1, "switched on: a second level, the last");
    G->tex_aniso(t, 1);
    check(level(t) == 1, "tex_aniso(1): 1x");
    G->tex_aniso(t, max * 4);
    check(level(t) == max, "a level past the most is the most");
    G->tex_aniso(t, 0);
    check(level(t) == 1, "a level under one is none");

    /* Without moving what the combiner stages have bound. */
    other = G->tex_new(TW, TW, stripes, &point);
    G->texture(0, t);
    G->texture(1, p);
    G->tex_aniso(other, hi);
    check(bound(0) == (int)t && bound(1) == (int)p, "the stages keep their textures");

    /* And it reaches the sampling: the far end changes at the new level, and
     * going back gives the first picture exactly -- drawn from a texture of
     * one level, so the second is never sampled. */
    G->tex_free(t);
    lin.mip = 0;
    lin.aniso = 1;
    t = G->tex_new(TW, TW, stripes, &lin);
    plane(t, one);
    G->tex_aniso(t, hi);
    plane(t, many);
    G->tex_aniso(t, 1);
    plane(t, again);
    printf("1x to %dx: %d of %d pixels change\n", hi, differ(one, many), W * H);
    check(differ(one, many) > 0, "the picture changes at the new level");
    check(has_level1(t, NULL), "drawn at the new level with a second");
    check(!memcmp(one, again, sizeof one), "back at 1x, the picture is the first one");

    G->tex_free(t);
    G->tex_free(m);
    G->tex_free(p);
    G->tex_free(other);
    G->target_free(rt);
    check(1, "tex_free");
    printf("gl_backend: anisotropic filtering passes\n");
    return 0;
}
