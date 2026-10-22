/* Run with: python tests/gl_backend/run.py
 *
 * FXAA in the OpenGL backend (target_fxaa() in src/nv2a_gl/nv2a_gfx_gl.c),
 * driven through the device interface the way the front half drives it and
 * called between draws as it is at the flip, in the context every test here
 * runs in (context.h). A target is 64x64 unless said otherwise. The white
 * triangle drawn over black has hard edges; through FXAA they have pixels in
 * between, and the rest is as it was. The pass must also leave the backend
 * as the next draw expects it: that draw's program, target, viewport and
 * state.
 */
#include <stdlib.h>
#include <string.h>
#include "context.h"
#include "nv2a_backend.h"

#define W 64
#define H 64

static const Nv2aGfx *G;
static uint32_t prog;
static const int attr_at[16] = { 0, -1, -1, -1, -1, -1, -1, -1,
                                 -1, -1, -1, -1, -1, -1, -1, -1 };
static const float no_const[16][4];
static const float black[4] = { 0, 0, 0, 1 };

/* Clip space, z 0 (depth 0.5): a white triangle with two slanted edges.
 * (16, 16) is inside it, (56, 56) outside. */
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

/* The backend, current on this thread, with `prog`; or the test ends having
 * said why. */
static void gl_backend(void)
{
    if (!gl_backend_context()) exit(1);
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    prog = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\n"
        "void main() { gl_Position = v0; }\n",
        NV2A_GLSL_VERSION "uniform vec4 drawTint;\nout vec4 frag;\n"
        "void main() { frag = drawTint; }\n", "test");
    check(prog != 0, "prog_new");
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

/* The white triangle over black, drawn flat (without depth) into target t. */
static void flat_scene(uint32_t t)
{
    Nv2aRenderState rs = plain();
    G->target_bind(t);
    G->clear(7, black, 1.0f, 0, NULL);
    draw(tri, 3, &rs, 1, 1, 1);
}

/* Target t's picture, W wide and h high, RGBA from the bottom row up. */
static void read_of(uint32_t t, uint32_t h, uint8_t *px)
{
    check(G->target_read(t, 0, 0, W, h, 0, px), "target_read");
}

static const uint8_t *at(const uint8_t *px, int x, int y) { return px + (y * W + x) * 4; }

static int is(const uint8_t *p, int r, int g, int b)
{
    return p[0] == r && p[1] == g && p[2] == b;
}

/* Of n pixels, those whose red is neither 0 nor 255: in between, on an edge
 * of the white triangle over black -- which only anti-aliasing makes. */
static int partial(const uint8_t *px, int n)
{
    int i, k = 0;
    for (i = 0; i < n; i++) k += px[i * 4] != 0 && px[i * 4] != 255;
    return k;
}

#define H2 32                      /* a second, shorter target */

/* Of a picture h rows high, every pixel black or white with all eight around
 * it the same -- away from the edges -- is as it was. */
static int same_away_from_edges(const uint8_t *was, const uint8_t *now, int h)
{
    int x, y, dx, dy;
    for (y = 1; y < h - 1; y++)
        for (x = 1; x < W - 1; x++) {
            int v = at(was, x, y)[0], flat = v == 0 || v == 255;
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++)
                    flat = flat && !memcmp(at(was, x + dx, y + dy), at(was, x, y), 4);
            if (flat && memcmp(at(now, x, y), at(was, x, y), 4)) return 0;
        }
    return 1;
}

int main(void)
{
    static uint8_t px[W * H * 4], src[W * H * 4], src2[W * H2 * 4], fx[W * H * 4];
    uint32_t rt, flat, shown, shorter;
    Nv2aRenderState rs, held;
    int n;

    gl_backend();
    check(G->target_fxaa != NULL, "the backend has FXAA");

    /* Hard edges in, smoothed edges out; the target itself as it was. */
    rt = G->target_new(W, H);
    check(rt != 0, "target_new");
    flat_scene(rt);
    read_of(rt, H, src);
    check(partial(src, W * H) == 0, "the triangle drawn flat has hard edges");
    shown = G->target_fxaa(rt);
    check(shown != 0 && shown != rt, "target_fxaa gives a target of its own");
    read_of(shown, H, fx);
    n = partial(fx, W * H);
    printf("FXAA: %d edge pixels in between\n", n);
    check(n > 0, "the edges are smoothed");
    check(same_away_from_edges(src, fx, H) && is(at(fx, 16, 16), 255, 255, 255)
       && is(at(fx, 56, 56), 0, 0, 0), "away from the edges it is as it was");
    read_of(rt, H, px);
    check(!memcmp(px, src, sizeof px), "the target itself is left as it was");

    /* A picture with no edges comes through exactly. */
    G->target_bind(rt);
    G->clear(7, (const float[4]){ 0.25f, 0.5f, 0.75f, 1 }, 1.0f, 0, NULL);
    read_of(rt, H, src);
    check(G->target_fxaa(rt) == shown, "the same size reuses its target");
    read_of(shown, H, fx);
    check(!memcmp(fx, src, sizeof fx), "a flat colour comes through as it was");

    /* Whatever state the last draw left, the pass draws the whole picture;
     * passed a shorter target than the one being drawn into. */
    flat = G->target_new(W, H2);
    flat_scene(flat);
    read_of(flat, H2, src2);
    G->target_bind(rt);
    G->clear(7, black, 1.0f, 0, NULL);
    rs = plain();
    rs.depth_test = rs.depth_write = 1;
    rs.depth_func = 0x201;                                         /* LESS */
    draw(tri, 3, &rs, 1, 1, 1);
    held = plain();
    held.depth_test = 1;
    held.depth_func = 0x200;                                       /* NEVER */
    held.stencil_test = 1;
    held.stencil_func = 0x200;
    held.blend_enable = 1;
    held.blend_src = 0;                                            /* ZERO */
    held.blend_dst = 1;                                            /* ONE */
    held.color_mask = 0x00010000u;                                 /* red only */
    rs = held;
    rs.cull_enable = 1;
    rs.cull_face = 0x408;                                          /* FRONT_AND_BACK */
    rs.scissor_enable = 1;
    rs.scissor[2] = rs.scissor[3] = 1;
    G->state(&rs);
    read_of(rt, H, src);
    shorter = G->target_fxaa(flat);
    check(shorter != 0 && shorter != flat && shorter != rt, "a shorter picture gets a target of its own");
    read_of(shorter, H2, fx);
    check(same_away_from_edges(src2, fx, H2) && partial(fx, W * H2) > 0,
          "the pass ignores the state the last draw left");

    /* And the draws after it are as they would have been, the front half
     * asking for nothing it believes is already so: state it asks for
     * again holds (here, nothing is drawn), and with the program still in
     * use, a draw reaches the target being drawn into, at its size. */
    {
        const float q[6][4] = { { -1, -1, 0.5f, 1 }, { 1, -1, 0.5f, 1 }, { 1, 1, 0.5f, 1 },
                                { -1, -1, 0.5f, 1 }, { 1, 1, 0.5f, 1 }, { -1, 1, 0.5f, 1 } };
        G->uniform_f(NV2A_U_DRAWTINT, (const float[4]){ 1, 0, 0, 1 }, 1);
        G->state(&held);
        G->vertices(&q[0][0], 6, 4, attr_at, 0, no_const);
        G->draw(NV2A_PRIM_TRIANGLES, 6);
        read_of(rt, H, px);
        check(!memcmp(px, src, sizeof px), "the state asked for again holds");
        rs = plain();
        rs.depth_test = 1;
        rs.depth_func = 0x201;
        G->uniform_f(NV2A_U_DRAWTINT, (const float[4]){ 0, 1, 0, 1 }, 1);
        G->state(&rs);
        G->vertices(&q[0][0], 6, 4, attr_at, 0, no_const);
        G->draw(NV2A_PRIM_TRIANGLES, 6);
    }
    read_of(rt, H, px);
    check(is(at(px, 16, 16), 255, 255, 255) && is(at(px, 56, 56), 0, 255, 0)
       && is(at(px, 4, 60), 0, 255, 0),
          "the next draw has its program, target, viewport and depth");

    G->target_free(flat);
    G->target_free(rt);
    check(1, "target_free");
    G->report();
    printf("gl_backend: FXAA passes\n");
    return 0;
}
