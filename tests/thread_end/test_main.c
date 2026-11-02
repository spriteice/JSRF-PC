/* Run with: python tests/thread_end/run.py
 *
 * What a thread started by src/platform/win32_compat.c's CreateThread leaves
 * behind once it has ended (known bug 21). On the console a thread's stack
 * goes when the thread ends, and its object, with the exit code, lasts while
 * a handle to it is open.
 *
 * - Threads that end, with their handles closed, give their stacks back:
 *   sixty-four of them, each with a 4 MiB stack, leave the address space less
 *   than a quarter of their stacks larger (glibc keeps a few for the next
 *   threads). Left out under the thread sanitizer, which reports a thread
 *   left behind itself, and whose own mappings would be in the count.
 * - A thread that has ended is not reached through its handle: terminating
 *   it leaves a newer thread running, also when the newer thread was given
 *   the ended one's stack, and so its pthread_t.
 */
#define _GNU_SOURCE
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

static void fail(const char *what)
{
    printf("FAIL: %s\n", what);
    fflush(stdout);
    _exit(1);
}

static long vm_size_kib(void)
{
    char line[256];
    long kib = -1;
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) fail("no /proc/self/status");
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "VmSize: %ld", &kib) == 1) break;
    fclose(f);
    return kib;
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

static DWORD WINAPI returns(LPVOID arg)
{
    (void)arg;
    g_tid = gettid();
    return 7;
}

static void stacks_given_back(void)
{
    long before, grown;
    int i;

    if (getenv("TEST_SANITIZER")) {
        printf("thread_end: the stacks' case is left out under the sanitizer\n");
        return;
    }
    before = vm_size_kib();
    for (i = 0; i < THREADS; i++) {
        DWORD code = 0;
        HANDLE h = CreateThread(NULL, STACK_BYTES, returns, NULL, 0, NULL);
        if (!h) fail("CreateThread");
        if (WaitForSingleObject(h, 5000) != WAIT_OBJECT_0) fail("a thread did not end");
        if (!GetExitCodeThread(h, &code) || code != 7) fail("the exit code was not kept");
        CloseHandle(h);
        until_gone(g_tid);
    }
    grown = vm_size_kib() - before;
    if (grown >= (long)THREADS * (STACK_BYTES >> 10) / 4) {
        printf("%d threads ended and the address space grew by %ld KiB, "
               "%ld of their %u KiB stacks\n", THREADS, grown,
               grown / (STACK_BYTES >> 10), STACK_BYTES >> 10);
        fail("threads that ended kept their stacks");
    }
    printf("thread_end: %d threads that ended gave their stacks back "
           "(the address space grew by %ld KiB)\n", THREADS, grown);
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
