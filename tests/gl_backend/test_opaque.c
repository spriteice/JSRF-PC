/* Run with: python tests/gl_backend/run.py
 *
 * A target sampled with alpha 1 (texture_opaque in
 * src/nv2a_gl/nv2a_gfx_gl.c), driven through the device interface the way
 * the front half drives it, on a headless EGL context (context.h). The
 * front half asks for it when the title samples a target as a format with
 * no alpha bits, as the boost dash samples its R5G6B5 copy: the console
 * reads alpha 1 there, whatever the draws into the target wrote. A target
 * is drawn with alpha 0.25 and sampled into another, opaque and then as it
 * is, from the target and from a snapshot of it: alpha 1 only when opaque,
 * the colour the same either way, and the target itself unchanged.
 */
#include "context.h"
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"

#define W 16
#define H 16

static const Nv2aGfx *G;
static uint32_t src, dst, fill, copy;
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

/* The whole of target `rt`, drawn by `prog` with blending off, sampling
 * `tex` on stage 0 if it is not 0. */
static void cover(uint32_t rt, uint32_t prog, uint32_t tex, int opaque)
{
    static const float v[6][8] = {
        { -1, -1, 0, 1,  0, 0, 0, 1 }, { 1, -1, 0, 1,  1, 0, 0, 1 },
        {  1,  1, 0, 1,  1, 1, 0, 1 },
        { -1, -1, 0, 1,  0, 0, 0, 1 }, { 1,  1, 0, 1,  1, 1, 0, 1 },
        { -1,  1, 0, 1,  0, 1, 0, 1 } };
    Nv2aRenderState rs;
    memset(&rs, 0, sizeof rs);
    rs.stencil_func = 0x207;
    rs.stencil_rmask = rs.stencil_wmask = 0xFF;
    rs.stencil_op[0] = rs.stencil_op[1] = rs.stencil_op[2] = 0x1E00;   /* KEEP */
    G->target_bind(rt);
    G->prog_use(prog);
    G->texture(0, tex);
    if (tex) {
        G->texture_opaque(0, opaque);
        G->uniform_i(NV2A_U_TEX0, 0);
    }
    G->state(&rs);
    G->vertices(&v[0][0], 6, 8, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, 6);
}

/* Pixel (8, 8) of `rt`, RGBA. */
static const uint8_t *middle(uint32_t rt)
{
    static uint8_t px[W * H * 4];
    check(G->target_read(rt, 0, 0, W, H, 0, px), "target_read");
    return px + (8 * W + 8) * 4;
}

static int near(const uint8_t *p, int r, int g, int b, int a)
{
    return abs(p[0] - r) <= 2 && abs(p[1] - g) <= 2 && abs(p[2] - b) <= 2
        && abs(p[3] - a) <= 2;
}

int main(void)
{
    const uint8_t *p;

    if (!gl_backend_context()) return 1;
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    check(G->texture_opaque != NULL, "texture_opaque");
    fill = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\n"
        "void main() { gl_Position = v0; }\n",
        NV2A_GLSL_VERSION "out vec4 frag;\n"
        "void main() { frag = vec4(0.2, 0.4, 0.6, 0.25); }\n", "fill");
    copy = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\nlayout(location = 1) in vec4 v1;\n"
        "out vec2 uv;\nvoid main() { gl_Position = v0; uv = v1.xy; }\n",
        NV2A_GLSL_VERSION "uniform sampler2D tex0;\nin vec2 uv;\nout vec4 frag;\n"
        "void main() { frag = texture(tex0, uv); }\n", "copy");
    check(fill != 0 && copy != 0, "prog_new");
    src = G->target_new(W, H);
    dst = G->target_new(W, H);
    check(src != 0 && dst != 0, "target_new");

    /* The target, as a draw into it leaves it: alpha 0.25. */
    cover(src, fill, 0, 0);
    p = middle(src);
    printf("drawn: %d %d %d %d\n", p[0], p[1], p[2], p[3]);
    check(near(p, 51, 102, 153, 64), "the target holds alpha 0.25");

    /* Sampled opaque, then as it is: the setting is the texture's, so each
     * draw that samples it sets it, and the last one set is what is read. */
    cover(dst, copy, G->target_texture(src), 1);
    p = middle(dst);
    printf("sampled opaque: %d %d %d %d\n", p[0], p[1], p[2], p[3]);
    check(near(p, 51, 102, 153, 255), "sampled opaque: alpha 1, the same colour");
    cover(dst, copy, G->target_texture(src), 0);
    p = middle(dst);
    printf("sampled as it is: %d %d %d %d\n", p[0], p[1], p[2], p[3]);
    check(near(p, 51, 102, 153, 64), "sampled as it is: alpha 0.25");

    /* A snapshot, which is what a draw into the target samples. */
    G->target_bind(src);
    cover(dst, copy, G->target_snapshot(src), 1);
    p = middle(dst);
    check(near(p, 51, 102, 153, 255), "a snapshot sampled opaque: alpha 1");
    G->target_bind(src);
    cover(dst, copy, G->target_snapshot(src), 0);
    p = middle(dst);
    check(near(p, 51, 102, 153, 64), "a snapshot sampled as it is: alpha 0.25");

    /* Only the sampling changes: what the target holds stays. */
    p = middle(src);
    check(near(p, 51, 102, 153, 64), "the target still holds alpha 0.25");

    G->prog_free(fill);
    G->prog_free(copy);
    G->target_free(src);
    G->target_free(dst);
    check(1, "target_free");
    printf("gl_backend: targets sampled opaque pass\n");
    return 0;
}
