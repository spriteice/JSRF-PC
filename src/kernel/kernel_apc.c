/**
 * kernel_apc.c - The file requests' APCs
 *
 * A file request that names an APC routine (NtReadFile, NtWriteFile) has it
 * queued on the thread that made the request, and delivered at that thread's
 * next alertable wait: deliver_one_apc, the per-thread ring, bridge_queue_apc
 * and bridge_deliver_pending_apcs. kernel_bridge.c's file requests queue them
 * and its alertable waits deliver them.
 *
 * Moved out of kernel_bridge.c as it was, so that it can be linked and tested
 * on its own.
 */

#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include "xbox_memory_layout.h"
#include "kernel_bridge.h"

void deliver_one_apc(uint32_t apc_routine, uint32_t apc_context,
                     uint32_t iostatus)
{
    /* The APC can be game code or a kernel export. Halo's XAPI passes the
     * latter -- 0xFE0000FC, one of our own synthetic thunk VAs -- so the recomp
     * dispatch correctly fails to find it and the kernel fallback is the one
     * that matters. Checking only recomp_lookup left it undelivered. */
    recomp_func_t fn = recomp_lookup(apc_routine);
    if (!fn) fn = recomp_lookup_manual(apc_routine);
    int kernel_target = 0;
    if (!fn) { fn = recomp_lookup_kernel(apc_routine); kernel_target = fn != NULL; }
    if (fn) {
        /* VOID ApcRoutine(PVOID ApcContext, PIO_STATUS_BLOCK, ULONG) */
        uint32_t esp0 = g_esp;
        g_esp -= 4; BRIDGE_MEM32(g_esp) = 0;
        g_esp -= 4; BRIDGE_MEM32(g_esp) = iostatus;
        g_esp -= 4; BRIDGE_MEM32(g_esp) = apc_context;
        g_esp -= 4; BRIDGE_MEM32(g_esp) = 0;   /* dummy return address */
        fn();
        /* A lifted routine returns with `ret` having popped only the return
         * address, so its three stdcall arguments are still ours to drop. A
         * kernel target goes through kernel_thunk_dispatch, which already
         * pops both the return address and the ordinal's argument bytes;
         * adding 12 on top of that over-popped the caller's frame (JSRF's
         * SleepEx came back with esp 12 high and esi from the wrong slot). */
        (void)kernel_target;
        g_esp = esp0;
    } else {
        uint32_t ord = 0;
        if (apc_routine >= KERNEL_VA_BASE && apc_routine < KERNEL_VA_END) {
            ord = g_slot_ordinals[(apc_routine - KERNEL_VA_BASE) / 4];
        }
        fprintf(stderr, "  [KERNEL] file I/O APC 0x%08X unresolved"
                " (kernel ordinal %u)\n", apc_routine, ord);
        fflush(stderr);
    }
}

/* Per-thread pending-APC ring. An APC is delivered on the thread that issued
 * the request, which is also the thread that waits, so thread-local is right.
 *
 * Deferred, not inline. JSRF's CRI file server (sub_00140BA0) issues
 * ReadFileEx, then tests its own "I/O pending" flag: only if still pending
 * does it SleepEx(0, TRUE) to let the completion run, and only then does it
 * account the bytes and mark the request done. With the completion routine
 * run inside NtReadFile the flag was already clear at the test, the
 * accounting never happened, the stream starved, and CRI's watchdog turned
 * the missing music into a "disc may be dirty or damaged" screen. Real
 * kernels never run a user APC before the request returns; neither do we
 * now. RECOMP_APC_INLINE=1 restores the old behaviour. */
#define BRIDGE_APC_RING 32
static RECOMP_TLS struct { uint32_t routine, context, iostatus; } t_apc_ring[BRIDGE_APC_RING];
static RECOMP_TLS int t_apc_head, t_apc_count;

void bridge_queue_apc(uint32_t apc_routine, uint32_t apc_context, uint32_t iostatus)
{
    if (t_apc_count >= BRIDGE_APC_RING) {
        fprintf(stderr, "  [KERNEL] APC ring full; delivering 0x%08X inline\n", apc_routine);
        deliver_one_apc(apc_routine, apc_context, iostatus);
        return;
    }
    int i = (t_apc_head + t_apc_count) % BRIDGE_APC_RING;
    if (getenv("JSRF_DBG")) fprintf(stderr, "  [APC] queued routine=0x%08X ctx=0x%08X (pending %d)\n", apc_routine, apc_context, t_apc_count + 1);
    t_apc_ring[i].routine = apc_routine;
    t_apc_ring[i].context = apc_context;
    t_apc_ring[i].iostatus = iostatus;
    t_apc_count++;
}

/* Deliver every APC queued on this thread; returns how many ran. Called from
 * the alertable waits, which then report STATUS_USER_APC instead of waiting. */
int bridge_deliver_pending_apcs(void)
{
    int n = 0;
    while (t_apc_count > 0) {
        int i = t_apc_head;
        uint32_t r = t_apc_ring[i].routine, c = t_apc_ring[i].context, io = t_apc_ring[i].iostatus;
        t_apc_head = (t_apc_head + 1) % BRIDGE_APC_RING;
        t_apc_count--;
        if (getenv("JSRF_DBG")) fprintf(stderr, "  [APC] delivering routine=0x%08X ctx=0x%08X\n", r, c);
        deliver_one_apc(r, c, io);
        n++;
    }
    return n;
}
