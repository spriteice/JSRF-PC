/* Run with: python tests/gl_backend/run.py
 *
 * The console driver's 0.53125-pixel nudge, taken off both paths alike
 * (nv2a_vsh_ff_vpoff and nv2a_vsh_vprect, src/nv2a_gl/nv2a_vsh.c). One
 * rectangle in the title's pixels, drawn the three ways the title draws,
 * through the shaders the engine emits and with the viewport uniforms the
 * engine sets, at 1x and at 3x, in the context every test here runs in
 * (context.h):
 *
 *   - by the fixed-function transform: the composite matrix carries the
 *     viewport, and the offset register its corner and the nudge;
 *   - by a vertex program that applies the viewport itself, as the title's
 *     do: position * c[58] + c[59], the viewport's scale and its offset with
 *     the nudge;
 *   - by D3D's pass-through program for pre-transformed vertices, which the
 *     title's 2D goes through: position + 0.53125, with the whole-surface
 *     viewport.
 *
 *   1. All three cover the same pixels. The garage's graffiti is drawn by a
 *      program onto walls the fixed-function transform draws, at the wall's
 *      own depth, so half a pixel between the two put the graffiti behind
 *      its wall.
 *   2. Those pixels are the rectangle's, whole pixels to pixel edges: a
 *      letterbox bar from (0, 0) covers the first row and column at 3x too.
 *   3. 1 and 2 in a viewport that is a window on the surface, as the
 *      character select's two are.
 */
#include "context.h"
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"
#include "nv2a_vsh.h"

#define FAR_END 16777215.0f
#define W 640
#define H 480

typedef struct { float ox, oy, sx, sy; } Viewport;   /* corner, scale */
typedef struct { int x0, y0, x1, y1; } Rect;          /* title pixels */
enum { FF, PROG, PASS };
static const char *const path_name[3] = { "fixed-function", "vertex program", "pass-through" };

static const Nv2aGfx *G;
static const float no_const[16][4];
static const float black[4] = { 0, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
static const Viewport whole = { 0, 0, W / 2.0f, -H / 2.0f };
static uint32_t prog[3];

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

/* src[0] (op) c[k] into the temporary or oPos. */
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

/* MUL R1, v0, c[a]; ADD oPos, R1, c[b]. */
static uint32_t scale_offset_prog(int a, int b, const char *what)
{
    static Nv2aVshProgram p;
    static char vs[96 * 1024];
    memset(&p, 0, sizeof p);
    insn(&p.insns[0], NV2A_MAC_MUL, NV2A_PARAM_V, 0, a, 0);
    insn(&p.insns[1], NV2A_MAC_ADD, NV2A_PARAM_R, 1, b, 1);
    p.length = 2;
    p.inputs_read = 1;
    check(nv2a_vsh_emit_glsl(&p, vs, sizeof vs) > 0, "nv2a_vsh_emit_glsl");
    return link(vs, what);
}

static void programs(void)
{
    static char vs[96 * 1024];
    Nv2aVshFixed f;
    memset(&f, 0, sizeof f);
    f.inputs_read = 1;
    check(nv2a_vsh_emit_ff_glsl(&f, vs, sizeof vs) > 0, "nv2a_vsh_emit_ff_glsl");
    prog[FF] = link(vs, "test ff");
    prog[PROG] = scale_offset_prog(58, 59, "test program");
    prog[PASS] = scale_offset_prog(0, 1, "test pass-through");
}

/* The viewport registers as the driver writes them for a vertex-program
 * draw: the scale, and the centre with the nudge. */
static void registers(const Viewport *v, float scale[4], float off[4])
{
    scale[0] = v->sx; scale[1] = v->sy; scale[2] = FAR_END; scale[3] = 0;
    off[0] = v->ox + v->sx + NV2A_D3D_NUDGE;
    off[1] = v->oy - v->sy + NV2A_D3D_NUDGE;
    off[2] = 0; off[3] = 0;
}

/* The rectangle drawn one way, white on black, into rt. */
static void draw(uint32_t rt, int path, const Viewport *v, const Rect *r)
{
    static const int attr_at[16] = { 0, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1 };
    static const int corner[6][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 },
                                      { 0, 0 }, { 1, 1 }, { 0, 1 } };
    static float c[192][4];
    const float surf[2] = { W, H }, zc[2] = { 0, FAR_END };
    const Viewport *vv = path == PASS ? &whole : v;
    float scale[4], off[4], u[4], pos[6][4];
    Nv2aRenderState rs;
    int i;

    registers(vv, scale, off);
    for (i = 0; i < 6; i++) {
        float x = (float)(corner[i][0] ? r->x1 : r->x0), y = (float)(corner[i][1] ? r->y1 : r->y0);
        if (path == PASS) { pos[i][0] = x; pos[i][1] = y; }
        else {                               /* the viewport's -1..1 */
            pos[i][0] = (x - v->ox - v->sx) / v->sx;
            pos[i][1] = (y - v->oy + v->sy) / v->sy;
        }
        pos[i][2] = 0; pos[i][3] = 1;
    }
    G->target_bind(rt);
    G->clear(7, black, 1.0f, 0, NULL);
    G->prog_use(prog[path]);
    G->uniform_f(NV2A_U_VPSURFACE, surf, 1);
    G->uniform_f(NV2A_U_ZCLIP, zc, 1);
    G->uniform_f(NV2A_U_DRAWTINT, white, 1);
    if (path == FF) {
        /* The viewport folded into the composite, relative to its corner,
         * and the offset register: the corner and the nudge. */
        const float m[4][4] = { { v->sx, 0, 0, v->sx }, { 0, v->sy, 0, -v->sy },
                                { 0, 0, FAR_END, 0 }, { 0, 0, 0, 1 } };
        const float reg[4] = { v->ox + NV2A_D3D_NUDGE, v->oy + NV2A_D3D_NUDGE, 0, 0 };
        G->uniform_f(NV2A_U_FFMAT, &m[0][0], 4);
        nv2a_vsh_ff_vpoff(reg, u);
        G->uniform_f(NV2A_U_FFVPOFF, u, 1);
    } else {
        memset(c, 0, sizeof c);
        if (path == PROG) { memcpy(c[58], scale, sizeof scale); c[58][3] = 1; memcpy(c[59], off, sizeof off); }
        else { c[0][0] = c[0][1] = c[0][3] = 1; c[0][2] = FAR_END; c[1][0] = c[1][1] = NV2A_D3D_NUDGE; }
        G->uniform_f(NV2A_U_C, &c[0][0], 192);
        G->uniform_f(NV2A_U_VPSCALE, scale, 1);
        G->uniform_f(NV2A_U_VPOFF, off, 1);
        nv2a_vsh_vprect(off, scale, W, H, u);
        G->uniform_f(NV2A_U_VPRECT, u, 1);
        G->uniform_i(NV2A_U_POSMODE, 0);
    }
    memset(&rs, 0, sizeof rs);
    rs.color_mask = 0x01010101u;
    G->state(&rs);
    G->vertices(&pos[0][0], 6, 4, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, 6);
}

/* Pixels where the drawing and the rectangle at scale s disagree, and the
 * first of them. */
static int wrong(uint32_t rt, int s, const Rect *r, int *wx, int *wy)
{
    static uint8_t px[W * 3 * H * 3 * 4];
    const int pw = W * s, ph = H * s;
    int x, y, n = 0;
    check(G->target_read(rt, 0, 0, (uint32_t)pw, (uint32_t)ph, 0, px), "target_read");
    for (y = 0; y < ph; y++)                        /* y down from the top */
        for (x = 0; x < pw; x++) {
            int drawn = px[((ph - 1 - y) * pw + x) * 4] > 127;
            int inside = x >= r->x0 * s && x < r->x1 * s && y >= r->y0 * s && y < r->y1 * s;
            if (drawn != inside && n++ == 0) { *wx = x; *wy = y; }
        }
    return n;
}

static void rectangle(const char *what, const Viewport *v, const Rect *r)
{
    int s, path;
    for (s = 1; s <= 3; s += 2) {
        uint32_t rt = G->target_new((uint32_t)(W * s), (uint32_t)(H * s));
        check(rt != 0, "target_new");
        for (path = FF; path <= PASS; path++) {
            int n, x = 0, y = 0;
            char msg[160];
            draw(rt, path, v, r);
            n = wrong(rt, s, r, &x, &y);
            if (n) fprintf(stderr, "  %s, %s, %dx: %d pixels wrong, the first at (%d, %d)\n",
                           what, path_name[path], s, n, x, y);
            snprintf(msg, sizeof msg, "%s at %dx by the %s path: exactly its pixels", what, s,
                     path_name[path]);
            check(n == 0, msg);
        }
        G->target_free(rt);
    }
    printf("%s (%d, %d)-(%d, %d): the same pixels by all three paths, at 1x and 3x\n",
           what, r->x0, r->y0, r->x1, r->y1);
}

int main(void)
{
    /* The character select's left window, as the title sets it. */
    static const Viewport window = { 52, 40, 149, -154 };
    static const Rect bar = { 0, 0, W, 60 }, inner = { 101, 37, 250, 203 },
                      in_window = { 60, 52, 301, 333 };

    if (!gl_backend_context()) return 1;
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    programs();
    rectangle("a letterbox bar", &whole, &bar);
    rectangle("a rectangle", &whole, &inner);
    rectangle("a rectangle in a window on the surface", &window, &in_window);
    return 0;
}
