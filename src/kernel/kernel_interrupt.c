/**
 * kernel_interrupt.c - The kernel's interrupts and DPCs
 *
 * Interrupt objects (HalGetInterruptVector, KeInitializeInterrupt,
 * KeConnectInterrupt), delivering an interrupt to the routine a title
 * connected (kernel_raise_interrupt), KeSynchronizeExecution, and the DPC
 * queue: KeInitializeDpc, KeInsertQueueDpc, KeRemoveQueueDpc, and the drain
 * the timer thread runs. The device models, in kernel_bridge.c and
 * elsewhere, and the timer thread in kernel_timer.c raise interrupts and
 * drain the queue through these.
 *
 * Moved out of kernel_bridge.c as it was, so that it can be linked and tested
 * on its own.
 */

#include "kernel.h"
#include <stdio.h>
#include <string.h>
#include "xbox_memory_layout.h"
#include "kernel_bridge.h"

/* -- KeInsertQueueDpc (ordinal 119) ------------------------
 * BOOLEAN KeInsertQueueDpc(PKDPC Dpc, PVOID SystemArgument1,
 *                          PVOID SystemArgument2)
 *
 * Unbridged this returned 0, which reads to a driver as "already queued" and
 * means the deferred routine never runs. That is not a small loss: an
 * interrupt service routine is expected to do almost nothing except mask the
 * source and queue a DPC, so with this missing every driver that follows the
 * normal pattern acknowledges its interrupt and then does none of the work.
 * Half-Life 2's USB stack does exactly that -- its ISR masks the master
 * interrupt enable and queues here, and the enumeration it should have started
 * lives entirely in the deferred routine.
 *
 * Queued, and run after the ISR has returned: the pending DPC queue, below.
 */
/* Run a DPC's deferred routine on the calling thread.
 *
 * VOID DeferredRoutine(PKDPC, PVOID Context, PVOID Arg1, PVOID Arg2),
 * __stdcall: its `ret 16` consumes the dummy return address and all four
 * arguments, so g_esp needs no fixup afterwards. The caller must already have
 * a guest stack -- true on a guest thread, and true on a host thread that has
 * taken a worker slice.
 *
 * Returns 1 if the routine was found and called.
 */
static int kernel_run_dpc_locked(uint32_t dpc_va, uint32_t arg1, uint32_t arg2);
/* An interrupt, DPC or synchronize routine is running on this thread. The
 * console switches no threads until it returns, so neither does a loop back
 * edge inside it (xbox_GuestCpuPreempt) nor the guest lock's time slice
 * (xbox_guest_lock_yield_now). */
RECOMP_TLS int t_in_callback;
int kernel_run_dpc(uint32_t dpc_va, uint32_t arg1, uint32_t arg2)
{
    int r;
    xbox_guest_lock_acquire();
    r = kernel_run_dpc_locked(dpc_va, arg1, arg2);
    xbox_guest_lock_release();
    return r;
}
static int kernel_run_dpc_locked(uint32_t dpc_va, uint32_t arg1, uint32_t arg2)
{
    uint32_t routine, context;
    recomp_func_t fn;

    if (!dpc_va)
        return 0;
    routine = BRIDGE_MEM32(dpc_va + 12);
    context = BRIDGE_MEM32(dpc_va + 16);
    if (!routine)
        return 0;

    fn = recomp_lookup(routine);
    if (!fn) fn = recomp_lookup_manual(routine);
    if (!fn) {
        fprintf(stderr, "  [KERNEL] DPC routine 0x%08X not in dispatch\n",
                routine);
        fflush(stderr);
        return 0;
    }

    { extern void xbox_waitlog_dpc(uint32_t routine) __attribute__((weak));
      if (xbox_waitlog_dpc) xbox_waitlog_dpc(routine); }

    BRIDGE_MEM32(dpc_va + 20) = arg1;
    BRIDGE_MEM32(dpc_va + 24) = arg2;

    g_esp -= 4; BRIDGE_MEM32(g_esp) = arg2;
    g_esp -= 4; BRIDGE_MEM32(g_esp) = arg1;
    g_esp -= 4; BRIDGE_MEM32(g_esp) = context;
    g_esp -= 4; BRIDGE_MEM32(g_esp) = dpc_va;
    g_esp -= 4; BRIDGE_MEM32(g_esp) = 0;
    t_in_callback++;
    fn();
    t_in_callback--;
    return 1;
}

/* -- KeSynchronizeExecution (ordinal 153) ------------------
 * BOOLEAN KeSynchronizeExecution(PKINTERRUPT Interrupt,
 *                                PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
 *                                PVOID SynchronizeContext)
 *
 * Runs a routine while holding the interrupt's spinlock at the ISR's IRQL,
 * which is how a driver touches its hardware from anywhere that is not the
 * ISR without racing the ISR. Unbridged it returned 0 and the routine never
 * ran -- the same failure as an unqueued DPC, and just as quiet: the driver
 * asks for exclusive access, is told it did not get it, and skips the work.
 *
 * The exclusion is the guest lock. The caller holds it, as every thread
 * running the title's code does, and every interrupt routine is called
 * holding it too (kernel_raise_interrupt), so the routine here never runs
 * beside the ISR it synchronises with, as on the console.
 */
void bridge_KeSynchronizeExecution(void)
{
    uint32_t routine = STACK_ARG(1);
    uint32_t context = STACK_ARG(2);
    recomp_func_t fn;

    fn = routine ? recomp_lookup(routine) : NULL;
    if (!fn && routine) fn = recomp_lookup_manual(routine);
    if (!fn) {
        fprintf(stderr, "  [KERNEL] KeSynchronizeExecution: routine 0x%08X "
                        "not in dispatch\n", routine);
        fflush(stderr);
        g_eax = 0;
        return;
    }

    /* BOOLEAN SynchronizeRoutine(PVOID Context), __stdcall. Its `ret 4` takes
     * the dummy return address and the argument, so g_esp needs no fixup. */
    g_esp -= 4; BRIDGE_MEM32(g_esp) = context;
    g_esp -= 4; BRIDGE_MEM32(g_esp) = 0;
    t_in_callback++;
    fn();
    t_in_callback--;
    /* g_eax is whatever the routine returned, which is this call's result. */
}

/* -- KeRemoveQueueDpc (ordinal 137) ------------------------
 * BOOLEAN KeRemoveQueueDpc(PKDPC Dpc)
 *
 * Cancels a queued DPC, returning whether it was still in the queue. DPCs run
 * inline here (see KeInsertQueueDpc), so by the time anyone can call this the
 * routine has already run and there is nothing to cancel. FALSE is both the
 * honest answer and the one that keeps a caller's bookkeeping right.
 */
void bridge_KeRemoveQueueDpc(void)
{
    g_eax = 0;
}

/* The pending DPC queue.
 *
 * These used to run inline, on whichever thread queued them, before the caller
 * returned. That is not what a DPC is: an interrupt service routine masks its
 * source and queues, and the deferred routine runs afterwards at DISPATCH_LEVEL
 * -- after the ISR has returned. Running it inline inverts that, so a routine
 * that re-enables interrupts does it while the ISR that masked them is still on
 * the stack, and a driver's state machine re-enters itself from inside its own
 * interrupt.
 *
 * Queued properly now, and drained by the timer thread, which is the one thread
 * here that already has a guest stack and a TIB and runs nothing else urgent.
 *
 * ponytail: one queue, no IRQL, no per-processor list, and a DPC queued from a
 * DPC runs on the next drain rather than immediately. Nothing here depends on
 * DPC ordering beyond "after the ISR".
 *
 * Every caller holds the guest lock -- an interrupt routine, a DPC or the
 * title's own code -- so callers never meet. The drain, the one reader, runs
 * without it: it reads a slot only once the tail has published it, and gives
 * the slot back only once it has been read. The USB interrupt's routine used
 * to queue here without the lock (ohci_call_isr), in the same instant as the
 * timer thread queued the vblank's, and both took the same slot: the vblank's
 * DPC was lost, the display interrupt stayed masked, and JSRF froze at its
 * first frame on some Windows starts.
 */
#define XBOX_MAX_PENDING_DPC 64
typedef struct { uint32_t dpc, arg1, arg2; } PendingDpc;
static PendingDpc g_dpc_queue[XBOX_MAX_PENDING_DPC];
static LONG g_dpc_head, g_dpc_tail;
/* For the freeze report: queued minus run is what is waiting. */
static unsigned long g_dpc_queued, g_dpc_run;

void bridge_KeInsertQueueDpc(void)
{
    uint32_t dpc  = STACK_ARG(0);
    uint32_t arg1 = STACK_ARG(1);
    uint32_t arg2 = STACK_ARG(2);
    LONG tail, next;

    if (!dpc) { g_eax = 0; return; }

    tail = g_dpc_tail;
    next = (tail + 1) % XBOX_MAX_PENDING_DPC;
    if (next == __atomic_load_n(&g_dpc_head, __ATOMIC_ACQUIRE)) {
        fprintf(stderr, "  [KERNEL] DPC queue full, dropping 0x%08X\n", dpc);
        fflush(stderr);
        g_eax = 0;
        return;
    }
    g_dpc_queue[tail].dpc  = dpc;
    g_dpc_queue[tail].arg1 = arg1;
    g_dpc_queue[tail].arg2 = arg2;
    __atomic_add_fetch(&g_dpc_queued, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&g_dpc_tail, next, __ATOMIC_RELEASE);
    g_eax = 1;
}

/* Call a connected interrupt service routine.
 *
 * BOOLEAN ServiceRoutine(PKINTERRUPT, PVOID ServiceContext), __stdcall: its
 * `ret 8` takes the dummy return address and both arguments, so g_esp needs no
 * fixup. The caller must already have a guest stack and a TIB, which the timer
 * thread has.
 *
 * Returns what the routine returned -- an ISR that does not recognise the
 * interrupt returns FALSE, and that is worth seeing rather than assuming.
 */
int kernel_raise_interrupt(uint32_t vector)
{
    int r;
    xbox_guest_lock_acquire();
    r = kernel_raise_interrupt_locked(vector);
    xbox_guest_lock_release();
    return r;
}
int kernel_raise_interrupt_locked(uint32_t vector)
{
    uint32_t kint = xbox_GetConnectedInterrupt(vector);
    uint32_t routine, context;
    recomp_func_t fn;

    if (!kint)
        return -1;
    routine = BRIDGE_MEM32(kint + 0);
    context = BRIDGE_MEM32(kint + 4);
    if (!routine)
        return -1;
    fn = recomp_lookup(routine);
    if (!fn) fn = recomp_lookup_manual(routine);
    if (!fn)
        return -1;

    { static uint32_t said[8];
      if (vector < 8 && said[vector] != routine) {
          said[vector] = routine;
          fprintf(stderr, "  [KERNEL] vector %u is serviced by guest 0x%08X\n",
                  vector, routine);
          fflush(stderr);
      } }

    g_esp -= 4; BRIDGE_MEM32(g_esp) = context;
    g_esp -= 4; BRIDGE_MEM32(g_esp) = kint;
    g_esp -= 4; BRIDGE_MEM32(g_esp) = 0;
    t_in_callback++;
    fn();
    t_in_callback--;
    return (int)(g_eax & 1u);
}

/* Run whatever is queued. Called from the timer thread, which has the guest
 * stack and TIB that a deferred routine needs. The only reader, so it takes
 * no lock (the queue, above). */
void kernel_drain_dpcs(void)
{
    while (g_dpc_head != __atomic_load_n(&g_dpc_tail, __ATOMIC_ACQUIRE)) {
        LONG head = g_dpc_head;
        PendingDpc d = g_dpc_queue[head];
        __atomic_store_n(&g_dpc_head, (head + 1) % XBOX_MAX_PENDING_DPC,
                         __ATOMIC_RELEASE);
        __atomic_add_fetch(&g_dpc_run, 1, __ATOMIC_RELAXED);
        kernel_run_dpc(d.dpc, d.arg1, d.arg2);
    }
}

/* DPCs queued, run, and waiting now, for the freeze report. */
void kernel_dpc_counts(unsigned long *queued, unsigned long *run, long *waiting)
{
    LONG tail = __atomic_load_n(&g_dpc_tail, __ATOMIC_ACQUIRE);
    LONG head = __atomic_load_n(&g_dpc_head, __ATOMIC_ACQUIRE);
    *queued  = __atomic_load_n(&g_dpc_queued, __ATOMIC_RELAXED);
    *run     = __atomic_load_n(&g_dpc_run, __ATOMIC_RELAXED);
    *waiting = (long)((tail - head + XBOX_MAX_PENDING_DPC) % XBOX_MAX_PENDING_DPC);
}

/* ── KeInitializeDpc (ordinal 107) ────────────────────────
 * VOID KeInitializeDpc(PKDPC Dpc, PKDEFERRED_ROUTINE DeferredRoutine,
 *                       PVOID DeferredContext)
 *
 * Initializes a DPC object. The Xbox KDPC structure is 32 bytes.
 * We zero it and set the routine and context pointers.
 */
void bridge_KeInitializeDpc(void)
{
    uint32_t dpc_va = STACK_ARG(0);
    uint32_t routine = STACK_ARG(1);
    uint32_t context = STACK_ARG(2);

    /* Zero the structure (32 bytes) */
    memset(XBOX_TO_NATIVE(dpc_va), 0, 32);

    /* Set Type (0x13 = DpcObject) and fields */
    BRIDGE_MEM16(dpc_va + 0) = 0x13;   /* Type */
    BRIDGE_MEM32(dpc_va + 12) = routine; /* DeferredRoutine */
    BRIDGE_MEM32(dpc_va + 16) = context; /* DeferredContext */
    g_eax = 0;
}

/* ── NV2A interrupt plumbing (ordinals 44, 98, 109) ───────
 *
 * The D3D8 library linked into a title installs an ISR for the GPU's vblank /
 * command-completion interrupt. There is no NV2A here and nothing ever raises
 * that interrupt, so these exist to let initialisation complete rather than to
 * deliver anything.
 *
 * KeConnectInterrupt reports success: reporting failure sends Halo's
 * rasterizer down an error path during preinitialize, and the goal is to get
 * past setup, not to pretend the hardware is broken.
 *
 * ponytail: no interrupt is ever delivered. Code that *waits* on the ISR
 * rather than polling will hang here, and the fix for that is to bridge the
 * D3D8 entry point that owns the wait, not to synthesise NV2A interrupts.
 */

/* ULONG HalGetInterruptVector(ULONG BusInterruptLevel, PKIRQL Irql) */
void bridge_HalGetInterruptVector(void)
{
    uint32_t level   = STACK_ARG(0);
    uint32_t irql_va = STACK_ARG(1);

    if (irql_va) {
        /* IRQL is conventionally the vector for device interrupts. */
        BRIDGE_MEM8(irql_va) = (uint8_t)level;
    }
    g_eax = level;
}

/* VOID KeInitializeInterrupt(PKINTERRUPT, ServiceRoutine, ServiceContext,
 *                            Vector, Irql, InterruptMode, ShareVector) */
void bridge_KeInitializeInterrupt(void)
{
    uint32_t interrupt_va = STACK_ARG(0);
    uint32_t routine      = STACK_ARG(1);
    uint32_t context      = STACK_ARG(2);
    uint32_t vector       = STACK_ARG(3);

    /* Xbox KINTERRUPT is 44 bytes. */
    memset(XBOX_TO_NATIVE(interrupt_va), 0, 44);
    BRIDGE_MEM32(interrupt_va + 0)  = routine;
    BRIDGE_MEM32(interrupt_va + 4)  = context;
    BRIDGE_MEM32(interrupt_va + 8)  = vector;
    g_eax = 0;
}

/* The interrupt objects a title has connected, by vector.
 *
 * KeInitializeInterrupt already writes the service routine, its context and
 * the vector into the guest KINTERRUPT; connecting is what says the title is
 * ready to be called on it. Recording that is what lets a device model raise
 * an interrupt at all -- without it the routine is written down in guest
 * memory and nothing on this side knows it is there.
 *
 * A small fixed table rather than a list: an Xbox has 26 interrupt vectors and
 * a title connects a handful, so the whole thing is smaller than the comment.
 */
#define XBOX_MAX_VECTORS 32
static uint32_t g_connected_isr[XBOX_MAX_VECTORS];   /* KINTERRUPT guest VA */

/* BOOLEAN KeConnectInterrupt(PKINTERRUPT Interrupt) */
void bridge_KeConnectInterrupt(void)
{
    uint32_t interrupt_va = STACK_ARG(0);

    if (interrupt_va) {
        uint32_t vector = BRIDGE_MEM32(interrupt_va + 8);
        if (vector < XBOX_MAX_VECTORS) {
            g_connected_isr[vector] = interrupt_va;
            fprintf(stderr, "  [KERNEL] KeConnectInterrupt: vector %u -> "
                            "routine 0x%08X context 0x%08X\n",
                    vector, BRIDGE_MEM32(interrupt_va + 0),
                    BRIDGE_MEM32(interrupt_va + 4));
            fflush(stderr);
        }
    }
    g_eax = 1;  /* connected -- see the note above */
}

/* The KINTERRUPT a title connected on this vector, or 0. Device models use it
 * to find the routine to call; the routine and its context are at +0 and +4. */
uint32_t xbox_GetConnectedInterrupt(uint32_t vector)
{
    return (vector < XBOX_MAX_VECTORS) ? g_connected_isr[vector] : 0;
}
