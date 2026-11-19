/**
 * kernel_float.c - The Xbox's float modes on every thread that runs the
 * game's code
 *
 * On the console the kernel starts every thread with MXCSR at 0x1F80: every
 * SSE exception masked, rounding to nearest, and denormals kept (no flush to
 * zero, no denormals-are-zero). Nothing in the game loads it (there is no
 * LDMXCSR in its image), so the game's SSE arithmetic, and its x87 doubles,
 * which the port computes with the host's SSE, run under those modes on every
 * thread for the whole run. What the game's own STMXCSR and LDMXCSR see is the
 * guest's copy, g_mxcsr (templates/runtime/recomp_types.h).
 *
 * In the port a guest thread is a host thread, and the host's MXCSR is that
 * thread's own. On Linux a new thread starts with its maker's, whole, and a
 * library built with -ffast-math (by clang 18, still) sets flush to zero and
 * denormals-are-zero on the thread that loads it, so one library loaded on
 * the main thread would reach every thread made after it. Under those modes a
 * denormal comes out as 0 where the console keeps it, and a run parts from
 * the console's, as the ending's did on AMD processors (known bug 60).
 *
 * So the kernel gives each thread the console's modes where it enters the
 * game's code: xbox_float_modes_enter, called first in
 * xbox_guest_lock_acquire (kernel_bridge.c), the one door every thread that
 * runs the game's code goes through: the main thread before the entry point,
 * a new guest thread, and the host threads that run interrupt routines, DPCs
 * and timers. A thread's first entry says what it found and sets the modes. A
 * later entry costs one read of the register, and if host code changed the
 * modes in between, it says so and puts them back.
 *
 * Only the modes are compared, not the whole register: bits 0 to 5 are flags
 * that any inexact operation sets and nothing here reads, so 0x1FA0 is the
 * Xbox's modes too.
 */

#include "kernel.h"
#include <stdio.h>
#include <string.h>
#include <xmmintrin.h>
#include "xbox_memory_layout.h"

/* A thread's MXCSR at its start on the console. */
#define XBOX_MXCSR_AT_START   0x1F80u
/* The modes: everything but the six exception flags (bits 0 to 5). */
#define MXCSR_MODES           0xFFC0u

/* MXCSR's fields, as Intel's manual gives them (volume 1, 10.2.3). */
#define MXCSR_DAZ             0x0040u   /* bit 6: denormals are zero */
#define MXCSR_MASKS           0x1F80u   /* bits 7 to 12: the exception masks */
#define MXCSR_MASK_FIRST      7
#define MXCSR_RC              0x6000u   /* bits 13 and 14: rounding control */
#define MXCSR_RC_FIRST        13
#define MXCSR_FTZ             0x8000u   /* bit 15: flush to zero */

/* A change found after a thread's first entry is said in full this many
 * times in a run. After that it is counted, and the count is said each time
 * it reaches a power of ten, so a thread whose host code changes the modes
 * at every take of the lock can't flood the log. */
#define CHANGES_SAID          5

static RECOMP_TLS int t_entered;
static unsigned long g_changes;

static int xbox_modes(uint32_t mxcsr)
{
    return (mxcsr & MXCSR_MODES) == XBOX_MXCSR_AT_START;
}

static int power_of_ten(unsigned long n)
{
    while (n >= 10 && n % 10 == 0)
        n /= 10;
    return n == 1;
}

/* What in mxcsr's modes isn't the Xbox's, in words. */
static void describe(uint32_t mxcsr, char *out, size_t size)
{
    static const char *const rounding[] = {
        "rounding to nearest", "rounding down", "rounding up",
        "rounding towards zero",
    };
    static const char *const exceptions[] = {
        "invalid operation", "denormal", "divide by zero", "overflow",
        "underflow", "precision",
    };
    size_t used = 0;
    unsigned rc = (mxcsr & MXCSR_RC) >> MXCSR_RC_FIRST;
    unsigned unmasked = (~mxcsr & MXCSR_MASKS) >> MXCSR_MASK_FIRST;
    unsigned i;

    out[0] = '\0';
#define SAY(...) do { \
        if (used < size) \
            used += (size_t)snprintf(out + used, size - used, __VA_ARGS__); \
    } while (0)
    if (mxcsr & MXCSR_FTZ)
        SAY("%sflush to zero", used ? ", " : "");
    if (mxcsr & MXCSR_DAZ)
        SAY("%sdenormals are zero", used ? ", " : "");
    if (rc)
        SAY("%s%s", used ? ", " : "", rounding[rc]);
    if (unmasked) {
        SAY("%sexceptions unmasked:", used ? ", " : "");
        for (i = 0; i < sizeof exceptions / sizeof exceptions[0]; i++)
            if (unmasked & (1u << i))
                SAY(" %s", exceptions[i]);
    }
#undef SAY
}

void xbox_float_modes_at_start(void)
{
    uint32_t found = _mm_getcsr();
    char why[160];

    if (xbox_modes(found)) {
        fprintf(stderr, "[SSE] float modes at the start: MXCSR 0x%04X, the "
                        "Xbox's modes\n", found);
        return;
    }
    describe(found, why, sizeof why);
    fprintf(stderr, "[SSE] float modes at the start: MXCSR 0x%04X, not the "
                    "Xbox's modes (%s); each thread gets 0x%04X where it "
                    "enters the game's code\n",
            found, why, XBOX_MXCSR_AT_START);
}

void xbox_float_modes_enter(void)
{
    uint32_t found = _mm_getcsr();
    unsigned long tid, n;
    char why[160];

    if (t_entered && xbox_modes(found))
        return;
    tid = (unsigned long)GetCurrentThreadId();
    if (!t_entered) {
        t_entered = 1;
        if (xbox_modes(found)) {
            fprintf(stderr, "[SSE] thread %lu enters the game's code: MXCSR "
                            "0x%04X, the Xbox's modes\n", tid, found);
            return;
        }
        describe(found, why, sizeof why);
        _mm_setcsr(XBOX_MXCSR_AT_START);
        fprintf(stderr, "[SSE] thread %lu enters the game's code: MXCSR "
                        "0x%04X, not the Xbox's modes (%s); set to 0x%04X\n",
                tid, found, why, XBOX_MXCSR_AT_START);
        return;
    }
    describe(found, why, sizeof why);
    _mm_setcsr(XBOX_MXCSR_AT_START);
    n = __atomic_add_fetch(&g_changes, 1, __ATOMIC_RELAXED);
    if (n <= CHANGES_SAID) {
        fprintf(stderr, "[SSE] thread %lu: MXCSR changed to 0x%04X outside "
                        "the game's code (%s); set back to 0x%04X\n",
                tid, found, why, XBOX_MXCSR_AT_START);
        if (n == CHANGES_SAID)
            fprintf(stderr, "[SSE] further changes are counted, and the count "
                            "said at each power of ten\n");
        return;
    }
    if (power_of_ten(n))
        fprintf(stderr, "[SSE] MXCSR changed outside the game's code %lu "
                        "times so far, on any thread; each set back to "
                        "0x%04X\n", n, XBOX_MXCSR_AT_START);
}
