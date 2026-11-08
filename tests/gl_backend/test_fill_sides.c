/* Run with: python tests/gl_backend/run.py
 *
 * Widescreen's side fill (fill_sides in src/nv2a_gl/nv2a_gfx_gl.c), on the
 * OpenGL backend and a real GL, driven through the device interface as the
 * front half drives it, on a headless EGL context (context.h), on a widened
 * frame's raster, 1707x960 (2x and 16:9), with the columns
 * nv2a_wide_fill_cols gives it there: the 4:3 screen's 214 to 1493, column
 * 216 copied across 0 to 216, and column 1490 across 1491 to 1707.
 *
 *   1. Each side takes its source column's colour row by row, to the
 *      screen's edge, top row to bottom: a band of rows the source column
 *      holds alone shows across its side, and the rows around it the
 *      colour the 4:3 screen has there.
 *   2. The columns between each source and its 4:3 edge take the source's
 *      colour too: the 4:3 screen's last column, left dark as the options
 *      menu's art leaves it, doesn't show as a line.
 *   3. Nothing from one source to the other changes, the sources among
 *      them.
 *   4. A scissor a draw left set doesn't cut it.
 *   5. The same multisampled (4 samples, where the device has them): the
 *      fill is on the picture resolved from it, which is what is shown.
 *   6. A strip hidden at the top (fill_strip, after fill_sides): rows
 *      150 to 199 of columns 0 to 216 take row 200's colour, and nothing
 *      else changes, the source column and the right side among them.
 */
#include "context.h"
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"

#define W 1707
#define H 960
#define X0 214u
#define X1 1493u
#define SRC_L 216u
#define SRC_R 1490u

static const Nv2aGfx *G;
static uint32_t rt;
static uint8_t *px;
static int s_fail;

static void check(int ok, const char *what)
{
    GLenum e = glGetError();
    if (!ok || e != GL_NO_ERROR) {
        fprintf(stderr, "FAIL %s%s (GL error 0x%x)\n", what, ok ? "" : ": wrong", e);
        exit(1);
    }
}

/* Pixel (x, y) counted from the top-left, as the title counts. */
static const uint8_t *at(int x, int y)
{
    return px + ((size_t)(H - 1 - y) * W + (size_t)x) * 4;
}

static void want(int x, int y, int r, int g, int b, const char *what, const char *pass)
{
    const uint8_t *p = at(x, y);
    if (abs(p[0] - r) > 2 || abs(p[1] - g) > 2 || abs(p[2] - b) > 2) {
        fprintf(stderr, "FAIL %s, %s: (%d, %d) is %d %d %d, not %d %d %d\n",
                pass, what, x, y, p[0], p[1], p[2], r, g, b);
        s_fail++;
    }
}

static void clear(float r, float g, float b, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    const float rgba[4] = { r, g, b, 1.0f };
    const uint32_t rect[4] = { x0, y0, x1, y1 };
    G->target_bind(rt);
    G->clear(1u, rgba, 1.0f, 0, rect);
}

/* The frame: red past the 4:3 screen, as the widened scene would leave it;
 * the 4:3 screen yellow in its top half and cyan in its bottom, but for its
 * last column, black, as the options menu's art leaves it; and rows 100 to
 * 199 of each source column alone magenta (left) and white (right). */
static void frame(void)
{
    clear(1.0f, 0.0f, 0.0f, 0, 0, W, H);
    clear(1.0f, 1.0f, 0.0f, X0, 0, X1, H / 2);
    clear(0.0f, 1.0f, 1.0f, X0, H / 2, X1, H);
    clear(0.0f, 0.0f, 0.0f, X1 - 1, 0, X1, H);
    clear(1.0f, 0.0f, 1.0f, SRC_L, 100, SRC_L + 1, 200);
    clear(1.0f, 1.0f, 1.0f, SRC_R, 100, SRC_R + 1, 200);
}

/* A scissor of 10 by 10 pixels, as a draw leaves it set. */
static void scissor_left_set(void)
{
    Nv2aRenderState rs;
    memset(&rs, 0, sizeof rs);
    rs.stencil_func = 0x207;
    rs.stencil_rmask = rs.stencil_wmask = 0xFF;
    rs.stencil_op[0] = rs.stencil_op[1] = rs.stencil_op[2] = 0x1E00;   /* KEEP */
    rs.scissor_enable = 1;
    rs.scissor[0] = 800; rs.scissor[1] = 400; rs.scissor[2] = 810; rs.scissor[3] = 410;
    G->target_bind(rt);
    G->state(&rs);
}

static void pass(const char *name)
{
    frame();
    scissor_left_set();
    G->fill_sides(rt, SRC_L, SRC_R);
    check(1, "fill_sides");
    check(G->target_read(rt, 0, 0, W, H, 0, px), "target_read");

    /* 1. Each side, row by row, to the screen's edge. */
    want(0, 0, 255, 255, 0, "left side: the top row, the 4:3 screen's", name);
    want(213, 99, 255, 255, 0, "left side: the row above the band", name);
    want(0, 100, 255, 0, 255, "left side: the band's first row, at the screen's edge", name);
    want(213, 199, 255, 0, 255, "left side: the band's last row, at the 4:3 edge", name);
    want(100, 200, 255, 255, 0, "left side: the row below the band", name);
    want(0, H / 2, 0, 255, 255, "left side: the bottom half's first row", name);
    want(0, H - 1, 0, 255, 255, "left side: the bottom row", name);
    want(W - 1, 0, 255, 255, 0, "right side: the top row", name);
    want(X1, 100, 255, 255, 255, "right side: the band's first row, at the 4:3 edge", name);
    want(W - 1, 199, 255, 255, 255, "right side: the band's last row, at the screen's edge", name);
    want(W - 1, 200, 255, 255, 0, "right side: the row below the band", name);
    want(W - 1, H - 1, 0, 255, 255, "right side: the bottom row", name);

    /* 2. Between each source and its 4:3 edge, the source's colour. */
    want(X0, 150, 255, 0, 255, "filled: the 4:3 screen's first column", name);
    want(SRC_L - 1, 150, 255, 0, 255, "filled: the column before the left source", name);
    want(X0, 50, 255, 255, 0, "filled: the first column, above the band", name);
    want(SRC_R + 1, 150, 255, 255, 255, "filled: the column after the right source", name);
    want(X1 - 1, 150, 255, 255, 255, "filled: the dark last column, in the band", name);
    want(X1 - 1, 50, 255, 255, 0, "filled: the dark last column, above the band", name);
    want(X1 - 1, H - 1, 0, 255, 255, "filled: the dark last column's bottom", name);

    /* 3. From one source to the other, as they were. */
    want(SRC_L, 150, 255, 0, 255, "kept: the left source", name);
    want(SRC_L + 1, 150, 255, 255, 0, "kept: the column after it", name);
    want(853, 150, 255, 255, 0, "kept: the middle", name);
    want(SRC_R - 1, 150, 255, 255, 0, "kept: the column before the right source", name);
    want(SRC_R, 150, 255, 255, 255, "kept: the right source", name);
    want(SRC_R, H - 1, 0, 255, 255, "kept: the right source's bottom", name);
}

static void strip(const char *name)
{
    frame();
    scissor_left_set();
    G->fill_sides(rt, SRC_L, SRC_R);
    G->fill_strip(rt, SRC_L, 200, 150, 200);
    check(1, "fill_strip");
    check(G->target_read(rt, 0, 0, W, H, 0, px), "target_read");
    want(0, 150, 255, 255, 0, "strip: its first row, at the screen's edge", name);
    want(SRC_L - 1, 199, 255, 255, 0, "strip: its last row, by the source", name);
    want(X0, 175, 255, 255, 0, "strip: the 4:3 screen's first column", name);
    want(0, 149, 255, 0, 255, "strip: the row above it, as filled", name);
    want(SRC_L - 1, 100, 255, 0, 255, "strip: the band's first row, as filled", name);
    want(0, 200, 255, 255, 0, "strip: the row under it, as it was", name);
    want(SRC_L, 175, 255, 0, 255, "strip: the source column, as it was", name);
    want(W - 1, 175, 255, 255, 255, "strip: the right side, as filled", name);
    want(853, 175, 255, 255, 0, "strip: the middle, as it was", name);
}

int main(void)
{
    if (!gl_backend_context()) return 1;
    px = malloc((size_t)W * H * 4);
    check(px != NULL, "malloc");
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    check(G->fill_sides != NULL, "fill_sides is there");
    check(G->fill_strip != NULL, "fill_strip is there");
    rt = G->target_new(W, H);
    check(rt != 0, "target_new 1707x960");

    pass("single-sampled");
    strip("single-sampled");

    /* 5. Multisampled, where the device has 4 samples a pixel. */
    if (G->samples_max && G->samples && G->samples_max() >= 4) {
        check(G->samples(4) == 4, "samples(4)");
        pass("4 samples");
        strip("4 samples");
        G->samples(1);
    } else {
        printf("gl_backend: fill sides: no 4 samples here, multisampled not run\n");
    }

    G->target_free(rt);
    check(1, "target_free");
    free(px);
    if (s_fail) {
        fprintf(stderr, "gl_backend: fill sides: %d failed\n", s_fail);
        return 1;
    }
    printf("gl_backend: fill sides pass\n");
    return 0;
}
