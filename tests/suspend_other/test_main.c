/* Run with: python tests/suspend_other/run.py
 *
 * A suspend aimed at another thread, through xbox_NtSuspendThread and
 * xbox_NtResumeThread (src/kernel/kernel_thread.c, whole), with the guest
 * heap of src/kernel/xbox_heap.c, whole. On Linux the threads are
 * src/platform/win32_compat.c's, on Windows the system's.
 *
 * "counted" (run.py's first run):
 *
 * - Known bug 49 (issue 2): a thread allocates and frees 4,500-byte blocks
 *   from the guest heap without a pause, as a new guest thread allocates its
 *   TIB, and so holds the heap's lock much of the time. Two thousand times,
 *   the main thread suspends it, allocates a block of its own, and resumes
 *   it. The thread is counted as suspended (previous counts 0, then 1 on the
 *   resume), reaches no checkpoint and carries on, and every allocation
 *   returns. Had the suspend stopped it inside the heap's lock, the main
 *   thread's allocation would wait for good, and the watch says so.
 * - A thread suspended twice by another stops at its next checkpoint, as the
 *   guest lock's time-slice check makes one (kernel_bridge.c's
 *   xbox_guest_lock_yield_now), and stays stopped until both suspends are
 *   resumed: previous counts 0 and 1, then 2 and 1.
 * - A thread that suspends itself, through its own handle, stops there
 *   until another resumes it.
 *
 * "real" (the second, with RECOMP_SUSPEND_REAL=1): only the first case, with
 * a watch of 100 ms, and it must fail, so that the first run is known to see
 * the hang: on Windows the suspend stops the thread where it is, which was
 * every Windows build's way before this, and on Linux a signal stops it
 * there for up to a quarter of a second. It lets the thread run for a
 * moment after each resume, and goes on suspending until an allocation
 * waits, for up to REAL_MS: on the PC, 2,000 rounds one straight after
 * another missed it in one run of 20 (11 November), and a control that
 * misses fails the test.
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

/* What the runtime gives the heap: guest memory and its size, the calling
 * thread's guest stack (none here), and where the game's code is. */
RECOMP_TLS uint32_t g_esp;
ptrdiff_t g_xbox_mem_offset;
size_t g_xbox_total_ram = 0x04000000u;   /* the heap runs to 64 MB */
size_t g_xbox_map_size;
size_t xbox_GetMappedSize(void) { return g_xbox_total_ram; }
uint32_t g_xbox_code_lo, g_xbox_code_hi;
void xbox_log(int level, const char *subsystem, const char *fmt, ...)
{
    (void)level; (void)subsystem; (void)fmt;
}
void xbox_kernel_call_waited(int waited) { (void)waited; }

#define ROUNDS   2000
#define REAL_MS  30000    /* the control's rounds, until an allocation waits */
#define TIB      4500     /* bytes: a guest thread's TIB, as it starts */
#define WAIT_MS  2000     /* for anything that should happen at once, however busy */

static void fail(const char *what)
{
    printf("FAIL: %s\n", what);
    fflush(stdout);
    _exit(1);
}

static long long now_ms(void) { return (long long)GetTickCount64(); }

static int load(volatile int *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void store(volatile int *p, int v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static long long load64(volatile long long *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }

static ULONG suspend(HANDLE h)
{
    ULONG prev = 0xAAAAAAAAu;
    if (xbox_NtSuspendThread(h, &prev) != STATUS_SUCCESS) fail("NtSuspendThread failed");
    return prev;
}

static ULONG resume(HANDLE h)
{
    ULONG prev = 0xAAAAAAAAu;
    if (xbox_NtResumeThread(h, &prev) != STATUS_SUCCESS) fail("NtResumeThread failed");
    return prev;
}

/* ---- the watch: an allocation that doesn't return ------------------------ */

static volatile long long s_alloc_since;   /* when the main thread's began, or 0 */
static long long s_watch_ms;
static volatile int s_round;                /* the suspend the main thread is in */

static DWORD WINAPI watch(LPVOID arg)
{
    (void)arg;
    for (;;) {
        long long since = load64(&s_alloc_since);
        if (since && now_ms() - since > s_watch_ms) {
            printf("FAIL: the main thread's heap allocation has waited over %lld ms, "
                   "in round %d: the thread it suspended is stopped holding the heap's "
                   "lock (known bug 49)\n", s_watch_ms, load(&s_round) + 1);
            fflush(stdout);
            _exit(1);
        }
        Sleep(5);
    }
    return 0;
}

/* ---- known bug 49: suspended inside the heap's lock ---------------------- */

static volatile int s_stop, s_rounds;

/* Half a millisecond, busy. A real suspend lands a little after the call
 * returns, on Windows as on Linux (a signal), and the main thread allocates
 * after this, so that the suspend has landed by then. */
static void settle(void)
{
    LARGE_INTEGER f, t0, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    do QueryPerformanceCounter(&t);
    while ((t.QuadPart - t0.QuadPart) * 2000 < f.QuadPart);
}

static DWORD WINAPI allocates(LPVOID arg)
{
    (void)arg;
    while (!load(&s_stop)) {
        uint32_t va = xbox_HeapAlloc(TIB, 16, XBOX_HEAP_RUNTIME);
        if (!va) fail("the heap had no 4,500 bytes for the thread");
        xbox_HeapFree(va, XBOX_HEAP_RUNTIME);
        __atomic_add_fetch(&s_rounds, 1, __ATOMIC_RELEASE);
    }
    return 0;
}

static void suspended_inside_the_heap(int real)
{
    HANDLE h;
    int i, ran_suspended = 0;
    long long until = now_ms() + REAL_MS;

    store(&s_stop, 0);
    h = CreateThread(NULL, 0, allocates, NULL, 0, NULL);
    if (!h) fail("CreateThread");
    while (load(&s_rounds) < 100) Sleep(0);
    for (i = 0; real ? now_ms() < until : i < ROUNDS; i++) {
        ULONG before, after;
        int rounds;
        uint32_t va;
        store(&s_round, i);
        before = suspend(h);
        settle();
        rounds = load(&s_rounds);
        __atomic_store_n(&s_alloc_since, now_ms(), __ATOMIC_RELEASE);
        va = xbox_HeapAlloc(TIB, 16, XBOX_HEAP_RUNTIME);
        __atomic_store_n(&s_alloc_since, 0, __ATOMIC_RELEASE);
        if (!va) fail("the heap had no 4,500 bytes for the main thread");
        xbox_HeapFree(va, XBOX_HEAP_RUNTIME);
        if (load(&s_rounds) != rounds) ran_suspended = 1;
        after = resume(h);
        /* The control lets the thread run a while before the next suspend,
         * so that each lands at a new point of its loop, however the system
         * schedules the two threads: on the PC, one run in 20 of 2,000 rounds
         * straight after each other caught nothing. */
        if (real) Sleep(1);
        if (!real && (before != 0 || after != 1)) {
            printf("FAIL: round %d: previous counts %lu on the suspend and %lu on the "
                   "resume, not 0 and 1\n", i, (unsigned long)before, (unsigned long)after);
            fflush(stdout);
            _exit(1);
        }
    }
    store(&s_stop, 1);
    if (WaitForSingleObject(h, WAIT_MS * 5) != WAIT_OBJECT_0) fail("the thread didn't end");
    CloseHandle(h);
    if (real) return;
    /* Counted, it ran on while suspended, as CRI's spinner does. Not every
     * round: the main thread's allocation is quick. */
    if (!ran_suspended) fail("the thread never ran while suspended: was it stopped?");
    printf("suspend_other: %d suspends of a thread allocating from the heap, "
           "and every allocation of the main thread's returned\n", ROUNDS);
}

/* ---- stopped at a checkpoint ---------------------------------------------- */

static volatile int s_turns;

static DWORD WINAPI checks(LPVOID arg)
{
    (void)arg;
    while (!load(&s_stop)) {
        __atomic_add_fetch(&s_turns, 1, __ATOMIC_RELEASE);
        if (xbox_thread_suspend_pending())   /* as xbox_guest_lock_yield_now */
            xbox_thread_suspend_checkpoint();
        Sleep(0);
    }
    return 0;
}

/* Whether s_turns stays still for ms. */
static int still_for(int ms)
{
    int turns = load(&s_turns);
    Sleep((DWORD)ms);
    return load(&s_turns) == turns;
}

static int moves_within(int ms)
{
    int turns = load(&s_turns);
    long long t0 = now_ms();
    while (now_ms() - t0 < ms) {
        if (load(&s_turns) != turns) return 1;
        Sleep(1);
    }
    return 0;
}

static void stops_at_a_checkpoint(void)
{
    HANDLE h;
    ULONG p1, p2, r1, r2;
    long long t0;

    store(&s_stop, 0);
    h = CreateThread(NULL, 0, checks, NULL, 0, NULL);
    if (!h) fail("CreateThread");
    if (!moves_within(WAIT_MS)) fail("the checking thread never ran");
    p1 = suspend(h);
    p2 = suspend(h);
    if (p1 != 0 || p2 != 1) fail("two suspends: previous counts not 0 and 1");
    t0 = now_ms();
    while (!still_for(20))
        if (now_ms() - t0 > WAIT_MS) fail("a thread suspended by another never stopped at its checkpoint");
    if (!still_for(100)) fail("a thread stopped at its checkpoint went on, unresumed");
    r1 = resume(h);
    if (r1 != 2) fail("the first resume: previous count not 2");
    if (!still_for(50)) fail("a thread went on with a suspend still outstanding");
    r2 = resume(h);
    if (r2 != 1) fail("the second resume: previous count not 1");
    if (!moves_within(WAIT_MS)) fail("a thread stopped at its checkpoint didn't go on when resumed");
    store(&s_stop, 1);
    if (WaitForSingleObject(h, WAIT_MS * 5) != WAIT_OBJECT_0) fail("the thread didn't end");
    CloseHandle(h);
    printf("suspend_other: a thread suspended twice by another stopped at its checkpoint, "
           "and went on after two resumes\n");
}

/* ---- a thread that suspends itself ---------------------------------------- */

static volatile int s_about, s_back;
static HANDLE s_self;

static DWORD WINAPI parks_itself(LPVOID arg)
{
    (void)arg;
    while (!__atomic_load_n(&s_self, __ATOMIC_ACQUIRE)) Sleep(0);
    store(&s_about, 1);
    suspend(s_self);
    store(&s_back, 1);
    return 0;
}

static void parks_itself_for_real(void)
{
    HANDLE h;
    long long t0;

    h = CreateThread(NULL, 0, parks_itself, NULL, 0, NULL);
    if (!h) fail("CreateThread");
    __atomic_store_n(&s_self, h, __ATOMIC_RELEASE);
    while (!load(&s_about)) Sleep(0);
    /* Under the 400 ms after which Linux lets a self-park go
     * (win32_compat.c's SuspendThread). */
    Sleep(200);
    if (load(&s_back)) fail("a thread that suspended itself went on, unresumed");
    /* A resume can come before the suspend; Windows drops it (previous count
     * 0) and Linux keeps it for the suspend, so resume until one lands. */
    t0 = now_ms();
    while (!load(&s_back) && resume(h) != 1)
        if (now_ms() - t0 > WAIT_MS) fail("no resume let the thread go");
    t0 = now_ms();
    while (!load(&s_back))
        if (now_ms() - t0 > WAIT_MS) fail("a thread that suspended itself didn't go on when resumed");
        else Sleep(1);
    if (WaitForSingleObject(h, WAIT_MS * 5) != WAIT_OBJECT_0) fail("the thread didn't end");
    CloseHandle(h);
    printf("suspend_other: a thread that suspended itself stopped until it was resumed\n");
}

int main(int argc, char **argv)
{
    int real = argc > 1 && !strcmp(argv[1], "real");
    void *ram = VirtualAlloc(NULL, XBOX_HEAP_TOP, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    HANDLE w;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!ram) fail("no memory for the guest");
    g_xbox_mem_offset = (ptrdiff_t)ram;
    xbox_HeapInit();                            /* as xbox_MemoryLayoutInit does */
    s_watch_ms = real ? 100 : WAIT_MS;
    w = CreateThread(NULL, 0, watch, NULL, 0, NULL);
    if (!w) fail("CreateThread");

    suspended_inside_the_heap(real);
    if (real) {
        printf("suspend_other: real suspends for %d s, and no allocation waited\n",
               REAL_MS / 1000);
        return 0;
    }
    stops_at_a_checkpoint();
    parks_itself_for_real();
    printf("suspend_other: all cases pass\n");
    return 0;
}
