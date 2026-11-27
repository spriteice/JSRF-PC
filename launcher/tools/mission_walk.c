/*
 * The story walk (mission_walk.h): mission-select/tools/walk3.py's model and
 * search, and starts.py's lines, as they are, in C, so JSRF-PC Tools makes
 * Developer > Missions' start-states.txt on the player's PC from their own
 * game (b3 row 38). The Python made the lines the Missions test builds were
 * checked with (b2.2 row 8; mission-select/FINDINGS-2.md to FINDINGS-4.md
 * have what each part was read from in the game's code). This walk gives
 * the same file, byte for byte, from the same mission files, with one
 * thing left out: walk3.py's 120-second limit on a search. Here the search
 * stops only at its 20,000 states, so a slow PC never gets another file.
 * At the end, writing and removing start-states.txt, each after a dated
 * backup of the one there.
 *
 * The model. A mission file has five lists: the cast (header +0x34, 0x20
 * bytes a record: type, id, conditions, flags), the setup list (+0x174),
 * the sensors (+0x17C), the actions (+0x184) and the events (+0x18C), each
 * entry 0x54 bytes (conditions, flags, command, 16 parameters), and its
 * exits (+0x194, 0x44 bytes: type, next, entrance). A condition or a flag
 * is a word: its bank (bits 0-2: Mission, Chapter, Global, bank 3), its
 * number (bits 3-18, below 0x200) and its value (bit 19). The skaters
 * unlocked are kept beside the four banks, as a fifth ("S").
 *   - Arriving at a mission: the Mission flags are cleared; the cast's
 *     records whose conditions hold join, their flags set (a skater's
 *     record only if that skater is unlocked); then the setup list runs
 *     once, in order; then what follows by itself (settle).
 *   - Settling: a sensor that has no test of its own (command 32) fires
 *     when its conditions hold; the actions run in order (an entry with no
 *     conditions follows the one before it); then the first event in order
 *     whose conditions hold, and the actions settle again. An event's
 *     commands 230, 229, 238, 246 and 248 take an exit (the parameter
 *     EXIT_PARAM names); 228 is a game over, a dead end; 249, the skip of
 *     a scene seen before, is left out. 214 [i] unlocks skater i, 215 [i]
 *     takes skater i away.
 *   - An exit: 0xFB, 0xFC or 0xFD go to the mission it names in its own
 *     chapter; 0xFF clears the chapter (its Chapter flags too), on to the
 *     next chapter's 96; 0x101, the garage on Load Game, to the chapter's
 *     00; anything else leaves to the game flow, a dead end here.
 *   - The player: each step of the search is one sensor of the mission the
 *     player is in firing (taken as possible), then what follows by itself.
 *     A sensor on a kept count (29, graffiti souls held; 31, the count at
 *     +0x2A1C) fires the mission's sensors on the same count that ask for
 *     less first, least first: the count rose past them.
 *   - The search: best first, by steps so far + 3 x an estimate (a relaxed
 *     plan's size: walk3.py's Estimate), over the chapter's rules, with each
 *     Mission flag its own mission's, and an entry behind a "you can't
 *     leave yet" action carrying that action's conditions (guarded).
 *   - The story: chapter 1 from New Game at 0101 with skaters 1, 2 and 6,
 *     each chapter after it from its 96 with what the chapter before left;
 *     a 96 that leaves to the game flow (the game goes back to the title)
 *     hands its flags to the chapter's 97, the garage on Load Game.
 *
 * A line: "CCNN e=<entrance> C=<flags> G=<flags> R=<flags> S=<skaters>
 * souls=<n> tanks=<n>", each part only when it has something (port/missions.c
 * reads them; tests/missions has the format): the entrance the last exit of
 * type 0xFB, 0xFC or 0xFD on the way gave (-1 for one that names none), the
 * flags on arrival, and the most a sensor on the way from New Game asked of
 * each kept count (souls only above New Game's 20). A story mission the walk
 * doesn't reach gets its chapter's opening, its 96's flags with no e=, and
 * story=0.
 */
#include "mission_walk.h"
#include "setup.h"
#include "setup_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- the model's numbers ---------------------------------------------------------- */

#define BANK_FLAGS      512u            /* a bank's flags: numbers below 0x200 */
#define FLAG_SKATER(i)  (4u * BANK_FLAGS + (i))
#define SKATERS         32u
#define NFLAGS          (4u * BANK_FLAGS + SKATERS)
#define WORDS           ((NFLAGS + 63u) / 64u)
#define BANK_WORDS      (BANK_FLAGS / 64u)
#define CONDS           33              /* 32 from the file, and one the walk adds */

#define OP_ALWAYS       32u             /* a sensor with no test of its own */
#define OP_SOULS        29u             /* a sensor on graffiti souls held */
#define OP_TANKS        31u             /* a sensor on the count at +0x2A1C */
#define OP_UNLOCK       214u
#define OP_LOCK         215u
#define OP_DEAD         228u            /* game over: retry or quit */
#define OP_SKIP         249u            /* the skip of a scene seen before */

#define EXIT_CHAPTER    0xFFu
#define EXIT_LOADED     0x101u
#define EXIT_GOES(t)    ((t) >= 0xFBu && (t) <= 0xFDu)

#define NEW_GAME        101             /* 0101 */
#define NEW_GAME_SOULS  20u
#define SEARCH_LIMIT    20000           /* states a search takes off its queue */
#define WEIGHT          3               /* the estimate's weight */
#define MAX_DEPTH       12              /* exits followed by themselves in a row */
#define SETTLE_ROUNDS   64
#define WAYS_MAX        16              /* guarded: the ways an entry is kept to */

enum { L_CAST, L_SETUP, L_TRIG, L_ACT, L_EV, L_N };
enum { S_NONE, S_DEAD, S_EXIT };                       /* what settling ended in */
enum { K_FLOW, K_NEXT, K_CHAPTER, K_LOADED };          /* where an exit leads */

typedef struct { uint16_t f; uint8_t v; } cond;
typedef struct { uint64_t w[WORDS]; } fset;

typedef struct rule {
    int list, index;                    /* its list and its place in it in the file */
    uint32_t op, p[16];                 /* the cast's: its type, and its id in p[0] */
    int npre, neff;
    cond pre[CONDS], eff[CONDS];
} rule;

typedef struct { uint32_t type, n, ent; } mexit;

typedef struct mission {
    int id;
    rule **list[L_N];                   /* NULL for an entry the walk can't read */
    int nlist[L_N];
    rule **rules;                       /* setup, sensors, actions, events, then the cast with flags */
    int nrules;
    mexit *exits;
    int nexits;
} mission;

/* What the steps on a way give the line: the last named entrance, and the
 * most each kept count was asked for. */
typedef struct { int has_goes; uint32_t goes, souls, tanks; } trail;

typedef struct { int have; trail tr; fset on; } arrival;

static int has(const fset *s, unsigned f) { return (int)((s->w[f >> 6] >> (f & 63)) & 1u); }
static void put1(fset *s, unsigned f, int v)
{
    if (v) s->w[f >> 6] |= (uint64_t)1 << (f & 63);
    else s->w[f >> 6] &= ~((uint64_t)1 << (f & 63));
}
static int same(const fset *a, const fset *b) { return !memcmp(a, b, sizeof *a); }

static int test(const fset *on, const cond *c, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (has(on, c[i].f) != c[i].v) return 0;
    return 1;
}

static void put(fset *on, const cond *c, int n)
{
    int i;
    for (i = 0; i < n; i++) put1(on, c[i].f, c[i].v);
}

static int exit_param(uint32_t op)
{
    switch (op) {
    case 230: return 0;
    case 229: return 3;
    case 238: return 0;
    case 246: return 1;
    case 248: return 1;
    default:  return -1;
    }
}

static void fail(char *why, size_t n, const char *fmt, int a, int b)
{
    snprintf(why, n, fmt, a, b);
}

/* ---- memory: everything a walk makes goes when it ends ---------------------------- */

typedef struct block { struct block *next; size_t used, size; } block;
typedef struct { block *head; } arena;

static void *take(arena *a, size_t n)
{
    block *b = a->head;
    n = (n + 15u) & ~(size_t)15u;
    if (!b || b->size - b->used < n) {
        size_t size = n > (1u << 20) ? n : (1u << 20);
        b = (block *)malloc(sizeof *b + 16u + size);
        if (!b) return NULL;
        b->next = a->head;
        b->used = 0;
        b->size = size;
        a->head = b;
    }
    {
        void *p = (char *)(b + 1) + 16u + b->used;
        b->used += n;
        memset(p, 0, n);
        return p;
    }
}

static void arena_free(arena *a)
{
    while (a->head) {
        block *b = a->head;
        a->head = b->next;
        free(b);
    }
}

/* ---- reading a mission file -------------------------------------------------------- */

typedef struct {
    const uint8_t *d;
    size_t n;
    int bad;
} reader;

static uint32_t rd(reader *r, uint64_t off)
{
    if (off + 4u > r->n) { r->bad = 1; return 0; }
    return (uint32_t)r->d[off] | (uint32_t)r->d[off + 1] << 8 | (uint32_t)r->d[off + 2] << 16 |
           (uint32_t)r->d[off + 3] << 24;
}

/* A condition or flag word: 1 with it in c, 0 for one the walk can't read
 * (a bank above 3, or a number from 0x200 on). */
static int word(uint32_t w, cond *c)
{
    uint32_t b = w & 7u, i = (w >> 3) & 0xFFFFu;
    if (b > 3u || i >= BANK_FLAGS) return 0;
    c->f = (uint16_t)(b * BANK_FLAGS + i);
    c->v = (uint8_t)((w >> 19) & 1u);
    return 1;
}

/* n words from off into c: 1, 0 when one can't be read, the file's bad flag
 * set when they run past its end. */
static int words(reader *r, uint32_t off, uint32_t n, cond *c)
{
    uint32_t k;
    int ok = 1;
    if (n && (uint64_t)off + 4u * (uint64_t)n > r->n) { r->bad = 1; return 0; }
    for (k = 0; k < n; k++)
        if (!word(rd(r, (uint64_t)off + 4u * k), &c[k])) ok = 0;
    return ok;
}

static int parse(arena *a, mission *m, const uint8_t *d, size_t n)
{
    static const uint32_t at[4] = { 0x174, 0x17C, 0x184, 0x18C };
    static const int lists[4] = { L_SETUP, L_TRIG, L_ACT, L_EV };
    reader r = { d, n, 0 };
    uint32_t off, cnt, i, k;
    int l, total = 0;

    /* Room for the rules: every entry of the four lists, and the cast. */
    for (l = 0; l < 4; l++) {
        cnt = rd(&r, at[l] + 4u);
        off = rd(&r, at[l]);
        if (r.bad || (uint64_t)off + 0x54u * (uint64_t)cnt > n) return -1;
        total += (int)cnt;
    }
    cnt = rd(&r, 0x38);
    off = rd(&r, 0x34);
    if (r.bad || (cnt && (uint64_t)off + 0x20u * ((uint64_t)cnt - 1u) + 24u > n)) return -1;  /* 6 words a record */
    total += (int)cnt;
    m->rules = (rule **)take(a, sizeof *m->rules * (size_t)(total + 1));
    if (!m->rules) return -1;

    for (l = 0; l < 4; l++) {
        int L = lists[l];
        off = rd(&r, at[l]);
        cnt = rd(&r, at[l] + 4u);
        m->list[L] = (rule **)take(a, sizeof *m->list[L] * (size_t)(cnt + 1));
        if (!m->list[L]) return -1;
        m->nlist[L] = (int)cnt;
        for (i = 0; i < cnt; i++) {
            uint64_t e = (uint64_t)off + 0x54u * i;
            uint32_t cp = rd(&r, e), cn = rd(&r, e + 4), sp = rd(&r, e + 8), sn = rd(&r, e + 12),
                     op = rd(&r, e + 16);
            rule *x;
            int ok;
            if (op == 0xFFFFFFFFu || cn > 32u || sn > 32u) continue;
            x = (rule *)take(a, sizeof *x);
            if (!x) return -1;
            ok = words(&r, cp, cn, x->pre);
            ok = words(&r, sp, sn, x->eff) && ok;
            if (r.bad) return -1;
            if (!ok) continue;
            x->list = L;
            x->index = (int)i;
            x->op = op;
            for (k = 0; k < 16; k++) x->p[k] = rd(&r, e + 20 + 4 * k);
            x->npre = (int)cn;
            x->neff = (int)sn;
            if ((op == OP_UNLOCK || op == OP_LOCK) && x->p[0] < SKATERS) {
                x->eff[x->neff].f = (uint16_t)FLAG_SKATER(x->p[0]);
                x->eff[x->neff].v = op == OP_UNLOCK;
                x->neff++;
            }
            m->rules[m->nrules++] = x;
            m->list[L][i] = x;
        }
    }

    off = rd(&r, 0x194);
    cnt = rd(&r, 0x198);
    if (r.bad || (uint64_t)off + 0x44u * (uint64_t)cnt > n) return -1;
    m->exits = (mexit *)take(a, sizeof *m->exits * (size_t)(cnt + 1));
    if (!m->exits) return -1;
    for (i = 0; i < cnt; i++) {
        uint64_t e = (uint64_t)off + 0x44u * i;
        m->exits[i].type = rd(&r, e);
        m->exits[i].n = rd(&r, e + 4);
        m->exits[i].ent = rd(&r, e + 8);
    }
    m->nexits = (int)cnt;

    off = rd(&r, 0x34);
    cnt = rd(&r, 0x38);
    m->list[L_CAST] = (rule **)take(a, sizeof *m->list[L_CAST] * (size_t)(cnt + 1));
    if (!m->list[L_CAST]) return -1;
    for (i = 0; i < cnt; i++) {
        uint64_t e = (uint64_t)off + 0x20u * i;
        uint32_t typ = rd(&r, e), cid = rd(&r, e + 4), cp = rd(&r, e + 8), cn = rd(&r, e + 12),
                 sp = rd(&r, e + 16), sn = rd(&r, e + 20);
        rule *x;
        int ok;
        if (cn > 32u || sn > 32u) continue;
        x = (rule *)take(a, sizeof *x);
        if (!x) return -1;
        ok = words(&r, cp, cn, x->pre);
        ok = words(&r, sp, sn, x->eff) && ok;
        if (r.bad) return -1;
        if (!ok) continue;
        x->list = L_CAST;
        x->index = (int)i;
        x->op = typ;
        x->p[0] = cid;
        x->npre = (int)cn;
        x->neff = (int)sn;
        /* A skater: only if unlocked. */
        if (typ == 2u && cid >= 0x80000000u && cid != 0xFFFFFFFFu && (cid & 0xFFu) < SKATERS) {
            x->pre[x->npre].f = (uint16_t)FLAG_SKATER(cid & 0xFFu);
            x->pre[x->npre].v = 1;
            x->npre++;
        }
        m->list[L_CAST][m->nlist[L_CAST]++] = x;
        if (x->neff) m->rules[m->nrules++] = x;
    }
    return r.bad ? -1 : 0;
}

/* ---- the walk's moves ---------------------------------------------------------------- */

typedef struct walker {
    arena a;
    mission *by_id[JSRF_WALK_MISSIONS];
    int most_sensors;           /* the longest sensors list, for lower() */
} walker;

/* Where an exit leads: the mission, or -1 (to the game flow, or to a file
 * that isn't there), and its kind. */
static int exit_to(const walker *w, int mid, const mexit *ex, int *kind)
{
    int g;
    if (EXIT_GOES(ex->type)) {
        g = mid / 100 * 100 + (int)(ex->n % 100u);
        *kind = K_NEXT;
    } else if (ex->type == EXIT_CHAPTER) {
        g = (mid / 100 + 1) * 100 + 96;
        *kind = K_CHAPTER;
    } else if (ex->type == EXIT_LOADED) {
        g = mid / 100 * 100;
        *kind = K_LOADED;
    } else {
        *kind = K_FLOW;
        return -1;
    }
    return g < JSRF_WALK_MISSIONS && w->by_id[g] ? g : -1;
}

static void log_exit(trail *t, const mexit *ex)
{
    if (EXIT_GOES(ex->type)) {
        t->has_goes = 1;
        t->goes = ex->ent;
    }
}

static void log_sensor(trail *t, const rule *r)
{
    if (r->op == OP_SOULS && r->p[0] > t->souls) t->souls = r->p[0];
    if (r->op == OP_TANKS && r->p[0] > t->tanks) t->tanks = r->p[0];
}

static trail joined(const trail *a, const trail *b)
{
    trail t = *a;
    if (b->has_goes) {
        t.has_goes = 1;
        t.goes = b->goes;
    }
    if (b->souls > t.souls) t.souls = b->souls;
    if (b->tanks > t.tanks) t.tanks = b->tanks;
    return t;
}

/* The actions, then the first event that holds, until nothing changes. */
static int settle(const mission *m, fset *on, trail *t, const mexit **exit)
{
    int round, i, l;
    for (round = 0; round < SETTLE_ROUNDS; round++) {
        int changed = 0;
        for (i = 0; i < m->nlist[L_TRIG]; i++) {           /* a sensor that always fires isn't the player's */
            const rule *r = m->list[L_TRIG][i];
            if (r && r->op == OP_ALWAYS && test(on, r->pre, r->npre)) {
                fset before = *on;
                put(on, r->eff, r->neff);
                if (!same(on, &before)) changed = 1;
            }
        }
        for (l = L_ACT; l <= L_EV; l++) {
            int ev = l == L_EV, fired = 0;
            for (i = 0; i < m->nlist[l]; i++) {
                const rule *r = m->list[l][i];
                fset before;
                int k;
                if (!r) {
                    fired = 0;
                    continue;
                }
                if (ev && r->op == OP_SKIP) {
                    fired = 0;
                    continue;
                }
                if (r->npre) fired = test(on, r->pre, r->npre);
                else if (ev) fired = 1;                    /* no conditions: always holds */
                if (!fired) continue;
                if (ev && r->op == OP_DEAD) return S_DEAD;
                before = *on;
                put(on, r->eff, r->neff);
                if (!same(on, &before)) changed = 1;
                k = exit_param(r->op);
                if (ev && k >= 0 && r->p[k] < (uint32_t)m->nexits) {
                    *exit = &m->exits[r->p[k]];
                    log_exit(t, *exit);
                    return S_EXIT;
                }
                if (ev && !same(on, &before)) break;      /* an event takes time: the actions run again first */
            }
            if (changed && l == L_ACT) break;              /* the actions settle before an event is looked at */
        }
        if (!changed) break;
    }
    return S_NONE;
}

/* Arriving at a mission, and what follows: the mission it comes to rest in
 * (-1 at a dead end), with on its flags then. 1 in *arrived when the way led
 * to target, on then the flags on arriving there, before its setup. */
static int enter(const walker *w, int mid, fset *on, int target, trail *t, int depth, int *arrived);

static int follow(const walker *w, int mid, fset *on, int target, trail *t, int s, const mexit *ex,
                  int depth, int *arrived)
{
    int g, kind;
    *arrived = 0;
    if (s == S_NONE) return mid;
    if (s == S_DEAD || depth >= MAX_DEPTH) return -1;
    g = exit_to(w, mid, ex, &kind);
    if (g < 0) return -1;
    if (kind == K_CHAPTER) memset(&on->w[BANK_WORDS], 0, BANK_WORDS * sizeof on->w[0]);
    return enter(w, g, on, target, t, depth + 1, arrived);
}

static int after(const walker *w, int mid, fset *on, int target, trail *t, int depth, int *arrived)
{
    const mexit *ex = NULL;
    int s = settle(w->by_id[mid], on, t, &ex);
    return follow(w, mid, on, target, t, s, ex, depth, arrived);
}

static int enter(const walker *w, int mid, fset *on, int target, trail *t, int depth, int *arrived)
{
    const mission *m;
    int l, i;
    memset(on->w, 0, BANK_WORDS * sizeof on->w[0]);       /* the Mission flags are the mission's own */
    *arrived = 0;
    if (mid == target) {
        *arrived = 1;
        return mid;
    }
    m = w->by_id[mid];
    for (l = L_CAST; l <= L_SETUP; l++)
        for (i = 0; i < m->nlist[l]; i++) {
            const rule *r = m->list[l][i];
            if (r && test(on, r->pre, r->npre)) put(on, r->eff, r->neff);
        }
    return after(w, mid, on, target, t, depth, arrived);
}

/* ---- the estimate: walk3.py's Estimate, a relaxed plan's size -------------------------- */

typedef struct {
    int at;                 /* 1: at a mission; 0: a flag */
    int mission;            /* the at's mission; a Mission flag's own; -1 for the others */
    uint16_t f;
    uint8_t v;
} fact;

typedef struct {
    int n, cap;
    int *list;
} ints;

typedef struct {
    uint64_t *keys;         /* the facts by key, open addressing */
    int *ids;
    size_t slots;
    int nfacts, capfacts;
    fact *facts;
    ints *users;            /* the rules each fact is a condition of */
    int nrules, caprules;
    int *pstart, *pn, *estart, *en;
    ints pool_pre, pool_eff;
    int *level, *sup, *queue, *left, *stack;
    unsigned *seen, *used, stamp;
    int ready;
} estimate;

static int ints_add(ints *v, int x)
{
    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 16;
        int *l = (int *)realloc(v->list, sizeof *l * (size_t)cap);
        if (!l) return -1;
        v->list = l;
        v->cap = cap;
    }
    v->list[v->n++] = x;
    return 0;
}

static uint64_t fact_key(const fact *x)
{
    return (uint64_t)x->at << 60 | (uint64_t)(uint32_t)(x->mission + 1) << 24 | (uint64_t)x->f << 1 | x->v;
}

static void est_free(estimate *e)
{
    int i;
    for (i = 0; i < e->nfacts; i++) free(e->users[i].list);
    free(e->keys); free(e->ids); free(e->facts); free(e->users);
    free(e->pstart); free(e->pn); free(e->estart); free(e->en);
    free(e->pool_pre.list); free(e->pool_eff.list);
    free(e->level); free(e->sup); free(e->queue); free(e->left); free(e->stack);
    free(e->seen); free(e->used);
    memset(e, 0, sizeof *e);
}

static int est_grow_keys(estimate *e)
{
    size_t slots = e->slots ? e->slots * 2 : 64, i;   /* small at first, so growing is well used */
    uint64_t *keys = (uint64_t *)malloc(sizeof *keys * slots);
    int *ids = (int *)malloc(sizeof *ids * slots);
    if (!keys || !ids) { free(keys); free(ids); return -1; }
    for (i = 0; i < slots; i++) ids[i] = -1;
    for (i = 0; i < e->slots; i++)
        if (e->ids[i] >= 0) {
            size_t s = (size_t)(e->keys[i] * 0x9E3779B97F4A7C15ull >> 20) & (slots - 1);
            while (ids[s] >= 0) s = (s + 1) & (slots - 1);
            keys[s] = e->keys[i];
            ids[s] = e->ids[i];
        }
    free(e->keys); free(e->ids);
    e->keys = keys;
    e->ids = ids;
    e->slots = slots;
    return 0;
}

/* The fact's number; a new one is added when add is set (-1 when it isn't
 * there, or no room). */
static int est_fact(estimate *e, const fact *x, int add)
{
    uint64_t k = fact_key(x);
    size_t s;
    if (!e->slots && (!add || est_grow_keys(e))) return -1;
    s = (size_t)(k * 0x9E3779B97F4A7C15ull >> 20) & (e->slots - 1);
    while (e->ids[s] >= 0) {
        if (e->keys[s] == k) return e->ids[s];
        s = (s + 1) & (e->slots - 1);
    }
    if (!add) return -1;
    if ((size_t)(e->nfacts + 1) * 2 > e->slots) {
        if (est_grow_keys(e)) return -1;
        return est_fact(e, x, add);
    }
    if (e->nfacts == e->capfacts) {
        int cap = e->capfacts ? e->capfacts * 2 : 256;
        fact *f = (fact *)realloc(e->facts, sizeof *f * (size_t)cap);
        ints *u = (ints *)realloc(e->users, sizeof *u * (size_t)cap);
        if (f) e->facts = f;
        if (u) e->users = u;
        if (!f || !u) return -1;
        e->capfacts = cap;
    }
    e->facts[e->nfacts] = *x;
    memset(&e->users[e->nfacts], 0, sizeof e->users[0]);
    e->keys[s] = k;
    e->ids[s] = e->nfacts;
    return e->nfacts++;
}

static int at_fact(estimate *e, int mid, int add)
{
    fact x;
    x.at = 1;
    x.mission = mid;
    x.f = 0;
    x.v = 0;
    return est_fact(e, &x, add);
}

static int flag_fact(estimate *e, int mid, const cond *c)
{
    fact x;
    x.at = 0;
    x.mission = c->f < BANK_FLAGS ? mid : -1;  /* a Mission flag is its mission's own */
    x.f = c->f;
    x.v = c->v;
    return est_fact(e, &x, 1);
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

/* One rule of the chapter: at its mission, with its conditions, it gives
 * its flags and the mission its exit leads to (goes, or -1). */
static int est_rule(estimate *e, int mid, const cond *pre, int npre, const cond *eff, int neff, int goes)
{
    int n = 0, i, r = e->nrules, kept = 0;
    int *p = (int *)malloc(sizeof *p * (size_t)(npre + 1));
    if (!p) return -1;
    if ((p[n++] = at_fact(e, mid, 1)) < 0) goto fail;
    for (i = 0; i < npre; i++)
        if ((p[n++] = flag_fact(e, mid, &pre[i])) < 0) goto fail;
    qsort(p, (size_t)n, sizeof *p, cmp_int);
    if (e->nrules == e->caprules) {
        int cap = e->caprules ? e->caprules * 2 : 1024;
        int *a = (int *)realloc(e->pstart, sizeof *a * (size_t)cap), *b, *c, *d;
        if (a) e->pstart = a;
        b = (int *)realloc(e->pn, sizeof *b * (size_t)cap);
        if (b) e->pn = b;
        c = (int *)realloc(e->estart, sizeof *c * (size_t)cap);
        if (c) e->estart = c;
        d = (int *)realloc(e->en, sizeof *d * (size_t)cap);
        if (d) e->en = d;
        if (!a || !b || !c || !d) goto fail;
        e->caprules = cap;
    }
    e->pstart[r] = e->pool_pre.n;
    for (i = 0; i < n; i++) {
        if (i && p[i] == p[i - 1]) continue;
        if (ints_add(&e->pool_pre, p[i]) || ints_add(&e->users[p[i]], r)) goto fail;
        kept++;
    }
    e->pn[r] = kept;
    e->estart[r] = e->pool_eff.n;
    for (i = 0; i < neff; i++) {
        int x = flag_fact(e, mid, &eff[i]);
        if (x < 0 || ints_add(&e->pool_eff, x)) goto fail;
    }
    if (goes >= 0) {
        int x = at_fact(e, goes, 1);
        if (x < 0 || ints_add(&e->pool_eff, x)) goto fail;
    }
    e->en[r] = e->pool_eff.n - e->estart[r];
    e->nrules++;
    free(p);
    return 0;
fail:
    free(p);
    return -1;
}

/* Room for est_h's work, once the chapter's rules are all in. */
static int est_ready(estimate *e)
{
    size_t nf = (size_t)e->nfacts + 1, nr = (size_t)e->nrules + 1;
    e->level = (int *)malloc(sizeof(int) * nf);
    e->sup = (int *)malloc(sizeof(int) * nf);
    e->queue = (int *)malloc(sizeof(int) * nf);
    e->left = (int *)malloc(sizeof(int) * nr);
    e->stack = (int *)malloc(sizeof(int) * ((size_t)e->pool_pre.n + 1));
    e->seen = (unsigned *)calloc(nf, sizeof(unsigned));
    e->used = (unsigned *)calloc(nr, sizeof(unsigned));
    e->ready = e->level && e->sup && e->queue && e->left && e->stack && e->seen && e->used;
    return e->ready ? 0 : -1;
}

static int holds(const fact *x, int here, const fset *on)
{
    int h;
    if (x->at) return x->mission == here;
    if (x->f < BANK_FLAGS) h = x->mission == here && has(on, x->f);   /* clear anywhere else */
    else h = has(on, x->f);
    return h == x->v;
}

/* How many rules a relaxed plan from (here, on) to target takes; -1 when
 * there is none. */
static int est_h(estimate *e, int here, const fset *on, int target)
{
    int goal = at_fact(e, target, 0), qn = 0, k = 0, i, sn = 0, count = 0;
    if (goal < 0) return -1;
    for (i = 0; i < e->nfacts; i++) {
        e->level[i] = -1;
        e->sup[i] = -1;
        if (holds(&e->facts[i], here, on)) {
            e->level[i] = 0;
            e->queue[qn++] = i;
        }
    }
    memcpy(e->left, e->pn, sizeof(int) * (size_t)e->nrules);
    while (k < qn) {
        int x = e->queue[k++], u;
        if (x == goal) break;
        for (u = 0; u < e->users[x].n; u++) {
            int r = e->users[x].list[u];
            if (--e->left[r] == 0) {
                int j;
                for (j = 0; j < e->en[r]; j++) {
                    int f = e->pool_eff.list[e->estart[r] + j];
                    if (e->level[f] < 0) {
                        e->level[f] = e->level[x] + 1;
                        e->sup[f] = r;
                        e->queue[qn++] = f;
                    }
                }
            }
        }
    }
    if (e->level[goal] < 0) return -1;
    if (++e->stamp == 0) {          /* the marks wrapped: start them again */
        memset(e->seen, 0, sizeof(unsigned) * (size_t)e->nfacts);
        memset(e->used, 0, sizeof(unsigned) * (size_t)e->nrules);
        e->stamp = 1;
    }
    e->stack[sn++] = goal;
    while (sn) {
        int x = e->stack[--sn], r;
        if (e->seen[x] == e->stamp) continue;
        e->seen[x] = e->stamp;
        r = e->sup[x];
        if (r >= 0 && e->used[r] != e->stamp) {
            int j;
            e->used[r] = e->stamp;
            count++;
            for (j = 0; j < e->pn[r]; j++) e->stack[sn++] = e->pool_pre.list[e->pstart[r] + j];
        }
    }
    return count;
}

/* ---- the chapter's rules: walk3.py's build, guards and guarded ---------------------- */

typedef struct { int k, gi, nother; cond other[CONDS]; } guard;

static int consistent(const cond *c, int n)
{
    int i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < i; j++)
            if (c[j].f == c[i].f) {
                if (c[j].v != c[i].v) return 0;
                break;          /* the first one for the flag is what counts */
            }
    return 1;
}

static int in_conds(const cond *c, int n, uint16_t f, int v)
{
    int i;
    for (i = 0; i < n; i++)
        if (c[i].f == f && c[i].v == v) return 1;
    return 0;
}

/* The actions that take a Mission flag back: an entry that waits for
 * M_k = 1 and sets M_k = 0 ("you can't leave yet"). */
static int guards_of(const mission *m, guard **out)
{
    int i, j, n = 0, cap = 0;
    guard *g = NULL;
    for (i = 0; i < m->nlist[L_ACT]; i++) {
        const rule *r = m->list[L_ACT][i];
        if (!r) continue;
        for (j = 0; j < r->neff; j++) {
            const cond *c = &r->eff[j];
            int q;
            if (c->f >= BANK_FLAGS || c->v != 0 || !in_conds(r->pre, r->npre, c->f, 1)) continue;
            if (n == cap) {
                guard *ng;
                cap = cap ? cap * 2 : 8;
                ng = (guard *)realloc(g, sizeof *g * (size_t)cap);
                if (!ng) { free(g); return -1; }
                g = ng;
            }
            g[n].k = c->f;
            g[n].gi = i;
            g[n].nother = 0;
            for (q = 0; q < r->npre; q++)
                if (!(r->pre[q].f == c->f && r->pre[q].v == 1)) g[n].other[g[n].nother++] = r->pre[q];
            n++;
        }
    }
    *out = g;
    return n;
}

typedef struct { int n; cond *c; } way;

/* The ways an entry's conditions can hold once the guards are counted, each
 * added to the estimate as a rule of its own. */
static int add_guarded(estimate *e, const mission *m, const guard *g, int ng, const rule *r, int goes)
{
    int room = r->npre + 1, nways = 1, i, gi, ok = 0;
    way ways[WAYS_MAX], next[WAYS_MAX];
    cond *store;
    /* A way gains one condition for each guard it is put behind. */
    for (i = 0; i < r->npre; i++)
        if (r->pre[i].f < BANK_FLAGS && r->pre[i].v == 1)
            for (gi = 0; gi < ng; gi++) room += g[gi].k == r->pre[i].f;
    store = (cond *)malloc(sizeof *store * (size_t)room * WAYS_MAX * 2);
    if (!store) return -1;
    for (i = 0; i < WAYS_MAX; i++) {
        ways[i].c = store + (size_t)room * i;
        next[i].c = store + (size_t)room * (WAYS_MAX + i);
    }
    ways[0].n = r->npre;
    memcpy(ways[0].c, r->pre, sizeof *r->pre * (size_t)r->npre);
    if (r->list == L_EV || r->list == L_ACT) {
        for (i = 0; i < r->npre; i++) {
            const cond *f = &r->pre[i];
            if (f->f >= BANK_FLAGS || f->v != 1) continue;
            for (gi = 0; gi < ng; gi++) {
                const guard *gd = &g[gi];
                int q, w, nn = 0, skip = 0;
                if (gd->k != f->f) continue;
                if (r->list == L_ACT && r->index <= gd->gi) continue;      /* it runs before the guard does */
                for (q = 0; q < gd->nother; q++)
                    if (in_conds(r->pre, r->npre, gd->other[q].f, 1 - gd->other[q].v)) skip = 1;
                if (skip) continue;     /* the entry already asks for the guard to be off */
                for (w = 0; w < nways && nn < WAYS_MAX; w++)
                    for (q = 0; q < gd->nother && nn < WAYS_MAX; q++) {
                        if (in_conds(ways[w].c, ways[w].n, gd->other[q].f, gd->other[q].v)) continue;
                        memcpy(next[nn].c, ways[w].c, sizeof *ways[w].c * (size_t)ways[w].n);
                        next[nn].n = ways[w].n;
                        next[nn].c[next[nn].n].f = gd->other[q].f;
                        next[nn].c[next[nn].n].v = (uint8_t)(1 - gd->other[q].v);
                        next[nn].n++;
                        nn++;
                    }
                for (w = 0; w < nn; w++) {
                    way t = ways[w];
                    ways[w] = next[w];
                    next[w] = t;
                }
                nways = nn;
            }
        }
    }
    for (i = 0; i < nways && !ok; i++)
        if (consistent(ways[i].c, ways[i].n) &&
            est_rule(e, m->id, ways[i].c, ways[i].n, r->eff, r->neff, goes)) ok = -1;
    free(store);
    return ok;
}

/* The chapter's rules, in walk3.py's build order, into the estimate. */
static int est_chapter(const walker *w, estimate *e, int chapter)
{
    int id;
    for (id = chapter * 100; id < chapter * 100 + 100; id++) {
        const mission *m = w->by_id[id];
        guard *g = NULL;
        int ng, i;
        if (!m) continue;
        if ((ng = guards_of(m, &g)) < 0) return -1;
        for (i = 0; i < m->nrules; i++) {
            const rule *r = m->rules[i];
            int goes = -1, k;
            if (!consistent(r->pre, r->npre)) continue;
            if (r->op == OP_SKIP || r->op == OP_DEAD) continue;
            k = exit_param(r->op);
            if (r->list == L_EV && k >= 0) {
                int kind;
                if (r->p[k] >= (uint32_t)m->nexits) continue;
                goes = exit_to(w, m->id, &m->exits[r->p[k]], &kind);
                if (goes < 0) continue;
            } else if (!r->neff) {
                continue;
            }
            if (add_guarded(e, m, g, ng, r, goes)) {
                free(g);
                return -1;
            }
        }
        free(g);
    }
    return est_ready(e);
}

/* ---- the search -------------------------------------------------------------------- */

typedef struct {
    int mid, seen;
    trail tr;               /* the way to it, once it is taken off the queue */
    fset on;
} state;

typedef struct {
    int f, g;
    unsigned tie;
    int s;
    trail tr;
} entry;

typedef struct {
    state *st;
    int nst, capst;
    int *slot;
    size_t slots;
    entry *heap;
    int nheap, capheap;
    unsigned tie;
} searcher;

static uint64_t state_hash(int mid, const fset *on)
{
    uint64_t h = 1469598103934665603ull ^ (uint64_t)mid;
    unsigned i;
    for (i = 0; i < WORDS; i++) h = (h ^ on->w[i]) * 1099511628211ull ^ (h >> 29);
    return h;
}

static void searcher_free(searcher *s)
{
    free(s->st);
    free(s->slot);
    free(s->heap);
    memset(s, 0, sizeof *s);
}

static int searcher_grow(searcher *s)
{
    size_t slots = s->slots ? s->slots * 2 : 256, i;
    int *slot = (int *)malloc(sizeof *slot * slots);
    if (!slot) return -1;
    for (i = 0; i < slots; i++) slot[i] = -1;
    for (i = 0; i < (size_t)s->nst; i++) {
        size_t k = (size_t)state_hash(s->st[i].mid, &s->st[i].on) & (slots - 1);
        while (slot[k] >= 0) k = (k + 1) & (slots - 1);
        slot[k] = (int)i;
    }
    free(s->slot);
    s->slot = slot;
    s->slots = slots;
    return 0;
}

/* The state's number, made the first time it is met; -1 for no room. */
static int intern(searcher *s, int mid, const fset *on)
{
    size_t k;
    if (((size_t)s->nst + 1) * 2 > s->slots && searcher_grow(s)) return -1;
    k = (size_t)state_hash(mid, on) & (s->slots - 1);
    while (s->slot[k] >= 0) {
        const state *t = &s->st[s->slot[k]];
        if (t->mid == mid && same(&t->on, on)) return s->slot[k];
        k = (k + 1) & (s->slots - 1);
    }
    if (s->nst == s->capst) {
        int cap = s->capst ? s->capst * 2 : 4096;
        state *n = (state *)realloc(s->st, sizeof *n * (size_t)cap);
        if (!n) return -1;
        s->st = n;
        s->capst = cap;
    }
    memset(&s->st[s->nst], 0, sizeof s->st[0]);
    s->st[s->nst].mid = mid;
    s->st[s->nst].on = *on;
    s->slot[k] = s->nst;
    return s->nst++;
}

static int before(const entry *a, const entry *b)
{
    if (a->f != b->f) return a->f < b->f;
    if (a->g != b->g) return a->g < b->g;
    return a->tie < b->tie;
}

static int push(searcher *s, int f, int g, int st, const trail *tr)
{
    int i;
    if (s->nheap == s->capheap) {
        int cap = s->capheap ? s->capheap * 2 : 4096;
        entry *h = (entry *)realloc(s->heap, sizeof *h * (size_t)cap);
        if (!h) return -1;
        s->heap = h;
        s->capheap = cap;
    }
    i = s->nheap++;
    s->heap[i].f = f;
    s->heap[i].g = g;
    s->heap[i].tie = s->tie++;
    s->heap[i].s = st;
    s->heap[i].tr = *tr;
    while (i > 0) {
        int p = (i - 1) / 2;
        entry t;
        if (!before(&s->heap[i], &s->heap[p])) break;
        t = s->heap[i];
        s->heap[i] = s->heap[p];
        s->heap[p] = t;
        i = p;
    }
    return 0;
}

static entry pop(searcher *s)
{
    entry top = s->heap[0];
    int i = 0;
    s->heap[0] = s->heap[--s->nheap];
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        entry t;
        if (l < s->nheap && before(&s->heap[l], &s->heap[m])) m = l;
        if (r < s->nheap && before(&s->heap[r], &s->heap[m])) m = r;
        if (m == i) break;
        t = s->heap[i];
        s->heap[i] = s->heap[m];
        s->heap[m] = t;
        i = m;
    }
    return top;
}

/* For a sensor on a kept count: the mission's other sensors on the same
 * count that ask for less, least first (a stable sort, as walk3.py's). */
static int lower(const mission *m, const rule *r, const rule **out)
{
    int i, n = 0, a, b;
    if (r->op != OP_SOULS && r->op != OP_TANKS) return 0;
    for (i = 0; i < m->nlist[L_TRIG]; i++) {
        const rule *g = m->list[L_TRIG][i];
        if (g && g->op == r->op && g->p[0] < r->p[0]) out[n++] = g;
    }
    for (a = 1; a < n; a++)
        for (b = a; b > 0 && out[b - 1]->p[0] > out[b]->p[0]; b--) {
            const rule *t = out[b];
            out[b] = out[b - 1];
            out[b - 1] = t;
        }
    return n;
}

/* Best first from start, with init's flags, to the entry of target. 1 found
 * (the way's trail and the flags on arrival in *got), 0 not, -1 no room.
 * *looked: the states looked at, as walk3.py counts them. */
static int search(const walker *w, estimate *e, int start, int target, const fset *init, arrival *got,
                  int *looked)
{
    searcher s;
    fset on;
    trail t0;
    int mid, arrived, n = 0, found = 0, s0;
    const rule **low = (const rule **)malloc(sizeof *low * (size_t)(w->most_sensors + 1));
    memset(&s, 0, sizeof s);
    *looked = 0;
    if (!low) return -1;
    memset(&t0, 0, sizeof t0);
    on = *init;
    mid = enter(w, start, &on, target, &t0, 0, &arrived);
    if (arrived || mid < 0) {
        free((void *)low);
        if (!arrived) return 0;
        got->have = 1;
        got->tr = t0;
        got->on = on;
        return 1;
    }
    if ((s0 = intern(&s, mid, &on)) < 0 || push(&s, 0, 0, s0, &t0)) goto no_room;
    while (s.nheap) {
        entry top = pop(&s);
        state *st = &s.st[top.s];
        const mission *m;
        int i;
        if (st->seen) continue;
        st->seen = 1;
        st->tr = top.tr;
        if (++n > SEARCH_LIMIT) break;
        m = w->by_id[st->mid];
        for (i = 0; i < m->nlist[L_TRIG]; i++) {
            const rule *r = m->list[L_TRIG][i];
            trail tr;
            const mexit *ex = NULL;
            int sx = S_NONE, nl, j, to, h, here = top.s;
            if (!r || r->op == OP_ALWAYS || !test(&s.st[here].on, r->pre, r->npre)) continue;
            on = s.st[here].on;
            memset(&tr, 0, sizeof tr);
            nl = lower(m, r, low);
            for (j = 0; j < nl; j++)                       /* the count passed these on its way up */
                if (test(&on, low[j]->pre, low[j]->npre)) {
                    put(&on, low[j]->eff, low[j]->neff);
                    log_sensor(&tr, low[j]);
                    sx = settle(m, &on, &tr, &ex);
                    if (sx != S_NONE) break;
                }
            if (sx != S_NONE) {
                mid = follow(w, m->id, &on, target, &tr, sx, ex, 0, &arrived);
            } else {
                if (test(&on, r->pre, r->npre)) {
                    put(&on, r->eff, r->neff);
                    log_sensor(&tr, r);
                }
                mid = after(w, m->id, &on, target, &tr, 0, &arrived);
            }
            if (arrived) {
                got->have = 1;
                got->tr = joined(&s.st[here].tr, &tr);
                got->on = on;
                found = 1;
                goto done;
            }
            if (mid < 0) continue;
            if ((to = intern(&s, mid, &on)) < 0) goto no_room;
            if (to == here || s.st[to].seen) continue;
            h = est_h(e, mid, &on, target);
            if (h < 0) continue;
            tr = joined(&s.st[here].tr, &tr);
            if (push(&s, top.g + 1 + h * WEIGHT, top.g + 1, to, &tr)) goto no_room;
        }
    }
done:
    *looked = n;
    free((void *)low);
    searcher_free(&s);
    return found;
no_room:
    free((void *)low);
    searcher_free(&s);
    return -1;
}

/* ---- the story, chapter by chapter, and the lines ------------------------------------ */

static void line(char *out, size_t n, int mid, const arrival *a, int with_entrance, int off_story)
{
    static const char keys[4] = { 'C', 'G', 'R', 'S' };
    size_t len = 0;
    int b;
#define ADD(...) do { if (len < n) len += (size_t)snprintf(out + len, n - len, __VA_ARGS__); } while (0)
    ADD("%04d", mid);
    if (with_entrance && a->tr.has_goes) {
        if (a->tr.goes == 0xFFFFFFFFu) ADD(" e=-1");
        else ADD(" e=%u", (unsigned)a->tr.goes);
    }
    for (b = 0; b < 4; b++) {               /* the Chapter, Global and bank 3 flags, then the skaters */
        unsigned first = (unsigned)(b + 1) * BANK_FLAGS, count = b < 3 ? BANK_FLAGS : SKATERS, i;
        int any = 0;
        for (i = 0; i < count; i++)
            if (has(&a->on, first + i)) {
                if (any) ADD(",%u", i);
                else ADD(" %c=%u", keys[b], i);
                any = 1;
            }
    }
    if (a->tr.souls > NEW_GAME_SOULS) ADD(" souls=%u", (unsigned)a->tr.souls);
    if (a->tr.tanks) ADD(" tanks=%u", (unsigned)a->tr.tanks);
    if (off_story) ADD(" story=0");
#undef ADD
}

typedef struct { char *s; size_t n, cap; } text;

static int text_add(text *t, const char *s)
{
    size_t l = strlen(s);
    if (t->n + l + 2 > t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 65536;
        char *n;
        while (cap < t->n + l + 2) cap *= 2;
        n = (char *)realloc(t->s, cap);
        if (!n) return -1;
        t->s = n;
        t->cap = cap;
    }
    memcpy(t->s + t->n, s, l);
    t->n += l;
    t->s[t->n++] = '\n';
    t->s[t->n] = 0;
    return 0;
}

static trail most(const trail *a, const trail *b)
{
    trail t = *b;
    if (a->souls > t.souls) t.souls = a->souls;
    if (a->tanks > t.tanks) t.tanks = a->tanks;
    return t;
}

#ifdef JSRF_WALK_TRACE
/* For the checks against walk3.py: every mission the walk reached, as its
 * line, whether the walk went on to the end or not. (Each search's count of
 * states looked at goes to stderr as it ends, in the story loop.) */
static void trace(const arrival *res, const char *ent_ok)
{
    char buf[16384];
    int id;
    for (id = 0; id < JSRF_WALK_MISSIONS; id++)
        if (res[id].have) {
            line(buf, sizeof buf, id, &res[id], ent_ok[id], 0);
            fprintf(stderr, "reached %s\n", buf);
        }
}
#else
#define trace(res, ent_ok) ((void)0)
#endif

static int walk_story(walker *w, jsrf_walk *out)
{
    arrival *res = (arrival *)calloc(JSRF_WALK_MISSIONS, sizeof *res);
    char *ent_ok = (char *)calloc(JSRF_WALK_MISSIONS, 1);      /* the line names its entrance */
    fset init;
    trail sofar;
    int start = NEW_GAME, c, id, r = -1;
    text t = { NULL, 0, 0 };
    char buf[16384];
    if (!res || !ent_ok) {
        snprintf(out->why, sizeof out->why, "There isn't the memory to walk the story.");
        goto end;
    }
    if (!w->by_id[NEW_GAME]) {
        snprintf(out->why, sizeof out->why, "The game's mission files have no 0101, where New Game starts.");
        goto end;
    }
    memset(&init, 0, sizeof init);
    put1(&init, FLAG_SKATER(1), 1);         /* New Game's skaters (the progress reset, sub_0003AED0) */
    put1(&init, FLAG_SKATER(2), 1);
    put1(&init, FLAG_SKATER(6), 1);
    memset(&sofar, 0, sizeof sofar);
    for (c = 1; c < 10; c++) {
        estimate e;
        int nxt = (c + 1) * 100 + 96, mid, arrived;
        fset left = init;
        trail scratch;
        memset(&e, 0, sizeof e);
        memset(&scratch, 0, sizeof scratch);
        if (est_chapter(w, &e, c)) {
            est_free(&e);
            snprintf(out->why, sizeof out->why, "There isn't the memory to walk the story.");
            goto end;
        }
        mid = enter(w, start, &left, -1, &scratch, 0, &arrived);
        if (mid < 0 && start % 100 == 96 && w->by_id[c * 100 + 97]) {
            /* It leaves to the game flow (the game goes back to the title):
             * on from the chapter's garage on Load Game, with what it left. */
            res[start].have = 1;
            res[start].on = init;
            memset(&res[start].tr, 0, sizeof res[start].tr);
            res[start].tr.souls = sofar.souls;
            res[start].tr.tanks = sofar.tanks;
            ent_ok[start] = 0;
            start = c * 100 + 97;
            init = left;
        }
        for (id = c * 100; id <= c * 100 + 100; id++) {
            int target = id < c * 100 + 100 ? id : nxt, s, looked;
            arrival got;
            if (target >= JSRF_WALK_MISSIONS || !w->by_id[target] || res[target].have) continue;
            memset(&got, 0, sizeof got);
            s = search(w, &e, start, target, &init, &got, &looked);
#ifdef JSRF_WALK_TRACE
            if (s >= 0) fprintf(stderr, "searched %04d %d\n", target, looked);
#endif
            if (s < 0) {
                est_free(&e);
                snprintf(out->why, sizeof out->why, "There isn't the memory to walk the story.");
                goto end;
            }
            if (!s) continue;
            got.tr = most(&sofar, &got.tr);
            res[target] = got;
            ent_ok[target] = 1;
            if (target == nxt) {
                start = nxt;
                init = got.on;
                sofar = got.tr;
            }
        }
        est_free(&e);
        if (c < 9 && start != nxt) {
            fail(out->why, sizeof out->why, "The walk found no way to chapter %d's clear, so its missions "
                 "and the chapters after it can't be given a start (the way to %04d).", c, nxt);
            goto end;
        }
    }
    for (id = 0; id < JSRF_WALK_MISSIONS; id++) {
        int story = id >= 100 && id < 1000 && w->by_id[id];
        if (story) out->story++;
        if (res[id].have) {
            if (story) out->reached++;
            line(buf, sizeof buf, id, &res[id], ent_ok[id], 0);
        } else if (story) {
            int opening = id / 100 * 100 + 96;
            if (!res[opening].have) {
                fail(out->why, sizeof out->why, "%04d isn't on the story's way, and its chapter's opening, "
                     "%04d, wasn't reached either.", id, opening);
                goto end;
            }
            out->marked++;
            line(buf, sizeof buf, id, &res[opening], 0, 1);
        } else {
            continue;
        }
        if (text_add(&t, buf)) {
            snprintf(out->why, sizeof out->why, "There isn't the memory to walk the story.");
            goto end;
        }
    }
    out->text = t.s;
    out->len = t.n;
    t.s = NULL;
    r = 0;
end:
    if (res && ent_ok) trace(res, ent_ok);
    free(t.s);
    free(res);
    free(ent_ok);
    return r;
}

/* ---- the files ---------------------------------------------------------------------- */

int jsrf_mission_id(const char *name)
{
    int i, id = 0;
    if (!name || strlen(name) != 12) return -1;
    for (i = 0; i < 4; i++)
        if ((name[i] | 0x20) != "mssn"[i] || (name[8 + i] | 0x20) != ".bin"[i]) return -1;
    for (i = 4; i < 8; i++) {
        if (name[i] < '0' || name[i] > '9') return -1;
        id = id * 10 + (name[i] - '0');
    }
    return id;
}

int jsrf_walk_states(const jsrf_mission_file *files, int n, jsrf_walk *out)
{
    walker *w;
    int i, r;
    memset(out, 0, sizeof *out);
    w = (walker *)calloc(1, sizeof *w);
    if (!w) {
        snprintf(out->why, sizeof out->why, "There isn't the memory to walk the story.");
        return -1;
    }
    for (i = 0; i < n; i++) {
        mission *m;
        int id = files[i].id;
        if (id < 0 || id >= JSRF_WALK_MISSIONS) {
            fail(out->why, sizeof out->why, "A mission file is numbered %d, outside 0000 to 9999.", id, 0);
            goto bad;
        }
        if (w->by_id[id]) {
            fail(out->why, sizeof out->why, "There are two files for mission %04d.", id, 0);
            goto bad;
        }
        m = (mission *)take(&w->a, sizeof *m);
        if (!m) {
            snprintf(out->why, sizeof out->why, "There isn't the memory to read the mission files.");
            goto bad;
        }
        m->id = id;
        if (parse(&w->a, m, files[i].data, files[i].size)) {
            fail(out->why, sizeof out->why, "mssn%04d.bin isn't laid out as the game's mission files are.", id, 0);
            goto bad;
        }
        w->by_id[id] = m;
        if (m->nlist[L_TRIG] > w->most_sensors) w->most_sensors = m->nlist[L_TRIG];
    }
    r = walk_story(w, out);
    arena_free(&w->a);
    free(w);
    return r;
bad:
    arena_free(&w->a);
    free(w);
    return -1;
}

/* ---- the game's folder ---------------------------------------------------------------- */

typedef struct {
    const char *want;           /* a folder's name, any case */
    char found[256];
} find_ctx;

static int find_each(void *ctx, const char *name, int is_dir)
{
    find_ctx *f = (find_ctx *)ctx;
    size_t i;
    if (!is_dir || strlen(name) != strlen(f->want)) return 0;
    for (i = 0; name[i]; i++)
        if ((name[i] | 0x20) != (f->want[i] | 0x20)) return 0;
    snprintf(f->found, sizeof f->found, "%s", name);
    return 1;
}

typedef struct {
    char dir[4096];
    jsrf_mission_file *files;
    int n, cap, bad;
    char why[400];
} files_ctx;

static int file_each(void *ctx, const char *name, int is_dir)
{
    files_ctx *f = (files_ctx *)ctx;
    char path[4096 + 64];
    unsigned char *data = NULL;
    size_t size = 0;
    int id = jsrf_mission_id(name);
    if (is_dir || id < 0) return 0;
    if (f->n == f->cap) {
        int cap = f->cap ? f->cap * 2 : 512;
        jsrf_mission_file *n = (jsrf_mission_file *)realloc(f->files, sizeof *n * (size_t)cap);
        if (!n) {
            snprintf(f->why, sizeof f->why, "There isn't the memory to read the mission files.");
            return 1;
        }
        f->files = n;
        f->cap = cap;
    }
    js_path(path, sizeof path, f->dir, name);
    if (js_read_file(path, JSRF_WALK_FILE_MAX, &data, &size)) {
        snprintf(f->why, sizeof f->why, "%.360s couldn't be read.", path);
        return 1;
    }
    f->files[f->n].id = id;
    f->files[f->n].data = data;
    f->files[f->n].size = size;
    f->n++;
    return 0;
}

int jsrf_walk_game(const char *game_dir, jsrf_walk *out)
{
    files_ctx *f = (files_ctx *)calloc(1, sizeof *f);
    find_ctx media, folder;
    char dir[4096];
    int i, r = -1;
    memset(out, 0, sizeof *out);
    if (!f) {
        snprintf(out->why, sizeof out->why, "There isn't the memory to read the mission files.");
        return -1;
    }
    media.want = "Media";
    folder.want = "Mission";
    if (js_list_dir(game_dir, find_each, &media) != 1) {
        snprintf(out->why, sizeof out->why, "%.300s has no Media folder, where the game keeps its missions.", game_dir);
        goto end;
    }
    js_path(dir, sizeof dir, game_dir, media.found);
    if (js_list_dir(dir, find_each, &folder) != 1) {
        snprintf(out->why, sizeof out->why, "%.360s has no Mission folder.", dir);
        goto end;
    }
    js_path(f->dir, sizeof f->dir, dir, folder.found);
    if (js_list_dir(f->dir, file_each, f) != 0) {
        snprintf(out->why, sizeof out->why, "%s", f->why[0] ? f->why : "The Mission folder couldn't be read.");
        goto end;
    }
    if (!f->n) {
        snprintf(out->why, sizeof out->why, "%.360s has no mission files in it.", f->dir);
        goto end;
    }
    r = jsrf_walk_states(f->files, f->n, out);
end:
    for (i = 0; i < f->n; i++) free((void *)f->files[i].data);
    free(f->files);
    free(f);
    return r;
}

void jsrf_walk_free(jsrf_walk *w)
{
    free(w->text);
    memset(w, 0, sizeof *w);
}

/* ---- start-states.txt in the settings file's folder ---------------------------------- */

#define STATES_BACKUP_MAX (64u << 20)   /* the largest start-states.txt backed up */

/* A new dated folder in backups, named as jsrf_backup (jsrf_save.c) names
 * the saves' backups: "2026-11-26 21.30.00", then " (2)" and on for a second
 * in the same second. 0, or -1. */
static int dated(const char *backups, char *dir, size_t n)
{
    char stamp[64], name[80];
    time_t now = time(NULL);
    struct tm tm;
    int i, r = 1;
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", &tm);
    if (js_mkdirs(backups) != 0) return -1;
    for (i = 1; i < 100 && r == 1; i++) {
        if (i == 1) snprintf(name, sizeof name, "%s", stamp);
        else snprintf(name, sizeof name, "%s (%d)", stamp, i);
        js_path(dir, n, backups, name);
        r = js_mkdir_new(dir);
    }
    return r == 0 ? 0 : -1;
}

/* file copied into a new dated folder in backups. */
static int back_up(const char *file, const char *backups, char *backup, size_t bn, char *why, size_t wn)
{
    unsigned char *data = NULL;
    size_t size = 0;
    char dir[4096], to[4200];
    if (js_read_file(file, STATES_BACKUP_MAX, &data, &size) != 0) {
        snprintf(why, wn, "%.360s couldn't be read to back it up, so nothing was changed.", file);
        return -1;
    }
    if (dated(backups, dir, sizeof dir) != 0) {
        free(data);
        snprintf(why, wn, "No backup folder could be made in %.330s, so nothing was changed.", backups);
        return -1;
    }
    js_path(to, sizeof to, dir, JSRF_WALK_FILE);
    if (js_write_file(to, data, size) != 0) {
        free(data);
        js_rmtree(dir);
        snprintf(why, wn, "The backup couldn't be written in %.330s, so nothing was changed.", dir);
        return -1;
    }
    free(data);
    snprintf(backup, bn, "%s", dir);
    return 0;
}

int jsrf_states_write(const char *dir, const char *backups, const char *text, size_t len,
                      char *backup, size_t bn, char *why, size_t wn)
{
    char file[4200], fresh[4300];
    backup[0] = why[0] = 0;
    if (js_mkdirs(dir) != 0) {
        snprintf(why, wn, "%.360s couldn't be made.", dir);
        return -1;
    }
    js_path(file, sizeof file, dir, JSRF_WALK_FILE);
    snprintf(fresh, sizeof fresh, "%s.new", file);
    /* Written whole beside it first, so a full disk changes nothing. */
    if (js_write_file(fresh, text, len) != 0) {
        snprintf(why, wn, "%.360s couldn't be written, so nothing was changed.", fresh);
        return -1;
    }
    if (js_is_file(file)) {
        if (back_up(file, backups, backup, bn, why, wn) != 0) {
            js_rmtree(fresh);
            return -1;
        }
        if (js_rmtree(file) != 0) {
            js_rmtree(fresh);
            snprintf(why, wn, "%.360s couldn't be replaced (is it open somewhere?), so it was left as it was.", file);
            return -1;
        }
    }
    if (js_rename_new(fresh, file) != 0) {
        snprintf(why, wn, "The new start-states.txt couldn't be put in place: it is %.360s.new.", file);
        return -1;
    }
    return 0;
}

int jsrf_states_remove(const char *dir, const char *backups, char *backup, size_t bn, char *why, size_t wn)
{
    char file[4200];
    backup[0] = why[0] = 0;
    js_path(file, sizeof file, dir, JSRF_WALK_FILE);
    if (!js_is_file(file)) return 0;
    if (back_up(file, backups, backup, bn, why, wn) != 0) return -1;
    if (js_rmtree(file) != 0) {
        snprintf(why, wn, "%.360s couldn't be removed (is it open somewhere?). It is backed up all the same.", file);
        return -1;
    }
    return 0;
}

int jsrf_states_same(const char *dir, const char *text, size_t len)
{
    char file[4200];
    unsigned char *data = NULL;
    size_t size = 0;
    int same;
    js_path(file, sizeof file, dir, JSRF_WALK_FILE);
    if (!js_is_file(file) || js_read_file(file, len + 1, &data, &size) != 0) return 0;
    same = size == len && (!len || !memcmp(data, text, len));
    free(data);
    return same;
}
