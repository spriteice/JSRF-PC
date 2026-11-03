/* Run with: python tests/file_apc/run.py
 *
 * File APCs, delivered on one thread while another runs as the title.
 *
 * A file request that names an APC routine has it queued on its thread
 * (bridge_queue_apc), and the thread's next alertable wait runs it
 * (bridge_deliver_pending_apcs), as src/kernel/kernel_apc.c does for
 * kernel_bridge.c. The wait is a blocking call, so the thread has given the
 * guest lock up by then, and a second thread stands in for the title, holding
 * the lock most of the time.
 *
 * Every APC's routine is the title's code: it must run holding the guest lock,
 * never beside the title, and the wait must go on without the lock once its
 * APCs have run. Both kinds of routine are here: one in the title's code, and
 * a kernel export, as XAPI's NtUserIoApcDispatcher is. A thread that holds the
 * lock already keeps it, and an APC delivered at once because the ring is full
 * runs holding it too. Before the routines took the lock, they ran without it,
 * beside the title, and the first one fails here. A wait that runs APCs also
 * says it didn't wait, for the guest lock's report (xbox_kernel_call_waited).
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

#define SECONDS       1
#define GUEST_BYTES   (1u << 20)
#define WAIT_STACK    0x00080000u   /* the waiting thread's guest stack top */
#define TITLE_APC_VA  0x00010000u   /* a completion routine in the title */
#define KERNEL_APC_VA (KERNEL_VA_BASE + 0x3Fu * 4)  /* a kernel export's slot */
#define IOSTATUS      0x00001000u
#define RING          32            /* kernel_apc.c's BRIDGE_APC_RING */

/* What the runtime gives a recompiled function: its registers, per thread,
 * and guest memory. */
RECOMP_TLS uint32_t g_esp;
ptrdiff_t g_xbox_mem_offset;
ULONG g_slot_ordinals[XBOX_KERNEL_THUNK_TABLE_SIZE];

/* The guest lock: one thread runs the title's code at a time. */
static CRITICAL_SECTION s_lock;
static RECOMP_TLS int s_holds;
void xbox_guest_lock_acquire(void)
{
    if (s_holds) return;
    EnterCriticalSection(&s_lock);
    s_holds = 1;
}
void xbox_guest_lock_release(void)
{
    if (!s_holds) return;
    s_holds = 0;
    LeaveCriticalSection(&s_lock);
}
int xbox_guest_lock_take(void)
{
    if (s_holds) return 0;
    xbox_guest_lock_acquire();
    return s_holds;
}

/* What the delivery tells the guest lock's report: 1 waited, 0 didn't. */
static RECOMP_TLS int s_waited = -1;
void xbox_kernel_call_waited(int waited) { s_waited = waited; }

/* ---- what the routines check ------------------------------------------- */

static volatile LONG s_failures, s_stop;
static long s_title_inside;    /* written by the title holding the lock */
static long s_title_turns;
static long s_ran;             /* APC routines run, under the lock */
static uint32_t s_last_context;

static void fail(const char *what)
{
    if (__atomic_fetch_add(&s_failures, 1, __ATOMIC_RELAXED) < 5)
        printf("file_apc: %s\n", what);
    fflush(stdout);
}

/* VOID ApcRoutine(PVOID ApcContext, PIO_STATUS_BLOCK, ULONG) */
static void apc_routine(const char *who)
{
    char line[160];
    if (!s_holds) {
        snprintf(line, sizeof line, "%s ran without the guest lock", who);
        fail(line);
    } else if (s_title_inside) {
        snprintf(line, sizeof line, "%s ran beside the title", who);
        fail(line);
    }
    s_last_context = BRIDGE_MEM32(g_esp + 4);
    s_ran++;
}
static void title_completion(void)  { apc_routine("the title's completion routine"); }
static void kernel_dispatcher(void) { apc_routine("the kernel export's APC"); }

recomp_func_t recomp_lookup(uint32_t xbox_va)
{
    return xbox_va == TITLE_APC_VA ? title_completion : NULL;
}
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_kernel(uint32_t xbox_va)
{
    return xbox_va == KERNEL_APC_VA ? kernel_dispatcher : NULL;
}

/* ---- the title ---------------------------------------------------------- */

static DWORD WINAPI title_thread(LPVOID unused)
{
    (void)unused;
    while (!__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
        int i;
        xbox_guest_lock_acquire();
        s_title_inside = 1;
        for (i = 0; i < 200; i++)
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
        s_title_turns++;
        s_title_inside = 0;
        xbox_guest_lock_release();
        Sleep(0);
    }
    __atomic_store_n(&s_stop, 2, __ATOMIC_RELEASE);
    return 0;
}

/* ---- the waiting thread ------------------------------------------------- */

static long ran_now(void)
{
    long n;
    xbox_guest_lock_acquire();
    n = s_ran;
    xbox_guest_lock_release();
    return n;
}

/* An alertable wait, after the dispatcher gave the lock up. */
static void wait_alertably(int queued, const char *what)
{
    char line[160];
    long before = ran_now();
    int n;
    s_waited = -1;
    n = bridge_deliver_pending_apcs();
    if (n != queued || ran_now() - before != queued) {
        snprintf(line, sizeof line, "%s: %d APCs delivered of %d queued", what,
                 n, queued);
        fail(line);
    }
    if (s_waited != 0) {
        snprintf(line, sizeof line, "%s: the wait that ran its APCs didn't say "
                 "it returned without waiting (%d)", what, s_waited);
        fail(line);
    }
    if (s_holds) {
        snprintf(line, sizeof line, "%s: the wait kept the guest lock after "
                 "its APCs", what);
        fail(line);
    }
}

int main(void)
{
    uint8_t *guest = calloc(1, GUEST_BYTES);
    long rounds = 0, start;
    int i;

    if (!guest) return 2;
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)guest;
    g_esp = WAIT_STACK;
    InitializeCriticalSection(&s_lock);
    CloseHandle(CreateThread(NULL, 0, title_thread, NULL, 0, NULL));

    /* A file request queues its APC, and the thread's next alertable wait
     * runs it: the title's own routine, and a kernel export, by turns. */
    start = (long)GetTickCount();
    while ((long)GetTickCount() - start < SECONDS * 1000) {
        bridge_queue_apc(TITLE_APC_VA, 0x1000u + (uint32_t)rounds, IOSTATUS);
        bridge_queue_apc(KERNEL_APC_VA, 0x2000u + (uint32_t)rounds, IOSTATUS);
        wait_alertably(2, "a wait");
        if (g_esp != WAIT_STACK) fail("the APCs moved the waiting thread's stack");
        rounds++;
    }

    /* A thread that holds the lock already keeps it. */
    xbox_guest_lock_acquire();
    bridge_queue_apc(TITLE_APC_VA, 0x3000u, IOSTATUS);
    if (bridge_deliver_pending_apcs() != 1 || s_last_context != 0x3000u)
        fail("an APC delivered holding the lock did not run");
    if (!s_holds) fail("a thread holding the lock lost it to its APC");
    xbox_guest_lock_release();

    /* A full ring delivers the next APC at once, inside the file request,
     * which gave the lock up too. */
    for (i = 0; i < RING; i++)
        bridge_queue_apc(TITLE_APC_VA, 0x4000u + (uint32_t)i, IOSTATUS);
    {
        long before = ran_now();
        bridge_queue_apc(TITLE_APC_VA, 0x5000u, IOSTATUS);
        if (ran_now() - before != 1) fail("a full ring's APC did not run at once");
        if (s_holds) fail("a full ring's APC left the guest lock held");
    }
    wait_alertably(RING, "a full ring's wait");

    __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE) < 2)
        Sleep(1);

    printf("file_apc: %ld rounds of 2 APCs, %ld run in all, the title %ld turns\n",
           rounds, s_ran, s_title_turns);
    if (rounds < 100 || s_title_turns < 100)
        fail("too few rounds to test anything");
    if (s_failures) {
        printf("file_apc: %ld failures\n", (long)s_failures);
        fflush(stdout);
        _exit(1);
    }
    printf("file_apc: all cases pass\n");
    fflush(stdout);
    _exit(0);
}
