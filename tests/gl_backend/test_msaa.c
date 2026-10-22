/* Run with: python tests/gl_backend/run.py
 *
 * Multisampling in the OpenGL backend (samples() in src/nv2a_gl/nv2a_gfx_gl.c),
 * driven through the device interface the way the front half drives it, in
 * the context every test here runs in (context.h). One 64x64 target holds a
 * white triangle with two slanted edges at depth 0.5, stencil 1, over a black
 * clear at depth 1, stencil 0.
 */
#include <stdlib.h>
#include <string.h>
#include "context.h"
#include "nv2a_backend.h"

unsigned nv2a_gfx_gl_fbo(uint32_t rt);  /* the window's: nv2a_gfx_gl.c */

#define W 64
#define H 64

static const Nv2aGfx *G;
static uint32_t rt, prog;
static int now;                    /* samples a pixel in use */
static const int attr_at[16] = { 0, -1, -1, -1, -1, -1, -1, -1,
                                 -1, -1, -1, -1, -1, -1, -1, -1 };
static const float no_const[16][4];

/* Clip space, z 0 (depth 0.5). (16, 16) is inside it, (56, 56) outside. */
static const float tri[3][4] = {
    { -0.8f, -0.8f, 0, 1 }, { 0.8f, -0.6f, 0, 1 }, { -0.6f, 0.8f, 0, 1 } };

static void check(int ok, const char *what)
{
    GLenum e = glGetError();
    if (!ok || e != GL_NO_ERROR) {
        fprintf(stderr, "FAIL %s%s (GL error 0x%x)\n", what, ok ? "" : ": wrong", e);
        exit(1);
    }
}

/* Everything off but what the caller sets. */
static Nv2aRenderState plain(void)
{
    Nv2aRenderState rs;
    memset(&rs, 0, sizeof rs);
    rs.stencil_func = 0x207;
    rs.stencil_rmask = rs.stencil_wmask = 0xFF;
    rs.stencil_op[0] = rs.stencil_op[1] = rs.stencil_op[2] = 0x1E00;   /* KEEP */
    return rs;
}

static void draw(const float (*v)[4], uint32_t n, const Nv2aRenderState *rs,
                 float r, float g, float b)
{
    const float tint[4] = { r, g, b, 1 };
    G->prog_use(prog);
    G->uniform_f(NV2A_U_DRAWTINT, tint, 1);
    G->state(rs);
    G->vertices(&v[0][0], n, 4, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, n);
}

/* Black at depth 1, stencil 0, then the white triangle at depth 0.5 writing
 * stencil 1. */
static void scene(void)
{
    static const float black[4] = { 0, 0, 0, 1 };
    Nv2aRenderState rs = plain();
    G->clear(7, black, 1.0f, 0, NULL);
    rs.depth_test = rs.depth_write = 1;
    rs.depth_func = 0x201;                                         /* LESS */
    rs.stencil_test = 1;
    rs.stencil_ref = 1;
    rs.stencil_op[2] = 0x1E01;                                     /* REPLACE */
    draw(tri, 3, &rs, 1, 1, 1);
}

static void quad(const Nv2aRenderState *rs, float z, float r, float g, float b)
{
    const float q[6][4] = { { -1, -1, z, 1 }, { 1, -1, z, 1 }, { 1, 1, z, 1 },
                            { -1, -1, z, 1 }, { 1, 1, z, 1 }, { -1, 1, z, 1 } };
    draw(q, 6, rs, r, g, b);
}

static void read_rt(uint8_t *px)
{
    check(G->target_read(rt, 0, 0, W, H, 0, px), "target_read");
}

static const uint8_t *at(const uint8_t *px, int x, int y) { return px + (y * W + x) * 4; }

/* Pixels whose red is neither 0 nor 255: an edge's coverage, which only
 * multisampling gives. */
static int partial(const uint8_t *px)
{
    int i, n = 0;
    for (i = 0; i < W * H; i++) n += px[i * 4] != 0 && px[i * 4] != 255;
    return n;
}

static int is(const uint8_t *p, int r, int g, int b)
{
    return p[0] == r && p[1] == g && p[2] == b;
}

/* The count changed with the picture, depth and stencil carried across: the
 * picture reads back the same, a quad at depth 0.75 drawn LESS lands only
 * outside the triangle, and one drawn where the stencil is 1 only inside. */
static void carried(int n)
{
    static uint8_t before[W * H * 4], after[W * H * 4];
    Nv2aRenderState rs = plain();
    char what[32], msg[128];
    int got;
    snprintf(what, sizeof what, "%dx to %dx", now, n);
    scene();
    read_rt(before);
    got = G->samples(n);
    snprintf(msg, sizeof msg, "%s: samples(%d) gives %d", what, n, got);
    check(got == n, msg);
    now = n;
    read_rt(after);
    snprintf(msg, sizeof msg, "%s: the picture is carried", what);
    check(!memcmp(before, after, sizeof before), msg);

    rs.depth_test = 1;
    rs.depth_func = 0x201;
    quad(&rs, 0.5f, 0, 1, 0);
    read_rt(after);
    snprintf(msg, sizeof msg, "%s: the depth is carried", what);
    check(is(at(after, 16, 16), 255, 255, 255) && is(at(after, 56, 56), 0, 255, 0), msg);

    rs = plain();
    rs.stencil_test = 1;
    rs.stencil_func = 0x202;                                       /* EQUAL */
    rs.stencil_ref = 1;
    quad(&rs, 0, 0, 0, 1);
    read_rt(after);
    snprintf(msg, sizeof msg, "%s: the stencil is carried", what);
    check(is(at(after, 16, 16), 0, 0, 255) && is(at(after, 56, 56), 0, 255, 0), msg);
}

int main(void)
{
    static uint8_t px[W * H * 4], tx[W * H * 4];
    static float depth[W * H];
    Nv2aRenderState rs;
    uint32_t shown;
    int max, hi;

    if (!gl_backend_context()) return 1;
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    max = G->samples_max();
    hi = max >= 4 ? 4 : max;
    printf("up to %dx here; testing %dx\n", max, hi);
    check(hi >= 2, "the device multisamples");
    prog = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\n"
        "void main() { gl_Position = v0; }\n",
        NV2A_GLSL_VERSION "uniform vec4 drawTint;\nout vec4 frag;\n"
        "void main() { frag = drawTint; }\n", "test");
    check(prog != 0, "prog_new");

    /* At the start, before there is a target: the targets made after it are
     * multisampled. */
    check((now = G->samples(hi)) == hi, "samples() before any target");
    rt = G->target_new(W, H);
    check(rt != 0, "target_new");
    G->target_bind(rt);
    scene();
    read_rt(px);
    printf("%dx: %d edge pixels in between\n", hi, partial(px));
    check(partial(px) > 0, "edges are smoothed");
    check(is(at(px, 16, 16), 255, 255, 255) && is(at(px, 56, 56), 0, 0, 0), "the triangle is drawn");

    /* Its depth, and every way the front half samples it, see the latest
     * draw. */
    check(G->target_read_depth(rt, W, H, depth), "target_read_depth");
    check(depth[16 * W + 16] > 0.49f && depth[16 * W + 16] < 0.51f && depth[56 * W + 56] == 1.0f,
          "the depth reads back");
    rs = plain();
    quad(&rs, 0, 0, 1, 0);
    check(G->tex_read(G->target_texture(rt), W, H, tx), "target_texture");
    G->target_read(rt, 0, 0, W, H, 1, px);
    check(!memcmp(px, tx, sizeof px) && is(at(px, 16, 16), 0, 255, 0), "target_texture has the last draw");
    quad(&rs, 0, 0, 0, 1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, nv2a_gfx_gl_fbo(rt));
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    check(is(px, 0, 0, 255), "the window's framebuffer has the last draw");
    quad(&rs, 0, 1, 0, 0);
    check(G->tex_read(G->target_snapshot(rt), W, H, tx), "target_snapshot");
    G->target_read(rt, 0, 0, W, H, 1, px);
    /* (Blue, green, red: tex_read gives BGRA.) */
    check(!memcmp(px, tx, sizeof px) && is(at(px, 16, 16), 0, 0, 255), "target_snapshot has the last draw");

    /* A resolve between state() and the draw leaves the draw's scissor on. */
    scene();
    rs = plain();
    rs.scissor_enable = 1;
    rs.scissor[0] = 0; rs.scissor[1] = 0; rs.scissor[2] = W / 2; rs.scissor[3] = H;
    G->prog_use(prog);
    G->uniform_f(NV2A_U_DRAWTINT, (const float[4]){ 1, 0, 0, 1 }, 1);
    G->state(&rs);
    G->target_texture(rt);
    {
        const float q[6][4] = { { -1, -1, 0, 1 }, { 1, -1, 0, 1 }, { 1, 1, 0, 1 },
                                { -1, -1, 0, 1 }, { 1, 1, 0, 1 }, { -1, 1, 0, 1 } };
        G->vertices(&q[0][0], 6, 4, attr_at, 0, no_const);
        G->draw(NV2A_PRIM_TRIANGLES, 6);
    }
    read_rt(px);
    check(is(at(px, 8, 56), 255, 0, 0) && is(at(px, 56, 56), 0, 0, 0), "the scissor survives a resolve");

    /* Switched at run time, every way, the target keeping what it holds. */
    carried(2);
    carried(1);
    scene();
    read_rt(px);
    check(partial(px) == 0, "off: edges are hard");
    carried(hi);
    scene();
    read_rt(px);
    check(partial(px) > 0, "multisampled again: edges are smoothed");

    /* FXAA over it sees the latest draw, which only the samples hold until
     * something resolves them: here, the triangle over a red picture already
     * resolved. And again after a change of count, into the same target. */
    rs = plain();
    quad(&rs, 0, 1, 0, 0);
    read_rt(px);
    scene();
    shown = G->target_fxaa(rt);
    check(shown != 0 && shown != rt, "target_fxaa over a multisampled target");
    check(G->target_read(shown, 0, 0, W, H, 0, px), "target_read of FXAA's");
    check(is(at(px, 16, 16), 255, 255, 255) && is(at(px, 56, 56), 0, 0, 0) && partial(px) > 0,
          "FXAA has the multisampled picture");
    carried(2);
    quad(&rs, 0, 1, 0, 0);
    read_rt(px);
    scene();
    check(G->target_fxaa(rt) == shown, "FXAA keeps its target across a change of count");
    check(G->target_read(shown, 0, 0, W, H, 0, px), "target_read of FXAA's");
    check(is(at(px, 16, 16), 255, 255, 255) && is(at(px, 56, 56), 0, 0, 0) && partial(px) > 0,
          "FXAA has the picture after a change of count");

    /* More than the device has is as many as it has; fewer than one is none. */
    check(G->samples(max * 4) == max, "a count past the most is clamped");
    check(G->samples(0) == 1, "a count under one is none");

    G->target_free(rt);
    check(1, "target_free");
    G->report();
    printf("gl_backend: multisampling passes\n");
    return 0;
}
