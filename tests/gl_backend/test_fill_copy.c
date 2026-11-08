/* Run with: python tests/gl_backend/run.py
 *
 * Widescreen's side fill drawn draw by draw (nv2a_wide_fill_copy, and the
 * shader's wideAcross and wideOff), on the OpenGL backend and a real GL, on
 * a widened frame's raster, 1707x960 (2x and 16:9), through the shaders the
 * engine emits, by the fixed-function path and a vertex program, as a menu
 * with moving rectangles is drawn: a rectangle that runs past the 4:3 edge
 * drawn first, stretched over the side and nearer, then the menu's band,
 * each draw of it in the middle (columns 216 to 1490) and again on each
 * side, magnified about that side's source column, 216 or 1490.
 *
 *   1. Each side shows, row by row, the band's colour at its source column:
 *      its two halves, and a short strip drawn at the source column alone.
 *   2. The 4:3 screen's dark last columns (214 and 215, left of the
 *      source) don't show on the side.
 *   3. The rectangle shows over the side, where it's nearer than the band,
 *      and the band around it; a bar drawn later and nearer still shows
 *      over the rectangle, on the side as in the middle.
 *   4. The middle is as the draws made it.
 *   5. The same multisampled (4 samples, where the device has them).
 */
#include "context.h"
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"
#include "nv2a_vsh.h"
#include "nv2a_wide.h"

#define FAR_END 16777215.0f
#define W 1707                  /* the raster */
#define H 960
#define TW 640                  /* the title's frame */
#define TH 480

enum { FF, PROG };

static const Nv2aGfx *G;
static const float no_const[16][4];
static uint32_t prog[2], rt;
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

static uint32_t link(const char *vs, const char *what)
{
    uint32_t p = G->prog_new(vs, NV2A_GLSL_VERSION "uniform vec4 drawTint;\nout vec4 frag;\n"
                             "void main() { frag = drawTint; }\n", what);
    check(p != 0, what);
    return p;
}

/* MUL R1, v0, c[0]; ADD oPos, R1, c[1]: D3D's pass-through program for the
 * title's 2D, as test_nudge.c builds it. */
static void insn(Nv2aVshInsn *in, Nv2aMacOp op, int a_type, int a_index, int k, int to_pos)
{
    int i;
    in->mac = op;
    in->src[0].type = (uint8_t)a_type; in->src[0].index = (uint8_t)a_index;
    in->src[op == NV2A_MAC_ADD ? 2 : 1].type = NV2A_PARAM_C;
    in->const_index = (int16_t)k;
    for (i = 0; i < 4; i++) in->src[0].swz[i] = in->src[1].swz[i] = in->src[2].swz[i] = (uint8_t)i;
    if (to_pos) { in->out_is_oreg = 1; in->out_reg = NV2A_OREG_POS; in->out_mask = 0xF; in->final = 1; }
    else { in->mac_temp = 1; in->mac_mask = 0xF; }
}

static void programs(void)
{
    static char vs[96 * 1024];
    static Nv2aVshProgram p;
    Nv2aVshFixed f;
    memset(&f, 0, sizeof f);
    f.inputs_read = 1;
    check(nv2a_vsh_emit_ff_glsl(&f, vs, sizeof vs) > 0, "nv2a_vsh_emit_ff_glsl");
    prog[FF] = link(vs, "test ff");
    memset(&p, 0, sizeof p);
    insn(&p.insns[0], NV2A_MAC_MUL, NV2A_PARAM_V, 0, 0, 0);
    insn(&p.insns[1], NV2A_MAC_ADD, NV2A_PARAM_R, 1, 1, 1);
    p.length = 2;
    p.inputs_read = 1;
    check(nv2a_vsh_emit_glsl(&p, vs, sizeof vs) > 0, "nv2a_vsh_emit_glsl");
    prog[PROG] = link(vs, "test pass-through");
}

/* A rectangle of the raster's columns x0 to x1 and rows y0 to y1 (the far
 * ones exclusive), at depth z, in colour rgb, by one path, with the x map
 * k x + off (1 and 0 for none) and the scissor sc (NULL for none). */
static void rect(int path, int x0, int y0, int x1, int y1, float z, const float rgb[3],
                 float k, float off, const uint32_t *sc)
{
    static const int attr_at[16] = { 0, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1 };
    static const int corner[6][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 },
                                      { 0, 0 }, { 1, 1 }, { 0, 1 } };
    static float c[192][4];
    const float surf[2] = { TW, TH }, zc[2] = { 0, FAR_END };
    const float tint[4] = { rgb[0], rgb[1], rgb[2], 1.0f };
    const float scale[4] = { TW / 2.0f, -TH / 2.0f, FAR_END, 0 };
    const float voff[4] = { TW / 2.0f + NV2A_D3D_NUDGE, TH / 2.0f + NV2A_D3D_NUDGE, 0, 0 };
    float pos[6][4], u[4], across = k - 1.0f;
    Nv2aRenderState rs;
    int i;

    for (i = 0; i < 6; i++) {
        /* The raster's pixels in the title's, which the frame is drawn in. */
        float x = (float)(corner[i][0] ? x1 : x0) * TW / W;
        float y = (float)(corner[i][1] ? y1 : y0) * TH / H;
        if (path == PROG) { pos[i][0] = x; pos[i][1] = y; }
        else { pos[i][0] = (x - TW / 2.0f) / (TW / 2.0f); pos[i][1] = (y - TH / 2.0f) / (-TH / 2.0f); }
        pos[i][2] = z; pos[i][3] = 1;
    }
    G->target_bind(rt);
    G->prog_use(prog[path]);
    G->uniform_f(NV2A_U_VPSURFACE, surf, 1);
    G->uniform_f(NV2A_U_ZCLIP, zc, 1);
    G->uniform_f(NV2A_U_DRAWTINT, tint, 1);
    G->uniform_f(NV2A_U_WIDEACROSS, &across, 1);
    G->uniform_f(NV2A_U_WIDEOFF, &off, 1);
    if (path == FF) {
        const float m[4][4] = { { TW / 2.0f, 0, 0, TW / 2.0f }, { 0, -TH / 2.0f, 0, TH / 2.0f },
                                { 0, 0, FAR_END, 0 }, { 0, 0, 0, 1 } };
        const float reg[4] = { NV2A_D3D_NUDGE, NV2A_D3D_NUDGE, 0, 0 };
        G->uniform_f(NV2A_U_FFMAT, &m[0][0], 4);
        nv2a_vsh_ff_vpoff(reg, u);
        G->uniform_f(NV2A_U_FFVPOFF, u, 1);
    } else {
        memset(c, 0, sizeof c);
        c[0][0] = c[0][1] = c[0][3] = 1; c[0][2] = FAR_END;
        c[1][0] = c[1][1] = NV2A_D3D_NUDGE;
        G->uniform_f(NV2A_U_C, &c[0][0], 192);
        G->uniform_f(NV2A_U_VPSCALE, scale, 1);
        G->uniform_f(NV2A_U_VPOFF, voff, 1);
        nv2a_vsh_vprect(voff, scale, TW, TH, u);
        G->uniform_f(NV2A_U_VPRECT, u, 1);
        G->uniform_i(NV2A_U_POSMODE, 0);
    }
    memset(&rs, 0, sizeof rs);
    rs.color_mask = 0x01010101u;
    rs.depth_test = 1; rs.depth_write = 1; rs.depth_func = 0x0203;     /* LEQUAL */
    rs.stencil_func = 0x207;
    rs.stencil_rmask = rs.stencil_wmask = 0xFF;
    rs.stencil_op[0] = rs.stencil_op[1] = rs.stencil_op[2] = 0x1E00;   /* KEEP */
    if (sc) { rs.scissor_enable = 1; memcpy(rs.scissor, sc, sizeof rs.scissor); }
    G->state(&rs);
    G->vertices(&pos[0][0], 6, 4, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, 6);
}

/* A draw of the menu's band, as the renderer makes it on a filled frame:
 * in the middle, then on each side, magnified about its source. */
static void band(int path, int x0, int y0, int x1, int y1, float z, const float rgb[3])
{
    int side;
    for (side = NV2A_WIDE_HUD_LEFT; side <= NV2A_WIDE_HUD_RIGHT; side++) {
        uint32_t a, b, src, sc[4];
        float k, off;
        check(nv2a_wide_fill_copy(W, 0.75f, side, &a, &b, &src, &k, &off), "nv2a_wide_fill_copy");
        sc[0] = a; sc[1] = 0; sc[2] = b; sc[3] = H;
        rect(path, x0, y0, x1, y1, z, rgb, k, off, sc);
    }
}

static const uint8_t *at(int x, int y)          /* from the top-left */
{
    return px + ((size_t)(H - 1 - y) * W + (size_t)x) * 4;
}

static void want(int x, int y, const float rgb[3], const char *what, const char *pass)
{
    const uint8_t *p = at(x, y);
    int i;
    for (i = 0; i < 3; i++)
        if (abs(p[i] - (int)(rgb[i] * 255.0f + 0.5f)) > 2) break;
    if (i < 3) {
        fprintf(stderr, "FAIL %s, %s: (%d, %d) is %d %d %d, not %.0f %.0f %.0f\n", pass, what,
                x, y, p[0], p[1], p[2], rgb[0] * 255, rgb[1] * 255, rgb[2] * 255);
        s_fail++;
    }
}

static const float blue[3] = { 0, 0, 1 }, red[3] = { 1, 0, 0 }, yellow[3] = { 1, 1, 0 },
                   cyan[3] = { 0, 1, 1 }, dark[3] = { 0.2f, 0.2f, 0.2f },
                   magenta[3] = { 1, 0, 1 }, black[3] = { 0, 0, 0 };

static void pass(const char *name)
{
    const float clear_rgba[4] = { 0, 0, 1, 1 };
    G->target_bind(rt);
    G->clear(7u, clear_rgba, 1.0f, 0, NULL);
    /* The moving rectangle, first: past the left 4:3 edge, over the side,
     * nearer than the band; not copied. */
    rect(PROG, 100, 100, 400, 200, 0.2f, red, 1.0f, 0.0f, NULL);
    /* The band, in the 4:3 screen, 214 to 1493: yellow above, cyan below,
     * the last columns dark, and a magenta strip at the left source alone
     * in rows 300 to 349. Then a black bar across, nearer than the
     * rectangle, in rows 150 to 169. */
    band(FF, 214, 0, 1493, H / 2, 0.6f, yellow);
    band(PROG, 214, H / 2, 1493, H, 0.6f, cyan);
    band(FF, 214, 0, 216, H, 0.5f, dark);
    band(FF, 1491, 0, 1493, H, 0.5f, dark);
    band(PROG, 216, 300, 217, 350, 0.4f, magenta);
    band(PROG, 214, 150, 1493, 170, 0.1f, black);
    check(G->target_read(rt, 0, 0, W, H, 0, px), "target_read");

    /* 1. Each side, row by row: the source column's colour. */
    want(0, 0, yellow, "left side: the top row, at the screen's edge", name);
    want(213, 299, yellow, "left side: above the strip", name);
    want(0, 300, magenta, "left side: the strip's first row, at the screen's edge", name);
    want(215, 349, magenta, "left side: the strip's last row, by the source", name);
    want(50, 350, yellow, "left side: below the strip", name);
    want(0, H / 2, cyan, "left side: the bottom half's first row", name);
    want(0, H - 1, cyan, "left side: the bottom row", name);
    want(W - 1, 0, yellow, "right side: the top row", name);
    want(W - 1, 325, yellow, "right side: no strip at its source", name);
    want(1491, H - 1, cyan, "right side: the bottom row, by the source", name);
    want(W - 1, H - 1, cyan, "right side: the bottom row, at the screen's edge", name);

    /* 2. The dark last columns don't show on the sides. */
    want(214, 50, yellow, "left side: the 4:3 screen's first column", name);
    want(215, 600, cyan, "left side: the column before the source", name);
    want(1491, 50, yellow, "right side: the column after the source", name);
    want(1492, 600, cyan, "right side: the 4:3 screen's last column", name);

    /* 3. The rectangle over the side, the band around it, the bar over it. */
    want(100, 100, red, "the rectangle: its first column and row, on the side", name);
    want(50, 130, yellow, "left side: left of the rectangle", name);
    want(99, 199, yellow, "left side: the column before the rectangle", name);
    want(150, 200, yellow, "left side: the row below the rectangle", name);
    want(150, 160, black, "the bar over the rectangle, on the side", name);
    want(0, 160, black, "the bar, at the screen's edge", name);
    want(W - 1, 160, black, "the bar, at the right edge", name);

    /* 4. The middle. */
    want(300, 120, red, "middle: the rectangle", name);
    want(300, 160, black, "middle: the bar over it", name);
    want(399, 199, red, "middle: the rectangle's last pixel", name);
    want(400, 120, yellow, "middle: past the rectangle", name);
    want(216, 325, magenta, "middle: the strip at the source", name);
    want(217, 325, yellow, "middle: past the strip", name);
    want(853, 600, cyan, "middle: the bottom half", name);
    want(1490, 50, yellow, "middle: the right source", name);
    (void)blue;
}

int main(void)
{
    if (!gl_backend_context()) return 1;
    px = malloc((size_t)W * H * 4);
    check(px != NULL, "malloc");
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    programs();
    rt = G->target_new(W, H);
    check(rt != 0, "target_new 1707x960");

    pass("single-sampled");

    /* 5. Multisampled, where the device has 4 samples a pixel. */
    if (G->samples_max && G->samples && G->samples_max() >= 4) {
        check(G->samples(4) == 4, "samples(4)");
        pass("4 samples");
        G->samples(1);
    } else {
        printf("gl_backend: fill copies: no 4 samples here, multisampled not run\n");
    }

    G->target_free(rt);
    free(px);
    if (s_fail) {
        fprintf(stderr, "gl_backend: fill copies: %d failed\n", s_fail);
        return 1;
    }
    printf("gl_backend: fill copies pass\n");
    return 0;
}
