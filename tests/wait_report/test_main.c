/* Run with: python tests/wait_report/run.py
 *
 * The handle waits in src/kernel/kernel_sync.c, and what they tell the guest
 * lock's report.
 *
 * The report (Developer > Logging > RECOMP_LOCK_WATCH) counts the threads
 * that take the guest lock over at a kernel call that gives it up, and splits
 * them by whether the call had to wait (kernel_bridge.c). A wait says which,
 * through xbox_kernel_call_waited: with the report on, it first looks without
 * blocking, and waits only when nothing was ready. That look must change no
 * result: an object it finds ready is taken once, as the wait would take it,
 * and an APC it runs ends the call as it would end the wait. With the report
 * off, the waits say nothing unless the timeout is zero, which is a look
 * that never waits.
 *
 * Every case runs with the report off and on, on a thread of its own, and
 * checks the status and what was said: a ready event, semaphore and mutex,
 * an auto-reset event taken once, a wait another thread ends by signalling,
 * a zero timeout, a timeout that runs out, wait-any and wait-all, both
 * kernels' calls, and an alertable wait with an APC queued.
 */
#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>

#define SIGNAL_AFTER_MS   50      /* the signalling thread's sleep first */
#define TIMEOUT_MS        20      /* a timeout that runs out */
#define TICKS_PER_MS      10000   /* NT time is in 100 ns units */
#define WAIT_ALL          0       /* NT's WaitType */
#define WAIT_ANY          1
#define SAID_NOTHING      (-1)

/* What kernel_bridge.c gives kernel_sync.c. Only the case thread waits. */
void xbox_log(int level, const char *subsystem, const char *fmt, ...)
{
    (void)level; (void)subsystem; (void)fmt;
}
static int s_watched;
static int s_said = SAID_NOTHING;
int xbox_kernel_call_watched(void) { return s_watched; }
void xbox_kernel_call_waited(int waited) { s_said = waited; }

static int s_failures;
static int s_apcs;

static void check(const char *what, NTSTATUS got, NTSTATUS want, int said)
{
    if (got != want) {
        printf("FAIL %s, report %s: status 0x%08X, not 0x%08X\n", what,
               s_watched ? "on" : "off", (unsigned)got, (unsigned)want);
        s_failures++;
    }
    if (s_said != said) {
        printf("FAIL %s, report %s: said %d, not %d\n", what,
               s_watched ? "on" : "off", s_said, said);
        s_failures++;
    }
    s_said = SAID_NOTHING;
}

/* What a wait with a timeout says: whether it waited with the report on, and
 * nothing with it off. */
static int said(int waited) { return s_watched ? waited : SAID_NOTHING; }

static LARGE_INTEGER relative_ms(long ms)
{
    LARGE_INTEGER t;
    t.QuadPart = -(LONGLONG)ms * TICKS_PER_MS;
    return t;
}

static DWORD WINAPI signal_later(LPVOID event)
{
    Sleep(SIGNAL_AFTER_MS);
    SetEvent((HANDLE)event);
    return 0;
}

static VOID CALLBACK count_apc(ULONG_PTR data)
{
    (void)data;
    s_apcs++;
}

static void cases(void)
{
    LARGE_INTEGER zero = relative_ms(0), timeout = relative_ms(TIMEOUT_MS);
    HANDLE ready = CreateEventA(NULL, TRUE, TRUE, NULL);
    HANDLE once = CreateEventA(NULL, FALSE, TRUE, NULL);
    HANDLE unset = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE sem = CreateSemaphoreA(NULL, 2, 2, NULL);
    HANDLE mutex = CreateMutexA(NULL, FALSE, NULL);
    HANDLE pair[2];
    HANDLE thread;

    check("a set event", xbox_NtWaitForSingleObject(ready, FALSE, NULL),
          STATUS_SUCCESS, said(0));

    check("an auto-reset event", xbox_NtWaitForSingleObjectEx(once, 0, FALSE, NULL),
          STATUS_SUCCESS, said(0));
    check("the same event again, with a zero timeout",
          xbox_NtWaitForSingleObject(once, FALSE, &zero), STATUS_TIMEOUT, 0);

    check("a semaphore of 2", xbox_KeWaitForSingleObject(sem, 0, 0, FALSE, NULL),
          STATUS_SUCCESS, said(0));
    check("the semaphore's second", xbox_KeWaitForSingleObject(sem, 0, 0, FALSE, NULL),
          STATUS_SUCCESS, said(0));
    check("the semaphore, empty, with a zero timeout",
          xbox_KeWaitForSingleObject(sem, 0, 0, FALSE, &zero), STATUS_TIMEOUT, 0);
    ReleaseSemaphore(sem, 2, NULL);

    check("a free mutex", xbox_NtWaitForSingleObject(mutex, FALSE, NULL),
          STATUS_SUCCESS, said(0));
    if (!ReleaseMutex(mutex) || ReleaseMutex(mutex)) {
        printf("FAIL a free mutex, report %s: not taken exactly once\n",
               s_watched ? "on" : "off");
        s_failures++;
    }

    thread = CreateThread(NULL, 0, signal_later, unset, 0, NULL);
    check("an event another thread sets", xbox_NtWaitForSingleObject(unset, FALSE, NULL),
          STATUS_SUCCESS, said(1));
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    check("an unset event, with a zero timeout",
          xbox_NtWaitForSingleObject(unset, FALSE, &zero), STATUS_TIMEOUT, 0);
    check("an unset event, with a timeout",
          xbox_NtWaitForSingleObject(unset, FALSE, &timeout), STATUS_TIMEOUT, said(1));

    pair[0] = unset;
    pair[1] = once;
    SetEvent(once);
    check("wait-any, the second set",
          xbox_NtWaitForMultipleObjectsEx(2, pair, WAIT_ANY, FALSE, NULL),
          STATUS_SUCCESS + 1, said(0));
    check("the second again, with a zero timeout",
          xbox_NtWaitForSingleObject(once, FALSE, &zero), STATUS_TIMEOUT, 0);
    SetEvent(once);
    check("KeWaitForMultipleObjects, wait-any, the second set",
          xbox_KeWaitForMultipleObjects(2, (PVOID *)pair, WAIT_ANY, 0, 0, FALSE,
                                        NULL, NULL),
          STATUS_SUCCESS + 1, said(0));

    pair[0] = ready;
    pair[1] = sem;
    check("wait-all, both ready",
          xbox_NtWaitForMultipleObjectsEx(2, pair, WAIT_ALL, FALSE, NULL),
          STATUS_SUCCESS, said(0));
    ReleaseSemaphore(sem, 1, NULL);

    s_apcs = 0;
    QueueUserAPC(count_apc, GetCurrentThread(), 0);
    check("an alertable wait with an APC queued",
          xbox_NtWaitForSingleObject(unset, TRUE, NULL), STATUS_ALERTED, said(0));
    if (s_apcs != 1) {
        printf("FAIL the APC, report %s: ran %d times, not once\n",
               s_watched ? "on" : "off", s_apcs);
        s_failures++;
    }

    CloseHandle(ready);
    CloseHandle(once);
    CloseHandle(unset);
    CloseHandle(sem);
    CloseHandle(mutex);
}

static DWORD WINAPI case_thread(LPVOID unused)
{
    (void)unused;
    s_watched = 0;
    cases();
    s_watched = 1;
    cases();
    return 0;
}

int main(void)
{
    HANDLE thread = CreateThread(NULL, 0, case_thread, NULL, 0, NULL);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    if (s_failures) {
        printf("wait_report: %d failures\n", s_failures);
        return 1;
    }
    printf("wait_report: all cases pass, with the report off and on\n");
    return 0;
}
