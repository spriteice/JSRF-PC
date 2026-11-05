/*
 * kernel_sync.c - Xbox Synchronization Primitives
 *
 * Implements events, semaphores, wait functions, kernel timers, and DPCs
 * using Win32 synchronization objects.
 *
 * Xbox synchronization model:
 *   - Events: notification (manual-reset) or synchronization (auto-reset)
 *   - Semaphores: standard counting semaphores
 *   - Wait functions: single and multiple, with optional timeout
 *   - Timers: kernel timers with optional DPC callback on expiry
 *   - DPCs: deferred procedure calls, executed via thread pool on Windows
 *
 * Time values use NT 100-nanosecond units (negative = relative).
 */

#include "kernel.h"
#include <string.h>

/* ============================================================================
 * Helper: Convert NT 100ns interval to Win32 milliseconds
 *
 * NT time intervals:
 *   - Negative = relative (most common), in 100ns units
 *   - Positive = absolute FILETIME
 *   - NULL = infinite wait
 * ============================================================================ */

static DWORD xbox_nt_timeout_to_ms(PLARGE_INTEGER Timeout)
{
    if (!Timeout)
        return INFINITE;

    if (Timeout->QuadPart == 0)
        return 0;

    if (Timeout->QuadPart < 0) {
        /* Relative: negative 100ns units */
        LONGLONG relative_100ns = -Timeout->QuadPart;
        DWORD ms = (DWORD)(relative_100ns / 10000);
        if (ms == 0 && relative_100ns > 0)
            ms = 1;
        return ms;
    }

    /* Absolute: compute delta from now */
    LARGE_INTEGER now;
    GetSystemTimeAsFileTime((LPFILETIME)&now);
    LONGLONG diff = Timeout->QuadPart - now.QuadPart;
    if (diff <= 0)
        return 0;
    return (DWORD)(diff / 10000);
}

/* ============================================================================
 * Events
 *
 * Xbox event types:
 *   XboxNotificationEvent (0) = manual-reset (Win32: TRUE)
 *   XboxSynchronizationEvent (1) = auto-reset (Win32: FALSE)
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtCreateEvent(
    PHANDLE EventHandle,
    PXBOX_OBJECT_ATTRIBUTES ObjectAttributes,
    ULONG EventType,
    BOOLEAN InitialState)
{
    HANDLE hEvent;
    BOOL bManualReset;

    (void)ObjectAttributes;

    if (!EventHandle)
        return STATUS_INVALID_PARAMETER;

    /* XboxNotificationEvent = manual-reset, XboxSynchronizationEvent = auto-reset */
    bManualReset = (EventType == XboxNotificationEvent) ? TRUE : FALSE;

    hEvent = CreateEventW(NULL, bManualReset, InitialState, NULL);
    if (!hEvent) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_SYNC,
            "NtCreateEvent: CreateEventW failed (error %u)", GetLastError());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *EventHandle = hEvent;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_SYNC,
        "NtCreateEvent: handle=%p, type=%s, initial=%d",
        hEvent, bManualReset ? "notification" : "synchronization", InitialState);

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtSetEvent(HANDLE EventHandle, PLONG PreviousState)
{
    if (PreviousState)
        *PreviousState = 0; /* We don't track previous state */

    if (!SetEvent(EventHandle)) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_SYNC,
            "NtSetEvent: SetEvent failed for handle %p (error %u)",
            EventHandle, GetLastError());
        return STATUS_INVALID_HANDLE;
    }

    return STATUS_SUCCESS;
}

/*
 * KeSetEvent - kernel-mode event signal.
 * On Xbox, this is the kernel-mode equivalent of NtSetEvent.
 * The Object parameter is treated as a Win32 event HANDLE.
 * Returns the previous signal state.
 */
LONG __stdcall xbox_KeSetEvent(PVOID Event, LONG Increment, BOOLEAN Wait)
{
    HANDLE hEvent = (HANDLE)Event;

    (void)Increment;
    (void)Wait;

    /* We can't easily query previous state, so just set and return 0 */
    SetEvent(hEvent);
    return 0;
}

/* ============================================================================
 * Semaphores
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtCreateSemaphore(
    PHANDLE SemaphoreHandle,
    PXBOX_OBJECT_ATTRIBUTES ObjectAttributes,
    LONG InitialCount,
    LONG MaximumCount)
{
    HANDLE hSemaphore;

    (void)ObjectAttributes;

    if (!SemaphoreHandle)
        return STATUS_INVALID_PARAMETER;

    hSemaphore = CreateSemaphoreW(NULL, InitialCount, MaximumCount, NULL);
    if (!hSemaphore) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_SYNC,
            "NtCreateSemaphore: CreateSemaphoreW failed (error %u)", GetLastError());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *SemaphoreHandle = hSemaphore;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_SYNC,
        "NtCreateSemaphore: handle=%p, initial=%d, max=%d",
        hSemaphore, InitialCount, MaximumCount);

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtReleaseSemaphore(
    HANDLE SemaphoreHandle,
    LONG ReleaseCount,
    PLONG PreviousCount)
{
    if (!ReleaseSemaphore(SemaphoreHandle, ReleaseCount, PreviousCount)) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_SYNC,
            "NtReleaseSemaphore: failed for handle %p (error %u)",
            SemaphoreHandle, GetLastError());
        return STATUS_INVALID_HANDLE;
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
 * Wait Functions
 * ============================================================================ */

/*
 * Map Win32 WaitFor* return codes to NTSTATUS.
 */
static NTSTATUS xbox_wait_result_to_ntstatus(DWORD result, ULONG count)
{
    if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count)
        return (NTSTATUS)(STATUS_SUCCESS + (result - WAIT_OBJECT_0));

    switch (result) {
        case WAIT_TIMEOUT:          return STATUS_TIMEOUT;
        case WAIT_IO_COMPLETION:    return STATUS_ALERTED;
        case WAIT_ABANDONED_0:      return STATUS_ABANDONED;
        case WAIT_FAILED:           return STATUS_UNSUCCESSFUL;
        default:                    return STATUS_UNSUCCESSFUL;
    }
}

/*
 * One host wait: on Handles, Count of them, or on Handle alone when Handles
 * is NULL. The two multiple-object calls pass their array even for one
 * object, so each call keeps the host wait it always made.
 */
static DWORD sync_host_wait(HANDLE Handle, DWORD Count, const HANDLE *Handles,
                            BOOL WaitAll, DWORD ms, BOOL Alertable)
{
    if (Handles)
        return WaitForMultipleObjectsEx(Count, Handles, WaitAll, ms, Alertable);
    return WaitForSingleObjectEx(Handle, ms, Alertable);
}

/*
 * A wait, saying whether it had to (xbox_kernel_call_waited), for the guest
 * lock's report of who takes the lock over at a call. A zero timeout is a
 * look that never waits. With the report on, a longer wait also looks first,
 * with a zero timeout, and waits only when nothing was ready: an object the
 * look finds ready is taken by it, as the wait would have taken it, and an
 * APC it runs ends it as it would have ended the wait. With the report off,
 * each call makes the one host wait it always made.
 */
static DWORD sync_wait(HANDLE Handle, DWORD Count, const HANDLE *Handles,
                       BOOL WaitAll, DWORD ms, BOOL Alertable)
{
    if (ms == 0 || xbox_kernel_call_watched()) {
        DWORD result = sync_host_wait(Handle, Count, Handles, WaitAll, 0, Alertable);
        if (result != WAIT_TIMEOUT || ms == 0) {
            xbox_kernel_call_waited(0);
            return result;
        }
        xbox_kernel_call_waited(1);
    }
    return sync_host_wait(Handle, Count, Handles, WaitAll, ms, Alertable);
}

NTSTATUS __stdcall xbox_NtWaitForSingleObject(
    HANDLE Handle,
    BOOLEAN Alertable,
    PLARGE_INTEGER Timeout)
{
    DWORD ms = xbox_nt_timeout_to_ms(Timeout);
    DWORD result = sync_wait(Handle, 1, NULL, FALSE, ms, Alertable);
    return xbox_wait_result_to_ntstatus(result, 1);
}

NTSTATUS __stdcall xbox_NtWaitForSingleObjectEx(
    HANDLE Handle,
    KPROCESSOR_MODE WaitMode,
    BOOLEAN Alertable,
    PLARGE_INTEGER Timeout)
{
    DWORD ms = xbox_nt_timeout_to_ms(Timeout);
    DWORD result;

    /*
     * Same as NtWaitForSingleObject but with an explicit wait mode. We run
     * everything in one host process with no kernel/user split, so WaitMode
     * has nothing to select and is ignored.
     */
    (void)WaitMode;

    result = sync_wait(Handle, 1, NULL, FALSE, ms, Alertable);
    return xbox_wait_result_to_ntstatus(result, 1);
}

NTSTATUS __stdcall xbox_NtWaitForMultipleObjectsEx(
    ULONG Count,
    HANDLE Handles[],
    ULONG WaitType,
    BOOLEAN Alertable,
    PLARGE_INTEGER Timeout)
{
    DWORD ms = xbox_nt_timeout_to_ms(Timeout);
    BOOL bWaitAll;
    DWORD result;

    /* WaitType: 0 = WaitAll, 1 = WaitAny (matches NT definitions) */
    bWaitAll = (WaitType == 0) ? TRUE : FALSE;

    result = sync_wait(NULL, Count, Handles, bWaitAll, ms, Alertable);
    return xbox_wait_result_to_ntstatus(result, Count);
}

/*
 * KeWaitForSingleObject - kernel-mode wait on a dispatcher object.
 * On Xbox, Objects can be events, timers, threads, etc.
 * We treat the object pointer as a Win32 HANDLE.
 */
NTSTATUS __stdcall xbox_KeWaitForSingleObject(
    PVOID Object,
    ULONG WaitReason,
    KPROCESSOR_MODE WaitMode,
    BOOLEAN Alertable,
    PLARGE_INTEGER Timeout)
{
    (void)WaitReason;
    (void)WaitMode;

    HANDLE hObject = (HANDLE)Object;
    DWORD ms = xbox_nt_timeout_to_ms(Timeout);
    DWORD result = sync_wait(hObject, 1, NULL, FALSE, ms, Alertable);
    return xbox_wait_result_to_ntstatus(result, 1);
}

NTSTATUS __stdcall xbox_KeWaitForMultipleObjects(
    ULONG Count,
    PVOID Objects[],
    ULONG WaitType,
    ULONG WaitReason,
    KPROCESSOR_MODE WaitMode,
    BOOLEAN Alertable,
    PLARGE_INTEGER Timeout,
    PVOID WaitBlockArray)
{
    (void)WaitReason;
    (void)WaitMode;
    (void)WaitBlockArray;

    BOOL bWaitAll = (WaitType == 0) ? TRUE : FALSE;
    DWORD ms = xbox_nt_timeout_to_ms(Timeout);

    /* Objects[] is an array of PVOID which we treat as HANDLE[] */
    DWORD result = sync_wait(NULL, Count, (HANDLE*)Objects,
                             bWaitAll, ms, Alertable);
    return xbox_wait_result_to_ntstatus(result, Count);
}

/* ============================================================================
 * Kernel Timers
 *
 * Xbox kernel timers are dispatcher objects that can be waited on and
 * optionally queue a DPC when they expire. The game's are kernel_timer.c's:
 * the bridge's KeInitializeTimerEx, KeSetTimer and KeSetTimerEx are there.
 * The bridge's KeCancelTimer calls kernel_timer.c's cancel and then this.
 * ============================================================================ */

/* Nothing here creates a timer queue now, so this stays NULL, which is Win32's
 * default queue. */
static HANDLE g_timer_queue = NULL;

BOOLEAN __stdcall xbox_KeCancelTimer(PXBOX_KTIMER Timer)
{
    BOOLEAN was_inserted;

    if (!Timer)
        return FALSE;

    was_inserted = Timer->Inserted;

    if (was_inserted && Timer->win32_timer) {
        DeleteTimerQueueTimer(g_timer_queue, Timer->win32_timer, INVALID_HANDLE_VALUE);
        Timer->win32_timer = NULL;
        Timer->Inserted = FALSE;
    }

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_SYNC,
        "KeCancelTimer: timer=%p, was_inserted=%d", Timer, was_inserted);

    return was_inserted;
}

/* ============================================================================
 * Events (continued)
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtClearEvent(HANDLE EventHandle)
{
    if (!ResetEvent(EventHandle)) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_SYNC,
            "NtClearEvent: ResetEvent failed (error %u)", GetLastError());
        return STATUS_INVALID_HANDLE;
    }
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Mutants (mutexes)
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtCreateMutant(
    PHANDLE MutantHandle,
    PXBOX_OBJECT_ATTRIBUTES ObjectAttributes,
    BOOLEAN InitialOwner)
{
    HANDLE hMutex;

    (void)ObjectAttributes;

    if (!MutantHandle)
        return STATUS_INVALID_PARAMETER;

    hMutex = CreateMutexW(NULL, InitialOwner, NULL);
    if (!hMutex) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_SYNC,
            "NtCreateMutant: CreateMutexW failed (error %u)", GetLastError());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *MutantHandle = hMutex;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_SYNC,
        "NtCreateMutant: handle=%p initial_owner=%d", hMutex, InitialOwner);

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtReleaseMutant(HANDLE MutantHandle, PLONG PreviousCount)
{
    /*
     * ReleaseMutex reports the previous count only through its own bookkeeping,
     * which Win32 does not expose. Callers overwhelmingly use PreviousCount as
     * an ignore-me out-param; report 0 (the count before this release took it
     * to unheld) rather than leaving the caller's storage uninitialised.
     */
    if (!ReleaseMutex(MutantHandle)) {
        DWORD err = GetLastError();
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_SYNC,
            "NtReleaseMutant: ReleaseMutex failed (error %u)", err);
        /* Releasing a mutex this thread does not own is the common failure. */
        return (err == ERROR_NOT_OWNER) ? STATUS_MUTANT_NOT_OWNED
                                        : STATUS_INVALID_HANDLE;
    }

    if (PreviousCount)
        *PreviousCount = 0;

    return STATUS_SUCCESS;
}
