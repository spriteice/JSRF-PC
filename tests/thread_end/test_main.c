/* Run with: python tests/thread_end/run.py
 *
 * What a thread started by src/platform/win32_compat.c's CreateThread leaves
 * behind once it has ended (known bug 21). On the console a thread's stack
 * goes when the thread ends, and its object, with the exit code, lasts while
 * a handle to it is open.
 *
 * - Threads that end, with their handles closed, give their stacks back: of
 *   the 4 MiB stacks sixty-four of them ran on, fewer than a quarter are
 *   still mapped once they have ended (glibc keeps a few for the next
 *   threads). The stacks are counted, not the address space: a thread that
 *   allocates or frees is given a malloc arena, 64 MiB of address space that
 *   glibc keeps for the next threads. These threads allocate, as the game's
 *   do, so the arena is there in every run. Left out under the thread
 *   sanitizer, which reports a thread left behind itself.
 * - A thread that has ended is not reached through its handle: terminating
 *   it leaves a newer thread running, also when the newer thread was given
 *   the ended one's stack, and so its pthread_t.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "win32_compat.h"

#define THREADS      64
#define STACK_BYTES  (4u << 20)
#define BLOCK_BYTES  64          /* what each of them allocates */

static void fail(const char *what)
{
    printf("FAIL: %s\n", what);
    fflush(stdout);
    _exit(1);
}

/* How many of the process's mappings hold one or more of these addresses. */
static int mappings_holding(const uintptr_t *at, int n)
{
    char *line = NULL;
    size_t size = 0;
    int i, held = 0;
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) fail("no /proc/self/maps");
    while (getline(&line, &size, f) > 0) {
        uintptr_t lo, hi;
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &lo, &hi) != 2) continue;
        for (i = 0; i < n; i++)
            if (at[i] >= lo && at[i] < hi) { held++; break; }
    }
    free(line);
    fclose(f);
    return held;
}

/* Until the host thread with this id has gone: a thread's handle is
 * signalled as it ends, a moment before its host thread has, and glibc gives
 * its stack to a new thread only then. */
static void until_gone(pid_t tid)
{
    char path[64];
    struct timespec t0, now;
    snprintf(path, sizeof path, "/proc/self/task/%d", (int)tid);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (access(path, F_OK) == 0) {
        sched_yield();
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - t0.tv_sec > 5) fail("a host thread was still there 5 s after its end");
    }
}

/* ---- the stacks of threads that ended ------------------------------------ */

static volatile pid_t g_tid;
static uintptr_t g_stack[THREADS];   /* an address on each thread's stack */

/* Notes where its stack is, through arg, and allocates. */
static DWORD WINAPI returns(LPVOID arg)
{
    char on_stack;
    void *volatile block = malloc(BLOCK_BYTES);
    free(block);
    *(uintptr_t *)arg = (uintptr_t)&on_stack;
    g_tid = gettid();
    return 7;
}

static void stacks_given_back(void)
{
    int i, kept;

    if (getenv("TEST_SANITIZER")) {
        printf("thread_end: the stacks' case is left out under the sanitizer\n");
        return;
    }
    for (i = 0; i < THREADS; i++) {
        DWORD code = 0;
        HANDLE h = CreateThread(NULL, STACK_BYTES, returns, &g_stack[i], 0, NULL);
        if (!h) fail("CreateThread");
        if (WaitForSingleObject(h, 5000) != WAIT_OBJECT_0) fail("a thread did not end");
        if (!GetExitCodeThread(h, &code) || code != 7) fail("the exit code was not kept");
        CloseHandle(h);
        until_gone(g_tid);
    }
    kept = mappings_holding(g_stack, THREADS);
    if (kept >= THREADS / 4) {
        printf("%d threads ended and %d of the stacks they ran on are still "
               "mapped\n", THREADS, kept);
        fail("threads that ended kept their stacks");
    }
    printf("thread_end: %d threads that ended gave their stacks back "
           "(%d of the stacks they ran on still mapped)\n", THREADS, kept);
}

/* ---- a thread that ended, reached through its handle --------------------- */

static pthread_t g_ended, g_newer;
static volatile pid_t g_ended_tid;
static volatile int g_newer_started;
static int g_pipe[2];

static DWORD WINAPI notes_itself(LPVOID arg)
{
    (void)arg;
    g_ended = pthread_self();
    g_ended_tid = gettid();
    return 3;
}

/* Blocks in read, where a cancel would end it, until the main thread writes. */
static DWORD WINAPI blocks(LPVOID arg)
{
    char c;
    (void)arg;
    g_newer = pthread_self();
    __atomic_store_n(&g_newer_started, 1, __ATOMIC_RELEASE);
    if (read(g_pipe[0], &c, 1) != 1) return 0;
    return 5;
}

static void ended_not_reached(void)
{
    HANDLE ended, newer;
    DWORD code = 0;
    int reused;

    if (pipe(g_pipe) != 0) fail("pipe");
    ended = CreateThread(NULL, 0, notes_itself, NULL, 0, NULL);
    if (!ended) fail("CreateThread");
    if (WaitForSingleObject(ended, 5000) != WAIT_OBJECT_0) fail("the first thread did not end");
    until_gone(g_ended_tid);

    newer = CreateThread(NULL, 0, blocks, NULL, 0, NULL);
    if (!newer) fail("CreateThread");
    while (!__atomic_load_n(&g_newer_started, __ATOMIC_ACQUIRE)) sched_yield();
    reused = pthread_equal(g_ended, g_newer);

    TerminateThread(ended, 9);

    if (write(g_pipe[1], "x", 1) != 1) fail("write");
    if (WaitForSingleObject(newer, 5000) != WAIT_OBJECT_0 ||
        !GetExitCodeThread(newer, &code) || code != 5) {
        printf("the newer thread %s the ended one's stack\n",
               reused ? "was given" : "was not given");
        fail("terminating a thread that had ended stopped a newer one");
    }
    CloseHandle(newer);
    CloseHandle(ended);
    printf("thread_end: terminating a thread that had ended left a newer one "
           "running (the newer one %s the ended one's stack)\n",
           reused ? "was given" : "was not given");
}

int main(void)
{
    stacks_given_back();
    ended_not_reached();
    printf("thread_end: all cases pass\n");
    return 0;
}
