/* Run with: python tests/float_modes/run.py
 *
 * The Xbox's float modes on every thread that runs the game's code
 * (src/kernel/kernel_float.c). On the console every thread starts with MXCSR
 * at 0x1F80 and nothing in the game changes it; in the port a thread's MXCSR
 * is whatever the thread that made it, or a library loaded on it, left there.
 * xbox_float_modes_enter, called first in xbox_guest_lock_acquire, gives the
 * thread the Xbox's modes, and says what it found.
 *
 * Five threads run at once, each starting with its own MXCSR, as a library
 * would leave it, then entering as xbox_guest_lock_acquire does:
 *
 * 1. Flush to zero and denormals-are-zero: after its first entry the modes
 *    are the Xbox's, and FLT_MIN / 2 is a denormal again, where it was 0.
 * 2. Only the flags set (0x1FA0): the Xbox's modes, and nothing is set, so
 *    the flag is still there.
 * 3. Rounding towards zero: put back, and 1 + 0.75 of a unit in the last
 *    place comes out as 1 + 1 unit, as rounding to nearest gives, where it
 *    was 1.
 * 4. The Xbox's modes at first, then flush to zero set between two entries,
 *    as host code would: found at the next entry and put back, and a third
 *    entry finds nothing.
 * 5. The Xbox's modes kept, with inexact arithmetic before each of 1,000
 *    entries after its first: nothing set, so its flags are left as they
 *    are.
 *
 * Then one thread changes its modes 120 times between entries: each is put
 * back. And the line at the start, with the Xbox's modes and without: it
 * sets nothing.
 *
 * Each thread says its case and its id on stdout. The [SSE] lines go to
 * stderr, as in the engine, and run.py holds them to the ids: each thread's
 * first entry says what it found, case 4's change is said once, case 5 says
 * nothing after its first line, and of the 121 changes the first 5 are said
 * in full and the rest counted, with a line at 10 and at 100.
 */
#include "kernel.h"
#include <float.h>
#include <stdio.h>
#include <xmmintrin.h>

#define XBOX_MXCSR      0x1F80u   /* a thread's MXCSR at its start on the console */
#define MODES           0xFFC0u   /* everything but the six flags */
#define FLAG_PRECISION  0x0020u   /* set by any inexact operation */
#define FTZ             0x8000u   /* flush to zero */
#define DAZ             0x0040u   /* denormals are zero */
#define RC_TOWARDS_ZERO 0x6000u   /* rounding control: towards zero */
#define CASES           5
#define KEPT_ENTRIES    1000      /* case 5's entries */
#define CHANGES         120       /* the last thread's changes */
#define WAIT_MS         30000     /* for a thread, however busy the machine */

static volatile LONG s_failures;
static HANDLE s_done[CASES], s_finish;

static void fail(int which, const char *what, unsigned found)
{
    __atomic_add_fetch(&s_failures, 1, __ATOMIC_RELAXED);
    printf("float_modes: case %d: %s (MXCSR 0x%04X)\n", which, what, found);
    fflush(stdout);
}

static void said(int which)
{
    printf("float_modes: case %d on thread %lu\n", which,
           (unsigned long)GetCurrentThreadId());
    fflush(stdout);
}

/* Worked out at run time, under the thread's modes. */
static volatile float s_min = FLT_MIN, s_two = 2.0f, s_one = 1.0f;
static volatile float s_three_quarters_ulp = 0x1.8p-24f;   /* 0.75 of 1.0f's ulp */
static float half_of_min(void) { return s_min / s_two; }
static float one_and_a_bit(void) { return s_one + s_three_quarters_ulp; }

static void flushed_to_zero(void)
{
    _mm_setcsr(XBOX_MXCSR | FTZ | DAZ);
    if (half_of_min() != 0.0f)
        fail(1, "FLT_MIN / 2 isn't 0 under flush to zero: the test can't tell", _mm_getcsr());
    _mm_setcsr(XBOX_MXCSR | FTZ | DAZ);
    said(1);
    xbox_float_modes_enter();
    if ((_mm_getcsr() & MODES) != XBOX_MXCSR)
        fail(1, "not the Xbox's modes after the first entry", _mm_getcsr());
    if (half_of_min() == 0.0f)
        fail(1, "FLT_MIN / 2 is still 0", _mm_getcsr());
}

static void flags_only(void)
{
    _mm_setcsr(XBOX_MXCSR | FLAG_PRECISION);
    said(2);
    xbox_float_modes_enter();
    if (_mm_getcsr() != (XBOX_MXCSR | FLAG_PRECISION))
        fail(2, "the register was set, though its modes were the Xbox's", _mm_getcsr());
}

static void towards_zero(void)
{
    _mm_setcsr(XBOX_MXCSR | RC_TOWARDS_ZERO);
    if (one_and_a_bit() != 1.0f)
        fail(3, "1 + 0.75 ulp isn't 1 rounding towards zero: the test can't tell", _mm_getcsr());
    _mm_setcsr(XBOX_MXCSR | RC_TOWARDS_ZERO);
    said(3);
    xbox_float_modes_enter();
    if ((_mm_getcsr() & MODES) != XBOX_MXCSR)
        fail(3, "not the Xbox's modes after the first entry", _mm_getcsr());
    if (one_and_a_bit() != 1.0f + FLT_EPSILON)
        fail(3, "1 + 0.75 ulp isn't 1 + 1 ulp, as rounding to nearest gives", _mm_getcsr());
}

static void changed_later(void)
{
    _mm_setcsr(XBOX_MXCSR);
    said(4);
    xbox_float_modes_enter();
    _mm_setcsr(XBOX_MXCSR | FTZ);               /* host code, between two entries */
    xbox_float_modes_enter();
    if ((_mm_getcsr() & MODES) != XBOX_MXCSR)
        fail(4, "not put back at the next entry", _mm_getcsr());
    if (half_of_min() == 0.0f)
        fail(4, "FLT_MIN / 2 is still 0", _mm_getcsr());
    xbox_float_modes_enter();
}

static void kept(void)
{
    int i;
    volatile float x = 1.0f;
    _mm_setcsr(XBOX_MXCSR);
    said(5);
    xbox_float_modes_enter();
    for (i = 0; i < KEPT_ENTRIES; i++) {
        x = x / 3.0f + 1.0f;                       /* inexact: sets the flag */
        xbox_float_modes_enter();
        if (!(_mm_getcsr() & FLAG_PRECISION)) {
            fail(5, "the register was set, though its modes were the Xbox's", _mm_getcsr());
            break;
        }
    }
}

static DWORD WINAPI case_thread(LPVOID param)
{
    int which = (int)(INT_PTR)param;
    switch (which) {
    case 1: flushed_to_zero(); break;
    case 2: flags_only();      break;
    case 3: towards_zero();    break;
    case 4: changed_later();   break;
    case 5: kept();            break;
    }
    SetEvent(s_done[which - 1]);
    /* Alive until every case is done, so no two of them share an id. */
    WaitForSingleObject(s_finish, INFINITE);
    return 0;
}

static DWORD WINAPI changing_thread(LPVOID param)
{
    int i;
    (void)param;
    _mm_setcsr(XBOX_MXCSR);
    said(6);
    xbox_float_modes_enter();
    for (i = 0; i < CHANGES; i++) {
        _mm_setcsr(XBOX_MXCSR | FTZ);
        xbox_float_modes_enter();
        if ((_mm_getcsr() & MODES) != XBOX_MXCSR) {
            fail(6, "a change not put back", _mm_getcsr());
            break;
        }
    }
    return 0;
}

static void at_start(void)
{
    _mm_setcsr(XBOX_MXCSR);
    xbox_float_modes_at_start();
    if (_mm_getcsr() != XBOX_MXCSR)
        fail(7, "the start line changed the register", _mm_getcsr());
    _mm_setcsr(XBOX_MXCSR | FTZ | DAZ);
    xbox_float_modes_at_start();
    if (_mm_getcsr() != (XBOX_MXCSR | FTZ | DAZ))
        fail(7, "the start line set the register", _mm_getcsr());
    _mm_setcsr(XBOX_MXCSR);
}

int main(void)
{
    HANDLE t[CASES], changing;
    int i;

    at_start();
    s_finish = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (i = 0; i < CASES; i++) {
        s_done[i] = CreateEventA(NULL, TRUE, FALSE, NULL);
        t[i] = CreateThread(NULL, 0, case_thread, (LPVOID)(INT_PTR)(i + 1), 0, NULL);
        if (!s_done[i] || !t[i]) {
            printf("float_modes: no thread for case %d\n", i + 1);
            return 1;
        }
    }
    for (i = 0; i < CASES; i++)
        if (WaitForSingleObject(s_done[i], WAIT_MS) != WAIT_OBJECT_0)
            fail(i + 1, "no end in 30 s", 0);

    /* After the others are done, so the run's count of changes is case 4's
     * one, and while they are still alive, so its id is its own. */
    changing = CreateThread(NULL, 0, changing_thread, NULL, 0, NULL);
    if (!changing || WaitForSingleObject(changing, WAIT_MS) != WAIT_OBJECT_0)
        fail(6, "no end in 30 s", 0);
    if (changing) CloseHandle(changing);

    SetEvent(s_finish);
    for (i = 0; i < CASES; i++) {
        WaitForSingleObject(t[i], WAIT_MS);
        CloseHandle(t[i]);
        CloseHandle(s_done[i]);
    }
    CloseHandle(s_finish);

    if (s_failures)
        return 1;
    printf("float_modes: all cases pass\n");
    return 0;
}
