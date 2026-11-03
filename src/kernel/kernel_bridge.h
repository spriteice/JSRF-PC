/**
 * kernel_bridge.h - What kernel_bridge.c shares with the kernel files split
 * out of it
 *
 * The guest's registers and memory as a bridge sees them, the interrupt and
 * DPC code in kernel_interrupt.c, which the bridge's device ticks, timer
 * thread and ordinal table use, the file APCs in kernel_apc.c, which its
 * file requests queue and its alertable waits deliver, the timers and timer
 * thread in kernel_timer.c, with the device ticks that thread runs, and the
 * NV2A's interrupts in nv2a_intr.c.
 */
#ifndef XBOX_KERNEL_BRIDGE_H
#define XBOX_KERNEL_BRIDGE_H

#include <stddef.h>
#include <stdint.h>
#include "xbox_memory_layout.h"   /* RECOMP_TLS */

void xbox_guest_lock_acquire(void);
void xbox_guest_lock_release(void);
/* Give the lock up, if held, and take it back after (1 if it was given up). */
int  xbox_guest_lock_drop(void);
void xbox_guest_lock_retake(int held);
/* Take the lock, if not held, and give it back after (1 if it was taken). */
int  xbox_guest_lock_take(void);
uint32_t xbox_GetConnectedInterrupt(uint32_t vector);

/* Access to recompiled code registers. Per-thread: RECOMP_TLS comes from
 * xbox_memory_layout.h and must match the definitions there -- a plain extern
 * here binds to the TLS template rather than the calling thread's copy, which
 * reads as every register being zero. */
extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern RECOMP_TLS uint32_t g_ebx, g_esi, g_edi;
extern uint32_t g_xbox_code_lo, g_xbox_code_hi;
extern RECOMP_TLS uint32_t g_seh_ebp;
extern ptrdiff_t g_xbox_mem_offset;

/* Dispatch table lookup (for function pointer args) */
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);

/* ── Synthetic VA range (for function exports) ─────────── */

#define KERNEL_VA_BASE  0xFE000000u
#define KERNEL_VA_END   (KERNEL_VA_BASE + XBOX_KERNEL_THUNK_TABLE_SIZE * 4)

/* Ordinal for each slot (read from Xbox memory during init) */
extern ULONG g_slot_ordinals[];

/* Memory access - same as recomp_types.h MEM32 but without the #define guard */
#define BRIDGE_MEM32(addr) (*(volatile uint32_t *)((uintptr_t)(addr) + g_xbox_mem_offset))

/* Translate Xbox VA to native pointer (NULL-safe: 0 → NULL) */
#define XBOX_TO_NATIVE(va) ((va) ? (void*)((uintptr_t)(va) + g_xbox_mem_offset) : NULL)

#define BRIDGE_MEM16(addr) (*(volatile uint16_t *)((uintptr_t)(addr) + g_xbox_mem_offset))
#define BRIDGE_MEM8(addr)  (*(volatile uint8_t  *)((uintptr_t)(addr) + g_xbox_mem_offset))

/* Read Xbox stack arg as uint32_t.
 * After kernel_thunk_dispatch pops the dummy return address (g_esp += 4),
 * arg0 is at g_esp+0, arg1 at g_esp+4, etc. */
#define STACK_ARG(n) ((uint32_t)BRIDGE_MEM32(g_esp + (n) * 4))

/* kernel_interrupt.c */

/* An interrupt, DPC or synchronize routine is running on this thread. */
extern RECOMP_TLS int t_in_callback;

/* Call the routine connected on a vector, taking the guest lock around it
 * (or with it already held), on the calling thread's guest stack. -1 if
 * nothing callable is connected, else what the routine returned. */
int  kernel_raise_interrupt(uint32_t vector);
int  kernel_raise_interrupt_locked(uint32_t vector);
/* Run a DPC's routine now, under the guest lock; 1 if it was called. */
int  kernel_run_dpc(uint32_t dpc_va, uint32_t arg1, uint32_t arg2);
/* Run every queued DPC. */
void kernel_drain_dpcs(void);
/* DPCs queued, run, and waiting now. */
void kernel_dpc_counts(unsigned long *queued, unsigned long *run, long *waiting);

/* Bridges, for the ordinal table. */
void bridge_HalGetInterruptVector(void);
void bridge_KeInitializeInterrupt(void);
void bridge_KeConnectInterrupt(void);
void bridge_KeSynchronizeExecution(void);
void bridge_KeInitializeDpc(void);
void bridge_KeInsertQueueDpc(void);
void bridge_KeRemoveQueueDpc(void);

/* kernel_apc.c */

/* Run one file APC now, on this thread. */
void deliver_one_apc(uint32_t apc_routine, uint32_t apc_context,
                     uint32_t iostatus);
/* Queue a file APC for this thread's next alertable wait. */
void bridge_queue_apc(uint32_t apc_routine, uint32_t apc_context, uint32_t iostatus);
int bridge_deliver_pending_apcs(void);

/* kernel_timer.c */

/* Start the kernel's timer thread, once. */
void kernel_timer_start(void);
/* Drop a timer so it stops firing. Returns whether it was armed. */
int  xbox_kernel_cancel_timer(uint32_t timer_va);
/* Bridges, for the ordinal table. */
void bridge_KeInitializeTimerEx(void);
void bridge_KeSetTimer(void);
void bridge_KeSetTimerEx(void);

/* kernel_bridge.c, for the timer thread */

/* Signal a guest dispatcher object. Returns the previous state. */
uint32_t xbox_dispatcher_signal(uint32_t va);
/* The devices' periodic work, on every pass of the timer thread. */
void kernel_freeze_watch(void);   /* has the picture stopped? */
void kernel_apu_tick(void);       /* the APU's interrupt line */
void kernel_cvfs_watch(void);     /* CRI's streaming file device */

/* nv2a_intr.c */

/* Set bits in one of the NV2A's interrupt status registers (its offset in
 * the NV2A's registers), as the unit does; the title's write clears them. */
void xbox_nv2a_intr_raise(uint32_t status, uint32_t bits);
/* The NV2A's interrupts, on every pass of the timer thread. */
void kernel_vblank_tick(void);    /* the GPU's frame clock */
void kernel_pgraph_tick(void);    /* the GPU's software methods */
/* The display interrupt: raised, claimed by the title's routine, declined
 * since the last claim, and PMC_INTR_EN_0 now. */
void kernel_vblank_counts(unsigned long *raised, unsigned long *claimed,
                          unsigned long *declined_since_claim, uint32_t *enabled);
/* The software methods: raised, waiting behind the one in flight, whether one
 * is and its parameter, and PGRAPH_INTR and PGRAPH_FIFO now. */
void kernel_swm_counts(unsigned long *raised, unsigned long *waiting, int *in_flight,
                       uint32_t *param, uint32_t *pgraph_intr, uint32_t *pgraph_fifo);

#endif /* XBOX_KERNEL_BRIDGE_H */
