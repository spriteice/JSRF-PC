/*
 * kernel_thread.c - Xbox Threading Subsystem
 *
 * Implements Xbox thread creation, termination, delays, and priority
 * management using Win32 threading APIs.
 *
 * Xbox threading model:
 *   - PsCreateSystemThreadEx creates kernel-mode threads (→ CreateThread)
 *   - Thread start routines are __stdcall with a single PVOID context
 *   - Time intervals use NT 100-nanosecond units (negative = relative)
 *   - Thread priorities use NT KPRIORITY increments
 */

#include "kernel.h"
#include "xbox_memory_layout.h"   /* XBOX_WORKER_STACK_* + worker-stack decls */
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * KeDelayExecutionThread
 *
 * Delays the current thread. The interval uses NT 100-nanosecond units:
 *   - Negative values = relative delay (most common)
 *   - Positive values = absolute time (rare)
 *   - Zero = yield
 *
 * Maps to: SleepEx (for alertable waits) or Sleep
 * ============================================================================ */

NTSTATUS __stdcall xbox_KeDelayExecutionThread(
    KPROCESSOR_MODE WaitMode,
    BOOLEAN Alertable,
    PLARGE_INTEGER Interval)
{
    DWORD ms;

    (void)WaitMode;

    if (!Interval)
        return STATUS_INVALID_PARAMETER;

    if (Interval->QuadPart == 0) {
        /* Zero interval = yield the thread's time slice */
        xbox_kernel_call_waited(0);
        SwitchToThread();
        return STATUS_SUCCESS;
    }

    if (Interval->QuadPart < 0) {
        /* Negative = relative time in 100ns units. Convert to milliseconds. */
        LONGLONG relative_100ns = -Interval->QuadPart;
        ms = (DWORD)(relative_100ns / 10000);
        /* Ensure at least 1ms for very short intervals */
        if (ms == 0 && relative_100ns > 0)
            ms = 1;
    } else {
        /* Positive = absolute time. Calculate relative delay from now. */
        LARGE_INTEGER now;
        GetSystemTimeAsFileTime((LPFILETIME)&now);
        LONGLONG diff = Interval->QuadPart - now.QuadPart;
        if (diff <= 0) {
            xbox_kernel_call_waited(0);
            return STATUS_SUCCESS; /* Already past */
        }
        ms = (DWORD)(diff / 10000);
    }
    xbox_kernel_call_waited(1);

    if (Alertable) {
        DWORD result = SleepEx(ms, TRUE);
        if (result == WAIT_IO_COMPLETION)
            return STATUS_ALERTED;
    } else {
        Sleep(ms);
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
 * Thread Priority
 *
 * Xbox uses NT KPRIORITY base priority increments relative to the process.
 * We map these to Win32 thread priority levels.
 * ============================================================================ */

/*
 * Map Xbox priority increment to Win32 priority level.
 * Xbox base priorities typically range from -2 to +2 for game threads.
 */
static int xbox_priority_to_win32(LONG increment)
{
    if (increment <= -15)       return THREAD_PRIORITY_IDLE;
    else if (increment <= -2)   return THREAD_PRIORITY_LOWEST;
    else if (increment == -1)   return THREAD_PRIORITY_BELOW_NORMAL;
    else if (increment == 0)    return THREAD_PRIORITY_NORMAL;
    else if (increment == 1)    return THREAD_PRIORITY_ABOVE_NORMAL;
    else if (increment <= 2)    return THREAD_PRIORITY_HIGHEST;
    else                        return THREAD_PRIORITY_TIME_CRITICAL;
}

static LONG win32_priority_to_xbox(int priority)
{
    switch (priority) {
        case THREAD_PRIORITY_IDLE:          return -15;
        case THREAD_PRIORITY_LOWEST:        return -2;
        case THREAD_PRIORITY_BELOW_NORMAL:  return -1;
        case THREAD_PRIORITY_NORMAL:        return 0;
        case THREAD_PRIORITY_ABOVE_NORMAL:  return 1;
        case THREAD_PRIORITY_HIGHEST:       return 2;
        case THREAD_PRIORITY_TIME_CRITICAL: return 15;
        default:                            return 0;
    }
}

/*
 * On Windows the priorities the title asks for are kept, not applied.
 *
 * They were applied, and Windows is the only host where that did anything:
 * elsewhere SetThreadPriority is win32_compat's, which only remembers the
 * number. JSRF gives CRI's ADX worker THREAD_PRIORITY_IDLE, and that thread
 * reads and decodes the music stream. On the console an idle thread runs
 * whenever the others wait -- which is half of every frame. On a Windows PC
 * it runs only when no normal-priority thread anywhere wants the core it is
 * on (the game's own threads, the browser, the overlay), so under load it
 * was starved and the music ran dry: audio stutter on Windows alone. The
 * title's other requests were real there as well: HIGHEST and ABOVE_NORMAL
 * for three of its threads, and one thread raising itself to TIME_CRITICAL
 * and back some fifty times a second.
 *
 * So Windows now does what the Mac and Linux builds always did: every guest
 * thread runs at normal priority, and the title reads back what it set.
 */
#if defined(_WIN32)
#define GUEST_PRIO_SLOTS 256
static struct { DWORD tid; int prio; } g_guest_prio[GUEST_PRIO_SLOTS];
static SRWLOCK g_guest_prio_lock = SRWLOCK_INIT;

static int guest_priority_get(HANDLE h)
{
    DWORD tid = GetThreadId(h);
    int i, p = THREAD_PRIORITY_NORMAL;
    AcquireSRWLockShared(&g_guest_prio_lock);
    for (i = 0; i < GUEST_PRIO_SLOTS; i++)
        if (g_guest_prio[i].tid == tid) { p = g_guest_prio[i].prio; break; }
    ReleaseSRWLockShared(&g_guest_prio_lock);
    return p;
}

static void guest_priority_set(HANDLE h, int prio)
{
    DWORD tid = GetThreadId(h);
    int i, free_slot = -1;
    if (!tid) return;
    AcquireSRWLockExclusive(&g_guest_prio_lock);
    for (i = 0; i < GUEST_PRIO_SLOTS; i++) {
        if (g_guest_prio[i].tid == tid) { g_guest_prio[i].prio = prio; free_slot = -2; break; }
        if (!g_guest_prio[i].tid && free_slot == -1) free_slot = i;
    }
    if (free_slot >= 0) { g_guest_prio[free_slot].tid = tid; g_guest_prio[free_slot].prio = prio; }
    ReleaseSRWLockExclusive(&g_guest_prio_lock);
}
#else
static int  guest_priority_get(HANDLE h)           { return GetThreadPriority(h); }
static void guest_priority_set(HANDLE h, int prio) { SetThreadPriority(h, prio); }
#endif

/* The priority the title gave a thread (the guest lock asks it). */
int xbox_thread_guest_priority(HANDLE h) { return guest_priority_get(h); }

/* Whether h is the calling thread: by the pseudo-handle, the handle the thread
 * was created with, or a duplicate. A suspend of the calling thread blocks it
 * and any other suspend does not, so this decides whether NtSuspendThread
 * gives the guest lock up (kernel_bridge.c). CRI's idle worker suspends itself
 * through its own handle, not the pseudo-handle, so the handle's value cannot
 * decide it.
 *
 * Windows: by thread id, as guest_priority_get does. Elsewhere every handle to
 * a thread is that thread's one object in win32_compat.c (DuplicateHandle hands
 * back the same one), and GetCurrentThread returns it. */
int xbox_thread_is_current(HANDLE h)
{
    if (!h) return 0;
#if defined(_WIN32)
    return GetThreadId(h) == GetCurrentThreadId();
#else
    return h == GetCurrentThread();
#endif
}

LONG __stdcall xbox_KeSetBasePriorityThread(PVOID Thread, LONG Increment)
{
    HANDLE hThread = (HANDLE)Thread;
    LONG previous;

    /* Get previous priority before setting new one */
    int prev_win32 = guest_priority_get(hThread);
    previous = win32_priority_to_xbox(prev_win32);

    int new_win32 = xbox_priority_to_win32(Increment);
    guest_priority_set(hThread, new_win32);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "KeSetBasePriorityThread: thread=%p, increment=%d (win32=%d), prev=%d",
        Thread, Increment, new_win32, previous);

    return previous;
}

LONG __stdcall xbox_KeQueryBasePriorityThread(PVOID Thread)
{
    HANDLE hThread = (HANDLE)Thread;
    int win32_priority = guest_priority_get(hThread);
    return win32_priority_to_xbox(win32_priority);
}

/* ============================================================================
 * KeAlertThread
 *
 * Sends an alert to a thread, which can wake it from an alertable wait.
 * On Xbox, this sets the alerted flag on the thread object.
 * We approximate this with QueueUserAPC using a no-op APC routine.
 * ============================================================================ */

static VOID CALLBACK xbox_alert_apc(ULONG_PTR dwParam)
{
    (void)dwParam;
    /* No-op - the purpose is just to wake the thread from alertable wait */
}

NTSTATUS __stdcall xbox_KeAlertThread(PVOID Thread, KPROCESSOR_MODE AlertMode)
{
    HANDLE hThread = (HANDLE)Thread;

    (void)AlertMode;

    if (!QueueUserAPC(xbox_alert_apc, hThread, 0)) {
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_THREAD,
            "KeAlertThread: QueueUserAPC failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
 * NtYieldExecution
 *
 * Yields the current thread's remaining time slice.
 * Maps directly to SwitchToThread.
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtYieldExecution(void)
{
    SwitchToThread();
    return STATUS_SUCCESS;
}

/* ============================================================================
 * NtSuspendThread / NtResumeThread
 *
 * A suspend aimed at another thread is counted, and the thread carries on.
 * It stops at its next checkpoint instead (xbox_thread_suspend_checkpoint,
 * which the guest lock's time-slice check calls on entry to recompiled
 * functions), with the guest lock given up and nothing of ours held. A
 * thread that suspends itself stops for real, as it always did: that blocks
 * only the caller, and CRI's idle worker parks that way. Linux has always
 * done this (SuspendThread in src/platform/win32_compat.c), and since this
 * change Windows does too.
 *
 * Windows' own SuspendThread stops a thread wherever it is, and a guest
 * thread runs host code of ours that holds host locks: the guest heap's, the
 * C runtime's, and Windows' own while a thread starts. CRI's unlock suspends
 * its spinner thread thousands of times a run. On a PC busy installing
 * another game, the spinner hadn't finished starting when CRI's first unlock
 * came, and the suspend caught it allocating its TIB, holding the guest
 * heap's lock. The main thread's next allocation waited for that lock while
 * holding the guest lock, and only CRI's next lock, which needs the guest
 * lock, would have resumed the spinner: a black screen before the Sega logo
 * that never ended (known bug 49, GitHub issue 2).
 *
 * CRI's spinner runs natively and reaches no checkpoint, so it carries on
 * through CRI's holds, sleeping 2 ms a turn and counting a counter nothing
 * reads (port/recomp_manual.c). It did that on Linux all along.
 * RECOMP_SUSPEND_REAL=1 stops the thread where it is again, on Windows as on
 * Linux.
 * ============================================================================ */

#if defined(_WIN32)
/* Each thread with a counted suspend outstanding: its id, how many, and
 * whether it is stopped at a checkpoint. A slot is free when tid is 0, and is
 * freed when the count comes back to 0 or the thread ends. */
#define SUSPEND_SLOTS 64
static struct { DWORD tid; LONG count; int parked; } g_suspended[SUSPEND_SLOTS];
static SRWLOCK g_suspended_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE g_suspended_resumed = CONDITION_VARIABLE_INIT;
/* The counts' sum, read without the lock: while it is 0 a checkpoint costs a
 * load. */
static volatile LONG g_suspended_total;

static int suspend_real(void)
{
    static int real = -1;
    if (real < 0) real = getenv("RECOMP_SUSPEND_REAL") ? 1 : 0;
    return real;
}

/* The slot of thread tid (0: a free slot), or -1. With the lock held. */
static int suspended_slot(DWORD tid)
{
    int i;
    for (i = 0; i < SUSPEND_SLOTS; i++)
        if (g_suspended[i].tid == tid) return i;
    return -1;
}

/* Count a suspend of h, with the count before it in *prev. 0 when it is not
 * counted, and SuspendThread takes it: a handle that isn't a thread's (where
 * it fails, as it always did), a thread that has ended (with nothing left to
 * stop, and whose id Windows may give to a new thread), and a full table. */
static int suspend_count_up(HANDLE h, DWORD *prev)
{
    DWORD tid = GetThreadId(h);
    int i;
    if (!tid || WaitForSingleObject(h, 0) == WAIT_OBJECT_0) return 0;
    AcquireSRWLockExclusive(&g_suspended_lock);
    i = suspended_slot(tid);
    if (i < 0) i = suspended_slot(0);
    if (i >= 0) {
        g_suspended[i].tid = tid;
        *prev = (DWORD)g_suspended[i].count++;
        InterlockedIncrement(&g_suspended_total);
    }
    ReleaseSRWLockExclusive(&g_suspended_lock);
    if (i < 0) {
        static int said;
        if (!said++)
            fprintf(stderr, "  [THREAD] %d threads suspended by others at once:"
                            " this one is stopped where it is\n", SUSPEND_SLOTS);
    }
    return i >= 0;
}

/* Take back a counted suspend of h, with the count before it in *prev. 0
 * when h has none, and ResumeThread takes it: a thread that suspended
 * itself. */
static int suspend_count_down(HANDLE h, DWORD *prev)
{
    DWORD tid = GetThreadId(h);
    int i, counted = 0;
    if (!tid) return 0;
    AcquireSRWLockExclusive(&g_suspended_lock);
    i = suspended_slot(tid);
    if (i >= 0 && g_suspended[i].count > 0) {
        counted = 1;
        *prev = (DWORD)g_suspended[i].count--;
        InterlockedDecrement(&g_suspended_total);
        if (g_suspended[i].count == 0) {
            if (!g_suspended[i].parked) g_suspended[i].tid = 0;   /* else it frees it */
            WakeAllConditionVariable(&g_suspended_resumed);
        }
    }
    ReleaseSRWLockExclusive(&g_suspended_lock);
    return counted;
}

static DWORD suspend_thread(HANDLE h)
{
    DWORD prev;
    if (!suspend_real() && !xbox_thread_is_current(h) && suspend_count_up(h, &prev))
        return prev;
    return SuspendThread(h);
}

static DWORD resume_thread(HANDLE h)
{
    DWORD prev;
    if (suspend_count_down(h, &prev)) return prev;
    return ResumeThread(h);
}

int xbox_thread_suspend_pending(void)
{
    DWORD tid;
    int i, pending;
    if (!g_suspended_total) return 0;
    tid = GetCurrentThreadId();
    AcquireSRWLockShared(&g_suspended_lock);
    i = suspended_slot(tid);
    pending = i >= 0 && g_suspended[i].count > 0 && !g_suspended[i].parked;
    ReleaseSRWLockShared(&g_suspended_lock);
    return pending;
}

void xbox_thread_suspend_checkpoint(void)
{
    DWORD tid = GetCurrentThreadId();
    int i;
    AcquireSRWLockExclusive(&g_suspended_lock);
    i = suspended_slot(tid);
    if (i >= 0 && g_suspended[i].count > 0) {
        static int said;
        if (!said++)
            fprintf(stderr, "  [THREAD] thread %lu, suspended by another thread, "
                            "has stopped at a checkpoint (said once)\n",
                    (unsigned long)tid);
        g_suspended[i].parked = 1;
        while (g_suspended[i].count > 0)
            SleepConditionVariableSRW(&g_suspended_resumed, &g_suspended_lock,
                                      INFINITE, 0);
        g_suspended[i].parked = 0;
        g_suspended[i].tid = 0;
    }
    ReleaseSRWLockExclusive(&g_suspended_lock);
}

void xbox_thread_suspend_forget(void)
{
    DWORD tid = GetCurrentThreadId();
    int i;
    AcquireSRWLockExclusive(&g_suspended_lock);
    i = suspended_slot(tid);
    if (i >= 0) {
        InterlockedExchangeAdd(&g_suspended_total, -g_suspended[i].count);
        g_suspended[i].count = 0;
        g_suspended[i].tid = 0;
    }
    ReleaseSRWLockExclusive(&g_suspended_lock);
}
#else
/* win32_compat.c's, which count a suspend of another thread themselves.
 * Weak, as a build without them links. */
extern int  w32_suspend_pending(void)    __attribute__((weak));
extern void w32_suspend_checkpoint(void) __attribute__((weak));
static DWORD suspend_thread(HANDLE h) { return SuspendThread(h); }
static DWORD resume_thread(HANDLE h)  { return ResumeThread(h); }
int  xbox_thread_suspend_pending(void)    { return w32_suspend_pending && w32_suspend_pending(); }
void xbox_thread_suspend_checkpoint(void) { if (w32_suspend_checkpoint) w32_suspend_checkpoint(); }
void xbox_thread_suspend_forget(void)     { }
#endif

NTSTATUS __stdcall xbox_NtSuspendThread(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount)
{
    DWORD prev;

    /*
     * SuspendThread returns the previous suspend count, or (DWORD)-1 on
     * failure. The Xbox call reports that count through an out-parameter, so
     * the two are not interchangeable: -1 must become an error status rather
     * than a suspend count of 0xFFFFFFFF.
     */
    prev = suspend_thread(ThreadHandle);
    if (prev == (DWORD)-1) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "NtSuspendThread: SuspendThread failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    if (PreviousSuspendCount)
        *PreviousSuspendCount = (ULONG)prev;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtSuspendThread: thread=%p previous_count=%u", ThreadHandle, prev);

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtResumeThread(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount)
{
    DWORD prev;

    /* Mirror of NtSuspendThread: (DWORD)-1 is failure, not a count. */
    prev = resume_thread(ThreadHandle);
    if (prev == (DWORD)-1) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "NtResumeThread: ResumeThread failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    if (PreviousSuspendCount)
        *PreviousSuspendCount = (ULONG)prev;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtResumeThread: thread=%p previous_count=%u", ThreadHandle, prev);

    return STATUS_SUCCESS;
}


/* ================================================================
 * Worker stack slices + game-thread tracking (host-tick-driven titles)
 * ================================================================
 *
 * Ported from the Burnout 3 fork as the runtimes reunite. A host-driven title
 * returns from its entry after spawning an init thread and expects the host's
 * own thread to drive the per-frame tick; to call recompiled code from there it
 * needs a guest stack, which a worker slice provides. Additive: a default-model
 * title never calls any of this, so it is inert for Halo, Crimson Skies, etc.
 * See docs/technical/burnout3-reunification.md in sp00nznet/xboxrecomp, and
 * XBOX_WORKER_STACK_* in xbox_memory_layout.h.
 */

/* The game's own thread, kept so a wedged boot can be inspected from the host
 * watchdog. Set when a title's game thread is spawned; NULL under the default
 * inline model, where there is no separate thread to sample. */
static HANDLE g_game_thread = NULL;

void  xbox_set_game_thread(void *h) { g_game_thread = (HANDLE)h; }
void *xbox_thread_debug_handle(void) { return (void *)g_game_thread; }

/* One bit per worker stack slice. Interlocked because a host-driven title can
 * allocate a slice from the host thread while recompiled code allocates one for
 * a spawned worker, so the allocator itself must be thread-safe. */
static volatile LONG g_worker_stack_used[XBOX_WORKER_STACK_COUNT];

int xbox_worker_stack_alloc(void)
{
    int i;
    for (i = 0; i < XBOX_WORKER_STACK_COUNT; i++) {
        if (InterlockedCompareExchange(&g_worker_stack_used[i], 1, 0) == 0)
            return i;
    }
    return -1;  /* all slices in use */
}

void xbox_worker_stack_free(int slot)
{
    if (slot >= 0 && slot < XBOX_WORKER_STACK_COUNT)
        InterlockedExchange(&g_worker_stack_used[slot], 0);
}
