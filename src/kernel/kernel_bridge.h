/**
 * kernel_bridge.h - What kernel_bridge.c shares with the kernel files split
 * out of it
 *
 * The guest's registers and memory as a bridge sees them, and the interrupt
 * and DPC code in kernel_interrupt.c, which the bridge's device ticks, timer
 * thread and ordinal table use.
 */
#ifndef XBOX_KERNEL_BRIDGE_H
#define XBOX_KERNEL_BRIDGE_H

#include <stddef.h>
#include <stdint.h>
#include "xbox_memory_layout.h"   /* RECOMP_TLS */

void xbox_guest_lock_acquire(void);
void xbox_guest_lock_release(void);
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

#endif /* XBOX_KERNEL_BRIDGE_H */
