/* The input latency log (nv2a_latency.h). */
#include "nv2a_latency.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LAT_WAITING, LAT_PASSED, LAT_HANDED, LAT_DONE };

#define LAT_LINE 512            /* room for a line */
#define MS(s) ((s) * 1000.0)

int nv2a_lat_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_INPUT_LATENCY");
        on = e && *e && atoi(e) != 0;
    }
    return on;
}

static void say(Nv2aLat *L, const char *line)
{
    if (L->say) {
        L->say(line);
    } else {
        fprintf(stderr, "%s\n", line);
        fflush(stderr);
    }
}

void nv2a_lat_read(Nv2aLat *L, const char *what, int key, unsigned pass,
                   double sdl, double before, double read, uint32_t put)
{
    unsigned in = __atomic_load_n(&L->q_in, __ATOMIC_RELAXED);
    unsigned out = __atomic_load_n(&L->q_out, __ATOMIC_ACQUIRE);
    Nv2aLatPress *p;
    if (in - out >= NV2A_LAT_QUEUE) {
        __atomic_fetch_add(&L->q_full, 1u, __ATOMIC_RELAXED);
        return;
    }
    p = &L->queue[in % NV2A_LAT_QUEUE];
    memset(p, 0, sizeof *p);
    p->what = what ? what : "?";
    p->key = key != 0;
    p->pass = pass;
    p->sdl = sdl;
    p->before = before;
    p->read = read;
    p->put = put;
    __atomic_store_n(&L->q_in, in + 1u, __ATOMIC_RELEASE);
}

void nv2a_lat_swapped(Nv2aLat *L, uint64_t serial, double at, double refresh)
{
    unsigned in = __atomic_load_n(&L->s_in, __ATOMIC_RELAXED);
    unsigned out = __atomic_load_n(&L->s_out, __ATOMIC_ACQUIRE);
    Nv2aLatSwap *s;
    if (in - out >= NV2A_LAT_SWAPS) {
        __atomic_fetch_add(&L->s_full, 1u, __ATOMIC_RELAXED);
        return;
    }
    s = &L->swaps[in % NV2A_LAT_SWAPS];
    s->serial = serial;
    s->at = at;
    s->refresh = refresh;
    __atomic_store_n(&L->s_in, in + 1u, __ATOMIC_RELEASE);
}

/* Whether x lies after the flip at `from` and up to the one at `to`, going
 * round the ring from `from`: past its end when `to` is below `from`. */
static int ring_between(uint32_t from, uint32_t to, uint32_t x)
{
    if (from < to) return x > from && x <= to;
    if (from > to) return x > from || x <= to;
    return 0;
}

static void stage_add(Nv2aLatStage *s, double ms)
{
    float v = (float)ms;
    if (s->kept < NV2A_LAT_KEEP) s->v[s->kept++] = v;
    if (!s->n || v > s->worst) s->worst = v;
    s->n++;
}

/* The median of the times kept. */
static float stage_median(const Nv2aLatStage *s)
{
    float v[NV2A_LAT_KEEP];
    unsigned i, j;
    if (!s->kept) return 0.0f;
    memcpy(v, s->v, s->kept * sizeof v[0]);
    for (i = 1; i < s->kept; i++)
        for (j = i; j > 0 && v[j - 1] > v[j]; j--) { float x = v[j]; v[j] = v[j - 1]; v[j - 1] = x; }
    return v[s->kept / 2];
}

/* A press shown: its line, and its stages for the summary. */
static void finish(Nv2aLat *L, Nv2aLatPress *p)
{
    Nv2aLatStage *st = L->st[p->key];
    char line[LAT_LINE];
    double end = p->refresh > 0.0 ? p->refresh : p->swapped;
    int n;
    n = snprintf(line, sizeof line, "[LATENCY] %s %s, read %u:", p->key ? "key" : "pad",
                 p->what, p->pass);
    if (p->key) {
        n += snprintf(line + n, sizeof line - (size_t)n, " pumped %.1f ms before the read",
                      MS(p->read - p->sdl));
        if (p->before > 0.0)
            n += snprintf(line + n, sizeof line - (size_t)n,
                          " (came in after the pump %.1f ms before it)", MS(p->read - p->before));
        n += snprintf(line + n, sizeof line - (size_t)n, ";");
        stage_add(&st[NV2A_LAT_PUMP], MS(p->read - p->sdl));
    }
    n += snprintf(line + n, sizeof line - (size_t)n,
                  " frame reached +%.1f ms, handed over +%.1f, swapped +%.1f%s",
                  MS(p->reached - p->read), MS(p->handed - p->read), MS(p->swapped - p->read),
                  p->shown > p->serial ? " (a newer frame: its own was replaced before it was shown)" : "");
    if (p->refresh > 0.0)
        n += snprintf(line + n, sizeof line - (size_t)n, ", next refresh +%.1f",
                      MS(p->refresh - p->read));
    snprintf(line + n, sizeof line - (size_t)n, "; %.1f ms in all, from the %s", MS(end - p->sdl),
             p->key ? "pump" : "read");
    say(L, line);
    stage_add(&st[NV2A_LAT_FRAME], MS(p->reached - p->read));
    stage_add(&st[NV2A_LAT_PACE], MS(p->handed - p->reached));
    stage_add(&st[NV2A_LAT_SWAP], MS(p->swapped - p->handed));
    if (p->refresh > 0.0) stage_add(&st[NV2A_LAT_SHOW], MS(p->refresh - p->swapped));
    stage_add(&st[NV2A_LAT_ALL], MS(end - p->sdl));
    L->presses[p->key]++;
    p->state = LAT_DONE;
}

/* The presses the game has read since the last flip. */
static void take_presses(Nv2aLat *L)
{
    unsigned in = __atomic_load_n(&L->q_in, __ATOMIC_ACQUIRE);
    unsigned out = L->q_out;
    while (out != in) {
        if (L->nlive < NV2A_LAT_LIVE) {
            L->live[L->nlive] = L->queue[out % NV2A_LAT_QUEUE];
            L->live[L->nlive].state = LAT_WAITING;
            L->nlive++;
        } else {
            L->dropped++;
        }
        out++;
    }
    __atomic_store_n(&L->q_out, out, __ATOMIC_RELEASE);
}

/* The swaps ended since the last flip: each finishes the presses handed over
 * in its frame or before it. */
static void take_swaps(Nv2aLat *L)
{
    unsigned in = __atomic_load_n(&L->s_in, __ATOMIC_ACQUIRE);
    unsigned out = L->s_out;
    while (out != in) {
        const Nv2aLatSwap *s = &L->swaps[out % NV2A_LAT_SWAPS];
        int i;
        for (i = 0; i < L->nlive; i++) {
            Nv2aLatPress *p = &L->live[i];
            if (p->state != LAT_HANDED || p->serial > s->serial) continue;
            p->swapped = s->at;
            p->refresh = s->refresh;
            p->shown = s->serial;
            finish(L, p);
        }
        out++;
    }
    __atomic_store_n(&L->s_out, out, __ATOMIC_RELEASE);
}

/* Presses no frame has shown in NV2A_LAT_LOST: given up, with a line. */
static void give_up(Nv2aLat *L, double t)
{
    static const char *const k_where[] = {
        "no flip past its read", "its frame not handed over", "no swap of its frame",
    };
    int i;
    for (i = 0; i < L->nlive; i++) {
        Nv2aLatPress *p = &L->live[i];
        char line[LAT_LINE];
        if (p->state == LAT_DONE || t - p->read < NV2A_LAT_LOST) continue;
        snprintf(line, sizeof line, "[LATENCY] %s %s, read %u: not shown in %.0f s (%s)",
                 p->key ? "key" : "pad", p->what, p->pass, NV2A_LAT_LOST, k_where[p->state]);
        say(L, line);
        L->lost++;
        p->state = LAT_DONE;
    }
}

/* Drop the presses finished. */
static void compact(Nv2aLat *L)
{
    int i, k = 0;
    for (i = 0; i < L->nlive; i++)
        if (L->live[i].state != LAT_DONE) L->live[k++] = L->live[i];
    L->nlive = k;
}

static void report(Nv2aLat *L, double t)
{
    static const char *const k_src[2] = { "pad", "keys" };
    unsigned q_full, s_full;
    int k;
    if (L->report_at <= 0.0) L->report_at = t + NV2A_LAT_REPORT;
    if (t < L->report_at) return;
    L->report_at = t + NV2A_LAT_REPORT;
    for (k = 0; k < 2; k++) {
        Nv2aLatStage *st = L->st[k];
        char line[LAT_LINE];
        int n;
        if (!L->presses[k]) continue;
        n = snprintf(line, sizeof line, "[LATENCY] the last %.0f s, %s: %u press%s; median (worst), ms:",
                     NV2A_LAT_REPORT, k_src[k], L->presses[k], L->presses[k] == 1 ? "" : "es");
        if (k)
            n += snprintf(line + n, sizeof line - (size_t)n, " the pump to the read %.1f (%.1f),",
                          (double)stage_median(&st[NV2A_LAT_PUMP]), (double)st[NV2A_LAT_PUMP].worst);
        n += snprintf(line + n, sizeof line - (size_t)n,
                      " the read to the frame %.1f (%.1f), the frame to its hand-over %.1f (%.1f),"
                      " the hand-over to the swap %.1f (%.1f),",
                      (double)stage_median(&st[NV2A_LAT_FRAME]), (double)st[NV2A_LAT_FRAME].worst,
                      (double)stage_median(&st[NV2A_LAT_PACE]), (double)st[NV2A_LAT_PACE].worst,
                      (double)stage_median(&st[NV2A_LAT_SWAP]), (double)st[NV2A_LAT_SWAP].worst);
        if (st[NV2A_LAT_SHOW].n)
            n += snprintf(line + n, sizeof line - (size_t)n, " the swap to the next refresh %.1f (%.1f),",
                          (double)stage_median(&st[NV2A_LAT_SHOW]), (double)st[NV2A_LAT_SHOW].worst);
        snprintf(line + n, sizeof line - (size_t)n, " in all %.1f (%.1f)",
                 (double)stage_median(&st[NV2A_LAT_ALL]), (double)st[NV2A_LAT_ALL].worst);
        say(L, line);
    }
    q_full = __atomic_load_n(&L->q_full, __ATOMIC_RELAXED);
    s_full = __atomic_load_n(&L->s_full, __ATOMIC_RELAXED);
    if (L->lost || L->dropped || q_full != L->q_full_seen || s_full != L->s_full_seen) {
        char line[LAT_LINE];
        snprintf(line, sizeof line, "[LATENCY] the last %.0f s: %u press%s not shown in %.0f s, "
                 "%u not kept (%u with the GPU thread's list full, %u with the game's), "
                 "%u swaps not kept", NV2A_LAT_REPORT, L->lost, L->lost == 1 ? "" : "es",
                 NV2A_LAT_LOST, L->dropped + (q_full - L->q_full_seen), L->dropped,
                 q_full - L->q_full_seen, s_full - L->s_full_seen);
        say(L, line);
    }
    L->q_full_seen = q_full;
    L->s_full_seen = s_full;
    memset(L->st, 0, sizeof L->st);
    memset(L->presses, 0, sizeof L->presses);
    L->lost = L->dropped = 0;
}

void nv2a_lat_flip(Nv2aLat *L, uint32_t at, double t)
{
    int i;
    if (!L->told) {
        L->told = 1;
        say(L, "[LATENCY] on: a line for each press on the first pad and the keyboard, from the "
               "game's read to the frame that shows it, and each stage's median and worst every "
               "10 s (RECOMP_INPUT_LATENCY)");
    }
    take_presses(L);
    take_swaps(L);
    for (i = 0; i < L->nlive; i++) {
        Nv2aLatPress *p = &L->live[i];
        if (p->state == LAT_WAITING && L->flipped && ring_between(L->flip_at, at, p->put))
            p->state = LAT_PASSED;
        if (p->state == LAT_PASSED) p->reached = t;     /* a newer flip: its frame, if shown */
    }
    L->flip_at = at;
    L->flipped = 1;
    give_up(L, t);
    compact(L);
    report(L, t);
}

void nv2a_lat_handed(Nv2aLat *L, uint64_t serial, double t)
{
    int i;
    for (i = 0; i < L->nlive; i++) {
        Nv2aLatPress *p = &L->live[i];
        if (p->state != LAT_PASSED) continue;
        p->state = LAT_HANDED;
        p->handed = t;
        p->serial = serial;
    }
}
