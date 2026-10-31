/* Run with: python tests/adx_err/run.py
 *
 * Drives port/adx_err.c as its hooks in recomp_manual.c do, without the game:
 * the thread ids come from GetCurrentThreadId, the Win32 shim's on Linux,
 * which this file stands in for, and the hand-overs from the function the
 * runtime is given (xbox_guest_lock_on_handover). The output is the [ADXREC]
 * and [ADXLOCK] lines, which run.py reads. The argument picks the case.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "adx_err.h"

#define MAIN_TID  2u      /* the sound manager's update */
#define SERVER    7u      /* the ADX server, which runs the watchdog */
#define OTHER     9u
#define HANDLE    0x0027D1C8u
#define HANDLE2   0x0027D270u
#define FROM      0x0013D41Au
#define DEV       0x002738D0u
#define SECTORS   13u

static uint32_t s_tid;
static void (*s_handover)(unsigned long tid, int prio);

uint32_t GetCurrentThreadId(void) { return s_tid; }
void xbox_guest_lock_on_handover(void (*fn)(unsigned long tid, int prio)) { s_handover = fn; }

/* The runtime's thread object is the id here, and each made-up thread has the
 * priority the title gives its counterpart: the update normal, the server
 * above normal. */
void *GetCurrentThread(void) { return (void *)(uintptr_t)s_tid; }
int xbox_thread_guest_priority(void *thread)
{
    uint32_t tid = (uint32_t)(uintptr_t)thread;
    return tid == MAIN_TID ? 0 : tid == SERVER ? 1 : 2;
}

/* The guest lock goes to tid, at priority prio. */
static void take(uint32_t tid, int prio)
{
    s_tid = tid;
    if (s_handover) s_handover(tid, prio);
}

static void update(void)
{
    s_tid = MAIN_TID;
    if (adx_err_on()) adx_err_sound_update();
}

static void begin(uint32_t h)
{
    if (adx_err_on()) adx_err_recovery_begin(535.92, h, -1, 1, FROM, DEV, SECTORS, 0);
}

/* CRI's lock and unlock as their hooks see them, on the current thread: the
 * guest's count (0x25EFA0) goes up at the end of the lock's body, after its
 * resume of the spinner, and down at the start of the unlock's. */
static int s_depth;
static void cri_lock(double now) { if (adx_err_on()) adx_err_crs_lock(now, s_depth); }
static void cri_locked(void) { s_depth++; if (adx_err_on()) adx_err_crs_lock_end(); }
static void cri_unlock(void) { if (adx_err_on()) adx_err_crs_unlock(s_depth); s_depth--; }
static void cri_unlocked(double now) { if (adx_err_on()) adx_err_crs_unlock_end(now, s_depth); }

/* A hold by the server with nothing else in it. */
static void hold(double now)
{
    s_tid = SERVER;
    cri_lock(now);
    cri_locked();
    cri_unlock();
    cri_unlocked(now);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    int i;
    update();
    take(SERVER, 1);
    if (!strcmp(c, "handover")) {
        begin(HANDLE);
        take(MAIN_TID, 0);
        update();
        take(SERVER, 15);
        adx_err_recovery_end(HANDLE);
    } else if (!strcmp(c, "quiet")) {
        begin(HANDLE);
        adx_err_recovery_end(HANDLE);
    } else if (!strcmp(c, "raised_in")) {
        begin(HANDLE);
        take(MAIN_TID, 0);
        if (adx_err_on()) adx_err_raised(536.00, -1);
    } else if (!strcmp(c, "raised_out")) {
        take(MAIN_TID, 0);
        if (adx_err_on()) adx_err_raised(536.00, -1);
    } else if (!strcmp(c, "nested")) {
        begin(HANDLE);
        begin(HANDLE);           /* a recovery of the same handle inside it */
        adx_err_recovery_end(HANDLE);
        begin(HANDLE2);          /* another handle's, while it runs: left out */
        take(OTHER, 2);
        take(SERVER, 15);
        adx_err_recovery_end(HANDLE2);
        adx_err_recovery_end(HANDLE);
    } else if (!strcmp(c, "many")) {
        begin(HANDLE);
        for (i = 0; i < 8; i++) {
            take(OTHER, 2);
            take(SERVER, 15);
        }
        adx_err_recovery_end(HANDLE);
    } else if (!strcmp(c, "after")) {
        begin(HANDLE);
        adx_err_recovery_end(HANDLE);
        take(MAIN_TID, 0);       /* the window is shut: not noted */
        take(SERVER, 1);
        begin(HANDLE);
        adx_err_recovery_end(HANDLE);
    } else if (!strcmp(c, "crs_quiet")) {
        hold(10.0);
        hold(15.0);
        hold(20.5);               /* 10.5 s on: the report */
    } else if (!strcmp(c, "crs_take")) {
        hold(10.0);
        cri_lock(11.0);
        take(MAIN_TID, 0);        /* as it is taken: in the resume of the spinner */
        update();
        take(SERVER, 15);         /* the holder back: not counted */
        cri_locked();
        take(OTHER, 2);           /* while held */
        take(SERVER, 15);
        cri_unlock();
        take(MAIN_TID, 0);        /* as it is let go: in the suspend */
        take(SERVER, 1);
        cri_unlocked(20.0);
    } else if (!strcmp(c, "crs_entered")) {
        hold(10.0);
        cri_lock(11.0);
        take(MAIN_TID, 0);
        cri_lock(11.0);           /* the count is still 0: the server hasn't raised it */
        take(SERVER, 15);
        cri_locked();             /* 1 */
        take(MAIN_TID, 0);
        cri_locked();             /* 2: the update's, ignored */
        take(SERVER, 15);
        cri_unlock();             /* 2 -> 1 */
        cri_unlocked(11.0);
        take(MAIN_TID, 0);
        cri_unlock();             /* 1 -> 0, by the update */
        cri_unlocked(20.0);
    } else if (!strcmp(c, "crs_nested")) {
        hold(10.0);
        s_tid = SERVER;
        cri_lock(11.0);
        cri_locked();
        cri_lock(11.0);           /* the holder's own nesting */
        cri_locked();
        cri_unlock();
        cri_unlocked(11.0);
        cri_unlock();
        cri_unlocked(20.0);
    } else if (!strcmp(c, "crs_period")) {
        hold(10.0);
        hold(19.9);
        hold(20.1);               /* the first report: 3 holds */
        hold(25.0);
        hold(30.2);               /* the second: 2 */
    } else if (!strcmp(c, "crs_lines")) {
        for (i = 0; i < 25; i++) {
            s_tid = SERVER;
            cri_lock(10.0 + i * 0.1);
            take(MAIN_TID, 0);
            update();
            take(SERVER, 15);
            cri_locked();
            cri_unlock();
            cri_unlocked(10.0 + i * 0.1);
        }
    } else {
        fprintf(stderr, "no case %s\n", c);
        return 1;
    }
    return 0;
}
