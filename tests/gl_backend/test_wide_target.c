/* Run with: python tests/gl_backend/run.py
 *
 * A widened frame's raster (nv2a_wide_raster in src/nv2a_gl/nv2a_wide.c:
 * 1707x960 at 2x and 16:9), on the OpenGL backend and a real GL, driven
 * through the device interface as the front half drives it, on a headless
 * EGL context (context.h). Its width is no multiple of 640 and not a power
 * of two, and is wider than it is tall by other than 4:3:
 *
 *   1. It is made, and a clear with no rectangle reaches its last column and
 *      its last row.
 *   2. A clear rectangle (raster pixels, top-down, far edges exclusive)
 *      clears exactly its own pixels, on each axis by its own numbers.
 *   3. A draw across the whole of clip space covers the whole raster: the
 *      viewport is the target's own size, so the right half of clip space
 *      is the right half of the 1707 columns, its middle at 853.5.
 *   4. A scissor clips by raster pixels, x and y each by their own.
 */
#include "context.h"
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"

#define W 1707
#define H 960

static const Nv2aGfx *G;
static uint32_t rt, fill;
static uint8_t *px;
static const int attr_at[16] = { 0, -1, -1, -1, -1, -1, -1, -1,
                                 -1, -1, -1, -1, -1, -1, -1, -1 };
static const float no_const[16][4];
static int s_fail;

static void check(int ok, const char *what)
{
    GLenum e = glGetError();
    if (!ok || e != GL_NO_ERROR) {
        fprintf(stderr, "FAIL %s%s (GL error 0x%x)\n", what, ok ? "" : ": wrong", e);
        exit(1);
    }
}

static void read_all(void)
{
    check(G->target_read(rt, 0, 0, W, H, 0, px), "target_read");
}

/* Pixel (x, y) counted from the top-left, as the title counts. */
static const uint8_t *at(int x, int y)
{
    return px + ((size_t)(H - 1 - y) * W + (size_t)x) * 4;
}

static void want(int x, int y, int r, int g, int b, const char *what)
{
    const uint8_t *p = at(x, y);
    if (abs(p[0] - r) > 2 || abs(p[1] - g) > 2 || abs(p[2] - b) > 2) {
        fprintf(stderr, "FAIL %s: (%d, %d) is %d %d %d, not %d %d %d\n",
                what, x, y, p[0], p[1], p[2], r, g, b);
        s_fail++;
    }
}

static void clear(float r, float g, float b, const uint32_t *rect)
{
    const float rgba[4] = { r, g, b, 1.0f };
    G->target_bind(rt);
    G->clear(1u, rgba, 1.0f, 0, rect);
}

/* The right half of clip space, x 0..1, the whole height, in blue; with
 * the scissor `sc` (raster pixels, top-down, far edges exclusive) if not
 * NULL. */
static void right_half(const uint32_t *sc)
{
    static const float v[6][4] = {
        { 0, -1, 0, 1 }, { 1, -1, 0, 1 }, { 1, 1, 0, 1 },
        { 0, -1, 0, 1 }, { 1,  1, 0, 1 }, { 0, 1, 0, 1 } };
    Nv2aRenderState rs;
    memset(&rs, 0, sizeof rs);
    rs.stencil_func = 0x207;
    rs.stencil_rmask = rs.stencil_wmask = 0xFF;
    rs.stencil_op[0] = rs.stencil_op[1] = rs.stencil_op[2] = 0x1E00;   /* KEEP */
    if (sc) { rs.scissor_enable = 1; memcpy(rs.scissor, sc, sizeof rs.scissor); }
    G->target_bind(rt);
    G->prog_use(fill);
    G->texture(0, 0);
    G->state(&rs);
    G->vertices(&v[0][0], 6, 4, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, 6);
}

int main(void)
{
    static const uint32_t box[4] = { 427, 240, 1280, 720 };
    static const uint32_t last_cols[4] = { 1700, 0, W, 100 };

    if (!gl_backend_context()) return 1;
    px = malloc((size_t)W * H * 4);
    check(px != NULL, "malloc");
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    fill = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\n"
        "void main() { gl_Position = v0; }\n",
        NV2A_GLSL_VERSION "out vec4 frag;\n"
        "void main() { frag = vec4(0.0, 0.0, 1.0, 1.0); }\n", "fill");
    check(fill != 0, "prog_new");
    rt = G->target_new(W, H);
    check(rt != 0, "target_new 1707x960");

    /* 1. All of it, to the last column and row. */
    clear(0.0f, 1.0f, 0.0f, NULL);
    read_all();
    want(0, 0, 0, 255, 0, "cleared whole: top left");
    want(W - 1, 0, 0, 255, 0, "cleared whole: top right");
    want(0, H - 1, 0, 255, 0, "cleared whole: bottom left");
    want(W - 1, H - 1, 0, 255, 0, "cleared whole: bottom right");

    /* 2. A rectangle: its own pixels and no others. */
    clear(0.0f, 0.0f, 0.0f, NULL);
    clear(1.0f, 0.0f, 0.0f, box);
    read_all();
    want(427, 240, 255, 0, 0, "rectangle: its first pixel");
    want(1279, 719, 255, 0, 0, "rectangle: its last pixel");
    want(426, 480, 0, 0, 0, "rectangle: the column before it");
    want(1280, 480, 0, 0, 0, "rectangle: the column after it");
    want(853, 239, 0, 0, 0, "rectangle: the row above it");
    want(853, 720, 0, 0, 0, "rectangle: the row below it");

    /* 3. The right half of clip space is the right half of the raster. */
    clear(0.0f, 0.0f, 0.0f, NULL);
    right_half(NULL);
    read_all();
    want(W - 1, 0, 0, 0, 255, "draw: the last column, top");
    want(W - 1, H - 1, 0, 0, 255, "draw: the last column, bottom");
    want(854, 480, 0, 0, 255, "draw: right of the middle");
    want(852, 480, 0, 0, 0, "draw: left of the middle");
    want(0, 480, 0, 0, 0, "draw: the first column");

    /* 4. A scissor: the last seven columns and the top 100 rows only. */
    clear(0.0f, 0.0f, 0.0f, NULL);
    right_half(last_cols);
    read_all();
    want(1700, 0, 0, 0, 255, "scissor: its first column");
    want(W - 1, 99, 0, 0, 255, "scissor: its last pixel");
    want(1699, 50, 0, 0, 0, "scissor: the column before it");
    want(W - 1, 100, 0, 0, 0, "scissor: the row below it");

    G->prog_free(fill);
    G->target_free(rt);
    check(1, "target_free");
    free(px);
    if (s_fail) {
        fprintf(stderr, "gl_backend: wide target: %d failed\n", s_fail);
        return 1;
    }
    printf("gl_backend: wide target pass\n");
    return 0;
}
