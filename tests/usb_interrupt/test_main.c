/* Run with: python tests/usb_interrupt/run.py
 *
 * The USB controller's interrupt, delivered beside the display's, as in the
 * first second of a start (known bug 16).
 *
 * src/usb/ohci.c's controller thread raises its start-of-frame interrupt
 * every tick, and a timer thread raises the display's interrupt and drains
 * the DPCs, as kernel_bridge.c's does, both through src/kernel/
 * kernel_interrupt.c. A third thread stands in for the title, holding the
 * guest lock most of the time. Each interrupt routine queues a DPC with
 * KeInsertQueueDpc, as the game's do.
 *
 * Every routine, interrupt or DPC, must run holding the guest lock and marked
 * as an interrupt, with no other routine running at the same time, and every
 * DPC queued must run once, with its own arguments. Before the USB interrupt
 * went through the kernel, its routine ran on the controller's thread without
 * the lock, beside the title and the timer thread, and the first one fails
 * here. A DPC overwritten in the queue by the other thread's shows as one
 * that never ran, or ran with the other's arguments.
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
#include "ohci.h"
#include "usb_gamepad.h"

#define SECONDS        1
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

recomp_func_t recomp_lookup(uint32_t va)
{
    switch (va) {
    case USB_ISR_VA:    return usb_isr;
    case VBLANK_ISR_VA: return vblank_isr;
    case USB_DPC_VA:    return usb_dpc;
    case VBLANK_DPC_VA: return vblank_dpc;
    default:            return NULL;
    }
}
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }

/* ---- the threads ------------------------------------------------------- */

/* The title: holds the guest lock most of the time, as its main thread does. */
static DWORD WINAPI title_thread(LPVOID unused)
{
    (void)unused;
    while (!__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
        xbox_guest_lock_acquire();
        work();
        xbox_guest_lock_release();
        Sleep(0);
    }
    __atomic_add_fetch(&s_stop, 1, __ATOMIC_RELEASE);
    return 0;
}

/* The timer thread: the display's interrupt, then the DPCs, every turn. */
static DWORD WINAPI timer_thread(LPVOID unused)
{
    int slot = xbox_worker_stack_alloc();
    (void)unused;
    g_esp = XBOX_WORKER_STACK_TOP(slot);
    while (!__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
        kernel_raise_interrupt(VBLANK_VECTOR);
        kernel_drain_dpcs();
        Sleep(0);
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

int main(void)
{
    const uint32_t usb_int[]    = { USB_INTERRUPT, USB_ISR_VA, 0, USB_VECTOR };
    const uint32_t vblank_int[] = { VBLANK_INTERRUPT, VBLANK_ISR_VA, 0, VBLANK_VECTOR };
    const uint32_t usb_dpc_init[]    = { USB_DPC, USB_DPC_VA, 0 };
    const uint32_t vblank_dpc_init[] = { VBLANK_DPC, VBLANK_DPC_VA, 0 };
    const uint32_t connect_usb[] = { USB_INTERRUPT }, connect_vblank[] = { VBLANK_INTERRUPT };
    unsigned long queued, run;
    long waiting;
    int slot;

    s_guest = calloc(1, GUEST_BYTES);
    if (!s_guest) return 2;
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)s_guest;
    InitializeCriticalSection(&s_lock);
    slot = xbox_worker_stack_alloc();
    g_esp = XBOX_WORKER_STACK_TOP(slot);

    /* The game's set-up: its interrupts connected, its DPCs made. */
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
    Sleep(SECONDS * 1000);
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
    if (s_usb_raised < 10 || s_vblank_raised < 10)
        fail("too few interrupts to test anything");
    if (s_usb_run != s_usb_queued || s_vblank_run != s_vblank_queued || waiting)
        fail("a DPC queued did not run once: it was lost in the queue");
    if (s_failures) {
        printf("usb_interrupt: %ld failures\n", (long)s_failures);
        fflush(stdout);
        _exit(1);
    }
    printf("usb_interrupt: all cases pass\n");
    fflush(stdout);
    _exit(0);
}
