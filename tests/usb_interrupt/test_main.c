/* Run with: python tests/usb_interrupt/run.py
 *
 * The USB controller's interrupt, delivered beside the display's, as in the
 * first second of a start (known bug 16).
 *
 * src/usb/ohci.c's controller thread raises its start-of-frame interrupt
 * every tick, and a timer thread raises the display's interrupt and drains
 * the DPCs once a millisecond, as the kernel's does
 * (src/kernel/kernel_timer.c), both through src/kernel/kernel_interrupt.c.
 * A third thread stands in for the title, holding the guest lock most of
 * the time. Each interrupt routine queues a DPC with
 * KeInsertQueueDpc, as the game's do.
 *
 * Every routine, interrupt or DPC, must run holding the guest lock and marked
 * as an interrupt, with no other routine running at the same time, and every
 * DPC queued must run once, with its own arguments. Before the USB interrupt
 * went through the kernel, its routine ran on the controller's thread without
 * the lock, beside the title and the timer thread, and the first one fails
 * here. A DPC overwritten in the queue by the other thread's shows as one
 * that never ran, or ran with the other's arguments.
 *
 * Last, KeDisconnectInterrupt must take the display's object off its vector,
 * so that its routine is no longer called, say it was connected, and leave
 * the game's 44 bytes as they were: a host function took them for a host
 * struct, cleared a byte of them, and left the routine connected
 * (code/pass2's Q). And KeInitializeInterrupt and KeInitializeDpc given an
 * object at guest address 0 each say so once ([CHECK]) and write nothing:
 * memset was handed NULL, and the test stopped there.
 *
 * And the rest of the file's calls, as they are today (b3 row 9e): each
 * vector HalGetInterruptVector gives is the level the device models raise,
 * with the IRQL written in the game's byte; KeSynchronizeExecution runs its
 * routine once, holding the guest lock as its caller does, and answers what
 * the routine returned; and a DPC, interrupt or synchronize routine that is
 * not in dispatch is never called, and the call says so: 0, -1 or FALSE,
 * with a line for the DPC's and KeSynchronizeExecution's, which run.py
 * counts.
 */
#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>   /* _exit */
#endif
#ifdef TEST_COVERAGE
/* GCC's, from libgcov: writes the line counts now. tests/run_all.py
 * --coverage defines TEST_COVERAGE, and links libgcov. */
void __gcov_dump(void);
#endif
#include "xbox_memory_layout.h"
#include "kernel_bridge.h"
#include "ohci.h"
#include "usb_gamepad.h"

#define ENOUGH         40            /* of each interrupt, to test anything */
#define MAX_MS         30000         /* however busy the machine */
#define POLL_MS        10
#define TIMER_MS       1             /* the timer thread's sleep, as kernel_timer.c's */
#define GUEST_BYTES    (16u << 20)   /* past the worker stacks' end */

/* The game's side, in guest memory and in the dispatch table. */
#define USB_INTERRUPT    0x00001000u   /* KINTERRUPTs, 44 bytes */
#define VBLANK_INTERRUPT 0x00001040u
#define USB_DPC          0x00001080u   /* KDPCs, 32 bytes */
#define VBLANK_DPC       0x000010C0u
#define HCCA             0x00002000u   /* 256 bytes, 256-aligned */
#define CONTROLLER_TIB   0x00003000u
#define USB_ISR_VA       0x00010000u   /* the routines' guest addresses */
#define VBLANK_ISR_VA    0x00010010u
#define USB_DPC_VA       0x00010020u
#define VBLANK_DPC_VA    0x00010030u
#define SYNC_VA          0x00010040u   /* KeSynchronizeExecution's routine */
#define LOST_DPC_VA      0x00010050u   /* routines not in dispatch */
#define LOST_ISR_VA      0x00010060u
#define LOST_SYNC_VA     0x00010070u
#define LOST_DPC         0x00001100u   /* their KDPC and KINTERRUPT */
#define LOST_INTERRUPT   0x00001140u
#define LOST_VECTOR      5u            /* a vector no device model raises */
#define IRQLS            0x00001180u   /* 8 bytes, the game's IRQL bytes among them */
#define USB_VECTOR       1u            /* ohci.c's OHCI_VECTOR */
#define VBLANK_VECTOR    3u            /* kernel_bridge.c's NV2A_VECTOR */

/* OHCI 1.0a's registers and bits, as the game's driver writes them. */
#define HC_CONTROL           0x04u
#define HC_INTERRUPT_STATUS  0x0Cu
#define HC_INTERRUPT_ENABLE  0x10u
#define HC_HCCA              0x18u
#define HC_OPERATIONAL       0x80u     /* HcControl.HCFS = 10b */
#define INTR_SF              0x04u     /* start of frame */
#define INTR_MIE             0x80000000u

/* What the runtime gives a recompiled function: its registers, per thread,
 * and guest memory. */
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_fs_base;
ptrdiff_t g_xbox_mem_offset;
static uint8_t *s_guest;

ptrdiff_t xbox_GetMemoryOffset(void) { return g_xbox_mem_offset; }
size_t xbox_GetMappedSize(void) { return GUEST_BYTES; }
uint32_t xbox_AllocThreadTib(void) { return CONTROLLER_TIB; }

/* The worker stack slices, as xbox_memory_layout.c hands them out. */
static volatile LONG s_slots;
int xbox_worker_stack_alloc(void)
{
    int i;
    for (i = 0; i < XBOX_WORKER_STACK_COUNT; i++) {
        LONG bit = 1L << i;
        if (!(__atomic_fetch_or(&s_slots, bit, __ATOMIC_ACQ_REL) & bit))
            return i;
    }
    return -1;
}
void xbox_worker_stack_free(int slot)
{
    __atomic_fetch_and(&s_slots, ~(1L << slot), __ATOMIC_RELEASE);
}

/* The guest lock: one thread runs the title's code at a time. How many
 * threads are queueing for it, and every take, for the title's hand-over
 * (title_thread). */
static CRITICAL_SECTION s_lock;
static RECOMP_TLS int s_holds;
static volatile LONG s_queueing, s_takes;
void xbox_guest_lock_acquire(void)
{
    if (s_holds) return;
    __atomic_add_fetch(&s_queueing, 1, __ATOMIC_ACQ_REL);
    EnterCriticalSection(&s_lock);
    __atomic_sub_fetch(&s_queueing, 1, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&s_takes, 1, __ATOMIC_RELEASE);
    s_holds = 1;
}
void xbox_guest_lock_release(void)
{
    if (!s_holds) return;
    s_holds = 0;
    LeaveCriticalSection(&s_lock);
}

/* No pad on the port: the controller is only asked for its frame interrupt. */
int usb_gamepad_control(const UsbSetup *setup, uint8_t *out, int max)
{
    (void)setup; (void)out; (void)max;
    return -1;
}
int usb_gamepad_report(uint8_t *out, int max) { (void)out; (void)max; return 0; }

/* ---- what the routines check ------------------------------------------- */

static volatile LONG s_inside, s_failures, s_stop;
static volatile LONG s_usb_raised, s_vblank_raised;
static volatile LONG s_usb_queued, s_vblank_queued, s_usb_run, s_vblank_run;

static void fail(const char *what)
{
    if (__atomic_fetch_add(&s_failures, 1, __ATOMIC_RELAXED) < 5)
        printf("usb_interrupt: %s\n", what);
    fflush(stdout);
}

static void enter(const char *who)
{
    char line[160];
    if (!s_holds) {
        snprintf(line, sizeof line, "%s ran without the guest lock", who);
        fail(line);
    }
    if (!t_in_callback) {
        snprintf(line, sizeof line, "%s ran unmarked as an interrupt", who);
        fail(line);
    }
    if (__atomic_add_fetch(&s_inside, 1, __ATOMIC_ACQ_REL) != 1) {
        snprintf(line, sizeof line, "%s ran beside another routine", who);
        fail(line);
    }
}

static void leave(void)
{
    __atomic_sub_fetch(&s_inside, 1, __ATOMIC_ACQ_REL);
}

/* A routine's work, long enough for another thread to arrive in it. */
static void work(void)
{
    volatile unsigned i;
    for (i = 0; i < 20000; i++) { }
}

/* KeInsertQueueDpc(Dpc, SystemArgument1, SystemArgument2), called as the
 * dispatcher calls a bridge: its arguments on the guest stack. */
static void queue_dpc(uint32_t dpc, uint32_t arg1, volatile LONG *queued)
{
    uint32_t esp = g_esp;
    g_esp -= 12;
    BRIDGE_MEM32(g_esp + 0) = dpc;
    BRIDGE_MEM32(g_esp + 4) = arg1;
    BRIDGE_MEM32(g_esp + 8) = arg1 + 1;
    bridge_KeInsertQueueDpc();
    if (g_eax == 1)
        __atomic_add_fetch(queued, 1, __ATOMIC_RELAXED);
    g_esp = esp;
}

/* BOOLEAN ServiceRoutine(PKINTERRUPT, PVOID), stdcall: its `ret 8` takes the
 * return address and both arguments. */
static void usb_isr(void)
{
    enter("the USB interrupt's routine");
    work();
    xbox_OhciWrite(XBOX_OHCI0_BASE + HC_INTERRUPT_STATUS, INTR_SF, 4);
    queue_dpc(USB_DPC, 0x11, &s_usb_queued);
    __atomic_add_fetch(&s_usb_raised, 1, __ATOMIC_RELAXED);
    leave();
    g_eax = 1;
    g_esp += 12;
}

static void vblank_isr(void)
{
    enter("the display interrupt's routine");
    work();
    queue_dpc(VBLANK_DPC, 0x21, &s_vblank_queued);
    __atomic_add_fetch(&s_vblank_raised, 1, __ATOMIC_RELAXED);
    leave();
    g_eax = 1;
    g_esp += 12;
}

/* VOID DeferredRoutine(PKDPC, PVOID Context, PVOID Arg1, PVOID Arg2),
 * stdcall: its `ret 16` takes the return address and all four. */
static void dpc_routine(uint32_t dpc, uint32_t arg1, volatile LONG *run, const char *who)
{
    uint32_t got_dpc  = BRIDGE_MEM32(g_esp + 4);
    uint32_t got_arg1 = BRIDGE_MEM32(g_esp + 12);
    uint32_t got_arg2 = BRIDGE_MEM32(g_esp + 16);
    enter(who);
    if (got_dpc != dpc || got_arg1 != arg1 || got_arg2 != arg1 + 1)
        fail("a DPC ran with another DPC's arguments: its slot was overwritten");
    __atomic_add_fetch(run, 1, __ATOMIC_RELAXED);
    leave();
    g_esp += 20;
}
static void usb_dpc(void)    { dpc_routine(USB_DPC, 0x11, &s_usb_run, "the USB interrupt's DPC"); }
static void vblank_dpc(void) { dpc_routine(VBLANK_DPC, 0x21, &s_vblank_run, "the display interrupt's DPC"); }

/* BOOLEAN SynchronizeRoutine(PVOID Context), stdcall: its `ret 4` takes the
 * return address and the context. It answers s_sync_answer. */
static volatile LONG s_sync_runs;
static uint32_t s_sync_context, s_sync_answer;
static void sync_routine(void)
{
    enter("KeSynchronizeExecution's routine");
    s_sync_context = BRIDGE_MEM32(g_esp + 4);
    __atomic_add_fetch(&s_sync_runs, 1, __ATOMIC_RELAXED);
    leave();
    g_eax = s_sync_answer;
    g_esp += 8;
}

recomp_func_t recomp_lookup(uint32_t va)
{
    switch (va) {
    case USB_ISR_VA:    return usb_isr;
    case VBLANK_ISR_VA: return vblank_isr;
    case USB_DPC_VA:    return usb_dpc;
    case VBLANK_DPC_VA: return vblank_dpc;
    case SYNC_VA:       return sync_routine;
    default:            return NULL;
    }
}
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }

/* ---- the threads ------------------------------------------------------- */

/* The title: holds the guest lock most of the time, as its main thread does,
 * and hands it over to a thread queueing for it, as the engine's guest lock
 * does (guest_lock_hand_over, kernel_bridge.c): it stands back until a take
 * is seen, for a millisecond at most. The lock isn't FIFO, and the thread
 * that drops it wins it back almost every time: the waiter it wakes is
 * still being scheduled. Taking it straight back, the title left the
 * controller's thread and the timer thread so little that GitHub's run 124,
 * the build for coverage, saw 26 USB and 29 display interrupts in the 30 s
 * the test allows. */
static DWORD WINAPI title_thread(LPVOID unused)
{
    LARGE_INTEGER freq, t0, t;
    (void)unused;
    QueryPerformanceFrequency(&freq);
    while (!__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
        LONG taken;
        xbox_guest_lock_acquire();
        work();
        taken = __atomic_load_n(&s_takes, __ATOMIC_RELAXED);
        xbox_guest_lock_release();
        if (!__atomic_load_n(&s_queueing, __ATOMIC_ACQUIRE)) {
            Sleep(0);
            continue;
        }
        QueryPerformanceCounter(&t0);
        do {
            SwitchToThread();
            QueryPerformanceCounter(&t);
        } while (__atomic_load_n(&s_takes, __ATOMIC_ACQUIRE) == taken
                 && t.QuadPart - t0.QuadPart < freq.QuadPart / 1000);
    }
    __atomic_add_fetch(&s_stop, 1, __ATOMIC_RELEASE);
    return 0;
}

/* The timer thread: the display's interrupt, then the DPCs, every turn,
 * with a millisecond's sleep between turns, as the kernel's timer thread
 * has (kernel_timer.c). Turning again at once, it took the guest lock back
 * to back with the title, and on a busy machine the controller's thread,
 * waking once a tick, seldom got it: GitHub's run 72 saw 39456 display
 * interrupts and 36 USB ones in the 30 s the test allows. */
static DWORD WINAPI timer_thread(LPVOID unused)
{
    int slot = xbox_worker_stack_alloc();
    (void)unused;
    g_esp = XBOX_WORKER_STACK_TOP(slot);
    while (!__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
        kernel_raise_interrupt(VBLANK_VECTOR);
        kernel_drain_dpcs();
        Sleep(TIMER_MS);
    }
    __atomic_add_fetch(&s_stop, 1, __ATOMIC_RELEASE);
    return 0;
}

/* A bridge called from here, as the game's code would. */
static void call_bridge(void (*bridge)(void), const uint32_t *args, int n)
{
    int i;
    g_esp -= (uint32_t)n * 4;
    for (i = 0; i < n; i++) BRIDGE_MEM32(g_esp + (uint32_t)i * 4) = args[i];
    bridge();
    g_esp += (uint32_t)n * 4;
}

/* ULONG HalGetInterruptVector(ULONG BusInterruptLevel, PKIRQL Irql), as the
 * game asks before it makes each interrupt object: the vector is the level,
 * which is what the device models raise (ohci.c's OHCI_VECTOR, and
 * kernel_bridge.c's NV2A_VECTOR), and the IRQL, the level too, is written in
 * the one byte the game gave; given no byte, it writes nothing. */
static void the_vectors_are_the_levels(void)
{
    const uint32_t usb[] = { USB_VECTOR, IRQLS + 2 }, vblank[] = { VBLANK_VECTOR, IRQLS + 5 };
    const uint32_t no_irql[] = { VBLANK_VECTOR, 0 };
    uint32_t usb_vector, vblank_vector, nothing_vector;
    uint8_t want[8], at_zero;
    int i;

    for (i = 0; i < 8; i++) {
        BRIDGE_MEM8(IRQLS + (uint32_t)i) = 0xEE;
        want[i] = 0xEE;
    }
    want[2] = USB_VECTOR;
    want[5] = VBLANK_VECTOR;
    call_bridge(bridge_HalGetInterruptVector, usb, 2);
    usb_vector = g_eax;
    call_bridge(bridge_HalGetInterruptVector, vblank, 2);
    vblank_vector = g_eax;
    at_zero = s_guest[0];
    call_bridge(bridge_HalGetInterruptVector, no_irql, 2);
    nothing_vector = g_eax;
    printf("usb_interrupt: HalGetInterruptVector gave vectors %u and %u for levels "
           "%u and %u, IRQLs %u and %u\n", usb_vector, vblank_vector, USB_VECTOR,
           VBLANK_VECTOR, BRIDGE_MEM8(IRQLS + 2), BRIDGE_MEM8(IRQLS + 5));
    if (usb_vector != USB_VECTOR || vblank_vector != VBLANK_VECTOR || nothing_vector != VBLANK_VECTOR)
        fail("HalGetInterruptVector gave a vector the device models don't raise");
    if (memcmp(want, XBOX_TO_NATIVE(IRQLS), sizeof want))
        fail("HalGetInterruptVector didn't write the IRQL in the game's byte alone");
    if (s_guest[0] != at_zero)
        fail("HalGetInterruptVector given no IRQL byte wrote at guest address 0");
}

/* BOOLEAN KeSynchronizeExecution(Interrupt, SynchronizeRoutine, Context),
 * called holding the guest lock, as every caller is (a bridge, on a thread
 * running the title's code): the routine runs once, on this thread, holding
 * it, marked as an interrupt and beside no other routine, with its context,
 * and what it answers is the call's answer. */
static void a_routine_synchronises(void)
{
    uint32_t answer;

    for (answer = 0; answer < 2; answer++) {
        const uint32_t args[] = { USB_INTERRUPT, SYNC_VA, 0x5100u + answer };
        uint32_t esp = g_esp;
        LONG runs = s_sync_runs;

        s_sync_answer = answer;
        g_eax = 0xDEADu;
        call_bridge(bridge_KeSynchronizeExecution, args, 3);
        printf("usb_interrupt: KeSynchronizeExecution ran its routine %ld times, "
               "with context 0x%X, and said %u for its %u\n",
               (long)(s_sync_runs - runs), s_sync_context, g_eax, answer);
        if (s_sync_runs - runs != 1)
            fail("KeSynchronizeExecution did not run its routine once");
        if (s_sync_context != 0x5100u + answer)
            fail("KeSynchronizeExecution's routine was not given its context");
        if (g_eax != answer)
            fail("KeSynchronizeExecution did not answer what its routine returned");
        if (g_esp != esp)
            fail("KeSynchronizeExecution moved the caller's stack");
    }
}

/* Routines not in dispatch, a lifted function missing: none is called, and
 * the stack is as it was. An interrupt's: kernel_raise_interrupt_locked says
 * -1. KeSynchronizeExecution's: it says FALSE, with a line. A DPC's:
 * kernel_run_dpc says 0, with a line. kernel_run_dpc takes the guest lock
 * and gives it back, as the timer thread's drain calls it, so the lock is
 * given up first. run.py counts the lines. */
static void routines_not_in_dispatch(void)
{
    const uint32_t interrupt[] = { LOST_INTERRUPT, LOST_ISR_VA, 0, LOST_VECTOR };
    const uint32_t connect[] = { LOST_INTERRUPT };
    const uint32_t sync[] = { USB_INTERRUPT, LOST_SYNC_VA, 0 };
    const uint32_t dpc[] = { LOST_DPC, LOST_DPC_VA, 0 };
    uint32_t esp = g_esp;
    int raised, ran;

    call_bridge(bridge_KeInitializeInterrupt, interrupt, 4);
    call_bridge(bridge_KeConnectInterrupt, connect, 1);
    raised = kernel_raise_interrupt_locked(LOST_VECTOR);
    if (raised != -1 || g_esp != esp)
        fail("an interrupt whose routine is not in dispatch was delivered");
    g_eax = 0xDEADu;
    call_bridge(bridge_KeSynchronizeExecution, sync, 3);
    if (g_eax != 0 || g_esp != esp)
        fail("KeSynchronizeExecution with a routine not in dispatch did not say FALSE");
    call_bridge(bridge_KeInitializeDpc, dpc, 3);
    xbox_guest_lock_release();
    ran = kernel_run_dpc(LOST_DPC, 0x31, 0x32);
    if (ran != 0 || g_esp != esp)
        fail("a DPC whose routine is not in dispatch was run");
    printf("usb_interrupt: routines not in dispatch: the interrupt's said %d, "
           "the DPC's %d\n", raised, ran);
}

int main(void)
{
    const uint32_t usb_int[]    = { USB_INTERRUPT, USB_ISR_VA, 0, USB_VECTOR };
    const uint32_t vblank_int[] = { VBLANK_INTERRUPT, VBLANK_ISR_VA, 0, VBLANK_VECTOR };
    const uint32_t usb_dpc_init[]    = { USB_DPC, USB_DPC_VA, 0 };
    const uint32_t vblank_dpc_init[] = { VBLANK_DPC, VBLANK_DPC_VA, 0 };
    const uint32_t connect_usb[] = { USB_INTERRUPT }, connect_vblank[] = { VBLANK_INTERRUPT };
    unsigned long queued, run;
    long waiting;
    int slot, waited;

    s_guest = calloc(1, GUEST_BYTES);
    if (!s_guest) return 2;
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)s_guest;
    InitializeCriticalSection(&s_lock);
    slot = xbox_worker_stack_alloc();
    g_esp = XBOX_WORKER_STACK_TOP(slot);

    /* The game's set-up: its vectors asked for, its interrupts connected,
     * its DPCs made. */
    the_vectors_are_the_levels();
    call_bridge(bridge_KeInitializeInterrupt, usb_int, 4);
    call_bridge(bridge_KeInitializeInterrupt, vblank_int, 4);
    call_bridge(bridge_KeInitializeDpc, usb_dpc_init, 3);
    call_bridge(bridge_KeInitializeDpc, vblank_dpc_init, 3);
    call_bridge(bridge_KeConnectInterrupt, connect_usb, 1);
    call_bridge(bridge_KeConnectInterrupt, connect_vblank, 1);

    /* The driver starts the controller with its frame interrupt on. The
     * controller's thread waits a tick before it first looks, and the
     * registers are written under the guest lock, as the driver does. */
    xbox_OhciInit();
    xbox_guest_lock_acquire();
    xbox_OhciWrite(XBOX_OHCI0_BASE + HC_HCCA, HCCA, 4);
    xbox_OhciWrite(XBOX_OHCI0_BASE + HC_INTERRUPT_ENABLE, INTR_MIE | INTR_SF, 4);
    xbox_OhciWrite(XBOX_OHCI0_BASE + HC_CONTROL, HC_OPERATIONAL, 4);
    xbox_guest_lock_release();

    CloseHandle(CreateThread(NULL, 0, title_thread, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, timer_thread, NULL, 0, NULL));
    /* Run until each interrupt has been raised often enough to test
     * anything, not for a fixed time: the controller raises one a tick
     * (ohci.c, every 20 ms), and each waits for the guest lock, which the
     * title and the timer thread take back to back. How many a second
     * brings depends on the machine and how busy it is. */
    for (waited = 0; waited < MAX_MS; waited += POLL_MS) {
        if (__atomic_load_n(&s_usb_raised, __ATOMIC_RELAXED) >= ENOUGH &&
            __atomic_load_n(&s_vblank_raised, __ATOMIC_RELAXED) >= ENOUGH)
            break;
        Sleep(POLL_MS);
    }
    __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE) < 3)
        Sleep(1);

    /* Run what the timer thread left queued, then read the counts holding
     * the guest lock, so the controller's routine stays out while they are
     * read. The drain runs without the lock, as each DPC takes it and gives
     * it back (kernel_run_dpc), and again if the controller's routine queued
     * one in between. */
    for (;;) {
        kernel_drain_dpcs();
        xbox_guest_lock_acquire();
        kernel_dpc_counts(&queued, &run, &waiting);
        if (!waiting) break;
        xbox_guest_lock_release();
    }

    printf("usb_interrupt: %ld USB and %ld display interrupts; DPCs %ld and %ld "
           "queued, %ld and %ld run (%lu queued, %lu run, %ld waiting in all)\n",
           (long)s_usb_raised, (long)s_vblank_raised, (long)s_usb_queued,
           (long)s_vblank_queued, (long)s_usb_run, (long)s_vblank_run,
           queued, run, waiting);
    if (s_usb_raised < ENOUGH || s_vblank_raised < ENOUGH)
        fail("too few interrupts to test anything");
    if (s_usb_run != s_usb_queued || s_vblank_run != s_vblank_queued || waiting)
        fail("a DPC queued did not run once: it was lost in the queue");

    /* The display's interrupt disconnected, still holding the guest lock. The
     * bytes past what KeInitializeInterrupt writes are the game's (on the
     * console, the kernel's Connected flag is at +16): filled, so that a
     * disconnect that writes them shows. */
    {
        const uint32_t disconnect[] = { VBLANK_INTERRUPT };
        uint8_t before[44];
        uint32_t first, second;
        int i;

        for (i = 12; i < 44; i++)
            BRIDGE_MEM8(VBLANK_INTERRUPT + (uint32_t)i) = (uint8_t)(0xA0 + i);
        memcpy(before, XBOX_TO_NATIVE(VBLANK_INTERRUPT), sizeof before);
        call_bridge(bridge_KeDisconnectInterrupt, disconnect, 1);
        first = g_eax;
        call_bridge(bridge_KeDisconnectInterrupt, disconnect, 1);
        second = g_eax;
        printf("usb_interrupt: KeDisconnectInterrupt said %u for the display's "
               "object, then %u; its vector now has %s\n", first, second,
               xbox_GetConnectedInterrupt(VBLANK_VECTOR) ? "it still" : "nothing");
        if (first != 1)
            fail("KeDisconnectInterrupt did not say a connected object was connected");
        if (second != 0)
            fail("KeDisconnectInterrupt said a disconnected object was still connected");
        if (xbox_GetConnectedInterrupt(VBLANK_VECTOR)
                || kernel_raise_interrupt_locked(VBLANK_VECTOR) != -1)
            fail("a disconnected object's routine is still called");
        if (memcmp(before, XBOX_TO_NATIVE(VBLANK_INTERRUPT), sizeof before))
            fail("KeDisconnectInterrupt changed the game's KINTERRUPT");
    }
    {
        /* Objects at guest address 0, pointers gone wrong in the game, are
         * turned away, and nothing is written there. run.py looks for the
         * [CHECK] lines. */
        const uint32_t null_int[] = { 0, USB_ISR_VA, 0, USB_VECTOR };
        const uint32_t null_dpc[] = { 0, USB_DPC_VA, 0 };
        uint8_t before[44];
        int i;

        for (i = 0; i < 44; i++) s_guest[i] = (uint8_t)(0x50 + i);
        memcpy(before, s_guest, sizeof before);
        call_bridge(bridge_KeInitializeInterrupt, null_int, 4);
        call_bridge(bridge_KeInitializeDpc, null_dpc, 3);
        if (memcmp(before, s_guest, sizeof before))
            fail("an interrupt object or DPC was written at guest address 0");
    }
    a_routine_synchronises();
    routines_not_in_dispatch();
#ifdef TEST_COVERAGE
    /* _exit() skips the exit handlers, and GCC writes the line counts from
     * one, so they are written first. */
    __gcov_dump();
#endif
    if (s_failures) {
        printf("usb_interrupt: %ld failures\n", (long)s_failures);
        fflush(stdout);
        _exit(1);
    }
    printf("usb_interrupt: all cases pass\n");
    fflush(stdout);
    _exit(0);
}
