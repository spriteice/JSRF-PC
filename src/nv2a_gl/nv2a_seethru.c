/*
 * See-through surfaces stop hiding what is behind them: which draws are
 * split, kept whole or held back, the held-back list, and its census
 * (nv2a_seethru.h).
 *
 * b2.1 row 9. Test builds 22a5376-seethru1 to -seethru4 found the cause and
 * which draws to leave alone (the shared folder's see-through/DIAGNOSIS.md):
 *
 *   - The title draws nearly everything blended, with depth writes and the
 *     alpha test at 0, the world and the characters too (seethru1's census:
 *     774 of a frame's 1,639 draws, 772 alpha-tested), so a draw's state
 *     can't tell glass from a wall. What can is the alpha its pixels end up
 *     with: a split draw writes depth only where it is opaque (seethru2).
 *   - A character, or the level's arrow, gets its dark outline from a
 *     slightly larger copy of its mesh drawn right after it, untextured,
 *     with as many vertices as the draws it outlines together (seethru2's
 *     census: program 909's 3,462 after five draws of 984, 1,386, 558, 222
 *     and 312). The body's depth, faded pixels too, hides the inside of that
 *     copy, so split, a faded body filled with it, black and brown. Those
 *     pieces are learnt and kept whole (seethru3): faded characters keep
 *     hiding what is behind them, as on the console (spriteice's choice, 12:45
 *     UTC on 2026-11-10).
 *   - In every frame of play, two draws are see-through and write depth
 *     with no alpha test, so they write it where they are invisible: that
 *     depth cuts the speed bar's rectangle into its triangle. Split, the
 *     bar showed whole. Draws without the alpha test are kept whole
 *     (seethru4).
 *   - The title draws its logo over the finished picture in 18 layers, after
 *     a third depth clear, and its front layers hide its back ones by depth
 *     as it fades; menus and fades are drawn the same way. Draws after a
 *     depth clear of a surface the frame has already drawn to with the depth
 *     test are kept whole (seethru4).
 *
 * seethru4 drew a split draw's see-through pixels in place, with no depth
 * writes, so what was drawn after them and behind them showed, but over
 * them, without the glass's colour: the crowd untinted (spriteice, 12:37 UTC on
 * 2026-11-10: "their colour is not tinted, something we would need to
 * add"). Here they are held back to the pass's end instead (spriteice's choice on
 * 2026-11-11). seethru2 to 4 drew a split draw's depth first, with no
 * colour, then all its colour; held back, that colour would come after the
 * shadows and the faded characters drawn in place, and over them. So here
 * the two halves are its pixels, by the shaders' alpha test (nv2a_psh.h):
 * the opaque ones as the title draws them, in place, and only the
 * see-through ones held back.
 *
 * seethru5 drew the list before any draw using the stencil, meant for the
 * shadows, and counted a stencil write mask as using it; but the title
 * leaves one set on nearly every draw, with the stencil test off, so it
 * writes nothing (seethru5's census at 99th Street: the list drawn four
 * times a frame, at the two shadows and at two draws with only the mask).
 * The level's tape behind the water was drawn after one of those, over the
 * water, untinted (spriteice, 10:15 UTC on 2026-11-11). Only the stencil test
 * counts now, and a draw using it that isn't split (a shadow) is drawn in
 * place without drawing the list. seethru6 also kept a see-through draw
 * using the stencil test whole, and the tape behind the water was hidden
 * again (spriteice, 11:47 UTC), as it is behind water kept whole. Such a draw is
 * split as any other now: its see-through half is held back with its
 * stencil state, and tests what the stencil holds at the pass's end, after
 * any shadow drawn since. The census gives each run's stencil state.
 */
#include "nv2a_seethru.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Blend factors and depth functions as the title writes them. */
#define SEE_ZERO        0x0000
#define SEE_LESS        0x0201
#define SEE_LEQUAL      0x0203
#define SEE_GREATER     0x0204
#define SEE_GEQUAL      0x0206
#define SEE_ALWAYS      0x0207

#define SEE_UNITS 4             /* texture stages */

static const Nv2aGfx *D;        /* the device */
static Nv2aGfx        W;        /* and the wrapper round it */
static volatile int   g_on = NV2A_SEE_DEFAULT;

static int env_is(const char *name, const char *want)
{
    const char *v = getenv(name);
    return v && !strcmp(v, want);
}

/* RECOMP_GL_SEETHRU_HOLD=0: split draws' see-through pixels in place. */
static int hold_on(void)
{
    static int on = -1;
    if (on < 0) on = !env_is("RECOMP_GL_SEETHRU_HOLD", "0");
    return on;
}

void nv2a_seethru_set(int on) { g_on = on != 0; }
int  nv2a_seethru_on(void)    { return g_on; }

/* ---- the census (RECOMP_GL_SEETHRU_LOG=1) --------------------------------- */

enum { CK_SOLID, CK_SEE_Z, CK_SEE, CK_UNTESTED, CK_CLEAR, CK_FLUSH };
/* Why a draw went as it did. */
enum { WHY_PLAIN, WHY_SPLIT, WHY_SPLIT_INPLACE, WHY_HELD, WHY_PIECE, WHY_OUTLINE,
       WHY_NO_ATEST, WHY_OVER, WHY_READS, WHY_FUNC, WHY_OFF, WHY_CUT, WHYS };
static const char *const k_why[WHYS] = {
    "", ", split", ", split, drawn in place", ", held back", ", kept whole for an outline",
    ", an outline", ", kept whole: no alpha test", ", kept whole: over the finished picture",
    ", kept whole: reads its surface", ", kept whole: its depth function",
    ", kept whole: the fix is off", ", drawn after what is held back: writes depth everywhere" };
/* What drew the held-back list. */
enum { FL_CLEAR, FL_SURFACE, FL_READ, FL_FLIP, FL_CUT, FL_FREE, FL_OTHER, FLS };
static const char *const k_fl[FLS] = {
    "a clear", "a change of surface", "the surface read", "the flip", "a draw writing depth everywhere",
    "something held back freed or refilled", "the device's other work" };

typedef struct {
    uint32_t draw, surf, verts;
    int      slot;
    uint16_t src, dst, func;
    uint8_t  kind, why, skinned, textured, atest, stencil;
    /* With the stencil test on: its function, reference, read and write
     * masks, and operations (fail, z-fail, z-pass). */
    uint16_t sfunc, sop[3];
    uint8_t  sref, srmask, swmask;
} CRec;
#define C_RECS 8192
#define C_RUNS 150
static int      g_c_on = -1;
static CRec    *g_c;
static int      g_c_n;
static uint32_t g_c_lost, g_c_frame = 0xFFFFFFFFu, g_c_done = 0xFFFFFFFFu;
static uint32_t g_c_bytes, g_c_redrawn;

static int census_on(void)
{
    if (g_c_on < 0) {
        g_c_on = env_is("RECOMP_GL_SEETHRU_LOG", "1");
        if (g_c_on && !(g_c = (CRec *)malloc(C_RECS * sizeof *g_c))) g_c_on = 0;
        if (g_c_on)
            fprintf(stderr, "  [SEETHRU] census on: one whole frame in %u\n", NV2A_SEE_EVERY);
    }
    return g_c_on;
}

static void census_report(void);

/* The frame turning over: the one recorded is reported, and every
 * NV2A_SEE_EVERY-th one is recorded. */
static void census_turn(uint32_t frame)
{
    if (!census_on()) return;
    if (g_c_frame != 0xFFFFFFFFu && frame != g_c_frame) {
        census_report();
        g_c_done = g_c_frame;
        g_c_frame = 0xFFFFFFFFu;
    }
    if (g_c_frame == 0xFFFFFFFFu && frame % NV2A_SEE_EVERY == 0 && frame != g_c_done) {
        g_c_frame = frame;
        g_c_n = 0;
        g_c_lost = g_c_bytes = g_c_redrawn = 0;
    }
}

static CRec *census_rec(void)
{
    if (g_c_on <= 0 || g_c_frame == 0xFFFFFFFFu) return NULL;
    if (g_c_n >= C_RECS) { g_c_lost++; return NULL; }
    memset(&g_c[g_c_n], 0, sizeof g_c[0]);
    return &g_c[g_c_n++];
}

/* The draw recorded last, to say later what became of it. */
static CRec *g_c_last;

static void census_report(void)
{
    int i, j, k, runs = 0;
    int n_draws = 0, n_see_z = 0, n_split = 0, n_inplace = 0, n_split_st = 0, n_held = 0, n_kept[WHYS];
    int n_fl[FLS], n_flushes = 0;
    memset(n_kept, 0, sizeof n_kept);
    memset(n_fl, 0, sizeof n_fl);
    for (i = 0; i < g_c_n; i++) {
        const CRec *r = &g_c[i];
        if (r->kind == CK_FLUSH) { n_fl[r->why]++; n_flushes++; continue; }
        if (r->kind == CK_CLEAR) continue;
        n_draws++;
        n_see_z += r->kind == CK_SEE_Z;
        n_split += r->why == WHY_SPLIT || r->why == WHY_SPLIT_INPLACE;
        n_inplace += r->why == WHY_SPLIT_INPLACE;
        n_split_st += (r->why == WHY_SPLIT || r->why == WHY_SPLIT_INPLACE) && r->stencil;
        n_held += r->why == WHY_HELD;
        if (r->kind == CK_SEE_Z) n_kept[r->why]++;
    }
    fprintf(stderr, "  [SEETHRU] frame %u%s: %d draws, %d see-through with depth writes: %d split"
            " (%d of them drawn in place, %d using the stencil), kept whole %d for an outline,"
            " %d outlines, %d without the alpha test, %d over the finished picture, %d reading"
            " their surface, %d for their depth function, %d with the fix off; %d more held back"
            " whole; %d held-back lists drawn (", g_c_frame,
            g_c_lost ? " (longer than the record)" : "", n_draws, n_see_z, n_split, n_inplace,
            n_split_st, n_kept[WHY_PIECE], n_kept[WHY_OUTLINE], n_kept[WHY_NO_ATEST] + n_kept[WHY_CUT],
            n_kept[WHY_OVER], n_kept[WHY_READS], n_kept[WHY_FUNC], n_kept[WHY_OFF], n_held,
            n_flushes);
    for (i = 0, k = 0; i < FLS; i++)
        if (n_fl[i]) fprintf(stderr, "%s%d at %s", k++ ? ", " : "", n_fl[i], k_fl[i]);
    fprintf(stderr, "), %u draws drawn again, %u KB kept for them\n", g_c_redrawn,
            (g_c_bytes + 1023) / 1024);
    /* The frame in order, as runs of draws next to each other in one surface
     * of the same kind, skinned or not, gone the same way, with the same
     * stencil state; for see-through ones, the same blend, alpha test and
     * depth function too. */
    for (i = 0; i < g_c_n && runs < C_RUNS; i = j) {
        const CRec *r = &g_c[i];
        int see = r->kind == CK_SEE_Z || r->kind == CK_SEE, nslot = 0, more = 0, slots[4];
        uint32_t verts = 0, tex = 0;
        runs++;
        if (r->kind == CK_CLEAR) {
            fprintf(stderr, "  [SEETHRU]   at draw %u: depth clear, surface %08X\n", r->draw, r->surf);
            j = i + 1;
            continue;
        }
        if (r->kind == CK_FLUSH) {
            fprintf(stderr, "  [SEETHRU]   at draw %u: %u held back drawn, at %s\n", r->draw, r->verts,
                    k_fl[r->why]);
            j = i + 1;
            continue;
        }
        for (j = i; j < g_c_n; j++) {
            const CRec *q = &g_c[j];
            if (q->kind != r->kind || q->surf != r->surf || q->skinned != r->skinned || q->why != r->why)
                break;
            if (q->stencil != r->stencil || (r->stencil && (q->sfunc != r->sfunc || q->sref != r->sref
                || q->srmask != r->srmask || q->swmask != r->swmask || memcmp(q->sop, r->sop, sizeof q->sop))))
                break;
            if (see && (q->src != r->src || q->dst != r->dst || q->atest != r->atest || q->func != r->func))
                break;
            verts += q->verts;
            tex += q->textured;
            for (k = 0; k < nslot && slots[k] != q->slot; k++) ;
            if (k == nslot) { if (nslot < 4) slots[nslot++] = q->slot; else more = 1; }
        }
        fprintf(stderr, "  [SEETHRU]   draws %u-%u (%d): %s%s", r->draw, g_c[j - 1].draw, j - i,
                r->kind == CK_SOLID ? "solid" : r->kind == CK_SEE_Z ? "see-through with depth writes"
                : r->kind == CK_SEE ? "see-through without" : "no depth test",
                r->skinned ? ", skinned" : "");
        if (r->stencil)
            fprintf(stderr, ", stencil func %03X ref %02X masks %02X %02X ops %04X %04X %04X", r->sfunc,
                    r->sref, r->srmask, r->swmask, r->sop[0], r->sop[1], r->sop[2]);
        fprintf(stderr, ", surface %08X, programs", r->surf);
        for (k = 0; k < nslot; k++) fprintf(stderr, "%s %d", k ? "," : "", slots[k]);
        fprintf(stderr, "%s, %u verts, %u textured", more ? " and more" : "", verts, tex);
        if (see)
            fprintf(stderr, "; blend %04X %04X, alpha test %s, depth func %03X", r->src, r->dst,
                    r->atest ? "on" : "off", r->func);
        fprintf(stderr, "%s\n", k_why[r->why]);
    }
    for (k = 0; i < g_c_n; i++) k++;
    if (k) fprintf(stderr, "  [SEETHRU]   (and %d more draws and clears)\n", k);
    fflush(stderr);
}

/* ---- which draws are split, kept whole or held back ---------------------- */

/* See-through: blended with a destination factor other than ZERO, some
 * colour written, and the depth test on. A colour mask of 0 was never
 * written, which the console takes as every channel. */
static int see_is(const Nv2aRenderState *rs)
{
    uint32_t cm = rs->color_mask ? rs->color_mask : 0x01010101u;
    return rs->blend_enable && rs->blend_dst != SEE_ZERO && (cm & 0x01010101u) && rs->depth_test;
}

static int writes_depth(const Nv2aRenderState *rs) { return rs->depth_test && rs->depth_write; }

static int writes_colour(const Nv2aRenderState *rs)
{
    uint32_t cm = rs->color_mask ? rs->color_mask : 0x01010101u;
    return (cm & 0x01010101u) != 0;
}

/* The stencil test on: a write mask alone writes nothing while the test is
 * off, on the NV2A as in GL, and the title leaves one set on nearly every
 * draw (seethru5's census at 99th Street). */
static int uses_stencil(const Nv2aRenderState *rs)
{
    return rs->stencil_test != 0;
}

/* Over the picture: for each colour surface, whether the frame has drawn to
 * it with the depth test, and whether a depth clear has come after that. A
 * table this small is enough: the title draws to five. */
#define OV_SURFS 8
static struct { uint32_t surf, frame; uint8_t drawn, over; } g_ov[OV_SURFS];
static int g_ov_n;

static int over_at(uint32_t surf, uint32_t frame)
{
    int i;
    for (i = 0; i < g_ov_n && g_ov[i].surf != surf; i++) ;
    if (i == g_ov_n) {
        if (g_ov_n == OV_SURFS) return -1;
        g_ov_n++;
        g_ov[i].surf  = surf;
        g_ov[i].frame = frame + 1u;
    }
    if (g_ov[i].frame != frame) {
        g_ov[i].frame = frame;
        g_ov[i].drawn = g_ov[i].over = 0;
    }
    return i;
}

/* Outlines: the see-through draws with depth writes since the last outline
 * or other draw, and the mesh pieces (where their positions are, and their
 * vertex count) an outline has followed. */
#define OL_RING 32
#define OL_KEYS 4096            /* a power of two */
/* A key's slot, by Fibonacci hashing: the key times 2^64 over the golden
 * ratio, whose high bits (from bit 40) are the slot. */
#define OL_HASH 0x9E3779B97F4A7C15ull
static struct { uint64_t key; uint32_t verts; } g_ol_ring[OL_RING];
static int      g_ol_n;
static uint64_t g_ol_keys[OL_KEYS];
static uint32_t g_ol_learnt;

static uint32_t ol_hash(uint64_t key)
{
    return (uint32_t)(key * OL_HASH >> 40) & (OL_KEYS - 1);
}

static int ol_has(uint64_t key)
{
    uint32_t h = ol_hash(key), i;
    for (i = 0; i < OL_KEYS; i++, h = (h + 1) & (OL_KEYS - 1)) {
        if (g_ol_keys[h] == key) return 1;
        if (!g_ol_keys[h]) return 0;
    }
    return 0;
}

static void ol_add(uint64_t key)
{
    uint32_t h = ol_hash(key), i;
    if (g_ol_learnt >= OL_KEYS / 2) return;
    for (i = 0; i < OL_KEYS; i++, h = (h + 1) & (OL_KEYS - 1)) {
        if (g_ol_keys[h] == key) return;
        if (!g_ol_keys[h]) { g_ol_keys[h] = key; g_ol_learnt++; return; }
    }
}

/* 2: an outline (an untextured see-through draw with depth writes whose
 * vertices are those of the last few such draws together, at least 12),
 * whose pieces are learnt; 1: a piece an outline has followed; 0: neither.
 * Inline draws (no address) are neither. */
static int outline(const Nv2aSeeDraw *d, int see_z)
{
    uint64_t key;
    int k;
    if (!see_z || !d->pos) { g_ol_n = 0; return 0; }
    key = (uint64_t)d->pos << 32 | d->verts;
    if (!d->textured) {
        uint32_t sum = 0;
        for (k = 1; k <= g_ol_n; k++) {
            sum += g_ol_ring[g_ol_n - k].verts;
            if (sum >= d->verts) break;
        }
        if (k <= g_ol_n && sum == d->verts && d->verts >= 12) {
            int j;
            for (j = g_ol_n - k; j < g_ol_n; j++) ol_add(g_ol_ring[j].key);
            g_ol_n = 0;
            return 2;
        }
    }
    if (g_ol_n == OL_RING) {
        memmove(g_ol_ring, g_ol_ring + 1, (OL_RING - 1) * sizeof g_ol_ring[0]);
        g_ol_n--;
    }
    g_ol_ring[g_ol_n].key = key;
    g_ol_ring[g_ol_n].verts = d->verts;
    g_ol_n++;
    return ol_has(key);
}

/* ---- the device, as the front half has left it --------------------------- */

/* Each uniform's place among a program's values, in floats (an int in the
 * bits of one), from nv2a_uniform_desc. */
static uint32_t g_u_at[NV2A_U_COUNT + 1];
static float   *g_zero;         /* zeros, as many as the longest uniform has */

static void u_layout(void)
{
    int id;
    uint32_t most = 0;
    if (g_u_at[NV2A_U_COUNT]) return;
    for (id = 0; id < NV2A_U_COUNT; id++) {
        uint32_t per;
        switch (nv2a_uniform_desc[id].type) {
        case NV2A_UT_VEC2: per = 2; break;
        case NV2A_UT_VEC3: per = 3; break;
        case NV2A_UT_VEC4: case NV2A_UT_MAT2: per = 4; break;
        default:           per = 1; break;
        }
        g_u_at[id + 1] = g_u_at[id] + per * nv2a_uniform_desc[id].count;
        if (per * nv2a_uniform_desc[id].count > most) most = per * nv2a_uniform_desc[id].count;
    }
    g_zero = (float *)calloc(most, sizeof(float));
}

static int u_is_int(int id)
{
    return nv2a_uniform_desc[id].type == NV2A_UT_INT || nv2a_uniform_desc[id].type == NV2A_UT_SAMPLER;
}

/* A program's uniforms as the front half last set them (OpenGL keeps them
 * with the program, and they start at 0, as these do), which of them it has
 * set, and, while the held-back list is drawn, what the device holds instead
 * where that is something else (`gpu`). */
typedef struct {
    float       *val;
    uint8_t      set[NV2A_U_COUNT];
    const float *gpu[NV2A_U_COUNT];
    uint8_t      touched;
} SeeProg;
static SeeProg  *g_prog;
static uint32_t  g_nprog;
static const float k_dirty[1];  /* gpu: something else, not known */

static SeeProg *prog_at(uint32_t h)
{
    SeeProg *p;
    if (!h || h > (1u << 16)) return NULL;
    if (h >= g_nprog) {
        uint32_t n = g_nprog ? g_nprog : 256;
        while (n <= h) n *= 2;
        p = (SeeProg *)realloc(g_prog, n * sizeof *p);
        if (!p) return NULL;
        memset(p + g_nprog, 0, (n - g_nprog) * sizeof *p);
        g_prog = p;
        g_nprog = n;
    }
    p = &g_prog[h];
    if (!p->val) {
        u_layout();
        if (!(p->val = (float *)calloc(g_u_at[NV2A_U_COUNT], sizeof(float)))) return NULL;
    }
    return p;
}

/* The front half's view: the program, the textures, the state and the
 * vertices it last set. */
static struct {
    uint32_t prog;
    uint32_t tex[SEE_UNITS];
    Nv2aSampler smp[SEE_UNITS];         /* how each is sampled ... */
    uint8_t  own[SEE_UNITS];            /* ... or 1: as it was made (texture()'s NULL) */
    int8_t   opaque[SEE_UNITS];         /* -1: not set since the texture was */
    Nv2aRenderState rs;
    int      rs_ok;
    int      vkind;                     /* VK_*: the last vertices() or vbuf_bind() */
    const float *v;
    uint32_t nverts, stride, vb, vserial;
    int      attr_at[16];
    uint32_t const_mask;
    float    const_attr[16][4];
    uint32_t rt;
    int      read;                      /* the surface was read since the last draw */
} F;
enum { VK_NONE, VK_ARRAY, VK_KEPT, VK_STREAM };
static uint32_t g_serial;               /* draw calls made so far */
static int      g_vlive;                /* the draw being made has its vertices at hand */

/* Buffers refilled for nearly every draw (vbuf_new's stream), and what each
 * was filled with last: the front half's own memory, good until its next
 * draw. */
#define STREAMS 8
static struct { uint32_t vb, serial, vbytes, ibytes; const float *v; const void *idx; } g_stream[STREAMS];
static int g_nstream;

static int stream_at(uint32_t vb)
{
    int i;
    for (i = 0; i < g_nstream; i++) if (g_stream[i].vb == vb) return i;
    return -1;
}

/* ---- the held-back list -------------------------------------------------- */

/* Kept in an arena that goes back to empty when the list is drawn: each
 * draw's uniforms, its vertices where they are not in a kept buffer, and
 * what the front half sent while it was held. */
static uint8_t *g_ar;
static size_t   g_ar_n, g_ar_cap;

static size_t ar_put(const void *p, size_t n)
{
    size_t at = (g_ar_n + 7) & ~(size_t)7;
    if (at + n > g_ar_cap) {
        size_t cap = g_ar_cap ? g_ar_cap : 1u << 20;
        uint8_t *a;
        while (cap < at + n) cap *= 2;
        if (!(a = (uint8_t *)realloc(g_ar, cap))) return (size_t)-1;
        g_ar = a;
        g_ar_cap = cap;
    }
    if (p) memcpy(g_ar + at, p, n);
    g_ar_n = at + n;
    return at;
}

/* What the front half sent while a draw was held. */
enum { CMD_STATE, CMD_UNI_F, CMD_UNI_I, CMD_UNI_PART, CMD_DRAW, CMD_DRAW_IDX };
typedef struct { uint16_t op, id; int32_t a, b, n; } SeeCmd;   /* data after it */

typedef struct {
    uint32_t prog;
    size_t   uni_at;                    /* (id, floats, the values) for each uniform set */
    uint32_t nuni;
    uint8_t  had[NV2A_U_COUNT];         /* which those are */
    uint32_t tex[SEE_UNITS];
    Nv2aSampler smp[SEE_UNITS];
    uint8_t  own[SEE_UNITS];
    int8_t   opaque[SEE_UNITS];
    Nv2aRenderState rs;
    int      vkind;
    size_t   v_at;
    uint32_t nverts, stride, vb, vbytes, ibytes;
    int      attr_at[16];
    uint32_t const_mask;
    float    const_attr[16][4];
    size_t   cmd_at;
    uint32_t ncmd;
} Held;
static Held    *g_held;
static int      g_nheld, g_capheld;
static int      g_holding;              /* between hold_begin and hold_end */
static int      g_flushing;
static uint32_t g_own_vb;               /* a stream buffer of the wrapper's own */
static uint32_t g_told;                 /* things said once */

static void flush(int why);

/* Something the held-back list refers to, about to go or change. */
static int held_tex(uint32_t tex)
{
    int i, u;
    for (i = 0; i < g_nheld; i++)
        for (u = 0; u < SEE_UNITS; u++) if (g_held[i].tex[u] == tex) return 1;
    return 0;
}
static int held_vb(uint32_t vb)
{
    int i;
    for (i = 0; i < g_nheld; i++) if (g_held[i].vkind == VK_KEPT && g_held[i].vb == vb) return 1;
    return 0;
}
static int held_prog(uint32_t prog)
{
    int i;
    for (i = 0; i < g_nheld; i++) if (g_held[i].prog == prog) return 1;
    return 0;
}

/* A call not to be made while a draw is held: it is drawn in place from
 * here on, and the log says so once. */
static void hold_break(const char *what)
{
    if (!g_holding) return;
    g_holding = 0;
    if (!(g_told & 1)) {
        g_told |= 1;
        fprintf(stderr, "  [SEETHRU] %s while a draw was held back: the rest of it drawn in place\n", what);
    }
}

static void cmd(int op, int id, int32_t a, int32_t b, const void *data, int32_t n, size_t bytes)
{
    SeeCmd c;
    size_t at;
    c.op = (uint16_t)op; c.id = (uint16_t)id; c.a = a; c.b = b; c.n = n;
    if ((at = ar_put(NULL, sizeof c + bytes)) == (size_t)-1) { hold_break("no memory"); return; }
    memcpy(g_ar + at, &c, sizeof c);
    if (bytes) memcpy(g_ar + at + sizeof c, data, bytes);
    g_held[g_nheld - 1].ncmd++;
}

int nv2a_seethru_hold_begin(void)
{
    Held *h;
    SeeProg *p;
    size_t at;
    int id;
    size_t start = g_ar_n;
    if (!hold_on() || g_holding || g_flushing || !g_vlive || !F.prog || !F.rs_ok || !(p = prog_at(F.prog)))
        return 0;
    if (g_nheld == g_capheld) {
        int cap = g_capheld ? g_capheld * 2 : 256;
        Held *n = (Held *)realloc(g_held, (size_t)cap * sizeof *n);
        if (!n) return 0;
        g_held = n;
        g_capheld = cap;
    }
    h = &g_held[g_nheld];
    memset(h, 0, sizeof *h);
    h->prog = F.prog;
    /* Every uniform the program has been given, as it has it now. */
    h->uni_at = g_ar_n;
    for (id = 0; id < NV2A_U_COUNT; id++) {
        uint32_t hd[2], n = g_u_at[id + 1] - g_u_at[id];
        if (!p->set[id]) continue;
        hd[0] = (uint32_t)id; hd[1] = n;
        if ((at = ar_put(hd, sizeof hd)) == (size_t)-1 || ar_put(p->val + g_u_at[id], n * sizeof(float)) == (size_t)-1) {
            g_ar_n = start;
            return 0;
        }
        if (!h->nuni++) h->uni_at = at;
        h->had[id] = 1;
    }
    memcpy(h->tex, F.tex, sizeof h->tex);
    memcpy(h->smp, F.smp, sizeof h->smp);
    memcpy(h->own, F.own, sizeof h->own);
    memcpy(h->opaque, F.opaque, sizeof h->opaque);
    h->rs = F.rs;
    h->vkind = F.vkind;
    h->stride = F.stride;
    memcpy(h->attr_at, F.attr_at, sizeof h->attr_at);
    h->const_mask = F.const_mask;
    memcpy(h->const_attr, F.const_attr, sizeof h->const_attr);
    switch (F.vkind) {
    case VK_ARRAY:
        h->nverts = F.nverts;
        h->vbytes = F.nverts * F.stride * (uint32_t)sizeof(float);
        if ((h->v_at = ar_put(F.v, h->vbytes)) == (size_t)-1) { g_ar_n = start; return 0; }
        break;
    case VK_KEPT:
        h->vb = F.vb;
        break;
    case VK_STREAM: {
        int s = stream_at(F.vb);
        if (s < 0) { g_ar_n = start; return 0; }
        h->vbytes = g_stream[s].vbytes;
        h->ibytes = g_stream[s].idx ? g_stream[s].ibytes : 0;
        if ((h->v_at = ar_put(g_stream[s].v, h->vbytes)) == (size_t)-1
         || (h->ibytes && ar_put(g_stream[s].idx, h->ibytes) == (size_t)-1)) {
            g_ar_n = start;
            return 0;
        }
        break;
    }
    }
    if (g_c_on > 0 && g_c_frame != 0xFFFFFFFFu) g_c_bytes += (uint32_t)(g_ar_n - h->uni_at);
    h->cmd_at = (g_ar_n + 7) & ~(size_t)7;
    g_nheld++;
    g_holding = 1;
    return 1;
}

void nv2a_seethru_hold_end(void)
{
    g_holding = 0;
}

/* A uniform from a program's values, or the list's, to the device. */
static void upload(int id, const float *v)
{
    if (u_is_int(id)) {
        int32_t i;
        memcpy(&i, v, sizeof i);
        D->uniform_i(id, i);
    } else {
        D->uniform_f(id, v, nv2a_uniform_desc[id].count);
    }
}

/* The same, where the device holds `was`: of the constants, only the run of
 * them that differs, as the front half sends them. */
static void upload_over(int id, const float *was, const float *v)
{
    if (id == NV2A_U_C && was != k_dirty && D->uniform_f_part) {
        uint32_t n = nv2a_uniform_desc[id].count, a = 0, b = n;
        while (a < n && !memcmp(was + 4 * a, v + 4 * a, 4 * sizeof(float))) a++;
        while (b > a && !memcmp(was + 4 * (b - 1), v + 4 * (b - 1), 4 * sizeof(float))) b--;
        if (a == b || D->uniform_f_part(id, (int)a, v + 4 * a, (int)(b - a))) return;
    }
    upload(id, v);
}


/* What the front half has set, back on the device, after the list. */
static void restore(void)
{
    uint32_t i;
    int id, u;
    for (i = 1; i < g_nprog; i++) {
        SeeProg *p = &g_prog[i];
        int bound = 0;
        if (!p->touched) continue;
        for (id = 0; id < NV2A_U_COUNT; id++) {
            const float *g = p->gpu[id], *f = p->val + g_u_at[id];
            if (!g) continue;
            p->gpu[id] = NULL;
            if (g != k_dirty && !memcmp(g, f, (g_u_at[id + 1] - g_u_at[id]) * sizeof(float))) continue;
            if (!bound) { D->prog_use(i); bound = 1; }
            upload_over(id, g, f);
        }
        p->touched = 0;
    }
    if (F.prog) D->prog_use(F.prog);
    for (u = 0; u < SEE_UNITS; u++) {
        D->texture(u, F.tex[u], F.own[u] ? NULL : &F.smp[u]);
        if (F.opaque[u] >= 0 && D->texture_opaque) D->texture_opaque(u, F.opaque[u]);
    }
    D->state_invalidate();
    if (F.rs_ok) D->state(&F.rs);
    /* The vertices the front half has set for the draw it is making. */
    if (F.vserial == g_serial) {
        if (F.vkind == VK_ARRAY)
            D->vertices(F.v, F.nverts, F.stride, F.attr_at, F.const_mask,
                        (const float (*)[4])F.const_attr);
        else if (F.vkind != VK_NONE)
            D->vbuf_bind(F.vb, F.stride, F.attr_at, F.const_mask, (const float (*)[4])F.const_attr);
    }
}

static void replay(const Held *h)
{
    SeeProg *p = prog_at(h->prog);
    const uint8_t *at;
    uint32_t i;
    int u, id;
    if (!p) return;
    D->prog_use(h->prog);
    p->touched = 1;
    at = g_ar + h->uni_at;
    for (i = 0; i < h->nuni; i++) {
        uint32_t hd[2];
        const float *v, *g;
        at = g_ar + (((size_t)(at - g_ar) + 7) & ~(size_t)7);
        memcpy(hd, at, sizeof hd);
        v = (const float *)(at + sizeof hd);
        at += sizeof hd + hd[1] * sizeof(float);
        g = p->gpu[hd[0]] ? p->gpu[hd[0]] : p->val + g_u_at[hd[0]];
        if (g != k_dirty && !memcmp(g, v, hd[1] * sizeof(float))) continue;
        upload_over((int)hd[0], g, v);
        p->gpu[hd[0]] = v;
    }
    /* One the program was first given after the draw was held: as it was
     * then, 0. */
    for (id = 0; id < NV2A_U_COUNT && g_zero; id++) {
        const float *g;
        if (h->had[id] || !p->set[id]) continue;
        g = p->gpu[id] ? p->gpu[id] : p->val + g_u_at[id];
        if (g != k_dirty && !memcmp(g, g_zero, (g_u_at[id + 1] - g_u_at[id]) * sizeof(float))) continue;
        upload_over(id, g, g_zero);
        p->gpu[id] = g_zero;
    }
    for (u = 0; u < SEE_UNITS; u++) {
        D->texture(u, h->tex[u], h->own[u] ? NULL : &h->smp[u]);
        if (h->opaque[u] >= 0 && D->texture_opaque) D->texture_opaque(u, h->opaque[u]);
    }
    D->state(&h->rs);
    switch (h->vkind) {
    case VK_ARRAY:
        D->vertices((const float *)(g_ar + h->v_at), h->nverts, h->stride, h->attr_at, h->const_mask,
                    (const float (*)[4])h->const_attr);
        break;
    case VK_KEPT:
        D->vbuf_bind(h->vb, h->stride, h->attr_at, h->const_mask, (const float (*)[4])h->const_attr);
        break;
    case VK_STREAM:
        if (!g_own_vb) g_own_vb = D->vbuf_new(1);
        D->vbuf_fill(g_own_vb, (const float *)(g_ar + h->v_at), h->vbytes,
                     h->ibytes ? g_ar + h->v_at + h->vbytes : NULL, h->ibytes);
        D->vbuf_bind(g_own_vb, h->stride, h->attr_at, h->const_mask, (const float (*)[4])h->const_attr);
        break;
    }
    at = g_ar + h->cmd_at;
    for (i = 0; i < h->ncmd; i++) {
        SeeCmd c;
        const uint8_t *data;
        at = g_ar + (((size_t)(at - g_ar) + 7) & ~(size_t)7);
        memcpy(&c, at, sizeof c);
        data = at + sizeof c;
        switch (c.op) {
        case CMD_STATE:
            D->state((const Nv2aRenderState *)(const void *)data);
            at = data + sizeof(Nv2aRenderState);
            break;
        case CMD_UNI_F:
            D->uniform_f(c.id, (const float *)(const void *)data, c.n);
            p->gpu[c.id] = k_dirty;
            at = data + (size_t)c.a * sizeof(float);
            break;
        case CMD_UNI_I:
            D->uniform_i(c.id, c.a);
            p->gpu[c.id] = k_dirty;
            at = data;
            break;
        case CMD_UNI_PART:
            D->uniform_f_part(c.id, c.a, (const float *)(const void *)data, c.n);
            p->gpu[c.id] = k_dirty;
            at = data + (size_t)c.b * sizeof(float);
            break;
        case CMD_DRAW:
            D->draw(c.a, (uint32_t)c.n);
            at = data;
            break;
        case CMD_DRAW_IDX:
            D->draw_indexed(c.a, (uint32_t)c.n, c.b);
            at = data;
            break;
        default:
            return;
        }
    }
}

/* The held-back list, drawn in the title's order, and the device put back
 * as the front half left it. */
static void flush(int why)
{
    int i;
    CRec *r;
    if (!g_nheld || g_flushing) return;
    g_flushing = 1;
    g_holding = 0;
    for (i = 0; i < g_nheld; i++) replay(&g_held[i]);
    restore();
    if ((r = census_rec()) != NULL) {
        r->kind = CK_FLUSH;
        r->why = (uint8_t)why;
        r->verts = (uint32_t)g_nheld;
        r->draw = g_c_last ? g_c_last->draw : 0;
        r->surf = 0;
    }
    if (g_c_on > 0 && g_c_frame != 0xFFFFFFFFu) g_c_redrawn += (uint32_t)g_nheld;
    g_nheld = 0;
    g_ar_n = 0;
    g_flushing = 0;
}

/* ---- the draw ------------------------------------------------------------ */

int nv2a_seethru_draw(const Nv2aRenderState *rs, const Nv2aSeeDraw *d)
{
    int see = see_is(rs), see_z = see && rs->depth_write, ol, over = 0, i, why, how = NV2A_SEE_KEEP;
    int f = rs->depth_func, read = F.read;
    CRec *r;
    census_turn(d->frame);
    F.read = 0;
    /* Its vertices: set since the last draw, and still the front half's. */
    g_vlive = F.vkind != VK_NONE && F.vserial == g_serial;
    if (g_vlive && F.vkind == VK_STREAM) {
        int s = stream_at(F.vb);
        g_vlive = s >= 0 && g_stream[s].serial == g_serial;
    }
    /* Over the picture: drawn to with the depth test, and cleared since. */
    if ((i = over_at(d->surf, d->frame)) >= 0) {
        if (rs->depth_test && !g_ov[i].over) g_ov[i].drawn = 1;
        over = g_ov[i].over;
    }
    ol = outline(d, see_z);
    if (!see_z) why = WHY_PLAIN;
    else if (!g_on) why = WHY_OFF;
    else if (ol) why = ol == 2 ? WHY_OUTLINE : WHY_PIECE;
    else if (over) why = WHY_OVER;
    else if (read) why = WHY_READS;
    else if (!rs->alpha_test || !d->alpha_uniforms) why = WHY_NO_ATEST;
    else if (f != SEE_LESS && f != SEE_LEQUAL && f != SEE_GREATER && f != SEE_GEQUAL && f != SEE_ALWAYS)
        why = WHY_FUNC;
    else { why = hold_on() ? WHY_SPLIT : WHY_SPLIT_INPLACE; how = NV2A_SEE_SPLIT; }
    /* With something held back: a draw that writes depth where it is
     * invisible, which the held-back pixels behind it never had to pass, is
     * drawn after them; one that writes no depth is held back as well, so
     * it stays over them. One that writes no depth but uses the stencil
     * test (a shadow) is drawn in place, as the title draws it, without
     * drawing the list, so what the title draws after it still goes behind
     * the held-back pixels (the tape behind the water), and they go over the
     * shadow. The rest is drawn in place too. */
    if (g_nheld && how == NV2A_SEE_KEEP) {
        if (writes_depth(rs) && see && !rs->alpha_test) { flush(FL_CUT); why = WHY_CUT; }
        else if (!writes_depth(rs) && writes_colour(rs) && !read && !uses_stencil(rs)) {
            how = NV2A_SEE_HOLD;
            why = WHY_HELD;
        }
    }
    if ((r = census_rec()) != NULL) {
        r->kind = (uint8_t)(!rs->depth_test ? CK_UNTESTED : !see ? CK_SOLID : rs->depth_write ? CK_SEE_Z : CK_SEE);
        r->why = (uint8_t)why;
        r->draw = d->draw;
        r->surf = d->surf;
        r->verts = d->verts;
        r->slot = d->slot;
        r->src = (uint16_t)rs->blend_src;
        r->dst = (uint16_t)rs->blend_dst;
        r->func = (uint16_t)rs->depth_func;
        r->atest = (uint8_t)(rs->alpha_test != 0);
        r->stencil = (uint8_t)(uses_stencil(rs) != 0);
        if (r->stencil) {
            r->sfunc = (uint16_t)rs->stencil_func;
            r->sref = (uint8_t)rs->stencil_ref;
            r->srmask = (uint8_t)rs->stencil_rmask;
            r->swmask = (uint8_t)rs->stencil_wmask;
            for (i = 0; i < 3; i++) r->sop[i] = (uint16_t)rs->stencil_op[i];
        }
        r->skinned = d->skinned;
        r->textured = d->textured;
    }
    g_c_last = r;
    return how;
}

void nv2a_seethru_depth_clear(uint32_t surf, uint32_t frame)
{
    int i;
    CRec *r;
    census_turn(frame);
    g_ol_n = 0;
    if ((i = over_at(surf, frame)) >= 0 && g_ov[i].drawn) g_ov[i].over = 1;
    if ((r = census_rec()) != NULL) {
        r->kind = CK_CLEAR;
        r->surf = surf;
        r->draw = g_c_last ? g_c_last->draw : 0;
    }
}

/* ---- the wrapper ----------------------------------------------------------
 *
 * Every call goes on to the device. Those that make up a draw are kept as
 * the front half's view (F, and each program's uniforms); while a draw is
 * held, the state, uniforms and draws are kept for it too, and its draws are
 * not made. Anything else that could make the list wrong if drawn later --
 * a clear, a change of surface, the surface read, the flip, something the
 * list refers to freed or refilled, the device's own work -- draws the list
 * first. */

static void w_prog_free(uint32_t prog)
{
    SeeProg *p;
    if (held_prog(prog)) flush(FL_FREE);
    D->prog_free(prog);
    if (prog < g_nprog && (p = &g_prog[prog])->val) {
        memset(p->val, 0, g_u_at[NV2A_U_COUNT] * sizeof(float));
        memset(p->set, 0, sizeof p->set);
    }
    if (F.prog == prog) F.prog = 0;
}

static void w_prog_use(uint32_t prog)
{
    hold_break("a program changed");
    D->prog_use(prog);
    F.prog = prog;
}

static void w_uniform_f(int id, const float *v, int n)
{
    SeeProg *p;
    D->uniform_f(id, v, n);
    if (id < 0 || id >= NV2A_U_COUNT || n <= 0) return;
    u_layout();
    if ((p = prog_at(F.prog)) != NULL) {
        uint32_t room = g_u_at[id + 1] - g_u_at[id], want = (uint32_t)n * (room / nv2a_uniform_desc[id].count);
        memcpy(p->val + g_u_at[id], v, (want < room ? want : room) * sizeof(float));
        p->set[id] = 1;
    }
    if (g_holding) {
        uint32_t floats = (uint32_t)n * ((g_u_at[id + 1] - g_u_at[id]) / nv2a_uniform_desc[id].count);
        cmd(CMD_UNI_F, id, (int32_t)floats, 0, v, n, floats * sizeof(float));
    }
}

static void w_uniform_i(int id, int v)
{
    SeeProg *p;
    D->uniform_i(id, v);
    if (id < 0 || id >= NV2A_U_COUNT) return;
    u_layout();
    if ((p = prog_at(F.prog)) != NULL) {
        int32_t i = v;
        memcpy(p->val + g_u_at[id], &i, sizeof i);
        p->set[id] = 1;
    }
    if (g_holding) cmd(CMD_UNI_I, id, v, 0, NULL, 0, 0);
}

static int w_uniform_f_part(int id, int first, const float *v, int n)
{
    SeeProg *p;
    int ok = D->uniform_f_part(id, first, v, n);
    if (!ok || id < 0 || id >= NV2A_U_COUNT || first < 0 || n <= 0) return ok;
    u_layout();
    if ((p = prog_at(F.prog)) != NULL) {
        uint32_t per = (g_u_at[id + 1] - g_u_at[id]) / nv2a_uniform_desc[id].count;
        uint32_t from = (uint32_t)first * per, room = g_u_at[id + 1] - g_u_at[id], want = (uint32_t)n * per;
        if (from < room) {
            memcpy(p->val + g_u_at[id] + from, v, (want < room - from ? want : room - from) * sizeof(float));
            p->set[id] = 1;
        }
    }
    if (g_holding) {
        uint32_t floats = (uint32_t)n * ((g_u_at[id + 1] - g_u_at[id]) / nv2a_uniform_desc[id].count);
        cmd(CMD_UNI_PART, id, first, (int32_t)floats, v, n, floats * sizeof(float));
    }
    return ok;
}

static void w_texture(int unit, uint32_t tex, const Nv2aSampler *s)
{
    hold_break("a texture changed");
    D->texture(unit, tex, s);
    if (unit >= 0 && unit < SEE_UNITS) {
        F.tex[unit] = tex;
        F.own[unit] = !s;
        if (s) F.smp[unit] = *s;
        F.opaque[unit] = -1;
    }
}

static void w_texture_opaque(int unit, int opaque)
{
    hold_break("a texture changed");
    D->texture_opaque(unit, opaque);
    if (unit >= 0 && unit < SEE_UNITS) F.opaque[unit] = (int8_t)(opaque != 0);
}

static void w_state(const Nv2aRenderState *rs)
{
    D->state(rs);
    F.rs = *rs;
    F.rs_ok = 1;
    if (g_holding) cmd(CMD_STATE, 0, 0, 0, rs, 0, sizeof *rs);
}

static void w_vertices(const float *v, uint32_t nverts, uint32_t stride_floats, const int attr_at[16],
                       uint32_t const_mask, const float const_attr[16][4])
{
    hold_break("the vertices changed");
    D->vertices(v, nverts, stride_floats, attr_at, const_mask, const_attr);
    F.vkind = VK_ARRAY;
    F.v = v;
    F.nverts = nverts;
    F.stride = stride_floats;
    memcpy(F.attr_at, attr_at, sizeof F.attr_at);
    F.const_mask = const_mask;
    memcpy(F.const_attr, const_attr, sizeof F.const_attr);
    F.vserial = g_serial;
}

static void w_vbuf_bind(uint32_t vb, uint32_t stride_floats, const int attr_at[16], uint32_t const_mask,
                        const float const_attr[16][4])
{
    hold_break("the vertices changed");
    D->vbuf_bind(vb, stride_floats, attr_at, const_mask, const_attr);
    F.vkind = stream_at(vb) >= 0 ? VK_STREAM : VK_KEPT;
    F.vb = vb;
    F.stride = stride_floats;
    memcpy(F.attr_at, attr_at, sizeof F.attr_at);
    F.const_mask = const_mask;
    memcpy(F.const_attr, const_attr, sizeof F.const_attr);
    F.vserial = g_serial;
}

static uint32_t w_vbuf_new(int stream)
{
    uint32_t vb = D->vbuf_new(stream);
    if (vb && stream && g_nstream < STREAMS) {
        memset(&g_stream[g_nstream], 0, sizeof g_stream[0]);
        g_stream[g_nstream++].vb = vb;
    }
    return vb;
}

static void w_vbuf_free(uint32_t vb)
{
    int s = stream_at(vb);
    if (held_vb(vb)) flush(FL_FREE);
    D->vbuf_free(vb);
    if (s >= 0) g_stream[s] = g_stream[--g_nstream];
}

static void w_vbuf_fill(uint32_t vb, const float *v, uint32_t vbytes, const void *idx, uint32_t ibytes)
{
    int s = stream_at(vb);
    hold_break("a vertex buffer refilled");
    if (s < 0 && held_vb(vb)) flush(FL_FREE);
    D->vbuf_fill(vb, v, vbytes, idx, ibytes);
    if (s >= 0) {
        g_stream[s].serial = g_serial;
        if (v) { g_stream[s].v = v; g_stream[s].vbytes = vbytes; }
        g_stream[s].idx = idx;
        g_stream[s].ibytes = idx ? ibytes : 0;
    }
}

static void w_draw(int prim, uint32_t count)
{
    if (g_holding) cmd(CMD_DRAW, 0, prim, 0, NULL, (int32_t)count, 0);
    if (!g_holding) D->draw(prim, count);
    g_serial++;
}

static void w_draw_indexed(int prim, uint32_t count, int idx32)
{
    if (g_holding) cmd(CMD_DRAW_IDX, 0, prim, idx32, NULL, (int32_t)count, 0);
    if (!g_holding) D->draw_indexed(prim, count, idx32);
    g_serial++;
}

static void w_clear(unsigned mask, const float rgba[4], float depth, uint32_t stencil, const uint32_t rect[4])
{
    hold_break("a clear");
    flush(FL_CLEAR);
    D->clear(mask, rgba, depth, stencil, rect);
}

static void w_target_bind(uint32_t rt)
{
    hold_break("a change of surface");
    if (rt != F.rt) flush(FL_SURFACE);
    D->target_bind(rt);
    F.rt = rt;
}

static void w_target_free(uint32_t rt)
{
    hold_break("a surface freed");
    flush(FL_SURFACE);
    D->target_free(rt);
    if (F.rt == rt) F.rt = 0;
}

static uint32_t w_target_texture(uint32_t rt)
{
    if (rt == F.rt) { hold_break("the surface read"); flush(FL_READ); F.read = 1; }
    return D->target_texture(rt);
}

static uint32_t w_target_snapshot(uint32_t rt)
{
    hold_break("the surface read");
    flush(FL_READ);
    F.read = 1;
    return D->target_snapshot(rt);
}

static int w_target_read(uint32_t rt, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int bgra, void *out)
{
    hold_break("the surface read");
    flush(FL_READ);
    return D->target_read(rt, x, y, w, h, bgra, out);
}

static int w_target_read_depth(uint32_t rt, uint32_t pw, uint32_t ph, float *out)
{
    hold_break("the surface read");
    flush(FL_READ);
    return D->target_read_depth(rt, pw, ph, out);
}

static void w_tex_free(uint32_t tex)
{
    hold_break("a texture freed");
    if (held_tex(tex)) flush(FL_FREE);
    D->tex_free(tex);
    {
        int u;
        for (u = 0; u < SEE_UNITS; u++) if (F.tex[u] == tex) F.tex[u] = 0;
    }
}

static void w_present(uint32_t rt, uint32_t pw, uint32_t ph)
{
    hold_break("the flip");
    flush(FL_FLIP);
    D->present(rt, pw, ph);
}

/* The device's own work, which could change what it has bound. */
static int w_cache_idle(double budget_ms)
{
    hold_break("the device's own work");
    flush(FL_OTHER);
    return D->cache_idle(budget_ms);
}
static int w_cache_rebuild(void)
{
    hold_break("the device's own work");
    flush(FL_OTHER);
    return D->cache_rebuild();
}
static void w_cache_open(const char *dir, const char *tag)
{
    hold_break("the device's own work");
    flush(FL_OTHER);
    D->cache_open(dir, tag);
}
static uint32_t w_target_smaa(uint32_t rt)
{
    hold_break("the device's own work");
    flush(FL_OTHER);
    return D->target_smaa(rt);
}
static int w_samples(int n)
{
    hold_break("the device's own work");
    flush(FL_OTHER);
    return D->samples(n);
}
static void w_fill_sides(uint32_t rt, uint32_t src_l, uint32_t src_r)
{
    hold_break("the device's own work");
    flush(FL_OTHER);
    D->fill_sides(rt, src_l, src_r);
}
static void w_fill_strip(uint32_t rt, uint32_t x1, uint32_t src, uint32_t r0, uint32_t r1)
{
    hold_break("the device's own work");
    flush(FL_OTHER);
    D->fill_strip(rt, x1, src, r0, r1);
}
static uint32_t w_prog_new(const char *vsrc, const char *fsrc, const char *what)
{
    hold_break("a program made");
    return D->prog_new(vsrc, fsrc, what);
}

const Nv2aGfx *nv2a_seethru_wrap(const Nv2aGfx *dev)
{
    int u;
    if (!dev) return dev;
    D = dev;
    W = *dev;
    u_layout();
    for (u = 0; u < SEE_UNITS; u++) F.opaque[u] = -1;
    W.prog_new = w_prog_new;
    W.prog_free = w_prog_free;
    W.prog_use = w_prog_use;
    W.uniform_f = w_uniform_f;
    W.uniform_i = w_uniform_i;
    W.texture = w_texture;
    W.state = w_state;
    W.vertices = w_vertices;
    W.draw = w_draw;
    W.clear = w_clear;
    W.present = w_present;
    W.target_bind = w_target_bind;
    W.target_free = w_target_free;
    W.target_texture = w_target_texture;
    W.target_snapshot = w_target_snapshot;
    W.target_read = w_target_read;
    W.target_read_depth = w_target_read_depth;
    W.tex_free = w_tex_free;
    if (dev->vbuf_new)       W.vbuf_new = w_vbuf_new;
    if (dev->vbuf_free)      W.vbuf_free = w_vbuf_free;
    if (dev->vbuf_fill)      W.vbuf_fill = w_vbuf_fill;
    if (dev->vbuf_bind)      W.vbuf_bind = w_vbuf_bind;
    if (dev->draw_indexed)   W.draw_indexed = w_draw_indexed;
    if (dev->uniform_f_part) W.uniform_f_part = w_uniform_f_part;
    if (dev->cache_open)     W.cache_open = w_cache_open;
    if (dev->cache_idle)     W.cache_idle = w_cache_idle;
    if (dev->cache_rebuild)  W.cache_rebuild = w_cache_rebuild;
    if (dev->target_smaa)    W.target_smaa = w_target_smaa;
    if (dev->samples)        W.samples = w_samples;
    if (dev->texture_opaque) W.texture_opaque = w_texture_opaque;
    if (dev->fill_sides)     W.fill_sides = w_fill_sides;
    if (dev->fill_strip)     W.fill_strip = w_fill_strip;
    fprintf(stderr, "  [SEETHRU] what shows through see-through surfaces is drawn %s\n",
            hold_on() ? "behind them: their see-through pixels held back to the pass's end"
            : "over them: their see-through pixels in place (RECOMP_GL_SEETHRU_HOLD=0)");
    return &W;
}
