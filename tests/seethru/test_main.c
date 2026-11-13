/*
 * The see-through fix (src/nv2a_gl/nv2a_seethru.c) on a device of the test's
 * own, which keeps what it has been given (the program in use, each
 * program's uniforms, the textures and how each is sampled, the state, the
 * vertices, the target) and
 * logs each draw as it would come out, with what it would be drawn with, and
 * the clears, binds and reads around them. Each case drives it as nv2a_gl.c's
 * do_draw_body and do_clear do, and checks the log and what the device holds
 * after.
 *
 * Argument: "hold" (the default build), "inplace" (run with
 * RECOMP_GL_SEETHRU_HOLD=0) or "census" (with RECOMP_GL_SEETHRU_LOG=1).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nv2a_seethru.h"

static int g_fail;

/* ---- the device ----------------------------------------------------------- */

#define NPROG 16
#define NTEX  64
#define NVB   64
#define LOGN  64

static struct {
    uint32_t prog;
    float    uf[NPROG][NV2A_U_COUNT][4];
    int      ui[NPROG][NV2A_U_COUNT];
    float    c[NPROG][NV2A_VSH_NUM_CONSTS][4];  /* the constants, whole */
    int      c_whole, c_part;                   /* how they were sent */
    uint32_t tex[4];
    int      wrap[4];               /* each unit's sampler's wrap on s; -1 the texture's own */
    int      opaque[NTEX];          /* each texture's own, as GL's swizzle */
    Nv2aRenderState rs;
    int      vk;                    /* 0 vertices(), 1 a buffer */
    float    v0;                    /* vertices(): the first float, copied */
    uint32_t vb;
    float    vbdata[NVB];           /* each buffer's first float */
    int      vbstream[NVB];
    uint32_t nvb;
    uint32_t rt;
    int      invalidated;
    char     log[LOGN][160];
    int      n;
} dev;

static void logf_(const char *s)
{
    if (dev.n < LOGN) snprintf(dev.log[dev.n++], sizeof dev.log[0], "%s", s);
}

static int per_of(int id)
{
    switch (nv2a_uniform_desc[id].type) {
    case NV2A_UT_VEC2: return 2;
    case NV2A_UT_VEC3: return 3;
    case NV2A_UT_VEC4: case NV2A_UT_MAT2: return 4;
    default: return 1;
    }
}

static void d_uniform_f(int id, const float *v, int n)
{
    int k, per = per_of(id);
    if (!dev.prog || n <= 0) return;
    if (id == NV2A_U_C) {
        memcpy(dev.c[dev.prog], v, (size_t)(n < NV2A_VSH_NUM_CONSTS ? n : NV2A_VSH_NUM_CONSTS) * 16);
        dev.c_whole++;
        return;
    }
    for (k = 0; k < per && k < 4; k++) dev.uf[dev.prog][id][k] = v[k];
}
static void d_uniform_i(int id, int v) { if (dev.prog) dev.ui[dev.prog][id] = v; }
static int  d_uniform_f_part(int id, int first, const float *v, int n)
{
    if (!dev.prog || id != NV2A_U_C || first < 0 || n <= 0 || first + n > NV2A_VSH_NUM_CONSTS) return 0;
    memcpy(dev.c[dev.prog][first], v, (size_t)n * 16);
    dev.c_part++;
    return 1;
}
static void d_prog_use(uint32_t p) { dev.prog = p; }
static void d_texture(int unit, uint32_t tex, const Nv2aSampler *s)
{
    dev.tex[unit] = tex;
    dev.wrap[unit] = s ? s->wrap_s : -1;
}
static void d_texture_opaque(int unit, int o) { dev.opaque[dev.tex[unit]] = o; }
static void d_state(const Nv2aRenderState *rs) { dev.rs = *rs; }
static void d_state_invalidate(void) { dev.invalidated++; }
static void d_vertices(const float *v, uint32_t nv, uint32_t stride, const int at[16], uint32_t cm,
                       const float ca[16][4])
{
    (void)nv; (void)stride; (void)at; (void)cm; (void)ca;
    dev.vk = 0;
    dev.v0 = v[0];
}
static uint32_t d_vbuf_new(int stream)
{
    uint32_t vb = ++dev.nvb;
    dev.vbstream[vb] = stream;
    return vb;
}
static void d_vbuf_free(uint32_t vb) { (void)vb; }
static void d_vbuf_fill(uint32_t vb, const float *v, uint32_t vbytes, const void *idx, uint32_t ibytes)
{
    (void)vbytes; (void)idx; (void)ibytes;
    if (v) dev.vbdata[vb] = v[0];
}
static void d_vbuf_bind(uint32_t vb, uint32_t stride, const int at[16], uint32_t cm, const float ca[16][4])
{
    (void)stride; (void)at; (void)cm; (void)ca;
    dev.vk = 1;
    dev.vb = vb;
}

/* A draw, as it would come out: its vertices (the case's marker), program,
 * tint, alpha test, depth write and function, blend source (- off), and
 * where they aren't nothing, its texture, fog, widescreen offset, the
 * stencil test (s) or a write mask alone (m), and target. */
static void d_draw_rec(uint32_t count, int indexed)
{
    char s[160], *e = s;
    uint32_t p = dev.prog, t = dev.tex[0];
    const Nv2aRenderState *rs = &dev.rs;
    e += sprintf(e, "v%g p%u t%g a%d/%g z%d/%X b", dev.vk ? dev.vbdata[dev.vb] : dev.v0, p,
                 dev.uf[p][NV2A_U_DRAWTINT][0], dev.ui[p][NV2A_U_ALPHAFUNC],
                 dev.uf[p][NV2A_U_ALPHAREF][0], rs->depth_write, rs->depth_func);
    if (rs->blend_enable) e += sprintf(e, "%X", rs->blend_src); else *e++ = '-', *e = 0;
    if (t) e += sprintf(e, " T%u", t);
    if (t && dev.wrap[0] > 0) e += sprintf(e, " W%d", dev.wrap[0]);
    if (t && dev.opaque[t]) e += sprintf(e, " o");
    if (dev.uf[p][NV2A_U_FOGCOLOR][0] != 0.0f) e += sprintf(e, " f%g", dev.uf[p][NV2A_U_FOGCOLOR][0]);
    if (dev.uf[p][NV2A_U_WIDEOFF][0] != 0.0f) e += sprintf(e, " w%g", dev.uf[p][NV2A_U_WIDEOFF][0]);
    if (dev.c[p][1][0] != 0.0f || dev.c[p][100][0] != 0.0f) e += sprintf(e, " c%g,%g", dev.c[p][1][0], dev.c[p][100][0]);
    if (rs->stencil_test) e += sprintf(e, " s");
    else if (rs->stencil_wmask) e += sprintf(e, " m");
    if (dev.rt != 1) e += sprintf(e, " rt%u", dev.rt);
    if (count != 3) e += sprintf(e, " n%u", count);
    if (indexed) e += sprintf(e, " i");
    logf_(s);
}
static void d_draw(int prim, uint32_t count) { (void)prim; d_draw_rec(count, 0); }
static void d_draw_indexed(int prim, uint32_t count, int i32) { (void)prim; (void)i32; d_draw_rec(count, 1); }

static void d_clear(unsigned mask, const float rgba[4], float depth, uint32_t st, const uint32_t rect[4])
{
    (void)rgba; (void)depth; (void)st; (void)rect;
    char s[32];
    snprintf(s, sizeof s, "CLEAR %u", mask);
    logf_(s);
}
static void d_target_bind(uint32_t rt)
{
    char s[32];
    dev.rt = rt;
    snprintf(s, sizeof s, "BIND %u", rt);
    logf_(s);
}
static void d_target_free(uint32_t rt) { (void)rt; logf_("TARGET FREE"); }
static uint32_t d_target_texture(uint32_t rt)
{
    char s[32];
    snprintf(s, sizeof s, "TEXTURE OF %u", rt);
    logf_(s);
    return 40 + rt;
}
static uint32_t d_target_snapshot(uint32_t rt) { (void)rt; logf_("SNAPSHOT"); return 50; }
static int d_target_read(uint32_t rt, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int bgra, void *out)
{
    (void)rt; (void)x; (void)y; (void)w; (void)h; (void)bgra; (void)out;
    logf_("READ");
    return 1;
}
static int d_target_read_depth(uint32_t rt, uint32_t pw, uint32_t ph, float *out)
{
    (void)rt; (void)pw; (void)ph; (void)out;
    return 0;
}
static void d_tex_free(uint32_t tex)
{
    char s[32];
    snprintf(s, sizeof s, "TEX FREE %u", tex);
    logf_(s);
}
static void d_prog_free(uint32_t p) { (void)p; }
static void d_present(uint32_t rt, uint32_t pw, uint32_t ph) { (void)rt; (void)pw; (void)ph; logf_("PRESENT"); }
static int  d_cache_idle(double ms) { (void)ms; logf_("IDLE"); return 0; }

static const Nv2aGfx k_dev = {
    .name = "test", .uniform_f = d_uniform_f, .uniform_i = d_uniform_i,
    .uniform_f_part = d_uniform_f_part, .prog_use = d_prog_use, .prog_free = d_prog_free,
    .texture = d_texture, .texture_opaque = d_texture_opaque, .state = d_state,
    .state_invalidate = d_state_invalidate, .vertices = d_vertices, .draw = d_draw,
    .draw_indexed = d_draw_indexed, .vbuf_new = d_vbuf_new, .vbuf_free = d_vbuf_free,
    .vbuf_fill = d_vbuf_fill, .vbuf_bind = d_vbuf_bind, .clear = d_clear,
    .target_bind = d_target_bind, .target_free = d_target_free, .target_texture = d_target_texture,
    .target_snapshot = d_target_snapshot, .target_read = d_target_read,
    .target_read_depth = d_target_read_depth, .tex_free = d_tex_free, .present = d_present,
    .cache_idle = d_cache_idle,
};

/* ---- the front half, as nv2a_gl.c draws ----------------------------------- */

static const Nv2aGfx *G;
static uint32_t g_frame = 1, g_draws, g_stream;
static const uint32_t k_surf = 0x1000;

/* What a case draws. */
typedef struct {
    float    v;                 /* its vertices' first float: which draw it is */
    int      vk;                /* 0 vertices(), 1 kept buffer `vb`, 2 the stream */
    uint32_t vb;
    uint32_t prog;
    float    tint, fog;
    int      af;
    float    ar;
    int      atest;
    uint32_t tex;
    int      opaque;            /* -1: not set */
    int      zt, zw, zf;
    int      src;               /* 0: no blending */
    int      stencil;           /* 1: the stencil test on; 2: a write mask alone, the test off */
    uint32_t pos, verts;
    int      textured;
    int      wide;              /* 1: a widescreen copy; 2: its offset set while held, put back after */
    int      reads, breaks;
    float    c1, c100;          /* constants 1 and 100, sent as the front half sends them */
    int      wrap;              /* how its texture is sampled: its wrap on s */
} Spec;

static Spec glass(float v)
{
    Spec s = { v, 0, 0, 1, 0.5f, 0, 4, 0, 1, 5, -1, 1, 1, 0x203, 0x302, 0, 0, 3, 1, 0, 0, 0, 0, 0, 0 };
    return s;
}
static Spec solid(float v)
{
    Spec s = { v, 0, 0, 2, 1, 0, 4, 0, 1, 6, -1, 1, 1, 0x203, 0, 0, 0, 3, 1, 0, 0, 0, 0, 0, 0 };
    return s;
}
static Spec hud(float v)
{
    Spec s = { v, 0, 0, 3, 1, 0, 4, 0, 1, 7, -1, 0, 0, 0x203, 0x302, 0, 0, 3, 1, 0, 0, 0, 0, 0, 0 };
    return s;
}

/* The front half's own: what it has sent each program (it sends a uniform
 * again only when it changes, as nv2a_gl.c does), the program in use, the
 * state, and its vertex memory, which the next draw writes over. */
static struct { int set; float tint, fog, ar, c1, c100; int af; } g_fh[NPROG];
static uint32_t g_fh_prog;
static Nv2aRenderState g_fh_rs;
static float g_fh_v[12], g_fh_sv[12];

static void fh_vertices(const Spec *s)
{
    static const int at[16] = { 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    static const float ca[16][4];
    int k;
    if (s->vk == 1) {
        G->vbuf_bind(s->vb, 4, at, 0, ca);
    } else if (s->vk == 2) {
        for (k = 0; k < 12; k++) g_fh_sv[k] = s->v;
        G->vbuf_fill(g_stream, g_fh_sv, sizeof g_fh_sv, NULL, 0);
        G->vbuf_bind(g_stream, 4, at, 0, ca);
    } else {
        for (k = 0; k < 12; k++) g_fh_v[k] = s->v;
        G->vertices(g_fh_v, 3, 4, at, 0, ca);
    }
}

static void uni1f(int id, float v) { G->uniform_f(id, &v, 1); }
static void uni4f(int id, float v) { float f[4] = { v, 0, 0, 1 }; G->uniform_f(id, f, 1); }

static void fh_draw_n(const Nv2aRenderState *rs, int n)
{
    G->draw(5, 3);
    if (n) {
        uni1f(NV2A_U_WIDEOFF, 1.0f);
        G->draw(5, 3);
        uni1f(NV2A_U_WIDEOFF, 0.0f);
        G->state(rs);
    }
}

/* One draw, as do_draw_body makes it: vertices, program, uniforms,
 * textures, state, then the fix's say, and the draw. Returns the say. */
static int fh_draw(Spec s)
{
    Nv2aRenderState rs, keep;
    Nv2aSeeDraw d;
    int how, held = 0;
    fh_vertices(&s);
    if (g_fh_prog != s.prog) { G->prog_use(s.prog); g_fh_prog = s.prog; }
    if (!g_fh[s.prog].set || g_fh[s.prog].tint != s.tint) { uni4f(NV2A_U_DRAWTINT, s.tint); g_fh[s.prog].tint = s.tint; }
    if (!g_fh[s.prog].set || g_fh[s.prog].fog != s.fog) {
        if (s.fog != 0.0f || g_fh[s.prog].set) uni4f(NV2A_U_FOGCOLOR, s.fog);
        g_fh[s.prog].fog = s.fog;
    }
    if (!g_fh[s.prog].set || g_fh[s.prog].af != s.af) { G->uniform_i(NV2A_U_ALPHAFUNC, s.af); g_fh[s.prog].af = s.af; }
    if (!g_fh[s.prog].set || g_fh[s.prog].ar != s.ar) { uni1f(NV2A_U_ALPHAREF, s.ar); g_fh[s.prog].ar = s.ar; }
    if (s.c1 != g_fh[s.prog].c1) {
        float c[4] = { s.c1, 0, 0, 0 };
        G->uniform_f_part(NV2A_U_C, 1, c, 1);
        g_fh[s.prog].c1 = s.c1;
    }
    if (s.c100 != g_fh[s.prog].c100) {
        float c[4] = { s.c100, 0, 0, 0 };
        G->uniform_f_part(NV2A_U_C, 100, c, 1);
        g_fh[s.prog].c100 = s.c100;
    }
    g_fh[s.prog].set = 1;
    if (s.reads) G->target_texture(dev.rt);
    {
        Nv2aSampler smp;
        memset(&smp, 0, sizeof smp);
        smp.wrap_s = (uint8_t)s.wrap;
        G->texture(0, s.tex, &smp);
    }
    if (s.opaque >= 0) G->texture_opaque(0, s.opaque);
    memset(&rs, 0, sizeof rs);
    rs.depth_test = s.zt; rs.depth_write = s.zw; rs.depth_func = s.zf;
    rs.blend_enable = s.src != 0; rs.blend_src = s.src ? s.src : 1; rs.blend_dst = s.src ? 0x303 : 0;
    rs.alpha_test = s.atest;
    rs.stencil_test = s.stencil == 1; rs.stencil_wmask = s.stencil ? 0xFF : 0;
    if (s.stencil == 1) {               /* where the stencil isn't 1: KEEP, KEEP, REPLACE with 1 */
        rs.stencil_func = 0x205; rs.stencil_ref = 1; rs.stencil_rmask = 0xFF;
        rs.stencil_op[0] = rs.stencil_op[1] = 0x1E00; rs.stencil_op[2] = 0x1E01;
    }
    G->state(&rs);
    g_fh_rs = rs;
    d.surf = k_surf; d.frame = g_frame; d.draw = g_draws++; d.pos = s.pos; d.verts = s.verts;
    d.slot = (int)s.prog; d.textured = (uint8_t)s.textured; d.skinned = 0; d.alpha_uniforms = 1;
    how = nv2a_seethru_draw(&rs, &d);
    keep = rs;
    if (how == NV2A_SEE_SPLIT) {
        G->uniform_i(NV2A_U_ALPHAFUNC, s.af | NV2A_PSH_ALPHA_OPAQUE);
        fh_draw_n(&rs, s.wide == 1);
        rs.depth_write = 0;
        G->state(&rs);
        G->uniform_i(NV2A_U_ALPHAFUNC, s.af | NV2A_PSH_ALPHA_SEE);
    }
    if (how != NV2A_SEE_KEEP) held = nv2a_seethru_hold_begin();
    if (s.breaks) G->texture(1, 9, NULL);
    if (s.wide == 2) uni1f(NV2A_U_WIDEOFF, 1.0f);
    fh_draw_n(&rs, s.wide == 1);
    if (held) nv2a_seethru_hold_end();
    if (s.wide == 2) uni1f(NV2A_U_WIDEOFF, 0.0f);
    if (how == NV2A_SEE_SPLIT) {
        G->uniform_i(NV2A_U_ALPHAFUNC, s.af);
        G->state(&keep);
    }
    return how;
}

/* do_clear: the device's, then the fix told of a depth clear. */
static void fh_clear(unsigned mask)
{
    float c[4] = { 0, 0, 0, 0 };
    G->clear(mask, c, 1.0f, 0, NULL);
    if (mask & 2u) nv2a_seethru_depth_clear(k_surf, g_frame);
}

/* ---- checks ---------------------------------------------------------------- */

static void start(const char *name)
{
    fprintf(stderr, "case %s\n", name);
    g_frame += 10;
    dev.n = 0;
}

/* The log since start(), lines joined by " | ", is `want`. */
static void expect_log(const char *name, const char *want)
{
    char got[LOGN * 170] = "";
    int i;
    for (i = 0; i < dev.n; i++) {
        if (i) strcat(got, " | ");
        strcat(got, dev.log[i]);
    }
    if (strcmp(got, want)) {
        fprintf(stderr, "FAIL %s:\n  got:  %s\n  want: %s\n", name, got, want);
        g_fail = 1;
    }
    dev.n = 0;
}

static void expect_how(const char *name, int got, int want)
{
    static const char *const k[] = { "keep", "split", "hold" };
    if (got != want) {
        fprintf(stderr, "FAIL %s: %s, not %s\n", name, k[got], k[want]);
        g_fail = 1;
    }
}

/* What the device holds is what the front half last gave it: the program,
 * each program's uniforms, the texture, the state. */
static void expect_device(const char *name)
{
    uint32_t p;
    for (p = 1; p < NPROG; p++) {
        if (!g_fh[p].set) continue;
        if (dev.uf[p][NV2A_U_DRAWTINT][0] != g_fh[p].tint || dev.uf[p][NV2A_U_FOGCOLOR][0] != g_fh[p].fog
            || dev.ui[p][NV2A_U_ALPHAFUNC] != g_fh[p].af || dev.uf[p][NV2A_U_ALPHAREF][0] != g_fh[p].ar
            || dev.uf[p][NV2A_U_WIDEOFF][0] != 0.0f
            || dev.c[p][1][0] != g_fh[p].c1 || dev.c[p][100][0] != g_fh[p].c100) {
            fprintf(stderr, "FAIL %s: program %u's uniforms aren't the front half's: tint %g fog %g alpha %d/%g"
                    " wide %g\n", name, p, dev.uf[p][NV2A_U_DRAWTINT][0], dev.uf[p][NV2A_U_FOGCOLOR][0],
                    dev.ui[p][NV2A_U_ALPHAFUNC], dev.uf[p][NV2A_U_ALPHAREF][0], dev.uf[p][NV2A_U_WIDEOFF][0]);
            g_fail = 1;
        }
    }
    if (dev.prog != g_fh_prog || memcmp(&dev.rs, &g_fh_rs, sizeof dev.rs)) {
        fprintf(stderr, "FAIL %s: the device's program (%u) or state isn't the front half's (%u)\n", name,
                dev.prog, g_fh_prog);
        g_fail = 1;
    }
}

/* ---- the cases ------------------------------------------------------------- */

static void cases_hold(void)
{
    Spec s;
    uint32_t kept;

    /* Glass, then what is behind it: the glass's depth where it is opaque,
     * what is behind it, then its colour over that at the clear. Its
     * vertices were copied: the front half has written over them since. */
    start("split");
    G->target_bind(1);
    expect_how("split: the glass", fh_draw(glass(1)), NV2A_SEE_SPLIT);
    expect_how("split: behind it", fh_draw(solid(2)), NV2A_SEE_KEEP);
    fh_clear(3);
    expect_log("split", "BIND 1"
               " | v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v2 p2 t1 a4/0 z1/203 b- T6"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | CLEAR 3");
    expect_device("split");

    /* Each texture as its own draw samples it: the glass's colour, drawn at
     * the clear, wraps as the glass asked, not as the wall drawn since. */
    start("samplers");
    s = glass(1); s.wrap = 1;
    fh_draw(s);
    s = solid(2); s.wrap = 2;
    fh_draw(s);
    fh_clear(3);
    expect_log("samplers", "v1 p1 t0.5 a20/0 z1/203 b302 T5 W1"
               " | v2 p2 t1 a4/0 z1/203 b- T6 W2"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5 W1"
               " | CLEAR 3");
    expect_device("samplers");
    if (dev.tex[0] != 6 || dev.wrap[0] != 2) {
        fprintf(stderr, "FAIL samplers: unit 0 holds %u, wrapped %d, not the wall's 6, wrapped 2\n",
                dev.tex[0], dev.wrap[0]);
        g_fail = 1;
    }

    /* Each half with the depth function the title asked for: the pixels
     * are the one's or the other's. */
    start("depth functions");
    s = glass(1); s.zf = 0x201;
    fh_draw(s);
    s = glass(2); s.zf = 0x204;
    fh_draw(s);
    s = glass(3); s.zf = 0x202;     /* EQUAL: kept whole */
    expect_how("depth functions: EQUAL", fh_draw(s), NV2A_SEE_KEEP);
    fh_clear(1);
    expect_log("depth functions", "v1 p1 t0.5 a20/0 z1/201 b302 T5"
               " | v2 p1 t0.5 a20/0 z1/204 b302 T5"
               " | v3 p1 t0.5 a4/0 z1/202 b302 T5"
               " | v1 p1 t0.5 a36/0 z0/201 b302 T5"
               " | v2 p1 t0.5 a36/0 z0/204 b302 T5"
               " | CLEAR 1");

    /* Held whole after it: what writes no depth (the HUD), in order; what
     * writes depth, in place. */
    start("held whole");
    fh_draw(glass(1));
    expect_how("held whole: the HUD", fh_draw(hud(3)), NV2A_SEE_HOLD);
    expect_how("held whole: a wall", fh_draw(solid(2)), NV2A_SEE_KEEP);
    s = hud(5); s.tint = 0.25f;
    expect_how("held whole: more HUD", fh_draw(s), NV2A_SEE_HOLD);
    G->present(1, 640, 480);
    expect_log("held whole", "v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v2 p2 t1 a4/0 z1/203 b- T6"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | v3 p3 t1 a4/0 z0/203 b302 T7"
               " | v5 p3 t0.25 a4/0 z0/203 b302 T7"
               " | PRESENT");
    expect_device("held whole");

    /* What uses the stencil, the test on, and writes no depth (a shadow):
     * drawn in place, as the title draws it, with the list still held, so
     * the glass's colour goes over the shadow; and what comes after it is
     * held back as before. A write mask alone, with the test off, writes
     * nothing and doesn't count (seethru5 counted it, and at 99th Street
     * nearly every draw has one). A see-through draw that writes depth and
     * uses the stencil is split as any other (seethru6 kept it whole, and
     * the tape behind the water was hidden), its see-through half held back
     * with its stencil state. */
    start("stencil");
    fh_draw(glass(1));
    fh_draw(hud(3));
    s = solid(4); s.zw = 0; s.src = 0x302; s.stencil = 1; s.prog = 3;
    expect_how("stencil: a shadow", fh_draw(s), NV2A_SEE_KEEP);
    s = hud(5); s.stencil = 2;
    expect_how("stencil: a write mask alone", fh_draw(s), NV2A_SEE_HOLD);
    s = glass(6); s.stencil = 1;
    expect_how("stencil: see-through, writing depth", fh_draw(s), NV2A_SEE_SPLIT);
    s = glass(7); s.stencil = 2;
    expect_how("stencil: see-through, a write mask alone", fh_draw(s), NV2A_SEE_SPLIT);
    G->present(1, 640, 480);
    expect_log("stencil", "v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v4 p3 t1 a4/0 z0/203 b302 T6 s"
               " | v6 p1 t0.5 a20/0 z1/203 b302 T5 s"
               " | v7 p1 t0.5 a20/0 z1/203 b302 T5 m"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | v3 p3 t1 a4/0 z0/203 b302 T7"
               " | v5 p3 t1 a4/0 z0/203 b302 T7 m"
               " | v6 p1 t0.5 a36/0 z0/203 b302 T5 s"
               " | v7 p1 t0.5 a36/0 z0/203 b302 T5 m"
               " | PRESENT");
    expect_device("stencil");

    /* A see-through draw that writes depth with no alpha test (the speed
     * bar's cut): the list drawn before it, so its depth doesn't cut what is
     * held. It shares the glass's program, with its own tint, which is put
     * back for it after the glass's is drawn. */
    start("cutter");
    fh_draw(glass(1));
    s = glass(8); s.atest = 0; s.tint = 0.25f; s.tex = 6;
    expect_how("cutter", fh_draw(s), NV2A_SEE_KEEP);
    expect_log("cutter", "v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | v8 p1 t0.25 a4/0 z1/203 b302 T6");
    expect_device("cutter");
    fh_clear(3);
    expect_log("cutter: nothing left", "CLEAR 3");

    /* A uniform the glass's program was first given after the glass was
     * held: 0 for the glass, as it was, and back after. */
    start("uniforms");
    s = glass(1); s.prog = 4;
    fh_draw(s);
    s = solid(2); s.prog = 4; s.tint = 0.75f; s.fog = 0.25f;
    fh_draw(s);
    fh_clear(3);
    fh_draw(s);
    expect_log("uniforms", "v1 p4 t0.5 a20/0 z1/203 b302 T5"
               " | v2 p4 t0.75 a4/0 z1/203 b- T6 f0.25"
               " | v1 p4 t0.5 a36/0 z0/203 b302 T5"
               " | CLEAR 3"
               " | v2 p4 t0.75 a4/0 z1/203 b- T6 f0.25");
    expect_device("uniforms");

    /* The constants: a draw's own in the list, and back after; the run of
     * them that differs is all that is sent again. */
    start("constants");
    s = glass(1); s.c1 = 1; s.c100 = 2;
    fh_draw(s);
    s = solid(2); s.prog = 1; s.c1 = 3; s.c100 = 2;
    fh_draw(s);
    dev.c_whole = dev.c_part = 0;
    fh_clear(3);
    fh_draw(s);
    expect_log("constants", "v1 p1 t0.5 a20/0 z1/203 b302 T5 c1,2"
               " | v2 p1 t1 a4/0 z1/203 b- T6 c3,2"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5 c1,2"
               " | CLEAR 3"
               " | v2 p1 t1 a4/0 z1/203 b- T6 c3,2");
    expect_device("constants");
    if (dev.c_whole || dev.c_part != 2) {
        fprintf(stderr, "FAIL constants: sent whole %d times and in part %d, not 0 and 2\n",
                dev.c_whole, dev.c_part);
        g_fail = 1;
    }
    /* (Back to 0, for the cases after.) */
    s = solid(2); s.prog = 1;
    fh_draw(s);
    dev.n = 0;

    /* The vertices: a kept buffer by itself, the stream's by a copy (the
     * stream is filled again for the next draw), and the list drawn before
     * a kept buffer it uses is filled again. */
    start("vertices");
    kept = G->vbuf_new(0);
    g_stream = G->vbuf_new(1);
    {
        float k10[12] = { 10 };
        G->vbuf_fill(kept, k10, sizeof k10, NULL, 0);
    }
    s = glass(1); s.vk = 1; s.vb = kept; s.v = 10;
    fh_draw(s);
    s = glass(11); s.vk = 2;
    fh_draw(s);
    s = solid(12); s.vk = 2;
    fh_draw(s);
    {
        float k13[12] = { 13 };
        G->vbuf_fill(kept, k13, sizeof k13, NULL, 0);
    }
    fh_clear(3);
    expect_log("vertices", "v10 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v11 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v12 p2 t1 a4/0 z1/203 b- T6"
               " | v10 p1 t0.5 a36/0 z0/203 b302 T5"
               " | v11 p1 t0.5 a36/0 z0/203 b302 T5"
               " | CLEAR 3");

    /* What draws the list: a change of surface (not the same one again),
     * the surface read, a texture it uses freed (not another), the device's
     * own work. */
    start("flushes");
    fh_draw(glass(1));
    G->target_bind(1);
    G->target_bind(2);
    expect_log("flushes: surface", "v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | BIND 1"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | BIND 2");
    G->target_bind(1);
    fh_draw(glass(1));
    G->target_snapshot(1);
    expect_log("flushes: snapshot", "BIND 1"
               " | v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | SNAPSHOT");
    /* (A snapshot is taken for the draw being made, which it marks as
     * reading its surface: a draw for it.) */
    fh_draw(solid(2));
    dev.n = 0;
    fh_draw(glass(1));
    G->tex_free(9);
    G->tex_free(5);
    expect_log("flushes: texture freed", "v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | TEX FREE 9"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | TEX FREE 5");
    fh_draw(glass(1));
    G->cache_idle(1.0);
    expect_log("flushes: the device's work", "v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | IDLE");
    fh_draw(glass(1));
    G->target_read(1, 0, 0, 1, 1, 0, NULL);
    expect_log("flushes: read", "v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | READ");
    expect_device("flushes");

    /* A draw that reads the surface it draws to: the list drawn before the
     * read, and the draw kept whole. */
    start("reads");
    fh_draw(glass(1));
    s = glass(5); s.reads = 1;
    expect_how("reads", fh_draw(s), NV2A_SEE_KEEP);
    expect_log("reads", "v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | TEXTURE OF 1"
               " | v5 p1 t0.5 a4/0 z1/203 b302 T5");

    /* The widescreen copies, and a texture's alpha as 1, held with it. */
    start("widescreen");
    s = glass(1); s.wide = 1; s.opaque = 1;
    fh_draw(s);
    s = solid(2); s.tex = 5; s.opaque = 0;
    fh_draw(s);
    fh_clear(3);
    expect_log("widescreen", "v1 p1 t0.5 a20/0 z1/203 b302 T5 o"
               " | v1 p1 t0.5 a20/0 z1/203 b302 T5 o w1"
               " | v2 p2 t1 a4/0 z1/203 b- T5"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5 o"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5 o w1"
               " | CLEAR 3");
    expect_device("widescreen");
    if (dev.opaque[5] != 0) {
        fprintf(stderr, "FAIL widescreen: texture 5's alpha left as 1\n");
        g_fail = 1;
    }
    /* A uniform set while a draw is held, and set back after: back after
     * the list is drawn too. (A new frame: the clear above was over the
     * picture.) */
    g_frame++;
    fh_draw(glass(1));
    s = hud(3); s.wide = 2;
    fh_draw(s);
    fh_clear(3);
    expect_log("widescreen: set while held", "v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | v3 p3 t1 a4/0 z0/203 b302 T7 w1"
               " | CLEAR 3");
    expect_device("widescreen: set while held");

    /* Over the finished picture: after a depth clear of a surface the frame
     * has drawn to, kept whole; a clear before anything is drawn isn't
     * that; the next frame starts again. */
    start("over the picture");
    fh_clear(2);
    expect_how("over: first", fh_draw(glass(1)), NV2A_SEE_SPLIT);
    fh_clear(2);
    expect_how("over: after the clear", fh_draw(glass(2)), NV2A_SEE_KEEP);
    g_frame++;
    expect_how("over: next frame", fh_draw(glass(3)), NV2A_SEE_SPLIT);
    fh_clear(3);
    dev.n = 0;

    /* Outlines: five pieces, then an untextured draw with as many vertices
     * as they have together: kept whole, and the pieces from then on. */
    start("outlines");
    {
        static const uint32_t verts[5] = { 984, 1386, 558, 222, 312 };
        int k;
        for (k = 0; k < 5; k++) {
            s = glass(1); s.pos = 0x100 + 0x10 * (uint32_t)k; s.verts = verts[k];
            expect_how("outlines: a piece, not yet known", fh_draw(s), NV2A_SEE_SPLIT);
        }
        s = glass(2); s.pos = 0x200; s.verts = 3462; s.textured = 0;
        expect_how("outlines: the outline", fh_draw(s), NV2A_SEE_KEEP);
        fh_clear(3);
        g_frame++;
        for (k = 0; k < 5; k++) {
            s = glass(1); s.pos = 0x100 + 0x10 * (uint32_t)k; s.verts = verts[k];
            expect_how("outlines: a piece", fh_draw(s), NV2A_SEE_KEEP);
        }
        s = glass(1); s.pos = 0x300; s.verts = 984;
        expect_how("outlines: another mesh", fh_draw(s), NV2A_SEE_SPLIT);
        s = glass(2); s.pos = 0x400; s.verts = 980; s.textured = 0;
        expect_how("outlines: untextured, not the sum", fh_draw(s), NV2A_SEE_SPLIT);
        fh_clear(3);
        dev.n = 0;
    }

    /* A call that has no place in a held draw: the rest of it drawn in
     * place, said once. */
    start("break");
    fh_draw(glass(1));
    s = hud(3); s.breaks = 1;
    fh_draw(s);
    fh_clear(3);
    expect_log("break", "v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v3 p3 t1 a4/0 z0/203 b302 T7"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | CLEAR 3");
    G->texture(1, 0, NULL);

    /* Off: as the title draws them. */
    start("off");
    nv2a_seethru_set(0);
    expect_how("off: glass", fh_draw(glass(1)), NV2A_SEE_KEEP);
    expect_how("off: HUD", fh_draw(hud(3)), NV2A_SEE_KEEP);
    fh_clear(3);
    nv2a_seethru_set(1);
    expect_log("off", "v1 p1 t0.5 a4/0 z1/203 b302 T5"
               " | v3 p3 t1 a4/0 z0/203 b302 T7"
               " | CLEAR 3");
}

/* RECOMP_GL_SEETHRU_HOLD=0: split, the colour drawn in place. */
static void cases_inplace(void)
{
    start("in place");
    G->target_bind(1);
    expect_how("in place: the glass", fh_draw(glass(1)), NV2A_SEE_SPLIT);
    expect_how("in place: the HUD", fh_draw(hud(3)), NV2A_SEE_KEEP);
    fh_clear(3);
    expect_log("in place", "BIND 1"
               " | v1 p1 t0.5 a20/0 z1/203 b302 T5"
               " | v1 p1 t0.5 a36/0 z0/203 b302 T5"
               " | v3 p3 t1 a4/0 z0/203 b302 T7"
               " | CLEAR 3");
    expect_device("in place");
}

/* RECOMP_GL_SEETHRU_LOG=1: frame 600 recorded, and said when 601 starts. */
static void cases_census(void)
{
    Spec s;
    G->target_bind(1);
    g_frame = 600;
    fh_draw(solid(2));
    fh_draw(glass(1));
    s = hud(3); s.stencil = 2;          /* a write mask alone: not marked */
    fh_draw(s);
    s = glass(4); s.stencil = 1;
    fh_draw(s);
    fh_clear(3);
    g_frame = 601;
    fh_draw(solid(2));
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "hold";
    /* The shaders' threshold as they write it is the renderer's. */
    if (strtof(NV2A_PSH_OPAQUE_GLSL, NULL) != NV2A_PSH_OPAQUE) {
        fprintf(stderr, "FAIL NV2A_PSH_OPAQUE_GLSL (%s) isn't NV2A_PSH_OPAQUE (%g)\n",
                NV2A_PSH_OPAQUE_GLSL, NV2A_PSH_OPAQUE);
        return 1;
    }
    /* Unset, the setting is its default, File > Presets' JSRF-PC default and
     * Max quality (nv2a_gl.c's row). */
    if (nv2a_seethru_on() != NV2A_SEE_DEFAULT || NV2A_SEE_DEFAULT != 1) {
        fprintf(stderr, "FAIL the setting starts %d, not on (NV2A_SEE_DEFAULT %d)\n",
                nv2a_seethru_on(), NV2A_SEE_DEFAULT);
        return 1;
    }
    G = nv2a_seethru_wrap(&k_dev);
    if (!strcmp(mode, "inplace")) cases_inplace();
    else if (!strcmp(mode, "census")) cases_census();
    else cases_hold();
    if (g_fail) return 1;
    fprintf(stderr, "seethru: all cases pass (%s)\n", mode);
    return 0;
}
