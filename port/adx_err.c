/* The sound library's stream errors (adx_err.h), with JSRF_ADX_ERR_LOG=1.
 *
 * A watchdog recovery is a window: from the watchdog's call of the stop or
 * the restart, with the handle's error code set, to its return, after which
 * the watchdog clears the code. A thread that takes the guest lock in that
 * window can read the code, as the game's check does every frame. The
 * runtime says when the lock changes hands (xbox_guest_lock_on_handover);
 * each hand-over in a window is kept, and the window's lines list them when
 * it ends:
 *
 *   [ADXREC] t=535.92 handle 0027D1C8 code -1: restart on tid 16316, from
 *            0013D41A, CRI's lock 0 deep; file 002738D0 (13 sectors); 1.84 ms
 *   [ADXREC]   the guest lock changed hands 2 times meanwhile: tid 1304 at
 *            +0.21 ms (priority 0, the sound update's), back at +0.25 ms
 *            (priority 15)
 *
 * and the error raised by the game's check says whether a recovery was
 * running at the time. The window is only touched with the guest lock held
 * -- the recovery's thread holds it, and the runtime calls the hand-over
 * function with it held -- so it needs no lock of its own.
 *
 * The hand-overs are counted the same way over every hold of CRI's lock,
 * which comes far more often (the [ADXLOCK] lines, below). */
#define _POSIX_C_SOURCE 200112L    /* clock_gettime */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "adx_err.h"

/* The Win32 shim's thread id, the one the runtime's own lines name threads
 * by. (On Windows it is the real one, from <windows.h>.) */
#if !defined(_WIN32)
extern uint32_t GetCurrentThreadId(void);
#else
#include <windows.h>
#endif
/* kernel_bridge.c: fn is called on each hand-over of the guest lock, on the
 * thread that took it, with its id and its guest priority. */
extern void xbox_guest_lock_on_handover(void (*fn)(unsigned long tid, int prio));

#define TAKES_KEPT 12   /* the hand-overs a window's line lists; the rest are counted */

static struct {
    int open, nest;     /* a recovery is running; and recoveries of its handle inside it */
    uint32_t h, from, dev, sectors;
    int code, restart, depth;
    unsigned long tid;
    double t, t0;       /* when it started: the run's clock (t=), and ours */
    int takes, updates;
    struct { unsigned long tid; int prio; double at; } take[TAKES_KEPT];
} s_rec;
static unsigned long s_update_tid;     /* the thread the sound manager's update runs on */

static double clock_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void crs_taken(unsigned long tid, int prio);

static void handed_over(unsigned long tid, int prio)
{
    crs_taken(tid, prio);
    if (!s_rec.open) return;
    if (s_rec.takes < TAKES_KEPT) {
        s_rec.take[s_rec.takes].tid = tid;
        s_rec.take[s_rec.takes].prio = prio;
        s_rec.take[s_rec.takes].at = clock_ms() - s_rec.t0;
    }
    s_rec.takes++;
}

int adx_err_on(void)
{
    static int on = -1;
    if (on < 0) {
        on = getenv("JSRF_ADX_ERR_LOG") ? 1 : 0;
        if (on) xbox_guest_lock_on_handover(handed_over);
    }
    return on;
}

void adx_err_recovery_begin(double now, uint32_t h, int code, int restart, uint32_t from,
                            uint32_t dev, uint32_t sectors, int depth)
{
    if (s_rec.open) {
        if (h == s_rec.h) s_rec.nest++;
        return;
    }
    s_rec.open = 1;
    s_rec.nest = 0;
    s_rec.h = h;
    s_rec.code = code;
    s_rec.restart = restart;
    s_rec.from = from;
    s_rec.dev = dev;
    s_rec.sectors = sectors;
    s_rec.depth = depth;
    s_rec.tid = (unsigned long)GetCurrentThreadId();
    s_rec.t = now;
    s_rec.takes = s_rec.updates = 0;
    s_rec.t0 = clock_ms();
}

void adx_err_recovery_end(uint32_t h)
{
    int i;
    if (!s_rec.open || h != s_rec.h) return;
    if (s_rec.nest) {
        s_rec.nest--;
        return;
    }
    s_rec.open = 0;
    fprintf(stderr, "[ADXREC] t=%.2f handle %08X code %d: %s on tid %lu%s, from %08X, CRI's lock %d deep; "
                    "file %08X (%u sectors); %.2f ms\n",
            s_rec.t, s_rec.h, s_rec.code, s_rec.restart ? "restart" : "stop", s_rec.tid,
            s_rec.tid == s_update_tid ? " (the sound update's)" : "", s_rec.from, s_rec.depth,
            s_rec.dev, s_rec.sectors, clock_ms() - s_rec.t0);
    if (!s_rec.takes) {
        fprintf(stderr, "[ADXREC]   no other thread took the guest lock meanwhile\n");
    } else {
        fprintf(stderr, "[ADXREC]   the guest lock changed hands %d time%s meanwhile:", s_rec.takes,
                s_rec.takes == 1 ? "" : "s");
        for (i = 0; i < s_rec.takes && i < TAKES_KEPT; i++) {
            if (s_rec.take[i].tid == s_rec.tid)
                fprintf(stderr, "%s back at +%.2f ms (priority %d)", i ? "," : "",
                        s_rec.take[i].at, s_rec.take[i].prio);
            else
                fprintf(stderr, "%s tid %lu at +%.2f ms (priority %d%s)", i ? "," : "",
                        s_rec.take[i].tid, s_rec.take[i].at, s_rec.take[i].prio,
                        s_rec.take[i].tid == s_update_tid ? ", the sound update's" : "");
        }
        if (s_rec.takes > TAKES_KEPT) fprintf(stderr, ", and %d more", s_rec.takes - TAKES_KEPT);
        fprintf(stderr, "\n");
    }
    if (s_rec.updates)
        fprintf(stderr, "[ADXREC]   the sound manager's update started %d time%s meanwhile\n", s_rec.updates,
                s_rec.updates == 1 ? "" : "s");
    fflush(stderr);
}

void adx_err_sound_update(void)
{
    unsigned long tid = (unsigned long)GetCurrentThreadId();
    if (tid != s_update_tid) {
        s_update_tid = tid;
        fprintf(stderr, "[ADXREC] the sound manager's update runs on tid %lu\n", tid);
    }
    if (s_rec.open && tid != s_rec.tid) s_rec.updates++;
}

void adx_err_raised(double now, int code)
{
    unsigned long tid = (unsigned long)GetCurrentThreadId();
    if (s_rec.open)
        fprintf(stderr, "[ADXREC] t=%.2f code %d raised on tid %lu%s, %.2f ms into tid %lu's %s of handle %08X "
                        "for code %d\n",
                now, code, tid, tid == s_update_tid ? " (the sound update's)" : "", clock_ms() - s_rec.t0,
                s_rec.tid, s_rec.restart ? "restart" : "stop", s_rec.h, s_rec.code);
    else
        fprintf(stderr, "[ADXREC] t=%.2f code %d raised on tid %lu%s, with no recovery running\n",
                now, code, tid, tid == s_update_tid ? " (the sound update's)" : "");
}

/* ---- CRI's lock --------------------------------------------------------------
 *
 * The recoveries above come about once a minute, and each is a chance of
 * microseconds. CRI's lock is what keeps the main thread out of them on the
 * console, and it is taken far more often, so its holds are where the guest
 * lock's hand-overs are counted. A hold runs from the outermost lock to the
 * unlock that lets it go. The lock's only kernel call that gives up the
 * guest lock is its resume of the spinner, and the unlock's its suspend, so
 * a take "as the lock was taken" or "as it was let go" is most likely one of
 * those; "while it was held" is anything between.
 *
 *   [ADXLOCK] the last 10.0 s: CRI's lock was held 3512 times, the longest
 *             0.41 ms; another thread took the guest lock during 3 of them,
 *             and entered CRI's lock itself during 0
 *   [ADXLOCK]   held by tid 18720 (priority 1) 1800 times, 2 with a take; ...
 *   [ADXLOCK]   taken by tid 16640 (priority 0, the sound update's) 1 time:
 *             0 as the lock was taken, 0 while it was held, 1 as it was let go
 *
 * and a line for each of the first takes by the sound update's thread, and
 * each of the first entries into a held lock by another thread.
 *
 * Unlike the recoveries, this state has a lock of its own: a thread at idle
 * priority runs outside the guest lock, and CRI's lock may be taken there
 * too. It is held for a few loads and stores, never across a call out. */

#if !defined(_WIN32)
extern void *GetCurrentThread(void);
#endif
/* kernel_thread.c: the priority the title gave a thread, -15 to 15. */
extern int xbox_thread_guest_priority(void *Thread);

#define TIDS_KEPT    8      /* threads a report names; the rest are counted together */
#define CRS_REPORT_S 10.0   /* a report every 10 s of the run's clock */
#define CRS_EXAMPLES 20     /* lines for single takes by the sound update's thread, and for entries */

enum { AS_TAKEN, WHILE_HELD, AS_LET_GO, PHASES };
static const char *const k_phase[PHASES] = {
    "as the lock was taken", "while it was held", "as it was let go",
};

typedef struct {
    unsigned long tid;
    int prio;
    unsigned long n;            /* holds (by a holder) or takes (by a taker) */
    unsigned long with;         /* a holder's holds with a take by another thread */
    unsigned long at[PHASES];   /* a taker's takes, by when in the hold */
} TidCount;

typedef struct {
    double since;               /* the run's clock when the period began */
    unsigned long holds, taken, entered, takes;
    double longest;
    TidCount holder[TIDS_KEPT], taker[TIDS_KEPT];
    unsigned long holder_rest, taker_rest;
} CrsPeriod;

static struct {
    int started, open, phase, prio, taken, entered;
    unsigned long owner;
    double t, t0;               /* the hold's start: the run's clock, and ours */
    int take_lines, entry_lines;
    CrsPeriod p;
} s_crs;
static int s_crs_lk;

static void crs_lock(void)   { while (__atomic_exchange_n(&s_crs_lk, 1, __ATOMIC_ACQUIRE)) { } }
static void crs_unlock(void) { __atomic_store_n(&s_crs_lk, 0, __ATOMIC_RELEASE); }

static TidCount *tid_count(TidCount *t, unsigned long *rest, unsigned long tid)
{
    int i;
    for (i = 0; i < TIDS_KEPT; i++) {
        if (t[i].tid == tid) return &t[i];
        if (!t[i].tid) {
            t[i].tid = tid;
            return &t[i];
        }
    }
    (*rest)++;
    return NULL;
}

static const char *whose(unsigned long tid)
{
    return tid == s_update_tid ? ", the sound update's" : "";
}

static void crs_taken(unsigned long tid, int prio)
{
    TidCount *c;
    int line = 0, phase = 0;
    unsigned long owner = 0;
    double into = 0, t = 0;
    crs_lock();
    if (s_crs.open && tid != s_crs.owner) {
        s_crs.taken = 1;
        s_crs.p.takes++;
        if ((c = tid_count(s_crs.p.taker, &s_crs.p.taker_rest, tid))) {
            c->prio = prio;
            c->n++;
            c->at[s_crs.phase]++;
        }
        if (tid == s_update_tid && s_crs.take_lines < CRS_EXAMPLES) {
            s_crs.take_lines++;
            line = 1;
            owner = s_crs.owner;
            phase = s_crs.phase;
            into = clock_ms() - s_crs.t0;
            t = s_crs.t + into / 1e3;
        }
    }
    crs_unlock();
    if (line) {
        fprintf(stderr, "[ADXLOCK] t=%.2f the sound update's thread (tid %lu, priority %d) took the guest lock "
                        "%.3f ms into tid %lu's hold of CRI's lock, %s\n",
                t, tid, prio, into, owner, k_phase[phase]);
        fflush(stderr);
    }
}

void adx_err_crs_lock(double now, int depth)
{
    unsigned long tid = (unsigned long)GetCurrentThreadId();
    int prio = xbox_thread_guest_priority(GetCurrentThread());
    int line = 0, phase = 0;
    unsigned long owner = 0;
    double into = 0, t = 0;
    crs_lock();
    if (s_crs.open) {
        if (tid != s_crs.owner) {          /* the owner's own nesting is nothing */
            s_crs.entered = 1;
            if (s_crs.entry_lines < CRS_EXAMPLES) {
                s_crs.entry_lines++;
                line = 1;
                owner = s_crs.owner;
                phase = s_crs.phase;
                into = clock_ms() - s_crs.t0;
                t = now;
            }
        }
    } else if (!depth) {
        s_crs.open = 1;
        s_crs.owner = tid;
        s_crs.prio = prio;
        s_crs.phase = AS_TAKEN;
        s_crs.taken = s_crs.entered = 0;
        s_crs.t = now;
        s_crs.t0 = clock_ms();
        if (!s_crs.started) {
            s_crs.started = 1;
            s_crs.p.since = now;
        }
    }
    crs_unlock();
    if (line) {
        fprintf(stderr, "[ADXLOCK] t=%.2f tid %lu (priority %d%s) entered CRI's lock %.3f ms into tid %lu's hold of "
                        "it, %s\n",
                t, tid, prio, whose(tid), into, owner, k_phase[phase]);
        fflush(stderr);
    }
}

void adx_err_crs_lock_end(void)
{
    unsigned long tid = (unsigned long)GetCurrentThreadId();
    crs_lock();
    if (s_crs.open && s_crs.phase == AS_TAKEN && tid == s_crs.owner) s_crs.phase = WHILE_HELD;
    crs_unlock();
}

void adx_err_crs_unlock(int depth)
{
    crs_lock();
    if (s_crs.open && depth == 1) s_crs.phase = AS_LET_GO;
    crs_unlock();
}

static void crs_report(const CrsPeriod *p, double secs)
{
    int i;
    fprintf(stderr, "[ADXLOCK] the last %.1f s: CRI's lock was held %lu time%s, the longest %.2f ms; another thread "
                    "took the guest lock during %lu of them, and entered CRI's lock itself during %lu\n",
            secs, p->holds, p->holds == 1 ? "" : "s", p->longest, p->taken, p->entered);
    fprintf(stderr, "[ADXLOCK]   held by");
    for (i = 0; i < TIDS_KEPT && p->holder[i].tid; i++)
        fprintf(stderr, "%s tid %lu (priority %d%s) %lu time%s, %lu with a take", i ? ";" : "",
                p->holder[i].tid, p->holder[i].prio, whose(p->holder[i].tid), p->holder[i].n,
                p->holder[i].n == 1 ? "" : "s", p->holder[i].with);
    if (p->holder_rest) fprintf(stderr, "; and %lu by other threads", p->holder_rest);
    fprintf(stderr, "\n");
    if (!p->takes) {
        fprintf(stderr, "[ADXLOCK]   no other thread took the guest lock while it was held\n");
    } else {
        fprintf(stderr, "[ADXLOCK]   taken by");
        for (i = 0; i < TIDS_KEPT && p->taker[i].tid; i++)
            fprintf(stderr, "%s tid %lu (priority %d%s) %lu time%s: %lu %s, %lu %s, %lu %s", i ? ";" : "",
                    p->taker[i].tid, p->taker[i].prio, whose(p->taker[i].tid), p->taker[i].n,
                    p->taker[i].n == 1 ? "" : "s", p->taker[i].at[AS_TAKEN], k_phase[AS_TAKEN],
                    p->taker[i].at[WHILE_HELD], k_phase[WHILE_HELD], p->taker[i].at[AS_LET_GO],
                    k_phase[AS_LET_GO]);
        if (p->taker_rest) fprintf(stderr, "; and %lu by other threads", p->taker_rest);
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

void adx_err_crs_unlock_end(double now, int depth)
{
    TidCount *c;
    CrsPeriod done;
    int report = 0;
    double ms, secs = 0;
    crs_lock();
    if (s_crs.open && !depth) {
        s_crs.open = 0;
        ms = clock_ms() - s_crs.t0;
        s_crs.p.holds++;
        if (ms > s_crs.p.longest) s_crs.p.longest = ms;
        if (s_crs.taken) s_crs.p.taken++;
        if (s_crs.entered) s_crs.p.entered++;
        if ((c = tid_count(s_crs.p.holder, &s_crs.p.holder_rest, s_crs.owner))) {
            c->prio = s_crs.prio;
            c->n++;
            if (s_crs.taken) c->with++;
        }
        if (now - s_crs.p.since >= CRS_REPORT_S) {
            done = s_crs.p;
            secs = now - s_crs.p.since;
            memset(&s_crs.p, 0, sizeof s_crs.p);
            s_crs.p.since = now;
            report = 1;
        }
    }
    crs_unlock();
    if (report) crs_report(&done, secs);
}
