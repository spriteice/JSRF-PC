/* Run with: python tests/gl_backend/run.py
 *
 * The polygon offset in the per-draw state (glx_state in
 * src/nv2a_gl/nv2a_gfx_gl.c), driven through the device interface the way
 * the front half drives it, on a headless EGL context (context.h): a second
 * quad drawn at the depth of the first, which the depth test then decides by
 * the offset alone.
 */
#include "context.h"
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"

#define TW 16

static const Nv2aGfx *G;
static uint32_t prog_col;
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

static void state(Nv2aRenderState *rs)
{
    memset(rs, 0, sizeof *rs);
    rs->stencil_func = 0x207;
    rs->stencil_rmask = rs->stencil_wmask = 0xFF;
    rs->stencil_op[0] = rs->stencil_op[1] = rs->stencil_op[2] = 0x1E00;   /* KEEP */
}

/* A quad over the whole of target `rt` at depth z, coloured `c`. */
static void quad(uint32_t rt, const float c[4], float z, const Nv2aRenderState *rs)
{
    float v[6][8];
    static const float xy[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 },
                                    { -1, -1 }, { 1, 1 }, { -1, 1 } };
    int i;
    for (i = 0; i < 6; i++) {
        v[i][0] = xy[i][0]; v[i][1] = xy[i][1]; v[i][2] = z; v[i][3] = 1;
        memcpy(&v[i][4], c, sizeof(float) * 4);
    }
    G->target_bind(rt);
    G->prog_use(prog_col);
    G->state(rs);
    G->vertices(&v[0][0], 6, 8, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, 6);
}

/* Whether every pixel of an n x n target is `want`, give or take a step. */
static int all(uint32_t rt, int n, const uint8_t want[4])
{
    static uint8_t px[TW * TW * 4];
    int i, k;
    check(G->target_read(rt, 0, 0, (uint32_t)n, (uint32_t)n, 0, px), "target_read");
    for (i = 0; i < n * n; i++)
        for (k = 0; k < 4; k++)
            if (abs(px[i * 4 + k] - want[k]) > 2) {
                fprintf(stderr, "  pixel %d: %u %u %u %u, wanted %u %u %u %u\n", i,
                        px[i * 4], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3],
                        want[0], want[1], want[2], want[3]);
                return 0;
            }
    return 1;
}

int main(void)
{
    uint32_t small;

    if (!gl_backend_context()) return 1;
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    prog_col = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\nlayout(location = 1) in vec4 v1;\n"
        "out vec4 c;\nvoid main() { gl_Position = v0; c = v1; }\n",
        NV2A_GLSL_VERSION "in vec4 c;\nout vec4 frag;\nvoid main() { frag = c; }\n", "test col");
    check(prog_col != 0, "prog_new");
    small = G->target_new(TW, TW);
    check(small != 0, "target_new");

    /* Polygon offset: a second quad at the depth of the first, depth test
     * LESS. It loses the tie with no offset, wins it pulled forward a step,
     * and loses it pushed back one -- and a state reset brings it back. */
    {
        static const float black[4] = { 0, 0, 0, 1 }, red[4] = { 1, 0, 0, 1 },
                           green[4] = { 0, 1, 0, 1 };
        static const uint8_t r8[4] = { 255, 0, 0, 255 }, g8[4] = { 0, 255, 0, 255 };
        Nv2aRenderState rs;
        int pass;
        for (pass = 0; pass < 4; pass++) {
            state(&rs);
            rs.depth_test = 1; rs.depth_write = 1; rs.depth_func = 0x201;  /* LESS */
            G->target_bind(small);
            G->clear(7, black, 1.0f, 0, NULL);
            quad(small, red, 0.25f, &rs);
            rs.poly_offset = pass == 1 || pass == 3;
            rs.poly_factor = 0.0f;
            rs.poly_units  = pass == 2 ? 1.0f : -1.0f;
            if (pass == 2) rs.poly_offset = 1;
            if (pass == 3) G->state_invalidate();
            quad(small, green, 0.25f, &rs);
            check(all(small, TW, pass == 1 || pass == 3 ? g8 : r8),
                  pass == 0 ? "no offset: the tie goes to the first"
                : pass == 1 ? "pulled forward one step: the second wins"
                : pass == 2 ? "pushed back one step: the first keeps it"
                            : "after a state reset, pulled forward again: the second wins");
        }
        printf("polygon offset settles the tie as asked\n");
    }

    G->target_free(small);
    check(1, "target_free");
    printf("gl_backend: polygon offset passes\n");
    return 0;
}
