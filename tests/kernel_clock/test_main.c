/* Run with: python tests/kernel_clock/run.py
 *
 * The kernel's millisecond clock, KeTickCount, moved by the kernel's timer
 * thread alone (b1.4 row 10).
 *
 * src/kernel/kernel_timer.c's thread is started as xbox_kernel_bridge_init
 * starts it, with no GPU thread and no timer set, and KeTickCount must keep
 * up with the host's millisecond clock: it must move, and never fall far
 * behind. It was written at the bottom of the GPU thread's loop, so here,
 * with no GPU thread, it never moves at all. Then a timer set with KeSetTimer
 * must fire its DPC once its due time has passed, as before the clock moved.
 * And the thread's guest stack and TIB must be taken by kernel_timer_start,
 * on the thread that starts it, before it runs: taken by the thread itself,
 * their lines in the log came out mixed with the starting thread's on
 * Windows, at every start. Last, KeCancelTimer must stop a timer that is set
 * and say so, and leave the game's 40 bytes of KTIMER as they were: a host
 * function took them for a host timer of its own, and on Linux read through
 * whatever its handle's bytes held (code/pass2's F). And KeInitializeTimerEx
 * given a timer at guest address 0 says so once ([CHECK]) and writes
 * nothing: memset was handed NULL, and the test stopped there.
 *
 * The thread's per-pass device work (the display and APU interrupts, the
 * GPU's software methods, the watches) and the DPCs are kernel_bridge.c's and
 * kernel_interrupt.c's, and stand-ins here: only the timer thread is tested.
 * It is started with CreateThread, src/platform/win32_compat.c's on Linux and
 * the system's on Windows.
 */
#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>   /* _exit */
#endif
#include "xbox_memory_layout.h"
#include "kernel_bridge.h"

#define ENOUGH_MOVES   200           /* of the clock, to test anything */
#define MAX_MS         30000         /* however busy the machine */
#define POLL_MS        1
#define MAX_BEHIND_MS  250           /* what a busy machine may delay a pass */
#define GUEST_BYTES    (16u << 20)   /* past the worker stacks' end */

/* The game's side, in guest memory. */
#define TIMER          0x00001000u   /* a KTIMER, 40 bytes */
#define DPC            0x00001040u   /* its KDPC, 32 bytes */
#define TIMER_DUE_MS   20
#define TIMER2         0x00001080u   /* the KTIMER KeCancelTimer is given */
#define DPC2           0x000010C0u   /* its KDPC, which must never run */
#define TIMER2_DUE_MS  50
#define TIB            0x00003000u

/* What the runtime gives a recompiled function: its registers, per thread,
 * and guest memory. */
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_fs_base;
ptrdiff_t g_xbox_mem_offset;
static uint8_t *s_guest;

ptrdiff_t xbox_GetMemoryOffset(void) { return g_xbox_mem_offset; }
size_t xbox_GetMappedSize(void) { return GUEST_BYTES; }
/* Which thread took the timer thread's guest stack and TIB:
 * kernel_timer_start must, before the thread runs, so that what they write
 * to the log comes in order. */
static DWORD s_main_thread;
static volatile LONG s_tibs, s_taken_elsewhere;
static void taken_here(void)
{
    if (GetCurrentThreadId() != s_main_thread)
        __atomic_store_n(&s_taken_elsewhere, 1, __ATOMIC_RELAXED);
}
uint32_t xbox_AllocThreadTib(void)
{
    taken_here();
    __atomic_add_fetch(&s_tibs, 1, __ATOMIC_RELAXED);
    return TIB;
}

/* The worker stack slices, as xbox_memory_layout.c hands them out. */
static volatile LONG s_slots;
int xbox_worker_stack_alloc(void)
{
    int i;
    taken_here();
    for (i = 0; i < XBOX_WORKER_STACK_COUNT; i++) {
        LONG bit = 1L << i;
        if (!(__atomic_fetch_or(&s_slots, bit, __ATOMIC_ACQ_REL) & bit))
            return i;
    }
    return -1;
}

/* The timer thread's other work, each pass. */
static volatile LONG s_passes;
void kernel_vblank_tick(void) { __atomic_add_fetch(&s_passes, 1, __ATOMIC_RELAXED); }
void kernel_pgraph_tick(void) { }
void kernel_apu_tick(void) { }
void kernel_cvfs_watch(void) { }
void kernel_drain_dpcs(void) { }
void xbox_kernel_thread_report(void) { }

/* What a timer that fires does: its object signalled, its DPC run. */
static volatile LONG s_signalled, s_dpc_runs, s_dpc2_runs;
static volatile uint32_t s_dpc_va;
uint32_t xbox_dispatcher_signal(uint32_t va)
{
    if (va == TIMER)
        __atomic_add_fetch(&s_signalled, 1, __ATOMIC_RELAXED);
    return 0;
}
int kernel_run_dpc(uint32_t dpc_va, uint32_t arg1, uint32_t arg2)
{
    (void)arg1; (void)arg2;
    if (dpc_va == DPC2) {
        __atomic_add_fetch(&s_dpc2_runs, 1, __ATOMIC_RELEASE);
        return 1;
    }
    __atomic_store_n(&s_dpc_va, dpc_va, __ATOMIC_RELAXED);
    __atomic_add_fetch(&s_dpc_runs, 1, __ATOMIC_RELEASE);
    return 1;
}

static int s_failures;

static void fail(const char *what)
{
    if (s_failures++ < 5)
        printf("kernel_clock: %s\n", what);
    fflush(stdout);
}

static uint32_t tick_count(void)
{
    return __atomic_load_n(&BRIDGE_MEM32(XBOX_KERNEL_DATA_BASE + KDATA_TICK_COUNT),
                           __ATOMIC_RELAXED);
}

/* A bridge's arguments on the guest stack, as the dispatcher passes them. */
static void call_bridge(void (*bridge)(void), const uint32_t *args, int n)
{
    int i;
    g_esp -= (uint32_t)n * 4;
    for (i = 0; i < n; i++) BRIDGE_MEM32(g_esp + (uint32_t)i * 4) = args[i];
    bridge();
    g_esp += (uint32_t)n * 4;
}

/* 1: the clock moves on its own. Watched until it has moved often enough to
 * test anything, not for a fixed time: how often the thread comes round
 * depends on the machine and how busy it is. */
static void the_clock_moves(int timing)
{
    uint32_t last = tick_count(), now_tick;
    long long start = (long long)GetTickCount64(), now, behind, most_behind = 0;
    long long last_move = start, step, largest_step = 0;
    int moves = 0;

    kernel_timer_start();
    if (__atomic_load_n(&s_tibs, __ATOMIC_RELAXED) != 1)
        fail("the timer thread's TIB was not taken before the thread started");
    while ((now = (long long)GetTickCount64()) - start < MAX_MS && moves < ENOUGH_MOVES) {
        Sleep(POLL_MS);
        now_tick = tick_count();
        now = (long long)GetTickCount64();
        if (now_tick != last) {
            moves++;
            step = now - last_move;
            if (step > largest_step) largest_step = step;
            last_move = now;
            last = now_tick;
        }
        /* How far the clock is behind the host's, as the guest would see it
         * now. The host clock is read after the guest's, so never behind. */
        behind = (long long)(uint32_t)((uint32_t)now - now_tick);
        if (moves && behind > most_behind) most_behind = behind;
        /* The thread has come round often enough to have moved it. */
        if (!moves && __atomic_load_n(&s_passes, __ATOMIC_RELAXED) >= ENOUGH_MOVES)
            break;
    }
    printf("kernel_clock: KeTickCount moved %d times in %lld ms, its largest step "
           "%lld ms, at most %lld ms behind the host's clock; %ld timer thread passes\n",
           moves, now - start, largest_step, most_behind,
           (long)__atomic_load_n(&s_passes, __ATOMIC_RELAXED));
    if (!moves)
        fail("KeTickCount never moved: nothing on the timer thread moves it");
    else if (moves < ENOUGH_MOVES)
        fail("KeTickCount moved too seldom to test anything");
    if (timing && most_behind > MAX_BEHIND_MS)
        fail("KeTickCount fell behind the host's clock");
    if (timing && largest_step > MAX_BEHIND_MS)
        fail("KeTickCount stood still");
}

/* 2: a timer set with KeSetTimer fires its DPC, once, after its due time. */
static void a_timer_fires(void)
{
    /* KeSetTimer(Timer, DueTime, Dpc): DueTime a LARGE_INTEGER, negative for
     * a time relative to now, in 100 ns units. */
    long long due = -(long long)TIMER_DUE_MS * 10000;
    const uint32_t init[] = { TIMER, 0 };
    const uint32_t set[] = { TIMER, (uint32_t)due, (uint32_t)((unsigned long long)due >> 32), DPC };
    long long t0, fired = -1;
    long runs, signalled;
    uint32_t dpc_va;

    call_bridge(bridge_KeInitializeTimerEx, init, 2);
    t0 = (long long)GetTickCount64();
    call_bridge(bridge_KeSetTimer, set, 4);
    if (g_eax != 0)
        fail("KeSetTimer said a timer never set was already set");
    while ((long long)GetTickCount64() - t0 < MAX_MS) {
        if (__atomic_load_n(&s_dpc_runs, __ATOMIC_ACQUIRE)) {
            fired = (long long)GetTickCount64() - t0;
            break;
        }
        Sleep(POLL_MS);
    }
    Sleep(TIMER_DUE_MS * 5);   /* a one-shot timer fires once */
    /* The timer thread's counts, read as it writes them. */
    runs = __atomic_load_n(&s_dpc_runs, __ATOMIC_ACQUIRE);
    signalled = __atomic_load_n(&s_signalled, __ATOMIC_RELAXED);
    dpc_va = __atomic_load_n(&s_dpc_va, __ATOMIC_RELAXED);
    printf("kernel_clock: the timer's DPC ran %ld times, first after %lld ms "
           "(due after %d); its object signalled %ld times\n",
           runs, fired, TIMER_DUE_MS, signalled);
    if (fired < 0)
        fail("the timer's DPC never ran");
    else if (fired < TIMER_DUE_MS)
        fail("the timer's DPC ran before its due time");
    if (runs > 1)
        fail("a one-shot timer fired more than once");
    if (runs && dpc_va != DPC)
        fail("the timer ran another DPC than its own");
    if (runs && signalled != runs)
        fail("the timer's object was not signalled when it fired");
}

/* 3: KeCancelTimer stops a timer that is set, says whether it was, and
 * leaves the game's KTIMER as it was. The bytes past its Type are the game's
 * (on the console, its kernel's list entry is at +24): filled here, so that a
 * cancel that reads them as anything else shows. */
static void a_timer_is_cancelled(void)
{
    long long due = -(long long)TIMER2_DUE_MS * 10000;
    const uint32_t init[] = { TIMER2, 0 };
    const uint32_t set[] = { TIMER2, (uint32_t)due, (uint32_t)((unsigned long long)due >> 32), DPC2 };
    const uint32_t cancel[] = { TIMER2 };
    uint8_t before[40];
    uint32_t first, second;
    int i;

    call_bridge(bridge_KeInitializeTimerEx, init, 2);
    for (i = 4; i < 40; i++)
        BRIDGE_MEM8(TIMER2 + (uint32_t)i) = (uint8_t)(0xA0 + i);
    memcpy(before, XBOX_TO_NATIVE(TIMER2), sizeof before);
    call_bridge(bridge_KeSetTimer, set, 4);
    call_bridge(bridge_KeCancelTimer, cancel, 1);
    first = g_eax;
    call_bridge(bridge_KeCancelTimer, cancel, 1);
    second = g_eax;
    Sleep(TIMER2_DUE_MS * 5);
    printf("kernel_clock: KeCancelTimer said %u for a set timer and %u for it "
           "cancelled; its DPC ran %ld times after\n", first, second,
           (long)__atomic_load_n(&s_dpc2_runs, __ATOMIC_ACQUIRE));
    if (first != 1)
        fail("KeCancelTimer did not say a set timer was set");
    if (second != 0)
        fail("KeCancelTimer said a cancelled timer was still set");
    if (__atomic_load_n(&s_dpc2_runs, __ATOMIC_ACQUIRE))
        fail("a cancelled timer still ran its DPC");
    if (memcmp(before, XBOX_TO_NATIVE(TIMER2), sizeof before))
        fail("KeCancelTimer changed the game's KTIMER");
}

/* 4: a timer at guest address 0, a pointer gone wrong in the game, is
 * turned away, and nothing is written there. run.py looks for the [CHECK]
 * line. */
static void a_null_timer_is_turned_away(void)
{
    const uint32_t init[] = { 0, 0 };
    uint8_t before[40];
    int i;

    for (i = 0; i < 40; i++) s_guest[i] = (uint8_t)(0x50 + i);
    memcpy(before, s_guest, sizeof before);
    call_bridge(bridge_KeInitializeTimerEx, init, 2);
    if (memcmp(before, s_guest, sizeof before))
        fail("KeInitializeTimerEx wrote a timer at guest address 0");
}

int main(void)
{
    /* Under the thread sanitizer (tests/run_all.py --tsan) the checks of
     * time are left out, as the sanitizer slows every thread. */
    const char *sanitizer = getenv("TEST_SANITIZER");
    int slot;

    s_main_thread = GetCurrentThreadId();
    s_guest = calloc(1, GUEST_BYTES);
    if (!s_guest) return 2;
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)s_guest;
    slot = xbox_worker_stack_alloc();
    g_esp = XBOX_WORKER_STACK_TOP(slot);

    the_clock_moves(!(sanitizer && *sanitizer));
    a_timer_fires();
    a_timer_is_cancelled();
    a_null_timer_is_turned_away();
    if (__atomic_load_n(&s_taken_elsewhere, __ATOMIC_RELAXED))
        fail("the timer thread took its own stack or TIB: kernel_timer_start "
             "must, before it starts, so its lines in the log come in order");

    /* The timer thread runs on, as it does until the engine exits. */
    if (s_failures) {
        printf("kernel_clock: %d failures\n", s_failures);
        fflush(stdout);
        _exit(1);
    }
    printf("kernel_clock: all cases pass\n");
    fflush(stdout);
    _exit(0);
}
