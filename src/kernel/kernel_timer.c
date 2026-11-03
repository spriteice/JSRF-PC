/**
 * kernel_timer.c - The kernel's timers, and the thread that runs them
 *
 * Timer objects (KeInitializeTimerEx, KeSetTimer, KeSetTimerEx, and the
 * firing table KeCancelTimer drops a timer from), and the kernel's timer
 * thread, which starts with the kernel: every millisecond it moves
 * KeTickCount, fires the timers that are due and runs their DPCs, drains the
 * DPC queue, and does the devices' periodic work in kernel_bridge.c (the
 * display and APU interrupts, the GPU's software methods, and the freeze and
 * file-device watches).
 *
 * Moved out of kernel_bridge.c as it was, so that it can be linked and tested
 * on its own.
 */

#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xbox_memory_layout.h"
#include "kernel_bridge.h"

/* ── KeInitializeTimerEx (ordinal 113) ────────────────────
 * VOID KeInitializeTimerEx(PKTIMER Timer, TIMER_TYPE Type)
 *
 * Initializes a timer object. Xbox KTIMER is 40 bytes.
 */
void bridge_KeInitializeTimerEx(void)
{
    uint32_t timer_va = STACK_ARG(0);
    uint32_t type = STACK_ARG(1);

    /* Zero the structure (40 bytes) */
    memset(XBOX_TO_NATIVE(timer_va), 0, 40);

    /* Set Type (0x08 = TimerNotificationObject, 0x09 = TimerSynchronizationObject) */
    BRIDGE_MEM16(timer_va + 0) = (uint16_t)(0x08 + (type & 1));
    g_eax = 0;
}

/* -- KeSetTimer / KeSetTimerEx (ordinal 149/150) ----------
 * BOOLEAN KeSetTimer(PKTIMER Timer, LARGE_INTEGER DueTime, PKDPC Dpc)
 * BOOLEAN KeSetTimerEx(PKTIMER Timer, LARGE_INTEGER DueTime, LONG Period,
 *                      PKDPC Dpc)
 *
 * These used to return FALSE and do nothing, on the grounds that timers were
 * not needed for basic execution. They are needed for more than that: a driver
 * that polls its hardware does it from a timer DPC, and one that never fires
 * is a state machine that never advances. Half-Life 2's USB stack sets one up
 * during XInitDevices and enumerates from it.
 *
 * A due time is in 100 ns units, negative for relative and positive for an
 * absolute time since 1601. Only the relative form is honoured here; an
 * absolute due time is treated as immediate, which is wrong in principle and
 * has not come up in practice.
 *
 * ponytail: one thread, a fixed table, and a 1 ms tick, so a due time is late
 * by up to a tick and a periodic timer drifts. Nothing here is scheduling
 * audio off a timer. A title that needs better wants the host's timer queue,
 * not a smaller sleep.
 */
#define XBOX_MAX_TIMERS 32
typedef struct {
    uint32_t timer_va;      /* the guest KTIMER, 0 for a free slot */
    uint32_t dpc_va;
    long long due_ms;       /* host tick when it fires */
    long      period_ms;    /* 0 for one-shot */
} XboxTimer;
static XboxTimer g_timers[XBOX_MAX_TIMERS];
static CRITICAL_SECTION g_timer_lock;
static int g_timer_started;

/* The thread's guest stack and TIB, which kernel_timer_start takes for it. */
static uint32_t g_timer_esp, g_timer_tib;

static DWORD WINAPI kernel_timer_thread(LPVOID unused)
{
    (void)unused;
    g_esp = g_timer_esp;
    g_fs_base = g_timer_tib;

    for (;;) {
        long long now;
        int i;

        Sleep(1);
        /* KeTickCount, the console's millisecond clock, which the XDK's
         * GetTickCount reads. The console's clock interrupt moves it every
         * millisecond whatever else is running, so this thread does, first
         * on each pass and from the clock its interrupts and timers use.
         *
         * It was written at the bottom of the GPU thread's loop, so it moved
         * a frame at a time and stood still through a long frame, such as a
         * shader build. Before that it was written once and left frozen,
         * which broke every timeout that polls it: Halo's DHCP setup waits
         * on a tick deadline that never came and spun forever bringing up
         * XNet. One writer and an aligned store, so no lock; atomic, so the
         * guest's reads on other threads see it. */
        __atomic_store_n(&BRIDGE_MEM32(XBOX_KERNEL_DATA_BASE + KDATA_TICK_COUNT),
                         (uint32_t)GetTickCount(), __ATOMIC_RELAXED);
        /*
         * RECOMP_TABLE_WATCH=<addr>,<bytes>,<stride>: catch a pointer table
         * going bad at the moment it goes bad.
         *
         * JSRF crashes about one run in ten dereferencing a vtable through an
         * object pointer of 0x00010010 -- which is its own XBE base plus
         * sixteen, i.e. the middle of the image's RSA signature. The pointer
         * comes out of a table of thirty-two sixteen-byte entries at guest
         * 0x00261388 that the title walks calling a virtual on each live
         * entry. So the question is no longer "what corrupted memory" but
         * "who wrote that value into those 512 bytes", and the answer is worth
         * having the moment it happens rather than several seconds later when
         * the walker reaches it and dies.
         *
         * Scanning from here rather than trapping the write: a write watch
         * means mprotect and a fault handler on a page the title writes
         * constantly, and this costs 128 loads every millisecond.
         */
        {
            static int on = -1;
            static uint32_t base, len, stride;
            static uint32_t prev[64];
            static int prev_valid, reported;
            if (on < 0) {
                const char *v = getenv("RECOMP_TABLE_WATCH");
                on = 0;
                if (v) {
                    base = (uint32_t)strtoul(v, (char **)&v, 0);
                    if (*v == ',') len = (uint32_t)strtoul(v + 1, (char **)&v, 0);
                    if (*v == ',') stride = (uint32_t)strtoul(v + 1, NULL, 0);
                    if (!len) len = 512;
                    if (!stride) stride = 16;
                    on = base ? 1 : 0;
                    if (on)
                        fprintf(stderr, "  [WATCH] %u bytes at guest %08X, "
                                "every %u, for pointers that stop making "
                                "sense\n", len, base, stride);
                }
            }
            if (on && reported < 8) {
                uint32_t i, n = len / stride;
                if (n > 64) n = 64;
                for (i = 0; i < n; i++) {
                    uint32_t v = BRIDGE_MEM32(base + i * stride);
                    /* Plausible: null, or somewhere the title could really
                     * have put an object -- its own image or its heap. The
                     * value that kills it, 0x10010, is inside the image but
                     * inside the header, which nothing allocates out of. */
                    int ok = (v == 0)
                          || (v >= 0x00011000u && v < 0x00290000u)   /* code/data */
                          || (v >= 0x00F80000u && v < 0x04000000u);  /* heap */
                    if (!ok || (prev_valid && v != prev[i] && v && !ok)) {
                        reported++;
                        fprintf(stderr, "  [WATCH] entry %u at %08X became "
                                "%08X (was %08X) -- not a plausible object "
                                "pointer\n", i, base + i * stride, v,
                                prev_valid ? prev[i] : 0);
                        fflush(stderr);
                        { extern void xbox_kernel_thread_report(void);
                          xbox_kernel_thread_report(); }
                        break;
                    }
                    prev[i] = v;
                }
                prev_valid = 1;
            }
        }
        kernel_freeze_watch(); /* has the picture stopped? */
        kernel_vblank_tick();  /* the GPU's frame clock */
        kernel_pgraph_tick();  /* the GPU's software methods */
        kernel_apu_tick();     /* the APU's interrupt line */
        kernel_cvfs_watch();   /* CRI's streaming file device */
        kernel_drain_dpcs();   /* deferred work, before due timers */
        now = (long long)GetTickCount64();

        for (i = 0; i < XBOX_MAX_TIMERS; i++) {
            uint32_t dpc, timer_va_fired;

            EnterCriticalSection(&g_timer_lock);
            if (!g_timers[i].timer_va || now < g_timers[i].due_ms) {
                LeaveCriticalSection(&g_timer_lock);
                continue;
            }
            dpc = g_timers[i].dpc_va;
            timer_va_fired = g_timers[i].timer_va;
            if (g_timers[i].period_ms > 0)
                g_timers[i].due_ms = now + g_timers[i].period_ms;
            else
                g_timers[i].timer_va = 0;      /* one-shot, done */
            LeaveCriticalSection(&g_timer_lock);

            /* Outside the lock: the routine can set or cancel timers. */
            xbox_dispatcher_signal(g_timers[i].timer_va ? g_timers[i].timer_va : timer_va_fired);
            if (dpc)
                kernel_run_dpc(dpc, 0, 0);
        }
    }
    return 0;   /* not reached: it runs until the engine exits */
}

/* Start the timer thread, once: with the kernel (xbox_kernel_bridge_init),
 * as the console's clock interrupt runs from boot. It started at the
 * title's first KeSetTimer, so until a title set a timer nothing raised the
 * display interrupt or ran DPCs. */
void kernel_timer_start(void)
{
    int slot;

    if (g_timer_started)
        return;
    InitializeCriticalSection(&g_timer_lock);
    g_timer_started = 1;

    /* Its guest stack and TIB are taken here, before it runs, not by the
     * thread itself. It starts just as the thread that starts the title
     * writes its first lines, and on Windows two threads' lines in the log
     * mix character by character, so the TIB's line from the heap, written
     * by the new thread, would come out mixed with "Entry point". Taken
     * here, the lines come in order, and the TIB is always the heap's first
     * block. */
    slot = xbox_worker_stack_alloc();
    if (slot < 0) {
        fprintf(stderr, "  [KERNEL] timer thread has no worker stack; "
                        "it doesn't start: no clock, timers or DPCs\n");
        fflush(stderr);
        return;
    }
    g_timer_esp = XBOX_WORKER_STACK_TOP(slot);
    /* Its own TIB, for the same reason bridge_thread_main gives one to
     * every worker: a DPC routine with an SEH prologue reads fs:[0], and a
     * thread whose g_fs_base is zero faults inside the runtime before the
     * routine runs. */
    g_timer_tib = xbox_AllocThreadTib();
    if (!g_timer_tib) {
        fprintf(stderr, "  [KERNEL] timer thread has no TIB; "
                        "it doesn't start: no clock, timers or DPCs\n");
        fflush(stderr);
        return;
    }
    CloseHandle(CreateThread(NULL, 0, kernel_timer_thread, NULL, 0, NULL));
    fprintf(stderr, "  Kernel timer thread: KeTickCount, timers, DPCs and "
                    "device interrupts every millisecond\n");
}

/* Shared by KeSetTimer and KeSetTimerEx; period is 0 for the former. */
static void kernel_set_timer(uint32_t timer_va, long long due_100ns,
                             long period_ms, uint32_t dpc_va)
{
    long long delay_ms = (due_100ns < 0) ? (-due_100ns) / 10000 : 0;
    int i, free_slot = -1;
    uint32_t was_set = 0;

    EnterCriticalSection(&g_timer_lock);
    for (i = 0; i < XBOX_MAX_TIMERS; i++) {
        if (g_timers[i].timer_va == timer_va) { free_slot = i; was_set = 1; break; }
        if (!g_timers[i].timer_va && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        g_timers[free_slot].timer_va  = timer_va;
        g_timers[free_slot].dpc_va    = dpc_va;
        g_timers[free_slot].due_ms    = (long long)GetTickCount64() + delay_ms;
        g_timers[free_slot].period_ms = period_ms;
    }
    LeaveCriticalSection(&g_timer_lock);
    g_eax = was_set;
}

void bridge_KeSetTimer(void)
{
    /* LARGE_INTEGER is two stack slots. */
    long long due = (long long)((uint64_t)STACK_ARG(1)
                              | ((uint64_t)STACK_ARG(2) << 32));
    kernel_set_timer(STACK_ARG(0), due, 0, STACK_ARG(3));
}

void bridge_KeSetTimerEx(void)
{
    long long due = (long long)((uint64_t)STACK_ARG(1)
                              | ((uint64_t)STACK_ARG(2) << 32));
    kernel_set_timer(STACK_ARG(0), due, (long)STACK_ARG(3), STACK_ARG(4));
}

/* Drop a timer so it stops firing. Returns whether it was armed. */
int xbox_kernel_cancel_timer(uint32_t timer_va)
{
    int i, was_set = 0;

    if (!g_timer_started)
        return 0;
    EnterCriticalSection(&g_timer_lock);
    for (i = 0; i < XBOX_MAX_TIMERS; i++)
        if (g_timers[i].timer_va == timer_va) {
            g_timers[i].timer_va = 0;
            was_set = 1;
        }
    LeaveCriticalSection(&g_timer_lock);
    return was_set;
}
