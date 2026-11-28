/* Run with: python tests/gl_backend/run.py
 *
 * A line's button picture at its letters' depth (src/nv2a_gl/nv2a_prompts.h,
 * NV2A_PROMPT_LINE_TAG): the depth worked out on the CPU (nv2a_vsh.c,
 * nv2a_vsh_ndc_z) a quarter of a step behind (nv2a_prompts_depth.h), and
 * the backend's picture drawn there (nv2a_gfx_gl.c, glx_picture), with depth
 * forward and reversed, in the context every test here runs in
 * (context.h), as the graffiti error box over the loading screen has it:
 *
 *   1. Letters drawn by a vertex program near the far end, as the title's 2D
 *      is, and the picture beside them: the picture's depth is stored at the
 *      letters' step, where it isn't clear, and nowhere where it is.
 *   2. A screen drawn after them, behind them, with LEQUAL: it covers
 *      neither the letters nor the picture, but does the picture's clear
 *      corners.
 *   3. A fade drawn after them at the letters' own depth: it covers both.
 *   4. The picture with no depth (as every picture was): the screen behind
 *      covers it, and not the letters -- the empty A of the error box.
 */
#include "context.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"
#include "nv2a_vsh.h"
#include "nv2a_prompts_depth.h"

#ifndef GL_LOWER_LEFT
#  define GL_LOWER_LEFT           0x8CA1
#endif
#ifndef GL_NEGATIVE_ONE_TO_ONE
#  define GL_NEGATIVE_ONE_TO_ONE  0x935E
#endif
#ifndef GL_ZERO_TO_ONE
#  define GL_ZERO_TO_ONE          0x935F
#endif

void nv2a_gfx_gl_set_revz(int on);   /* nv2a_gfx_gl.c */

#define FAR_END 16777215.0
#define W 640
#define H 480
/* The letters: this many steps from the far end, over the left half. The
 * screen behind them this many steps farther. */
#define LETTERS_AT 8
#define BEHIND     2
/* The picture: its rectangle in NDC, a TEX x TEX texture opaque in its
 * middle half, clear round it; a pixel in the middle and one in a clear
 * corner, and one of the letters'. */
#define TEX 16
static const float pic_rect[4] = { 0.25f, -0.5f, 0.75f, 0.5f };
#define MID_X     480
#define MID_Y     240
#define CORNER_X  410
#define CORNER_Y  130
#define LETTER_X  160
#define LETTER_Y  240

typedef void (APIENTRY *ClipControlFn)(GLenum, GLenum);

static const Nv2aGfx *G;
static ClipControlFn clip_control;
static const float no_const[16][4];
static const float black[4] = { 0, 0, 0, 1 };

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

static void reversed(int on)
{
    clip_control(GL_LOWER_LEFT, on ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
    nv2a_glsl_set_revz(on);
    nv2a_gfx_gl_set_revz(on);
    G->state_invalidate();
}

/* MOV oPos, v0: the title's screen position, as its own programs end. */
static uint32_t vp_prog(void)
{
    static Nv2aVshProgram p;
    static char vs[96 * 1024];
    Nv2aVshInsn *in = &p.insns[0];
    int k;
    memset(&p, 0, sizeof p);
    in->mac = NV2A_MAC_MOV;
    in->src[0].type = NV2A_PARAM_V;
    for (k = 0; k < 4; k++) in->src[0].swz[k] = (uint8_t)k;
    in->out_is_oreg = 1; in->out_reg = NV2A_OREG_POS; in->out_mask = 0xF;
    in->final = 1;
    p.length = 1;
    p.inputs_read = 1;
    check(nv2a_vsh_emit_glsl(&p, vs, sizeof vs) > 0, "nv2a_vsh_emit_glsl");
    return G->prog_new(vs, NV2A_GLSL_VERSION "uniform vec4 drawTint;\nout vec4 frag;\n"
                       "void main() { frag = drawTint; }\n", "test vp");
}

/* A quad from x0 to x1 across the whole height, `steps` from the far end, in
 * colour col, with LEQUAL and depth written. */
static void quad(int x0, int x1, double steps, const float col[4])
{
    static const int attr_at[16] = { 0, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1 };
    static const int corner[6][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 },
                                      { 0, 0 }, { 1, 1 }, { 0, 1 } };
    float v[6][4];
    Nv2aRenderState rs;
    int i;
    for (i = 0; i < 6; i++) {
        v[i][0] = (float)(corner[i][0] ? x1 : x0);
        v[i][1] = (float)(corner[i][1] * H);
        v[i][2] = (float)(FAR_END - steps);
        v[i][3] = 1.0f;
    }
    G->uniform_f(NV2A_U_DRAWTINT, col, 1);
    state(&rs);
    rs.depth_test = 1; rs.depth_write = 1; rs.depth_func = 0x203;   /* LEQUAL */
    G->state(&rs);
    G->vertices(&v[0][0], 6, 4, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, 6);
}

/* The depth buffer at (x, y), as stored: steps from 0. */
static uint32_t step_at(int x, int y)
{
    uint32_t d = 0;
    glReadPixels(x, y, 1, 1, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, &d);
    check(1, "glReadPixels");
    return d >> 8;
}

/* Whether the colour at (x, y) is want. */
static int colour_at(uint32_t rt, int x, int y, const uint8_t want[4])
{
    uint8_t px[4];
    check(G->target_read(rt, (uint32_t)x, (uint32_t)y, 1, 1, 0, px), "target_read");
    return abs(px[0] - want[0]) <= 2 && abs(px[1] - want[1]) <= 2 && abs(px[2] - want[2]) <= 2;
}

static uint32_t picture_tex(void)
{
    static uint8_t rgba[TEX * TEX * 4];
    Nv2aSampler smp;
    int x, y;
    for (y = 0; y < TEX; y++)
        for (x = 0; x < TEX; x++) {
            const int in = x >= TEX / 4 && x < 3 * TEX / 4 && y >= TEX / 4 && y < 3 * TEX / 4;
            memset(&rgba[(y * TEX + x) * 4], in ? 255 : 0, 4);
        }
    memset(&smp, 0, sizeof smp);
    return G->tex_new(TEX, TEX, rgba, &smp);
}

/* The four steps, depth one way (revz) or the other. */
static void line(uint32_t rt, uint32_t tex, int revz)
{
    static const float red[4] = { 1, 0, 0, 1 }, green[4] = { 0, 1, 0, 1 }, blue[4] = { 0, 0, 1, 1 };
    static const uint8_t r8[4] = { 255, 0, 0, 255 }, g8[4] = { 0, 255, 0, 255 },
                         b8[4] = { 0, 0, 255, 255 }, w8[4] = { 255, 255, 255, 255 };
    const float scale[4] = { W / 2.0f, -H / 2.0f, 0, 0 }, off[4] = { W / 2.0f, H / 2.0f, 0, 0 },
                rect[4] = { W / 2.0f, H / 2.0f, W / 2.0f, H / 2.0f },
                surf[2] = { W, H }, zc[2] = { 0.0f, (float)FAR_END };
    const float opos[4] = { (float)LETTER_X, (float)LETTER_Y, (float)(FAR_END - LETTERS_AT), 1.0f };
    const char *way = revz ? "reversed" : "forward";
    char what[160];
    uint32_t prog, letters;
    float z;

    reversed(revz);
    prog = vp_prog();
    check(prog != 0, "prog_new");
    G->target_bind(rt);
    G->prog_use(prog);
    G->uniform_f(NV2A_U_VPSCALE, scale, 1);
    G->uniform_f(NV2A_U_VPOFF, off, 1);
    G->uniform_f(NV2A_U_VPRECT, rect, 1);
    G->uniform_f(NV2A_U_VPSURFACE, surf, 1);
    G->uniform_f(NV2A_U_ZCLIP, zc, 1);
    G->uniform_i(NV2A_U_POSMODE, 0);
    z = nv2a_prompts_behind(nv2a_vsh_ndc_z(opos, 0, zc, off[2], scale[2], 0), revz);

    /* 1. the letters, and the picture beside them at their depth */
    G->clear(7, black, 1.0f, 0, NULL);
    quad(0, W / 2, LETTERS_AT, red);
    G->picture(tex, pic_rect, NULL, 1.0f, &z);
    letters = step_at(LETTER_X, LETTER_Y);
    snprintf(what, sizeof what, "%s: the picture's depth at its letters' step (%u, %u)", way,
             step_at(MID_X, MID_Y), letters);
    check(step_at(MID_X, MID_Y) == letters, what);
    snprintf(what, sizeof what, "%s: no depth where the picture is clear", way);
    check(step_at(CORNER_X, CORNER_Y) == step_at(W - 1, H - 1)
          && step_at(CORNER_X, CORNER_Y) != letters, what);
    snprintf(what, sizeof what, "%s: the picture drawn", way);
    check(colour_at(rt, MID_X, MID_Y, w8) && colour_at(rt, LETTER_X, LETTER_Y, r8), what);

    /* 2. a screen behind them, drawn after them */
    quad(0, W, LETTERS_AT - BEHIND, green);
    snprintf(what, sizeof what, "%s: the screen behind covers neither the letters nor the picture", way);
    check(colour_at(rt, MID_X, MID_Y, w8) && colour_at(rt, LETTER_X, LETTER_Y, r8), what);
    snprintf(what, sizeof what, "%s: the screen behind shows in the picture's clear corner", way);
    check(colour_at(rt, CORNER_X, CORNER_Y, g8), what);

    /* 3. a fade at the letters' depth, drawn after them */
    quad(0, W, LETTERS_AT, blue);
    snprintf(what, sizeof what, "%s: a fade at the letters' depth covers both", way);
    check(colour_at(rt, MID_X, MID_Y, b8) && colour_at(rt, LETTER_X, LETTER_Y, b8), what);

    /* 4. as it was: no depth, and the screen behind covers it */
    G->clear(7, black, 1.0f, 0, NULL);
    quad(0, W / 2, LETTERS_AT, red);
    G->picture(tex, pic_rect, NULL, 1.0f, NULL);
    quad(0, W, LETTERS_AT - BEHIND, green);
    snprintf(what, sizeof what, "%s: with no depth, the screen behind covers the picture and not "
             "the letters", way);
    check(colour_at(rt, MID_X, MID_Y, g8) && colour_at(rt, LETTER_X, LETTER_Y, r8), what);

    G->prog_free(prog);
    check(1, "prog_free");
    printf("%s: the picture at its letters' step (%u), %.9f in GL's depth: the screen behind under it, "
           "a fade over it\n", way, letters, (double)z);
}

int main(void)
{
    uint32_t rt, tex;
    if (!gl_backend_context()) return 1;
    clip_control = (ClipControlFn)eglGetProcAddress("glClipControl");
    if (!clip_control) {
        fprintf(stderr, "FAIL no glClipControl\n");
        return 1;
    }
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    check(G->picture != NULL, "a picture call");
    rt = G->target_new(W, H);
    check(rt != 0, "target_new");
    tex = picture_tex();
    check(tex != 0, "tex_new");
    line(rt, tex, 0);
    line(rt, tex, 1);
    G->tex_free(tex);
    G->target_free(rt);
    check(1, "target_free");
    printf("gl_backend: a line's picture at its letters' depth passes\n");
    return 0;
}
