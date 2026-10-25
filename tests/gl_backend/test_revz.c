/* Run with: python tests/gl_backend/run.py
 *
 * Reversed depth: clip control's 0..w range, which revz_start in
 * src/nv2a_gl/nv2a_gl.c turns on, the emitters counting depth down from the
 * far end (nv2a_vsh.c), and the backend turning its depth state round
 * (nv2a_gfx_gl.c), in the context every test here runs in (context.h). This
 * one also needs OpenGL 4.5 or ARB_clip_control, as the engine does (Mesa's
 * llvmpipe has both).
 *
 *   1. The backend: with depth reversed, LESS, the clear, the polygon offset
 *      and the depth readback mean what they mean with it forward.
 *   2. Layers a step apart near the far end, as the title draws its menus,
 *      by a vertex program with LEQUAL: a layer one step behind is hidden
 *      everywhere, and one a step in front shown everywhere. llvmpipe keeps
 *      them apart with depth forward too, so here this guards reversed depth
 *      only.
 *   3. The fixed-function depth near the far end, a composite matrix with the
 *      0..16777215 scale folded in as the title's are: with the precise row
 *      (ffRevZ), every depth is stored as one of the two steps either side
 *      of its exact z / w. Forward depth's figures, and reversed depth's
 *      without the precise row, are printed alongside: they are what
 *      flickered.
 */
#include "context.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"
#include "nv2a_vsh.h"

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

/* The 24-bit depth range: the far end, in steps. */
#define FAR_END 16777215.0
#define W 640
#define H 480
/* Test 1's target, and test 3's points: one every GRID pixels. */
#define TW 16
#define GRID 8
#define NX (W / GRID - 1)
#define NY (H / GRID - 1)
/* Test 2's layer: this many steps from the far end at its top left corner,
 * and this many more across it and down it. */
#define LAYER_AT     2
#define LAYER_ACROSS 498
#define LAYER_DOWN   248
/* Test 3's camera: a D3D projection with these planes, and a model-view
 * turned a little on each axis, so that every term of the sum counts. */
#define NEAR_PLANE 0.5
#define FAR_PLANE  2000.0
#define FOV_Y      1.0
#define TURN_X     0.3
#define TURN_Y     0.7
/* Test 3's points: this far from the camera, where the depth is within the
 * last half a percent of the range. */
#define W_MIN 100.0
#define W_MAX 1000.0

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

/* Depth one way or the other, as revz_start sets it (or leaves it). */
static void reversed(int on)
{
    clip_control(GL_LOWER_LEFT, on ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
    nv2a_glsl_set_revz(on);
    nv2a_gfx_gl_set_revz(on);
    G->state_invalidate();
}

/* The depth buffer as stored: steps from 0, whichever way round. */
static void read_steps(uint32_t *out, int pw, int ph)
{
    int i;
    glReadPixels(0, 0, pw, ph, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, out);
    for (i = 0; i < pw * ph; i++) out[i] >>= 8;
    check(1, "glReadPixels");
}

/* ---- 1. The backend's depth state, turned round ---------------------- */

static uint32_t prog_col;

/* A quad over the whole target at forward depth z, coloured c, drawn as an
 * emitted program would with depth reversed: 1 - z. */
static void quad(uint32_t rt, const float c[4], float z, const Nv2aRenderState *rs)
{
    static const int attr_at[16] = { 0, 4, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1 };
    static const float xy[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 },
                                    { -1, -1 }, { 1, 1 }, { -1, 1 } };
    float v[6][8];
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

/* Whether every pixel of the target is `want`, give or take a step. */
static int all(uint32_t rt, const uint8_t want[4])
{
    static uint8_t px[TW * TW * 4];
    int i, k;
    check(G->target_read(rt, 0, 0, TW, TW, 0, px), "target_read");
    for (i = 0; i < TW * TW; i++)
        for (k = 0; k < 4; k++)
            if (abs(px[i * 4 + k] - want[k]) > 2) {
                fprintf(stderr, "  pixel %d: %u %u %u %u, wanted %u %u %u %u\n", i,
                        px[i * 4], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3],
                        want[0], want[1], want[2], want[3]);
                return 0;
            }
    return 1;
}

static void backend_turned_round(void)
{
    static const float red[4] = { 1, 0, 0, 1 }, green[4] = { 0, 1, 0, 1 },
                       blue[4] = { 0, 0, 1, 1 };
    static const uint8_t r8[4] = { 255, 0, 0, 255 }, g8[4] = { 0, 255, 0, 255 },
                         b8[4] = { 0, 0, 255, 255 };
    static float depth[TW * TW];
    Nv2aRenderState rs;
    uint32_t small;
    int i;

    prog_col = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\nlayout(location = 1) in vec4 v1;\n"
        "out vec4 c;\nvoid main() { gl_Position = vec4(v0.xy, 1.0 - v0.z, 1.0); c = v1; }\n",
        NV2A_GLSL_VERSION "in vec4 c;\nout vec4 frag;\nvoid main() { frag = c; }\n", "test col");
    check(prog_col != 0, "prog_new");
    small = G->target_new(TW, TW);
    check(small != 0, "target_new");
    reversed(1);

    state(&rs);
    rs.depth_test = 1; rs.depth_write = 1; rs.depth_func = 0x201;   /* LESS */
    G->target_bind(small);
    G->clear(7, black, 1.0f, 0, NULL);
    quad(small, red, 0.5f, &rs);
    check(all(small, r8), "LESS against the clear to the far end: drawn");
    quad(small, green, 0.75f, &rs);
    check(all(small, r8), "LESS behind it: not drawn");
    quad(small, blue, 0.25f, &rs);
    check(all(small, b8), "LESS in front of it: drawn");
    check(G->target_read_depth(small, TW, TW, depth), "target_read_depth");
    for (i = 0; i < TW * TW; i++)
        if (fabs(depth[i] - 0.25) > 1.0 / FAR_END) {
            fprintf(stderr, "  depth %d: %.9f, wanted 0.25\n", i, depth[i]);
            check(0, "the depth read back is the forward depth");
        }
    quad(small, green, 0.25f, &rs);
    check(all(small, b8), "LESS at the same depth: the tie goes to the first");
    rs.poly_offset = 1; rs.poly_units = -1.0f;
    quad(small, green, 0.25f, &rs);
    check(all(small, g8), "pulled forward one step: the second wins");
    rs.poly_units = 1.0f;
    quad(small, red, 0.25f, &rs);
    check(all(small, g8), "pushed back one step: the first keeps it");

    G->target_free(small);
    G->prog_free(prog_col);
    check(1, "target_free");
    printf("reversed: LESS, the clear, the polygon offset and the readback turn round\n");
}

/* ---- 2. Layers a step apart at the far end ------------------------- */

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

/* The layer over the whole target, `shift` steps nearer than LAYER_AT and
 * the rest, in colour col, with LEQUAL. */
static void layer(int shift, const float col[4])
{
    static const int attr_at[16] = { 0, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1 };
    static const int corner[6][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 },
                                      { 0, 0 }, { 1, 1 }, { 0, 1 } };
    float v[6][4];
    Nv2aRenderState rs;
    int i;
    for (i = 0; i < 6; i++) {
        v[i][0] = (float)(corner[i][0] * W);
        v[i][1] = (float)(corner[i][1] * H);
        v[i][2] = (float)(FAR_END - LAYER_AT - shift
                          - corner[i][0] * LAYER_ACROSS - corner[i][1] * LAYER_DOWN);
        v[i][3] = 1.0f;
    }
    G->uniform_f(NV2A_U_DRAWTINT, col, 1);
    state(&rs);
    rs.depth_test = 1; rs.depth_write = 1; rs.depth_func = 0x203;   /* LEQUAL */
    G->state(&rs);
    G->vertices(&v[0][0], 6, 4, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, 6);
}

/* How many pixels of the target are not `want`. */
static int not_colour(uint32_t rt, const uint8_t want[4])
{
    static uint8_t px[W * H * 4];
    int i, n = 0;
    check(G->target_read(rt, 0, 0, W, H, 0, px), "target_read");
    for (i = 0; i < W * H; i++)
        n += abs(px[i * 4] - want[0]) > 2 || abs(px[i * 4 + 1] - want[1]) > 2 ||
             abs(px[i * 4 + 2] - want[2]) > 2;
    return n;
}

typedef struct { int behind_shown, front_hidden; } Layers;

/* The layer, then one a step behind it, then one a step in front. */
static Layers layers(uint32_t rt, int revz)
{
    static const float red[4] = { 1, 0, 0, 1 }, green[4] = { 0, 1, 0, 1 },
                       blue[4] = { 0, 0, 1, 1 };
    static const uint8_t r8[4] = { 255, 0, 0, 255 }, b8[4] = { 0, 0, 255, 255 };
    const float scale[4] = { W / 2.0f, -H / 2.0f, 0, 0 }, off[4] = { W / 2.0f, H / 2.0f, 0, 0 },
                rect[4] = { W / 2.0f, H / 2.0f, W / 2.0f, H / 2.0f },
                surf[2] = { W, H }, zc[2] = { 0.0f, (float)FAR_END };
    Layers l;
    uint32_t prog;

    reversed(revz);
    prog = vp_prog();
    check(prog != 0, "prog_new");
    G->target_bind(rt);
    G->clear(7, black, 1.0f, 0, NULL);
    G->prog_use(prog);
    G->uniform_f(NV2A_U_VPSCALE, scale, 1);
    G->uniform_f(NV2A_U_VPOFF, off, 1);
    G->uniform_f(NV2A_U_VPRECT, rect, 1);
    G->uniform_f(NV2A_U_VPSURFACE, surf, 1);
    G->uniform_f(NV2A_U_ZCLIP, zc, 1);
    G->uniform_i(NV2A_U_POSMODE, 0);
    layer(0, red);
    check(not_colour(rt, r8) == 0, "the first layer drawn everywhere");
    layer(-1, green);
    l.behind_shown = not_colour(rt, r8);
    layer(1, blue);
    l.front_hidden = not_colour(rt, b8);
    G->prog_free(prog);
    check(1, "layers at the far end");
    return l;
}

/* ---- 3. The fixed-function depth at the far end ---------------------- */

static float M[4][4];   /* the composite, rounded to float as the title's */

/* viewport * projection * model-view, worked out in double, as D3D does. */
static void composite(void)
{
    const double q = FAR_PLANE / (FAR_PLANE - NEAR_PLANE), ys = 1.0 / tan(FOV_Y / 2.0),
                 xs = ys * H / W;
    const double cx = cos(TURN_X), sx = sin(TURN_X), cy = cos(TURN_Y), sy = sin(TURN_Y);
    const double mv[4][4] = {
        { cy, sx * sy, cx * sy, 0 },
        { 0, cx, -sx, 0 },
        { -sy, sx * cy, cx * cy, 0 },
        { 0, 0, 0, 1 },
    };
    const double proj[4][4] = {
        { xs, 0, 0, 0 }, { 0, ys, 0, 0 }, { 0, 0, q, -q * NEAR_PLANE }, { 0, 0, 1, 0 },
    };
    const double vp[4][4] = {
        { W / 2.0, 0, 0, W / 2.0 }, { 0, -H / 2.0, 0, H / 2.0 },
        { 0, 0, FAR_END, 0 }, { 0, 0, 0, 1 },
    };
    double pm[4][4];
    int r, c, k;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            pm[r][c] = 0;
            for (k = 0; k < 4; k++) pm[r][c] += proj[r][k] * mv[k][c];
        }
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            double s = 0;
            for (k = 0; k < 4; k++) s += vp[r][k] * pm[k][c];
            M[r][c] = (float)s;
        }
}

/* The object position (w = 1) that M puts on screen x, y at clip w: rows 0,
 * 1 and 3 of M solved, in double, then rounded to float as a title's
 * vertices are. */
static void solve(double sx, double sy, double w, float out[4])
{
    const int rows[3] = { 0, 1, 3 };
    double a[3][4], rhs[3];
    int r, c, k;
    rhs[0] = sx * w - M[0][3];
    rhs[1] = sy * w - M[1][3];
    rhs[2] = w - M[3][3];
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) a[r][c] = M[rows[r]][c];
        a[r][3] = rhs[r];
    }
    for (k = 0; k < 3; k++)
        for (r = k + 1; r < 3; r++) {
            double f = a[r][k] / a[k][k];
            for (c = k; c < 4; c++) a[r][c] -= f * a[k][c];
        }
    for (k = 2; k >= 0; k--) {
        double s = a[k][3];
        for (c = k + 1; c < 3; c++) s -= a[k][c] * out[c];
        out[k] = (float)(s / a[k][k]);
    }
    out[3] = 1.0f;
}

typedef struct { int n; double mean, worst; int off; } Errors;

/* The points drawn by the emitted fixed-function program, their depth read
 * back and set against the exact z / w of the same float matrix and
 * vertices: how far off, in steps. */
static Errors ff_far_end(uint32_t rt, int revz, int precz)
{
    static const int attr_at[16] = { 0, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1 };
    static float pts[NX * NY][4];
    static double exact[NX * NY];
    static int px[NX * NY], py[NX * NY];
    static uint32_t steps[W * H];
    static char vs[96 * 1024];
    const float zc[2] = { 0.0f, (float)FAR_END }, surf[2] = { W, H }, vpoff[4] = { 0 },
                tint[4] = { 1, 1, 1, 1 };
    Nv2aVshFixed f;
    Nv2aRenderState rs;
    Errors e = { 0, 0, 0, 0 };
    float row[4];
    uint32_t prog, cleared;
    unsigned rng = 1;
    int i, c;

    reversed(revz);
    nv2a_glsl_set_precz(precz);
    memset(&f, 0, sizeof f);
    f.inputs_read = 1;
    check(nv2a_vsh_emit_ff_glsl(&f, vs, sizeof vs) > 0, "nv2a_vsh_emit_ff_glsl");
    check(!strstr(vs, "ffRevZ") == !(revz && precz), "ffRevZ only with the precise depth");
    prog = G->prog_new(vs, NV2A_GLSL_VERSION "uniform vec4 drawTint;\nout vec4 frag;\n"
                       "void main() { frag = drawTint; }\n", "test ff");
    check(prog != 0, "prog_new");

    for (i = 0; i < NX * NY; i++) {
        double w, pz = 0, pw = 0;
        rng = rng * 1103515245u + 12345u;
        w = W_MIN + (W_MAX - W_MIN) * ((rng >> 8) & 0xFFFF) / 65536.0;
        px[i] = GRID + (i % NX) * GRID;
        py[i] = GRID + (i / NX) * GRID;
        solve(px[i] + 0.5, py[i] + 0.5, w, pts[i]);
        for (c = 0; c < 4; c++) {
            pz += (double)M[2][c] * pts[i][c];
            pw += (double)M[3][c] * pts[i][c];
        }
        exact[i] = pz / pw;
    }
    /* ff_revz_row in nv2a_gl.c: far * row w - row z, over the range. */
    for (c = 0; c < 4; c++)
        row[c] = (float)((FAR_END * M[3][c] - M[2][c]) / FAR_END);

    G->target_bind(rt);
    G->clear(2, black, 0.0f, 0, NULL);   /* to the near end: no point is there */
    cleared = revz ? (uint32_t)FAR_END : 0;
    G->prog_use(prog);
    G->uniform_f(NV2A_U_ZCLIP, zc, 1);
    G->uniform_f(NV2A_U_VPSURFACE, surf, 1);
    G->uniform_f(NV2A_U_FFVPOFF, vpoff, 1);
    G->uniform_f(NV2A_U_FFMAT, &M[0][0], 4);
    if (G->prog_uses(prog, NV2A_U_FFREVZ))
        G->uniform_f(NV2A_U_FFREVZ, row, 1);
    G->uniform_f(NV2A_U_DRAWTINT, tint, 1);
    state(&rs);
    rs.depth_test = 1; rs.depth_write = 1; rs.depth_func = 0x207;   /* ALWAYS */
    G->state(&rs);
    G->vertices(&pts[0][0], NX * NY, 4, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_POINTS, NX * NY);
    read_steps(steps, W, H);
    for (i = 0; i < NX * NY; i++) {
        uint32_t got = steps[(H - 1 - py[i]) * W + px[i]];
        double z = revz ? FAR_END - got : got, err = fabs(z - exact[i]);
        if (got == cleared) check(0, "every point drawn");
        e.n++;
        e.mean += err;
        if (err > e.worst) e.worst = err;
        e.off += err >= 1.0;
    }
    e.mean /= e.n;
    G->prog_free(prog);
    check(1, "fixed-function at the far end");
    return e;
}

static void print_errors(const char *what, Errors e)
{
    printf("%s: %d points, mean %.3f steps off, worst %.3f, a step or more: %d\n",
           what, e.n, e.mean, e.worst, e.off);
}

int main(void)
{
    uint32_t rt;
    Errors fwd, rev, prec;
    Layers lf, lr;

    if (!gl_backend_context()) return 1;
    clip_control = (ClipControlFn)eglGetProcAddress("glClipControl");
    if (!clip_control) {
        fprintf(stderr, "FAIL no glClipControl\n");
        return 1;
    }
    G = nv2a_gfx_gl();
    check(G->init(), "init");

    backend_turned_round();

    rt = G->target_new(W, H);
    check(rt != 0, "target_new");

    lf = layers(rt, 0);
    lr = layers(rt, 1);
    printf("layers a step apart, of %d pixels: forward, %d behind shown and %d in front "
           "hidden; reversed, %d and %d\n", W * H, lf.behind_shown, lf.front_hidden,
           lr.behind_shown, lr.front_hidden);
    check(lr.behind_shown == 0, "reversed: the layer a step behind hidden everywhere");
    check(lr.front_hidden == 0, "reversed: the layer a step in front shown everywhere");

    composite();
    fwd = ff_far_end(rt, 0, 0);
    rev = ff_far_end(rt, 1, 0);
    prec = ff_far_end(rt, 1, 1);
    print_errors("fixed-function, forward          ", fwd);
    print_errors("fixed-function, reversed         ", rev);
    print_errors("fixed-function, reversed, precise", prec);
    check(prec.off == 0, "reversed and precise: every depth on a step either side of its own");

    G->target_free(rt);
    check(1, "target_free");
    printf("gl_backend: reversed depth passes\n");
    return 0;
}
