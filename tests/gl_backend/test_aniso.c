/* Run with: python tests/gl_backend/run.py
 *
 * Anisotropic filtering in the OpenGL backend (aniso_max, and tex_new in
 * src/nv2a_gl/nv2a_gfx_gl.c), driven through the device interface the way
 * the front half drives it, on a headless EGL context (context.h): a texture
 * with mipmaps is made with the level the front half asks for (the in-game
 * menu's, or the title's own if more), one without them with none. The
 * level is read back from each texture, which is what the backend sets; the
 * picture is only checked to change, since how a driver spends its samples
 * is its own business. The texture is 256x256 of one-texel stripes across v,
 * drawn on a plane leaning away from the eye, so the far end squeezes many
 * stripes into a pixel along v and few along u.
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
    static uint8_t stripes[TW * TW * 4], one[W * H * 4], many[W * H * 4];
    Nv2aSampler lin = { 1, 1, 0, 1, 1, 1 }, point = { 0, 0, 0, 1, 1, 1 };
    uint32_t t, p, m;
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

    /* Made with the level the front half asks for: with mipmaps, none, some
     * and more than the device has; without them, none whatever is asked. */
    lin.mip = 2;
    t = G->tex_new(TW, TW, stripes, &lin);
    check(level(t) == 1, "with mipmaps, made at 1x: 1x");
    lin.aniso = (uint8_t)hi;
    m = G->tex_new(TW, TW, stripes, &lin);
    snprintf(msg, sizeof msg, "with mipmaps, made at %dx: %dx", hi, hi);
    check(level(m) == hi, msg);
    G->tex_free(m);
    lin.aniso = (uint8_t)(max < 64 ? max * 4 : 255);
    m = G->tex_new(TW, TW, stripes, &lin);
    check(level(m) == max, "a level past the most is the most");
    point.aniso = (uint8_t)hi;
    p = G->tex_new(TW, TW, stripes, &point);
    check(level(p) == 1, "without mipmaps: 1x");

    /* And it reaches the sampling: the far end changes at the level. */
    plane(t, one);
    plane(m, many);
    printf("1x to %dx: %d of %d pixels change\n", max, differ(one, many), W * H);
    check(differ(one, many) > 0, "the picture changes at the level");

    G->tex_free(t);
    G->tex_free(m);
    G->tex_free(p);
    G->target_free(rt);
    check(1, "tex_free");
    printf("gl_backend: anisotropic filtering passes\n");
    return 0;
}
