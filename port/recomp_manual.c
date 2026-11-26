#include <time.h>
/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * An override goes here only to trace a function (wrap the generated
 * version), or when the game function's own logic is the root cause
 * (docs/project-rules.md, Where code goes 6). A function the lifter got
 * wrong is fixed in the recompiler (tools/recomp), and a crash is fixed
 * where it starts: nothing is stubbed out, and no call is intercepted here
 * to make up for a bug in another layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "platform/recomp_check.h"
static double jsrf_now(void);   /* defined further down */

#if defined(_WIN32)
/* No pwrite on Windows: a seek and a write do the same for the one writer here. */
#include <io.h>
static long long jsrf_pwrite(int fd, const void *buf, size_t n, long long off)
{
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    return _write(fd, buf, (unsigned)n);
}
#define pwrite jsrf_pwrite
#endif

/* The Win32 shim's thread id, so traces here and the kernel's [WAITLOG]
 * name the same thread. (On Windows it is the real one, from <windows.h>.) */
#if !defined(_WIN32)
extern uint32_t GetCurrentThreadId(void);
#else
#include "platform/xbox_winnt.h"     /* <windows.h>, as the runtime includes it */
#endif

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

#include "recomp/gen/recomp_types.h"
extern ptrdiff_t g_xbox_mem_offset;

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
int g_icall_verbose;
/* In-memory ring of indirect calls: what was called, from where, and what it
 * did to esp. Dumped by the crash handler; never printed live (printing from
 * inside guest code perturbs the run). */
#define ICALLV_RING 256
static struct { uint32_t va, esp0, esp1, eax, ra; } g_icallv[ICALLV_RING];
static unsigned g_icallv_n;
void recomp_icallv_record(uint32_t va, uint32_t esp0, uint32_t esp1, uint32_t eax, uint32_t ra)
{
    unsigned i = g_icallv_n++ & (ICALLV_RING - 1);
    g_icallv[i].va = va; g_icallv[i].esp0 = esp0; g_icallv[i].esp1 = esp1; g_icallv[i].eax = eax; g_icallv[i].ra = ra;
}
void recomp_icallv_dump(void)
{
    unsigned n = g_icallv_n < ICALLV_RING ? g_icallv_n : ICALLV_RING;
    fprintf(stderr, "[ICALLV] last %u indirect calls (oldest first):\n", n);
    for (unsigned k = 0; k < n; k++) {
        unsigned i = (g_icallv_n - n + k) & (ICALLV_RING - 1);
        fprintf(stderr, "   %08X from %08X esp %08X->%08X (%+d) eax=%08X\n",
                g_icallv[i].va, g_icallv[i].ra, g_icallv[i].esp0, g_icallv[i].esp1,
                (int)(g_icallv[i].esp1 - g_icallv[i].esp0), g_icallv[i].eax);
    }
}
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 *
 * Said in full the first 4 times for an address, then once every 1000: a
 * missing callback runs every frame, and 1590 copies of the same 18 lines
 * made a player's log 1.4 MB without saying anything the first copy had not.
 */
void recomp_icall_fail_log(uint32_t va)
{
    enum { SLOTS = 64 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int nseen;
    uint64_t n = 0;
    int k;
    for (k = 0; k < nseen && seen[k] != va; k++)
        ;
    if (k == nseen && nseen < SLOTS) { seen[k] = va; hits[k] = 0; nseen++; }
    if (k < nseen) n = ++hits[k];
    if (n > 4 && n % 1000) return;

    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);
    if (n > 4) {
        fprintf(stderr, "  (%llu times for this address)\n", (unsigned long long)n);
        fflush(stderr);
        return;
    }

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Where from: the return address the lifted call pushed is at [esp]. */
    {
        static uint32_t sites[64]; static int nsites;
        uint32_t ra = *(uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp);
        int k; for (k = 0; k < nsites; k++) if (sites[k] == ra) break;
        if (k == nsites && nsites < 64) {
            sites[nsites++] = ra;
            fprintf(stderr, "[ICALL] not-code target 0x%08X called from site 0x%08X\n", va, ra);
        }
    }
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);

    /*
     * The first one, in full.
     *
     * This line turned out to be the precursor to the crash that kills a run
     * now and then: a scene-graph walk calls a virtual on every node it
     * visits, one node's vtable slot reads as zero, the call is skipped, and
     * a few instructions later the walk dereferences a sibling pointer of
     * 0x8B28246C and dies. Waiting for the fault to report it is the wrong
     * end of the problem -- by then the object is three frames of registers
     * away. Here, the object is still in ecx and its vtable is still in eax.
     *
     * Once per address, and only the first time, so a wild pointer inside a
     * loop cannot turn this into the log.
     */
    if (hits[i] == 1) {
        uint32_t obj = g_ecx, vtbl = g_eax;
        fprintf(stderr, "  [ICALL] this=0x%08X vtable=0x%08X "
                "esi=0x%08X edi=0x%08X esp=0x%08X\n",
                obj, vtbl, g_esi, g_edi, g_esp);
        if (g_xbox_mem_offset && obj >= 0x10000u && obj < (64u << 20)) {
            const uint32_t *o = (const uint32_t *)
                                ((uintptr_t)g_xbox_mem_offset + obj);
            int k;
            fprintf(stderr, "  [ICALL] the object it was called on:\n");
            for (k = 0; k < 16; k += 4)
                fprintf(stderr, "    +%02X: %08X %08X %08X %08X\n",
                        k * 4, o[k], o[k+1], o[k+2], o[k+3]);
        }
        if (g_xbox_mem_offset && g_esp) {
            const uint32_t *sp = (const uint32_t *)
                                 ((uintptr_t)g_xbox_mem_offset + g_esp);
            int shown = 0, k;
            fprintf(stderr, "  [ICALL] guest stack (return addresses):\n");
            for (k = 0; k < 256 && shown < 16; k++) {
                uint32_t v = sp[k];
                if (v > g_xbox_code_lo && v < g_xbox_code_hi) {
                    fprintf(stderr, "    [esp+%-4d] 0x%08X\n", k * 4, v);
                    shown++;
                }
            }
        }
    }
    fflush(stderr);
}

/* ── RCPSS and RSQRTSS ───────────────────────────────────────────────────
 *
 * The runtime works out the Xbox's answers (recomp_rcpss and recomp_rsqrtss
 * in recomp_types.h). One line at the start says whether this processor's
 * own instructions give the same: they do on Intel's, and not on AMD's, where
 * the game ran differently before (known bug 60). */
void recomp_sse_approx_log(void);
void recomp_sse_approx_log(void)
{
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    unsigned checked, differ = recomp_sse_approx_host_differs(&checked);
    if (differ)
        fprintf(stderr, "[SSE] rcpss and rsqrtss: the Xbox's answers, worked out; this "
                "processor's own differ at %u of %u inputs checked\n", differ, checked);
    else
        fprintf(stderr, "[SSE] rcpss and rsqrtss: the Xbox's answers, worked out; this "
                "processor's own give the same at all %u inputs checked\n", checked);
#else
    fprintf(stderr, "[SSE] rcpss and rsqrtss: the Xbox's answers, worked out\n");
#endif
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}


/* ---- Heap tracing wrappers (bring-up) ---------------------------------- */
#define GARG(n) (*(uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp + 4 * (n)))
static int g_heap_trace = -1;
static uint32_t g_last_freed;
static int heap_trace_on(void) { if (g_heap_trace < 0) g_heap_trace = getenv("JSRF_HEAP_TRACE") ? 1 : 0; return g_heap_trace; }
extern void sub_00149407_gen(void);
extern void sub_001497DC_gen(void);
extern void sub_00149F5E_gen(void);
extern void sub_0014A12E_gen(void);
void sub_00149407(void) { /* RtlCreateHeap */
    uint32_t f=GARG(1), base=GARG(2), res=GARG(3), com=GARG(4);
    sub_00149407_gen();
    if (heap_trace_on()) fprintf(stderr, "[HEAPX] RtlCreateHeap(flags=%X base=%08X reserve=%X commit=%X) = %08X\n", f, base, res, com, g_eax);
}
void sub_001497DC(void) { /* RtlAllocateHeap(heap, flags, size) */
    uint32_t h=GARG(1), f=GARG(2), sz=GARG(3), ra=GARG(0);
    sub_001497DC_gen();
    if (g_eax == g_last_freed) g_last_freed = 0;
    if (heap_trace_on()) fprintf(stderr, "[HEAPX]   (alloc from %08X)\n", ra);
    if (heap_trace_on()) fprintf(stderr, "[HEAPX] alloc(h=%08X f=%X size=%X) = %08X\n", h, f, sz, g_eax);
}
/* The block freed last, freed again with nothing handed out between, is a
 * double free: said once, with the caller, and the game's own RtlFreeHeap
 * then runs, as it would on the console. A check, not a stop: the game goes
 * on (src/platform/recomp_check.h). */
void sub_00149F5E(void) { /* RtlFreeHeap(heap, flags, ptr) */
    uint32_t h=GARG(1), f=GARG(2), p=GARG(3);
    if (heap_trace_on()) fprintf(stderr, "[HEAPX] free(h=%08X f=%X ptr=%08X) from %08X\n", h, f, p, GARG(0));
    RECOMP_CHECK(!p || p != g_last_freed, "the block at %08X freed again, from %08X", p, GARG(0));
    g_last_freed = p;
    sub_00149F5E_gen();
}
void sub_0014A12E(void) { /* RtlReAllocateHeap(heap, flags, ptr, size) */
    uint32_t h=GARG(1), f=GARG(2), p=GARG(3), sz=GARG(4);
    sub_0014A12E_gen();
    if (g_eax == g_last_freed) g_last_freed = 0;   /* handed out again */
    if (heap_trace_on()) fprintf(stderr, "[HEAPX] realloc(h=%08X f=%X ptr=%08X size=%X) = %08X\n", h, f, p, sz, g_eax);
}

/* ---- CRT memcpy (sub_0017CEC0) ------------------------------------------
 * MSVC's memcpy.asm dispatches its tail copies through jump tables indexed
 * from base+4 and, on the overlap path, with a negated index -- neither of
 * which the disassembler recovers, so the lifted body loses every tail copy.
 * The semantics are memmove's; implement it natively. cdecl: (dst, src, n),
 * returns dst. */
#include <string.h>
void sub_0017CEC0(void)
{
    uint32_t dst = GARG(1), src = GARG(2), n = GARG(3);
    uint8_t *mem = (uint8_t *)g_xbox_mem_offset;
    if (getenv("JSRF_MEMCPY_WATCH") && n >= 1024 &&
        ((dst < 0x27E074 && dst + n > 0x1EB760) || dst >= 0x04000000)) {
        fprintf(stderr, "[MEMCPY] dst=%08X src=%08X n=%u from %08X -- guest stack:\n", dst, src, n, GARG(0));
        const uint32_t *sp = (const uint32_t *)(mem + g_esp);
        for (int i = 0; i < 120; i++)
            if (sp[i] > 0x11000 && sp[i] < 0x1C3F60) fprintf(stderr, "    [esp+%03X] %08X\n", i*4, sp[i]);
        fflush(stderr);
    }
    if (n) memmove(mem + dst, mem + src, n);
    g_eax = dst;
    g_esp += 4;   /* ret: pop the return address */
}

/* ---- sub_0013B180: JSRF's spin thread --------------------------------------
 * `while (!g_exit) g_counter++;` at raised priority: it never blocks and calls
 * nothing, so under the guest scheduler lock it would hold the CPU forever.
 * It touches nothing shared, so run it outside the lock. */
extern void sub_0013B180_gen(void);
void sub_0013B180(void)
{
    extern void xbox_guest_lock_mark_idle(void);
    xbox_guest_lock_mark_idle();
    /* The loop itself, natively, with a sleep: the guest spins
     * `while (!exit) counter++` at raised priority, which on a two-core host
     * starved every other thread. Nothing reads the counter's rate. */
    {
        uint8_t *m = (uint8_t *)g_xbox_mem_offset;
        volatile uint32_t *exit_flag = (volatile uint32_t *)(m + 0x25EFC0);
        volatile uint32_t *counter = (volatile uint32_t *)(m + 0x25EFA8);
        while (!*exit_flag) {
            (*counter)++;
            struct timespec ts = { 0, 2000000L };
            nanosleep(&ts, NULL);
        }
    }
    sub_0013B180_gen();   /* sees the flag set: goes straight to ExitThread */
}

/* ---- sub_00012770: the "fatal error" handler (writes JSRF_FATAL.ERR) ------
 * Bring-up diagnostic: dump the guest call chain that reached it. */
extern void sub_00012770_gen(void);
void sub_00012770(void)
{
    const uint32_t *sp = (const uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp);
    fprintf(stderr, "[FATALX] sub_00012770 reached, ecx=%08X -- guest stack code refs:\n", g_ecx);
    for (int i = 0; i < 400; i++)
        if (sp[i] > 0x11000 && sp[i] < 0x1C3F60) fprintf(stderr, "    [esp+%03X] %08X\n", i*4, sp[i]);
    fflush(stderr);
    sub_00012770_gen();
}

/* ---- sub_00143240: CRI ADX error reporter (ADXERR) -- print what it says */
extern void sub_00143240_gen(void);
void sub_00143240(void)
{
    const char *fmt = (const char *)((uint8_t *)g_xbox_mem_offset + GARG(1));
    fprintf(stderr, "[ADXERR] fmt=\"%s\"\n", fmt);
    sub_00143240_gen();
    fprintf(stderr, "[ADXERR] msg=\"%s\"\n", (const char *)((uint8_t *)g_xbox_mem_offset + 0x26a280));
    fflush(stderr);
}

/* ---- sub_0006F730: raise the "disc error" screen; eax = ADXT error code */
#include "adx_err.h"
extern void sub_0006F730_gen(void);
void sub_0006F730(void)
{
    fprintf(stderr, "[ADXTRAP] sub_0006F730: ADXT error code %d (eax=%08X), raised from %08X (255AD: save writer, 664C3: sub_000257B0 state, 116EA8: ADX handle status)\n",
            (int)(int16_t)g_eax, g_eax, MEM32(g_esp));
    if (adx_err_on()) adx_err_raised(jsrf_now(), (int)(int16_t)g_eax);
    fflush(stderr);
    sub_0006F730_gen();
}

/* ---- sub_001403B0: JSRF cvFs device ReqRd(handle, nsct, buf) diagnostic */
extern void sub_001403B0_gen(void);
void sub_001403B0(void)
{
    uint32_t h = GARG(1), nsct = GARG(2), buf = GARG(3);
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    if (h)
        fprintf(stderr, "[CVFS] ReqRd h=%08X state=%u nsct=%d buf=%08X sctsz=%u total=%u pos=%u\n",
                h, m[h + 1], (int)nsct, buf, *(uint32_t *)(m + h + 0xc),
                *(uint32_t *)(m + h + 0x14), *(uint32_t *)(m + h + 0x18));
    sub_001403B0_gen();
    if (h) fprintf(stderr, "[CVFS]   -> %d (state now %u)\n", (int)g_eax, m[h + 1]);
}
/* ---- the cvFs read pipeline, for the stalled tutorial music.
 * sub_001407E0(h): issues ReadFileEx for the pending request (+0x140 offset,
 * +0x144 bytes) with completion routine sub_001401B0, which clears the
 * in-flight flag +0x14C; sub_00140BA0(h), the per-handle server, SleepEx(0,
 * TRUE)s while +0x14C is set, then accounts the bytes and sets state 1. */
static void cvfs_line(const char *what, uint32_t h, int extra)
{
    static int lines;
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    if (!h || h + 0x150 >= (64u << 20) || lines >= 3000) return;
    lines++;
    fprintf(stderr, "[CVFS2] t=%.2f tid %lu %s h=%08X state=%u pend148=%u inflight14c=%u off=%u bytes=%u pos=%u total=%u %d\n",
            jsrf_now(), (unsigned long)GetCurrentThreadId(), what, h, m[h + 1],
            *(uint32_t *)(m + h + 0x148), *(uint32_t *)(m + h + 0x14c),
            *(uint32_t *)(m + h + 0x140), *(uint32_t *)(m + h + 0x144),
            *(uint32_t *)(m + h + 0x18), *(uint32_t *)(m + h + 0x14), extra);
}
extern void sub_001407E0_gen(void);
void sub_001407E0(void)
{
    uint32_t h = GARG(1);
    cvfs_line("issue>", h, 0);
    sub_001407E0_gen();
    cvfs_line("issue<", h, (int)g_eax);
}
extern void sub_001401B0_gen(void);
void sub_001401B0(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t err = GARG(1), bytes = GARG(2), ov = GARG(3);
    uint32_t h = (ov && ov + 0x14 < (64u << 20)) ? *(uint32_t *)(m + ov + 0x10) : 0;
    cvfs_line("done>", h, (int)bytes);
    (void)err;
    sub_001401B0_gen();
    cvfs_line("done<", h, (int)err);
}
extern void sub_00140BA0_gen(void);
void sub_00140BA0(void)
{
    /* Only the interesting calls: a request pending or in flight. */
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t h = GARG(1);
    int busy = h && h + 0x150 < (64u << 20) && m[h + 1] == 2;
    static unsigned quiet;
    if (busy && (quiet++ % 64) == 0) cvfs_line("serve>", h, (int)quiet);
    sub_00140BA0_gen();
    if (busy && m[h + 1] != 2) cvfs_line("serve< completed", h, 0);
}

/* ---- sub_001402F0: cvFs device Seek(handle, pos) */
extern void sub_001402F0_gen(void);
void sub_001402F0(void)
{
    uint32_t h = GARG(1), pos = GARG(2);
    fprintf(stderr, "[CVFS] Seek h=%08X pos=%d\n", h, (int)pos);
    sub_001402F0_gen();
}

/* ---- sub_0013D300: ADXT trap/watchdog check -- print the handle's counters */
extern void sub_0013D300_gen(void);
void sub_0013D300(void)
{
    uint32_t h = GARG(1);
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    /* Print a handle's line when any of its fields change (and every 5 s),
     * for the whole run -- the old first-60 cap spent itself on the title
     * and said nothing about the tutorial's stream. On a new stream handle,
     * dump its first words once so the cvFs handle it reads through shows. */
    {
        static struct { uint32_t h, sig[6]; double t; uint32_t stm; } seen[8];
        static int lines;
        if (h && h + 0x80 < (64u << 20) && lines < 2000) {
            uint32_t sig[6];
            int i, slot = -1, changed;
            double now = jsrf_now();
            sig[0] = (uint8_t)m[h + 1]; sig[1] = *(uint16_t *)(m + h + 0x60);
            sig[2] = *(uint16_t *)(m + h + 0x68); sig[3] = *(uint16_t *)(m + h + 0x6a);
            sig[4] = m[h + 0x6d]; sig[5] = *(uint32_t *)(m + h + 0x14);
            for (i = 0; i < 8; i++) if (seen[i].h == h) { slot = i; break; }
            if (slot < 0) for (i = 0; i < 8; i++) if (!seen[i].h) { slot = i; seen[i].h = h; break; }
            if (slot >= 0) {
                changed = memcmp(seen[slot].sig, sig, sizeof sig) != 0;
                if (changed || now - seen[slot].t >= 5.0) {
                    lines++;
                    fprintf(stderr, "[ADXT] t=%.1f h=%08X state=%d thresh[0x38]=%d err[0x60]=%d cnt68=%d cnt6a=%d mode6d=%d stm=%08X%s\n",
                            now, h, (int8_t)m[h + 1], *(int32_t *)(m + h + 0x38), *(int16_t *)(m + h + 0x60),
                            *(int16_t *)(m + h + 0x68), *(int16_t *)(m + h + 0x6a), m[h + 0x6d], sig[5],
                            changed ? "  <- changed" : "");
                    if (sig[5] && sig[5] != seen[slot].stm && sig[5] + 0x40 < (64u << 20)) {
                        const uint32_t *w = (const uint32_t *)(m + sig[5]);
                        fprintf(stderr, "[ADXT]   stm %08X: %08X %08X %08X %08X %08X %08X %08X %08X  %08X %08X %08X %08X %08X %08X %08X %08X\n",
                                sig[5], w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7],
                                w[8], w[9], w[10], w[11], w[12], w[13], w[14], w[15]);
                        seen[slot].stm = sig[5];
                    }
                    memcpy(seen[slot].sig, sig, sizeof sig);
                    seen[slot].t = now;
                }
            }
        }
    }
    /*
     * JSRF_ADX_RATE=1: how often the ADXT server runs, against how often it
     * decides to decode.
     *
     * The voice consumes 44,100 samples a second and the title supplies
     * 1,152 to 4,608 of them, so the music plays as correct fragments in the
     * wrong order. Two quite different faults produce that. If this watchdog
     * runs 38 times a second and decodes 3, the library believes the buffer
     * is full and the play position it reads is wrong. If it runs 3 times a
     * second, nothing is wrong with the library and its thread is starved.
     */
    { static int on = -1; static int runs; static uint64_t last_ms;
      if (on < 0) on = getenv("JSRF_ADX_RATE") ? 1 : 0;
      if (on) {
          struct timespec ts; uint64_t now;
          clock_gettime(CLOCK_MONOTONIC, &ts);
          now = (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
          runs++;
          if (!last_ms) last_ms = now;
          if (now - last_ms >= 1000) {
              fprintf(stderr, "[ADXSERV] watchdog ran %d times/s on tid %lu\n",
                      runs, (unsigned long)GetCurrentThreadId());
              fflush(stderr); runs = 0; last_ms = now;
          }
      } }
    /* JSRF_ADX_STALL=1: while an ADXT handle sits in state 1 (preparing)
     * past a second, dump its stream (+0x14) and the cvFs handle the stream
     * reads through -- what the stream is waiting for. */
    { static int on = -1, dumps;
      if (on < 0) on = getenv("JSRF_ADX_STALL") ? 1 : 0;
      if (on && h && h + 0x80 < (64u << 20) && dumps < 12) {
          uint8_t *mm = (uint8_t *)g_xbox_mem_offset;
          int8_t st = (int8_t)mm[h + 1];
          int16_t c6a = *(int16_t *)(mm + h + 0x6a);
          uint32_t stm = *(uint32_t *)(mm + h + 0x14);
          if (st == 1 && c6a > 0 && (c6a % 90) == 60 && stm && stm + 0x100 < (64u << 20)) {
              const uint32_t *w = (const uint32_t *)(mm + stm);
              const uint32_t *hw = (const uint32_t *)(mm + h);
              int i;
              dumps++;
              fprintf(stderr, "[ADXSTALL] t=%.1f h=%08X cnt6a=%d\n[ADXSTALL]  adxt:", jsrf_now(), h, c6a);
              for (i = 0; i < 32; i++) fprintf(stderr, " %08X", hw[i]);
              fprintf(stderr, "\n[ADXSTALL]  sj %08X:", stm);
              for (i = 0; i < 16; i++) fprintf(stderr, " %08X", w[i]);
              fprintf(stderr, "\n");
              /* +8 is the ADXSTM (the stream), +4 the decoder, +0xC the
               * sound output (CRI ADX_TALK layout); dump each object. */
              { int f; static const int offs[3] = { 8, 4, 0xC };
                for (f = 0; f < 3; f++) {
                    uint32_t o = hw[offs[f] / 4];
                    if (o && o + 0x80 < (64u << 20)) {
                        const uint32_t *ow = (const uint32_t *)(mm + o);
                        fprintf(stderr, "[ADXSTALL]  +%02X -> %08X:", offs[f], o);
                        for (i = 0; i < 32; i++) fprintf(stderr, " %08X", ow[i]);
                        fprintf(stderr, "\n");
                    }
                } }
              { uint32_t fs = 0x00273780u; const uint8_t *f = mm + fs;
                fprintf(stderr, "[ADXSTALL]  cvfs %08X: inuse %u state %u off %u bytes %u pend %u inflight %u\n",
                        fs, f[0], f[1], *(const uint32_t *)(f + 0x140), *(const uint32_t *)(f + 0x144),
                        *(const uint32_t *)(f + 0x148), *(const uint32_t *)(f + 0x14c)); }
              /* CRI's Xbox file layer formats its last error here (wxCi*). */
              fprintf(stderr, "[ADXSTALL]  wxci last error: '%.160s'\n", (const char *)(mm + 0x00273640u));
              fflush(stderr);
          }
      } }
    sub_0013D300_gen();
}

/* ---- the watchdog's recoveries (port/adx_err.c) ---------------------------
 * The watchdog above writes a handle's error code, runs the recovery the
 * handle was given -- ADXT_Stop (sub_0013A6B0) or a restart from the top
 * (sub_0013D250) -- and writes 0 back. So a recovery that finds the code set
 * is the watchdog's: the game's own stops and restarts find it 0.
 * JSRF_ADX_ERR_LOG=1 logs each of the watchdog's, and which threads took the
 * guest lock while it ran. */
#define ADX_GUEST_LOW    0x10000u      /* below it, no object: a null, or a small number */
#define ADX_GUEST_RAM    (64u << 20)   /* the guest's memory, as the hooks above bound it */
#define ADXT_ERR_CODE    0x60u         /* int16: -1 no input, -2 decoding stuck */
#define ADXT_STREAM      0x08u         /* the handle's file stream (ADXSTM) */
#define ADXSTM_CVFS      0x08u         /* the stream's cvFs handle */
#define CVFS_DEVICE      0x04u         /* the cvFs handle's device handle, the [CVFS] lines' h= */
#define CVFS_DEV_SECTORS 0x14u         /* the device handle's file size, in sectors */
#define ADXCRS_DEPTH     0x0025EFECu   /* how deep CRI's lock is held (sub_0013C460 counts it) */
static uint32_t adx_word(uint32_t va)
{
    return va >= ADX_GUEST_LOW && va + 4 <= ADX_GUEST_RAM ? MEM32(va) : 0;
}
static void adx_recovery(int restart, void (*gen)(void))
{
    uint32_t h = GARG(1);
    int code = 0;
    if (adx_err_on() && h >= ADX_GUEST_LOW && h + ADXT_ERR_CODE + 2 <= ADX_GUEST_RAM)
        code = *(int16_t *)((uint8_t *)g_xbox_mem_offset + h + ADXT_ERR_CODE);
    if (code) {
        uint32_t dev = adx_word(adx_word(adx_word(h + ADXT_STREAM) + ADXSTM_CVFS) + CVFS_DEVICE);
        adx_err_recovery_begin(jsrf_now(), h, code, restart, MEM32(g_esp), dev,
                               adx_word(dev + CVFS_DEV_SECTORS), (int)adx_word(ADXCRS_DEPTH));
    }
    gen();
    if (code) adx_err_recovery_end(h);
}
extern void sub_0013A6B0_gen(void);
void sub_0013A6B0(void)
{
    adx_recovery(0, sub_0013A6B0_gen);
}
extern void sub_0013D250_gen(void);
void sub_0013D250(void)
{
    adx_recovery(1, sub_0013D250_gen);
}
/* CRI's lock and unlock, at the layer under sub_0013C460 that raises its
 * holder to time-critical and resumes the spinner (sub_0013B180), and puts
 * both back. CRI's thread setup installs them as its lock functions
 * (0x13B377), so they're reached through a pointer. JSRF_ADX_ERR_LOG=1
 * counts the takes of the guest lock by other threads while it's held. */
#define ADXM_LOCK_DEPTH  0x0025EFA0u   /* how deep this layer is held (sub_0013B0A0 counts it) */
extern void sub_0013B0A0_gen(void);
void sub_0013B0A0(void)
{
    int on = adx_err_on();
    if (on) adx_err_crs_lock(jsrf_now(), (int)adx_word(ADXM_LOCK_DEPTH));
    sub_0013B0A0_gen();
    if (on) adx_err_crs_lock_end();
}
extern void sub_0013B0E0_gen(void);
void sub_0013B0E0(void)
{
    int on = adx_err_on();
    if (on) adx_err_crs_unlock((int)adx_word(ADXM_LOCK_DEPTH));
    sub_0013B0E0_gen();
    if (on) adx_err_crs_unlock_end(jsrf_now(), (int)adx_word(ADXM_LOCK_DEPTH));
}

/* ---- sub_00011070: scene-graph walk -- validate node pointers first, so
 * the holder of a corrupted link is named before the walk dereferences it. */
extern void sub_00011070_gen(void);
static int jsrf_node_ok(uint32_t p) { return p >= 0x10000 && p < 0x04000000 && (p & 3) == 0; }
static void jsrf_check_tree(uint32_t node, int depth, uint32_t holder, const char *field)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    int guard = 0;
    while (node) {
        if (!jsrf_node_ok(node)) {
            fprintf(stderr, "[TREE] bad node %08X held at %08X (%s) depth %d -- guest stack:\n", node, holder, field, depth);
            { const uint32_t *sp = (const uint32_t *)(m + g_esp);
              for (int i = 0; i < 200; i++)
                  if (sp[i] > 0x11000 && sp[i] < 0x1C3F60) fprintf(stderr, "    [esp+%03X] %08X\n", i*4, sp[i]); }
            fflush(stderr);
            abort();
        }
        if (depth < 64 && *(uint32_t *)(m + node + 0x28))
            jsrf_check_tree(*(uint32_t *)(m + node + 0x28), depth + 1, node + 0x28, "child");
        holder = node + 0x30; field = "sibling";
        node = *(uint32_t *)(m + node + 0x30);
        if (++guard > 100000) break;
    }
}
/* JSRF_TASK_DUMP="t1,t2,..." (test only): at each time, one [TASK] line per
 * node of the task tree the frame walks -- depth, address, vtable, its update
 * and draw entries, and the words at +4 (flags: bit 31 set = not updated),
 * +8 and +0xC -- so two runs can be set side by side to see which tasks one
 * of them has switched off. */
#include "pad.h"   /* pad_now, the clock the test hooks share */
static void jsrf_dump_tree(uint32_t node, int depth)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    int guard = 0;
    while (node && jsrf_node_ok(node) && guard++ < 4000) {
        const uint32_t *w = (const uint32_t *)(m + node);
        uint32_t vt = w[0], upd = 0, drw = 0;
        if (jsrf_node_ok(vt)) { upd = *(uint32_t *)(m + vt + 4); drw = *(uint32_t *)(m + vt + 0xC); }
        fprintf(stderr, "[TASK] %*s%08X vt %08X upd %08X drw %08X f4 %08X f8 %08X fC %08X f10 %08X f14 %08X f18 %08X f1C %08X f20 %08X f24 %08X\n",
                depth * 2, "", node, vt, upd, drw, w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9]);
        if (depth < 32 && w[0x28 / 4]) jsrf_dump_tree(w[0x28 / 4], depth + 1);
        node = w[0x30 / 4];
    }
}
void sub_00011070(void)
{
    static int check = -1, depth;
    static double dump_t[16]; static int ndump = -1, idump;
    if (check < 0) {
        const char *e = getenv("JSRF_TASK_DUMP");
        check = getenv("JSRF_TREE_CHECK") ? 1 : 0;
        ndump = 0;
        while (e && *e && ndump < 16) {
            char *end; double v = strtod(e, &end);
            if (end == e) break;
            dump_t[ndump++] = v;
            e = (*end == ',') ? end + 1 : end;
        }
    }
    if (check && g_ecx) jsrf_check_tree(g_ecx, 0, 0, "root");
    if (depth == 0 && idump < ndump && g_ecx && pad_now() >= dump_t[idump]) {
        idump++;
        fprintf(stderr, "[TASK] ---- t=%.1f root %08X ----\n", pad_now(), g_ecx);
        jsrf_dump_tree(g_ecx, 0);
        fflush(stderr);
    }
    depth++;
    sub_00011070_gen();
    depth--;
}

/* ---- sub_000128C0: game-manager object table lookup -- flag garbage slots */
extern void sub_000128C0_gen(void);
void sub_000128C0(void)
{
    uint32_t idx = GARG(1), gm = g_ecx;
    sub_000128C0_gen();
    if (g_eax && !jsrf_node_ok(g_eax) && getenv("JSRF_TREE_CHECK")) {
        uint8_t *m = (uint8_t *)g_xbox_mem_offset;
        fprintf(stderr, "[OBJTAB] slot %d at %08X holds garbage %08X (gm=%08X) -- guest stack:\n",
                (int)idx, gm + 0x98 + idx * 4, g_eax, gm);
        const uint32_t *sp = (const uint32_t *)(m + g_esp);
        for (int i = 0; i < 200; i++)
            if (sp[i] > 0x11000 && sp[i] < 0x1C3F60) fprintf(stderr, "    [esp+%03X] %08X\n", i*4, sp[i]);
        fflush(stderr);
        abort();
    }
}

/* ---- corruption hunt: poll the sound manager's buffer-pointer array on every
 * traced function entry (JSRF_WATCH_SNDMGR=<va of manager>)
 *
 * The hook below is on the entry of every recompiled function, so the runtime
 * only calls it when something has asked for it -- see RECOMP_TRACE_ENTER.
 * These three switches are what "asked for it" means here, and the
 * constructor tells the runtime so before the first guest function runs. */
__attribute__((constructor))
static void jsrf_hook_switch(void)
{
    extern void recomp_trace_enable_hooks(void);
    if (getenv("JSRF_WATCH_SNDMGR") || getenv("JSRF_DEEP_ESP")
     || getenv("JSRF_BONE_CHECK") || getenv("JSRF_CRI_CALLS")
     || getenv("JSRF_ROBO_LOG"))
        recomp_trace_enable_hooks();
}

/* JSRF_CRI_CALLS=1 (test only): which functions of the CRI library
 * (0x138000-0x145000) the game calls, and from where. Every 10 s, the pairs
 * called since the last report, with counts. */
static void cri_census(uint32_t va)
{
    static int on = -1;
    static struct { uint32_t va, ra; unsigned n, shown; } tab[768];
    static int ntab; static double next;
    uint32_t ra; int i;
    if (on < 0) on = getenv("JSRF_CRI_CALLS") ? 1 : 0;
    if (!on || va < 0x00138000u || va >= 0x00145000u) return;
    ra = *(uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp);
    if (ra >= 0x00138000u && ra < 0x00145000u) return;
    for (i = 0; i < ntab; i++) if (tab[i].va == va && tab[i].ra == ra) { tab[i].n++; break; }
    if (i == ntab && ntab < 768) { tab[i].va = va; tab[i].ra = ra; tab[i].n = 1; tab[i].shown = 0; ntab++; }
    if (pad_now() >= next) {
        next = pad_now() + 10.0;
        fprintf(stderr, "[CRI] t=%.1f calls since the last report:\n", pad_now());
        for (i = 0; i < ntab; i++)
            if (tab[i].n != tab[i].shown) {
                fprintf(stderr, "[CRI]   sub_%08X <- %08X x%u\n", tab[i].va, tab[i].ra, tab[i].n - tab[i].shown);
                tab[i].shown = tab[i].n;
            }
        fflush(stderr);
    }
}

/* JSRF_ROBO_LOG=<seconds> (test only): the Rokkaku robots built on
 * Media/Enemy/Robo. Enemy type 27 (made by sub_000ECF50; per-frame update
 * sub_000ECDB0, state at +0x128) is the Terror Drone of the Urban Battle at
 * the future site of the Expo Stadium; type 12 (made by sub_000EAC20; its
 * brain's update is sub_000EA6A0, state at +8) is the other robot. One line
 * per robot whenever its state changes, every that many seconds otherwise,
 * and every few frames while it attacks (states 2 and 3); with the player's
 * position, the robot's grind rails (its component at +0xBC keeps their
 * handles in the rail table at 0x0022FCE8, see sub_000179B0; each rail's
 * bounds at +0x10/+0x1C, its points at +4, how many at +0x28) and its four
 * graffiti spots (+0x174, 16 bytes each). One line too for each enemy a
 * mission creates (sub_000C0B30), with its type.
 * JSRF_ROBO_RIDE=<seconds>[:<rail>[:<point>]][,...] (with the log): at each
 * of those times (up to 8), put the player 40 units above a point of that
 * rail of the drone (rail 0 by default; the point by its index, or -1 for
 * the lowest, the default, -2 for the highest) -- to see whether landing on
 * it starts a grind -- and log the player's position every 0.2 s for 5 s
 * after. */
static float s_player_pos[3];
static uint32_t s_player_va;
static uint32_t robo_rail(const uint8_t *m, uint32_t h)
{
    uint32_t p;
    int guard;
    if (h == 0xFFFFFFFFu) return 0;
    p = *(const uint32_t *)(m + 0x0022FCE8u + 0x890u + (h & 0xFFu) * 4u);
    for (guard = 0; p >= 0x10000u && p < (64u << 20) && guard < 256; guard++) {
        if (*(const uint32_t *)(m + p + 0x38) == h) return p;
        p = *(const uint32_t *)(m + p + 0x94);
    }
    return 0;
}
static void jsrf_robo_log(uint32_t va)
{
    static double period = -1, ride_t[8], ride_until = -1, ride_next;
    static int ride_rail[8], ride_pt[8], ride_n, ride_cur;
    static struct { uint32_t obj, state, calls; double next; } r[16];
    const uint8_t *m;
    const uint32_t *w;
    const float *f;
    const uint32_t *cw;
    const float *cf;
    uint32_t obj, st, comp;
    int i, k, fine, t27, nrails;
    double now;
    if (va != 0x000ECDB0u && va != 0x000EA6A0u && va != 0x000C0B30u) return;
    if (period < 0) {
        const char *e = getenv("JSRF_ROBO_LOG");
        period = e ? atof(e) : 0;
        e = getenv("JSRF_ROBO_RIDE");
        while (e && *e && ride_n < 8) {
            char *x;
            ride_t[ride_n] = strtod(e, &x);
            if (x == e) break;
            ride_rail[ride_n] = *x == ':' ? (int)strtol(x + 1, &x, 10) : 0;
            ride_pt[ride_n] = *x == ':' ? (int)strtol(x + 1, &x, 10) : -1;
            ride_n++;
            e = *x == ',' ? x + 1 : x;
        }
    }
    if (period <= 0) return;
    m = (const uint8_t *)g_xbox_mem_offset;
    now = pad_now();
    if (va == 0x000C0B30u) {
        uint32_t desc = GARG(2);
        fprintf(stderr, "[ROBO] t=%.1f enemy created: type %d (args %08X %08X)\n", now,
                desc >= 0x10000u && desc < (64u << 20) ? (int)*(const uint32_t *)(m + desc) : -1,
                GARG(1), desc);
        fflush(stderr);
        return;
    }
    obj = g_ecx;
    if (obj < 0x10000u || obj >= (64u << 20)) return;
    w = (const uint32_t *)(m + obj);
    f = (const float *)(m + obj);
    st = va == 0x000ECDB0u ? w[0x128 / 4] : w[0x8 / 4];
    for (i = 0; i < 16 && r[i].obj && r[i].obj != obj; i++)
        ;
    if (i == 16) return;
    if (!r[i].obj) { r[i].obj = obj; r[i].state = 0xFFFFFFFFu; r[i].next = 0; r[i].calls = 0; }
    r[i].calls++;
    t27 = va == 0x000ECDB0u;
    comp = obj + (t27 ? 0xBCu : 0x30u);       /* the rails-and-spots component, sub_000EB830 */
    nrails = t27 ? 2 : 5;
    cw = (const uint32_t *)(m + comp);
    cf = (const float *)(m + comp);
    if (ride_cur < ride_n && now >= ride_t[ride_cur] && s_player_va) {
        int rr = ride_rail[ride_cur], rp = ride_pt[ride_cur];
        uint32_t rl = rr >= 0 && rr < nrails ? robo_rail(m, cw[1 + rr]) : 0;
        ride_cur++;
        if (rl) {
            uint32_t pts = *(const uint32_t *)(m + rl + 4), n = *(const uint32_t *)(m + rl + 0x28), lo = 0;
            if (pts >= 0x10000u && pts < (64u << 20) && n && n < 64) {
                const float *P = (const float *)(m + pts);
                float *pl = (float *)((uint8_t *)g_xbox_mem_offset + s_player_va + 0xCA4);
                if (rp >= 0) lo = (uint32_t)(rp < (int)n ? rp : (int)n - 1);
                else
                    for (k = 1; k < (int)n; k++)
                        if (rp == -2 ? P[k * 6 + 1] > P[lo * 6 + 1] : P[k * 6 + 1] < P[lo * 6 + 1]) lo = (uint32_t)k;
                fprintf(stderr, "[ROBO] t=%.1f ride: player (%.1f %.1f %.1f) -> 40 above point %u of rail %d"
                        " (%.1f %.1f %.1f) of the type-%d robot\n", now, pl[0], pl[1], pl[2], lo, rr,
                        P[lo * 6], P[lo * 6 + 1], P[lo * 6 + 2], t27 ? 27 : 12);
                pl[0] = P[lo * 6]; pl[1] = P[lo * 6 + 1] + 40.0f; pl[2] = P[lo * 6 + 2];
                ride_until = now + 5.0; ride_next = now;
            }
        } else
            fprintf(stderr, "[ROBO] t=%.1f ride: the robot has no rail %d\n", now, rr);
    }
    if (now < ride_until && now >= ride_next) {
        fprintf(stderr, "[ROBO] t=%.2f riding? player at (%.1f %.1f %.1f)\n", now,
                s_player_pos[0], s_player_pos[1], s_player_pos[2]);
        ride_next = now + 0.2;
    }
    fine = t27 && (st == 2 || st == 3) && r[i].calls % 6 == 0;
    if (st == r[i].state && now < r[i].next && !fine) return;
    if (t27)
        fprintf(stderr, "[ROBO] t=%.2f type27 %08X state %u%s count %u idx %u shot %u flags %08X"
                " motion %08X@%.2f %08X@%.2f at (%.1f %.1f %.1f) ground %.1f dist %.1f player (%.1f %.1f %.1f)\n",
                now, obj, st, st != r[i].state ? " (new)" : "", w[0x12C / 4], w[0x134 / 4],
                w[0x138 / 4], w[0x90 / 4], w[0x148 / 4], f[0x154 / 4], w[0x14C / 4], f[0x158 / 4],
                f[0x48 / 4], f[0x4C / 4], f[0x50 / 4], f[0x88 / 4], f[0x74 / 4],
                s_player_pos[0], s_player_pos[1], s_player_pos[2]);
    else {
        uint32_t ent = w[4 / 4];
        const float *ef = ent >= 0x10000u && ent < (64u << 20) ? (const float *)(m + ent + 0x44) : 0;
        fprintf(stderr, "[ROBO] t=%.1f type12 %08X state %u%s +10 %08X +144 %08X at (%.1f %.1f %.1f) ground %.1f"
                " entity %08X at (%.1f %.1f %.1f) flags %08X player (%.1f %.1f %.1f)\n", now, obj, st,
                st != r[i].state ? " (new)" : "", w[0x10 / 4], w[0x144 / 4], f[0x14 / 4], f[0x18 / 4],
                f[0x1C / 4], f[0x2C / 4], ent, ef ? ef[0] : 0.0f, ef ? ef[1] : 0.0f, ef ? ef[2] : 0.0f,
                ef ? *(const uint32_t *)(m + ent + 0xA4) : 0u, s_player_pos[0], s_player_pos[1], s_player_pos[2]);
    }
    if (now >= r[i].next) {
        for (k = 0; k < nrails; k++) {
            uint32_t h = cw[1 + k], rl = robo_rail(m, h);
            if (!rl) { fprintf(stderr, "[ROBO]   rail %d: handle %08X, none\n", k, h); continue; }
            {
                const float *R = (const float *)(m + rl);
                fprintf(stderr, "[ROBO]   rail %d: handle %08X, %u points, (%.1f %.1f %.1f) .. (%.1f %.1f %.1f)\n",
                        k, h, *(const uint32_t *)(m + rl + 0x28), R[0x10 / 4], R[0x14 / 4], R[0x18 / 4],
                        R[0x1C / 4], R[0x20 / 4], R[0x24 / 4]);
            }
        }
        fprintf(stderr, "[ROBO]   graffiti: handles %08X %08X %08X %08X progress %.2f %.2f %.2f %.2f\n",
                cw[0x3C / 4], cw[0x40 / 4], cw[0x44 / 4], cw[0x48 / 4], cf[0x4C / 4], cf[0x50 / 4],
                cf[0x54 / 4], cf[0x58 / 4]);
    }
    fflush(stderr);
    if (st != r[i].state || now >= r[i].next) r[i].next = now + period;
    r[i].state = st;
}

void recomp_trace_user_hook(const char *name, uint32_t va)
{
    cri_census(va);
    jsrf_robo_log(va);
    static int init = -1; static uint32_t mgr; static const char *last_name; static uint32_t last_va;
    if (init < 0) { const char *e = getenv("JSRF_WATCH_SNDMGR"); mgr = e ? (uint32_t)strtoul(e, 0, 0) : 0; init = 1; }
    if (mgr) {
        uint8_t *m = (uint8_t *)g_xbox_mem_offset;
        uint32_t n = *(uint32_t *)(m + mgr + 0x3f18);
        if (n < 64) {
            for (uint32_t i = 0; i < n; i++) {
                uint32_t p = *(uint32_t *)(m + mgr + 0x3f1c + i * 4);
                if (p && !jsrf_node_ok(p)) {
                    fprintf(stderr, "[SNDWATCH] entry %u = %08X (n=%u) seen entering %s; previous entry was %s (0x%08X)\n",
                            i, p, n, name, last_name ? last_name : "?", last_va);
                    { const uint32_t *sp = (const uint32_t *)(m + g_esp);
                      for (int k = 0; k < 120; k++)
                          if (sp[k] > 0x11000 && sp[k] < 0x1C3F60) fprintf(stderr, "    [esp+%03X] %08X\n", k*4, sp[k]); }
                    fflush(stderr); abort();
                }
            }
        }
    }
    last_name = name; last_va = va;
    /* Deep-stack snapshot: the first time the main thread's esp goes below
     * JSRF_DEEP_ESP, print who is on the stack. */
    {
        static int init3 = -1; static uint32_t limit; static int done;
        extern uint32_t xbox_current_thread_stack_top(void);
        if (init3 < 0) { const char *e = getenv("JSRF_DEEP_ESP"); limit = e ? (uint32_t)strtoul(e, 0, 0) : 0; init3 = 1; }
        if (limit && !done && g_esp < limit && xbox_current_thread_stack_top() == 0) {
            uint8_t *m = (uint8_t *)g_xbox_mem_offset;
            done = 1;
            fprintf(stderr, "[DEEP] esp=%08X entering %s -- guest stack code refs (sparse):\n", g_esp, name);
            const uint32_t *sp = (const uint32_t *)(m + g_esp);
            int shown = 0;
            for (uint32_t i = 0; i < (0x00F80000 - g_esp) / 4 && shown < 400; i++)
                if (sp[i] > 0x11000 && sp[i] < 0x1C3F60) { fprintf(stderr, "    [esp+%06X] %08X\n", i*4, sp[i]); shown++; }
            fflush(stderr);
        }
    }
    /* JSRF_BONE_CHECK: the node hierarchy sub_00048190 is about to measure.
     *
     * It counts the tree at arg3 by walking child (+0x60) and sibling (+0x64),
     * multiplies by 0x6c and allocates that. A corrupt link therefore shows up
     * as a 3 GB allocation and a wild pointer several frames later, with
     * nothing left to say which node was bad. Walk it here, where the tree is
     * still intact enough to name the offender.
     */
    if (va == 0x00048190) {
        static int on = -1; static int shown;
        if (on < 0) on = getenv("JSRF_BONE_CHECK") ? 1 : 0;
        if (on) {
            uint8_t *m = (uint8_t *)g_xbox_mem_offset;
            uint32_t root = GARG(3), stack[512]; int sp = 0; long n = 0; int bad = 0;
            uint32_t node = root;
            if (shown < 8) {
                fprintf(stderr, "[BONE] sub_00048190 this=%08X a1=%08X a2=%08X a3=%08X from=%08X\n"
                        "       D3D globals: 251d64=%08X 251d68=%08X 251d6c=%08X 251d70=%08X 251d74=%08X 251d78=%08X\n",
                        g_ecx, GARG(1), GARG(2), GARG(3), GARG(0),
                        *(uint32_t *)(m + 0x251d64), *(uint32_t *)(m + 0x251d68),
                        *(uint32_t *)(m + 0x251d6c), *(uint32_t *)(m + 0x251d70),
                        *(uint32_t *)(m + 0x251d74), *(uint32_t *)(m + 0x251d78));
                { uint32_t dev = *(uint32_t *)(m + 0x251d6c);
                  uint32_t vt = dev ? *(uint32_t *)(m + dev) : 0;
                  fprintf(stderr, "       device %08X vtable=%08X  [0x00]=%08X [0xf4]=%08X [0x148]=%08X\n",
                          dev, vt,
                          vt ? *(uint32_t *)(m + vt) : 0,
                          vt ? *(uint32_t *)(m + vt + 0xf4) : 0,
                          vt ? *(uint32_t *)(m + vt + 0x148) : 0); }
                shown++;
            }
            while (node || sp) {
                if (!node) { node = stack[--sp]; continue; }
                if (!jsrf_node_ok(node) || ++n > 200000) { bad = 1; break; }
                { uint32_t c = *(uint32_t *)(m + node + 0x60);
                  if (c && sp < 512) stack[sp++] = c; }
                node = *(uint32_t *)(m + node + 0x64);
            }
            if (bad) {
                fprintf(stderr, "[BONE] tree at %08X is corrupt after %ld nodes"
                        " (stopped at %08X, depth %d)\n", root, n, node, sp);
                for (uint32_t p = root; p && p < root + 0x300; p += 0x80) {
                    fprintf(stderr, "  node %08X:", p);
                    for (int k = 0; k < 8; k++)
                        fprintf(stderr, " %08X", *(uint32_t *)(m + p + 0x60 - 0x20 + k * 4));
                    fprintf(stderr, "  (that is +40..+5C, then +60 child=%08X sib=%08X)\n",
                            *(uint32_t *)(m + p + 0x60), *(uint32_t *)(m + p + 0x64));
                }
                fflush(stderr);
            }
        }
    }
    /* JSRF_SHADOW=<esp limit>: keep a shadow call stack of the main thread
     * (entry esp per traced function) and print the live chain, with the
     * stack each frame owns, the first time esp drops below the limit. */
    {
        static int init4 = -1; static uint32_t limit; static int done;
        static struct { const char *name; uint32_t va, esp; } sh[8192]; static int top;
        extern uint32_t xbox_current_thread_stack_top(void);
        if (init4 < 0) { const char *e = getenv("JSRF_SHADOW"); limit = e ? (uint32_t)strtoul(e, 0, 0) : 0; init4 = 1; }
        static struct { const char *name; uint32_t esp, ret; } ring[64]; static unsigned rn;
        if (limit && !done && xbox_current_thread_stack_top() == 0) {
            ring[rn & 63].name = name; ring[rn & 63].esp = g_esp; ring[rn & 63].ret = *(uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp); rn++;
            { static uint32_t last_e, last_r; static int nn;
              if (va == 0x00080BD0) {
                  uint32_t r = ring[(rn - 1) & 63].ret;
                  if (last_e && last_e - g_esp == 64 && nn++ < 6)
                      fprintf(stderr, "[SH64] sub_00080BD0 esp=%08X ret=%08X (prev esp=%08X ret=%08X) prev-entry=%s ecx=%08X\n", g_esp, r, last_e, last_r, ring[(rn - 2) & 63].name, g_ecx);
                  last_e = g_esp; last_r = r;
              } }
            while (top > 0 && sh[top - 1].esp <= g_esp) top--;
            if (top < 8192) { sh[top].name = name; sh[top].va = va; sh[top].esp = g_esp; top++; }
            if (g_esp < limit) {
                done = 1;
                fprintf(stderr, "[SHADOW] esp=%08X depth=%d entering %s\n", g_esp, top, name);
                for (unsigned i = 0; i < 64 && i < rn; i++) { unsigned k = (rn - 64 + i) & 63; fprintf(stderr, "    ring %s esp=%08X ret=%08X\n", ring[k].name, ring[k].esp, ring[k].ret); }
                for (int i = top - 1; i >= 0 && i >= top - 400; i--)
                    fprintf(stderr, "    #%-4d %s (0x%08X) esp=%08X frame=%u\n", top - 1 - i, sh[i].name, sh[i].va, sh[i].esp,
                            i > 0 ? sh[i - 1].esp - sh[i].esp : 0);
                fflush(stderr);
            }
        }
    }
    /* JSRF_ESP_WATCH=<va>: print the guest esp at every entry to that function
     * (main thread only), to see whether the frame loop leaks stack. */
    {
        static int init2 = -1; static uint32_t wva; static uint32_t last_esp; static int n;
        extern uint32_t xbox_current_thread_stack_top(void);
        if (init2 < 0) { const char *e = getenv("JSRF_ESP_WATCH"); wva = e ? (uint32_t)strtoul(e, 0, 0) : 0; init2 = 1; }
        if (wva && va == wva && xbox_current_thread_stack_top() == 0) {
            if (g_esp != last_esp && n++ < 400) { fprintf(stderr, "[ESPW] %s esp=%08X (%+d)\n", name, g_esp, (int)(g_esp - last_esp)); fflush(stderr); }
            last_esp = g_esp;
        }
    }
}

/* ---- ADX decoders: report the output buffer they are about to write */
static void jsrf_adx_dec_report(const char *who)
{
    static int n;
    uint32_t h = GARG(1);
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    if (!h || n++ >= 12) return;
    fprintf(stderr, "[ADXDEC] %s h=%08X state=%u outbuf[0x64]=%08X [0x68]=%d [0x6c]=%d [0x54]=%d [0x50]=%08X [0x88]=%08X [0x8c]=%08X\n",
            who, h, *(uint32_t *)(m + h + 4), *(uint32_t *)(m + h + 0x64), *(int32_t *)(m + h + 0x68),
            *(int32_t *)(m + h + 0x6c), *(int32_t *)(m + h + 0x54), *(uint32_t *)(m + h + 0x50),
            *(uint32_t *)(m + h + 0x88), *(uint32_t *)(m + h + 0x8c));
    fflush(stderr);
}
extern void sub_00143EB0_gen(void);
void sub_00143EB0(void) { jsrf_adx_dec_report("stereo"); sub_00143EB0_gen(); }
extern void sub_00143FD0_gen(void);
void sub_00143FD0(void) { jsrf_adx_dec_report("mono"); sub_00143FD0_gen(); }

/* ---- sub_00144F60: ADX block decode core -- print args (out pointers)
 *
 * An output pointer in the title's .data (port/xbe_check.c's section, from
 * JSRF_DATA_START to JSRF_DATA_END) is never a sound buffer: it is a pointer
 * gone wrong before this call. Said once, with the caller, and the decode
 * then runs, as it would on the console. A check, not a stop. */
#define JSRF_DATA_START 0x001EB760u
#define JSRF_DATA_END   0x0027E074u
extern void sub_00144F60_gen(void);
void sub_00144F60(void)
{
    static int n;
    uint32_t a3 = GARG(3), a5 = GARG(5);
    int in_data = (a3 >= JSRF_DATA_START && a3 < JSRF_DATA_END) ||
                  (a5 >= JSRF_DATA_START && a5 < JSRF_DATA_END);
    if (n++ < 6)
        fprintf(stderr, "[ADXCORE] src=%08X a2=%08X outL=%08X a4=%08X outR=%08X a6=%08X a7=%08X a8=%08X esp=%08X\n",
                GARG(1), GARG(2), a3, GARG(4), a5, GARG(6), GARG(7), GARG(8), g_esp);
    RECOMP_CHECK(!in_data, "an output in .data, outL=%08X outR=%08X, from %08X", a3, a5, GARG(0));
    /*
     * JSRF_ADX_DUMP=<prefix>: the encoded blocks this call is given and the
     * PCM it produces from them, for the first few calls.
     *
     * The title's own ADX decoder is recompiled x86, and whether it decodes
     * correctly is the one question that separates "the emulated APU plays
     * the music wrongly" from "the music never was the music". Its arguments
     * already name the answer: a7 and a8 are 0x1CA6 and 0xF32D, which are
     * 7334 and -3283 -- exactly the predictor coefficients CRI's formula
     * gives for a 44.1 kHz stream with a 500 Hz highpass. So the same blocks
     * can be decoded here and compared sample for sample.
     *
     * src advances 0x510 = 1296 bytes a call: 72 blocks of 18 bytes, which
     * for a stereo stream is 36 per channel, 1152 samples each.
     */
    /*
     * JSRF_ADX_RATE=1: how fast the title is decoding, against how fast the
     * APU is consuming.
     *
     * The voice plays a 16384-sample ring that the title refills as it goes,
     * and the music comes out as correct fragments in the wrong order: 100 ms
     * of our output matches a reference decode at 0.998, 250 ms at 0.94, a
     * second at 0.35. Fragments that good cannot be a decode fault; an order
     * that bad is a producer and a consumer running at different speeds.
     *
     * Count the samples, not the calls. The first version of this multiplied
     * calls by 1152 because the first few calls carried 72 blocks, and read
     * 59 calls a second as 68,000 samples a second -- a 1.5x over-supply that
     * would have been a second bug to chase. The block count is an argument;
     * a caller topping up a ring passes whatever will fit, so it varies by
     * design. GARG(2) is that count, two blocks (one per channel) giving 32
     * samples.
     */
    { static int on = -1; static int calls; static long long samples;
      static long long blocks; static uint64_t last_ms;
      if (on < 0) on = getenv("JSRF_ADX_RATE") ? 1 : 0;
      if (on) {
          struct timespec ts; uint64_t now;
          uint32_t cnt = GARG(2);
          clock_gettime(CLOCK_MONOTONIC, &ts);
          now = (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
          calls++; blocks += cnt; samples += (long long)(cnt / 2u) * 32;
          if (!last_ms) last_ms = now;
          if (now - last_ms >= 1000) {
              fprintf(stderr, "[ADXRATE] %d decode calls/s, %lld blocks "
                      "(%.1f per call) = %lld samples/s (44100 wanted)\n",
                      calls, blocks, calls ? (double)blocks / calls : 0.0,
                      samples);
              fflush(stderr);
              calls = 0; blocks = 0; samples = 0; last_ms = now;
          }
      } }

    { static int init, n; static const char *pfx;
      if (!init) { init = 1; pfx = getenv("JSRF_ADX_DUMP"); }
      if (pfx && n < 4) {
          uint8_t *m = (uint8_t *)g_xbox_mem_offset;
          uint32_t src = GARG(1), cnt = GARG(2), outL = GARG(3), outR = GARG(5);
          char nm[256]; FILE *f;
          uint32_t src_bytes = cnt * 18u;
          uint32_t out_samples = (cnt / 2u) * 32u;
          sub_00144F60_gen();                      /* decode first */
          snprintf(nm, sizeof nm, "%s_%d_src.bin", pfx, n);
          f = fopen(nm, "wb");
          if (f) { fwrite(m + src, 1, src_bytes, f); fclose(f); }
          snprintf(nm, sizeof nm, "%s_%d_outL.bin", pfx, n);
          f = fopen(nm, "wb");
          if (f) { fwrite(m + outL, 2, out_samples, f); fclose(f); }
          snprintf(nm, sizeof nm, "%s_%d_outR.bin", pfx, n);
          f = fopen(nm, "wb");
          if (f) { fwrite(m + outR, 2, out_samples, f); fclose(f); }
          fprintf(stderr, "[ADXDUMP] call %d: %u blocks from %08X -> %u samples "
                  "at %08X/%08X (coefs %d %d)\n", n, cnt, src, out_samples,
                  outL, outR, (int)(int16_t)GARG(7), (int)(int16_t)GARG(8));
          fflush(stderr);
          n++;
          return;
      } }
    sub_00144F60_gen();
}

/* arm the .data write trap when the title music file is opened */
static unsigned s_game_frames;   /* sound-manager updates: one per game frame */
void xbox_file_open_hook(const char *path)
{
    extern void jsrf_protect_data_arm(void);
    const char *t = getenv("JSRF_PROTECT_AT");
    if (t && strstr(path, t)) jsrf_protect_data_arm();
    /* When each ADX stream is opened, on the wall clock and the game's own
     * frame count: a cutscene timed in frames drifts from its voice track
     * whenever the game runs below 60 fps, and this is how that shows. */
    if (strstr(path, "Z_ADX") && strstr(path, ".adx"))
        fprintf(stderr, "[ADXOPEN] t=%.2f frame %u %s\n", pad_now(), s_game_frames, path);
}

/* ---- sub_0017CAB0: _chkstk -- report absurd frame sizes */
extern void sub_0017CAB0_gen(void);
void sub_0017CAB0(void)
{
    static int n;
    if (g_eax > 0x10000 && n++ < 10) {
        uint8_t *m = (uint8_t *)g_xbox_mem_offset;
        fprintf(stderr, "[CHKSTK] size=0x%X esp=%08X ret=%08X\n", g_eax, g_esp, *(uint32_t *)(m + g_esp));
        const uint32_t *sp = (const uint32_t *)(m + g_esp);
        for (int i = 0; i < 60; i++)
            if (sp[i] > 0x11000 && sp[i] < 0x1C3F60) fprintf(stderr, "    [esp+%03X] %08X\n", i*4, sp[i]);
        fflush(stderr);
    }
    sub_0017CAB0_gen();
}

/* ======================================================================
 * XInput HLE (XPP section)
 *
 * The XDK's pad API, replaced: its eight entry points are at the end of
 * this section, each calling port/pad.c, which says why. Before them, the
 * test hooks for unattended runs, which reach the pad through pad.c's
 * pad_test_start and pad_test_inputs (below, after auto_step):
 * JSRF_PAD_SCRIPT="20 START;26 A;30 A" presses buttons on a schedule
 * (seconds since start, 250 ms per press), and the autopilot steers.
 *
 *   0x001BD5FF  XGetDevices(type)                        stdcall 4
 *   0x001BD621  XGetDeviceChanges(type, &ins, &rem)      stdcall 12
 *   0x001C3BA1  XInputOpen(type, port, slot, polling)    stdcall 16
 *   0x001C3C16  XInputClose(h)                           stdcall 4
 *   0x001C3C22  XInputGetCapabilities(h, caps)           stdcall 8
 *   0x001C3E14  XInputGetState(h, state)                 stdcall 8
 *   0x001C3E85  XInputSetState(h, feedback)              stdcall 8
 *   0x001C3EBD  XInputPoll(h)                            stdcall 4
 * ====================================================================== */
#include "xinput_xbox.h"
#include "freecam.h"
#include "camera_walls.h"
#include "camera_scenes.h"
#include "intro.h"
#include "keyboard_move.h"
#include "missions.h"
#include "photo_mode.h"
#include "playback.h"
#include "playback_mark.h"
#include "random_graffiti.h"
#include "soul_indicator.h"
#include "sound_keep.h"

static struct { double t, dur; uint16_t buttons; uint8_t analog[8];
                int16_t axis[4]; int has_axis; char name[16]; int said; } s_pad_script[64];
static int s_pad_script_n = -1;
static void pad_script_load(void)
{
    const char *s = getenv("JSRF_PAD_SCRIPT");
    s_pad_script_n = 0;
    if (!s) return;
    while (*s && s_pad_script_n < 64) {
        char name[16] = {0}; double t; int k = 0;
        while (*s == ' ' || *s == ';') s++;
        if (!*s) break;
        t = strtod(s, (char **)&s);
        while (*s == ' ') s++;
        while (*s && *s != ';' && *s != ' ' && k < 15) name[k++] = *s++;
        s_pad_script[s_pad_script_n].t = t;
        s_pad_script[s_pad_script_n].dur = 0.25;
        s_pad_script[s_pad_script_n].buttons = 0;
        s_pad_script[s_pad_script_n].said = 0;
        s_pad_script[s_pad_script_n].has_axis = 0;
        memset(s_pad_script[s_pad_script_n].axis, 0, sizeof s_pad_script[0].axis);
        /* "NAME@secs" holds for that long instead of a 250 ms tap. */
        { char *at = strchr(name, '@');
          if (at) { *at = 0; s_pad_script[s_pad_script_n].dur = atof(at + 1); } }
        memcpy(s_pad_script[s_pad_script_n].name, name, sizeof name);
        memset(s_pad_script[s_pad_script_n].analog, 0, 8);
        /* Sticks: LX+ LX- LY+ LY- RX+ RX- RY+ RY-, full deflection, or a
         * percentage after the sign ("LY+60"). Y is up, as on the pad. */
        if ((name[0] == 'L' || name[0] == 'R') && (name[1] == 'X' || name[1] == 'Y')
            && (name[2] == '+' || name[2] == '-')) {
            int ax = (name[0] == 'R' ? 2 : 0) + (name[1] == 'Y' ? 1 : 0);
            int pct = name[3] ? atoi(name + 3) : 100;
            long v;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            v = (long)pct * 32767 / 100;
            s_pad_script[s_pad_script_n].axis[ax] = (int16_t)(name[2] == '-' ? -v : v);
            s_pad_script[s_pad_script_n].has_axis = 1;
        }
        if (!strcmp(name, "START")) s_pad_script[s_pad_script_n].buttons = XBOX_GAMEPAD_START;
        else if (!strcmp(name, "BACK")) s_pad_script[s_pad_script_n].buttons = XBOX_GAMEPAD_BACK;
        else if (!strcmp(name, "UP")) s_pad_script[s_pad_script_n].buttons = XBOX_GAMEPAD_DPAD_UP;
        else if (!strcmp(name, "DOWN")) s_pad_script[s_pad_script_n].buttons = XBOX_GAMEPAD_DPAD_DOWN;
        else if (!strcmp(name, "LEFT")) s_pad_script[s_pad_script_n].buttons = XBOX_GAMEPAD_DPAD_LEFT;
        else if (!strcmp(name, "RIGHT")) s_pad_script[s_pad_script_n].buttons = XBOX_GAMEPAD_DPAD_RIGHT;
        else if (!strcmp(name, "A")) s_pad_script[s_pad_script_n].analog[XBOX_BUTTON_A] = 255;
        else if (!strcmp(name, "B")) s_pad_script[s_pad_script_n].analog[XBOX_BUTTON_B] = 255;
        else if (!strcmp(name, "X")) s_pad_script[s_pad_script_n].analog[XBOX_BUTTON_X] = 255;
        else if (!strcmp(name, "Y")) s_pad_script[s_pad_script_n].analog[XBOX_BUTTON_Y] = 255;
        else if (!strcmp(name, "LT")) s_pad_script[s_pad_script_n].analog[XBOX_BUTTON_LTRIGGER] = 255;
        else if (!strcmp(name, "RT")) s_pad_script[s_pad_script_n].analog[XBOX_BUTTON_RTRIGGER] = 255;
        s_pad_script_n++;
    }
    fprintf(stderr, "[PAD] script: %d scheduled presses\n", s_pad_script_n);
}
/* ---- autopilot for headless runs: JSRF_AUTOPILOT="t:x,z;t:x,z;...".
 * Waypoints are taken in order. Once a waypoint's time t has come (same
 * clock as JSRF_PAD_SCRIPT), the left stick steers the player toward world
 * (x, z), relative to where the follow camera looks, until the player is
 * within 12 units (or 40 s pass). The stick is recomputed every frame from
 * the camera update (sub_000A5070), so the loop closes through the game's
 * own camera-relative controls. Off unless the variable is set. */
static struct { double t, t_on, t_done; float x, z; int state; char btn[8]; } s_auto[16];  /* 0 waiting, 1 active, 2 done */
static int s_auto_n = -1;
static int16_t s_auto_lx, s_auto_ly;
/* JSRF_TALK_AUTO_A=1: while the follow camera is in talk mode (12), tap A
 * every 2 s to page through the conversation. */
static int s_talk_auto_a = -1;
static double s_talk_auto_a_until = 1e30;
static volatile uint32_t s_cam_mode_now;
static int s_auto_live;
static void auto_load(void)
{
    const char *s = getenv("JSRF_AUTOPILOT");
    s_auto_n = 0;
    if (!s) return;
    while (*s && s_auto_n < 16) {
        double t; float x, z; int used = 0;
        while (*s == ' ' || *s == ';') s++;
        if (!*s) break;
        if (sscanf(s, "%lf:%f,%f%n", &t, &x, &z, &used) != 3 || used <= 0) break;
        s_auto[s_auto_n].t = t; s_auto[s_auto_n].x = x; s_auto[s_auto_n].z = z;
        s_auto[s_auto_n].state = 0; s_auto[s_auto_n].t_on = 0; s_auto[s_auto_n].t_done = 0;
        memset(s_auto[s_auto_n].btn, 0, sizeof s_auto[0].btn);
        s += used;
        /* "t:x,z:RT" -- on arrival, tap that button three times, 1.5 s apart */
        if (*s == ':') {
            int k = 0; s++;
            while (*s && *s != ';' && *s != ' ' && k < 7) s_auto[s_auto_n].btn[k++] = *s++;
        }
        s_auto_n++;
    }
    fprintf(stderr, "[AUTO] %d waypoints\n", s_auto_n);
}
/* JSRF_TELEPORT="t:x,y,z" -- once, at time t, put the player at (x, y, z).
 * Test-only shortcut to a spot the autopilot cannot path to. */
static void teleport_step(float *chw)
{
    static int state = -1; static double t; static float x, y, z;
    if (state < 0) {
        const char *s = getenv("JSRF_TELEPORT");
        state = (s && sscanf(s, "%lf:%f,%f,%f", &t, &x, &y, &z) == 4) ? 1 : 0;
    }
    if (state != 1 || pad_now() < t) return;
    state = 2;
    fprintf(stderr, "[TELEPORT] t=%.1f (%.1f %.1f %.1f) -> (%.1f %.1f %.1f)\n", pad_now(),
            chw[0xCA4 / 4], chw[0xCA8 / 4], chw[0xCAC / 4], x, y, z);
    chw[0xCA4 / 4] = x; chw[0xCA8 / 4] = y; chw[0xCAC / 4] = z;
}
/* JSRF_CANS="t:n" -- once, at time t, give the player n spray cans
 * (char +0xFA0; sub_0007FC90 checks it against the boost cost, sub_0007FC60
 * takes the cost away). Test-only: the boost dash needs ten.
 * Every frame: log the boost timer (char +0xE60, set from the character's
 * parameters when B starts a boost, sub_0009C6A5) starting and ending, and
 * with JSRF_BOOST_DUMP="shots,every,frames" capture a burst of frames and a
 * per-pass log when the first boost starts. */
extern void nv2a_gl_dump_burst(int shots, int every);
extern void nv2a_gl_trace_frames(int n);
extern uint32_t nv2a_gl_flips(void);
static void test_hooks_step(uint8_t *ch)
{
    static int cans_state = -1; static double cans_t; static int cans_n;
    static int dump_state = -1; static int d_shots, d_every, d_frames;
    static uint32_t last_timer; static int boosts;
    uint32_t *cw = (uint32_t *)ch;
    uint32_t timer;
    if (cans_state < 0) {
        const char *s = getenv("JSRF_CANS");
        cans_state = (s && sscanf(s, "%lf:%d", &cans_t, &cans_n) == 2) ? 1 : 0;
        s = getenv("JSRF_BOOST_DUMP");
        dump_state = (s && sscanf(s, "%d,%d,%d", &d_shots, &d_every, &d_frames) == 3) ? 1 : 0;
    }
    if (cans_state == 1 && pad_now() >= cans_t) {
        cans_state = 2;
        fprintf(stderr, "[CANS] t=%.1f cans %d -> %d\n", pad_now(), (int)cw[0xFA0 / 4], cans_n);
        cw[0xFA0 / 4] = (uint32_t)cans_n;
    }
    timer = cw[0xE60 / 4];
    if (timer != last_timer && (timer == 0 || last_timer == 0)) {
        if (timer) boosts++;
        fprintf(stderr, "[BOOST] t=%.2f flip %u boost %s: timer %u cans %d\n",
                pad_now(), nv2a_gl_flips(), timer ? "starts" : "ends", timer,
                (int)cw[0xFA0 / 4]);
        if (timer && dump_state == 1) {
            dump_state = 2;
            nv2a_gl_dump_burst(d_shots, d_every);
            nv2a_gl_trace_frames(d_frames);
        }
    }
    last_timer = timer;
}
/* ---- missions -------------------------------------------------------------
 * sub_00030490 / sub_00030690 queue the load of mission file .bin / .dat for
 * the id in their one argument: high 16 bits and low 16 bits, two decimal
 * digits each (0x00010001 is mssn0101, the tutorial; 0x00010060 is mssn0196,
 * what follows it). Every request is logged once per id.
 * JSRF_MISSION="101:196@20" (test only): from 20 s on, a request for 0101 is
 * turned into one for 0196 -- straight to the end of the tutorial. Up to four
 * pairs, comma-separated, share the time: "0x10001:0x200FA,0x10064:0x200FA@45"
 * sends New Game's tutorial and Load Game's garage alike to 99th Street.
 * Each request, as it goes on, is an anchor for input playback
 * (port/playback.c), as are the stage the flow asks for and a cutscene's
 * start and end, below: with the game's random generator, which playback
 * keeps there and puts back. */
#define GAME_RANDOM 0x0022E58Cu  /* the game's random generator: seed * 0x55 + 0x7F, drawn by
                                  * sub_0014C2B0 and sub_0014C2D0, seeded 0x416C at 0x00012335 */
static uint32_t *game_random(void) { return (uint32_t *)((uint8_t *)g_xbox_mem_offset + GAME_RANDOM); }

/* Each draw from it, counted for playback's checks (port/playback.c): on
 * the game's own thread, the one that runs the main loop's passes
 * (sub_00013A80 marks it), or another, and where it returns to. Phase 0
 * found it drawn a number of times that follows the host's time; this says
 * where. */
static RECOMP_TLS int s_game_thread;
extern void sub_0014C2B0_gen(void);
void sub_0014C2B0(void)
{
    playback_drawn(s_game_thread, *(uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp));
    sub_0014C2B0_gen();
}
extern void sub_0014C2D0_gen(void);
void sub_0014C2D0(void)
{
    playback_drawn(s_game_thread, *(uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp));
    sub_0014C2D0_gen();
}
static uint32_t s_mission_mgr;   /* the object whose vtable requests missions */
static void mission_hook(const char *what)
{
    static int init; static uint32_t from[4], to[4]; static double after; static int npairs;
    static uint32_t said[32]; static int nsaid;
    uint32_t *arg = (uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp + 4);
    uint32_t id = *arg; int i, seen = 0;
    if (!init) {
        const char *e = getenv("JSRF_MISSION"); unsigned a, b; double t = 0;
        const char *at = e ? strchr(e, '@') : NULL;
        init = 1;
        /* An id is (chapter << 16) | number and the file is named after
         * the number modulo 100: 0x000100C4 (196) loads mssn0196, the scene
         * after the tutorial, and 0x00010064 (100) mssn0100, the garage.
         * "101:196" therefore meant 0x00010060, the right files under the
         * wrong number, and the game hung on "Now Loading". Values of
         * 0x10000 and up are taken as ids: JSRF_MISSION="0x10001:0x100C4". */
        if (at) t = atof(at + 1);
        while (e && npairs < 4 && sscanf(e, "%i:%i", (int *)&a, (int *)&b) == 2) {
            from[npairs] = a >= 0x10000u ? a : ((a / 100) << 16) | (a % 100);
            to[npairs]   = b >= 0x10000u ? b : ((b / 100) << 16) | (b % 100);
            npairs++;
            e = strchr(e, ',');
            if (!e || (at && e > at)) break;
            e++;
        }
        after = t;
    }
    for (i = 0; i < nsaid; i++) if (said[i] == (id ^ (what[0] << 24))) seen = 1;
    if (!seen && nsaid < 32) {
        const uint32_t *sp = (const uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp);
        int k, shown = 0;
        said[nsaid++] = id ^ (what[0] << 24);
        fprintf(stderr, "[MISSION] t=%.1f %s request mssn%02u%02u (id %08X) mgr %08X from",
                pad_now(), what, id >> 16, id & 0xFFFF, id, g_ecx);
        for (k = 0; k < 64 && shown < 8; k++)
            if (sp[k] > 0x11000u && sp[k] < 0x138000u) { fprintf(stderr, " %08X", sp[k]); shown++; }
        fprintf(stderr, "\n");
    }
    if (what[0] == '.' && what[1] == 'b') s_mission_mgr = g_ecx;
    for (i = 0; i < npairs; i++)
        if (id == from[i] && pad_now() >= after) {
            fprintf(stderr, "[MISSION] t=%.1f %s: redirecting mssn%02u%02u -> mssn%02u%02u\n",
                    pad_now(), what, from[i] >> 16, from[i] & 0xFFFF, to[i] >> 16, to[i] & 0xFFFF);
            *arg = to[i];
            break;
        }
    playback_anchor(what[1] == 'b' ? PLAYBACK_MISSION_BIN : PLAYBACK_MISSION_DAT, *arg, game_random());
}
/* ---- sub_00046FD0: fill in one storage device's record for the save menu
 * (slot 8 is the hard disk: +0 present, +4 ?, +8 room for a save, +0xC the
 * existing save's info, 0 = none). [SAVE] says what the menu is told. */
extern void sub_00046FD0_gen(void);
void sub_00046FD0(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t slot = *(uint32_t *)(m + g_esp + 4), rec = *(uint32_t *)(m + g_esp + 8);
    sub_00046FD0_gen();
    if (slot == 8 && rec) {
        static int n;
        if (n++ < 24)
            fprintf(stderr, "[SAVE] t=%.1f hard disk record %08X: present %u, %u, room %u, save %d, %04X\n",
                    pad_now(), rec, *(uint32_t *)(m + rec), *(uint32_t *)(m + rec + 4),
                    *(uint32_t *)(m + rec + 8), (int)*(uint32_t *)(m + rec + 0xC),
                    *(uint16_t *)(m + rec + 0x10));
    }
}
/* ---- the save menu (test only) ------------------------------------------
 *
 * sub_000681C0(mode, records) opens it: mode 0 saves, 1 loads; records is an
 * optional copy of the nine storage-device records (sub_00047080). The menu
 * object's state machine is sub_00067E90: +0x98 state, +0xA4 mode, +0xA8 the
 * active column (0 device, 1 slot, 2 confirm), +0x33C a table whose +8/+0x18/
 * +0x28 are each column's selection, +0x340 the device items (32 bytes, +8
 * enabled; item 0 is the hard disk), +0x5CC the hard disk's record, +0x678
 * the save buffer, +0x6B8 1 for a new save.
 *
 * JSRF_OPEN_SAVE=t opens it (save mode) at t seconds, from the camera update,
 * so a test can reach it without playing to the garage. [SAVEMENU] logs every
 * change of what the menu is doing. */
static uint32_t guest_call_cdecl(void (*fn)(void), uint32_t ra, int nargs, const uint32_t *args)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t sv[8], ret;
    int top = g_fp_top, i;
    sv[0] = g_eax; sv[1] = g_ecx; sv[2] = g_edx; sv[3] = g_ebx;
    sv[4] = g_esp; sv[5] = g_ebp; sv[6] = g_esi; sv[7] = g_edi;
    for (i = nargs - 1; i >= 0; i--) { g_esp -= 4; *(uint32_t *)(m + g_esp) = args[i]; }
    g_esp -= 4; *(uint32_t *)(m + g_esp) = ra;
    fn();
    ret = g_eax;
    g_eax = sv[0]; g_ecx = sv[1]; g_edx = sv[2]; g_ebx = sv[3];
    g_esp = sv[4]; g_ebp = sv[5]; g_esi = sv[6]; g_edi = sv[7];
    g_fp_top = top;
    return ret;
}
extern void sub_000681C0(void);
extern void sub_00030490(void);
static void mission_at_step(void)
{
    static int state = -1; static double t; static uint32_t id;
    if (state < 0) {
        const char *e = getenv("JSRF_MISSION_AT"); int v = 0;
        state = (e && sscanf(e, "%lf:%i", &t, &v) == 2) ? 1 : 0; id = (uint32_t)v;
    }
    if (state != 1 || pad_now() < t || !s_mission_mgr) return;
    state = 2;
    fprintf(stderr, "[MISSION] t=%.1f asking mgr %08X for mission %08X\n", pad_now(), s_mission_mgr, id);
    { uint32_t sv_ecx = g_ecx; uint32_t a[1]; a[0] = id;
      g_ecx = s_mission_mgr;
      { uint32_t r = guest_call_cdecl(sub_00030490, 0x00030490u, 1, a);
        g_ecx = sv_ecx;
        fprintf(stderr, "[MISSION] request returned %u\n", r); } }
}
/* JSRF_OPEN_SAVE="t[:mode]" (test only): open the save menu at t seconds;
 * mode 1 opens it to LOAD, which is how a test gets a player's saved
 * progress into a run that started with New Game. */
/* Display > Aspect ratio changed in play (widescreen on or off, or another
 * wide shape): the title sets its camera once, at startup, so the
 * projection and the culling planes are recomputed here -- the same two
 * calls SetCamera makes, on the device it made them on -- the frame after
 * the choice changes. */
static uint32_t s_cam_dev;      /* the device the camera was last set on */
static int s_wide_applied = -1; /* the mode it was set for */
static float s_wide_squeeze_applied;   /* and the squeeze, when widened */
extern int nv2a_widescreen(void);
extern float nv2a_wide_squeeze(void);
extern int nv2a_wide_scene(void);
extern void nv2a_set_wide_scene(int on);
extern void sub_00153A90(void);
extern void sub_00153EC0(void);
static void wide_reapply_step(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t sv_eax, sv_ecx, sv_edx;
    /* JSRF_WIDE_AT=<t> (test only): flip the mode at time t, as the menu would. */
    { static int st = -1; static double t;
      if (st < 0) { const char *e = getenv("JSRF_WIDE_AT");
                    st = (e && sscanf(e, "%lf", &t) == 1) ? 1 : 0; }
      if (st == 1 && pad_now() >= t) {
          extern void nv2a_set_widescreen(int on);
          st = 2;
          nv2a_set_widescreen(!nv2a_widescreen());
          fprintf(stderr, "[WIDE] t=%.1f JSRF_WIDE_AT: widescreen %s\n", pad_now(),
                  nv2a_widescreen() ? "on" : "off");
      } }
    if (!s_cam_dev || s_wide_applied < 0) return;
    if (s_wide_applied == nv2a_wide_scene()
     && (!s_wide_applied || s_wide_squeeze_applied == nv2a_wide_squeeze())) return;
    /* Only on the device the camera was set on, and only while it is still
     * one: the graphics device class, vtable 0x1E0F00. */
    if (s_cam_dev < 0x10000u || s_cam_dev + 0x60u >= (64u << 20)
     || *(uint32_t *)(m + s_cam_dev) != 0x001E0F00u) { s_cam_dev = 0; return; }
    sv_eax = g_eax; sv_ecx = g_ecx; sv_edx = g_edx;
    g_esp -= 4; *(uint32_t *)(m + g_esp) = 0;   /* each callee's ret pops it */
    g_ecx = s_cam_dev;
    sub_00153A90();
    g_esp -= 4; *(uint32_t *)(m + g_esp) = 0;
    g_ecx = s_cam_dev;
    sub_00153EC0();
    g_eax = sv_eax; g_ecx = sv_ecx; g_edx = sv_edx;
    if (nv2a_wide_scene())
        fprintf(stderr, "[WIDE] t=%.1f camera re-set for widescreen (squeeze %.3f)\n",
                pad_now(), (double)nv2a_wide_squeeze());
    else
        fprintf(stderr, "[WIDE] t=%.1f camera re-set for 4:3\n", pad_now());
}

/* JSRF_FIND_PROJ=<t> (test only): at time t, scan guest RAM for projection
 * matrices -- [a 0 0 0; 0 b 0 0; 0 0 c d; 0 0 e f] with b/a = 4/3 -- in
 * both the row-major and the transposed layout, to find where the title
 * keeps the one it draws the world with. */
static void find_proj_step(void)
{
    static int state = -1; static double t;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t a, n = 0;
    if (state < 0) {
        const char *e = getenv("JSRF_FIND_PROJ");
        state = (e && sscanf(e, "%lf", &t) == 1) ? 1 : 0;
    }
    if (state != 1 || pad_now() < t) return;
    state = 2;
    for (a = 0x10000; a + 64 <= (64u << 20) && n < 64; a += 4) {
        const float *f = (const float *)(m + a);
        float x = f[0], y;
        if (!(x > 0.2f && x < 8.0f)) continue;
        /* row-major: m00 at 0, m11 at 5 */
        y = f[5];
        if (y > 0.0f && fabsf(y / x - 4.0f / 3.0f) < 2e-3f
            && f[1] == 0 && f[2] == 0 && f[3] == 0 && f[4] == 0 && f[6] == 0 && f[7] == 0
            && f[8] == 0 && f[9] == 0 && f[12] == 0 && f[13] == 0) {
            fprintf(stderr, "[PROJ] t=%.1f %08X: %.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f\n",
                    pad_now(), a, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7],
                    f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15]);
            n++;
        }
    }
    fprintf(stderr, "[PROJ] t=%.1f scan done, %u found\n", pad_now(), n);
}

static void save_menu_open_step(void)
{
    static int state = -1; static double t; static int mode;
    if (state < 0) {
        const char *e = getenv("JSRF_OPEN_SAVE");
        state = e ? 1 : 0; t = e ? atof(e) : 0;
        mode = (e && strchr(e, ':')) ? atoi(strchr(e, ':') + 1) : 0;
    }
    if (state != 1 || pad_now() < t) return;
    state = 2;
    { uint32_t a[2]; a[0] = (uint32_t)mode; a[1] = 0;
      fprintf(stderr, "[SAVEMENU] t=%.1f opening the save menu (mode %d)\n", pad_now(), mode);
      guest_call_cdecl(sub_000681C0, 0x0007D25Au, 2, a); }
}
extern void sub_00067E90_gen(void);
void sub_00067E90(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t o = g_ecx;
    static uint32_t last[16]; static int n;
    sub_00067E90_gen();
    if (o >= 0x10000u && o + 0x6C0u < (64u << 20) && n < 400) {
        uint32_t tab = *(uint32_t *)(m + o + 0x33C), items = *(uint32_t *)(m + o + 0x340);
        uint32_t v[16] = {0};
        v[0] = *(uint32_t *)(m + o + 0x98); v[1] = *(uint32_t *)(m + o + 0xA4);
        v[2] = *(uint32_t *)(m + o + 0xA8);
        if (tab && tab < (64u << 20)) { v[3] = *(uint32_t *)(m + tab + 8); v[4] = *(uint32_t *)(m + tab + 0x18);
                                        v[5] = *(uint32_t *)(m + tab + 0x28); }
        if (items && items < (64u << 20)) v[6] = *(uint32_t *)(m + items + 8);
        v[7] = *(uint32_t *)(m + o + 0x5D4); v[8] = *(uint32_t *)(m + o + 0x5D8);
        v[9] = *(uint32_t *)(m + o + 0x678); v[10] = *(uint32_t *)(m + o + 0x6B8);
        v[11] = *(uint32_t *)(m + 0x251EE8); v[12] = *(uint32_t *)(m + 0x251F20);
        v[13] = *(uint32_t *)(m + 0x251E1C + (*(uint32_t *)(m + 0x251F20) & 3) * 0x40);
        v[14] = *(uint32_t *)(m + o + 0x334);
        if (memcmp(v, last, sizeof v)) {
            memcpy(last, v, sizeof v); n++;
            fprintf(stderr, "[SAVEMENU] t=%.1f obj %08X state %u mode %u col %u sel %d/%d/%d hdd-item-on %u "
                    "room %u save %d buf %08X new %u | pressed %08X port %u portflag %08X ready %u\n",
                    pad_now(), o, v[0], v[1], v[2], (int)v[3], (int)v[4], (int)v[5], v[6], v[7], (int)v[8],
                    v[9], v[10], v[11], v[12], v[13], v[14]);
        }
    }
}

/* ---- the characters' voice lines kept on them (voice_follow.c) -----------
 *
 * sub_00119540(bank, sound, where) plays a sound and returns its handle; the
 * sound manager's update ends in sub_001185B0, which hands the frame's
 * settings to the sound engine. Before it, voice_follow.c moves each voice
 * line it keeps as the character update moves its own looped one
 * (sub_00080340): sub_00118710(handle), whether it still plays, then
 * sub_00118BD0(handle, where), both cdecl. sub_0007E830 is the characters'
 * destructor (thiscall), which their deleting destructor, sub_00080320,
 * runs before it frees one: voice_follow.c lets that character's lines go. */
#define VOICE_PLAYING_RET  0x0008039Du /* where sub_00080340's sub_00118710 returns to */
#define VOICE_MOVE_RET     0x000803B7u /* and its sub_00118BD0 */
#include "voice_follow.h"
#include "widescreen_menus.h"   /* the title's widened states, for the scene below */
extern size_t xbox_GetMappedSize(void);   /* the heap runs to its end */
extern void sub_00119540_gen(void);
extern void sub_001185B0_gen(void);
extern void sub_0007E830_gen(void);
extern void sub_00118710(void);
extern void sub_00118BD0(void);
static int voice_playing(uint32_t handle)
{
    return guest_call_cdecl(sub_00118710, VOICE_PLAYING_RET, 1, &handle) != 0;
}
static void voice_move(uint32_t handle, uint32_t where)
{
    uint32_t a[2] = { handle, where };
    guest_call_cdecl(sub_00118BD0, VOICE_MOVE_RET, 2, a);
}
void sub_00119540(void)
{
    uint32_t bank = GARG(1), where = GARG(3);
    sub_00119540_gen();
    voice_follow_played((const uint8_t *)g_xbox_mem_offset, (uint32_t)xbox_GetMappedSize(), g_eax,
                        bank, where, g_esp);
}
void sub_001185B0(void)
{
    voice_follow_frame((const uint8_t *)g_xbox_mem_offset, voice_playing, voice_move);
    sub_001185B0_gen();
}
void sub_0007E830(void)
{
    uint32_t chr = g_ecx;
    sub_0007E830_gen();
    voice_follow_gone(chr);
}

/* ---- list menus and the keyboard -----------------------------------------
 *
 * The game's list menus move their cursor on either input: a new d-pad
 * press moves once (and the menu returns for that frame), and on a later
 * frame a stick that has just crossed its threshold moves once more. On a
 * pad those are two different hands. On the keyboard W/A/S/D drive the
 * d-pad and the left stick together -- the skater only reads the stick, the
 * menus mostly the d-pad -- so every tap moved the cursor twice. In a
 * two-item YES/NO that is no move at all: the save prompt sat on NO, A went
 * back a column, and the game could not be saved from the keyboard.
 *
 * While a d-pad direction is held, these menus now see the stick centred:
 * the d-pad has already said where to go. Input structs: 0x251DE0 + 0x40 *
 * port, and the active port's copy at 0x251EE0; +4 held buttons (d-pad
 * 0xF000), +0x14/+0x18 stick X/Y. The skater reads angle and magnitude
 * (+0x34/+0x38), which are left alone. Only the menus that read both inputs
 * are wrapped; RECOMP_MENU_STICK_BOTH=1 turns it off. */
static void menu_stick_mute(uint32_t saved[5][2], int *mask)
{
    static int off = -1;
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    int i;
    *mask = 0;
    if (off < 0) off = getenv("RECOMP_MENU_STICK_BOTH") ? 1 : 0;
    if (off) return;
    for (i = 0; i < 5; i++) {
        uint32_t b = 0x00251DE0u + 0x40u * (uint32_t)i;
        if (*(uint32_t *)(m + b + 4) & 0xF000u) {
            saved[i][0] = *(uint32_t *)(m + b + 0x14); saved[i][1] = *(uint32_t *)(m + b + 0x18);
            *(uint32_t *)(m + b + 0x14) = 0; *(uint32_t *)(m + b + 0x18) = 0;
            *mask |= 1 << i;
        }
    }
}
static void menu_stick_restore(uint32_t saved[5][2], int mask)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    int i;
    for (i = 0; i < 5; i++)
        if (mask & (1 << i)) {
            uint32_t b = 0x00251DE0u + 0x40u * (uint32_t)i;
            *(uint32_t *)(m + b + 0x14) = saved[i][0]; *(uint32_t *)(m + b + 0x18) = saved[i][1];
        }
}
#define MENU_NAV_WRAP(va) \
    extern void sub_##va##_gen(void); \
    void sub_##va(void) { uint32_t sv[5][2]; int mk; menu_stick_mute(sv, &mk); \
                          sub_##va##_gen(); menu_stick_restore(sv, mk); }
/* One per line as well as inside the macro: the pipeline's manual_scan finds
 * wrapped functions by these declarations, and emits each body as sub_X_gen
 * only if it sees one. */
extern void sub_00069040_gen(void);
extern void sub_000689A0_gen(void);
extern void sub_00077000_gen(void);
extern void sub_00079740_gen(void);
extern void sub_0007A170_gen(void);
extern void sub_0011B680_gen(void);
extern void sub_000F9C40_gen(void);
extern void sub_000FA070_gen(void);
extern void sub_00074460_gen(void);
extern void sub_0007DAE0_gen(void);
MENU_NAV_WRAP(00069040)   /* save/load menu, active port */
MENU_NAV_WRAP(000689A0)   /* save/load menu, every port */
MENU_NAV_WRAP(00077000)
MENU_NAV_WRAP(00079740)
MENU_NAV_WRAP(0007A170)
MENU_NAV_WRAP(0011B680)
MENU_NAV_WRAP(000F9C40)
MENU_NAV_WRAP(000FA070)
MENU_NAV_WRAP(00074460)
MENU_NAV_WRAP(0007DAE0)

/* ---- sub_00051FC0: the game flow's per-frame step (test only) -------------
 *
 * The flow object runs the mission sequence: +0x58 the mission id, +0x5C a
 * state indexing the step table at 0x1F9888 (0 asks for the mission file,
 * 1 waits for it, and so on). JSRF_FLOW_LOG=1 (Developer > Logging): a
 * [FLOW] line for every state change, with the mission's id.
 * JSRF_FLOW_GOTO="t:id" sets the id and state 0 at t seconds -- how a test
 * gets to a later mission without playing the ones before it; it turns the
 * [FLOW] lines on too. */
/* JSRF_MSCRIPT_DUMP=<mission id> (test only): when that mission's flow first
 * reaches state 14 (its setup commands) and again at the first state 15
 * after a state 19 (an event ended and set its flags), list the setup commands ([mission+0x174], count
 * +0x178) and the per-frame triggers (+0x17C, count +0x180): 0x54 bytes each,
 * a condition list at +0 (count +4) of story-flag tests, flags to set at +8
 * (+0xC), the op at +0x10 and its parameters after. Each condition is shown
 * with whether it holds now (flags object 0x1EFFB0: bank = c & 7, bit =
 * (c >> 3) & 0xFFFF, wanted value = bit 19). */
static int mscript_cond(uint32_t c)
{
    static const uint32_t bank_off[4] = { 0x6A14, 0x18, 0x58, 0x98 };
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t bank = c & 7, bit = (c >> 3) & 0xFFFF, want = (c >> 19) & 1, w;
    if (bank > 3 || bit >= 0x200) return -1;
    w = *(uint32_t *)(m + 0x001EFFB0u + bank_off[bank] + (bit >> 5) * 4);
    return ((w >> (bit & 31)) & 1) == want;
}
static void mscript_list(const char *what, uint32_t base, uint32_t n)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t i, k;
    if (base < 0x10000u || n > 400 || base + n * 0x54u >= (64u << 20)) return;
    for (i = 0; i < n; i++) {
        const uint32_t *c = (const uint32_t *)(m + base + i * 0x54u);
        int all = 1;
        fprintf(stderr, "[MSCRIPT] %s #%u op %3u |", what, i, c[4]);
        for (k = 5; k < 21; k++) fprintf(stderr, " %X", c[k]);
        fprintf(stderr, " | if");
        if (c[1] <= 16 && c[0] >= 0x10000u && c[0] < (64u << 20))
            for (k = 0; k < c[1]; k++) {
                uint32_t cv = *(uint32_t *)(m + c[0] + 4 * k);
                int r = mscript_cond(cv);
                if (r != 1) all = 0;
                fprintf(stderr, " %u:%03X=%u(%s)", cv & 7, (cv >> 3) & 0xFFFF, (cv >> 19) & 1, r == 1 ? "y" : r == 0 ? "n" : "?");
            }
        fprintf(stderr, " -> %s | set", all ? "TRUE" : "false");
        if (c[3] <= 16 && c[2] >= 0x10000u && c[2] < (64u << 20))
            for (k = 0; k < c[3]; k++) {
                uint32_t cv = *(uint32_t *)(m + c[2] + 4 * k);
                fprintf(stderr, " %u:%03X=%u", cv & 7, (cv >> 3) & 0xFFFF, (cv >> 19) & 1);
            }
        fprintf(stderr, "\n");
    }
}
static void mscript_dump(uint32_t flow, uint32_t state)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t ms = *(uint32_t *)(m + flow + 0x1040), b;
    if (ms < 0x10000u || ms + 0x200u >= (64u << 20)) return;
    fprintf(stderr, "[MSCRIPT] ---- t=%.1f mission %08X state %u: %u setup commands (pc %u), %u triggers ----\n",
            pad_now(), *(uint32_t *)(m + flow + 0x58), state, *(uint32_t *)(m + ms + 0x178),
            *(uint32_t *)(m + flow + 0x2B0), *(uint32_t *)(m + ms + 0x180));
    for (b = 0; b < 4; b++) {
        static const uint32_t bank_off[4] = { 0x6A14, 0x18, 0x58, 0x98 };
        const uint32_t *w = (const uint32_t *)(m + 0x001EFFB0u + bank_off[b]);
        int k;
        fprintf(stderr, "[MSCRIPT] flags bank %u:", b);
        for (k = 0; k < 16; k++) fprintf(stderr, " %08X", w[k]);
        fprintf(stderr, "\n");
    }
    mscript_list("setup", *(uint32_t *)(m + ms + 0x174), *(uint32_t *)(m + ms + 0x178));
    mscript_list("trig", *(uint32_t *)(m + ms + 0x17C), *(uint32_t *)(m + ms + 0x180));
    fflush(stderr);
}
/* What the flow last said about widescreen (-1 nothing yet: the boot logos,
 * widened, see jsrf_wide_boot). */
static int s_wide_scene_said = -1;
static int wide_log_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("JSRF_WIDE_LOG") ? 1 : 0;
    return on;
}
/* The boot logos (SEGA, Smilebit, Dolby) are widened like the game: each is
 * an overlay on a cleared screen, so the logo keeps its shape and its
 * background fills the wide screen, where kept 4:3 it sat between black bars.
 * JSRF_WIDE_BOOT=0 keeps them 4:3. */
__attribute__((constructor)) static void jsrf_wide_boot(void)
{
    const char *v = getenv("JSRF_WIDE_BOOT");
    nv2a_set_wide_scene(v && v[0] == '0' ? 0 : 1);
}
/* Input playback's flow anchors (port/playback.h, playback_flow): each
 * flow's step, seen as the pass's step is about to run (after it, a flow
 * handing over may have let itself go); playback makes each change, and each
 * new flow, an anchor. Some steps wait on the sound manager: a step starts a
 * track (sub_00117350) and the next waits until the manager has it ready
 * (step 21 on its music slot, sub_001167F0; 27, 53, 69, 75, 79, 85 and 89 the
 * same), which it is once the track has decoded enough to play
 * (sub_00116C30), on the host's time, not the game's passes. So a play can
 * reach the step after a pass sooner or later than its recording did, and
 * the anchor lines the presses up with it again. */
static void flow_anchor(const uint8_t *m, uint32_t flow)
{
    if (flow < ADX_GUEST_LOW || flow + SOUL_FLOW_SIZE > ADX_GUEST_RAM) return;
    playback_flow(flow, *(const uint32_t *)(m + flow + SOUL_FLOW_MISSION),
                  *(const uint32_t *)(m + flow + SOUL_FLOW_STATE), game_random());
}
extern void sub_00051FC0_gen(void);
void sub_00051FC0(void)
{
    static int init = -1, log_on; static double t; static uint32_t id, last_state = 0xFFFFFFFFu;
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t flow = g_ecx;
    if (init < 0) {
        const char *e = getenv("JSRF_FLOW_GOTO"); int v = 0;
        init = (e && sscanf(e, "%lf:%i", &t, &v) == 2) ? 1 : 0; id = (uint32_t)v;
        log_on = (init || getenv("JSRF_FLOW_LOG")) ? 1 : 0;
    }
    if (flow >= 0x10000u && flow + 0x1100u < (64u << 20)) {
        /* JSRF_GMVAR_SET="t:idx=val[,idx=val...]" (test only): write game
         * manager variables (+0x7868 + 4*idx) once, at t seconds. */
        { static int gs = -1; static double gt; static char spec[256];
          if (gs < 0) {
              const char *e = getenv("JSRF_GMVAR_SET"); const char *c;
              gs = 0;
              if (e && (c = strchr(e, ':')) != NULL) { gt = atof(e); snprintf(spec, sizeof spec, "%s", c + 1); gs = 1; }
          }
          if (gs == 1 && pad_now() >= gt) {
              uint32_t gm = *(uint32_t *)(m + 0x0022FCE0u);
              char *p = spec;
              gs = 2;
              while (gm >= 0x10000u && p && *p) {
                  char *end; uint32_t idx = (uint32_t)strtoul(p, &end, 0), val;
                  if (*end != '=') break;
                  val = (uint32_t)strtoul(end + 1, &end, 0);
                  if (idx < 0x800) {
                      fprintf(stderr, "[GMVAR] t=%.1f var %03X: %08X -> %08X\n", pad_now(), idx,
                              *(uint32_t *)(m + gm + 0x7868u + 4u * idx), val);
                      *(uint32_t *)(m + gm + 0x7868u + 4u * idx) = val;
                  }
                  p = (*end == ',') ? end + 1 : NULL;
              }
          } }
        if (init == 1 && pad_now() >= t) {
            /* The way a mission ends: +0x4C/+0x54 the next chapter and
             * number, state 93 creates the next flow (task 0xA, 0x4E9B0)
             * and hands over to it once it has loaded. */
            init = 2;
            fprintf(stderr, "[FLOW] t=%.1f flow %08X: state %u mission %08X -> next %u/%u via state 93\n",
                    pad_now(), flow, *(uint32_t *)(m + flow + 0x5C), *(uint32_t *)(m + flow + 0x58),
                    id >> 16, id & 0xFFFF);
            *(uint32_t *)(m + flow + 0x4C) = id >> 16;
            *(uint32_t *)(m + flow + 0x54) = id & 0xFFFF;
            *(uint32_t *)(m + flow + 0x5C) = 93;
        }
        { static int ms_init = -1; static uint32_t ms_id; static uint32_t ms_done;
          uint32_t st = *(uint32_t *)(m + flow + 0x5C);
          if (ms_init < 0) { const char *e = getenv("JSRF_MSCRIPT_DUMP"); ms_init = e ? 1 : 0; ms_id = e ? (uint32_t)strtoul(e, NULL, 0) : 0; }
          /* at 14 before the setup runs, and at the first 15 after a 19 --
           * once the event's own flags have been set */
          if (ms_init && *(uint32_t *)(m + flow + 0x58) == ms_id) {
              if (st == 19) ms_done |= 2;
              if ((st == 14 && !(ms_done & 1)) || (st == 15 && (ms_done & 6) == 2)) {
                  ms_done |= st == 14 ? 1 : 4;
                  mscript_dump(flow, st);
              }
          } }
        /* Widescreen by scene: every mission is widened, the title (mission
         * 0x5A, the opening, the attract demo and its menu) from its first
         * frame. The title object's own draws -- its opening's giant logo and
         * bars, its logo and menus -- are a composition for a 4:3 screen that
         * parks its bars just past the 4:3 edges, so its bars go across
         * (widescreen_menus_title_bars, in every state but the city's) and
         * come in from the screen's edges, as the rest does with the widened
         * view, and the city behind them fills the sides.
         * JSRF_WIDE_TITLE=0 keeps the title 4:3, pillarboxed, until the title
         * object's first "please press start" (widescreen_menus_title_wide),
         * as test build menus5 had it. A flow handing over to the next one
         * (states 93/94) has no say. */
        { uint32_t mi = *(uint32_t *)(m + flow + 0x58), st = *(uint32_t *)(m + flow + 0x5C);
          if (st != 93 && st != 94) {
              static int title_wide = -1;
              int want;
              if (title_wide < 0) {
                  const char *v = getenv("JSRF_WIDE_TITLE");
                  title_wide = v && v[0] == '0' ? 0 : 1;
              }
              want = (mi != 0x5Au) || title_wide || widescreen_menus_title_wide(jsrf_now());
              if (want != s_wide_scene_said) {
                  s_wide_scene_said = want;
                  nv2a_set_wide_scene(want);
                  if (wide_log_on())
                      fprintf(stderr, "[WIDE] t=%.1f mission %08X: %s\n", pad_now(), mi,
                              want ? "widened" : "kept 4:3 (pillarboxed)");
              }
          } }
        if (log_on && *(uint32_t *)(m + flow + 0x5C) != last_state) {
            last_state = *(uint32_t *)(m + flow + 0x5C);
            fprintf(stderr, "[FLOW] t=%.1f state %u mission %08X\n", pad_now(), last_state,
                    *(uint32_t *)(m + flow + 0x58));
        }
        /* Gameplay > Graffiti souls > Visual indicator: the mission's soul
         * challenge read at its setup, before the setup runs, and dropped
         * when the flow hands over to the next (port/soul_indicator.c). */
        soul_indicator_flow(m, ADX_GUEST_RAM, flow, jsrf_now());
    }
    flow_anchor(m, flow);
    sub_00051FC0_gen();
}

/* ---- the intro (port/intro.c) ---------------------------------------------
 * sub_0007E360 updates the task that shows the cards the game opens with --
 * four logos, then the graffiti disclaimer -- which sub_0007E7D0 makes in the
 * boot sequence. Its state (+0x98) goes three to a card (fading in, held,
 * fading out), then 15, done: the task ends, and the boot goes on to the
 * title screen. While the cards show, a task of their own (sub_0007E260)
 * loads the title's files ahead of it. The cards' end stops that task
 * (sub_0007E0E0, from their destructor), and the title asks for whatever is
 * not in yet itself, and waits for it: its own files at sub_0004F8E0, its
 * mission's in the game flow's steps (0x1F9888: sub_0004ABA0 on).
 *
 * With Gameplay > Skips > Skip intro on, the task is put at its end before
 * its first update, with the screen's fade black, as the disclaimer leaves
 * it: the game opens at the title's loading, and no card is drawn. A start
 * at a mission (port/missions.c) leaves the cards out the same way, whatever
 * Skip intro says. [INTRO] logs each card's steps, with the time and the
 * game frame. */
#define INTRO_STATE        0x098u  /* three to a card: in, held, out; then done */
#define INTRO_PER_CARD     3u
#define INTRO_DONE         15u
#define INTRO_FADE_BLACK   0xFF000000u  /* ARGB, for the screen's fade */
#define INTRO_FADE_SET_RA  0x0007E75Cu  /* its return address from the cards' constructor */
extern void sub_0007E360_gen(void);
extern void sub_00024600(void);   /* the screen's fade, to a colour at once (cdecl) */
void sub_0007E360(void)
{
    static const char *const step[INTRO_PER_CARD] = { "fading in", "held", "fading out" };
    static uint32_t said = 0xFFFFFFFFu;
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t task = g_ecx, *t, now;
    if (task < 0x10000u || task + INTRO_STATE + 4 > (64u << 20)) { sub_0007E360_gen(); return; }
    t = (uint32_t *)(uintptr_t)(m + task);
    if ((intro_skip() || missions_starting()) && t[INTRO_STATE / 4] != INTRO_DONE) {
        const uint32_t black = INTRO_FADE_BLACK;
        t[INTRO_STATE / 4] = INTRO_DONE;
        guest_call_cdecl(sub_00024600, INTRO_FADE_SET_RA, 1, &black);
        fprintf(stderr, "[INTRO] t=%.2f frame %u: skipped (%s)\n", pad_now(), s_game_frames,
                missions_starting() ? "a start at a mission" : "Gameplay > Skips > Skip intro");
    }
    sub_0007E360_gen();
    now = t[INTRO_STATE / 4];
    if (now != said && now <= INTRO_DONE) {
        if (now == INTRO_DONE)
            fprintf(stderr, "[INTRO] t=%.2f frame %u: done\n", pad_now(), s_game_frames);
        else
            fprintf(stderr, "[INTRO] t=%.2f frame %u: card %u of %u %s\n", pad_now(), s_game_frames,
                    now / INTRO_PER_CARD + 1, INTRO_DONE / INTRO_PER_CARD, step[now % INTRO_PER_CARD]);
    }
    said = now;
}

/* ---- the game flow, and a start at a mission (port/missions.c) -------------
 * sub_0007BDD0 is the update of the game's flow object (its state at +0x48
 * picks one of 64 functions from the table at 0x20D2B8), once a frame, from
 * the boot through the title to each chapter's missions. A start at a mission
 * (JSRF_START_MISSION) changes what follows the boot, a choice the game makes
 * in this function's states and nowhere else; missions_flow makes it, before
 * the game's own update. The game's code here is right, and so are the
 * recompiler and the runtime: this is a feature, not a fix.
 *
 * In place of the title (state 0xA), missions_flow makes that state's own
 * calls but the title's scene task through missions_title, in its order:
 * the entrances' reset (sub_0004EAF0, cdecl), the progress object's reset
 * and its table at +0x6F58 (sub_0003AED0 and sub_0003B3C0, thiscall on the
 * progress object), and the music's fade (sub_001165D0, cdecl, the fade's
 * length). Each is called with the return address its call in state 0xA's
 * function (0x7C160) has. */
#define FLOW_PROGRESS            0x001EFFB0u  /* the progress object, the reset's this */
#define FLOW_MUSIC_FADE          0x3Cu        /* state 0xA's fade, in frames */
#define FLOW_ENTRANCES_RA        0x0007C1BDu  /* after state 0xA's call of sub_0004EAF0 */
#define FLOW_PROGRESS_RESET_RA   0x0007C1FFu  /* ... of sub_0003AED0 */
#define FLOW_PROGRESS_TABLE_RA   0x0007C209u  /* ... of sub_0003B3C0 */
#define FLOW_MUSIC_FADE_RA       0x0007C210u  /* ... of sub_001165D0 */
extern void sub_0004EAF0(void);
extern void sub_0003AED0(void);
extern void sub_0003B3C0(void);
extern void sub_001165D0(void);
static void missions_title(void)
{
    const uint32_t fade = FLOW_MUSIC_FADE;
    const uint32_t sv_ecx = g_ecx;
    guest_call_cdecl(sub_0004EAF0, FLOW_ENTRANCES_RA, 0, NULL);
    g_ecx = FLOW_PROGRESS;
    guest_call_cdecl(sub_0003AED0, FLOW_PROGRESS_RESET_RA, 0, NULL);
    guest_call_cdecl(sub_0003B3C0, FLOW_PROGRESS_TABLE_RA, 0, NULL);
    g_ecx = sv_ecx;
    guest_call_cdecl(sub_001165D0, FLOW_MUSIC_FADE_RA, 1, &fade);
}
extern void sub_0007BDD0_gen(void);
void sub_0007BDD0(void)
{
    missions_flow((uint8_t *)g_xbox_mem_offset, 64u << 20, g_ecx, missions_title, pad_now());
    sub_0007BDD0_gen();
}

/* ---- sub_0018E584: D3D's DAC gamma-ramp upload (CMiniport, runs in the
 * vblank DPC via sub_00193D10 when a SetGammaRamp is pending): writes the
 * 256-entry red, green and blue tables (768 bytes at the argument) to the
 * VGA DAC ports 0x6813C8/9 -- which on the console the display applies at
 * scan-out. JSRF_GAMMA_LOG=1: log each ramp (identity or not, samples). */
extern void sub_0018E584_gen(void);
void sub_0018E584(void)
{
    static int on = -1;
    if (on < 0) on = getenv("JSRF_GAMMA_LOG") ? 1 : 0;
    if (on) {
        uint8_t *m = (uint8_t *)g_xbox_mem_offset;
        uint32_t p = GARG(1);
        if (p >= 0x10000u && p + 0x300u < (64u << 20)) {
            const uint8_t *r = m + p;
            int i, c, ident = 1;
            for (c = 0; c < 3; c++) for (i = 0; i < 256; i++) if (r[c * 256 + i] != i) ident = 0;
            fprintf(stderr, "[GAMMA] t=%.2f ramp at %08X from %08X: %s |", pad_now(), p, GARG(0), ident ? "identity" : "NOT identity");
            for (c = 0; c < 3; c++) {
                static const int at[] = { 0, 16, 32, 64, 96, 128, 160, 192, 224, 255 };
                fprintf(stderr, " %c:", "RGB"[c]);
                for (i = 0; i < 10; i++) fprintf(stderr, " %u", r[c * 256 + at[i]]);
            }
            fprintf(stderr, "\n");
            fflush(stderr);
        }
    }
    sub_0018E584_gen();
}

/* ---- the player spawner (test only) --------------------------------------
 *
 * Task 0x1CD5C8's update (sub_000A8370) makes the players: for each of 32
 * slots it reads the game manager's variables (+0x7868 + 4*idx) -- 0x6D+i
 * the spawn request, 0x2D+i, 0x8D+i, 0xAD+i, 0xCD+i -- and the object table
 * (+0x98 + 4*idx) entry 0x0C+i; with a request and no object it builds a
 * player manager (sub_000A6980, vtable 0x1CD570, which builds the player,
 * vtable 0x1CCFF8), then clears 0x6D+i and 0x8D+i. JSRF_PLAYER_LOG=1 logs
 * those (and 0x0D+i, the slot's character: -1 and op 72 makes no player;
 * 0x14D+i) for the first four slots whenever one changes, and every player
 * manager made or destroyed -- to see whether a stage that comes up
 * without a player was never asked for one or lost it. */
static int player_log_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("JSRF_PLAYER_LOG") ? 1 : 0;
    return on;
}
/* The game manager's variable store, sub_000127C0(idx, value) -- with
 * JSRF_PLAYER_LOG, name whoever writes a spawn request (0x6D..0x70). */
extern void sub_000127C0_gen(void);
void sub_000127C0(void)
{
    if (player_log_on()) {
        uint32_t idx = GARG(1);
        if (idx >= 0x6D && idx <= 0x70) {
            uint8_t *m = (uint8_t *)g_xbox_mem_offset;
            const uint32_t *sp = (const uint32_t *)(m + g_esp);
            int i, n = 0;
            fprintf(stderr, "[PLAYER] t=%.2f set var %03X = %08X from %08X; stack:", pad_now(), idx, GARG(2), GARG(0));
            for (i = 3; i < 160 && n < 10; i++)
                if (sp[i] > 0x11000 && sp[i] < 0x18CB40) { fprintf(stderr, " %08X", sp[i]); n++; }
            fprintf(stderr, "\n");
        }
    }
    sub_000127C0_gen();
}
extern void sub_000A8370_gen(void);
void sub_000A8370(void)
{
    if (player_log_on()) {
        static uint32_t last[4][7]; static int init;
        uint8_t *m = (uint8_t *)g_xbox_mem_offset;
        uint32_t gm = *(uint32_t *)(m + 0x0022FCE0u), i, k;
        static const uint32_t var[6] = { 0x6D, 0x2D, 0x0D, 0xAD, 0x14D, 0x0B };
        if (!init) { init = 1; memset(last, 0xFF, sizeof last); }
        if (gm >= 0x10000u && gm + 0x8000u < (64u << 20))
            for (i = 0; i < 4; i++) {
                uint32_t now[7];
                for (k = 0; k < 6; k++) now[k] = *(uint32_t *)(m + gm + 0x7868u + 4u * (var[k] + (k < 5 ? i : 0)));
                now[6] = *(uint32_t *)(m + gm + 0x98u + 4u * (0x0Cu + i));
                if (memcmp(now, last[i], sizeof now)) {
                    fprintf(stderr, "[PLAYER] t=%.2f slot %u: req(6D) %08X 2D %08X char(0D) %08X AD %08X 14D %08X | obj(0C) %08X | var0B %08X\n",
                            pad_now(), i, now[0], now[1], now[2], now[3], now[4], now[6], now[5]);
                    memcpy(last[i], now, sizeof now);
                }
            }
    }
    sub_000A8370_gen();
}
extern void sub_000A6980_gen(void);
void sub_000A6980(void)
{
    if (player_log_on())
        fprintf(stderr, "[PLAYER] t=%.2f create manager %08X (args %08X %08X %08X %08X) from %08X\n",
                pad_now(), g_ecx, GARG(1), GARG(2), GARG(3), GARG(4), GARG(0));
    sub_000A6980_gen();
}
extern void sub_000A6110_gen(void);
void sub_000A6110(void)
{
    if (player_log_on())
        fprintf(stderr, "[PLAYER] t=%.2f destroy manager %08X from %08X\n", pad_now(), g_ecx, GARG(0));
    sub_000A6110_gen();
}

/* ---- sub_00116E30: the sound manager's per-frame update; counts frames,
 * and hands its slots to sound_keep.h before its slot step (sub_00116C30,
 * its first call) runs.
 *
 * JSRF_VOICE_LOG=1 (test only): the speech slots, frame by frame. The
 * sound manager (task 5) keeps two ADXTs for speech: +0x6C with its state
 * at +0xC4 and +0x70 with +0xC0 (1 loading, 2 ready or playing, 0 done).
 * The talk boxes use +0x70 alone (sub_00117140). An event alternates the
 * two (sub_00117280): it loads the next line paused into one slot while
 * the other plays, and unpauses it when the playing line ends -- the DJ K
 * scene's seventeen lines run back to back that way. The ADXT's status is
 * the byte at +1 (3 playing, 4 decoded, 5 ended), its pause flag +0x72.
 * [VOICE] lines give the time, the game frame and the present count for
 * every change, so a line's real start and end can be set against the
 * frames captured while it plays. */
extern void sub_0013A9A0_gen(void);
static int voice_log_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("JSRF_VOICE_LOG") ? 1 : 0;
    return on;
}
static uint32_t s_sndmgr;   /* the sound manager (task 5), seen by its update */

/* The sound manager's words that sound_slots reads. A slot's state is 1
 * while it loads. */
#define SNDMGR_READ           0x200u  /* as far into it as they reach, for the bounds check */
#define SNDMGR_ADXT_MUSIC     0x68u   /* the music's two ADXTs: this word and the next */
#define SNDMGR_ADXT_SPEECH_2  0x6Cu   /* the ADXT of speech's second slot and of the other two's second: the music's second */
#define SNDMGR_ADXT_SPEECH    0x70u   /* the ADXT of speech's first slot */
#define SNDMGR_ADXT_OTHER     0x74u   /* the ADXT of the other two slots' first */
#define SNDMGR_MUSIC_PLAYING  0x78u   /* bit 0: which of the music's two ADXTs plays */
#define SNDMGR_MUSIC_NEXT     0xA4u   /* set while the music's next track loads */
#define SNDMGR_STATE_MUSIC    0xBCu   /* the music's state */
#define SNDMGR_STATE_SPEECH   0xC0u   /* speech's first slot's state */
#define SNDMGR_STATE_SPEECH_2 0xC4u   /* speech's second slot's */
#define SNDMGR_STATE_OTHER    0xC8u   /* the other two slots' states */
#define SNDMGR_STATE_OTHER_2  0xCCu

/* The sound manager at o's slots as its slot step will find them, for
 * sound_keep_pass, while playback keeps the sound to the passes: loading,
 * in sound_keep_site's order, and the ADXT each loads. Of the music's two
 * ADXTs, SNDMGR_MUSIC_PLAYING says which plays; while SNDMGR_MUSIC_NEXT is
 * set, the next track loads in the other, and the music's own state isn't
 * looked at. */
static void sound_slots(uint32_t o)
{
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    int loading[SOUND_KEEP_SLOTS] = { 0 };
    uint32_t track[SOUND_KEEP_SLOTS] = { 0 };
    int passes = playback_sound_kept();
#define SND(off) (*(const uint32_t *)(m + o + (off)))
    if (passes && o >= 0x10000u && o + SNDMGR_READ < (64u << 20)) {
        uint32_t next = SND(SNDMGR_MUSIC_NEXT), cur = SND(SNDMGR_MUSIC_PLAYING) & 1u;
        loading[0] = next != 0;
        track[0] = SND(SNDMGR_ADXT_MUSIC + 4 * (cur ^ 1u));
        loading[1] = !next && SND(SNDMGR_STATE_MUSIC) == 1;
        track[1] = SND(SNDMGR_ADXT_MUSIC + 4 * cur);
        loading[2] = SND(SNDMGR_STATE_SPEECH) == 1;   track[2] = SND(SNDMGR_ADXT_SPEECH);
        loading[3] = SND(SNDMGR_STATE_OTHER) == 1;    track[3] = SND(SNDMGR_ADXT_OTHER);
        loading[4] = SND(SNDMGR_STATE_OTHER_2) == 1;  track[4] = SND(SNDMGR_ADXT_SPEECH_2);
        loading[5] = SND(SNDMGR_STATE_SPEECH_2) == 1; track[5] = SND(SNDMGR_ADXT_SPEECH_2);
    }
#undef SND
    sound_keep_pass(passes, loading, track);
}

extern void sub_00116E30_gen(void);
void sub_00116E30(void)
{
    /* two voice slots: +0x6C with its state at +0xC4, +0x70 with +0xC0 */
    static const uint32_t hoff[2] = { 0x6C, 0x70 }, soff[2] = { 0xC4, 0xC0 };
    static uint32_t last[2][4];
    static double next_level[2];
    static int init;
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t o = g_ecx;
    int k;
    s_game_frames++;
    freecam_frame();
    if (adx_err_on()) adx_err_sound_update();
    sound_slots(o);
    sub_00116E30_gen();
    if (!voice_log_on() || o < 0x10000u || o + 0x200u >= (64u << 20)) return;
    s_sndmgr = o;
    if (!init) { init = 1; memset(last, 0xFF, sizeof last); }
    for (k = 0; k < 2; k++) {
        uint32_t vs = *(uint32_t *)(m + o + soff[k]);
        uint32_t h = *(uint32_t *)(m + o + hoff[k]);
        uint32_t st, pz, f70;
        if (h < 0x10000u || h + 0x100u >= (64u << 20)) continue;
        st = (uint32_t)(int32_t)(int8_t)m[h + 1];
        pz = m[h + 0x72];
        f70 = m[h + 0x70] | (m[h + 0x6C] << 8);
        if (vs != last[k][0] || st != last[k][1] || pz != last[k][2] || f70 != last[k][3]) {
            fprintf(stderr, "[VOICE] t=%.3f frame %u flip %u slot %X: state %u adxt %08X status %d pause %u flags %04X\n",
                    pad_now(), s_game_frames, nv2a_gl_flips(), hoff[k], vs, h, (int)st, pz, f70);
            last[k][0] = vs; last[k][1] = st; last[k][2] = pz; last[k][3] = f70;
        }
        /* how full the decoded-sample buffer is, twice a second while the
         * line is live: rising means the output is not taking samples */
        if ((int)st >= 1 && (int)st <= 4 && pad_now() >= next_level[k]) {
            uint32_t a[2]; a[0] = h; a[1] = 0;
            next_level[k] = pad_now() + 0.5;
            fprintf(stderr, "[VOICE] t=%.3f slot %X obuf %u samples\n", pad_now(), hoff[k],
                    guest_call_cdecl(sub_0013A9A0_gen, 0x00116C5Cu, 2, a));
        }
    }
}

/* ---- sub_0013A9A0: a track's samples decoded (its ADXT's), which only the
 * sound manager's slot step asks, a slot ready at 0x2000: the count it sees
 * kept to the game's passes while playback keeps the sound (sound_keep.h).
 * The slot step's own call runs first; any more, while a slot's data is
 * waited for at its pass, run as the game's would, between naps in which
 * the game's thread gives up the guest lock, so the file system's and the
 * sound's threads, which bring the data, can run. */
typedef struct { uint32_t a[2], ra, first; int asked; } SoundCall;
static uint32_t sound_count(void *ctx)
{
    SoundCall *c = (SoundCall *)ctx;
    if (!c->asked++) return c->first;
    return guest_call_cdecl(sub_0013A9A0_gen, c->ra, 2, c->a);
}
static void sound_nap(void *ctx)
{
    extern int xbox_guest_lock_drop(void);
    extern void xbox_guest_lock_retake(int held);
    struct timespec ts = { 0, 1000000L };
    int held = xbox_guest_lock_drop();
    (void)ctx;
    nanosleep(&ts, NULL);
    xbox_guest_lock_retake(held);
}
void sub_0013A9A0(void)
{
    SoundCall c;
    int slot = sound_keep_site(GARG(0));
    c.ra = GARG(0);
    c.a[0] = GARG(1);
    c.a[1] = GARG(2);
    c.asked = 0;
    sub_0013A9A0_gen();
    if (slot < 0) return;
    c.first = g_eax;
    {
        SOUND_KEEP_IO io;
        io.count = sound_count;
        io.nap = sound_nap;
        io.now = pad_now;
        io.ctx = &c;
        g_eax = sound_keep_count(slot, &io);
    }
}

/* ---- speech: sub_00117140 plays a line for the talk boxes, sub_00117280
 * one for an event (arg 1 the slot, arg 2 the line), sub_00116810 stops
 * the voice, sub_0013AA80 is ADXT_Pause. Logged with JSRF_VOICE_LOG=1. */
extern void sub_00117140_gen(void);
void sub_00117140(void)
{
    if (voice_log_on())
        fprintf(stderr, "[VOICE] t=%.3f frame %u flip %u play id %u from %08X\n",
                pad_now(), s_game_frames, nv2a_gl_flips(), GARG(1), GARG(0));
    sub_00117140_gen();
}
extern void sub_00117280_gen(void);
void sub_00117280(void)
{
    if (voice_log_on())
        fprintf(stderr, "[VOICE] t=%.3f frame %u flip %u event play slot %u line %u from %08X\n",
                pad_now(), s_game_frames, nv2a_gl_flips(), GARG(1), GARG(2), GARG(0));
    sub_00117280_gen();
}
extern void sub_00116810_gen(void);
void sub_00116810(void)
{
    if (voice_log_on())
        fprintf(stderr, "[VOICE] t=%.3f frame %u flip %u stop from %08X\n",
                pad_now(), s_game_frames, nv2a_gl_flips(), GARG(0));
    sub_00116810_gen();
}
extern void sub_0013AA80_gen(void);
void sub_0013AA80(void)
{
    if (voice_log_on() && s_sndmgr) {
        uint8_t *m = (uint8_t *)g_xbox_mem_offset;
        uint32_t h = GARG(1);
        if (h == *(uint32_t *)(m + s_sndmgr + 0x6C) || h == *(uint32_t *)(m + s_sndmgr + 0x70))
            fprintf(stderr, "[VOICE] t=%.3f frame %u pause %08X %u from %08X\n",
                    pad_now(), s_game_frames, h, GARG(2), GARG(0));
    }
    sub_0013AA80_gen();
}

/* ---- sub_001912A0: D3D's KickOff -------------------------------------------
 *
 * Hands the push buffer to the GPU: sets the write-combine flush bit
 * (+0x100410, 0x10000), spins until the GPU clears it, then writes the new
 * DMA_PUT. The GPU here is the ack thread in xbox_memory_layout.c, which
 * polled every 200 us, so each kickoff waited out a poll: in the DJ K scene
 * the game thread spent 84% of its time in this spin and the scene ran at
 * 42-56 fps. While KickOff runs the ack thread now polls without sleeping,
 * and it is woken once more for the new DMA_PUT. RECOMP_NV2A_KICK=0 = old. */
extern void xbox_nv2a_kick(void);
extern void xbox_nv2a_hurry(int on);
extern void sub_001912A0_gen(void);
void sub_001912A0(void)
{
    xbox_nv2a_hurry(1);
    sub_001912A0_gen();
    xbox_nv2a_hurry(0);
    xbox_nv2a_kick();
}

/* ---- sub_001910E0: how far the GPU is behind the title (test only) ---------
 *
 * D3D's make-space path (0x00191440) inserts NOP(5), kicks, and asks this how
 * much of the push buffer the GPU still has to run before it reaches that
 * point: under 32 KB it spins; 32 KB or more it sleeps until the GPU raises
 * software method 5 -- the branch that froze the game for good while nothing
 * delivered software methods (kernel_pgraph_tick in src/kernel/nv2a_intr.c).
 * Taken only when the GPU thread happens to be far behind, so only now and
 * then.
 *
 * JSRF_TEST_MAKESPACE_WAIT=1 takes it every time: the answer to the call from
 * 0x001914C6 is never below 32 KB. With the software methods delivered the
 * game plays on; with RECOMP_NV2A_SWM=0 it stops at the first make-space --
 * the 3 October freeze, on demand. */
extern void sub_001910E0_gen(void);
void sub_001910E0(void)
{
    static int force = -1;
    uint32_t from = GARG(0);
    if (force < 0) {
        const char *e = getenv("JSRF_TEST_MAKESPACE_WAIT");
        force = (e && e[0] == '1') ? 1 : 0;
    }
    sub_001910E0_gen();
    if (force && from == 0x001914C6u && g_eax < 0x8000u) {
        static unsigned long n;
        if (n++ < 3)
            fprintf(stderr, "  [TEST] make-space: sleeping branch forced (#%lu)\n", n);
        g_eax = 0x8000u;
    }
}

/* ---- sub_00191440: D3D's wait for the GPU to reach a point -----------------
 *
 * (time, flag), stdcall. D3D numbers the work it hands the GPU, and fences in
 * the push buffer write those numbers to one word. This returns once the word
 * has reached `time`. Short of it, D3D puts in a fence for `time` if there is
 * none yet (sub_00191390), finds where that fence is (sub_00191150), and asks
 * how much of the push buffer the GPU still has to read to get there
 * (sub_001910E0): 32 KB or more, it sleeps until the GPU raises NOP(5) there;
 * less, it spins on the word, as the console's GPU reads that much in
 * microseconds.
 *
 * Here the GPU is a thread paced to the frame rate, and the game queues a
 * frame or more ahead of it, so the spin lasted until that thread came back
 * from frame pacing's sleep: 55 to 71% of the game thread's time in the
 * garage and the menus on the Ally X. So this takes the game's steps up to
 * the spin, the same calls in the same order, and where it would spin it
 * sleeps with the guest lock given up, until the GPU thread writes the
 * number (nv2a_pb_wait_fence, in xboxrecomp's nv2a_pb_exec.c). The game's
 * own code then runs: it finds the word there and returns, or, where the
 * thread can't sleep, spins as before. RECOMP_FENCE_WAIT=0 spins always. */
#define D3D_DEVICE         0x0019DCE0u /* the pointer to D3D's device */
#define D3D_NEWEST         0x030u      /* the newest number handed out */
#define D3D_FENCE_WORD     0x034u      /* the address of the word the fences write */
#define D3D_WAIT_FAR       0x8000u     /* from this far on, D3D sleeps on NOP(5) */
#define D3D_FENCE_FOR_RET  0x0019146Fu /* where sub_00191390 returns to */
#define D3D_FIND_RET       0x00191478u /* where sub_00191150 returns to */
#define D3D_HOW_FAR_RET    0x00191489u /* where sub_001910E0 returns to */
extern void sub_00191440_gen(void);
extern void sub_00191390(void);
extern void sub_00191150(void);
extern int nv2a_pb_wait_fence(uint32_t va, uint32_t top, uint32_t time);
void sub_00191440(void)
{
    uint32_t time = GARG(1), dev = MEM32(D3D_DEVICE);
    uint32_t top = MEM32(dev + D3D_NEWEST);
    if (top - time < top - MEM32(MEM32(dev + D3D_FENCE_WORD))) {
        uint32_t sv_eax = g_eax, sv_ecx = g_ecx, sv_edx = g_edx, none = 0, at;
        if (time == top)
            guest_call_cdecl(sub_00191390, D3D_FENCE_FOR_RET, 1, &none);
        g_eax = time;
        at = guest_call_cdecl(sub_00191150, D3D_FIND_RET, 0, NULL);
        g_ecx = dev;
        g_edx = at;
        if (at && guest_call_cdecl(sub_001910E0, D3D_HOW_FAR_RET, 0, NULL) < D3D_WAIT_FAR)
            nv2a_pb_wait_fence(MEM32(dev + D3D_FENCE_WORD), MEM32(dev + D3D_NEWEST), time);
        g_eax = sv_eax; g_ecx = sv_ecx; g_edx = sv_edx;
    }
    sub_00191440_gen();
}

/* ---- sub_00013A80: one pass of the game's main loop (a count, and a test
 * only log) ------
 * Each pass is counted, at its start, for what the port draws once a pass
 * (the graffiti soul indicator, after the gauge, below).
 * JSRF_FRAME_LOG=1: once a second, how many passes ran, how many of them
 * presented (+0x74 set = no Present this pass) and how many waited a vblank
 * (+0x94), and the vblank counter the game keeps at 0x265174. Input playback
 * keys the pad to these passes, the game's own frames (port/playback.c). */
static unsigned s_loop_passes;  /* the main loop's passes so far */
static void photo_frame(uint32_t gm);
extern void sub_00013A80_gen(void);
void sub_00013A80(void)
{
    static int on = -1; static unsigned n, nopres, waitv; static double next; static uint32_t v0;
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t o = g_ecx;
    uint32_t w94 = (o > 0x10000u && o < (64u << 20)) ? *(uint32_t *)(m + o + 0x94) : 0;
    s_game_thread = 1;
    s_loop_passes++;
    playback_pass();
    photo_frame(o);             /* before the pass takes up the holds asked for */
    if (on < 0) on = getenv("JSRF_FRAME_LOG") ? 1 : 0;
    sub_00013A80_gen();
    if (!on || o < 0x10000u || o >= (64u << 20)) return;
    n++;
    if (*(uint32_t *)(m + o + 0x74)) nopres++;
    if (w94) waitv++;
    if (pad_now() >= next) {
        uint32_t v = *(uint32_t *)(m + 0x265174);
        fprintf(stderr, "[FRAMES] t=%.1f passes %u, no-present %u, vblank-wait %u, vblanks %u, sound updates %u\n",
                pad_now(), n, nopres, waitv, v - v0, s_game_frames);
        n = nopres = waitv = 0; v0 = v; next = pad_now() + 1.0;
    }
}

extern void sub_00030490_gen(void);
void sub_00030490(void) { mission_hook(".bin"); sub_00030490_gen(); }
extern void sub_00030690_gen(void);
void sub_00030690(void) { mission_hook(".dat"); sub_00030690_gen(); }

/* ---- the game's resource loads, checked (resource_load.c) -----------------
 * sub_00032C70, the character loader's step, releases a character's .dat and
 * can keep the pointer, which a character unload then releases again: after
 * the step, it is forgotten once its buffer is released. The graffiti cache
 * builder's four functions that release its file buffer keep the pointer
 * too, and its sheets' writes (sub_0002D8D0, sub_0002D950) release it again:
 * after each of the four, it is forgotten once released, and before each
 * write what was forgotten is logged. The hooks are on those functions, not
 * on the builder's step, as the step can give the guest lock up between a
 * release and its end. sub_0004AC80 and sub_0004ACB0, the flow's steps that
 * ask for its stage, are logged, and the stage handed to Far buildings. */
#include "resource_load.h"
#include "draw_distance.h"
extern int xbox_HeapIsFreeSpace(uint32_t xbox_va);   /* released, or never handed out */
extern void sub_00032C70_gen(void);
extern void sub_0002DA70_gen(void);
extern void sub_0002D8D0_gen(void);
extern void sub_0002D950_gen(void);
extern void sub_0002D9D0_gen(void);
extern void sub_0004AC80_gen(void);
extern void sub_0004ACB0_gen(void);
void sub_00032C70(void)
{
    uint32_t mgr = g_ecx;
    sub_00032C70_gen();
    resource_load_char_step((uint8_t *)g_xbox_mem_offset, mgr, xbox_HeapIsFreeSpace, pad_now());
}
/* A texture copied into the sheet. */
void sub_0002DA70(void)
{
    uint32_t mgr = g_ecx;
    sub_0002DA70_gen();
    resource_load_graffiti_release((uint8_t *)g_xbox_mem_offset, mgr, xbox_HeapIsFreeSpace);
}
/* Sheet 0 written: first, for random graffiti, a texture of its own made of
 * it where the build is one of the other sets' (rg_sheet, below). */
static void rg_sheet(uint32_t sheet);
void sub_0002D8D0(void)
{
    uint32_t mgr = g_ecx;
    resource_load_graffiti_sheet(0, pad_now());
    rg_sheet(0);
    sub_0002D8D0_gen();
    resource_load_graffiti_release((uint8_t *)g_xbox_mem_offset, mgr, xbox_HeapIsFreeSpace);
}
/* Sheet 1 written. */
void sub_0002D950(void)
{
    uint32_t mgr = g_ecx;
    resource_load_graffiti_sheet(1, pad_now());
    rg_sheet(1);
    sub_0002D950_gen();
    resource_load_graffiti_release((uint8_t *)g_xbox_mem_offset, mgr, xbox_HeapIsFreeSpace);
}
/* A ready-made sheet copied. */
void sub_0002D9D0(void)
{
    uint32_t mgr = g_ecx;
    sub_0002D9D0_gen();
    resource_load_graffiti_release((uint8_t *)g_xbox_mem_offset, mgr, xbox_HeapIsFreeSpace);
}
/* Each also tells Far buildings the stage asked for, which its rules for one
 * map know it by (draw_buildings_stage), and input playback, as an anchor. */
static void stage_asked(uint32_t stage)
{
    if (stage == RESOURCE_LOAD_NO_STAGE) return;
    draw_buildings_stage(stage);
    playback_anchor(PLAYBACK_STAGE, stage, game_random());
}
void sub_0004AC80(void)
{
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t flow = g_ecx, state = resource_load_flow_state(m, flow);
    sub_0004AC80_gen();
    stage_asked(resource_load_stage_step(m, flow, state, pad_now()));
}
void sub_0004ACB0(void)
{
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t flow = g_ecx, state = resource_load_flow_state(m, flow);
    sub_0004ACB0_gen();
    stage_asked(resource_load_stage_step(m, flow, state, pad_now()));
}

/* JSRF_TRACE_AT="t:frames[:shots,every][;t:frames[:shots,every]...]" (test
 * only): at each time t, log every render pass for that many frames and
 * capture a burst of frames -- up to eight of them, in time order, so one
 * run can photograph the title, the menus and the level. */
static void trace_at_step(void)
{
    static int n = -1, cur; static double t[8]; static int frames[8], shots[8], every[8];
    if (n < 0) {
        const char *e = getenv("JSRF_TRACE_AT");
        n = 0;
        while (e && *e && n < 8) {
            shots[n] = 0; every[n] = 1;
            if (sscanf(e, "%lf:%d:%d,%d", &t[n], &frames[n], &shots[n], &every[n]) >= 2) n++;
            e = strchr(e, ';');
            if (e) e++;
        }
    }
    if (cur >= n || pad_now() < t[cur]) return;
    fprintf(stderr, "[TRACE] t=%.1f tracing %d frames, %d shots every %d\n", pad_now(),
            frames[cur], shots[cur], every[cur]);
    nv2a_gl_trace_frames(frames[cur]);
    if (shots[cur] > 0) nv2a_gl_dump_burst(shots[cur], every[cur]);
    cur++;
}
static void auto_step(const float *camf, const float *chf)
{
    static double next_say;
    double now, dx, dz, dist, fx, fz, fl, rx, rz, sx, sy, sl, scale;
    int i, cur = -1;
    if (s_auto_n < 0) auto_load();
    if (s_auto_n <= 0) return;
    now = pad_now();
    for (i = 0; i < s_auto_n; i++)
        if (s_auto[i].state != 2) { if (now >= s_auto[i].t) cur = i; break; }
    if (cur < 0) { s_auto_live = 0; return; }
    dx = s_auto[cur].x - chf[0xCA4 / 4];
    dz = s_auto[cur].z - chf[0xCAC / 4];
    dist = sqrt(dx * dx + dz * dz);
    if (s_auto[cur].state == 0) {
        s_auto[cur].state = 1; s_auto[cur].t_on = now;
        fprintf(stderr, "[AUTO] t=%.1f waypoint %d: heading for (%.0f, %.0f) from (%.1f, %.1f), %.0f units\n",
                now, cur, s_auto[cur].x, s_auto[cur].z, chf[0xCA4 / 4], chf[0xCAC / 4], dist);
    }
    if (dist < 12.0 || now - s_auto[cur].t_on > 40.0) {
        s_auto[cur].state = 2; s_auto_live = 0; s_auto[cur].t_done = now;
        fprintf(stderr, "[AUTO] t=%.1f waypoint %d %s at (%.1f %.1f %.1f)\n", now, cur,
                dist < 12.0 ? "reached" : "GAVE UP", chf[0xCA4 / 4], chf[0xCA8 / 4], chf[0xCAC / 4]);
        return;
    }
    fx = camf[0x5C / 4] - camf[0x44 / 4];
    fz = camf[0x64 / 4] - camf[0x4C / 4];
    fl = sqrt(fx * fx + fz * fz);
    if (!(fl > 1e-3)) { s_auto_live = 0; return; }
    fx /= fl; fz /= fl;
    rx = fz; rz = -fx;                          /* left-handed: right of forward */
    sx = (dx * rx + dz * rz) / dist;
    sy = (dx * fx + dz * fz) / dist;
    sl = sqrt(sx * sx + sy * sy);
    if (!(sl > 1e-6)) { s_auto_live = 0; return; }
    scale = 32767.0 / sl;
    if (dist < 40.0) scale *= (dist < 16.0 ? 0.4 : dist / 40.0);
    s_auto_lx = (int16_t)(sx * scale);
    s_auto_ly = (int16_t)(sy * scale);
    s_auto_live = 1;
    if (now >= next_say) {
        next_say = now + 1.0;
        fprintf(stderr, "[AUTO] t=%.1f at (%.1f %.1f %.1f) -> (%.0f, %.0f) %.0f units, stick (%d, %d)\n",
                now, chf[0xCA4 / 4], chf[0xCA8 / 4], chf[0xCAC / 4], s_auto[cur].x, s_auto[cur].z,
                dist, s_auto_lx, s_auto_ly);
    }
}
/* The test hooks' part of each pad read (port/pad.c's pad_read). At the
 * first read: the press script and the autopilot's waypoints. At each:
 * JSRF_TRACE_AT's traces, then, on port 0 while a script or the autopilot
 * is set, the pad they make, in place of the host's. */
void pad_test_start(void)
{
    pad_script_load();
    if (s_auto_n < 0) auto_load();
}
int pad_test_inputs(uint32_t port, XBOX_INPUT_STATE *st)
{
    static uint32_t packet;
    trace_at_step();
    if (port == 0 && (s_pad_script_n > 0 || s_auto_n > 0)) {
        double now = pad_now();
        for (int i = 0; i < s_pad_script_n; i++)
            if (now >= s_pad_script[i].t && now < s_pad_script[i].t + s_pad_script[i].dur) {
                st->Gamepad.wButtons |= s_pad_script[i].buttons;
                if (s_pad_script[i].has_axis) {
                    if (s_pad_script[i].axis[0]) st->Gamepad.sThumbLX = s_pad_script[i].axis[0];
                    if (s_pad_script[i].axis[1]) st->Gamepad.sThumbLY = s_pad_script[i].axis[1];
                    if (s_pad_script[i].axis[2]) st->Gamepad.sThumbRX = s_pad_script[i].axis[2];
                    if (s_pad_script[i].axis[3]) st->Gamepad.sThumbRY = s_pad_script[i].axis[3];
                }
                for (int k = 0; k < 8; k++) if (s_pad_script[i].analog[k]) st->Gamepad.bAnalogButtons[k] = s_pad_script[i].analog[k];
                /* A scheduled press that is never polled looks exactly like
                 * a press the title ignored, and the two want completely
                 * different fixes. Say which one happened, once per entry. */
                if (!s_pad_script[i].said) {
                    s_pad_script[i].said = 1;
                    fprintf(stderr, "[PAD] %s delivered at %.1fs\n",
                            s_pad_script[i].name, now);
                }
            }
        if (s_auto_live) { st->Gamepad.sThumbLX = s_auto_lx; st->Gamepad.sThumbLY = s_auto_ly; }
        /* JSRF_TALK_AUTO_A=<t> (t > 1): stop at t seconds. The camera mode
         * it keys on is only updated while the follow camera runs, so after
         * a cut to an event it can stay 12 and skip the event's lines. */
        if (s_talk_auto_a < 0) {
            const char *v = getenv("JSRF_TALK_AUTO_A");
            s_talk_auto_a = v != NULL;
            s_talk_auto_a_until = (v && atof(v) > 1.0) ? atof(v) : 1e30;
        }
        if (s_talk_auto_a && now < s_talk_auto_a_until && s_cam_mode_now == 12 && fmod(now, 2.0) < 0.2)
            st->Gamepad.bAnalogButtons[XBOX_BUTTON_A] = 255;
        for (int i = 0; i < s_auto_n && i < 16; i++) {
            double dt = now - s_auto[i].t_done;
            const char *b = s_auto[i].btn;
            if (s_auto[i].state != 2 || !b[0] || dt < 0) continue;
            if (!((dt < 0.3) || (dt >= 1.5 && dt < 1.8) || (dt >= 3.0 && dt < 3.3))) continue;
            if (!strcmp(b, "A")) st->Gamepad.bAnalogButtons[XBOX_BUTTON_A] = 255;
            else if (!strcmp(b, "B")) st->Gamepad.bAnalogButtons[XBOX_BUTTON_B] = 255;
            else if (!strcmp(b, "X")) st->Gamepad.bAnalogButtons[XBOX_BUTTON_X] = 255;
            else if (!strcmp(b, "Y")) st->Gamepad.bAnalogButtons[XBOX_BUTTON_Y] = 255;
            else if (!strcmp(b, "LT")) st->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER] = 255;
            else if (!strcmp(b, "RT")) st->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER] = 255;
            else if (!strcmp(b, "START")) st->Gamepad.wButtons |= XBOX_GAMEPAD_START;
            { static int said[16]; int n = dt < 0.3 ? 0 : dt < 1.8 ? 1 : 2;
              if (!(said[i] & (1 << n))) { said[i] |= 1 << n;
                fprintf(stderr, "[AUTO] t=%.1f arrival press %s (%d of 3) for waypoint %d\n", now, b, n + 1, i); } }
        }
        st->dwPacketNumber = ++packet;
        return 1;
    }
    return 0;
}

/* The XDK's pad API, answered by port/pad.c. */
/* XGetDevices(type) */
void sub_001BD5FF(void)
{
    g_eax = pad_get_devices((uint8_t *)g_xbox_mem_offset, GARG(1));
    g_esp += 4 + 4;
}
/* XGetDeviceChanges(type, &ins, &rem): pad.c says why it reports no change. */
void sub_001BD621(void)
{
    g_eax = pad_get_device_changes((uint8_t *)g_xbox_mem_offset, GARG(1), GARG(2), GARG(3));
    g_esp += 4 + 12;
}
/* XInputOpen(type, port, slot, polling) */
void sub_001C3BA1(void)
{
    g_eax = pad_open(GARG(1), GARG(2));
    g_esp += 4 + 16;
}
/* XInputClose(h) */
void sub_001C3C16(void)
{
    g_esp += 4 + 4;
}
/* XInputGetCapabilities(h, caps) */
void sub_001C3C22(void)
{
    g_eax = pad_get_capabilities((uint8_t *)g_xbox_mem_offset, GARG(1), GARG(2));
    g_esp += 4 + 8;
}
/* XInputGetState(h, state) */
void sub_001C3E14(void)
{
    g_eax = pad_get_state((uint8_t *)g_xbox_mem_offset, GARG(1), GARG(2));
    g_esp += 4 + 8;
}
/* XInputSetState(h, feedback): pad.c says where the motors' speeds are. */
void sub_001C3E85(void)
{
    g_eax = pad_set_state((uint8_t *)g_xbox_mem_offset, GARG(1), GARG(2));
    g_esp += 4 + 8;
}
/* XInputPoll(h) */
void sub_001C3EBD(void)
{
    g_eax = 0; g_esp += 4 + 4;
}

/* ---- DirectSound DSP doorbell (sub_001A1769) ----------------------------
 * The XDK's DirectSound sends a command to the APU's GP DSP and then spins:
 *
 *     add  ebx, 0x810
 *     rep  movsb                  ; the command block
 *     mov  dword ptr [ebx], eax   ; ring the doorbell
 *     cmp  dword ptr [ebx], 0
 *     jne  0x1A18D0               ; ... until the DSP clears it
 *
 * Nothing here runs DSP56300 code, so the word never clears and the title's
 * audio thread spins for ever -- and in JSRF that thread is the one the whole
 * engine is created on, so the title never reaches its first frame.
 *
 * The APU can clear it (that is what RECOMP_APU_DSP_ACK does), but the address
 * is a DirectSound heap allocation: it moves whenever the heap layout changes,
 * and hunting for it again with a debugger after every build is not a fix. It
 * is derivable from the object this method is called on, so derive it, and
 * hand it to the APU the first time through.
 *
 *     doorbell = *(*(*(this + 8) + 0x10)) + 0x810
 */
extern void sub_001A1769_gen(void);
extern void mcpx_apu_dsp_ack_add(uint32_t addr);
void sub_001A1769(void)
{
    static uint32_t registered;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t p = g_ecx;

    if (p) p = *(const uint32_t *)(m + p + 8);
    if (p) p = *(const uint32_t *)(m + p + 0x10);
    if (p) p = *(const uint32_t *)(m + p);
    if (p && p != registered) {
        registered = p;
        mcpx_apu_dsp_ack_add(p + 0x810);
    }
    sub_001A1769_gen();
}

/* ---- unaligned locked operations ---------------------------------------
 *
 * See RECOMP_ATOMIC_ADD32 in recomp_types.h. x86 allows a lock-prefixed
 * read-modify-write at any address; ARM's exclusive-load instructions do not,
 * and raise SIGBUS instead. The guest depends on these being atomic across
 * its threads, so the misaligned case is serialised under a lock rather than
 * quietly becoming non-atomic.
 *
 * Striped by address so that the rare unaligned refcount does not become a
 * contention point for every other one, and memcpy rather than a cast because
 * an unaligned uint32_t lvalue is undefined even where the hardware allows
 * the access.
 */
#include <pthread.h>

#define RECOMP_UNALIGNED_LOCKS 64
static pthread_mutex_t g_unaligned_locks[RECOMP_UNALIGNED_LOCKS] = {
#define M15 PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, \
            PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    M15 M15 M15 M15 M15 M15 M15 M15
    M15 M15 M15 M15 M15 M15 M15 M15
#undef M15
};

static pthread_mutex_t *unaligned_lock_for(const void *p)
{
    uintptr_t a = (uintptr_t)p >> 2;
    a ^= a >> 11;
    return &g_unaligned_locks[a & (RECOMP_UNALIGNED_LOCKS - 1)];
}

uint32_t recomp_atomic_add32_unaligned(void *p, uint32_t v)
{
    pthread_mutex_t *m = unaligned_lock_for(p);
    uint32_t old, neu;
    pthread_mutex_lock(m);
    memcpy(&old, p, 4);
    neu = old + v;
    memcpy(p, &neu, 4);
    pthread_mutex_unlock(m);
    return old;
}

uint32_t recomp_atomic_cas32_unaligned(void *p, uint32_t cmp, uint32_t val)
{
    pthread_mutex_t *m = unaligned_lock_for(p);
    uint32_t old;
    pthread_mutex_lock(m);
    memcpy(&old, p, 4);
    if (old == cmp) memcpy(p, &val, 4);
    pthread_mutex_unlock(m);
    return old;
}

/* ==========================================================================
 * The skeleton poser, measured and replayed.
 *
 * sub_0005F0F0(obj, model, motion, frame[, mode]) poses a character: it
 * evaluates `motion` at `frame` over the node tree and stores one 4x4 matrix
 * per bone at obj+0x20 (the count is at obj+0x1c). Every rudie drawn in a
 * dialogue close-up goes through here -- sub_00048DB0 calls it via
 * sub_00047970 just before it builds the skinning palette.
 *
 * JSRF_POSE_CENSUS=<flip>  For every call during that frame and the next,
 *     print the arguments and how far each bone's up axis leans from bone
 *     0's. A spine folded backwards is a large number on a low bone.
 *
 * JSRF_SNAP=<flip>[:<k>]   On the k-th call (default 1) during the first frame
 * JSRF_SNAP_DIR=<dir>      at or after <flip>, write guest RAM and the CPU
 *     state to <dir>/snap_in.bin as the call begins and <dir>/snap_out.bin as
 *     it returns. tools/snap_replay.py runs the ORIGINAL x86 from snap_in in
 *     an emulator and compares with snap_out: any byte the two disagree on is
 *     something the recompiled code computed that the Xbox would not have.
 * ========================================================================== */
#include <math.h>
#include <string.h>
extern size_t g_xbox_total_ram;

static void jsrf_snap_write(const char *dir, const char *name, uint32_t va,
                            uint32_t phase, uint32_t flip, uint32_t idx)
{
    char path[1024];
    uint8_t hdr[512];
    uint32_t w[16];
    size_t ram = g_xbox_total_ram ? g_xbox_total_ram : ((size_t)64 << 20);
    const RecompXmm *x[8] = { &g_xmm0, &g_xmm1, &g_xmm2, &g_xmm3,
                              &g_xmm4, &g_xmm5, &g_xmm6, &g_xmm7 };
    FILE *f;
    int i;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[SNAP] cannot write %s\n", path); return; }
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "JSRFSNAP", 8);
    w[0] = 1; w[1] = va; w[2] = phase; w[3] = (uint32_t)ram;
    w[4] = g_eax; w[5] = g_ecx; w[6] = g_edx; w[7] = g_ebx;
    w[8] = g_esp; w[9] = g_ebp; w[10] = g_esi; w[11] = g_edi;
    w[12] = g_fs_base; w[13] = (uint32_t)g_fp_top; w[14] = flip; w[15] = idx;
    memcpy(hdr + 8, w, sizeof w);                 /* 8 .. 72   */
    memcpy(hdr + 72, (const void *)g_fp_stack, 64);  /* 72 .. 136 */
    for (i = 0; i < 8; i++) memcpy(hdr + 136 + 16 * i, x[i], 16);  /* .. 264 */
    fwrite(hdr, 1, sizeof hdr, f);
    fflush(f);
    /* Page by page with pwrite: a page the runtime has decommitted cannot be
     * read, and one fwrite of the whole span stops dead at the first such
     * page (the first rudie snapshot came out 37 MB of 64). Unreadable pages
     * are written as zeros and flagged in a bitmap after the RAM image, so
     * the replay can leave them unmapped instead of trusting zeros. */
    {
        int fd = fileno(f);
        static uint8_t zero[4096];
        static uint8_t bitmap[(256u << 20) / 4096 / 8];
        size_t npages = ram / 4096, pg, bad = 0;
        memset(bitmap, 0, sizeof bitmap);
        for (pg = 0; pg < npages; pg++) {
            const uint8_t *src = (const uint8_t *)g_xbox_mem_offset + pg * 4096;
            off_t off = (off_t)sizeof hdr + (off_t)pg * 4096;
            if (pwrite(fd, src, 4096, off) != 4096) {
                (void)pwrite(fd, zero, 4096, off);
                bad++;
            } else if (pg / 8 < sizeof bitmap) {
                bitmap[pg / 8] |= (uint8_t)(1u << (pg & 7));
            }
        }
        (void)pwrite(fd, bitmap, npages / 8, (off_t)sizeof hdr + (off_t)ram);
        if (bad) fprintf(stderr, "[SNAP] %s: %zu unreadable page(s) written as zeros\n", name, bad);
    }
    fclose(f);
}

static void jsrf_pose_census(uint32_t flip, uint32_t idx, uint32_t obj,
                             uint32_t model, uint32_t motion, uint32_t fbits,
                             uint32_t from)
{
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t nb, mats;
    float fr;
    char line[2048];
    int n = 0, i;
    memcpy(&fr, &fbits, 4);
    if (obj < 0x10000u || obj >= lim) return;
    nb = *(const uint32_t *)(m + obj + 0x1c);
    mats = *(const uint32_t *)(m + obj + 0x20);
    fprintf(stderr, "[POSE] flip %u #%u obj=%08X model=%08X motion=%08X frame=%.3f"
            " bones=%u mats=%08X from=%08X", flip, idx, obj, model, motion, fr,
            nb, mats, from);
    if (motion >= 0x10000u && motion < lim)
        fprintf(stderr, " mhdr=%08X %08X %08X", *(const uint32_t *)(m + motion),
                *(const uint32_t *)(m + motion + 4), *(const uint32_t *)(m + motion + 8));
    fprintf(stderr, "\n");
    if (nb == 0 || nb > 128 || mats < 0x10000u || mats + nb * 64u > lim) return;
    {
        const float *b0 = (const float *)(m + mats);
        float ux = b0[4], uy = b0[5], uz = b0[6];
        float ul = sqrtf(ux * ux + uy * uy + uz * uz);
        n += snprintf(line + n, sizeof line - n, "[POSE]   lean from bone0 (deg):");
        for (i = 1; i < (int)nb && n < (int)sizeof line - 16; i++) {
            const float *b = (const float *)(m + mats + 64u * (uint32_t)i);
            float vx = b[4], vy = b[5], vz = b[6];
            float vl = sqrtf(vx * vx + vy * vy + vz * vz);
            float c = (ul > 0 && vl > 0) ? (ux * vx + uy * vy + uz * vz) / (ul * vl) : 1.0f;
            if (c > 1) c = 1;
            if (c < -1) c = -1;
            n += snprintf(line + n, sizeof line - n, " %d", (int)lrintf(acosf(c) * 57.29578f));
        }
        fprintf(stderr, "%s\n", line);
        fprintf(stderr, "[POSE]   bone0 at (%.2f %.2f %.2f) up (%.3f %.3f %.3f)"
                " bone1 at (%.2f %.2f %.2f)\n", b0[12], b0[13], b0[14], ux, uy, uz,
                nb > 1 ? b0[16 + 12] : 0.f, nb > 1 ? b0[16 + 13] : 0.f,
                nb > 1 ? b0[16 + 14] : 0.f);
    }
    fflush(stderr);
}

static double jsrf_t0;
__attribute__((constructor)) static void jsrf_t0_init(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    jsrf_t0 = ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---- the snapshot trigger, for any wrapped function ------------------------
 *
 * JSRF_SNAP_VA=<va>        which wrapped function to capture (default the
 *                          poser, 0x5F0F0)
 * JSRF_SNAP=<flip>[:<k>]   the k-th matching call at or after that flip, or
 * JSRF_SNAP=@<sec>[:<k>]   ... at or after that many seconds after launch
 * JSRF_SNAP_ARG=<n>=<val>  only calls whose n-th stack argument is <val>
 * JSRF_SNAP_DIR=<dir>      where snap_in.bin / snap_out.bin go            */
static struct {
    int init, have_arg, done;
    uint32_t va, flip, k, argi, argv, seen;
    double sec;
    const char *dir;
} SN;

static double jsrf_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9 - jsrf_t0;
}

static int jsrf_snap_begin(uint32_t va)
{
    uint32_t fl;
    if (!SN.init) {
        const char *v = getenv("JSRF_SNAP_VA"), *s = getenv("JSRF_SNAP");
        const char *a = getenv("JSRF_SNAP_ARG");
        SN.va = v ? (uint32_t)strtoul(v, 0, 0) : 0x0005F0F0u;
        SN.sec = -1; SN.k = 1;
        if (s) {
            char *e;
            if (*s == '@') { SN.sec = strtod(s + 1, &e); SN.flip = 0xFFFFFFFFu; }
            else SN.flip = (uint32_t)strtoul(s, &e, 0);
            if (*e == ':') SN.k = (uint32_t)strtoul(e + 1, 0, 0);
        }
        if (a) {
            char *e;
            SN.argi = (uint32_t)strtoul(a, &e, 0);
            if (*e == '=') { SN.argv = (uint32_t)strtoul(e + 1, 0, 0); SN.have_arg = 1; }
        }
        SN.dir = getenv("JSRF_SNAP_DIR");
        if (!SN.dir) SN.dir = ".";
        SN.init = 1;
    }
    if (SN.done || !SN.flip || va != SN.va) return 0;
    fl = nv2a_gl_flips();
    if (SN.sec >= 0) {
        double now = jsrf_now();
        if (now < SN.sec) return 0;
        SN.flip = fl; SN.sec = -1;
        fprintf(stderr, "[SNAP] armed at flip %u (%.1f s after launch) for sub_%08X call #%u\n",
                fl, now, va, SN.k);
    }
    if (fl < SN.flip) return 0;
    if (SN.have_arg && GARG(SN.argi) != SN.argv) return 0;
    if (++SN.seen != SN.k) return 0;
    fprintf(stderr, "[SNAP] capturing sub_%08X call #%u at flip %u: esp=%08X from=%08X"
            " args %08X %08X %08X %08X %08X\n", va, SN.seen, fl, g_esp, GARG(0),
            GARG(1), GARG(2), GARG(3), GARG(4), GARG(5));
    jsrf_snap_write(SN.dir, "snap_in.bin", va, 0, fl, SN.seen);
    return 1;
}

static void jsrf_snap_end(uint32_t va)
{
    jsrf_snap_write(SN.dir, "snap_out.bin", va, 1, nv2a_gl_flips(), SN.seen);
    SN.done = 1;
    fprintf(stderr, "[SNAP] written: %s/snap_in.bin, snap_out.bin\n", SN.dir);
    fflush(stderr);
}

static void jsrf_cam_counts(void);
/* ---- where the rudies are ---------------------------------------------------
 *
 * JSRF_WHERE=<seconds>: every that many seconds, one line per 20-bone skeleton
 * posed in world space (the pass whose root has an exactly vertical up axis --
 * the other pass is camera space and tilts with the camera), giving its world
 * position. Enough to steer a scripted run towards a character. */
static void jsrf_where(uint32_t fl, uint32_t obj)
{
    static double period = -1, next_at;
    static uint32_t where_flip;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t nb, mats;
    const float *b0;
    if (period < 0) {
        const char *e = getenv("JSRF_WHERE");
        period = e ? atof(e) : 0;
        next_at = period;
    }
    if (period <= 0) return;
    if (jsrf_now() >= next_at) { where_flip = fl + 1; next_at = jsrf_now() + period; }
    if (fl != where_flip) return;
    if (obj < 0x10000u || obj >= (64u << 20)) return;
    nb = *(const uint32_t *)(m + obj + 0x1c);
    mats = *(const uint32_t *)(m + obj + 0x20);
    if (nb != 20 || mats < 0x10000u || mats + 64u * 20u > (64u << 20)) return;
    b0 = (const float *)(m + mats);
    if (fabsf(b0[5] - 1.0f) > 1e-3f || fabsf(b0[6]) > 1e-3f) return;
    fprintf(stderr, "[WHERE] t=%.1f flip %u obj=%08X at (%.1f %.1f %.1f)\n",
            jsrf_now(), fl, obj, b0[12], b0[13], b0[14]);
}

/* ---- where a skater is posed: JSRF_EVT_LOG=<seconds> ------------------------
 *
 * The character select's preview skaters were drawn into the floor and under
 * the rails they grind. Every that many seconds, for each skeleton posed
 * through sub_00047970 (the usual "pose this skeleton with this motion"):
 * who called sub_00047970 (the return address above its saved ebx), the
 * matrix the skeleton was posed under (the matrix stack's top: translation and
 * up axis -- up exactly vertical means a world-space pass), and the posed
 * bones: bone 0, bone 1, the lowest and the highest bone, in that space. Plus,
 * from sub_00080BD0, each character's own position (+0xCA4). Enough to tell a
 * skater posed low (bones under the matrix origin) from one placed low.
 * Developer > Logging's row sets it to 0.2. */
static double evt_period = -1;
static int evt_on(void)
{
    if (evt_period < 0) {
        const char *e = getenv("JSRF_EVT_LOG");
        evt_period = e ? atof(e) : 0;
    }
    return evt_period > 0;
}
static void jsrf_evt_pose(uint32_t caller, uint32_t obj, uint32_t motion)
{
    static struct { uint32_t obj, caller; double next; } seen[256];
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t nb, mats, top, i, lo = 0, hi = 0;
    const float *mt, *b;
    float ylo = 1e30f, yhi = -1e30f;
    int k, slot = -1;
    double now = jsrf_now();
    if (obj < 0x10000u || obj + 0x24u > lim) return;
    top = *(const uint32_t *)(m + 0x264C04u);
    top = (top >= 0x10000u && top + 4 <= lim) ? *(const uint32_t *)(m + top) : 0;
    mt = (top >= 0x10000u && top + 64 <= lim) ? (const float *)(m + top) : NULL;
    /* One line per object and pass kind (world: up exactly vertical). */
    {
        uint32_t kind = (mt && fabsf(mt[5] - 1.0f) < 1e-3f && fabsf(mt[4]) < 1e-3f && fabsf(mt[6]) < 1e-3f) ? 1u : 0u;
        uint32_t key = caller ^ kind;
        for (k = 0; k < 256; k++) {
            if (seen[k].obj == obj && seen[k].caller == key) { slot = k; break; }
            if (!seen[k].obj) { seen[k].obj = obj; seen[k].caller = key; slot = k; break; }
        }
        if (slot < 0) {                    /* full: a new scene's objects -- start over */
            memset(seen, 0, sizeof seen);
            seen[0].obj = obj; seen[0].caller = key; slot = 0;
        }
        if (now < seen[slot].next) return;
        seen[slot].next = now + evt_period;
        nb = *(const uint32_t *)(m + obj + 0x1c);
        mats = *(const uint32_t *)(m + obj + 0x20);
        fprintf(stderr, "[EVT] t=%.2f flip %u obj %08X from %08X %s", now, nv2a_gl_flips(), obj, caller,
                kind ? "world" : "other");
        if (mt) fprintf(stderr, " | matrix at (%.2f %.2f %.2f) up (%.3f %.3f %.3f)",
                        mt[12], mt[13], mt[14], mt[4], mt[5], mt[6]);
        if (motion >= 0x10000u && motion + 0x10u <= lim)
            fprintf(stderr, " | motion %08X root flags %08X", *(const uint32_t *)(m + motion),
                    *(const uint32_t *)(m + motion + 8));
        if (nb == 20 && mats >= 0x10000u && mats + 64u * 20u <= lim) {
            for (i = 0; i < 20; i++) {
                b = (const float *)(m + mats + 64u * i);
                if (b[13] < ylo) { ylo = b[13]; lo = i; }
                if (b[13] > yhi) { yhi = b[13]; hi = i; }
            }
            b = (const float *)(m + mats);
            fprintf(stderr, " | bone0 (%.2f %.2f %.2f) bone1 (%.2f %.2f %.2f) lowest %u y %.2f highest %u y %.2f",
                    b[12], b[13], b[14], b[16 + 12], b[16 + 13], b[16 + 14], lo, ylo, hi, yhi);
        }
        fprintf(stderr, "\n");
        fflush(stderr);
    }
}
static void jsrf_evt_char(uint32_t self)
{
    static struct { uint32_t obj; double next; } seen[64];
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const float *p;
    int k, slot = -1;
    double now = jsrf_now();
    if (self < 0x10000u || self + 0xCB0u > (64u << 20)) return;
    for (k = 0; k < 64; k++) {
        if (seen[k].obj == self) { slot = k; break; }
        if (!seen[k].obj) { seen[k].obj = self; slot = k; break; }
    }
    if (slot < 0) { memset(seen, 0, sizeof seen); seen[0].obj = self; slot = 0; }
    if (now < seen[slot].next) return;
    seen[slot].next = now + evt_period;
    p = (const float *)(m + self + 0xCA4u);
    fprintf(stderr, "[EVTCHAR] t=%.2f flip %u char %08X at (%.2f %.2f %.2f)\n", now, nv2a_gl_flips(),
            self, p[0], p[1], p[2]);
    fflush(stderr);
}

/* ---- the poser: census + snapshot ---------------------------------------- */
extern void sub_0005F0F0_gen(void);
void sub_0005F0F0(void)
{
    static int init;
    static uint32_t census_flip, cur_flip = 0xFFFFFFFFu, idx;
    static double census_sec = -1;
    uint32_t fl, from, obj, model, motion, fr;
    int snap;
    if (!init) {
        const char *c = getenv("JSRF_POSE_CENSUS");
        if (c && *c == '@') census_sec = atof(c + 1);
        else census_flip = c ? (uint32_t)strtoul(c, 0, 0) : 0;
        init = 1;
    }
    /* Called through sub_00047970: its caller's return address sits above
     * sub_00047970's saved ebx, at GARG(7). */
    if (evt_on() && GARG(0) == 0x0004798Du) {
        uint32_t caller = GARG(7), o = GARG(1), mo = GARG(3);
        sub_0005F0F0_gen();
        jsrf_evt_pose(caller, o, mo);
        return;
    }
    snap = jsrf_snap_begin(0x0005F0F0u);
    if (!census_flip && census_sec < 0) {
        uint32_t o = GARG(1);
        sub_0005F0F0_gen();
        if (snap) jsrf_snap_end(0x0005F0F0u);
        jsrf_where(nv2a_gl_flips(), o);
        jsrf_cam_counts();
        return;
    }
    fl = nv2a_gl_flips();
    if (census_sec >= 0 && jsrf_now() >= census_sec) {
        census_flip = fl + 1; census_sec = -1;
        fprintf(stderr, "[POSE] census armed for flip %u\n", census_flip);
    }
    if (fl != cur_flip) { cur_flip = fl; idx = 0; }
    idx++;
    from = GARG(0); obj = GARG(1); model = GARG(2); motion = GARG(3); fr = GARG(4);
    sub_0005F0F0_gen();
    if (snap) jsrf_snap_end(0x0005F0F0u);
    if (census_flip && (fl == census_flip || fl == census_flip + 1))
        jsrf_pose_census(fl, idx, obj, model, motion, fr, from);
    jsrf_where(fl, obj);
}

/* ---- the pose sampler: sub_0005E7B0 ----------------------------------------
 *
 * Samples a source motion at a (fractional) frame into a one-key motion --
 * the kind every rudie in a dialogue is posed from. JSRF_SAMPLE_LOG=1 prints
 * each call: the source motion and frame, the floor/ceil the original would
 * take, and the first joints' angles it produced.                         */
static void jsrf_motion_brief(const char *tag, uint32_t mo)
{
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t ch, n, i;
    if (mo < 0x10000u || mo >= (64u << 20)) { fprintf(stderr, " %s=%08X", tag, mo); return; }
    ch = *(const uint32_t *)(m + mo + 4);
    n = (*(const uint32_t *)(m + mo) >> 8) & 0xFF;
    fprintf(stderr, " %s=%08X[type %02X nodes %u frames %d]", tag, mo, m[mo],
            n, *(const int16_t *)(m + mo + 2));
    if (ch < 0x10000u || ch >= (64u << 20)) return;
    for (i = 1; i < n && i < 5; i++) {
        uint32_t c = ch + 16u * i, fl = *(const uint32_t *)(m + c);
        uint32_t ap = *(const uint32_t *)(m + c + 8);
        if ((fl & 0x10) && ap >= 0x10000u && ap < (64u << 20))
            fprintf(stderr, " n%u(%d %d %d)", i, *(const int16_t *)(m + ap),
                    *(const int16_t *)(m + ap + 2), *(const int16_t *)(m + ap + 4));
    }
}


/* ---- where the rudies' folded pose first appears ---------------------------
 * JSRF_FOLD_TRACE=1: in the sampler and the blender, note a destination pose
 * whose joint 1 and joint 3 pitch are both strongly negative (the folded
 * dialogue pose starts n1 x=-6792, n3 x=-10084) where it was not before the
 * call, and print every input with its own joint angles -- the first such
 * line names the computation that produced the fold. */
static int jsrf_node_ang(uint32_t mo, uint32_t node, uint32_t key, int16_t out[3])
{
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t ch, c, fl, ap;
    if (mo < 0x10000u || mo >= (64u << 20)) return 0;
    ch = *(const uint32_t *)(m + mo + 4);
    if (ch < 0x10000u || ch >= (64u << 20)) return 0;
    c = ch + 16u * node;
    fl = *(const uint32_t *)(m + c);
    ap = *(const uint32_t *)(m + c + 8);
    if (!(fl & 0x10) || ap < 0x10000u || ap >= (64u << 20)) return 0;
    ap += 6u * key;
    out[0] = *(const int16_t *)(m + ap); out[1] = *(const int16_t *)(m + ap + 2);
    out[2] = *(const int16_t *)(m + ap + 4);
    return 1;
}
static int jsrf_is_folded(uint32_t mo)
{
    int16_t a1[3], a3[3];
    if (!jsrf_node_ang(mo, 1, 0, a1) || !jsrf_node_ang(mo, 3, 0, a3)) return 0;
    return a1[0] < -4000 && a3[0] < -8000;
}
static int jsrf_fold_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("JSRF_FOLD_TRACE") ? 1 : 0;
    return on;
}
static void jsrf_fold_report(const char *who, uint32_t dst, const uint32_t *a, int na,
                             int was)
{
    static int shown;
    int i;
    if (shown >= 60) return;
    shown++;
    fprintf(stderr, "[FOLD] %s made dst %08X folded (was %s) at flip %u t=%.2f from=%08X:",
            who, dst, was ? "folded" : "normal", nv2a_gl_flips(), jsrf_now(), a[0]);
    for (i = 1; i < na; i++) fprintf(stderr, " %08X", a[i]);
    fprintf(stderr, "\n");
    for (i = 1; i < na; i++) {
        uint32_t mo = a[i];
        int16_t q[3];
        uint32_t n;
        if (mo < 0x10000u || mo >= (64u << 20)) continue;
        if ((*(const uint32_t *)((const uint8_t *)g_xbox_mem_offset + mo) & 0xFF) != 0x11
         && (*(const uint32_t *)((const uint8_t *)g_xbox_mem_offset + mo) & 0xFF) != 0x15) continue;
        fprintf(stderr, "[FOLD]   arg%d", i);
        jsrf_motion_brief("", mo);
        fprintf(stderr, " folded=%d", jsrf_is_folded(mo));
        for (n = 5; n < 8; n++)
            if (jsrf_node_ang(mo, n, 0, q)) fprintf(stderr, " n%u(%d %d %d)", n, q[0], q[1], q[2]);
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}
extern void sub_0005E7B0_gen(void);
void sub_0005E7B0(void)
{
    /* JSRF_SAMPLE_LOG=1 or =@<sec> (from that many seconds on);
     * JSRF_SAMPLE_DST=<lo>:<hi> keeps only samples into that address range. */
    static int on = -1, shown;
    static double from_sec;
    static uint32_t dlo, dhi = 0xFFFFFFFFu;
    uint32_t a[6], i;
    float fr;
    int snap = jsrf_snap_begin(0x0005E7B0u);
    if (on < 0) {
        const char *e = getenv("JSRF_SAMPLE_LOG"), *d = getenv("JSRF_SAMPLE_DST");
        on = e ? 1 : 0;
        from_sec = (e && *e == '@') ? atof(e + 1) : 0;
        if (d) { char *x; dlo = (uint32_t)strtoul(d, &x, 0); if (*x == ':') dhi = (uint32_t)strtoul(x + 1, 0, 0); }
    }
    for (i = 0; i < 6; i++) a[i] = GARG(i);
    { int was = jsrf_fold_on() ? jsrf_is_folded(a[2]) : 0;
      sub_0005E7B0_gen();
      if (jsrf_fold_on() && !was && jsrf_is_folded(a[2]))
          jsrf_fold_report("sample(sub_0005E7B0)", a[2], a, 6, was); }
    if (snap) jsrf_snap_end(0x0005E7B0u);
    if (on && shown < 600 && a[2] >= dlo && a[2] <= dhi
        && (from_sec <= 0 || jsrf_now() >= from_sec)) {
        shown++;
        memcpy(&fr, &a[5], 4);
        fprintf(stderr, "[SAMPLE] flip %u t=%.1f from=%08X a1=%08X a3=%08X frame=%.4f"
                " (floor %.0f ceil %.0f)", nv2a_gl_flips(), jsrf_now(), a[0], a[1],
                a[3], fr, floor(fr), ceil(fr));
        jsrf_motion_brief("src", a[4]);
        jsrf_motion_brief("dst", a[2]);
        fprintf(stderr, "\n");
        fflush(stderr);
    }
}

/* ---- the motion blender: sub_0005EC20(tree, dst, ?, motionA, frameA,
 * motionB, frameB, weight) -------------------------------------------------
 * Writes the pose a rudie is actually drawn with (the store watch on Corn's
 * pose buffer names sub_0005E480, called from here, as its only steady
 * writer). JSRF_BLEND_LOG=@<sec> with JSRF_SAMPLE_DST=<lo>:<hi> prints each
 * call into that range: both motions, both frames, the weight, and the
 * first joints it produced. */
extern void sub_0005EC20_gen(void);
void sub_0005EC20(void)
{
    static int on = -1, shown;
    static double from_sec;
    static uint32_t dlo, dhi = 0xFFFFFFFFu;
    uint32_t a[9], i;
    int snap = jsrf_snap_begin(0x0005EC20u);
    if (on < 0) {
        const char *e = getenv("JSRF_BLEND_LOG"), *d = getenv("JSRF_SAMPLE_DST");
        on = e ? 1 : 0;
        from_sec = (e && *e == '@') ? atof(e + 1) : 0;
        if (d) { char *x; dlo = (uint32_t)strtoul(d, &x, 0); if (*x == ':') dhi = (uint32_t)strtoul(x + 1, 0, 0); }
    }
    for (i = 0; i < 9; i++) a[i] = GARG(i);
    { int was = jsrf_fold_on() ? jsrf_is_folded(a[2]) : 0;
      sub_0005EC20_gen();
      if (jsrf_fold_on() && !was && jsrf_is_folded(a[2]))
          jsrf_fold_report("blend(sub_0005EC20)", a[2], a, 9, was); }
    if (snap) jsrf_snap_end(0x0005EC20u);
    if (on && shown < 400 && a[2] >= dlo && a[2] <= dhi
        && (from_sec <= 0 || jsrf_now() >= from_sec)) {
        float fa, fb, w;
        shown++;
        memcpy(&fa, &a[5], 4); memcpy(&fb, &a[7], 4); memcpy(&w, &a[8], 4);
        fprintf(stderr, "[BLEND] flip %u t=%.1f from=%08X tree=%08X a3=%08X frameA=%.4f"
                " frameB=%.4f w=%.4f", nv2a_gl_flips(), jsrf_now(), a[0], a[1], a[3],
                fa, fb, w);
        jsrf_motion_brief("A", a[4]);
        jsrf_motion_brief("B", a[6]);
        jsrf_motion_brief("dst", a[2]);
        fprintf(stderr, "\n");
        fflush(stderr);
    }
}

/* ---- DirectSound: a voice-done notification for a voice nobody owns ---------
 *
 * sub_001A200D is the APU interrupt handler's per-voice step: it looks the
 * hardware voice up in DirectSound's owner table (dsound+0x2C4, 256 entries)
 * and, if that voice is still on its list, retires it (sub_001A2E2E). It
 * never checks the table entry for NULL -- on the console a notification
 * only ever arrives for a voice that is playing, so the entry is always set.
 *
 * Here one arrives for hardware voice 0 while no DirectSound voice owns it.
 * With a NULL entry the handler reads the "voice" at address 0; the
 * console's page 0 is unmapped and that would fault, ours reads zeros, so
 * the checks pass and sub_001A2E2E retires a voice at address 0. Its list
 * link reads as NULL, CONTAINING_RECORD turns that into 0xFFFFFFB4, and the
 * next load lands at guest 0xFFFFFFBE: "[CRASH] signal 10 ... fault
 * addr=0x3FFFFFFBE in sub_001A2E2E" -- the black screen on New Game (23 Sep,
 * a player's run and 1 of 2 reproductions with those settings).
 *
 * There is nothing to retire when nobody owns the voice, so the notification
 * is dropped. Why the voice processor reports voice 0 at all is an APU
 * emulation question and is logged so it can be followed up. */
extern void sub_001A200D_gen(void);
void sub_001A200D(void)
{
    /* Only a NULL entry is caught. Owners live wherever DirectSound put them
     * -- in practice the contiguous window at 0x80000000 -- so anything that
     * is not page 0 is left to the original code, exactly as before. (The
     * first version of this guard also rejected pointers above 64 MB, which
     * threw away every legitimate notification and stalled the audio thread:
     * 23 Sep, run g2.) The address is computed exactly as the original does,
     * so this read is no riskier than the one it guards. */
    static unsigned drops;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t hw = GARG(1);
    uint32_t owner = *(const uint32_t *)(m + (uint32_t)(g_ecx + 0x2C4u + hw * 4u));
    if (owner < 0x10000u) {
        unsigned n = ++drops;
        if (n <= 8 || (n & (n - 1)) == 0) {
            fprintf(stderr, "[DSOUND] voice-done notification for hardware voice %u,"
                    " which no DirectSound voice owns (entry %08X) -- dropped (%u so far)\n",
                    hw, owner, n);
            fflush(stderr);
        }
        g_esp += 8;          /* ret 4: return address and the one argument */
        return;
    }
    sub_001A200D_gen();
}


/* ---- talk events: which one starts, when, and from where --------------------
 *
 * sub_00038460 is the event manager's "start talk event N" (virtual, slot 8
 * of the table at 0x1EC068; thiscall, the event number as its one argument;
 * it records N at +0x1578 and the loader state machine at sub_00038890 then
 * reads TE<N>.bin). JSRF_TALK_LOG=1 (Developer > Logging, as are the
 * other JSRF_TALK_LOG lines below) prints every call with its return
 * address, so the code that decided to start a talk can be found. Added
 * 23 Sep: Gum's talk (TE002) starts the moment Corn's ends, with Beat still
 * 570 units away from her. */
extern void sub_00038460_gen(void);
void sub_00038460(void)
{
    static int on = -1;
    if (on < 0) on = getenv("JSRF_TALK_LOG") != NULL;
    if (on) {
        const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
        uint32_t cur = (g_ecx >= 0x10000u && g_ecx < (64u << 20))
                     ? *(const uint32_t *)(m + g_ecx + 0x1578u) : 0xFFFFFFFFu;
        fprintf(stderr, "[TALK] t=%.1f flip %u: start talk event %u (was %u) this=%08X"
                " from %08X  stack %08X %08X %08X %08X %08X %08X\n",
                jsrf_now(), nv2a_gl_flips(), GARG(1), cur, g_ecx, GARG(0),
                GARG(2), GARG(3), GARG(4), GARG(5), GARG(6), GARG(8));
        fflush(stderr);
    }
    sub_00038460_gen();
}


/* ---- the event manager's command and query API ------------------------------
 *
 * sub_000256A0(cmd, arg) runs command <cmd> (table 0x1EC068, 33 entries; 8 is
 * "start talk event <arg>") on the event manager, and sub_000256C0(cmd, arg)
 * asks it question <cmd> (table 0x1EC178). The tutorial is plain code calling
 * these -- 52 command and 84 query call sites -- so logging them gives the
 * tutorial's script as it actually runs. JSRF_TALK_LOG=1 prints each
 * (call site, cmd, arg, result) the first time and whenever the result
 * changes: queries are polled every frame. */
static int ev_log_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("JSRF_TALK_LOG") != NULL;
    return on;
}
static int ev_changed(uint32_t from, uint32_t cmd, uint32_t arg, uint32_t res)
{
    static struct { uint32_t from, cmd, arg, res; int used; } t[512];
    uint32_t h = (from * 2654435761u ^ cmd * 40503u ^ arg * 97u) & 511u;
    int i;
    for (i = 0; i < 512; i++) {
        uint32_t k = (h + (uint32_t)i) & 511u;
        if (!t[k].used) {
            t[k].used = 1; t[k].from = from; t[k].cmd = cmd; t[k].arg = arg; t[k].res = res;
            return 1;
        }
        if (t[k].from == from && t[k].cmd == cmd && t[k].arg == arg) {
            if (t[k].res == res) return 0;
            t[k].res = res;
            return 1;
        }
    }
    return 1;
}
/* The game's letting go of the display set, held back while it loaded for
 * the skip prompt's A: done before the game asks about the set again (the
 * prompt's A, below). */
static void disp_let_go_held(const char *where);
extern void sub_000256A0_gen(void);
void sub_000256A0(void)
{
    uint32_t from = GARG(0), cmd = GARG(1), arg = GARG(2);
    disp_let_go_held("before the game's command");
    sub_000256A0_gen();
    if (ev_log_on() && ev_changed(from, cmd | 0x80000000u, arg, g_eax)) {
        fprintf(stderr, "[EVCMD] t=%.2f flip %u cmd %u arg %u (0x%X) -> %u from %08X\n",
                jsrf_now(), nv2a_gl_flips(), cmd, arg, arg, g_eax, from);
        fflush(stderr);
    }
}
extern void sub_000256C0_gen(void);
void sub_000256C0(void)
{
    uint32_t from = GARG(0), cmd = GARG(1), arg = GARG(2);
    disp_let_go_held("before the game's question");
    sub_000256C0_gen();
    if (ev_log_on() && ev_changed(from, cmd, arg, g_eax)) {
        fprintf(stderr, "[EVQRY] t=%.2f flip %u query %u arg %u (0x%X) -> %u from %08X\n",
                jsrf_now(), nv2a_gl_flips(), cmd, arg, arg, g_eax, from);
        fflush(stderr);
    }
}


/* ---- the stage script runner ------------------------------------------------
 *
 * sub_0005B3F0 (thiscall) walks the stage's script: header at this+0x1040,
 * entries at header+0x174 (0x54 bytes each), count at header+0x178, current
 * index at this+0x2B0. An entry runs only if its flag conditions hold
 * (entry[0] -> list, entry[1] = count; sub_00039B50 checks one flag), and
 * entry[4] is its opcode; 0x7E..0xEF are "blocking" and handed back to the
 * caller, the rest go to sub_000585E0 (which issues event commands).
 * JSRF_TALK_LOG=1 dumps the whole script once and logs every index change. */
extern void sub_0005B3F0_gen(void);
void sub_0005B3F0(void)
{
    static uint32_t dumped_hdr;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t self = g_ecx, hdr = 0, before = 0, after;
    int on = ev_log_on() && self >= 0x10000u && self < lim;
    if (on) {
        hdr = *(const uint32_t *)(m + self + 0x1040u);
        before = *(const uint32_t *)(m + self + 0x2B0u);
        if (hdr >= 0x10000u && hdr < lim && hdr != dumped_hdr) {
            uint32_t ents = *(const uint32_t *)(m + hdr + 0x174u);
            uint32_t n = *(const uint32_t *)(m + hdr + 0x178u), i, k;
            dumped_hdr = hdr;
            fprintf(stderr, "[SCRIPT] t=%.2f runner %08X header %08X: %u entries at %08X\n",
                    jsrf_now(), self, hdr, n, ents);
            for (i = 0; i < n && i < 512 && ents >= 0x10000u && ents + (i + 1) * 0x54u < lim; i++) {
                const uint32_t *e = (const uint32_t *)(m + ents + i * 0x54u);
                char line[512]; int len = 0;
                len += snprintf(line + len, sizeof line - len, "[SCRIPT]   #%-3u op %3u (0x%02X)", i, e[4], e[4]);
                for (k = 0; k < 21; k++)
                    len += snprintf(line + len, sizeof line - len, " %08X", e[k]);
                if (e[1] && e[1] < 16 && e[0] >= 0x10000u && e[0] + e[1] * 4u < lim) {
                    len += snprintf(line + len, sizeof line - len, "  conds:");
                    for (k = 0; k < e[1]; k++)
                        len += snprintf(line + len, sizeof line - len, " %08X",
                                        *(const uint32_t *)(m + e[0] + k * 4u));
                }
                fprintf(stderr, "%s\n", line);
            }
            fflush(stderr);
        }
    }
    sub_0005B3F0_gen();
    if (on) {
        after = *(const uint32_t *)(m + self + 0x2B0u);
        if (after != before) {
            fprintf(stderr, "[SCRIPT] t=%.2f flip %u index %u -> %u, returned %d\n",
                    jsrf_now(), nv2a_gl_flips(), before, after, (int32_t)g_eax);
            fflush(stderr);
        }
    }
}


/* ---- story flags and the triggers that read them ------------------------------
 *
 * The flag manager (object at 0x1EFFB0) keeps four banks of 512 bits at
 * +0x6A14, +0x18, +0x58, +0x98. A condition word is bank | index << 3 |
 * value << 19; sub_00039B50 tests one, sub_00039BE0 sets one. The event
 * triggers are entries of 0x54 bytes run every frame by sub_000585E0(list,
 * index): if all of the entry's conditions hold and the event manager is
 * idle, its opcode becomes an event command. JSRF_TALK_LOG=1 logs every flag
 * that changes (with the code that changed it) and every trigger that fires. */
static uint32_t jsrf_flag_word(uint32_t fm, uint32_t cond, uint32_t *addr, uint32_t *bit)
{
    static const uint32_t bank_off[4] = { 0x6A14u, 0x18u, 0x58u, 0x98u };
    uint32_t bank = cond & 7u, idx = (cond >> 3) & 0xFFFFu;
    if (bank > 3 || idx >= 0x200u) return 0;
    *addr = fm + bank_off[bank] + (idx >> 5) * 4u;
    *bit = idx & 31u;
    return 1;
}
extern void sub_00039BE0_gen(void);
void sub_00039BE0(void)
{
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t fm = g_ecx, cond = GARG(1), from = GARG(0), a = 0, b = 0, was = 0, now;
    int ok = ev_log_on() && fm >= 0x10000u && fm < (64u << 20) && jsrf_flag_word(fm, cond, &a, &b);
    if (ok) was = (*(const uint32_t *)(m + a) >> b) & 1u;
    sub_00039BE0_gen();
    if (ok) {
        now = (*(const uint32_t *)(m + a) >> b) & 1u;
        if (now != was) {
            fprintf(stderr, "[FLAG] t=%.2f flip %u bank %u flag %u := %u (cond %08X) from %08X\n",
                    jsrf_now(), nv2a_gl_flips(), cond & 7u, (cond >> 3) & 0xFFFFu, now, cond, from);
            fflush(stderr);
        }
    }
}
extern void sub_000585E0_gen(void);
void sub_000585E0(void)
{
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t self = g_ecx, list = GARG(1), idx = GARG(2), busy0 = 0, busy1;
    int on = ev_log_on() && self >= 0x10000u && self < lim;
    if (on) busy0 = *(const uint32_t *)(m + self + 0x238u);
    /* Every entry whose conditions have just become true -- whatever its
     * opcode, not only the ones that issue an event command. */
    if (on && list >= 0x10000u && list + (idx + 1) * 0x54u < lim) {
        static struct { uint32_t list, idx; int held; } seen[1024];
        const uint32_t *e = (const uint32_t *)(m + list + idx * 0x54u);
        uint32_t k, a, b, h, ok = 1;
        for (k = 0; k < e[1] && k < 16; k++) {
            uint32_t c;
            if (e[0] < 0x10000u || e[0] + (k + 1) * 4u >= lim) { ok = 0; break; }
            c = *(const uint32_t *)(m + e[0] + k * 4u);
            if (!jsrf_flag_word(0x1EFFB0u, c, &a, &b)) { ok = 0; break; }
            if (((*(const uint32_t *)(m + a) >> b) & 1u) != ((c >> 19) & 1u)) { ok = 0; break; }
        }
        h = (list * 2654435761u ^ idx * 97u) & 1023u;
        for (k = 0; k < 1024; k++) {
            uint32_t j = (h + k) & 1023u;
            if (!seen[j].list) { seen[j].list = list; seen[j].idx = idx; seen[j].held = 0; h = j; break; }
            if (seen[j].list == list && seen[j].idx == idx) { h = j; break; }
        }
        if (ok && !seen[h].held) {
            char line[640]; int len = 0;
            len += snprintf(line + len, sizeof line - len,
                            "[ENTRY] t=%.2f flip %u list %08X #%u op %u (0x%X) args %08X %08X %08X %08X %08X %08X; conds",
                            jsrf_now(), nv2a_gl_flips(), list, idx, e[4], e[4], e[5], e[6], e[7], e[8], e[9], e[10]);
            for (k = 0; k < e[1] && k < 12; k++) {
                uint32_t c = *(const uint32_t *)(m + e[0] + k * 4u);
                len += snprintf(line + len, sizeof line - len, " [b%u f%u =%u]",
                                c & 7u, (c >> 3) & 0xFFFFu, (c >> 19) & 1u);
            }
            len += snprintf(line + len, sizeof line - len, "; sets");
            for (k = 0; k < e[3] && k < 12 && e[2] >= 0x10000u && e[2] + (k + 1) * 4u < lim; k++) {
                uint32_t c = *(const uint32_t *)(m + e[2] + k * 4u);
                len += snprintf(line + len, sizeof line - len, " [b%u f%u =%u]",
                                c & 7u, (c >> 3) & 0xFFFFu, (c >> 19) & 1u);
            }
            fprintf(stderr, "%s\n", line);
            fflush(stderr);
        }
        seen[h].held = (int)ok;
    }
    sub_000585E0_gen();
    if (!on) return;
    busy1 = *(const uint32_t *)(m + self + 0x238u);
    if (!busy0 && busy1 && list >= 0x10000u && list + (idx + 1) * 0x54u < lim) {
        const uint32_t *e = (const uint32_t *)(m + list + idx * 0x54u);
        char line[640]; int len = 0; uint32_t k;
        len += snprintf(line + len, sizeof line - len,
                        "[TRIGGER] t=%.2f flip %u list %08X entry #%u op %u (0x%X) arg %u (0x%X) fired; conds",
                        jsrf_now(), nv2a_gl_flips(), list, idx, e[4], e[4], e[5], e[5]);
        for (k = 0; k < e[1] && k < 12 && e[0] >= 0x10000u && e[0] + (k + 1) * 4u < lim; k++) {
            uint32_t c = *(const uint32_t *)(m + e[0] + k * 4u);
            len += snprintf(line + len, sizeof line - len, " [b%u f%u =%u]",
                            c & 7u, (c >> 3) & 0xFFFFu, (c >> 19) & 1u);
        }
        len += snprintf(line + len, sizeof line - len, "; sets");
        for (k = 0; k < e[3] && k < 12 && e[2] >= 0x10000u && e[2] + (k + 1) * 4u < lim; k++) {
            uint32_t c = *(const uint32_t *)(m + e[2] + k * 4u);
            len += snprintf(line + len, sizeof line - len, " [b%u f%u =%u]",
                            c & 7u, (c >> 3) & 0xFFFFu, (c >> 19) & 1u);
        }
        fprintf(stderr, "%s\n", line);
        fflush(stderr);
    }
}


/* ---- the player's talk trigger ----------------------------------------------
 *
 * sub_00080BD0 is a character's per-frame update (this = the character; its
 * position is the vec3 at +0xCA4). When +0x308 is set it looks up the talk
 * point +0x2F8 in the event-scene registry (0x20C750) and opens the talk --
 * the "get close to her and pull the Right Trigger" path. JSRF_TALK_LOG=1
 * logs every change of those fields, per character, with the position. */
extern void sub_00080BD0_gen(void);
void sub_00080BD0(void)
{
    static struct { uint32_t obj, f308, f2f8, f2dc, f2e0; } seen[16];
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t self = g_ecx;
    sub_00080BD0_gen();
    if (evt_on()) jsrf_evt_char(self);
    if (ev_log_on() && self >= 0x10000u && self < (64u << 20)) {
        uint32_t f308 = *(const uint32_t *)(m + self + 0x308u);
        uint32_t f2f8 = *(const uint32_t *)(m + self + 0x2F8u);
        uint32_t f2dc = *(const uint32_t *)(m + self + 0x2DCu);
        uint32_t f2e0 = *(const uint32_t *)(m + self + 0x2E0u);
        const float *pos = (const float *)(m + self + 0xCA4u);
        int i, slot = -1;
        for (i = 0; i < 16; i++) {
            if (seen[i].obj == self) { slot = i; break; }
            if (!seen[i].obj) { seen[i].obj = self; seen[i].f308 = ~f308; slot = i; break; }
        }
        if (slot >= 0 && (seen[slot].f308 != f308 || seen[slot].f2f8 != f2f8
                          || seen[slot].f2dc != f2dc || seen[slot].f2e0 != f2e0)) {
            fprintf(stderr, "[TALKPT] t=%.2f flip %u char %08X at (%.1f %.1f %.1f): +308=%08X +2F8=%08X +2DC=%08X +2E0=%08X\n",
                    jsrf_now(), nv2a_gl_flips(), self, pos[0], pos[1], pos[2], f308, f2f8, f2dc, f2e0);
            fflush(stderr);
            seen[slot].f308 = f308; seen[slot].f2f8 = f2f8;
            seen[slot].f2dc = f2dc; seen[slot].f2e0 = f2e0;
        }
    }
}


/* ---- who opens a talk ------------------------------------------------------
 * sub_0006D9A0 constructs the talk display (class 0x1CC5D0; args: owner,
 * 0x1DDE, 0x10, talk id, camera shot, scene position, ...). sub_00052780 is
 * the stage script's talk op, which also moves the player to the scene. */
extern void sub_0006D9A0_gen(void);
void sub_0006D9A0(void)
{
    if (ev_log_on()) {
        fprintf(stderr, "[TALKOPEN] t=%.2f flip %u display %08X from %08X args %08X %08X %08X talk=%d shot=%d %08X %08X\n",
                jsrf_now(), nv2a_gl_flips(), g_ecx, GARG(0), GARG(1), GARG(2), GARG(3),
                (int32_t)GARG(4), (int32_t)GARG(5), GARG(6), GARG(7));
        fflush(stderr);
    }
    sub_0006D9A0_gen();
}
extern void sub_00052780_gen(void);
void sub_00052780(void)
{
    if (ev_log_on()) {
        fprintf(stderr, "[TALKOP] t=%.2f flip %u script talk op, this=%08X from %08X\n",
                jsrf_now(), nv2a_gl_flips(), g_ecx, GARG(0));
        fflush(stderr);
    }
    sub_00052780_gen();
}


/* ---- the stage's blocking-entry list ------------------------------------------
 * sub_00057DE0 (thiscall, the event object) scans header+0x18C (count at
 * +0x190, 0x54-byte entries): the first entry whose flag conditions hold and
 * whose op is 0xE2..0xF9 is returned and becomes a mode (0xE7 = open a talk).
 * Only op 0xF6 also asks the player (message 0x1AF). JSRF_TALK_LOG=1 dumps
 * the list and the trigger list (header+0x17C / +0x180) once, and logs every
 * entry this returns. Where it returns none, a rival's preview may end
 * here at a second press of A, with Skip cutscenes on (preview_skip,
 * below). */
static void jsrf_dump_entries(const char *what, uint32_t ents, uint32_t n)
{
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t i, k;
    fprintf(stderr, "[LIST] %s: %u entries at %08X\n", what, n, ents);
    for (i = 0; i < n && i < 256 && ents >= 0x10000u && ents + (i + 1) * 0x54u < lim; i++) {
        const uint32_t *e = (const uint32_t *)(m + ents + i * 0x54u);
        char line[768]; int len = 0;
        len += snprintf(line + len, sizeof line - len, "[LIST]   %s #%-3u op 0x%02X args %08X %08X %08X %08X %08X %08X; conds",
                        what, i, e[4], e[5], e[6], e[7], e[8], e[9], e[10]);
        for (k = 0; k < e[1] && k < 12 && e[0] >= 0x10000u && e[0] + (k + 1) * 4u < lim; k++) {
            uint32_t c = *(const uint32_t *)(m + e[0] + k * 4u);
            len += snprintf(line + len, sizeof line - len, " [b%u f%u =%u]", c & 7u, (c >> 3) & 0xFFFFu, (c >> 19) & 1u);
        }
        len += snprintf(line + len, sizeof line - len, "; sets");
        for (k = 0; k < e[3] && k < 12 && e[2] >= 0x10000u && e[2] + (k + 1) * 4u < lim; k++) {
            uint32_t c = *(const uint32_t *)(m + e[2] + k * 4u);
            len += snprintf(line + len, sizeof line - len, " [b%u f%u =%u]", c & 7u, (c >> 3) & 0xFFFFu, (c >> 19) & 1u);
        }
        fprintf(stderr, "%s\n", line);
    }
    fflush(stderr);
}
extern void sub_00057DE0_gen(void);
static void preview_skip(uint32_t runner);  /* Skip cutscenes: a rival's preview */
void sub_00057DE0(void)
{
    static uint32_t dumped;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t self = g_ecx, hdr = 0;
    int on = ev_log_on() && self >= 0x10000u && self < lim;
    if (on) {
        hdr = *(const uint32_t *)(m + self + 0x1040u);
        if (hdr >= 0x10000u && hdr < lim && hdr != dumped) {
            dumped = hdr;
            jsrf_dump_entries("blocking", *(const uint32_t *)(m + hdr + 0x18Cu), *(const uint32_t *)(m + hdr + 0x190u));
            jsrf_dump_entries("trigger", *(const uint32_t *)(m + hdr + 0x17Cu), *(const uint32_t *)(m + hdr + 0x180u));
        }
    }
    sub_00057DE0_gen();
    preview_skip(self);
    if (on && (int32_t)g_eax >= 0) {
        uint32_t ents = *(const uint32_t *)(m + hdr + 0x18Cu);
        uint32_t cur = *(const uint32_t *)(m + self + 0xFB8u);
        fprintf(stderr, "[BLOCK] t=%.2f flip %u returned op 0x%02X, entry #%d\n",
                jsrf_now(), nv2a_gl_flips(), g_eax,
                (cur >= ents && ents) ? (int)((cur - ents) / 0x54u) : -1);
        fflush(stderr);
    }
}


/* ---- the zone test ----------------------------------------------------------
 * sub_0004A6F0(point, zone) -> 1 if the point is on the inner side of all six
 * planes of the zone (6 plane points at zone+0x00, 6 normals at zone+0x48):
 * dot(plane_point - point, normal) must not be below 0 (a NaN counts as
 * inside, on the console too). The event-scene registry uses it to decide
 * that a character has walked into a talk zone. JSRF_TALK_LOG=1 logs the
 * first 24 "inside" answers and a few "outside" ones, with the numbers. */
extern void sub_0004A6F0_gen(void);
void sub_0004A6F0(void)
{
    static unsigned n_in, n_out;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t pt = GARG(1), zone = GARG(2), from = GARG(0);
    sub_0004A6F0_gen();
    /* Only the event-scene registry's calls (sub_0006C6E0), and only once the
     * stage is running: the title's own zone checks would use up the budget. */
    if (ev_log_on() && from >= 0x0006C6E0u && from < 0x0006C840u && jsrf_now() > 60.0
        && pt >= 0x10000u && pt < lim && zone >= 0x10000u && zone + 0x90u < lim
        && ((g_eax && n_in < 24) || (!g_eax && n_out < 12))) {
        const float *p = (const float *)(m + pt), *z = (const float *)(m + zone);
        char line[640]; int len = 0, i;
        if (g_eax) n_in++; else n_out++;
        len += snprintf(line + len, sizeof line - len,
                        "[ZONE] t=%.2f %s from %08X point %08X (%.1f %.1f %.1f) zone %08X dots",
                        jsrf_now(), g_eax ? "INSIDE" : "outside", from, pt, p[0], p[1], p[2], zone);
        for (i = 0; i < 6; i++) {
            const float *q = z + 3 * i, *nv = z + 18 + 3 * i;
            float d = (q[0] - p[0]) * nv[0] + (q[1] - p[1]) * nv[1] + (q[2] - p[2]) * nv[2];
            len += snprintf(line + len, sizeof line - len, " %.1f", d);
        }
        len += snprintf(line + len, sizeof line - len, "  p0 (%.1f %.1f %.1f) n0 (%.2f %.2f %.2f)",
                        z[0], z[1], z[2], z[18], z[19], z[20]);
        {   /* which registration, which probe */
            uint32_t reg = (from == 0x0006C769u || from == 0x0006C7E2u) ? zone - 0xE0u : zone - 0x50u;
            const uint32_t *r = (const uint32_t *)(m + reg);
            const uint32_t *pr = (const uint32_t *)(m + pt);
            len += snprintf(line + len, sizeof line - len,
                            "  reg %08X id=%u task=%u param=%u type=%u shot=%d | probe idx=%d +24=%u +28=%u +2C=%u +30=%u",
                            reg, r[0x178 / 4], r[0x17C / 4], r[0x184 / 4], r[8 / 4], (int32_t)r[0xC / 4],
                            (int32_t)pr[0xC / 4], pr[0x24 / 4], pr[0x28 / 4], pr[0x2C / 4], pr[0x30 / 4]);
        }
        fprintf(stderr, "%s\n", line);
        fflush(stderr);
    }
}


/* ---- "has character <a> walked into the zone of scene <b>?" --------------------
 * sub_0006C680(a, b), thiscall on the event-scene registry (0x20C750): true
 * if a registration with +0x17C == b has +0x180 == a + 1 (the registry's
 * per-frame update, sub_0006C6E0, sets +0x180 to the index + 1 of a probe it
 * finds inside the scene's zone). Trigger op 0x04 uses it: Gum's lesson
 * waits on (0, 1) -- the player inside scene 1's zone. JSRF_TALK_LOG=1 logs
 * every true answer and the first false ones, with every registration. */
extern void sub_0006C680_gen(void);
void sub_0006C680(void)
{
    static unsigned n_false;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t self = g_ecx, a = GARG(1), b = GARG(2), from = GARG(0);
    sub_0006C680_gen();
    if (ev_log_on() && self >= 0x10000u && self < lim && (g_eax || n_false < 4)) {
        uint32_t reg = *(const uint32_t *)(m + self + 4u);
        int k = 0;
        if (!g_eax) n_false++;
        fprintf(stderr, "[INZONE] t=%.2f flip %u (%u, %u) -> %u from %08X; regs:", jsrf_now(),
                nv2a_gl_flips(), a, b, g_eax, from);
        while (reg >= 0x10000u && reg + 0x190u < lim && k++ < 16) {
            const uint32_t *r = (const uint32_t *)(m + reg);
            fprintf(stderr, " [%08X id=%u task=%u in=%u]", reg, r[0x178 / 4], r[0x17C / 4], r[0x180 / 4]);
            reg = r[0x18C / 4];
        }
        fprintf(stderr, "\n");
        fflush(stderr);
    }
}


/* ---- graffiti spots ---------------------------------------------------------
 * sub_0003FEC0 is a graffiti spot's per-frame update (ecx = the spot; a
 * switch on +0x60). JSRF_SPOT_LOG=1: each spot the first time it updates --
 * its id (+0x58), state, the player's position then, and the floats of its
 * first 0xC0 bytes, to find where spots are -- and every change of state. */
/* A spot's descriptor: the graffiti manager at 0x2314B0 keeps them in a hash
 * table at +8 (bucket id & 0xFF, chained through +0x104, matched on +0x9C).
 * Its +0xA4 holds 3 bits per part (+0x98 parts): the design sprayed there, 7
 * for none; +0xA8 the same for the design being replaced; +0xA0 is set while
 * some part is still 7. sub_0003F9E0 draws the spot's graffiti mesh (+0x90)
 * from these, the arrows while +0xA0 is set. */
static uint32_t spot_desc(const uint8_t *m, uint32_t id)
{
    uint32_t e = *(const uint32_t *)(m + 0x2314B8u + (id & 0xFFu) * 4u);
    int guard = 0;
    while (e >= 0x10000u && e + 0x110u < (64u << 20) && guard++ < 256) {
        if (*(const uint32_t *)(m + e + 0x9C) == id) return e;
        e = *(const uint32_t *)(m + e + 0x104);
    }
    return 0;
}
extern void sub_0003FEC0_gen(void);
void sub_0003FEC0(void)
{
    static int on = -1, nseen;
    static uint32_t seen[128], st[128], d4c[128], d50[128];
    static uint32_t da0[128], da4[128], da8[128], de4[128];
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t spot = g_ecx;
    int k = -1, i;
    if (on < 0) on = getenv("JSRF_SPOT_LOG") != NULL;
    if (on && spot >= 0x10000u && spot + 0x100u < (64u << 20)) {
        const uint32_t *w = (const uint32_t *)(m + spot);
        for (i = 0; i < nseen; i++) if (seen[i] == spot) { k = i; break; }
        if (k < 0 && nseen < 128) {
            k = nseen++; seen[k] = spot; st[k] = w[0x60 / 4];
            d4c[k] = w[0x4C / 4]; d50[k] = w[0x50 / 4];
            fprintf(stderr, "[SPOT] t=%.1f new %08X id %08X state %u player (%.1f %.1f %.1f) |",
                    pad_now(), spot, w[0x58 / 4], w[0x60 / 4],
                    s_player_pos[0], s_player_pos[1], s_player_pos[2]);
            for (i = 0; i < 0xC0 / 4; i++) {
                float f; memcpy(&f, &w[i], 4);
                if (w[i] && f == f && fabsf(f) > 1e-3f && fabsf(f) < 1e5f)
                    fprintf(stderr, " %02X=%.2f", i * 4, f);
                else if (w[i])
                    fprintf(stderr, " %02X:%08X", i * 4, w[i]);
            }
            fprintf(stderr, "\n");
            /* +0x54: its 0xB8-byte record (the stride the update walks). */
            { uint32_t rec = w[0x54 / 4];
              if (rec >= 0x10000u && rec + 0xB8u < (64u << 20)) {
                  const uint32_t *r = (const uint32_t *)(m + rec);
                  fprintf(stderr, "[SPOT]   record %08X:", rec);
                  for (i = 0; i < 0xB8 / 4; i++) {
                      float f; memcpy(&f, &r[i], 4);
                      if (r[i] && f == f && fabsf(f) > 1e-3f && fabsf(f) < 1e5f)
                          fprintf(stderr, " %02X=%.2f", i * 4, f);
                      else if (r[i])
                          fprintf(stderr, " %02X:%08X", i * 4, r[i]);
                  }
                  fprintf(stderr, "\n");
              } }
        }
    }
    sub_0003FEC0_gen();
    if (on && k >= 0) {
        const uint32_t *w = (const uint32_t *)(m + spot);
        if (w[0x60 / 4] != st[k] || w[0x4C / 4] != d4c[k] || w[0x50 / 4] != d50[k]) {
            fprintf(stderr, "[SPOT] t=%.1f %08X id %08X state %u -> %u, +4C %08X -> %08X, "
                    "+50 %08X -> %08X, player (%.1f %.1f %.1f)\n",
                    pad_now(), spot, w[0x58 / 4], st[k], w[0x60 / 4], d4c[k], w[0x4C / 4],
                    d50[k], w[0x50 / 4],
                    s_player_pos[0], s_player_pos[1], s_player_pos[2]);
            st[k] = w[0x60 / 4]; d4c[k] = w[0x4C / 4]; d50[k] = w[0x50 / 4];
        }
        { uint32_t de = spot_desc(m, w[0x58 / 4]);
          if (de) {
              const uint32_t *q = (const uint32_t *)(m + de);
              if (q[0xA0 / 4] != da0[k] || q[0xA4 / 4] != da4[k] || q[0xA8 / 4] != da8[k] ||
                  q[0xE4 / 4] != de4[k]) {
                  uint32_t np = q[0x98 / 4], pa = q[0x94 / 4], i2;
                  fprintf(stderr, "[SPOT] t=%.1f desc %08X id %08X parts %u: A0 %u A4 %08X A8 %08X "
                          "B8 %08X BC %08X E4 %u FC %08X | spot +8C %08X +90 %08X | timers",
                          pad_now(), de, w[0x58 / 4], np, q[0xA0 / 4], q[0xA4 / 4], q[0xA8 / 4],
                          q[0xB8 / 4], q[0xBC / 4], q[0xE4 / 4], q[0xFC / 4],
                          w[0x8C / 4], w[0x90 / 4]);
                  for (i2 = 0; i2 < np && i2 < 10 && pa >= 0x10000u && pa + i2 * 0xB8u + 0xB8u < (64u << 20); i2++) {
                      const float *pf = (const float *)(m + pa + i2 * 0xB8u);
                      fprintf(stderr, " %.2f@(%.0f %.0f %.0f)", pf[0x90 / 4], pf[0xA0 / 4], pf[0xA4 / 4], pf[0xA8 / 4]);
                  }
                  fprintf(stderr, "\n");
                  da0[k] = q[0xA0 / 4]; da4[k] = q[0xA4 / 4]; da8[k] = q[0xA8 / 4]; de4[k] = q[0xE4 / 4];
              }
              /* The player's spray state against this spot (sub_0008D280
               * commits a part when the player is inside its six planes --
               * sub_0004A6F0, dot(plane point - P, normal) >= 0 for all six --
               * and the part does not already hold the player's design +0xC9C). */
              if (s_player_va) {
                  const uint32_t *pl = (const uint32_t *)(m + s_player_va);
                  const float *P = (const float *)(m + s_player_va + 0xCA4);
                  uint32_t np = q[0x98 / 4], pa = q[0x94 / 4], i2, mask = 0;
                  static uint32_t lastkey[128]; static double lastt[128];
                  float best = 1e9f;
                  for (i2 = 0; i2 < np && i2 < 16 && pa >= 0x10000u && pa + (i2 + 1) * 0xB8u < (64u << 20); i2++) {
                      const float *b = (const float *)(m + pa + i2 * 0xB8u);
                      float worst = 1e9f; int j;
                      for (j = 0; j < 6; j++) {
                          float dx = b[j * 3] - P[0], dy = b[j * 3 + 1] - P[1], dz = b[j * 3 + 2] - P[2];
                          float dd = dx * b[18 + j * 3] + dy * b[18 + j * 3 + 1] + dz * b[18 + j * 3 + 2];
                          if (dd < worst) worst = dd;
                      }
                      if (worst >= 0.0f) mask |= 1u << i2;
                      if (-worst < best) best = -worst;
                  }
                  if (pl[0x2C0 / 4] == w[0x58 / 4] || mask) {
                      uint32_t key = mask ^ (pl[0x438 / 4] << 20) ^ (pl[0x2C0 / 4] << 8) ^ (q[0xA4 / 4] * 31u);
                      double now = pad_now();
                      if (key != lastkey[k] || now - lastt[k] > 2.0) {
                          lastkey[k] = key; lastt[k] = now;
                          fprintf(stderr, "[SPRAY] t=%.2f spot %08X: player at (%.1f %.1f %.1f) +2C0 %08X +438 %u +44C %u "
                                  "+C9C %u +CA0 %u +450 %08X +468 %08X | inside mask %X (nearest miss %.1f) A4 %08X B4 %08X C4 %u\n",
                                  now, w[0x58 / 4], P[0], P[1], P[2], pl[0x2C0 / 4], pl[0x438 / 4], pl[0x44C / 4],
                                  pl[0xC9C / 4], pl[0xCA0 / 4], pl[0x450 / 4], pl[0x468 / 4], mask, best,
                                  q[0xA4 / 4], q[0xB4 / 4], q[0xC4 / 4]);
                      }
                  }
              }
          } }
    }
}


/* ---- widescreen -------------------------------------------------------------
 *
 * The title draws 4:3 and never asks the console whether the TV is 16:9 (its
 * one XGetVideoFlags call, at 0x18AEC6, is about PAL-60). Its camera is the
 * graphics device class at vtable 0x1E0F00:
 *
 *   +0x7C SetCamera(angle, near, far), sub_00153FE0: +0x48 the horizontal
 *         field of view (0x10000 = 360 degrees), +0x54 tan(hfov/2), +0x50
 *         the projection distance in pixels; then
 *   sub_00153A90, the culling frustum: per side plane a slope (tan, at
 *         0x264E84 = -tan and 0x264E8C = +tan) and a sphere-radius factor
 *         (sec = sqrt(1 + tan^2), 0x264E80 / 0x264E88) -- the vertical
 *         planes at 0x264E78.. are separate;
 *   sub_00153EC0, the projection: the static matrix at 0x22E6B8, whose
 *         m00 is a constant 1.0 and whose field of view rides in w (m23 =
 *         tan), copied to 0x264EF8 for the shaders and passed to
 *         D3DDevice_SetTransform(D3DTS_PROJECTION = 1 on Xbox).
 *   +0x6C SetTransform(state, matrix) (sub_00154420): any other matrix the
 *         title sets explicitly.
 *
 * Widescreen (nv2a_widescreen(): RECOMP_WIDESCREEN, or Display > Aspect
 * ratio) keeps the vertical field of view and widens the horizontal one to
 * the shape chosen there (nv2a_wide_aspect): m00 = s = (4/3) / that shape
 * (0.75 at 16:9) for every perspective projection, and the side planes'
 * slope / s so the title culls against what is now on screen. The 640x480
 * frame then holds the wide view squeezed, and the window shows it at its
 * shape; the renderer squeezes the 2D overlays (nv2a_gl.c) so they keep
 * their shape. JSRF_WIDE_LOG=1 logs the projections the title sets. */
static int wide_log(void)
{
    static int on = -1;
    if (on < 0) on = getenv("JSRF_WIDE_LOG") ? 1 : 0;
    return on;
}
/* JSRF_WIDE_NOPROJ=1 (test only): leave the 3D projection and culling 4:3,
 * so a widescreen run differs from a 4:3 one only by the 2D squeeze. */
static int wide_proj(void)
{
    static int on = -1;
    if (on < 0) on = getenv("JSRF_WIDE_NOPROJ") ? 0 : 1;
    return on && nv2a_wide_scene();
}

extern void sub_00153EC0_gen(void);
void sub_00153EC0(void)
{
    float *m00 = (float *)((uint8_t *)g_xbox_mem_offset + 0x22E6B8u);
    if (wide_log()) {
        static uint32_t said;
        if (g_ecx != said && g_ecx >= 0x10000u && g_ecx < (64u << 20)) {
            said = g_ecx;
            fprintf(stderr, "[WIDE] t=%.1f camera device %08X, vtable %08X\n", pad_now(), g_ecx,
                    *(uint32_t *)((uint8_t *)g_xbox_mem_offset + g_ecx));
        }
    }
    s_cam_dev = g_ecx;
    s_wide_applied = nv2a_wide_scene();
    s_wide_squeeze_applied = nv2a_wide_squeeze();
    float want = wide_proj() ? nv2a_wide_squeeze() : 1.0f;
    /* Only over what was there: the title's 1.0, or what this last put. */
    static float put = 0.75f;
    if ((*m00 == 1.0f || *m00 == put) && *m00 != want) {
        if (wide_log())
            fprintf(stderr, "[WIDE] t=%.1f camera projection m00 %.2f -> %.2f\n",
                    pad_now(), *m00, want);
        *m00 = want;
    }
    if (want != 1.0f) put = want;
    sub_00153EC0_gen();
}

extern void sub_00153A90_gen(void);
void sub_00153A90(void)
{
    sub_00153A90_gen();
    if (wide_proj()) {
        float *f = (float *)((uint8_t *)g_xbox_mem_offset + 0x264E78u);
        /* f[2] = sec at 0x264E80, f[3] = -tan, f[4] = sec, f[5] = +tan */
        float t = f[5] * (1.0f / nv2a_wide_squeeze());   /* x 4/3 at 16:9 */
        float sec = sqrtf(1.0f + t * t);
        f[2] = sec; f[3] = -t; f[4] = sec; f[5] = t;
        { static float said;
          if (wide_log() && t != said) {
              said = t;
              fprintf(stderr, "[WIDE] t=%.1f culling: side slope %.4f (camera tan %.4f), radius factor %.4f\n",
                      pad_now(), t, t * nv2a_wide_squeeze(), sec);
          } }
    }
}

extern void sub_00154420_gen(void);
void sub_00154420(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t self = *(uint32_t *)(m + g_esp + 4);
    uint32_t state = *(uint32_t *)(m + g_esp + 8);
    uint32_t mat = *(uint32_t *)(m + g_esp + 12);
    float *mf = NULL, keep[4];
    int i;
    if (state == 1 && wide_proj()) {
        if (mat == 0xFFFFFFFFu && self >= 0x10000u && self + 0x68u < (64u << 20)) {
            uint32_t top = *(uint32_t *)(m + self + 0x64);
            if (top >= 0x10000u && top + 4u < (64u << 20))
                mat = *(uint32_t *)(m + top);
        }
        if (mat >= 0x10000u && mat != 0xFFFFFFFFu && mat + 64u < (64u << 20)) {
            float *p = (float *)(m + mat);
            /* Perspective: w comes from z (m23) and not from the constant
             * (m33). An orthographic projection is a 2D overlay -- left to
             * the renderer. */
            if (p[11] != 0.0f && p[15] == 0.0f) {
                mf = p;
                for (i = 0; i < 4; i++) { keep[i] = p[i * 4]; p[i * 4] *= nv2a_wide_squeeze(); }
            }
            if (wide_log()) {
                static int n;
                if (n++ < 40)
                    fprintf(stderr, "[WIDE] t=%.1f SetTransform(PROJECTION) %08X: %s m00 %.3f m11 %.3f "
                            "m22 %.3f m23 %.3f m32 %.3f m33 %.3f\n", pad_now(), mat,
                            mf ? "perspective, widened" : "not perspective, left",
                            mf ? keep[0] : p[0], p[5], p[10], p[11], p[14], p[15]);
            }
        }
    }
    sub_00154420_gen();
    if (mf)
        for (i = 0; i < 4; i++) mf[i * 4] = keep[i];
}

/* ---- the boost's heat haze, widened with the camera (known bug 2) --------
 * While a jet boost lasts, the world is drawn into a copy, and
 * sub_000A2960 (the camera class's draw, vtable 0x1CD518 slot +0xC) lays a
 * haze over the frame that samples it: a quad of 4 vertices at 0x215458
 * (stride 0x1C, x y z first, 6 indices at 0x215528). It and the camera's
 * constructor, sub_000A4D80, place the quad under a camera of their own
 * (SetCamera 0x2AAA, 1.0, 20.0) by taking the screen's four corners back
 * into view space at depth 2.45 with the device's unproject (slot +0x94,
 * sub_00154180), which knows nothing of the widening above. So through m00
 * = s the quad covers only the middle 4:3, and the sides show the black the
 * world wasn't drawn on. The functions are right on the console, and the
 * lifted code does what they do (the same constants, the same unproject);
 * only our widening makes them wrong, so the recompiler, the runtime and the
 * renderer aren't the cause. No other full-screen 3D effect is placed this
 * way: SetCamera(0x2AAA, 1.0, 20.0) and 0x215458 are used by these two
 * functions only. So once either has written the corners, their x is
 * divided by s, and set back when s changes or widescreen goes off.
 * The game writes them on a boost's first frame, whose haze it doesn't draw,
 * and builds the mesh from them on every frame after, so its next frame
 * draws the wider quad. The texture coordinates span the copy, which holds
 * the whole widened view, so they stay. Every placing it changes is logged,
 * "[WIDE] ... boost haze". tests/boost_haze checks the corners, cut from
 * here by text (from HAZE_VERTS to the graffiti section's heading); that
 * the game places them, and that the haze then fills the screen, needs the
 * game and the whole renderer, which is the test build's boost step. */
#define HAZE_VERTS  0x215458u
#define HAZE_STRIDE 0x1Cu
static void wide_haze_corners(const char *who)
{
    static float base[4], put[4];
    static int have;
    float *x[4], s = wide_proj() ? nv2a_wide_squeeze() : 1.0f, was[4];
    int i, moved = 0;
    if (!(s > 0.0f)) s = 1.0f;
    for (i = 0; i < 4; i++) {
        x[i] = (float *)((uint8_t *)g_xbox_mem_offset + HAZE_VERTS + (uint32_t)i * HAZE_STRIDE);
        if (!have || *x[i] != put[i]) base[i] = *x[i];   /* the game placed it again */
        was[i] = *x[i];
        put[i] = base[i] / s;
        if (*x[i] != put[i]) { *x[i] = put[i]; moved = 1; }
    }
    have = 1;
    if (moved)
        fprintf(stderr, "[WIDE] t=%.1f flip %u boost haze (%s): corners x %.4f %.4f %.4f %.4f"
                " -> %.4f %.4f %.4f %.4f (squeeze %.3f)\n", pad_now(), nv2a_gl_flips(), who,
                was[0], was[1], was[2], was[3], put[0], put[1], put[2], put[3], s);
}


/* ---- a graffiti mesh's draw ---------------------------------------------------
 * sub_00042EE0 (ecx = the spot's mesh, spot +0x90) draws a sprayed spot: the
 * mesh's texture (+8: count, id) on stages 0-3, c0 = (256, 1, 0, 0.5), and per
 * part pair c12+ the fades (+0xC/+0x10 per part, 8 apart) and c22+ the design
 * offsets (+0x64/+0x68), then [0x1F90C4]->+0x58(dev, [0x1F90C8], +0, +4).
 * JSRF_GRF_LOG=1 prints what it drew with, at most once a second per mesh.
 *
 * Its one caller is the spot's own draw (sub_0003F9E0, the call at
 * 0x0003FB7D), with the spot still in esi, which has just written the
 * mesh's layers from the spot's record. With random graffiti on, the mesh's
 * texture id and its layers' words are, for this draw, the ones the spot's
 * design takes (random_graffiti_spot), and the game's own again after it:
 * the game's code looks the id up and binds what it finds, as it does its
 * own. Where the spot has another tagger's layer too, the game's function
 * is run twice, once for each sheet, the first with the depth test the
 * draw gives (random_graffiti.h, RG_Z_LESS), set on the game's device as
 * the game sets it and put back after. The game's function sets no depth
 * test of its own (none of its calls is the device's slot 0x134), so the
 * spot's draw's own holds for the second. */
#define GRF_SPOT_DRAW_RA  0x0003FB82u  /* the spot's draw's call's return */
#define GRF_SPOT_ID       0x58u        /* the spot's id */
#define GRF_SPOT_MESH     0x90u        /* the spot's mesh */
#define GRF_MESH_TEXTURES 0x08u        /* the mesh's textures: a count, then the ids */
#define GRF_DEVICE        0x00251D6Cu  /* -> the game's device (vtable 0x1E0F00), as the prompts' GAME_DEVICE */
#define GRF_DEV_Z_FUNC    0x134u       /* its slot (device, D3D's ZFUNC), as DEV_Z_FUNC */
#define GRF_RS_ZFUNC      0x0019E1C4u  /* D3D's ZFUNC as the device has it now, as RS_ZFUNC */
static int prompt_device(uint32_t dev, uint32_t slot, uint32_t ra, int nargs, const uint32_t *args);
/* The game's device's depth test set to func for a spot's draw: 0 when it
 * isn't the game's code to call. */
static int grf_depth_test(uint32_t func)
{
    const uint32_t dev = MEM32(GRF_DEVICE);
    uint32_t a[2];
    a[0] = dev; a[1] = func;
    return dev && prompt_device(dev, GRF_DEV_Z_FUNC, GRF_SPOT_DRAW_RA, 2, a);
}
extern void sub_00042EE0_gen(void);
void sub_00042EE0(void)
{
    static int on = -1;
    static uint32_t seen[32]; static double when[32]; static int nseen;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t mesh = g_ecx;
    static int skip = -1;
    if (on < 0) on = getenv("JSRF_GRF_LOG") != NULL;
    if (skip < 0) skip = getenv("JSRF_GRF_SKIP") != NULL;
    if (skip) { g_esp += 4; return; }   /* A/B: which NV2A draw is the graffiti
                                         * (the callee's `ret` pops the return address) */
    {
        RANDOM_GRAFFITI_DRAW draw;
        const uint32_t spot = g_esi;
        uint32_t tex = 0, id = 0, passes = 0, was[RG_PARTS][4], pass, b;
        if (random_graffiti_on() && GARG(0) == GRF_SPOT_DRAW_RA && spot >= 0x10000u
         && spot + 0xC0u < lim && mesh >= 0x10000u && mesh + 0xC0u < lim
         && MEM32(spot + GRF_SPOT_MESH) == mesh)
            tex = adx_word(mesh + GRF_MESH_TEXTURES);
        if (tex && adx_word(tex)) {
            id = adx_word(tex + 4);
            passes = random_graffiti_spot(m, MEM32(spot + GRF_SPOT_ID), id, mesh, pad_now(), &draw);
        }
        if (!passes) {
            sub_00042EE0_gen();
        } else {
            for (b = 0; b < draw.parts; b++) {
                was[b][0] = MEM32(mesh + RG_MESH_SHOWN + 8u * b);
                was[b][1] = MEM32(mesh + RG_MESH_SHOWN + 8u * b + 4u);
                was[b][2] = MEM32(mesh + RG_MESH_ROW + 8u * b);
                was[b][3] = MEM32(mesh + RG_MESH_ROW + 8u * b + 4u);
            }
            for (pass = 0; pass < passes; pass++) {
                const uint32_t zfunc = MEM32(GRF_RS_ZFUNC);
                int set = 0;
                for (b = 0; b < draw.parts; b++) {
                    MEM32(mesh + RG_MESH_SHOWN + 8u * b) = draw.layer[pass][b][0];
                    MEM32(mesh + RG_MESH_SHOWN + 8u * b + 4u) = draw.layer[pass][b][1];
                    MEM32(mesh + RG_MESH_ROW + 8u * b) = draw.layer[pass][b][2];
                    MEM32(mesh + RG_MESH_ROW + 8u * b + 4u) = draw.layer[pass][b][3];
                }
                MEM32(tex + 4) = draw.texture[pass];
                if (draw.depth_test[pass] && draw.depth_test[pass] != zfunc)
                    set = grf_depth_test(draw.depth_test[pass]);
                if (pass + 1u < passes)     /* a draw before the last: the game's function, called again */
                    guest_call_cdecl(sub_00042EE0_gen, GRF_SPOT_DRAW_RA, 0, NULL);
                else
                    sub_00042EE0_gen();
                if (set)                    /* the game's own test back */
                    grf_depth_test(zfunc);
            }
            for (b = 0; b < draw.parts; b++) {
                MEM32(mesh + RG_MESH_SHOWN + 8u * b) = was[b][0];
                MEM32(mesh + RG_MESH_SHOWN + 8u * b + 4u) = was[b][1];
                MEM32(mesh + RG_MESH_ROW + 8u * b) = was[b][2];
                MEM32(mesh + RG_MESH_ROW + 8u * b + 4u) = was[b][3];
            }
            MEM32(tex + 4) = id;
        }
    }
    if (!on || mesh < 0x10000u || mesh + 0xC0u >= lim) return;
    {
        const uint32_t *w = (const uint32_t *)(m + mesh);
        double now = pad_now();
        int k = -1, i;
        for (i = 0; i < nseen; i++) if (seen[i] == mesh) { k = i; break; }
        if (k < 0) { if (nseen >= 32) return; k = nseen++; seen[k] = mesh; when[k] = -9; }
        if (now - when[k] < 1.0) return;
        when[k] = now;
        fprintf(stderr, "[GRF] t=%.2f mesh %08X: +0 %08X +4 %08X +8 %08X", now, mesh, w[0], w[1], w[2]);
        if (w[2] >= 0x10000u && w[2] + 16u < lim) {
            const uint32_t *tl = (const uint32_t *)(m + w[2]);
            fprintf(stderr, " (texlist n=%u id %08X %08X)", tl[0], tl[1], tl[2]);
        }
        for (i = 0; i < 2; i++) {
            uint32_t o = w[i];
            if (o >= 0x10000u && o + 0x20u < lim) {
                const uint32_t *b = (const uint32_t *)(m + o);
                fprintf(stderr, " | +%d-> %08X %08X %08X %08X %08X", i * 4, b[0], b[1], b[2], b[3], b[4]);
            }
        }
        { uint32_t t = *(const uint32_t *)(m + 0x251D54u);
          /* A handle into the title's own table: its SetTexture (0x14FDE0)
           * passes [0x264F68][handle] to D3DDevice_SetTexture when the handle
           * is below [0x264F70]. */
          uint32_t tab = *(const uint32_t *)(m + 0x264F68u), cnt = *(const uint32_t *)(m + 0x264F70u);
          fprintf(stderr, " | stage0 handle %u (table %08X n %u)", t, tab, cnt);
          if (t < cnt && tab >= 0x10000u && tab + 4u * cnt < lim) {
              uint32_t tx = *(const uint32_t *)(m + tab + 4u * t);
              fprintf(stderr, " -> D3D tex %08X", tx);
              if (tx >= 0x10000u && tx + 0x20u < lim) {
                  const uint32_t *b = (const uint32_t *)(m + tx);
                  fprintf(stderr, " = common %08X data %08X lock %08X format %08X size %08X", b[0], b[1], b[2], b[3], b[4]);
              }
          } }
        { uint32_t r = *(const uint32_t *)(m + 0x251D7Cu);
          if (r >= 0x10000u && r + 0x580u < lim)
              fprintf(stderr, " | fx %08X/%08X", *(const uint32_t *)(m + r + 0x544), *(const uint32_t *)(m + r + 0x56C));
          fprintf(stderr, " dev %08X vb? %08X draws %u", *(const uint32_t *)(m + 0x1F90C4u),
                  *(const uint32_t *)(m + 0x1F90C8u), *(const uint32_t *)(m + 0x251D40u));
          { uint32_t d = *(const uint32_t *)(m + 0x251D6Cu), d2 = *(const uint32_t *)(m + 0x1F90C4u);
            if (d >= 0x10000u && d + 8u < lim) {
                uint32_t vt = *(const uint32_t *)(m + d);
                if (vt >= 0x10000u && vt + 0x150u < lim)
                    fprintf(stderr, " | d3d %08X vt %08X +144 %08X", d, vt, *(const uint32_t *)(m + vt + 0x144));
            }
            if (d2 >= 0x10000u && d2 + 8u < lim) {
                uint32_t vt = *(const uint32_t *)(m + d2);
                if (vt >= 0x10000u && vt + 0x60u < lim)
                    fprintf(stderr, " | drawer vt %08X +58 %08X", vt, *(const uint32_t *)(m + vt + 0x58));
            } } }
        fprintf(stderr, "\n[GRF]   parts (fadeA fadeB | offA offB):");
        for (i = 0; i < 10; i++) {
            float a, b2, c, d;
            memcpy(&a, m + mesh + 0x10 + i * 8, 4); memcpy(&b2, m + mesh + 0xC + i * 8, 4);
            memcpy(&c, m + mesh + 0x68 + i * 8, 4); memcpy(&d, m + mesh + 0x64 + i * 8, 4);
            fprintf(stderr, " %d:%.2f %.2f|%.2f %.2f", i, a, b2, c, d);
        }
        fprintf(stderr, "\n");
    }
}


/* ---- the HUD at the screen's edges (widescreen_hud.h) ------------------------
 * In widescreen the renderer squeezes the title's 2D into the middle 4:3 of
 * the screen, the HUD with it, since the game places the HUD for a 4:3
 * screen. Each HUD piece is drawn by one function, the slot +0xC of its
 * class, which the frame calls once for each child of the HUD manager after
 * the world is drawn; it fills its piece's own quad batch and draws it, and
 * its model if it has one, before it returns. These functions are right for
 * the console's screen, and the lifted code does what they do: nothing is
 * wrong in them or in the recompiler, the runtime or the renderer, only
 * widescreen gives the HUD a wider screen to be at the edges of. So these
 * overrides mark the start and the end of a piece's draws in D3D's push
 * buffer (widescreen_hud_begin and _end), and the renderer moves what is
 * drawn between the marks to the piece's side (nv2a_wide.h). Display > HUD
 * scale sizes what is drawn between them too, or draws none of it
 * (nv2a_hud_scale.h), so they are written whenever the scene is widened or
 * the HUD isn't at the game's own size (nv2a_hud_sized). The marks are
 * written as D3D writes a method (sub_0018EC80): at its device's put
 * pointer, after its make-space call sub_001916B0 when the pointer is at the
 * limit, which takes no arguments and returns the new put pointer. In 4:3,
 * and on the screens the game keeps 4:3, with the HUD at its own size,
 * nothing is marked. The font call's
 * override, after them, has a piece's text drawn with the piece rather than
 * later with all the text (widescreen_hud.h). tests/widescreen_hud checks
 * the pieces, the marks and how they are written, and the text's way; that
 * each piece then sits at the edge needs the game and the whole renderer,
 * which is the test build's HUD step. */
#include "widescreen_hud.h"
extern int nv2a_hud_sized(void);
#define D3D_MAKE_SPACE 0x001916B0u      /* D3D's make-space call (no arguments) */
extern void sub_001916B0(void);
static uint32_t d3d_make_space(void)
{
    return guest_call_cdecl(sub_001916B0, D3D_MAKE_SPACE, 0, NULL);
}
static int wide_hud_begin(int piece)
{
    return widescreen_hud_begin((uint8_t *)g_xbox_mem_offset, MEM32(D3D_DEVICE), piece,
                                nv2a_wide_scene() || nv2a_hud_sized(), d3d_make_space);
}
static void wide_hud_end(int piece, int marked)
{
    widescreen_hud_end((uint8_t *)g_xbox_mem_offset, MEM32(D3D_DEVICE), piece, marked,
                       d3d_make_space);
}
/* Each piece's function, as the HUD manager's walk calls it (its class's
 * slot +0xC, with this in ecx): its draws between its marks. The classes
 * are the HUD's own, made only by the HUD manager (sub_00108870 and
 * sub_001077D0). The HUD's own signs, such as the stop sign, are marked in
 * the middle, where their models put them and they stay, for HUD scale
 * alone (the street pause menu's area name, also a sign, goes with that
 * menu, below). */
/* sub_001060B0: the health bar and its border (class 0x1D1F98), at the top
 * right. */
extern void sub_001060B0_gen(void);
void sub_001060B0(void)
{
    int marked = wide_hud_begin(WIDE_HUD_HEALTH);
    sub_001060B0_gen();
    wide_hud_end(WIDE_HUD_HEALTH, marked);
}
/* sub_0010D480: the gauge and its numbers (class 0x1D2328; one argument,
 * the HUD walk's pass, ret 4), at the bottom right, in two draws. The
 * graffiti soul indicator draws after it, when it drew (soul_draw, below):
 * it draws on some of the walk's passes, as its own table and a flag say,
 * and D3D's put pointer moves only when it does, which keeps no copy of
 * those rules here. Compared for changed, not grown: D3D's make-space can
 * take the pointer back to its buffer's start. */
extern void sub_0010D480_gen(void);
static void soul_draw(uint32_t pass);
static void mark_draw(void);
static uint32_t d3d_put(void)
{
    uint32_t dev = MEM32(D3D_DEVICE);
    return dev ? MEM32(dev + WIDE_HUD_D3D_PUT) : 0;
}
void sub_0010D480(void)
{
    uint32_t pass = GARG(1), put;
    int marked = wide_hud_begin(WIDE_HUD_GAUGE), drew;
    put = d3d_put();
    sub_0010D480_gen();
    drew = d3d_put() != put;
    wide_hud_end(WIDE_HUD_GAUGE, marked);
    if (drew) {
        soul_draw(pass);
        mark_draw();
    }
}
/* ---- the graffiti soul indicator's badge (soul_indicator.h) -----------------
 * The game has no badge to show while a soul challenge is on track, so the
 * gauge's override, above, draws one after the gauge: nothing in the
 * gauge's function, the recompiler or the runtime is wrong, and this adds
 * to what the HUD draws, as Music info in pause adds to the pause menus.
 * soul_indicator_frame gives the quad (its place, size, fade and red); this
 * draws it the way the gauge draws its own 1-quad batch, with the game's
 * calls: its states (alpha test off, blending on, no texture on stage 1,
 * the badge's texture on stage 0, each set only when the game's cache of
 * it differs, and the cache written), one quad through a batch, at the
 * gauge's z, and the states put back. The badge is drawn white (spriteice,
 * 11:21 UTC on 2026-11-17): stage 0's colour is the dot product of the
 * texture's and the diffuse's, each taken as -1 to 1, with the diffuse
 * green alone, which gives twice the texture's green less one. The badge
 * is cyan, its red near nothing, so its green is its brightness: white
 * where it is cyan, its outlines and figure dark. Its alpha is the
 * texture's times the diffuse's, the fade. While it is red, the same quad
 * again through a second batch, with stage 0's colour the diffuse (red) and
 * its alpha the texture's times the diffuse's, so the badge blends to red,
 * the white showing through. Stage 0's six states are read from D3D's copy
 * before and put back after, so the game's cache of the mode it set them
 * with stays true. Its own two batches, made once from the game's factory
 * as the HUD's pieces make theirs, so the gauge's vertex buffer isn't
 * locked a second time in a frame, and the red's begin doesn't lock the
 * buffer the white's draw still uses. It is a HUD piece of its own (WIDE_HUD_SOUL), so
 * widescreen takes it to the left edge, Display > HUD scale sizes it, and
 * Disable HUD hides it. Once a pass of the game's loop (soul_indicator_once),
 * as the gauge can draw on more than one of the walk's passes. Every call is
 * through a vtable, stdcall with the object first, the callee popping; the
 * scratch for the quad, the six states and the factory's out is below the
 * stack the gauge left, as music_info_draw takes its. tests/soul_indicator
 * checks the quad and its vertices; what it looks like needs the game, the
 * test build's steps. */
#define HUD_DEVICE         0x00251D6Cu  /* -> the game's device (vtable 0x1E0F00), as DD_DEVICE */
#define HUD_DEV_BLEND      0x124u       /* (device, on): alpha blending */
#define HUD_DEV_TEXTURE    0x144u       /* (device, stage, texture), -1 for none */
#define HUD_DEV_STAGE_OPS  0x148u       /* (device, stage, -> the six states below) */
#define HUD_DEV_ALPHA_TEST 0x1C4u       /* (device, on) */
#define HUD_BLEND_SET      0x001FA1D0u  /* the blending last set through HUD_DEV_BLEND */
#define HUD_STAGE0_SET     0x00251D54u  /* the texture last set on stage 0 */
#define HUD_STAGE1_SET     0x00251D58u  /* and on stage 1 */
#define HUD_NO_TEXTURE     0xFFFFFFFFu
#define HUD_FACTORY        0x00251D74u  /* -> the quad batches' factory (vtable 0x1E13F0) */
#define HUD_FACTORY_MAKE   0x24u        /* (factory, quads, 0, -> the batch made): < 0 failed */
#define HUD_BATCH_BEGIN    0x0Cu        /* (batch): locks its vertices, < 0 when it can't */
#define HUD_BATCH_END      0x10u        /* (batch): unlocks them, and gives the count */
#define HUD_BATCH_ADD      0x14u        /* (batch, -> the vertices, mode) */
#define HUD_BATCH_ADD_QUAD 0u           /* the add's mode: a whole quad, four vertices */
#define HUD_BATCH_Z        0x20u        /* (batch, z, step): the z its adds take */
#define HUD_BATCH_DRAW     0x28u        /* (batch, count) */
#define HUD_TEXTURES       0x00252790u  /* the texture manager, a hash table */
#define HUD_TEXTURE_NONE   0xFFFFFFFFu  /* what its lookup gives for a hash it hasn't */
#define D3D_STAGE_STATES   0x0019DEE0u  /* D3D's copy of stage 0's states, a dword each */
#define D3D_COLOROP        0x30u
#define D3D_COLORARG1      0x38u
#define D3D_COLORARG2      0x3Cu
#define D3D_ALPHAOP        0x40u
#define D3D_ALPHAARG1      0x48u
#define D3D_ALPHAARG2      0x4Cu
#define D3D_TA_DIFFUSE     0u
#define D3D_TA_TEXTURE     2u
#define D3D_OP_SELECTARG2  3u
#define D3D_OP_MODULATE    4u
/* The Xbox's own numbering: its two bump map ops are at the end (25 and 26,
 * as SetTextureStageState at 0x18F2B3 and the game's stage modes at
 * 0x14FFDE take them), so the dot product is 22, where the PC's is 24. */
#define D3D_OP_DOTPRODUCT3 22u
#define SOUL_BADGE         0x58646C0Au  /* the badge's texture, in the Misc set */
#define SOUL_Z             0x3FB33333u  /* 1.4f, the gauge's 1-quad batch's z */
#define SOUL_SCRATCH       0x80u        /* the quad (SOUL_QUAD_BYTES), then six dwords */
/* The return addresses: each just after the gauge's own call that the
 * badge's stands for (sub_0010D480, and its constructor's for the texture
 * and the batch), and the main loop's stage 0 call (sub_00013A80) for the
 * stage's six, which the gauge doesn't set. */
#define SOUL_ALPHA_OFF_RET  0x0010D4D1u
#define SOUL_BLEND_ON_RET   0x0010D4F6u
#define SOUL_STAGE1_RET     0x0010D519u
#define SOUL_STAGE0_RET     0x0010D543u
#define SOUL_BEGIN_RET      0x0010D554u
#define SOUL_ADD_RET        0x0010D56Du
#define SOUL_END_RET        0x0010D579u
#define SOUL_Z_RET          0x0010D594u
#define SOUL_DRAW_RET       0x0010D5A1u
#define SOUL_BLEND_BACK_RET 0x0010D7B9u
#define SOUL_ALPHA_ON_RET   0x0010D7C9u
#define SOUL_LOOKUP_RET     0x0010D094u
#define SOUL_MAKE_RET       0x0010D056u
#define SOUL_STAGE_OPS_RET  0x00013DE2u
extern void sub_0006E130(void);   /* the texture manager's lookup: thiscall, (hash) */

/* A call through slot slot of args[0]'s vtable, stdcall with the object
 * first: g_eax after it, or 0 and *ok cleared when the slot isn't the game's
 * code (said once in the log). */
static uint32_t soul_vcall(uint32_t slot, uint32_t ra, int nargs, const uint32_t *args, int *ok)
{
    uint32_t va = adx_word(adx_word(args[0]) + slot);
    recomp_func_t fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    if (!fn) {
        static int said;
        if (!said++)
            fprintf(stderr, "[SOUL] %08X's slot +%X is %08X, not the game's code: no badge\n",
                    args[0], slot, va);
        *ok = 0;
        return 0;
    }
    return guest_call_cdecl(fn, ra, nargs, args);
}

/* The badge's two batches, made once from the game's factory, with the
 * scratch for its out: 0 when the factory gave none (said once). */
static int soul_batches(uint32_t *batch, uint32_t scratch, int *ok)
{
    int k;
    for (k = 0; k < 2 && *ok; k++) {
        uint32_t a[4] = { adx_word(HUD_FACTORY), 1, 0, scratch };
        int32_t hr;
        if (batch[k]) continue;
        MEM32(scratch) = 0;
        hr = (int32_t)soul_vcall(HUD_FACTORY_MAKE, SOUL_MAKE_RET, 4, a, ok);
        batch[k] = *ok && hr >= 0 ? adx_word(scratch) : 0;
        if (*ok && !batch[k]) {
            fprintf(stderr, "[SOUL] the factory at %08X gave no batch (%08X): no badge\n", a[0],
                    (uint32_t)hr);
            *ok = 0;
        }
    }
    return *ok;
}

/* One quad through batch: the gauge's z, then begin, the add, end and draw,
 * as the gauge draws its 1-quad batch (it sets its z after its draw, for its
 * next; set before, the first draw has it too). */
static void soul_batch(uint32_t batch, uint32_t quad, int *ok)
{
    uint32_t a[3] = { batch, SOUL_Z, 0 }, count;
    soul_vcall(HUD_BATCH_Z, SOUL_Z_RET, 3, a, ok);
    if (!*ok || (int32_t)soul_vcall(HUD_BATCH_BEGIN, SOUL_BEGIN_RET, 1, a, ok) < 0 || !*ok)
        return;
    a[1] = quad;
    a[2] = HUD_BATCH_ADD_QUAD;
    soul_vcall(HUD_BATCH_ADD, SOUL_ADD_RET, 3, a, ok);
    count = soul_vcall(HUD_BATCH_END, SOUL_END_RET, 1, a, ok);
    a[1] = count;
    if (*ok) soul_vcall(HUD_BATCH_DRAW, SOUL_DRAW_RET, 2, a, ok);
}

/* Stage 0's six states (D3D_COLORARG1 to D3D_ALPHAOP, in soul_draw's order)
 * through the device, from six dwords at at. */
static void soul_stage_ops(uint32_t dev, uint32_t at, int *ok)
{
    const uint32_t a[3] = { dev, 0, at };
    soul_vcall(HUD_DEV_STAGE_OPS, SOUL_STAGE_OPS_RET, 3, a, ok);
}

static void soul_draw(uint32_t pass)
{
    static const uint32_t state[6] = { D3D_COLORARG1, D3D_COLORARG2, D3D_COLOROP,
                                       D3D_ALPHAARG1, D3D_ALPHAARG2, D3D_ALPHAOP };
    static const uint32_t white[6] = { D3D_TA_TEXTURE, D3D_TA_DIFFUSE, D3D_OP_DOTPRODUCT3,
                                       D3D_TA_TEXTURE, D3D_TA_DIFFUSE, D3D_OP_MODULATE };
    static const uint32_t red[6] = { D3D_TA_TEXTURE, D3D_TA_DIFFUSE, D3D_OP_SELECTARG2,
                                     D3D_TA_TEXTURE, D3D_TA_DIFFUSE, D3D_OP_MODULATE };
    static uint32_t batch[2];   /* the white pass's, and the red's */
    static int given_up, said_texture;
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t esp = g_esp, scratch, ops, dev, tex, blend, sv_ecx = g_ecx, a[3], kept[6];
    int ok = 1, marked, k;
    SoulQuad q;
    if (given_up || !soul_indicator_once(s_loop_passes, pass)
     || !soul_indicator_frame(m, ADX_GUEST_RAM, jsrf_now(), &q))
        return;
    dev = adx_word(HUD_DEVICE);
    g_ecx = HUD_TEXTURES;
    a[0] = SOUL_BADGE;
    tex = guest_call_cdecl(sub_0006E130, SOUL_LOOKUP_RET, 1, a);
    g_ecx = sv_ecx;
    if (tex == HUD_TEXTURE_NONE || !dev) {
        if (!said_texture++)
            fprintf(stderr, "[SOUL] not drawn: the badge's texture %08X is %08X, the device "
                    "%08X\n", SOUL_BADGE, tex, dev);
        return;
    }
    g_esp -= SOUL_SCRATCH;
    scratch = g_esp;
    ops = scratch + SOUL_QUAD_BYTES;
    if (!soul_batches(batch, scratch, &ok)) {
        given_up = 1;
        g_esp = esp;
        return;
    }
    marked = wide_hud_begin(WIDE_HUD_SOUL);
    /* The gauge's states, each set only when the game's cache differs. */
    a[0] = dev;
    a[1] = 0;
    soul_vcall(HUD_DEV_ALPHA_TEST, SOUL_ALPHA_OFF_RET, 2, a, &ok);
    blend = MEM32(HUD_BLEND_SET);
    if (blend != 1) {
        MEM32(HUD_BLEND_SET) = 1;
        a[1] = 1;
        soul_vcall(HUD_DEV_BLEND, SOUL_BLEND_ON_RET, 2, a, &ok);
    }
    if (MEM32(HUD_STAGE1_SET) != HUD_NO_TEXTURE) {
        MEM32(HUD_STAGE1_SET) = HUD_NO_TEXTURE;
        a[1] = 1;
        a[2] = HUD_NO_TEXTURE;
        soul_vcall(HUD_DEV_TEXTURE, SOUL_STAGE1_RET, 3, a, &ok);
    }
    if (MEM32(HUD_STAGE0_SET) != tex) {
        MEM32(HUD_STAGE0_SET) = tex;
        a[1] = 0;
        a[2] = tex;
        soul_vcall(HUD_DEV_TEXTURE, SOUL_STAGE0_RET, 3, a, &ok);
    }
    /* The badge, white at its fade; then, while red shows, red over it;
     * then stage 0's states as they were. */
    for (k = 0; k < 6; k++) {
        kept[k] = MEM32(D3D_STAGE_STATES + state[k]);
        MEM32(ops + 4u * (uint32_t)k) = white[k];
    }
    soul_stage_ops(dev, ops, &ok);
    soul_indicator_vertices(m, scratch, &q, q.colour);
    if (ok) soul_batch(batch[0], scratch, &ok);
    if (ok && q.red > 0.0f) {
        for (k = 0; k < 6; k++) MEM32(ops + 4u * (uint32_t)k) = red[k];
        soul_stage_ops(dev, ops, &ok);
        soul_indicator_vertices(m, scratch, &q, q.red_colour);
        if (ok) soul_batch(batch[1], scratch, &ok);
    }
    for (k = 0; k < 6; k++) MEM32(ops + 4u * (uint32_t)k) = kept[k];
    soul_stage_ops(dev, ops, &ok);
    /* What it changed, put back as the gauge puts its back. */
    a[0] = dev;
    if (blend != 1) {
        MEM32(HUD_BLEND_SET) = blend;
        a[1] = blend;
        soul_vcall(HUD_DEV_BLEND, SOUL_BLEND_BACK_RET, 2, a, &ok);
    }
    a[1] = 1;
    soul_vcall(HUD_DEV_ALPHA_TEST, SOUL_ALPHA_ON_RET, 2, a, &ok);
    wide_hud_end(WIDE_HUD_SOUL, marked);
    g_esp = esp;
    if (!ok) given_up = 1;
}

/* ---- the mark that a recording plays (playback_mark.h) ---------------------
 * While a recording plays, a disc with a play sign at the middle of the
 * HUD's left edge, which turns green, red or blue as the play ends. Drawn
 * after the soul badge, once a pass of the game's loop, the same way as it
 * (soul_draw's calls, states and return addresses), but with no texture:
 * stage 0's colour and alpha are the diffuse's, which is the quads' colour
 * at the fade (playback_mark_vertices). Its batch, of PB_MARK_QUADS quads,
 * is made once from the game's factory; each quad goes in with an add of
 * its own. A HUD piece of its own (WIDE_HUD_MARK), so widescreen takes it to
 * the left edge, Display > HUD scale sizes it, and Disable HUD hides it. Its
 * time is pad_now's, as playback's end of a play. */
#define MARK_SCRATCH (PB_MARK_BYTES + 0x20u)    /* the quads, then six dwords */
static void mark_draw(void)
{
    static const uint32_t state[6] = { D3D_COLORARG1, D3D_COLORARG2, D3D_COLOROP,
                                       D3D_ALPHAARG1, D3D_ALPHAARG2, D3D_ALPHAOP };
    static const uint32_t plain[6] = { D3D_TA_TEXTURE, D3D_TA_DIFFUSE, D3D_OP_SELECTARG2,
                                       D3D_TA_TEXTURE, D3D_TA_DIFFUSE, D3D_OP_SELECTARG2 };
    static uint32_t batch;
    static unsigned drawn_in = ~0u;
    static int given_up;
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t esp = g_esp, scratch, ops, dev, blend, a[4], kept[6], count;
    int ok = 1, marked, k;
    PbMarkShape sh;
    if (given_up || drawn_in == s_loop_passes) return;
    if (!playback_mark(pad_now(), &sh)) return;
    drawn_in = s_loop_passes;
    dev = adx_word(HUD_DEVICE);
    if (!dev) return;
    g_esp -= MARK_SCRATCH;
    scratch = g_esp;
    ops = scratch + PB_MARK_BYTES;
    if (!batch) {
        int32_t hr;
        a[0] = adx_word(HUD_FACTORY);
        a[1] = PB_MARK_QUADS;
        a[2] = 0;
        a[3] = scratch;
        MEM32(scratch) = 0;
        hr = (int32_t)soul_vcall(HUD_FACTORY_MAKE, SOUL_MAKE_RET, 4, a, &ok);
        batch = ok && hr >= 0 ? adx_word(scratch) : 0;
        if (!batch) {
            fprintf(stderr, "[PLAYBACK] the mark: the factory at %08X gave no batch (%08X): no mark\n", a[0],
                    (uint32_t)hr);
            given_up = 1;
            g_esp = esp;
            return;
        }
    }
    marked = wide_hud_begin(WIDE_HUD_MARK);
    /* The gauge's states, as soul_draw sets them, and no texture on either
     * stage, each set only when the game's cache differs. */
    a[0] = dev;
    a[1] = 0;
    soul_vcall(HUD_DEV_ALPHA_TEST, SOUL_ALPHA_OFF_RET, 2, a, &ok);
    blend = MEM32(HUD_BLEND_SET);
    if (blend != 1) {
        MEM32(HUD_BLEND_SET) = 1;
        a[1] = 1;
        soul_vcall(HUD_DEV_BLEND, SOUL_BLEND_ON_RET, 2, a, &ok);
    }
    for (k = 1; k >= 0; k--) {
        uint32_t cache = k ? HUD_STAGE1_SET : HUD_STAGE0_SET;
        if (MEM32(cache) == HUD_NO_TEXTURE) continue;
        MEM32(cache) = HUD_NO_TEXTURE;
        a[1] = (uint32_t)k;
        a[2] = HUD_NO_TEXTURE;
        soul_vcall(HUD_DEV_TEXTURE, k ? SOUL_STAGE1_RET : SOUL_STAGE0_RET, 3, a, &ok);
    }
    for (k = 0; k < 6; k++) {
        kept[k] = MEM32(D3D_STAGE_STATES + state[k]);
        MEM32(ops + 4u * (uint32_t)k) = plain[k];
    }
    soul_stage_ops(dev, ops, &ok);
    playback_mark_vertices(m, scratch, &sh);
    /* The quads through the batch: its z, begin, an add each, end, draw. */
    a[0] = batch;
    a[1] = SOUL_Z;
    a[2] = 0;
    soul_vcall(HUD_BATCH_Z, SOUL_Z_RET, 3, a, &ok);
    if (ok && (int32_t)soul_vcall(HUD_BATCH_BEGIN, SOUL_BEGIN_RET, 1, a, &ok) >= 0 && ok) {
        for (k = 0; k < PB_MARK_QUADS && ok; k++) {
            a[1] = scratch + (uint32_t)k * (PB_MARK_BYTES / PB_MARK_QUADS);
            a[2] = HUD_BATCH_ADD_QUAD;
            soul_vcall(HUD_BATCH_ADD, SOUL_ADD_RET, 3, a, &ok);
        }
        count = soul_vcall(HUD_BATCH_END, SOUL_END_RET, 1, a, &ok);
        a[1] = count;
        if (ok) soul_vcall(HUD_BATCH_DRAW, SOUL_DRAW_RET, 2, a, &ok);
    }
    for (k = 0; k < 6; k++) MEM32(ops + 4u * (uint32_t)k) = kept[k];
    soul_stage_ops(dev, ops, &ok);
    a[0] = dev;
    if (blend != 1) {
        MEM32(HUD_BLEND_SET) = blend;
        a[1] = blend;
        soul_vcall(HUD_DEV_BLEND, SOUL_BLEND_BACK_RET, 2, a, &ok);
    }
    a[1] = 1;
    soul_vcall(HUD_DEV_ALPHA_TEST, SOUL_ALPHA_ON_RET, 2, a, &ok);
    wide_hud_end(WIDE_HUD_MARK, marked);
    g_esp = esp;
    if (!ok) given_up = 1;
}
/* sub_0010DC60: the spray can's model and its count's digits (class
 * 0x1D2370), on the right. */
extern void sub_0010DC60_gen(void);
void sub_0010DC60(void)
{
    int marked = wide_hud_begin(WIDE_HUD_CAN);
    sub_0010DC60_gen();
    wide_hud_end(WIDE_HUD_CAN, marked);
}
/* sub_00109B70: the boost icon (class 0x1D2060), a model, on the right. */
extern void sub_00109B70_gen(void);
void sub_00109B70(void)
{
    int marked = wide_hud_begin(WIDE_HUD_BOOST);
    sub_00109B70_gen();
    wide_hud_end(WIDE_HUD_BOOST, marked);
}
/* sub_00102DB0: a digit and the icon it picks (class 0x1D1E18), 530 to 585. */
extern void sub_00102DB0_gen(void);
void sub_00102DB0(void)
{
    int marked = wide_hud_begin(WIDE_HUD_DIGIT);
    sub_00102DB0_gen();
    wide_hud_end(WIDE_HUD_DIGIT, marked);
}
/* sub_00103AE0: two digits and an icon (class 0x1D1E58), 472 to 585. */
extern void sub_00103AE0_gen(void);
void sub_00103AE0(void)
{
    int marked = wide_hud_begin(WIDE_HUD_DIGITS);
    sub_00103AE0_gen();
    wide_hud_end(WIDE_HUD_DIGITS, marked);
}
/* sub_001040B0: two two-digit numbers (class 0x1D1E98), 500 to 604, made on
 * demand and shown for two seconds. */
extern void sub_001040B0_gen(void);
void sub_001040B0(void)
{
    int marked = wide_hud_begin(WIDE_HUD_NUMBERS);
    sub_001040B0_gen();
    wide_hud_end(WIDE_HUD_NUMBERS, marked);
}
/* sub_000FFFD0: the score and trick count (class 0x1D1CB0), on the left,
 * whose digits slide in from past the screen's edge, and its label, which
 * it writes with the font call (WIDE_HUD_TEXT). */
extern void sub_000FFFD0_gen(void);
void sub_000FFFD0(void)
{
    int marked = wide_hud_begin(WIDE_HUD_SCORE);
    sub_000FFFD0_gen();
    wide_hud_end(WIDE_HUD_SCORE, marked);
}
/* sub_00101F20: the trick's name (class 0x1D1D90), on the left. */
extern void sub_00101F20_gen(void);
void sub_00101F20(void)
{
    int marked = wide_hud_begin(WIDE_HUD_TRICK);
    sub_00101F20_gen();
    wide_hud_end(WIDE_HUD_TRICK, marked);
}
/* sub_0010A750: a seven-digit number that rolls upward when it changes
 * (class 0x1D20A0), at the top left. It sets a viewport of its own, 159 by
 * 19, which cuts off the roll, and puts the player's back after
 * (NV2A_WIDE_HUD_OWN_VIEW). */
extern void sub_0010A750_gen(void);
void sub_0010A750(void)
{
    int marked = wide_hud_begin(WIDE_HUD_ROLLING);
    sub_0010A750_gen();
    wide_hud_end(WIDE_HUD_ROLLING, marked);
}
/* sub_001045D0: a message that slides in from past the left edge (class
 * 0x1D1ED8), drawn with the font call alone (WIDE_HUD_TEXT). */
extern void sub_001045D0_gen(void);
void sub_001045D0(void)
{
    int marked = wide_hud_begin(WIDE_HUD_MESSAGE);
    sub_001045D0_gen();
    wide_hud_end(WIDE_HUD_MESSAGE, marked);
}
/* sub_00104AB0: a label and a three-digit number in the middle of the
 * screen (class 0x1D1F18), shown for two seconds when the number changes.
 * The HUD manager makes it (sub_001047B0, from sub_001077D0), under the
 * narrow root 0x1163 rather than itself; it stays in the middle in
 * widescreen, and is marked there for HUD scale (NV2A_WIDE_HUD_MIDDLE). */
extern void sub_00104AB0_gen(void);
void sub_00104AB0(void)
{
    int marked = wide_hud_begin(WIDE_HUD_COUNTER);
    sub_00104AB0_gen();
    wide_hud_end(WIDE_HUD_COUNTER, marked);
}
/* ---- the menus laid out for widescreen (widescreen_menus.h) ------------------
 * The game lays its menus out for a 4:3 screen, and in widescreen they sit
 * in the middle of the wide screen with their backdrop's edges showing.
 * Each menu that has a wide layout gets an override of its draw method
 * here, named for the menu (docs/project-rules.md, Where code goes 6),
 * which tells widescreen_menus.c which menu is drawing and its object. The
 * menu draws each of its parts, a 3D model held in a field of its object,
 * through the models' draw, sub_00047690; that call's override asks which
 * part the model is, by its field, and marks its draws for its side, or for
 * across the width: the bars and bands are stretched across, the backdrop's
 * grids are carried on to the screen's edges in two halves, and the other
 * parts keep their shape and go to the left or the right edge, as the HUD's
 * pieces do. The menu's text goes through the font call's override,
 * below, and the three parts that aren't fields through overrides of their
 * own draws. The marks are the HUD's, written the same way. As with the
 * HUD, nothing in these functions or in the recompiler is wrong: the menus
 * were laid out for the console's screen. In 4:3, and with no menu
 * drawing, nothing is marked. tests/widescreen_menus checks the parts, the
 * text's sides and the marks; how each menu then looks needs the game, the
 * test build's menu step. */
#include "widescreen_menus.h"
#include "nv2a_wide.h"
static int wide_menu_mark(unsigned piece, int side)
{
    return widescreen_menus_mark((uint8_t *)g_xbox_mem_offset, MEM32(D3D_DEVICE), piece, side,
                                 d3d_make_space);
}
static void wide_menu_unmark(unsigned piece, int marked)
{
    widescreen_menus_unmark((uint8_t *)g_xbox_mem_offset, MEM32(D3D_DEVICE), piece, marked,
                            d3d_make_space);
}
/* ---- music info in pause (music_info.h) --------------------------------------
 * The game has no display of the song that is playing, so a line for it
 * inside its own pause menus is added where those menus draw: each pause
 * menu's draw, below, calls music_info_draw after the menu's own text, on
 * the passes the menu draws on (once a frame). Nothing in these functions,
 * the recompiler or the runtime is wrong: this adds to what the menu draws,
 * with the game's own font call, so the lines get the font, its white and
 * the menu's 2D path, and in widescreen go to the left edge with CHANGE MAP
 * (the font call's override, while the menu's marks are open). A mix's play
 * position is ADXT_GetTime's. Both calls are made below the stack the menu
 * left, with MUSIC_SCRATCH bytes under it for ADXT_GetTime's two outputs
 * and the number's text: the guest's code keeps nothing below its stack
 * pointer, and the font call turns the text into glyphs as it is called.
 * tests/music_info checks the reading, the lookup and the lines; what they
 * look like needs the game, the test build's steps. */
#include "music_info.h"
extern void sub_0013A7D0(void);      /* ADXT_GetTime(adxt, int *ncount, int *tscale), cdecl */
void sub_0003D5D0(void);             /* the font call's override, below */
#define MUSIC_GET_TIME     0x0013A7D0u  /* ADXT_GetTime, as its own call's return address */
#define MUSIC_STREET_FONT  0x00112D5Cu  /* the street pause menu's first font call's return */
#define MUSIC_OTHER_FONT   0x00113643u  /* the other pause menu's font call's return */
#define PAUSE_PASS_DRAW    0u           /* the passes a pause menu's draw draws on */
#define PAUSE_PASS_DRAW_8  8u
#define MUSIC_SCRATCH      32u          /* the two outputs, then the number's text */
static void music_info_draw(int menu, uint32_t pass, uint32_t font_ret)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t esp = g_esp, scratch;
    int32_t time[2] = { 0, 0 };
    MusicNow now;
    MusicLine line[MUSIC_INFO_LINES];
    int song, n, k;
    if (!music_info_on() || (pass != PAUSE_PASS_DRAW && pass != PAUSE_PASS_DRAW_8)) return;
    if (!music_info_now(m, ADX_GUEST_RAM, &now)) return;
    g_esp -= MUSIC_SCRATCH;
    scratch = g_esp;
    if (music_info_is_mix(now.id)) {
        uint32_t a[3] = { now.player, scratch, scratch + 4 };
        memset(m + scratch, 0, sizeof time);
        guest_call_cdecl(sub_0013A7D0, MUSIC_GET_TIME, 3, a);
        memcpy(time, m + scratch, sizeof time);
    }
    song = music_info_pick(menu, now.id, time[0], time[1], jsrf_now());
    n = song < 0 ? 0 : music_info_lines(m, ADX_GUEST_RAM, song, line,
                                        (char *)m + scratch + sizeof time,
                                        MUSIC_SCRATCH - sizeof time);
    for (k = 0; k < n; k++) {
        const float f[5] = { line[k].x, line[k].y, MUSIC_INFO_SPACING, line[k].scale,
                             line[k].scale };
        uint32_t a[7];
        memcpy(&a[0], &f[0], 4);
        memcpy(&a[1], &f[1], 4);
        memcpy(&a[2], &f[2], 4);
        a[3] = MUSIC_INFO_WHITE;
        memcpy(&a[4], &f[3], 4);
        memcpy(&a[5], &f[4], 4);
        a[6] = line[k].text ? line[k].text : scratch + (uint32_t)sizeof time;
        guest_call_cdecl(sub_0003D5D0, font_ret, 7, a);
    }
    g_esp = esp;
}
/* sub_001129E0: the street pause menu's draw (class 0x1D2A00, slot +0x24,
 * this in ecx; one argument, the pass, ret 4), with the 3D map drawn
 * before it and the ticker after. The music's lines go after its own. */
extern void sub_001129E0_gen(void);
void sub_001129E0(void)
{
    uint32_t pass = GARG(1);
    int began = widescreen_menus_begin((uint8_t *)g_xbox_mem_offset, WIDE_MENU_STREET_PAUSE,
                                       g_ecx, nv2a_wide_scene(), jsrf_now());
    sub_001129E0_gen();
    music_info_draw(MUSIC_MENU_STREET, pass, MUSIC_STREET_FONT);
    widescreen_menus_end(began);
}
/* sub_001133A0: the other pause menu's draw, a race's or a mission's (class
 * 0x1D2AA8, slot +0x24, as the street one), with its header drawn after it
 * as its child. The music's lines go after its own. */
extern void sub_001133A0_gen(void);
void sub_001133A0(void)
{
    uint32_t pass = GARG(1);
    int began = widescreen_menus_begin((uint8_t *)g_xbox_mem_offset, WIDE_MENU_OTHER_PAUSE,
                                       g_ecx, nv2a_wide_scene(), jsrf_now());
    sub_001133A0_gen();
    music_info_draw(MUSIC_MENU_OTHER, pass, MUSIC_OTHER_FONT);
    widescreen_menus_end(began);
}
/* sub_00115030: the GG notebook's draw (class 0x1D2B68, slot +0x24, this in
 * ecx; one argument, the pass, ret 4), from the street pause menu. Its area
 * name, the map's sign, is drawn after it (sub_000FC370). */
extern void sub_00115030_gen(void);
void sub_00115030(void)
{
    int began = widescreen_menus_begin((uint8_t *)g_xbox_mem_offset, WIDE_MENU_NOTEBOOK,
                                       g_ecx, nv2a_wide_scene(), jsrf_now());
    sub_00115030_gen();
    widescreen_menus_end(began);
}
/* sub_00115A40: the draw of the character select that saving a graffiti
 * opens (class 0x1D2BB0, slot +0x24, this in ecx; one argument, the pass,
 * ret 4), which draws on its passes 0 and 8, laid out as the notebook. */
extern void sub_00115A40_gen(void);
void sub_00115A40(void)
{
    int began = widescreen_menus_begin((uint8_t *)g_xbox_mem_offset, WIDE_MENU_CHARACTERS,
                                       g_ecx, nv2a_wide_scene(), jsrf_now());
    sub_00115A40_gen();
    widescreen_menus_end(began);
}
/* sub_00111320: the race preview's draw (class 0x1D2788, slot +0xC, this in
 * ecx; one argument, the pass, ret 4; its +0x24 comes back here), shown
 * before every race and chase mission, with its course map, its race's
 * name (a header), its area's name and its ticker drawn after it. */
extern void sub_00111320_gen(void);
void sub_00111320(void)
{
    int began = widescreen_menus_begin((uint8_t *)g_xbox_mem_offset, WIDE_MENU_RACE_PREVIEW,
                                       g_ecx, nv2a_wide_scene(), jsrf_now());
    sub_00111320_gen();
    widescreen_menus_end(began);
}
/* sub_00114A80 and sub_001137B0: the pause menus' 3D map's draws, modes 0
 * and 1 (classes 0x1D2B28 and 0x1D2AE8, slot +0x24, this in ecx), in their
 * own viewport and camera, before the menu (widescreen-3b/menus/search-1,
 * section 5). The whole draw goes between marks of its own
 * (widescreen_menus_map), so that the renderer can keep the map off the
 * dark grid at the top and carry its yellow lines out to the wide screen's
 * edges (nv2a_wide_lines); the rest of the map is drawn as it was. */
extern void sub_00114A80_gen(void);
void sub_00114A80(void)
{
    int marked = widescreen_menus_map(0, nv2a_wide_scene())
              && wide_menu_mark(WIDE_MENU_PIECE_MAP, NV2A_WIDE_HUD_LINES);
    sub_00114A80_gen();
    wide_menu_unmark(WIDE_MENU_PIECE_MAP, marked);
}
extern void sub_001137B0_gen(void);
void sub_001137B0(void)
{
    int marked = widescreen_menus_map(1, nv2a_wide_scene())
              && wide_menu_mark(WIDE_MENU_PIECE_MAP, NV2A_WIDE_HUD_LINES);
    sub_001137B0_gen();
    wide_menu_unmark(WIDE_MENU_PIECE_MAP, marked);
}
/* sub_00112470: the race preview's course map's draw (class 0x1D27C8, slot
 * +0xC, this in ecx; it calls sub_00112150, which draws the course, its
 * route, arrow and flag), a child of the race preview drawn after it
 * (widescreen-3b/menus/search-6). Between marks of its own, as the pause
 * map's are, so that the renderer keeps it off the dark grid at the top. */
extern void sub_00112470_gen(void);
void sub_00112470(void)
{
    int marked = widescreen_menus_map(2, nv2a_wide_scene())
              && wide_menu_mark(WIDE_MENU_PIECE_COURSE, NV2A_WIDE_HUD_LINES);
    sub_00112470_gen();
    wide_menu_unmark(WIDE_MENU_PIECE_COURSE, marked);
}
/* sub_00047690: the models' draw (class 0x1CAA8C, slot +0x14, the model in
 * ecx, two arguments it passes on; it pops them), which every 3D model in
 * the game goes through. While a menu draws, a model that is one of its
 * parts is drawn between marks for its side. A model drawn while a part's
 * marks are open is left inside them.
 *
 * The backdrop (WIDE_MENU_EXTEND) is drawn twice, unstretched, where the
 * shape leaves room for a whole step of its grid (nv2a_wide_half_move):
 * first between marks for the screen's left half, through a call of its own
 * with a copy of the arguments, then between marks for the right half, as
 * the caller's own call. The renderer moves each copy out and shows it on
 * its half (nv2a_wide_half_shift); the game's draw is the same both times. */
extern void sub_00047690_gen(void);
#define WIDE_MENU_MODEL_DRAW 0x00047690u  /* the models' draw, as its own call's return address */
static void wide_menu_extend(unsigned piece)
{
    uint32_t args[2];
    int marked = nv2a_wide_half_move(nv2a_wide_squeeze()) > 0.0f
              && wide_menu_mark(piece, NV2A_WIDE_HUD_LEFT_HALF);
    if (!marked) { sub_00047690_gen(); return; }
    memcpy(args, (uint8_t *)g_xbox_mem_offset + g_esp + 4, sizeof args);
    guest_call_cdecl(sub_00047690_gen, WIDE_MENU_MODEL_DRAW, 2, args);
    wide_menu_unmark(piece, marked);
    marked = wide_menu_mark(piece, NV2A_WIDE_HUD_RIGHT_HALF);
    sub_00047690_gen();
    wide_menu_unmark(piece, marked);
}
static void wide_menu_model_draw(void)
{
    static int open;
    unsigned piece;
    int side, marked;
    if (open) { sub_00047690_gen(); return; }
    side = widescreen_menus_part((uint8_t *)g_xbox_mem_offset, g_ecx, &piece);
    if (side == NV2A_WIDE_HUD_NONE) {
        /* A plain menu's animated model: its moving rectangles, marked node
         * by node in sub_0005BD80. The animation is the first argument. */
        int sliders = widescreen_menus_sliders_begin((const uint8_t *)g_xbox_mem_offset,
                                                     ADX_GUEST_RAM, g_ecx, MEM32(g_esp + 4));
        sub_00047690_gen();
        widescreen_menus_sliders_end(sliders);
        return;
    }
    open = 1;
    if (side == WIDE_MENU_EXTEND) {
        wide_menu_extend(piece);
    } else {
        marked = wide_menu_mark(piece, side);
        sub_00047690_gen();
        wide_menu_unmark(piece, marked);
    }
    open = 0;
}
/* The message boxes' A and B, drawn flat in the button prompts' styles
 * (prompt_buttons.h). In any art but the game's own, the display set's A or
 * B (disp_models, with the skip prompt's A, below) is posed by the game's
 * own draw, but its nodes are handed to prompt_buttons_node by the node
 * functions (model_node, below) and not drawn; its mark goes into the push
 * buffer after, placed through the game's device's projection then
 * (prompt_view, below). Any other model is drawn as the widescreen menus
 * have it. Nothing in the game's code is wrong: a feature's hook. */
#include "prompt_buttons.h"
#include "input_prompts.h"
#include "nv2a_prompts.h"          /* the mark, and NV2A_PROMPT_LARGER */
static uint32_t disp_models(void);
static void prompt_view(float *focal, float *tall, uint32_t view[4]);
static PromptButtonsWalk s_flat = { .button = -1 };
static int s_in_box;                  /* the message box's draw is running */
/* The menus' 3D A and B (spriteice, 12:19 to 12:36 UTC on 21 November 2026:
 * the graffiti picker's and the VS menu's B, the graffiti editor's A and B;
 * 23:44 UTC: still tilted in prompts6, and a quarter larger wanted, as the
 * message box's were made). They aren't group 0x12's models: prompts6's
 * log (button-prompts/p12-rework/logs/ in the shared folder) has none of
 * those at their places. A menu may take the A and B from the display set
 * (group 10) for a set of its own, through the holder's getter,
 * sub_00025700(10, set, 1, id). So the getter's override notes every model
 * it hands out of group 10 as the A (0x69) or the B (0x6A), with the first
 * words of the model's object, and the models' draw (below) draws a model
 * so noted flat, as the message box's (prompt_buttons_open), the words
 * checked first so a model the game has since freed, and another put at
 * its address, isn't taken for the button. The message box's own come
 * through disp_models, as before. Model 0x6B, which the pause menus and
 * the graffiti picker fetch and pose by animation 0x12 (at 0x111025,
 * 0x112779, 0x113156, 0x11569E and 0x115896), is no button: it is the
 * bands, their lines and marks behind the menus (widescreen_menus.c's
 * table), 12 nodes, and prompts7, which took it for a B, drew a B picture
 * 1,072 of the title's pixels across over the whole menu
 * (prompts7's log, button-prompts/p12-rework/logs/prompts7-allyx-last-run-1.log
 * in the shared folder, and spriteice's screenshots, 00:29 UTC on 22
 * November). The picker's and the VS menu's B were already flat.
 *
 * The graffiti editor's A and B, still tilted in prompts6, are some other
 * model. So the next build can name them, the getter also notes everything else it
 * hands out, of any group, with its group, set, type and id; the first
 * draw of each is walked as the flat buttons are, but drawn as it is, its
 * nodes' spheres only watched (model_node, s_watch), and when it is small
 * on the title's screen, under MENU_SMALL pixels across as a button is, the
 * log says where and how big, up to MENU_TELLS lines for group 10, group
 * 0x12 and the rest each. */
#define MENU_GROUP    0x12u
#define BUTTON_GROUP  10u             /* the display set's (DISP_GROUP, below) */
#define BUTTON_TYPE   1u              /* its models */
#define BUTTON_NOTED  16              /* the buttons' notes: a few sets' A and B */
#define BUTTON_WORDS  8u              /* the model object's first words, checked before each draw */
#define MENU_NOTED    4096            /* a power of two: the notes' slots, by model */
#define MENU_PROBES   32              /* the slots looked at for one (every draw looks) */
#define MENU_TELLS    128
#define MENU_SMALL    80.0f           /* told only under this many of the title's pixels across */
static struct { uint32_t model, group, set, type, id; uint8_t told; } s_menu_note[MENU_NOTED];
static int s_menu_tells[3];           /* told: group 0x12's, group 10's, the rest's */
static int menu_bucket(uint32_t group) { return group == MENU_GROUP ? 0 : group == BUTTON_GROUP ? 1 : 2; }
static PromptButtonsWalk s_watch = { .button = -1 };
static struct { uint32_t model, id, words[BUTTON_WORDS]; } s_button_note[BUTTON_NOTED];
static unsigned s_button_next;
static uint32_t menu_slot(uint32_t model)
{
    uint32_t i = (model >> 4) & (MENU_NOTED - 1u), n;
    for (n = 0; n < MENU_PROBES; n++, i = (i + 1u) & (MENU_NOTED - 1u))
        if (!s_menu_note[i].model || s_menu_note[i].model == model) return i;
    return MENU_NOTED;
}
static int button_words(uint32_t model, uint32_t words[BUTTON_WORDS])
{
    if (model < 0x10000u || model > ADX_GUEST_RAM - BUTTON_WORDS * 4u) return 0;
    memcpy(words, (const uint8_t *)g_xbox_mem_offset + model, BUTTON_WORDS * 4u);
    return 1;
}
static void button_note(uint32_t model, uint32_t id)
{
    uint32_t words[BUTTON_WORDS];
    unsigned i;
    if (!button_words(model, words)) return;
    for (i = 0; i < BUTTON_NOTED; i++)
        if (s_button_note[i].model == model) break;
    if (i == BUTTON_NOTED) i = s_button_next++ % BUTTON_NOTED;
    s_button_note[i].model = model;
    s_button_note[i].id = id;
    memcpy(s_button_note[i].words, words, sizeof words);
}
/* The display set's id of a noted button at model, still the same object,
 * or 0. */
static uint32_t button_noted(uint32_t model)
{
    uint32_t words[BUTTON_WORDS];
    unsigned i;
    if (!model) return 0;
    for (i = 0; i < BUTTON_NOTED; i++)
        if (s_button_note[i].model == model)
            return button_words(model, words) && !memcmp(words, s_button_note[i].words, sizeof words)
                 ? s_button_note[i].id : 0;
    return 0;
}
extern void sub_00025700_gen(void);
void sub_00025700(void)
{
    const uint32_t group = MEM32(g_esp + 4u), set = MEM32(g_esp + 8u), type = MEM32(g_esp + 12u),
                   id = MEM32(g_esp + 16u);
    uint32_t i;
    sub_00025700_gen();
    if (!g_eax) return;
    if (group == BUTTON_GROUP && type == BUTTON_TYPE && prompt_buttons_id(id) >= 0) {
        button_note(g_eax, id);
        return;
    }
    i = menu_slot(g_eax);
    if (i >= MENU_NOTED || s_menu_note[i].model) return;
    s_menu_note[i].model = g_eax;
    s_menu_note[i].group = group;
    s_menu_note[i].set = set;
    s_menu_note[i].type = type;
    s_menu_note[i].id = id;
}
/* Whether model is one noted and not yet told of: its walk watched. */
static uint32_t menu_watch(uint32_t model)
{
    uint32_t i;
    if (s_watch.button >= 0 || !model) return MENU_NOTED;
    i = menu_slot(model);
    if (i >= MENU_NOTED || s_menu_note[i].model != model || s_menu_note[i].told
        || s_menu_tells[menu_bucket(s_menu_note[i].group)] >= MENU_TELLS)
        return MENU_NOTED;
    memset(&s_watch, 0, sizeof s_watch);
    s_watch.model = model;
    return i;
}
static void menu_told(uint32_t i)
{
    float focal, tall, x = 0.0f, y = 0.0f, half = 0.0f;
    uint32_t view[4];
    int placed, *tells = &s_menu_tells[menu_bucket(s_menu_note[i].group)];
    prompt_view(&focal, &tall, view);
    placed = prompt_buttons_place(&s_watch, focal, tall, view, &x, &y, &half);
    s_menu_note[i].told = 1;
    s_watch.button = -1;
    if (placed && !(2.0f * half < MENU_SMALL)) return;
    ++*tells;
    if (placed)
        fprintf(stderr, "[INPUT] button prompts: a model of group 0x%X's set %u, type %u, id 0x%X (at %08X): "
                "node %u of %u, sphere %.2f, scale %.3f, on screen at %.1f, %.1f, %.1f pixels across, in art "
                "%d\n", (unsigned)s_menu_note[i].group, (unsigned)s_menu_note[i].set,
                (unsigned)s_menu_note[i].type, (unsigned)s_menu_note[i].id, (unsigned)s_watch.model,
                s_watch.node, s_watch.nodes, (double)s_watch.r, (double)s_watch.scale, (double)x, (double)y,
                (double)(2.0f * half), input_prompts_art_now());
    else
        fprintf(stderr, "[INPUT] button prompts: a model of group 0x%X's set %u, type %u, id 0x%X (at %08X): "
                "%u nodes, none in front\n", (unsigned)s_menu_note[i].group, (unsigned)s_menu_note[i].set,
                (unsigned)s_menu_note[i].type, (unsigned)s_menu_note[i].id, (unsigned)s_watch.model,
                s_watch.nodes);
    if (*tells == MENU_TELLS)
        fprintf(stderr, "[INPUT] button prompts: %d small models told of (group 0x%X's bucket); no more\n",
                MENU_TELLS, (unsigned)s_menu_note[i].group);
}
/* A model the getter noted as a button (above), opened as one. */
static int button_begin(uint32_t model)
{
    const uint32_t id = button_noted(model);
    return id && prompt_buttons_open(&s_flat, model, id, input_prompts_art_now(), s_in_box > 0);
}
/* The graffiti editor's parts (widescreen_menus.h, "The graffiti editor"),
 * while its parts' draw runs in a widened scene (sub_0012A640, below): the
 * bar of static behind its ticker is drawn between marks for across the
 * width, as the pause menus' top bar is, all of it stretched. 1 when the
 * draw was done here. */
static int editor_part_draw(void)
{
    int marked;
    if (!widescreen_menus_editor_bar(widescreen_menus_editor_part((const uint8_t *)g_xbox_mem_offset, g_ecx)))
        return 0;
    marked = wide_menu_mark(WIDE_MENU_PIECE_EDITOR_BAR, NV2A_WIDE_HUD_ACROSS);
    sub_00047690_gen();
    wide_menu_unmark(WIDE_MENU_PIECE_EDITOR_BAR, marked);
    return 1;
}
void sub_00047690(void)
{
    uint8_t *mem = (uint8_t *)g_xbox_mem_offset;
    const uint32_t esp = g_esp;
    float focal, tall;
    uint32_t view[4];
    if (s_flat.button >= 0
        || !(prompt_buttons_begin(&s_flat, mem, ADX_GUEST_RAM, disp_models(), g_ecx,
                                  input_prompts_art_now(), s_in_box > 0)
             || button_begin(g_ecx))) {
        uint32_t watch;
        if (s_flat.button < 0 && editor_part_draw()) return;
        watch = s_flat.button < 0 ? menu_watch(g_ecx) : MENU_NOTED;
        if (watch < MENU_NOTED) s_watch.button = 0;
        wide_menu_model_draw();
        if (watch < MENU_NOTED) menu_told(watch);
        return;
    }
    sub_00047690_gen();
    /* Its `ret 8`: the walk, with a node skipped or drawn, leaves the stack
     * as the draw found it. */
    RECOMP_CHECK(g_esp == esp + 12u, "the flat button's walk left esp at %08X, not %08X", g_esp, esp + 12u);
    prompt_view(&focal, &tall, view);
    prompt_buttons_end(&s_flat, mem, MEM32(D3D_DEVICE), focal, tall, view, d3d_make_space);
}
/* sub_0006EC80: the message box's draw (class 0x1CC660, slot +0xC, this in
 * ecx), which draws its frame and its A and B through the models' draw:
 * while it runs, a button drawn flat is the box's (prompt_buttons.h).
 * Nothing in it is wrong: a feature's hook. */
extern void sub_0006EC80_gen(void);
void sub_0006EC80(void)
{
    s_in_box++;
    sub_0006EC80_gen();
    s_in_box--;
}
/* The node functions, one for each way the models' draw can draw a node
 * (the table at 0x1FA1B0, by the model's mode: sub_0005BE50 for 0,
 * sub_0005BD80 for 1, the fixed-function pass, sub_0005BF00 for 3,
 * sub_0005C840 for 4, sub_0005C990 for 5, a vertex program's, and
 * sub_0005CB90 for 6; 2's is empty), all cdecl, ret, with the model at +4
 * and the node at +8, which the pose's walk calls for each node with a
 * mesh, its matrix on top, nothing drawn between. While a plain menu's
 * animated model draws, a node that its animation slides along x
 * (widescreen_menus_slider), one of the moving rectangles, is drawn between
 * marks of its own: stretched across the whole width, over the filled
 * sides. menus5 had only the fixed-function pass's, which Roboy's menu
 * uses. */
static void wide_menu_node(void (*body)(void), uint32_t via)
{
    unsigned piece;
    int marked = 0;
    if (widescreen_menus_slider(MEM32(g_esp + 4), MEM32(g_esp + 8), via, &piece)
        == NV2A_WIDE_HUD_OVER)
        marked = wide_menu_mark(piece, NV2A_WIDE_HUD_OVER);
    body();
    wide_menu_unmark(WIDE_MENU_PIECE_SLIDERS, marked);
}
/* While a button is drawn flat (sub_00047690, above), its node is handed
 * in with its matrix on top, and not drawn. The pose's walks never read
 * what a node function returns (sub_000479D0, sub_00047BE0, sub_0005E9B0's
 * loop). With the body skipped, its `ret` is done here: the esp it would
 * leave comes back from prompt_buttons_node_ret. A node of the button's
 * model that isn't the button's (the save point's bubble round the Start's ball,
 * prompt_buttons_takes) is counted and drawn as the game draws it. */
static void model_node(void (*body)(void), uint32_t via)
{
    if (s_flat.button >= 0) {
        const uint8_t *mem = (const uint8_t *)g_xbox_mem_offset;
        if (prompt_buttons_takes(&s_flat)) {
            g_esp = prompt_buttons_node_ret(&s_flat, mem, ADX_GUEST_RAM, g_esp);
            return;
        }
        prompt_buttons_pass(&s_flat, mem, ADX_GUEST_RAM, MEM32(g_esp + 8u));
        wide_menu_node(body, via);
        return;
    }
    /* A menu's model watched (menu_watch, above): its node taken, and drawn. */
    if (s_watch.button >= 0)
        prompt_buttons_node(&s_watch, (const uint8_t *)g_xbox_mem_offset, ADX_GUEST_RAM, MEM32(g_esp + 8u));
    wide_menu_node(body, via);
}
extern void sub_0005BE50_gen(void);
void sub_0005BE50(void) { model_node(sub_0005BE50_gen, 0x0005BE50u); }
extern void sub_0005BD80_gen(void);
void sub_0005BD80(void) { model_node(sub_0005BD80_gen, 0x0005BD80u); }
extern void sub_0005BF00_gen(void);
void sub_0005BF00(void) { model_node(sub_0005BF00_gen, 0x0005BF00u); }
extern void sub_0005C840_gen(void);
void sub_0005C840(void) { model_node(sub_0005C840_gen, 0x0005C840u); }
extern void sub_0005C990_gen(void);
void sub_0005C990(void) { model_node(sub_0005C990_gen, 0x0005C990u); }
extern void sub_0005CB90_gen(void);
void sub_0005CB90(void) { model_node(sub_0005CB90_gen, 0x0005CB90u); }
/* sub_000FC370: the HUD signs' draw (class 0x1D1B68, this in ecx). The map
 * makes one too, an area's name: the strip at the top left of the street
 * pause menu, which goes to the left edge with that menu's left-hand parts.
 * The banner with an area's name under the arrow as the player nears
 * another area, a sign of the arrow's object's, goes there too, the arrow
 * staying where it is. So does the word FLAG under the same arrow in a
 * flag chase, its x (+WIDE_MENU_SIGN_X) moved further left for its draw,
 * to the banner's gap (widescreen_menus_sign_x), and put back after, word
 * for word: only the sign's maker writes it. The result screens' banners,
 * YOU WIN, CLEARED, GAME OVER and YOU LOSE, are signs the result flow
 * makes, and go to the right edge. The HUD's own signs, such as the stop
 * sign, stay in the middle, marked there for HUD scale
 * (widescreen_hud_sign, WIDE_HUD_SIGN). */
extern void sub_000FC370_gen(void);
void sub_000FC370(void)
{
    uint8_t *mem = (uint8_t *)g_xbox_mem_offset;
    uint32_t sign = g_ecx, x = 0;
    unsigned piece = 0;
    int side = nv2a_wide_scene()
        ? widescreen_menus_sign(mem, ADX_GUEST_RAM, sign, jsrf_now(), &piece)
        : NV2A_WIDE_HUD_NONE;
    int marked = side != NV2A_WIDE_HUD_NONE && wide_menu_mark(piece, side);
    float move = marked && piece == WIDE_MENU_PIECE_FLAG
               ? widescreen_menus_sign_x(mem, ADX_GUEST_RAM, sign) : 0.0f;
    int hud = side == NV2A_WIDE_HUD_NONE
           && widescreen_hud_sign(mem, sign)
           && wide_hud_begin(WIDE_HUD_SIGN);
    if (move != 0.0f) {
        float f;
        memcpy(&x, mem + sign + WIDE_MENU_SIGN_X, sizeof x);
        memcpy(&f, &x, sizeof f);
        f += move;
        memcpy(mem + sign + WIDE_MENU_SIGN_X, &f, sizeof f);
    }
    sub_000FC370_gen();
    if (move != 0.0f) memcpy(mem + sign + WIDE_MENU_SIGN_X, &x, sizeof x);
    wide_hud_end(WIDE_HUD_SIGN, hud);
    wide_menu_unmark(piece, marked);
}
/* sub_000F7F00: the draw of a header (class 0x1D0770, this in ecx), which
 * other screens use too. The other pause menu's header, the menu's first
 * child, goes to the left edge with that menu's left-hand parts, and so
 * does the race preview's, the race's name; every other header stays as it
 * is. */
extern void sub_000F7F00_gen(void);
void sub_000F7F00(void)
{
    int marked = nv2a_wide_scene()
              && widescreen_menus_child((uint8_t *)g_xbox_mem_offset, g_ecx, jsrf_now())
              && wide_menu_mark(WIDE_MENU_PIECE_CHILD, NV2A_WIDE_HUD_LEFT);
    sub_000F7F00_gen();
    wide_menu_unmark(WIDE_MENU_PIECE_CHILD, marked);
}
/* sub_00115F40: the ticker's draw (class 0x1D3770, or the race preview's
 * 0x1D07F8; this in ecx), a line of text that scrolls in from the right
 * along a pause menu's top bar in a mission, or the race preview's, drawn
 * after the menu. Its text runs the whole width, through the font call's
 * override. */
extern void sub_00115F40_gen(void);
void sub_00115F40(void)
{
    int began = widescreen_menus_ticker_begin((uint8_t *)g_xbox_mem_offset, g_ecx,
                                              nv2a_wide_scene(), jsrf_now());
    sub_00115F40_gen();
    widescreen_menus_ticker_end(began);
}
/* sub_0003D5D0: the game's font call (x, y, depth, colour, x scale, y scale,
 * text; cdecl), which queues the text for the font's one draw late in the
 * frame, with every other piece of text. While a piece with text runs,
 * marked, its text goes instead through sub_0003D610, the font's call that
 * takes the same arguments and draws at once, through the font's own
 * batch: so it is drawn between the piece's marks, and moves and is clipped
 * with it, as the piece's digits are. Called in its place, with the stack
 * as the caller left it, it returns to the caller as sub_0003D5D0 would.
 * While a menu draws, its text is drawn at once the same way, between marks
 * of its own for the side its x is on (widescreen_menus_text_side): the
 * float at +4. While the ticker draws, its text is drawn at once across the
 * whole width, its x spread out from the middle (widescreen_menus_ticker_x)
 * and put back after. Any other text that is one of the game's lines
 * naming the A or B button by its letter, or the game's loaded text naming
 * a control, in a style that draws those (prompt_text.h, below), is drawn
 * at once too, through sub_0003D610's override, so its pictures are drawn
 * with it. */
extern void sub_0003D5D0_gen(void);
extern void sub_0003D610(void);
static int text_line_now(void);
void sub_0003D5D0(void)
{
    uint8_t *arg_x = (uint8_t *)g_xbox_mem_offset + g_esp + 4;
    unsigned piece;
    float x, wide_x;
    int side;
    if (widescreen_hud_text_now()) {
        sub_0003D610();
        return;
    }
    /* A result screen's choice drawing: its label, between its marks. */
    if (widescreen_menus_result_text()) {
        sub_0003D610();
        return;
    }
    memcpy(&x, arg_x, sizeof x);
    side = widescreen_menus_ticker_x(x, nv2a_wide_squeeze(), &wide_x, &piece);
    if (side != NV2A_WIDE_HUD_NONE) {
        int marked = wide_menu_mark(piece, side);
        if (marked) memcpy(arg_x, &wide_x, sizeof wide_x);
        sub_0003D610();
        memcpy(arg_x, &x, sizeof x);    /* sub_0003D610 has returned: x is no longer at +4 */
        wide_menu_unmark(piece, marked);
        return;
    }
    {
        float y;
        memcpy(&y, arg_x + 4, sizeof y);
        side = widescreen_menus_text_side(x, y, &piece);
    }
    if (side != NV2A_WIDE_HUD_NONE) {
        int marked = wide_menu_mark(piece, side);
        sub_0003D610();
        wide_menu_unmark(piece, marked);
        return;
    }
    /* A line naming the A or B by its letter, or loaded text naming a
     * control, in a style that draws it: at once, with its pictures
     * (prompt_text.h, below). */
    if (text_line_now()) {
        sub_0003D610();
        return;
    }
    sub_0003D5D0_gen();
}
/* ---- Skip cutscenes (cutscene_skip.h) ---------------------------------------
 * The scene's update, sub_00023790 (class 0x1C4BA8, slot +0x10, this in ecx,
 * no arguments), plays it frame by frame; at its last frame, and for the
 * game's own skip of a scene the save has seen (A or Start pressed this
 * frame, bit 2 or 0 of the word at PAD_PRESSED), it calls the scene's end,
 * sub_0001E8C0 (thiscall, no arguments): the scene marked seen, the fade
 * back to play, its actors, sound and resources let go, and the scene and
 * its holder removed, so the mission's flow carries on. Here, after the
 * update, a scene that still plays (still the one in the game manager's
 * slot SCENE_ACTOR, its class, state SCENE_PLAYING) has the frame's A
 * press counted (cutscene_skip_frame), and at a second press its end is
 * called as the update calls it, from the update's own call site. With the
 * option on, that is the one way out of a scene: the game's own skip of a
 * scene seen before (its seen test, sub_0003A8C0, at 0x238F4) is told "not
 * seen", so a scene seen before takes the two presses too (spriteice, 13:48
 * UTC on 16 November 2026: "a single rule with our option ticked only,
 * without is the games default"). Off, nothing is changed. Nothing
 * in these functions, the recompiler or the runtime is wrong: this reaches
 * the end the game already has, sooner. The idea is Miggas's (JSRF-Recomp)
 * and mstan's (JSRFRecomp), whose code isn't used.
 *
 * The prompt is drawn at the end of the frame's draw, sub_000131F0 (the
 * game manager's, this in ecx, no arguments; its one caller, sub_00013930,
 * calls it on every frame that draws and then presents), after every view
 * the game draws, so it is drawn in a scene, when the HUD isn't. (Build 3
 * drew it after the font's own last pass of the frame, sub_0003C310's pass
 * 8, which a scene's frames don't run: the presses worked and the prompt
 * never showed.) The device's BeginScene and EndScene (vtable 0x1E0F00,
 * +0xB0 and +0xB4, both sub_0014D080) do nothing, so a draw after the
 * frame's EndScene and before its Present is drawn as any other. Each of the
 * prompt's pieces goes through sub_0003D610, the font's call that draws at
 * once (sub_0003D210: its glyphs made, then the font's own draw, passes 7
 * and 8, which set the blend and textures they need), between marks of a
 * HUD piece of its own (WIDE_HUD_SKIP): the bottom right, as the gauge, so
 * widescreen and HUD scale move and size it with the HUD. Each piece's
 * width is read back from the pen the font leaves (FONT_PEN_X), for the
 * next frame's places. The first draw, and a frame without the font, are
 * logged once.
 *
 * The A on the prompt is the game's own: the green A button its message
 * boxes show (spriteice, 11:26 UTC on 16 November 2026: "the xbox green A
 * button when skipping text dialogue"), drawn as the box draws it
 * (prompt_a_draw, below). Where the game has no such model loaded, the
 * font's cap and A are drawn in its place, and the log says so once.
 * tests/cutscene_skip checks the presses, the fades and the places; that a
 * scene ends cleanly, and how the prompt looks, need the game: the test
 * build's steps. A scene starting to play, and ending, is an anchor for
 * input playback (port/playback.c), whether or not it was skipped. */
#include "cutscene_skip.h"
#define SCENE_CLASS      0x001C4BA8u  /* the scene's class (its vtable) */
#define SCENE_STATE      0x88u        /* 0 loading, 1 fading in, 2 playing */
#define SCENE_PLAYING    2u
#define SCENE_ACTOR      0x1DDDu      /* the scene's slot in the game manager */
#define SCENE_END_RA     0x00023915u  /* after the update's own call of sub_0001E8C0 */
#define SCENE_SEEN_RA    0x000238F9u  /* after the update's call of sub_0003A8C0, for its own skip */
#define GAME_MANAGER     0x0022FCE0u  /* the pointer to the game manager (MUSIC_OBJECTS) */
#define GM_ACTORS        0x98u        /* the game manager's actors, one pointer each */
#define PAD_PRESSED      0x00251EE8u  /* the buttons pressed this frame, as the scene's own skip reads them */
#define PAD_PRESSED_A    0x04u
#define FONT_ACTOR       0x1166u      /* the font's slot in the game manager */
#define FONT_PEN_X       0x0004B118u  /* the font's pen, after a draw at once: its x */
#define FONT_SPACING     1.1f         /* the font call's third argument, as every menu label passes */
#define PROMPT_FONT_RA   0x0010057Fu  /* a HUD piece's font call's return: the score's */
#define PROMPT_SCRATCH   32u          /* below the stack: a piece's text */
extern void sub_00023790_gen(void);
extern void sub_0001E8C0(void);
extern void sub_000131F0_gen(void);
extern void sub_0003D610(void);
/* The scene at scene is still the one the game manager has, and plays. */
static int scene_playing(uint32_t scene)
{
    uint32_t gm = MEM32(GAME_MANAGER);
    return gm && scene && MEM32(gm + GM_ACTORS + SCENE_ACTOR * 4u) == scene
        && MEM32(scene) == SCENE_CLASS && MEM32(scene + SCENE_STATE) == SCENE_PLAYING;
}
void sub_00023790(void)
{
    const uint32_t scene = g_ecx;
    const int was = scene_playing(scene);
    sub_00023790_gen();
    if (cutscene_skip_on() && scene_playing(scene)
        && cutscene_skip_frame(scene, (MEM8(PAD_PRESSED) & PAD_PRESSED_A) != 0, jsrf_now())) {
        const uint32_t sv_ecx = g_ecx;
        g_ecx = scene;
        guest_call_cdecl(sub_0001E8C0, SCENE_END_RA, 0, NULL);
        g_ecx = sv_ecx;
    }
    if (scene_playing(scene) != was)
        playback_anchor(was ? PLAYBACK_SCENE_END : PLAYBACK_SCENE, 0, game_random());
}

/* Rival previews (spriteice, 12:58 UTC on 16 November 2026: "skip
 * cutscenes also cover skater previews (eg watching monster jam skate to
 * their location in stg10 0121)"; known bug 82, 8:30pm on 24 November 2026:
 * "cant skip beats follow skater scenes in dogenzaka hill"). A preview isn't
 * a scene object: its mission plays it, and waits on its rival's arrival.
 * Its trigger 27 [slot] (the trigger scan's case at 0x5718B) holds when the
 * game manager's actor in slot slot + RIVAL_SLOT_FIRST is there, its state
 * (RIVAL_STATE) RIVAL_IDLE or RIVAL_ARRIVED and its RIVAL_LINK 0, and sets
 * the arrival's flag; the mission's actions then remove the rival (77, at
 * 0x596C9), take the bars away (207) and set the preview's seen bit (210),
 * and its events play a scene where it has one (227) and end it (230). The
 * rival (class RIVAL_CLASS, its update sub_000A76E0) replays its run in
 * state RIVAL_ON_RUN (0xA8023): its records from the one at RIVAL_RECORD,
 * the frames left of the current one at RIVAL_FRAMES_LEFT, RIVAL_STARTING
 * set until its first frame loads the first record. When the last record's
 * frames run out it sets RIVAL_ARRIVED (0xA8083), or in game modes 2 and 3
 * removes itself (0xA806E). 22 missions play a preview this way; every one
 * has one rival, in slot 1.
 *
 * With Skip cutscenes on, after the events scan (sub_00057DE0) has found
 * nothing and no scene is there, a rival the mission waits on
 * (cutscene_skip_rivals, over its trigger list) that is still on its run
 * (its first frame done) has the frame's A press counted
 * (cutscene_skip_frame, keyed by the rival), and at the second press its
 * run jumps to its last record (cutscene_skip_run_last) with one frame of
 * it left: its next update ends the run its own way, and the mission plays
 * the arrival its own way, as if the run had played out. That is every
 * preview, whether the game has a skip of its own for it (command 249) or
 * not: the 249's seen test (sub_0003A8C0, from the scan at 0x57E8B) is
 * answered "not seen" while the option is on, so the game's one-press skip
 * of a preview seen before is left out, as for scenes. The prompt goes when
 * the run is over, the presses no longer counted. Nothing in these
 * functions, the recompiler or the runtime is wrong: this reaches the end
 * the rival's run already has, sooner.
 *
 * Until known bug 82 a preview was known by its events list's shape (only
 * ends and the game's own skips, at least one 249), which five previews
 * don't have (0111, 0242, 0701, 0817, 0876), and was ended by a
 * reconstruction of its end, which jumped over what the mission does at the
 * arrival (known bug 68: the bars). Now the mission's own script does all
 * of it. After a skip the log says when the rival has arrived, and a
 * RECOMP_CHECK says once if it hasn't within ARRIVAL_WAIT seconds: trigger
 * 27 would then not hold, and the preview would wait. */
#define PREVIEW_SEEN_RA   0x00057E90u  /* the events scan's call of sub_0003A8C0 for command 249 */
#define RUNNER_SCRIPT     0x1040u      /* the stage script runner's (ecx's) mission script */
#define SCRIPT_TRIGGERS   0x17Cu       /* its trigger list, and the count after it */
#define RIVAL_SLOT_FIRST  0x0Cu        /* a mission's rival n is the game manager's actor n + this */
#define RIVAL_CLASS       0x001CD570u  /* the rival's class (its vtable) */
#define RIVAL_STARTING    0x50u        /* set until its run's first frame */
#define RIVAL_FRAMES_LEFT 0x54u        /* the frames left of its current record */
#define RIVAL_RECORD      0x58u        /* its current record */
#define RIVAL_LINK        0x8B0u       /* 0 for trigger 27 to hold (a link its update keeps; not read further) */
#define RIVAL_STATE       0x8B4u
#define RIVAL_IDLE        0u           /* ... which trigger 27 takes as arrived, */
#define RIVAL_ON_RUN      1u           /* ... replaying its run, */
#define RIVAL_ARRIVED     6u           /* ... and its run over */
#define RIVAL_SLOTS       4            /* rivals one mission waits on, at most */
#define ARRIVAL_WAIT      1.0          /* seconds a skipped rival has to arrive */
extern void sub_0003A8C0_gen(void);
void sub_0003A8C0(void)
{
    const uint32_t ra = MEM32(g_esp);
    sub_0003A8C0_gen();
    if (cutscene_skip_on() && (ra == SCENE_SEEN_RA || ra == PREVIEW_SEEN_RA))
        g_eax = 0;      /* the game's own skip left out: the two presses instead */
}

static int scene_there(void)
{
    uint32_t gm = MEM32(GAME_MANAGER);
    return gm && MEM32(gm + GM_ACTORS + SCENE_ACTOR * 4u) != 0;
}
/* The rival in a mission's slot, as the game's actor lookup (sub_000128C0)
 * finds it, if it is one; else 0. */
static uint32_t rival_actor(uint32_t slot)
{
    const uint32_t gm = MEM32(GAME_MANAGER);
    const uint32_t at = slot + RIVAL_SLOT_FIRST;
    const uint32_t actor = gm && (int32_t)at >= 0 ? MEM32(gm + GM_ACTORS + at * 4u) : 0;
    return actor && MEM32(actor) == RIVAL_CLASS ? actor : 0;
}
/* Trigger 27's test of a rival (0x5718B). */
static int rival_arrived(uint32_t actor)
{
    const uint32_t state = MEM32(actor + RIVAL_STATE);
    return (state == RIVAL_IDLE || state == RIVAL_ARRIVED) && MEM32(actor + RIVAL_LINK) == 0;
}
static void preview_skip(uint32_t runner)
{
    static uint32_t s_told, s_skipped, s_skipped_slot;
    static double s_skipped_at;
    uint32_t script, slots[RIVAL_SLOTS], slot = 0, actor = 0, record, last;
    int count, k;
    const double now = jsrf_now();
    if (s_skipped) {
        const uint32_t there = rival_actor(s_skipped_slot);
        if (there != s_skipped || rival_arrived(there)) {
            fprintf(stderr, "[SKIP] t=%.2f the rival in slot %u has arrived, %.2f s after the skip: the "
                    "mission plays its arrival\n", now, s_skipped_slot, now - s_skipped_at);
            s_skipped = 0;
        } else if (!RECOMP_CHECK(now - s_skipped_at < ARRIVAL_WAIT, "the rival in slot %u (%08X) hasn't "
                                 "arrived %.2f s after its run was ended: state %u, link %08X",
                                 s_skipped_slot, there, now - s_skipped_at, MEM32(there + RIVAL_STATE),
                                 MEM32(there + RIVAL_LINK))) {
            s_skipped = 0;
        }
    }
    if ((int32_t)g_eax >= 0 || !cutscene_skip_on() || scene_there()) return;
    script = MEM32(runner + RUNNER_SCRIPT);
    if (!script) return;
    count = cutscene_skip_rivals((const uint8_t *)g_xbox_mem_offset, MEM32(script + SCRIPT_TRIGGERS),
                                 MEM32(script + SCRIPT_TRIGGERS + 4u), slots, RIVAL_SLOTS);
    for (k = 0; k < count && !actor; k++) {
        const uint32_t a = rival_actor(slots[k]);
        if (a && MEM32(a + RIVAL_STATE) == RIVAL_ON_RUN && !MEM32(a + RIVAL_STARTING)) {
            actor = a;
            slot = slots[k];
        }
    }
    if (!actor) {
        s_told = 0;     /* the next preview told, even at the same address */
        return;
    }
    if (actor != s_told) {
        s_told = actor;
        fprintf(stderr, "[SKIP] t=%.2f a preview plays: the mission waits on its rival in slot %u (%08X), "
                "on its run: two presses of A end it\n", now, slot, actor);
    }
    if (!cutscene_skip_frame(actor, (MEM8(PAD_PRESSED) & PAD_PRESSED_A) != 0, now)) return;
    record = MEM32(actor + RIVAL_RECORD);
    last = cutscene_skip_run_last((const uint8_t *)g_xbox_mem_offset, ADX_GUEST_RAM, record);
    if (!last) {
        fprintf(stderr, "[SKIP] t=%.2f the rival in slot %u has no last record from %08X: its preview "
                "plays on\n", now, slot, record);
        return;
    }
    MEM32(actor + RIVAL_RECORD) = last;
    MEM32(actor + RIVAL_FRAMES_LEFT) = 1;
    s_skipped = actor;
    s_skipped_slot = slot;
    s_skipped_at = now;
    fprintf(stderr, "[SKIP] t=%.2f the preview ends: the run of the rival in slot %u (%08X) jumps from its "
            "record at %08X to its last, at %08X, with one frame left, for the rival to end it and the "
            "mission to play the arrival\n", now, slot, actor, record, last);
}
/* Widths of the prompt's pieces at its own size, as the font last drew
 * them; before the first draw, a guess (the first frame is drawn clear).
 * The cap's guess is the line's height times CUTSCENE_SKIP_CAP_SIZE, which
 * it keeps while the game's A is drawn there and the font's cap isn't. */
static float s_prompt_width[CUTSCENE_SKIP_PIECES] = { 52.0f, 19.5f, 12.0f, 120.0f };
static int s_prompt_logged, s_prompt_no_font;

/* ---- The prompt's A: the game's own ----------------------------------------
 * The message box (class 0x1CC660, made by sub_0006F200 with the box's
 * flags) loads from the display set, through sub_00025700(10, 0, type, id):
 * its frame, model 0xFA, and two buttons, models 0x69 and 0x6A (type 1),
 * with animation 0x11 (type 2). A box that closes at A alone, as
 * sub_0006F6D0's do (flags 0x0B000000), shows model 0x69, the green A. The
 * box's draw, sub_0006EC80, at the frame's views' 0x13: the viewport the
 * whole 640 x 480 (the game's device +0x78, a RECT), the projection 0x222
 * across (3 degrees: a turn is 0x10000), near 1, far 4000 (+0x7C), a depth
 * offset of -28.5 (+0x84); a matrix of its own on the game's stack
 * (sub_001BAA00), moved to the box, 1220 deep, and the A's further to
 * 1201 deep (sub_001BAA60); the model's draw, sub_00047690 (this the
 * model, the animation and its frame); the matrix dropped (sub_001BAA50).
 * The box's update (sub_0006F3A0) steps the frame by 1, back to 0 at the
 * animation's last (its frame count, a short at +2).
 *
 * Here it is drawn the same way at the frame's end, where the font's cap
 * was, its middle at the cap's middle. The box draws its A beside text at
 * the font's scale 1; the prompt's text is at CUTSCENE_SKIP_SCALE times
 * the prompt's size, so the A is that much smaller: 1201 deep divided by
 * it. Its place is found from the projection the device has just been
 * given (its focal length, +0x50, and the height's share of it, +0x60).
 * The frame's own drawing is done, so the depth test passes always and
 * nothing is written to the depth (D3D's ZFUNC and ZWRITEENABLE, the
 * device's +0x134 and +0x130). Then all of it is put back: the depth
 * states, the depth offset, the viewport and the projection. The model
 * fades with no opacity of its own: it shows while the prompt is half in
 * or more. JSRF_SKIP_A_MODEL (a number) draws another of the set's models
 * instead, and JSRF_SKIP_A_SIZE (a factor) another size, for trying them
 * on the PC without a new build. In a button prompts' style other than the
 * game's own art, the models' draw draws it as a flat picture, as the boxes' A
 * (prompt_buttons.h).
 *
 * The set (D:\Media\Disp\SprNorm.bin, or SprNorm1 to 4 by sub_00043700's
 * answer, loaded by sub_000348A0 a step a frame) isn't always there: the
 * game lets it go for some story scenes (the flow's steps at 0x0007C020
 * and 0x0007D290, sub_00025740(10, 0)), as for the
 * garage's first scene after the tutorial, and loads it again where it's
 * wanted, as the garage's speech boxes want it (the step at 0x0007BFD0:
 * if sub_000256C0(10, 0) says it isn't loaded and the holder isn't serving
 * another (sub_00025680, its +0x44), sub_000256A0(10, 0) asks for it).
 * Where the prompt finds no set, it asks for it the same way, each frame
 * until it's there; the holder loads it in a few frames, and the cap's
 * place is left empty meanwhile. After A_WAIT seconds with no set,
 * the font's cap and A are drawn. The set is left loaded: the game's own
 * steps let it go where they would have, and its step that loads it finds
 * it there.
 *
 * The holder serves one command at a time, a step a frame from its update
 * (sub_00025310: the group's step, table 0x1EC0F0, until it answers 1),
 * and the game never lets the set go while it is loading it: its flow
 * asks, waits for it, and only later lets it go. The prompt's ask can be
 * loading when a scene ends and the flow's next step lets the set go
 * (sub_00025740(10, 0), which would free what the load is still filling).
 * So that letting go is held back while the set loads, and done as soon
 * as the holder is done: at the frame's end, or first thing when the game
 * next asks the holder anything (sub_000256A0, sub_000256C0), so that it
 * finds the set gone, as it left it.
 *
 * spriteice's notes of 13:04 and 13:16 UTC on 2026-11-16: the font's cap
 * at that first scene, and the speech boxes above the characters there
 * with their A. */
#define GAME_DEVICE       0x00251D6Cu /* the pointer to the game's own device (vtable 0x1E0F00) */
#define DEV_VIEWPORT      0x78u       /* (device, RECT *) */
#define DEV_PROJECTION    0x7Cu       /* (device, angle, near, far) */
#define DEV_DEPTH_OFFSET  0x84u       /* (device, offset) */
#define DEV_Z_WRITE       0x130u      /* (device, D3D's ZWRITEENABLE) */
#define DEV_Z_FUNC        0x134u      /* (device, D3D's ZFUNC) */
#define DEV_NEAR          0x40u
#define DEV_FAR           0x44u
#define DEV_ANGLE         0x48u
#define DEV_OFFSET        0x4Cu
#define DEV_FOCAL         0x50u       /* the viewport's half width over the half angle's tangent */
#define DEV_TALL          0x60u       /* the height's focal length, as a share of DEV_FOCAL */
#define VIEW_NOW          0x00264ED0u /* the viewport now: x, y, width, height */
#define RS_ZFUNC          0x0019E1C4u /* D3D's render state: ZFUNC */
#define RS_ZWRITE         0x0019E1E0u /* ... and ZWRITEENABLE */
#define Z_ALWAYS          0x207u      /* D3DCMP_ALWAYS (the GPU's own) */
#define DISP_ACTOR        7u          /* the display set's holder, in the game manager */
#define DISP_SET          0x15FCu     /* its loaded set (sub_00034850): */
#define DISP_MODELS       0x10u       /*   type 1, by id */
#define DISP_ANIMS        0x18u       /*   type 2, by id */
#define DISP_GROUP        10u         /* the set's number in the holder's tables (0x1EC068 and on) */
#define DISP_BUSY         0x44u       /* the holder is serving a command (sub_00025680) */
#define DISP_SERVING      0x48u       /* ... which (the group), and +0x4C its word */
#define DISP_ASK_RA       0x0007BFF4u /* the flow's own ask's return (its call at 0x0007BFEF) */
#define DISP_LET_GO_RA    0x0007D29Cu /* the flow's own letting go's return (its call at 0x0007D297) */
#define A_WAIT            2.0         /* seconds the prompt waits for the set before the font's cap */
#define A_MODEL           0x69u
#define A_ANIM            0x11u
#define A_DEPTH           1201.0f     /* the box's A, beside text at scale 1 */
#define BOX_ANGLE         0x222u
#define BOX_NEAR          1.0f
#define BOX_FAR           4000.0f
#define BOX_OFFSET        (-28.5f)
#define BOX_VIEWPORT_RA   0x0006ECC7u /* the box's own calls' returns */
#define BOX_PROJECTION_RA 0x0006ECE1u
#define BOX_OFFSET_RA     0x0006ED1Au
#define BOX_PUSH_RA       0x0006ED1Fu
#define BOX_MOVE_RA       0x0006ED67u
#define BOX_DRAW_RA       0x0006ED87u
#define BOX_POP_RA        0x0006ED8Cu
#define BOX_ZERO_RA       0x0006EE5Cu
extern void sub_001BAA00(void);
extern void sub_001BAA60(void);
extern void sub_001BAA50(void);
extern void sub_00047690(void);
static uint32_t f32_bits(float f) { uint32_t u; memcpy(&u, &f, sizeof u); return u; }
static float bits_f32(uint32_t u) { float f; memcpy(&f, &u, sizeof f); return f; }
/* A call of the game's device's slot `slot`, D3D's way: the device the
 * first argument. 0 when the slot isn't the game's code. */
static int prompt_device(uint32_t dev, uint32_t slot, uint32_t ra, int nargs, const uint32_t *args)
{
    const uint32_t va = MEM32(MEM32(dev) + slot);
    recomp_func_t fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    if (!fn) return 0;
    guest_call_cdecl(fn, ra, nargs, args);
    return 1;
}
static uint32_t prompt_env(const char *name, uint32_t fallback)
{
    const char *e = getenv(name);
    return e && *e ? (uint32_t)strtoul(e, NULL, 0) : fallback;
}
enum { A_NONE, A_THERE, A_WAITING };
static float s_a_frame;
static int s_a_logged;
static double s_a_since = -1.0;    /* when this showing of the prompt found no set; -1 not */
static double s_a_asked = -1.0;    /* when the game took the ask, for the set's line; -1 not */
static int s_a_gave_up;            /* this showing's line said */
static uint32_t s_want_model = 0xFFFFFFFFu;
static float s_want_size;
static int s_let_go_held;          /* the game let the set go while it loaded */
static uint32_t disp_holder(void)
{
    const uint32_t gm = MEM32(GAME_MANAGER);
    return gm ? MEM32(gm + GM_ACTORS + DISP_ACTOR * 4u) : 0;
}
static uint32_t disp_models(void)
{
    const uint32_t holder = disp_holder();
    const uint32_t set = holder ? MEM32(holder + DISP_SET) : 0;
    return set ? MEM32(set + DISP_MODELS) : 0;
}
/* The game's device's projection now, as prompt_a_draw reads it, for a
 * button drawn flat (sub_00047690, prompt_buttons_place): its focal length,
 * the height's share of it, and the viewport. */
static void prompt_view(float *focal, float *tall, uint32_t view[4])
{
    const uint32_t dev = MEM32(GAME_DEVICE);
    *focal = dev ? bits_f32(MEM32(dev + DEV_FOCAL)) : 0.0f;
    *tall = dev ? bits_f32(MEM32(dev + DEV_TALL)) : 1.0f;
    if (!(*tall > 0.5f && *tall < 2.0f)) *tall = 1.0f;
    view[0] = MEM32(VIEW_NOW);
    view[1] = MEM32(VIEW_NOW + 4u);
    view[2] = MEM32(VIEW_NOW + 8u);
    view[3] = MEM32(VIEW_NOW + 12u);
}
static int disp_loading(uint32_t holder)
{
    return holder && MEM32(holder + DISP_BUSY) && MEM32(holder + DISP_SERVING) == DISP_GROUP;
}
extern void sub_00025740_gen(void);
static void disp_let_go_held(const char *where)
{
    uint32_t a[2];
    if (!s_let_go_held || disp_loading(disp_holder())) return;
    s_let_go_held = 0;
    a[0] = DISP_GROUP;
    a[1] = 0;
    guest_call_cdecl(sub_00025740_gen, DISP_LET_GO_RA, 2, a);
    fprintf(stderr, "[SKIP] t=%.2f the game's letting go of its display set, held while it loaded, done "
            "now (%s)\n", jsrf_now(), where);
}
/* sub_00025740(group, word): the holder lets a group go (table 0x1EC288).
 * cdecl: it ends in a plain `ret`, and its callers take the group and word
 * off. A letting go held back returns as the game's does, its return
 * address taken off: without it, the flow's step that called it (0x0007C020,
 * 0x0007C800, 0x0007D120, 0x0007D290) went back with the stack 4 bytes low
 * and its esi popped from the word's slot (known bug 87). */
void sub_00025740(void)
{
    if (GARG(1) == DISP_GROUP && disp_loading(disp_holder())) {
        if (!s_let_go_held)
            fprintf(stderr, "[SKIP] t=%.2f the game lets its display set go while it loads for the prompt's "
                    "A (from %08X): held until it's loaded\n", jsrf_now(), GARG(0));
        s_let_go_held = 1;
        g_esp += 4;   /* its `ret` */
        return;
    }
    sub_00025740_gen();
}
/* The display set's A (JSRF_SKIP_A_MODEL's model, 0x69 by default) and its
 * animation, asking the game for the set where it isn't loaded, as its
 * flow asks. A_THERE when there; A_WAITING while the set is asked for, up
 * to A_WAIT seconds into this showing; A_NONE when there is none to have. */
static int prompt_a_find(double now, uint32_t *model, uint32_t *anim)
{
    const uint32_t holder = disp_holder();
    const uint32_t set = holder ? MEM32(holder + DISP_SET) : 0;
    const uint32_t models = set ? MEM32(set + DISP_MODELS) : 0;
    const uint32_t anims = set ? MEM32(set + DISP_ANIMS) : 0;
    static int s_no_model;
    if (s_want_model == 0xFFFFFFFFu) {
        s_want_model = prompt_env("JSRF_SKIP_A_MODEL", A_MODEL);
        s_want_size = getenv("JSRF_SKIP_A_SIZE") ? (float)atof(getenv("JSRF_SKIP_A_SIZE")) : 1.0f;
        if (!(s_want_size > 0.0f)) s_want_size = 1.0f;
    }
    *model = models && s_want_model < 0x400u ? MEM32(models + s_want_model * 4u) : 0;
    *anim = anims ? MEM32(anims + A_ANIM * 4u) : 0;
    if (*model) {
        if (s_a_asked >= 0.0)
            fprintf(stderr, "[SKIP] t=%.2f the game's A: its display set loaded %.2f s after the ask (set "
                    "%08X)\n", now, now - s_a_asked, set);
        s_a_since = s_a_asked = -1.0;
        return A_THERE;
    }
    if (!holder || set) {
        if (!s_no_model++)
            fprintf(stderr, "[SKIP] t=%.2f the game's A not drawn: no model %X in the display set (holder "
                    "%08X, set %08X): the font's cap instead\n", now, s_want_model, holder, set);
        return A_NONE;
    }
    /* No set: the flow's ask (sub_000256A0 answers 0 while the holder is
     * serving another), each frame until it's there. */
    if (s_a_since < 0.0) {
        s_a_since = now;
        s_a_gave_up = 0;
    }
    if (!MEM32(holder + DISP_BUSY) && !s_let_go_held) {
        uint32_t a[2];
        a[0] = DISP_GROUP;
        a[1] = 0;
        if (guest_call_cdecl(sub_000256A0, DISP_ASK_RA, 2, a) && s_a_asked < 0.0) {
            s_a_asked = now;
            fprintf(stderr, "[SKIP] t=%.2f the game's A: its display set isn't loaded (holder %08X): asked "
                    "the game for it, as its flow asks\n", now, holder);
        }
    }
    if (now - s_a_since < A_WAIT) return A_WAITING;
    if (!s_a_gave_up) {
        s_a_gave_up = 1;
        fprintf(stderr, "[SKIP] t=%.2f the game's A not drawn: its display set not loaded %.1f s on (holder "
                "%08X, %s): the font's cap instead\n", now, now - s_a_since, holder,
                MEM32(holder + DISP_BUSY) ? "serving a command" : s_a_asked >= 0.0 ? "asked" : "not asked");
    }
    return A_NONE;
}
/* The game's A, model and anim from prompt_a_find, its middle at cx, cy
 * on the 640 x 480, at the prompt's size. 1 when drawn. */
static int prompt_a_draw(uint32_t model, uint32_t anim, float cx, float cy, float size, double now)
{
    const uint32_t dev = MEM32(GAME_DEVICE);
    uint32_t a[4], rect, zfunc, zwrite;
    uint32_t angle, near_z, far_z, offset, view[4];
    float focal, tall, depth, x, y;
    int ok;
    if (!dev) {
        if (!s_a_logged++)
            fprintf(stderr, "[SKIP] t=%.2f the game's A not drawn: no device: the font's cap instead\n", now);
        return 0;
    }
    /* As the device is now, to put back. */
    angle = MEM32(dev + DEV_ANGLE);
    near_z = MEM32(dev + DEV_NEAR);
    far_z = MEM32(dev + DEV_FAR);
    offset = MEM32(dev + DEV_OFFSET);
    view[0] = MEM32(VIEW_NOW);
    view[1] = MEM32(VIEW_NOW + 4u);
    view[2] = MEM32(VIEW_NOW + 8u);
    view[3] = MEM32(VIEW_NOW + 12u);
    zfunc = MEM32(RS_ZFUNC);
    zwrite = MEM32(RS_ZWRITE);
    /* The box's view of it. */
    g_esp -= 16u;
    rect = g_esp;
    MEM32(rect) = 0; MEM32(rect + 4u) = 0; MEM32(rect + 8u) = 640u; MEM32(rect + 12u) = 480u;
    a[0] = dev; a[1] = rect;
    ok = prompt_device(dev, DEV_VIEWPORT, BOX_VIEWPORT_RA, 2, a);
    a[1] = BOX_ANGLE; a[2] = f32_bits(BOX_NEAR); a[3] = f32_bits(BOX_FAR);
    ok = ok && prompt_device(dev, DEV_PROJECTION, BOX_PROJECTION_RA, 4, a);
    a[1] = f32_bits(BOX_OFFSET);
    ok = ok && prompt_device(dev, DEV_DEPTH_OFFSET, BOX_OFFSET_RA, 2, a);
    a[1] = Z_ALWAYS;
    ok = ok && prompt_device(dev, DEV_Z_FUNC, BOX_OFFSET_RA, 2, a);
    a[1] = 0;
    ok = ok && prompt_device(dev, DEV_Z_WRITE, BOX_OFFSET_RA, 2, a);
    focal = bits_f32(MEM32(dev + DEV_FOCAL));
    tall = bits_f32(MEM32(dev + DEV_TALL));
    if (!(tall > 0.5f && tall < 2.0f)) tall = 1.0f;
    depth = A_DEPTH / (CUTSCENE_SKIP_SCALE * size * s_want_size);
    if (ok && focal > 0.0f) {
        x = (cx - 320.0f) * depth / focal;
        y = (240.0f - cy) * depth / (focal * tall);
        guest_call_cdecl(sub_001BAA00, BOX_PUSH_RA, 0, NULL);
        a[0] = f32_bits(x); a[1] = f32_bits(y); a[2] = f32_bits(depth);
        guest_call_cdecl(sub_001BAA60, BOX_MOVE_RA, 3, a);
        {
            const uint32_t sv_ecx = g_ecx;
            g_ecx = model;
            a[0] = anim; a[1] = f32_bits(s_a_frame);
            guest_call_cdecl(sub_00047690, BOX_DRAW_RA, 2, a);
            g_ecx = sv_ecx;
        }
        guest_call_cdecl(sub_001BAA50, BOX_POP_RA, 0, NULL);
        if (!s_a_logged++)
            fprintf(stderr, "[SKIP] t=%.2f the game's A drawn: model %X at %08X, animation %08X (%d frames), "
                    "its middle at %.1f, %.1f, %.0f deep (focal %.1f, height %.3f)\n", now, s_want_model, model,
                    anim, anim ? (int)(int16_t)MEM16(anim + 2u) : 0, cx, cy, depth, focal, tall);
    } else if (!s_a_logged++) {
        fprintf(stderr, "[SKIP] t=%.2f the game's A not drawn: the device's calls aren't the game's code\n", now);
    }
    /* Put back. */
    a[0] = dev;
    a[1] = zwrite;
    prompt_device(dev, DEV_Z_WRITE, BOX_ZERO_RA, 2, a);
    a[1] = zfunc;
    prompt_device(dev, DEV_Z_FUNC, BOX_ZERO_RA, 2, a);
    a[1] = offset;
    prompt_device(dev, DEV_DEPTH_OFFSET, BOX_ZERO_RA, 2, a);
    MEM32(rect) = view[0]; MEM32(rect + 4u) = view[1];
    MEM32(rect + 8u) = view[0] + view[2]; MEM32(rect + 12u) = view[1] + view[3];
    a[1] = rect;
    prompt_device(dev, DEV_VIEWPORT, BOX_VIEWPORT_RA, 2, a);
    a[1] = angle; a[2] = near_z; a[3] = far_z;
    prompt_device(dev, DEV_PROJECTION, BOX_PROJECTION_RA, 4, a);
    g_esp += 16u;
    /* The next frame's step of its animation. */
    if (anim) {
        const int frames = (int)(int16_t)MEM16(anim + 2u);
        s_a_frame += 1.0f;
        if (s_a_frame >= (float)frames - 1.0f) s_a_frame = 0.0f;
    }
    return ok && focal > 0.0f;
}
static void prompt_draw(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    CutsceneSkipPlace place[CUTSCENE_SKIP_PIECES];
    float alpha, size;
    uint32_t esp = g_esp, text, gm, font, a_model, a_anim;
    int marked, k, font_cap, a_state;
    const double now = jsrf_now();
    static double try_at = -2.0;
    /* JSRF_SKIP_PROMPT_AT=t: the prompt tried once at t seconds, with no
     * scene (cutscene_skip_try), so a test run sees it drawn. */
    if (try_at == -2.0) {
        const char *e = getenv("JSRF_SKIP_PROMPT_AT");
        try_at = e && *e ? atof(e) : -1.0;
    }
    if (try_at >= 0.0 && now >= try_at) {
        try_at = -1.0;
        cutscene_skip_try(now);
    }
    disp_let_go_held("at the frame's end");
    if (!cutscene_skip_prompt(now, &alpha, &size)) {
        s_a_since = -1.0;
        return;
    }
    gm = MEM32(GAME_MANAGER);
    font = gm ? MEM32(gm + GM_ACTORS + FONT_ACTOR * 4u) : 0;
    if (!font) {
        if (!s_prompt_no_font++)
            fprintf(stderr, "[SKIP] t=%.2f the prompt not drawn: the game manager has no font\n", now);
        return;
    }
    if (!s_prompt_logged++)
        fprintf(stderr, "[SKIP] t=%.2f the prompt drawn at the frame's end, with the font at %08X\n",
                now, font);
    cutscene_skip_place(size, s_prompt_width, place);
    marked = wide_hud_begin(WIDE_HUD_SKIP);
    /* The game's A in the cap's place, while the prompt is half in or more
     * (its set asked for from the first frame, where it isn't loaded, and
     * the place left empty while it loads); the font's cap and A, fading
     * with the words, where the game has no A to draw. */
    a_state = prompt_a_find(now, &a_model, &a_anim);
    font_cap = a_state == A_NONE;
    if (a_state == A_THERE && alpha >= 0.5f)
        font_cap = !prompt_a_draw(a_model, a_anim,
                                  place[CUTSCENE_SKIP_CAP].x + s_prompt_width[CUTSCENE_SKIP_CAP] * size * 0.5f,
                                  CUTSCENE_SKIP_MIDDLE, size, now);
    g_esp -= PROMPT_SCRATCH;
    text = g_esp;
    for (k = 0; k < CUTSCENE_SKIP_PIECES; k++) {
        const float f[5] = { place[k].x, place[k].y, FONT_SPACING, place[k].scale, place[k].scale };
        const uint32_t colour = cutscene_skip_colour[k];
        uint32_t a[7];
        float pen;
        if ((k == CUTSCENE_SKIP_CAP || k == CUTSCENE_SKIP_A) && !font_cap) continue;
        snprintf((char *)m + text, PROMPT_SCRATCH, "%s", cutscene_skip_text[k]);
        memcpy(&a[0], &f[0], 4);
        memcpy(&a[1], &f[1], 4);
        memcpy(&a[2], &f[2], 4);
        a[3] = ((uint32_t)(alpha * (float)(colour >> 24)) << 24) | (colour & 0x00FFFFFFu);
        memcpy(&a[4], &f[3], 4);
        memcpy(&a[5], &f[4], 4);
        a[6] = text;
        guest_call_cdecl(sub_0003D610, PROMPT_FONT_RA, 7, a);
        memcpy(&pen, m + font + FONT_PEN_X, sizeof pen);
        if (pen > place[k].x && size > 0.0f) s_prompt_width[k] = (pen - place[k].x) / size;
    }
    wide_hud_end(WIDE_HUD_SKIP, marked);
    g_esp = esp;
}
/* ---- random graffiti (random_graffiti.h) ------------------------------------
 * The graffiti sheet's builder (resource 0x19) starts at sub_0002D240 (ecx =
 * the holder, one argument: the build's mode), and asks the progress object
 * (ecx = 0x1EFFB0) which design fills a row's place for a size with
 * sub_0003A820(row, size, set), and whether that choice is one of the
 * player's own with sub_0003A870(row, size, set). Developer > Experimental >
 * Random graffiti answers the builder's calls from the designs it draws: for
 * the player's row in a build of the game's own sheet, and for every row in
 * a build of another set's, whose sheet is all the player's. Every other
 * call, the garage's menu's among them, runs the game's function.
 *
 * The game builds the sheet at Load Game and around the garage only. For new
 * designs at each area's load, the mission flow's step that asks for the
 * sheet's textures (sub_0004AC00, ecx = the flow: state 5 of its table at
 * 0x1F9888) is held while the feature lets the sheet and its textures go and
 * asks the holder for the build, with the game's own calls, as its scene
 * task does on the way to the garage (0x0004F143 to 0x0004F190); the step
 * then finds no textures and asks for the new ones, as it does after a
 * garage visit.
 *
 * For a design for each spot the step is held through a build for each set
 * drawn, the game's own sheet's last. The builder has one sheet in memory at
 * a time (the megabyte at [0x230DBC]: 1024 x 1024, DXT) and writes it to its
 * file (sub_0002D8D0, sub_0002D950: the overrides are known bug 24's, above).
 * Where the build is one of the other sets', the sheet is first made a
 * texture of its own (rg_sheet), with the game's own call for making a
 * sheet's texture, sub_0006E660(ecx = the texture manager; which of the two,
 * the sheet's bytes): it takes the id from a table of the two (0x20CC40),
 * which holds the feature's id for the length of the call. The game lets the
 * sheet's two textures go in sub_0002D040 (resource 0x1A's letting go, ecx =
 * the holder, one argument), and the others go there too, by the manager's
 * own sub_0006E7B0(id). A spot's draw is told which to use in sub_00042EE0's
 * override, above.
 *
 * A graffiti soul's design is marked as got by sub_00039E80(design) (ecx =
 * the progress object), which the soul's own end calls (sub_00042A30, the
 * call at 0x00042A67) as the pickup's animation ends. After it the sheets
 * are built again in play, from the holder's update (sub_00025310, ecx = the
 * holder: a step of the command it serves, if any), which is where the
 * holder is found free: a build is asked for there and the update's own
 * function run until the build is done, so that the holder is never busy
 * when the game's flows next ask it (24 of their 40 asks don't look whether
 * the ask was taken). Each sheet is made a texture again in its place by the
 * same call, sub_0006E660, which makes the texture in the place the id has
 * (the device's own call lets the place's old texture go first).
 *
 * Nothing is wrong in these functions, and nothing in the recompiler, the
 * runtime or the device models is being made up for: the recompiled code is
 * the game's own, and runs whenever the feature has no answer. This is a
 * feature in the game's own logic: the builder's two questions are where a
 * row's design is decided, the flow's step is where the game itself loads a
 * newly built sheet, the builder's write is where a sheet is whole, and the
 * textures' letting go is where the game is done with them, so that is where
 * each is hooked (docs/project-rules.md, Where code goes 6). */
#define RG_FLOW_FILE      0x1040u      /* the flow's mission's file */
#define RG_FILE_SHEETS    0x1Cu        /* its word the flow asks for the sheet's textures with */
#define RG_HOLDER_STAGES  0x1C4u       /* the holder's loaded stages, a list (sub_000360B0 walks it) */
#define RG_LOADED_RA      0x0004AC14u  /* the flow's own "are the textures loaded" call's return */
#define RG_LET_GO_RA      0x0004F14Cu  /* the scene task's own letting go's return (its call at 0x0004F147) */
#define RG_ASK_RA         0x0004F195u  /* ... and its ask's (its call at 0x0004F190) */
#define RG_BUILT_SHEET    0x00230DBCu  /* -> the sheet the builder is on */
#define RG_SHEET_IDS      0x0020CC40u  /* the two ids sub_0006E660 makes a sheet's texture under */
#define RG_MAKE_RA        0x0002D10Du  /* the game's own making of the first sheet's texture's return */
#define RG_MAKE_RA_XL     0x0002D1A9u  /* ... and of the second's */
#define RG_FREE_RA        0x0002D059u  /* the game's own letting go of a sheet's texture's return */
#define RG_DEVICE_TEXTURES 0x00264F68u /* -> the device's textures, a pointer for each place */
#define RG_SOUL_RA        0x00042A6Cu  /* the soul's end's marking of its design's return */
#define RG_UPDATE_RA      0x00025310u  /* for the holder's update run from here: its own address, as the game
                                        * calls it by its table (0x001C4F6C) and has no call to copy */
/* The game's texture memory: kernel.h's XBOX_CONTIG_SIZE less its
 * XBOX_GPU_INSTANCE_DEFAULT, which the runtime keeps off the top. */
#define RG_TEXTURE_MEMORY ((64u << 20) - (128u << 10))
extern uint32_t xbox_ContiguousAllocatedBytes(void);    /* how far into it anything was ever handed out */
extern void sub_00025310_gen(void);
extern void sub_0002D040_gen(void);
extern void sub_0002D240_gen(void);
extern void sub_00039E80_gen(void);
extern void sub_0003A820_gen(void);
extern void sub_0003A870_gen(void);
extern void sub_0004AC00_gen(void);
extern void sub_0006E660(void);
extern void sub_0006E7B0(void);
/* The builder's start: its mode says whose sheet it builds. */
void sub_0002D240(void)
{
    random_graffiti_build(GARG(1));
    sub_0002D240_gen();
}
/* Which design: the drawn one, to the builder. In another set's sheet a
 * size with none unlocked has the garage's design in every row: the game's
 * own answer for the player's row. */
void sub_0003A820(void)
{
    int design = random_graffiti_design((const uint8_t *)g_xbox_mem_offset, GARG(0), GARG(1),
                                        GARG(2), GARG(3), pad_now());
    if (design == RG_AS_ROW_0) GARG(1) = 0;
    if (design < 0) { sub_0003A820_gen(); return; }
    g_eax = (uint32_t)design;
    g_esp += 16;        /* ret 0xC: the return address and the three arguments */
}
/* Whether the choice is one of the player's own: a drawn design never is. */
void sub_0003A870(void)
{
    int design = random_graffiti_design((const uint8_t *)g_xbox_mem_offset, GARG(0), GARG(1),
                                        GARG(2), GARG(3), pad_now());
    if (design == RG_AS_ROW_0) GARG(1) = 0;
    if (design < 0) { sub_0003A870_gen(); return; }
    g_eax = 0;
    g_esp += 16;        /* ret 0xC: the return address and the three arguments */
}
/* The texture manager lets a texture go, by its own call. */
static void rg_free(uint32_t id)
{
    const uint32_t sv_ecx = g_ecx, a[1] = { id };
    g_ecx = RG_TEXTURES;
    guest_call_cdecl(sub_0006E7B0, RG_FREE_RA, 1, a);
    g_ecx = sv_ecx;
}
/* The builder is about to write the sheet it has built: for a build that is
 * one of the other sets', a texture of its own is made of it first, and for
 * any build in play the sheet's texture is made again in its place. The
 * game's call takes a node of the manager's and a place without asking
 * whether there is one, so that is asked here; and it doesn't say whether the
 * device made the texture, so the device's own table is read after it, and
 * the place given back where it made none (but for the game's own two, which
 * are the game's to let go). */
static void rg_sheet(uint32_t sheet)
{
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t id = random_graffiti_sheet(m, sheet);
    uint32_t bytes, sv_ecx, was, a[2];
    int place;

    if (!id) return;
    bytes = adx_word(RG_BUILT_SHEET);
    if (bytes < ADX_GUEST_LOW || bytes > ADX_GUEST_RAM - RG_SHEET_BYTES) {
        random_graffiti_made(id, RG_NO_SHEET, pad_now());
        return;
    }
    if (random_graffiti_texture(m, id) < 0 && !random_graffiti_places(m)) {
        random_graffiti_made(id, RG_NO_PLACE, pad_now());
        return;
    }
    sv_ecx = g_ecx;
    was = MEM32(RG_SHEET_IDS + sheet * 4u);
    MEM32(RG_SHEET_IDS + sheet * 4u) = id;
    a[0] = sheet;
    a[1] = bytes;
    g_ecx = RG_TEXTURES;
    guest_call_cdecl(sub_0006E660, sheet ? RG_MAKE_RA_XL : RG_MAKE_RA, 2, a);
    g_ecx = sv_ecx;
    MEM32(RG_SHEET_IDS + sheet * 4u) = was;
    place = random_graffiti_texture(m, id);
    if (place >= 0 && ((uint32_t)place >= RG_TM_PLACE_COUNT
                       || !adx_word(adx_word(RG_DEVICE_TEXTURES) + (uint32_t)place * 4u))) {
        if (id >= RG_OWN_TEXTURE) rg_free(id);
        place = RG_NOT_MADE;
    }
    random_graffiti_made(id, place < 0 ? RG_NOT_MADE : place, pad_now());
}
/* The sheet's two textures let go: the other sets' go with them. */
void sub_0002D040(void)
{
    sub_0002D040_gen();
    if (random_graffiti_on()) {
        uint32_t n;
        for (n = RG_SHEETS; n < RG_SETS * RG_SHEETS; n++)
            if (random_graffiti_texture((const uint8_t *)g_xbox_mem_offset, RG_OWN_TEXTURE + n) >= 0)
                rg_free(RG_OWN_TEXTURE + n);
        random_graffiti_let_go();
    }
}
/* The holder, as an area's load asks it: with the game's own calls. */
static int rg_busy(void)
{
    const uint32_t holder = disp_holder();
    return holder && MEM32(holder + DISP_BUSY);
}
static void rg_let_go(uint32_t resource, uint32_t word)
{
    const uint32_t a[2] = { resource, word };
    guest_call_cdecl(sub_00025740, RG_LET_GO_RA, 2, a);
}
static int rg_ask(uint32_t resource, uint32_t word)
{
    const uint32_t a[2] = { resource, word };
    return guest_call_cdecl(sub_000256A0, RG_ASK_RA, 2, a) != 0;
}
static uint32_t rg_unused(void)
{
    const uint32_t used = xbox_ContiguousAllocatedBytes();
    return used < RG_TEXTURE_MEMORY ? RG_TEXTURE_MEMORY - used : 0;
}
static void rg_step(void)
{
    const uint32_t sv_ecx = g_ecx;
    g_ecx = disp_holder();
    guest_call_cdecl(sub_00025310_gen, RG_UPDATE_RA, 0, NULL);
    g_ecx = sv_ecx;
}
static const RANDOM_GRAFFITI_GAME k_rg_game = { rg_busy, rg_let_go, rg_ask, rg_unused, rg_step };
/* A graffiti soul's design marked as got: by the soul's own end, and not by
 * the game's loops that mark many at once (0x0011ABAC and on). */
void sub_00039E80(void)
{
    const uint32_t from = GARG(0), design = GARG(1);
    sub_00039E80_gen();
    if (from == RG_SOUL_RA) random_graffiti_soul(design, pad_now());
}
/* The holder's update: after it, a soul's builds in play. */
void sub_00025310(void)
{
    const uint32_t holder = g_ecx;
    sub_00025310_gen();
    if (random_graffiti_on() && holder && holder == disp_holder())
        random_graffiti_play(&k_rg_game, (const uint8_t *)g_xbox_mem_offset, pad_now());
}
/* The flow's step that asks for the sheet's textures. */
void sub_0004AC00(void)
{
    const uint32_t flow = g_ecx, holder = disp_holder();
    const uint32_t file = MEM32(flow + RG_FLOW_FILE);

    if (random_graffiti_on() && holder && file) {
        const uint32_t a[2] = { RG_SHEET_TEXTURES, MEM32(file + RG_FILE_SHEETS) };
        int loaded = guest_call_cdecl(sub_000256C0, RG_LOADED_RA, 2, a) != 0;
        if (!random_graffiti_area(&k_rg_game, (const uint8_t *)g_xbox_mem_offset, loaded,
                                  MEM32(holder + RG_HOLDER_STAGES) != 0, pad_now())) {
            g_esp += 4;     /* ret: the step is held this pass */
            return;
        }
    }
    sub_0004AC00_gen();
}

/* ---- The game's lines naming the A or B button (prompt_text.h) --------------
 * sub_0003D610, the font's call that draws at once (x, y, depth, colour,
 * x scale, y scale, text; cdecl), wrapped: a text that is one of the
 * game's lines naming the A or B button by its letter (prompt_text_match),
 * or the game's loaded text, a talk's or a mission's, naming a control
 * (prompt_text_find, known bug 81), in any style but Default (the game's
 * own art), is drawn in pieces
 * through the call's own body, each letter's place left for its picture, a
 * mark in the push buffer (prompt_text_draw); any other text, and every
 * text in Default, is the call's own. The font call that queues,
 * sub_0003D5D0, hands such a line here (its override, above), as do the
 * widescreen menus'. The pieces go below the stack the caller left, with
 * room under it for each piece's text; the pen the font leaves gives where
 * each piece ended, and the game's measure (sub_0003D750, its float read
 * off the x87 stack) a letter's width. Then the call returns to its caller
 * as its own `ret` would. The first time each line is drawn so, the log
 * says where its first picture went; for loaded text, the first time each
 * font call draws some (loaded_text_told). tests/prompt_text checks the table,
 * the matching and the pieces; how the lines look needs the game, the test
 * build's steps. */
#include "prompt_text.h"
#include "input_remap.h"           /* the layouts now, for what a name's picture shows */
extern void sub_0003D610_gen(void);
extern void sub_0003D750_gen(void);
#define TEXT_FONT_ARGS 7u
/* guest_call_cdecl, for a function returning a float on the x87 stack: the
 * float, the stack as it was. */
static float guest_call_float(void (*fn)(void), uint32_t ra, int nargs, const uint32_t *args)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t sv[8];
    int top = g_fp_top, i;
    float f;
    sv[0] = g_eax; sv[1] = g_ecx; sv[2] = g_edx; sv[3] = g_ebx;
    sv[4] = g_esp; sv[5] = g_ebp; sv[6] = g_esi; sv[7] = g_edi;
    for (i = nargs - 1; i >= 0; i--) { g_esp -= 4; *(uint32_t *)(m + g_esp) = args[i]; }
    g_esp -= 4; *(uint32_t *)(m + g_esp) = ra;
    fn();
    f = (float)g_fp_stack[g_fp_top & 7];
    g_eax = sv[0]; g_ecx = sv[1]; g_edx = sv[2]; g_ebx = sv[3];
    g_esp = sv[4]; g_ebp = sv[5]; g_esi = sv[6]; g_edi = sv[7];
    g_fp_top = top;
    return f;
}
/* A call's arguments and what its pieces need. */
typedef struct {
    uint32_t a[TEXT_FONT_ARGS], ra, font;
    int art, marks;
    float first[3];
} TextCall;
static int text_draw(void *ctx, uint32_t va, float x, float y, float *pen_x, float *pen_y)
{
    TextCall *t = (TextCall *)ctx;
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t a[TEXT_FONT_ARGS];
    memcpy(a, t->a, sizeof a);
    memcpy(&a[0], &x, sizeof x);
    memcpy(&a[1], &y, sizeof y);
    a[6] = va;
    guest_call_cdecl(sub_0003D610_gen, t->ra, (int)TEXT_FONT_ARGS, a);
    memcpy(pen_x, m + t->font + PROMPT_TEXT_PEN_X, sizeof *pen_x);
    memcpy(pen_y, m + t->font + PROMPT_TEXT_PEN_Y, sizeof *pen_y);
    return 1;
}
static float text_measure(void *ctx, uint32_t va, float scale)
{
    const TextCall *t = (const TextCall *)ctx;
    uint32_t a[2];
    memcpy(&a[0], &scale, sizeof scale);
    a[1] = va;
    return guest_call_float(sub_0003D750_gen, t->ra, 2, a);
}
static void text_mark(void *ctx, PromptTextPicture pic, float x, float y, float half)
{
    TextCall *t = (TextCall *)ctx;
    uint32_t words[NV2A_PROMPT_MARK_WORDS];
    const uint32_t device = MEM32(D3D_DEVICE);
    words[0] = nv2a_prompts_mark_alpha(nv2a_prompts_mark(pic.show, 0, pic.art), t->a[3] >> 24);
    memcpy(&words[1], &x, sizeof x);
    memcpy(&words[2], &y, sizeof y);
    memcpy(&words[3], &half, sizeof half);
    if (!t->marks++) {
        t->first[0] = x;
        t->first[1] = y;
        t->first[2] = half;
    }
    if (device) prompt_buttons_push((uint8_t *)g_xbox_mem_offset, device, words, d3d_make_space);
}
static PromptTextMatch s_text;        /* the line text_line_now found */
/* Whether the text at text is a line, in a style that draws it: one of the
 * table's, or the game's loaded text with names in it (more, if not 0,
 * where it goes on: prompt_text_find). *m then holds it, each name with the
 * pictures of what is on its control now. */
static int text_line_more(uint32_t text, uint32_t more, PromptTextMatch *m)
{
    const uint8_t *mem = (const uint8_t *)g_xbox_mem_offset;
    const int art = input_prompts_art_now();
    InputLayout pad, keys;
    if (art == PROMPT_ART_GAME) return 0;
    if (!prompt_text_match(prompt_text_lines, prompt_text_nlines, mem, ADX_GUEST_RAM, text, m)
     && !prompt_text_find(prompt_text_names, prompt_text_nnames, mem, ADX_GUEST_RAM, text, more, m))
        return 0;
    input_remap_layouts(&pad, &keys);
    prompt_text_show(m, art, &pad, &keys);
    return 1;
}
static int text_line(uint32_t text, PromptTextMatch *m)
{
    return text_line_more(text, 0u, m);
}
/* The talk window (class 0x1D07B0, made by the talk display's manager,
 * actor 0x1165) types a page of a talk out: each frame sub_000F8360 copies
 * the next character of its page (its pages at +0x50, 0x114 bytes each, the
 * one it types at +0x15C, how far into it at +0x16C) to its own text at
 * +0x54, a colour code over whole and the codes that play a sound or wait
 * acted on, not copied; and its draw, sub_000F8130, hands that text as
 * typed so far to the font's queuing call, by one of two calls. For those
 * calls the rest of the page is where the text goes on (prompt_text_find),
 * so a name's picture comes in as the typing reaches it (known bug 81).
 * Taken only while the window is the class, its page is one of its pages,
 * and the text's last byte is the page's last one read: otherwise 0, and a
 * name is drawn once it is typed whole. */
#define TALK_WINDOW_CLASS    0x001D07B0u /* its class: the first word of the window */
#define TALK_WINDOW_DRAW_AT  0x000F8181u /* its draw's font call's return, at the window's own x and y */
#define TALK_WINDOW_DRAW_ROW 0x000F81AAu /*   the other's, at its page's y */
#define TALK_WINDOW_PAGES    0x50u       /* its pages */
#define TALK_WINDOW_TEXT     0x54u       /* its text, as typed so far */
#define TALK_WINDOW_NPAGES   0x158u      /* how many pages */
#define TALK_WINDOW_PAGE     0x15Cu      /* the page it types */
#define TALK_WINDOW_READ     0x16Cu      /* how far into it */
#define TALK_WINDOW_PAGE_SIZE 0x114u     /* a page's bytes */
#define TALK_WINDOW_PAGE_TEXT 0x100u     /* ... its text's room */
static uint32_t talk_window_rest(uint32_t ra, uint32_t text)
{
    const uint32_t w = text - TALK_WINDOW_TEXT;
    uint32_t pages, page, read, at, typed;
    if (ra != TALK_WINDOW_DRAW_AT && ra != TALK_WINDOW_DRAW_ROW) return 0;
    if (adx_word(w) != TALK_WINDOW_CLASS) return 0;
    pages = adx_word(w + TALK_WINDOW_PAGES);
    page = adx_word(w + TALK_WINDOW_PAGE);
    read = adx_word(w + TALK_WINDOW_READ);
    if (!pages || page >= adx_word(w + TALK_WINDOW_NPAGES) || !read || read >= TALK_WINDOW_PAGE_TEXT) return 0;
    at = pages + page * TALK_WINDOW_PAGE_SIZE;
    if (at < ADX_GUEST_LOW || at + TALK_WINDOW_PAGE_SIZE > ADX_GUEST_RAM) return 0;
    for (typed = 0; typed < TALK_WINDOW_PAGE_TEXT && MEM8(text + typed); typed++) {}
    if (!typed || MEM8(text + typed - 1u) != MEM8(at + read - 1u)) return 0;
    return at + read;
}
/* ... the font call's own text (its seventh argument), into s_text. */
static int text_line_now(void)
{
    const uint32_t text = MEM32(g_esp + 4u * TEXT_FONT_ARGS);
    return text_line_more(text, talk_window_rest(MEM32(g_esp), text), &s_text);
}
/* ---- The graffiti editor's ticker (widescreen_menus.h, prompt_text.h) --------
 * sub_001284F0, the editor's ticker's draw (this in ecx; one argument, the
 * editor's mode, ret 4): it sets a viewport round the bar of static at the
 * top of the screen (80 to 560 across by 38 to 68 down, through the game's
 * device), measures the mode's help line (sub_0003D750, returning to
 * EDITOR_TICKER_MEASURE), moves its scroll on a pixel and starts it again
 * past the line's width, draws the line at once at EDITOR_TICKER_START
 * less the scroll (sub_0003D610, returning to EDITOR_TICKER_FIRST) and,
 * while that copy's end is in the window, again a width on (returning to
 * EDITOR_TICKER_SECOND); then it takes the viewport off. Nothing in it is
 * wrong: it was laid out for the console's screen, and names the console's
 * controls. Three things are done round it.
 *
 * In a widened scene its text is drawn between marks of its own for the
 * whole width in its own viewport (WIDE_MENU_PIECE_EDITOR_TICKER), so every
 * page of the font is squeezed alike, in the window where the game put it,
 * over the bar drawn across (editor_part_draw, above; known bug 77). The
 * squeezed line no longer reaches the window's ends, so the first copy's
 * call draws every copy that shows in the wider window, from its right end
 * as the game's comes on from its own (widescreen_menus_editor_copies, by
 * the line's turns, widescreen_menus_editor_turns), each through the font
 * call again, and the game's second copy's call draws nothing.
 *
 * In any style but Default the line is drawn with its controls' pictures
 * (the font call's override, below), which are wider than the names they
 * stand for. The game spaces its copies and starts again by the width it
 * measured, so its measure's call is given the line's width with its
 * pictures (prompt_text_width): each copy follows the last with the gap
 * the game left, and the scroll starts again as the next copy reaches the
 * first's place.
 *
 * The game's viewport cuts its text at the window's ends, but not a
 * picture, which the renderer draws over the frame: the window is marked
 * for the pictures before the line (nv2a_prompts.h, NV2A_PROMPT_WINDOW_TAG)
 * and none after the ticker. */
#define EDITOR_TICKER_MEASURE 0x00128582u  /* the ticker's measure's call's return address */
#define EDITOR_TICKER_FIRST   0x00128600u  /* its first copy's font call's */
#define EDITOR_TICKER_SECOND  0x0012865Eu  /* its second copy's */
#define EDITOR_TICKER_PIECE   0x001284F0u  /* a piece's measure, as a call of the ticker's own */
#define EDITOR_TICKER_START   0x001DA394u  /* the float the line starts at: 78 */
static struct {
    int on;                    /* the ticker's draw is running */
    int wide;                  /* ... between its marks, in a widened scene */
    int windowed;              /* a window is marked for its pictures */
    int copying;               /* its copies are being drawn: the font call is the ticker's own again */
    int copied;                /* the first copy's call drew them all: */
    int copies, turns;         /* ... how many, and the line's turns then */
    float x, at;               /* ... the game's x, and the first copy's */
    float width;               /* the line's width as its measure's call gave it */
} s_ticker;
static void editor_ticker_window(float left, float right)
{
    uint32_t words[NV2A_PROMPT_MARK_WORDS] = { NV2A_PROMPT_WINDOW_TAG, 0u, 0u, 0u };
    const uint32_t device = MEM32(D3D_DEVICE);
    memcpy(&words[1], &left, sizeof left);
    memcpy(&words[2], &right, sizeof right);
    if (device && prompt_buttons_push((uint8_t *)g_xbox_mem_offset, device, words, d3d_make_space))
        s_ticker.windowed = right > left;
}
/* sub_0003D750: the font's measure (scale, text; cdecl, the width a float
 * on the x87 stack). The ticker's call of it, for a line drawn with
 * pictures, is given the width so drawn, as is any measure of the game's
 * loaded text with names in it (prompt_text_find), such as the talk
 * window's of a line it centres; any other is the game's own. The pieces
 * are measured below the stack the caller left, as the font call's
 * override draws them. */
void sub_0003D750(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    const uint32_t esp = g_esp, ra = MEM32(esp), text = MEM32(esp + 8u);
    const float scale = bits_f32(MEM32(esp + 4u));
    const int ticker = s_ticker.on && ra == EDITOR_TICKER_MEASURE;
    PromptTextCalls c = { text_draw, text_measure, text_mark, NULL };
    PromptTextMatch line;
    TextCall t;
    uint32_t left;
    float width;
    sub_0003D750_gen();
    if (ticker) s_ticker.width = (float)g_fp_stack[g_fp_top & 7];
    if (!text_line(text, &line) || (!ticker && line.line != PROMPT_TEXT_FOUND)) return;
    memset(&t, 0, sizeof t);
    t.ra = ticker ? EDITOR_TICKER_PIECE : ra;
    c.ctx = &t;
    left = g_esp;
    g_esp = (esp - (line.len + 16u)) & ~15u;
    width = prompt_text_width(&line, m, text, g_esp, scale, scale, &c);
    g_esp = left;
    if (!(width > 0.0f)) return;
    g_fp_stack[g_fp_top & 7] = (double)width;
    if (ticker) s_ticker.width = width;
}
/* The ticker's own font calls (above): 1 when the call was done here, its
 * `ret` with it; 0 for the font call's override to draw it. */
static int editor_ticker_text(void)
{
    const uint32_t esp = g_esp, ra = MEM32(esp);
    const float left = (float)MEM32(VIEW_NOW), right = left + (float)MEM32(VIEW_NOW + 8u);
    uint32_t a[TEXT_FONT_ARGS];
    float at[WIDE_EDITOR_COPIES];
    int i, n, turns;
    if (ra != EDITOR_TICKER_FIRST && ra != EDITOR_TICKER_SECOND) return 0;
    if (ra == EDITOR_TICKER_SECOND) {
        if (!s_ticker.copied) return 0;
        g_esp = esp + 4u;
        return 1;
    }
    /* The window the game's viewport is now, for the line's pictures. */
    if (right > left && text_line_now()) editor_ticker_window(left, right);
    /* The line's turns are followed in 4:3 too, for a change of shape (F10)
     * while it scrolls. */
    turns = widescreen_menus_editor_turns(bits_f32(MEM32(esp + 4u)), bits_f32(MEM32(EDITOR_TICKER_START)));
    if (!s_ticker.wide) return 0;
    for (i = 0; i < (int)TEXT_FONT_ARGS; i++) a[i] = MEM32(esp + 4u + 4u * (uint32_t)i);
    n = widescreen_menus_editor_copies(bits_f32(a[0]), s_ticker.width, left, right, nv2a_wide_squeeze(), turns,
                                       at);
    if (n < 0) return 0;
    s_ticker.copying = 1;
    for (i = 0; i < n; i++) {
        memcpy(&a[0], &at[i], sizeof at[i]);
        guest_call_cdecl(sub_0003D610, ra, (int)TEXT_FONT_ARGS, a);
    }
    s_ticker.copying = 0;
    s_ticker.copied = 1;
    s_ticker.copies = n;
    s_ticker.turns = turns;
    s_ticker.x = bits_f32(MEM32(esp + 4u));
    s_ticker.at = n ? at[0] : 0.0f;
    g_esp = esp + 4u;   /* the call's `ret` */
    return 1;
}
extern void sub_001284F0_gen(void);
void sub_001284F0(void)
{
    static unsigned char told[2];   /* a line coming on, and one that has turned */
    int marked = nv2a_wide_scene() && wide_menu_mark(WIDE_MENU_PIECE_EDITOR_TICKER, NV2A_WIDE_HUD_WHOLE);
    memset(&s_ticker, 0, sizeof s_ticker);
    s_ticker.on = 1;
    s_ticker.wide = marked;
    sub_001284F0_gen();
    if (s_ticker.windowed) editor_ticker_window(0.0f, 0.0f);
    if (s_ticker.copied && s_ticker.copies && !told[s_ticker.turns > 0]) {
        told[s_ticker.turns > 0] = 1;
        fprintf(stderr, "[WIDE] the graffiti editor's ticker: its line, %.1f wide, %s: the game's at %.1f, "
                "drawn in %d %s from %.1f in the widened window (squeeze %.3f)\n", (double)s_ticker.width,
                s_ticker.turns > 0 ? "after its scroll started again" : "coming on", (double)s_ticker.x,
                s_ticker.copies, s_ticker.copies == 1 ? "copy" : "copies", (double)s_ticker.at,
                (double)nv2a_wide_squeeze());
    }
    wide_menu_unmark(WIDE_MENU_PIECE_EDITOR_TICKER, marked);
    s_ticker.on = 0;
}
/* sub_0012A640: the graffiti editor's parts' draw (called by its draw's
 * sub_0012B180), which draws each of the holder's 128 models that is shown
 * through the models' draw: while it runs in a widened scene, that draw
 * knows the editor's parts (editor_part_draw). Nothing in it is wrong: the
 * editor was laid out for the console's screen. */
extern void sub_0012A640_gen(void);
void sub_0012A640(void)
{
    const uint32_t gm = MEM32(GAME_MANAGER);
    const uint32_t holder = gm ? MEM32(gm + GM_ACTORS + WIDE_EDITOR_HOLDER * 4u) : 0;
    int began = widescreen_menus_editor_begin((const uint8_t *)g_xbox_mem_offset, ADX_GUEST_RAM, holder,
                                              nv2a_wide_scene());
    sub_0012A640_gen();
    widescreen_menus_editor_end(began);
}
/* The log's line for the game's loaded text drawn with pictures
 * (prompt_text_find): once for each font call that draws it, by its
 * return address, with what its names were the first time. */
#define LOADED_TEXT_CALLS 32
static void loaded_text_told(uint32_t ra, const PromptTextMatch *s, int marks, int art, const float f[4],
                             const float first[3])
{
    static uint32_t told[LOADED_TEXT_CALLS];
    static const char *const what[PROMPT_TEXT_WHATS] = {
        [PROMPT_TEXT_A] = "A", [PROMPT_TEXT_B] = "B", [PROMPT_TEXT_X] = "X", [PROMPT_TEXT_Y] = "Y",
        [PROMPT_TEXT_LTRIGGER] = "left trigger", [PROMPT_TEXT_RTRIGGER] = "right trigger",
        [PROMPT_TEXT_DPAD] = "D-pad", [PROMPT_TEXT_DPAD_ACROSS] = "D-pad across",
        [PROMPT_TEXT_DPAD_UPDOWN] = "D-pad up and down", [PROMPT_TEXT_LSTICK] = "left stick",
        [PROMPT_TEXT_LSTICK_ACROSS] = "left stick across", [PROMPT_TEXT_LSTICK_UPDOWN] = "left stick up and down",
    };
    char names[160];
    int i, k, len = 0;
    for (i = 0; i < LOADED_TEXT_CALLS && told[i] && told[i] != ra; i++) {}
    if (i == LOADED_TEXT_CALLS || told[i]) return;
    told[i] = ra;
    names[0] = 0;
    for (k = 0; k < s->n && len < (int)sizeof names; k++)
        len += snprintf(names + len, sizeof names - (size_t)len, "%s%s at byte %u", k ? ", " : "",
                        s->name[k].what < PROMPT_TEXT_WHATS && what[s->name[k].what] ? what[s->name[k].what] : "?",
                        (unsigned)s->name[k].at);
    fprintf(stderr, "[INPUT] button prompts: the game's loaded text, by the font call returning to %08X%s, "
            "drawn with %d picture%s in art %d (%s; %u bytes), from %.1f, %.1f at scale %.2f: the first at "
            "%.1f, %.1f, %.1f pixels across\n", (unsigned)ra,
            ra == TALK_WINDOW_DRAW_AT || ra == TALK_WINDOW_DRAW_ROW ? " (the talk window)" : "", marks,
            marks == 1 ? "" : "s", art, names, (unsigned)s->len, (double)f[0], (double)f[1], (double)f[3],
            (double)first[0], (double)first[1], (double)(2.0f * first[2]));
}
void sub_0003D610(void)
{
    static unsigned char told[256];
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    const uint32_t esp = g_esp, gm = MEM32(GAME_MANAGER);
    PromptTextCalls c = { text_draw, text_measure, text_mark, NULL };
    TextCall t;
    float f[4];
    uint32_t i, text;
    int line;
    if (s_ticker.on && !s_ticker.copying && editor_ticker_text()) return;
    if (!text_line_now()) {
        sub_0003D610_gen();
        return;
    }
    memset(&t, 0, sizeof t);
    t.font = gm ? MEM32(gm + GM_ACTORS + FONT_ACTOR * 4u) : 0;
    if (!t.font) {
        sub_0003D610_gen();
        return;
    }
    t.art = input_prompts_art_now();
    t.ra = MEM32(esp);
    for (i = 0; i < TEXT_FONT_ARGS; i++) t.a[i] = MEM32(esp + 4u + 4u * i);
    memcpy(&f[0], &t.a[0], sizeof f[0]);
    memcpy(&f[1], &t.a[1], sizeof f[1]);
    memcpy(&f[2], &t.a[4], sizeof f[2]);
    memcpy(&f[3], &t.a[5], sizeof f[3]);
    text = t.a[6];
    line = s_text.line;
    c.ctx = &t;
    g_esp = (esp - (s_text.len + 16u)) & ~15u;
    prompt_text_draw(&s_text, m, text, g_esp, f[0], f[1], f[2], f[3], &c);
    if (line >= 0 && line < (int)sizeof told && !told[line] && t.marks) {
        told[line] = 1;
        fprintf(stderr, "[INPUT] button prompts: the game's line at %08X drawn with %d picture%s in art %d, "
                "from %.1f, %.1f at scale %.2f: the first at %.1f, %.1f, %.1f pixels across\n",
                (unsigned)prompt_text_lines[line].va, t.marks, t.marks == 1 ? "" : "s", t.art, (double)f[0],
                (double)f[1], (double)f[3], (double)t.first[0], (double)t.first[1],
                (double)(2.0f * t.first[2]));
    }
    if (line == PROMPT_TEXT_FOUND && t.marks) loaded_text_told(t.ra, &s_text, t.marks, t.art, f, t.first);
    g_esp = esp + 4u;   /* its `ret`: the caller takes its arguments off */
}
static void photo_draw(void);
void sub_000131F0(void)
{
    uint32_t sv_eax, sv_ecx, sv_edx;
    photo_draw();               /* the game's draw, as in play while photo mode holds */
    sv_eax = g_eax;
    sv_ecx = g_ecx;
    sv_edx = g_edx;
    prompt_draw();
    g_eax = sv_eax;
    g_ecx = sv_ecx;
    g_edx = sv_edx;
}
/* The plain menus (widescreen_menus.h): each one's draw, its class's slot
 * +0xC with this in ecx, marks its frame for the side fill before the menu
 * draws, and its moving rectangles are found while it draws, until
 * widescreen_menus_fill_end. Each is the only draw of its class, entered
 * only through it. pass is the draw's one argument, for the graffiti menu. */
static int wide_menu_fill(int plain, uint32_t pass)
{
    return widescreen_menus_fill((uint8_t *)g_xbox_mem_offset, MEM32(D3D_DEVICE), plain, g_ecx,
                                 pass, nv2a_wide_scene(), d3d_make_space);
}
/* sub_0011DD10: the save and load menu's draw (class 0x1D8CD0). */
extern void sub_0011DD10_gen(void);
void sub_0011DD10(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_SAVE, 0);
    sub_0011DD10_gen();
    widescreen_menus_fill_end(began);
}
/* sub_0011C070: Roboy's garage menu's draw (class 0x1D8638). */
extern void sub_0011C070_gen(void);
void sub_0011C070(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_GARAGE, 0);
    sub_0011C070_gen();
    widescreen_menus_fill_end(began);
}
/* sub_0011FEB0: the garage's system menu's draw (class 0x1D9090). */
extern void sub_0011FEB0_gen(void);
void sub_0011FEB0(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_SYSTEM, 0);
    sub_0011FEB0_gen();
    widescreen_menus_fill_end(began);
}
/* sub_0011CF10: the garage's options menu's draw (class 0x1D8898). */
extern void sub_0011CF10_gen(void);
void sub_0011CF10(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_OPTIONS, 0);
    sub_0011CF10_gen();
    widescreen_menus_fill_end(began);
}
/* sub_0011D740: the garage's practice menu's draw (class 0x1D8C90). */
extern void sub_0011D740_gen(void);
void sub_0011D740(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_PRACTICE, 0);
    sub_0011D740_gen();
    widescreen_menus_fill_end(began);
}
/* sub_0011F7D0: the garage's music menu's draw (class 0x1D8F88). */
extern void sub_0011F7D0_gen(void);
void sub_0011F7D0(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_MUSIC, 0);
    sub_0011F7D0_gen();
    widescreen_menus_fill_end(began);
}
/* sub_0011C750: the garage's test run menu's draw (class 0x1D8798). */
extern void sub_0011C750_gen(void);
void sub_0011C750(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_TEST_RUN, 0);
    sub_0011C750_gen();
    widescreen_menus_fill_end(began);
}
/* sub_0011A490: the test run's course screen's draw (class 0x1D7C20). */
extern void sub_0011A490_gen(void);
void sub_0011A490(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_COURSE, 0);
    sub_0011A490_gen();
    widescreen_menus_fill_end(began);
}
/* sub_0011AC50: the garage's character select's draw (class 0x1D7D00). */
extern void sub_0011AC50_gen(void);
void sub_0011AC50(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_CHARACTERS, 0);
    sub_0011AC50_gen();
    widescreen_menus_fill_end(began);
}
/* sub_00127080: the garage's graffiti menu's draw (class 0x1DA300, slot
 * +0x24; one argument, the pass, ret 4), filled on its pass 9 in its
 * states 4 and 8. */
extern void sub_00127080_gen(void);
void sub_00127080(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_GRAFFITI, MEM32(g_esp + 4));
    sub_00127080_gen();
    widescreen_menus_fill_end(began);
}
/* sub_00066550: the loading screen's draw (class 0x1CB0A8; one argument,
 * ret 4), with the blue moving rectangles, filled while it's shown. */
extern void sub_00066550_gen(void);
void sub_00066550(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_LOADING, 0);
    sub_00066550_gen();
    widescreen_menus_fill_end(began);
}
/* sub_00071100: VS mode's setup screen's draw, its object's (class
 * 0x1CCA80, slot +0xC; one argument, the pass, ret 4). The screen's layout,
 * one full-screen picture, is fitted into the 4:3 screen with its panels'
 * shades, and its teal carried out to the screen's edges. Its draw (through
 * sub_0006FE40 or sub_00070060, which it jumps to) is flat quads and text
 * only: its 3D models, the Back among them, are its objects', each drawn
 * on its own from the game's list of objects. */
extern void sub_00071100_gen(void);
void sub_00071100(void)
{
    int began = wide_menu_fill(WIDE_PLAIN_VS, 0);
    sub_00071100_gen();
    widescreen_menus_fill_end(began);
}
/* The result screens (widescreen_menus.h): the choice's draw, its class's
 * slot +0xC (this in ecx; one argument, the pass, ret 4), marked whole for
 * the right edge on the passes it draws on. The race time's is marked the
 * same way, only in the result screen's modes, with its places moved
 * (below). Their banner is a sign (sub_000FC370). */
static void wide_result(int which, void (*gen)(void))
{
    unsigned piece = widescreen_menus_results[which].piece;
    int began = widescreen_menus_result((uint8_t *)g_xbox_mem_offset, which, g_ecx,
                                        MEM32(g_esp + 4), nv2a_wide_scene());
    int marked = began && wide_menu_mark(piece, NV2A_WIDE_HUD_RIGHT);
    gen();
    wide_menu_unmark(piece, marked);
    widescreen_menus_result_end(which, began);
}
/* sub_0010F670: the result screen's choice's draw (class 0x1D24F0): the
 * return to title and restart icons, the grey static bar and its label. */
extern void sub_0010F670_gen(void);
void sub_0010F670(void)
{
    wide_result(WIDE_RESULT_CHOICE, sub_0010F670_gen);
}
/* sub_0010EC10: the race time's draw (class 0x1D23F0), the result screen's
 * under the banner (modes 4 and 6) and the race HUD's timer (modes 2 and 3),
 * which isn't moved. The result screen's is marked as the choice is and,
 * for its draw only, its mode's places moved right so its right end comes
 * under the banner's (widescreen_menus_result_time_begin), then put back as
 * they were. */
extern void sub_0010EC10_gen(void);
void sub_0010EC10(void)
{
    uint8_t *mem = (uint8_t *)g_xbox_mem_offset;
    uint32_t self = g_ecx;
    WideTimePlaces places;
    int began = widescreen_menus_result(mem, WIDE_RESULT_TIME, self, MEM32(g_esp + 4),
                                        nv2a_wide_scene());
    int marked = began && wide_menu_mark(WIDE_MENU_PIECE_TIME, NV2A_WIDE_HUD_RIGHT);
    int moved = began && widescreen_menus_result_time_begin(mem, self, &places);
    sub_0010EC10_gen();
    widescreen_menus_result_time_end(mem, &places, moved);
    wide_menu_unmark(WIDE_MENU_PIECE_TIME, marked);
    widescreen_menus_result_end(WIDE_RESULT_TIME, began);
}
/* sub_0007DA30: the title's draw (class 0x1CCEF8; one argument, ret 4),
 * by its state (+0x44): the scene is widened from its first frame (the
 * flow's scene, above; with JSRF_WIDE_TITLE=0, from its first "please press
 * start" on). Widened, in every state but the
 * city's, the bars it draws go across, between marks
 * (widescreen_menus_title_bars): its opening parks its tall bars just past
 * the 4:3 edges and sweeps them in, so stretched across they come in from
 * the screen's edges; the rest is drawn as it is, and the city, another
 * object's, fills the sides. */
extern void sub_0007DA30_gen(void);
void sub_0007DA30(void)
{
    int bars;
    widescreen_menus_title((uint8_t *)g_xbox_mem_offset, g_ecx, jsrf_now());
    bars = widescreen_menus_title_bars((uint8_t *)g_xbox_mem_offset, g_ecx, nv2a_wide_scene())
        && wide_menu_mark(WIDE_MENU_PIECE_BARS, NV2A_WIDE_HUD_BARS);
    sub_0007DA30_gen();
    wide_menu_unmark(WIDE_MENU_PIECE_BARS, bars);
}
/* sub_0007D8B0: the title's state 6 draw (this in ecx, ret), the city with
 * the top-left "please press start", its pack's model 1, which goes to the
 * left edge, and the logo at the bottom right, its pack's model 0, which
 * goes to the right edge (widescreen_menus_part). */
extern void sub_0007D8B0_gen(void);
void sub_0007D8B0(void)
{
    int began = widescreen_menus_title_press_begin((uint8_t *)g_xbox_mem_offset, g_ecx,
                                                   nv2a_wide_scene());
    sub_0007D8B0_gen();
    widescreen_menus_title_press_end(began);
}


/* ---- the camera -------------------------------------------------------------
 * Object 0x4C (class 0x1CD518) is the camera; sub_000A5070 its per-frame
 * update. It picks a mode from its target character's state (+0xE58, with
 * overrides from +0xF18, +0xEC8, +0xE38/+0xE3C), switches mode through the
 * table at 0x215534 when that changes (+0x208 holds the current one), runs
 * the mode's update from 0x215570, and returns early while +0x1E0 is set.
 * Eye at +0x44, target at +0x5C. JSRF_CAM_LOG=1 prints every change of the
 * mode inputs and the eye position once a second. */

/* What the free camera's hooks below use: the camera's fields, the fields of
 * the character it follows, the modes, and the follow camera's wall test as
 * sub_000A5B60 makes it. */
#define CAM_FOLLOWS        0x024u      /* the character it follows (sub_00011BD0) */
#define CAM_WALL_ARG       0x0BCu      /* what the wall test takes first */
#define CAM_SMOOTH_EYE     0x1E4u      /* the smoothed eye */
#define CAM_MODE           0x208u      /* the mode it is in */
#define CAM_EYE            0x20Cu      /* the working eye: the smoothed one, lifted */
#define CAM_TARGET         0x218u      /* the working target */
#define CHR_GRAFFITI_ON    0xE38u      /* the graffiti camera's other half: both make mode 13 (0xA50DF) */
#define CHR_GRAFFITI_CAM   0xE3Cu      /* the graffiti camera, asked for (sub_0008D280) */
#define CHR_FIXED_SHOT     0xEC8u      /* a fixed shot asked for: mode 10 (0xA50D0) */
#define CHR_FIXED_KIND     0xECCu      /* its kind: 0 an eye at a point, 1 one along a path, ... (sub_000A4640) */
#define CHR_EXIT_SHOT      0xF04u      /* the level exit's camera point set (sub_000A76E0, 0xA78F7) */
#define CHR_CAM_MODE       0xE58u      /* the mode it asks the camera for */
#define CHR_CAM_BEHIND     0xE5Cu      /* the camera back behind it, asked for */
#define CAM_MODE_FOLLOW    3u          /* follow: what a look goes back to (0x8B2CF) */
#define CAM_MODE_LOOK      9u          /* look at whoever the player talks to */
#define CAM_WORLD          0x0022FCE8u /* the collision world, the wall test's this */
#define CAM_WALL_MASK      0x33B7u     /* what the wall test stops at */
#define CAM_WALL_ANY       0xFFFFFFFFu /* its last argument, as sub_000A5B60 passes it */
#define CAM_WALL_LINE_RET  0x000A602Au /* where sub_000A5B60's line test returns to */
#define CAM_WALL_NEAR_RET  0x000A604Cu /* where its nearest-hit call returns to */
#define CAM_WALL_LOCALS    0x50u       /* sub_000A5B60's locals: the test's scratch */
#define CAM_WALL_HITS      0x40u       /* the hit record in them */
#define CAM_WALL_POINT_1   0x2Cu       /* the point in a hit from the record's first list */
#define CAM_WALL_POINT_2   0x50u       /* and from its second */
#define CAM_WALL_ENDS      0x18u       /* a line's two ends, kept past those locals */
#define CAM_SKATER_WHOLE   0x001C4BA4u /* the game's 15.0: a skater whose camera's eye is nearer is faded (sub_00081860) */
#define CAM_SKATER_HIDDEN  0x001CC624u /* the game's 3.0: nearer than this, it isn't drawn at all */
#define CAM_SKATER_FADE    0x001CD098u /* the game's 1/12: the fade a unit, from CAM_SKATER_HIDDEN out */
#define CAM_ID             0x008u      /* what the frame's camera is known by (GAME_FRAME_CAM) */
#define CAM_DISTANCE       0x230u      /* the eye's distance from the target (sub_000A28B0) */
#define DEV_CLASS          0x001E0F00u /* the graphics device the camera is set on (s_cam_dev) */
#define DEV_NEAR           0x40u       /* its near plane: where the view starts, ahead of the eye */
#define DEV_TAN_HALF       0x54u       /* tan of half its view's width (SetCamera, sub_00153FE0) */
#define VIEW_TALL          0.75        /* the frame's height over the width that angle spans */
#define CHR_DRIVER         0x024u      /* the player manager that built it (sub_00011BD0) */
#define DRV_CLASS          0x001CD570u /* the player manager's class (sub_000A6980) */
#define DRV_PAD            0x8ACu      /* its pad: a port 0 to 3, DRV_MAIN_PAD, or DRV_NO_PAD (sub_000A8370) */
#define DRV_MAIN_PAD       4u          /* the pad the game was started with: the main player's */
#define GAME_MAIN_PORT     0x251F20u   /* that pad's port, which the game looks up each time (sub_0004EBC0) */
#define DRV_CONTROL        0x8B4u      /* what moves its skater (sub_000A76E0) */
#define DRV_NO_PAD         0xFFFFFFFFu
#define DRV_PAD_PLAYS      0u          /* the pad moves it */
#define DRV_PAD_RECORDED   2u          /* the pad moves it, and the run is recorded */
#define DRV_PLAYBACK       1u          /* a recorded run moves it */
#define CAM_VIEW_EYE       0x044u      /* the eye the view is built from (sub_000A28B0 puts it there) */
#define CAM_VIEW_TARGET    0x05Cu      /* and what it looks at: up on the stick is from one to the other */
#define CHR_TURN           0xCC0u      /* its turn on the ground, 0x10000 a turn: +z to (sin, cos) (sub_000A32B0) */
#define CHR_POSITION       0xCA4u      /* where it is: x, y and z (JSRF_TELEPORT puts the player there) */
#define CAM_HEADING        0x228u      /* the heading the controls follow, 0x10000 a turn (sub_000A28B0 sets it) */
#define CAM_HELD_HEADING   0x234u      /* the heading kept when a mode is switched to (sub_000A58E0) */
#define CAM_HOLDS          0x238u      /* set while the controls follow that kept heading */
#define CAM_HEADING_RET    0x000A2937u /* where sub_000A28B0's call of the game's heading (sub_0004A7A0) returns */

/* Whether the camera follows a skater the pad moves, which the free camera
 * steers. Every skater with a pad builds a camera that follows it
 * (sub_00084200), and a scene that follows another skater around (Beat round
 * Dogenzaka Hill, Combo showing his line) is shown through that skater's own.
 * What moves a skater is its player manager's control state: the manager
 * reads the pad in states 0 and 2 only (sub_000A76E0, its table at 0xA8298);
 * 1 plays back a recorded run and 3 to 13 are scripted, and none of those
 * sees the L trigger either. The game's own test for the player's skater is
 * the same (sub_0004EBC0). say: log each camera's answer when it changes. */
static int cam_pad_steered(const uint8_t *m, uint32_t cam, int say)
{
    static struct { uint32_t cam, ctl, pad; int ok; } said[4];
    static int next_slot;
    const uint32_t lim = 64u << 20;
    uint32_t who = *(const uint32_t *)(m + cam + CAM_FOLLOWS), drv = 0, ctl = 0, pad = DRV_NO_PAD;
    int ok = 0, i;
    if (who >= 0x10000u && who + 0x1000u < lim)
        drv = *(const uint32_t *)(m + who + CHR_DRIVER);
    if (drv >= 0x10000u && drv + 0x1000u < lim && *(const uint32_t *)(m + drv) == DRV_CLASS) {
        pad = *(const uint32_t *)(m + drv + DRV_PAD);
        ctl = *(const uint32_t *)(m + drv + DRV_CONTROL);
        ok = pad != DRV_NO_PAD && (ctl == DRV_PAD_PLAYS || ctl == DRV_PAD_RECORDED);
    } else
        drv = 0;
    if (!say) return ok;
    for (i = 0; i < 4 && said[i].cam != cam; i++) {}
    if (i < 4 && said[i].ok == ok && said[i].ctl == ctl && said[i].pad == pad) return ok;
    if (i == 4) { i = next_slot; next_slot = (next_slot + 1) % 4; }
    said[i].cam = cam; said[i].ok = ok; said[i].ctl = ctl; said[i].pad = pad;
    if (ok)
        fprintf(stderr, "[FREECAM] t=%.2f camera %08X: its skater is moved by the pad (port %d, control %u); the free camera's\n",
                pad_now(), cam, (int)pad, ctl);
    else if (!drv)
        fprintf(stderr, "[FREECAM] t=%.2f camera %08X: it follows %08X, which no player manager moves; the game's camera\n",
                pad_now(), cam, who);
    else
        fprintf(stderr, "[FREECAM] t=%.2f camera %08X: its skater is moved by %s, not a pad (control %u, pad %d); the game's camera\n",
                pad_now(), cam, pad == DRV_NO_PAD && (ctl == DRV_PAD_PLAYS || ctl == DRV_PAD_RECORDED)
                ? "nothing" : ctl == DRV_PLAYBACK ? "a recording" : "a script", ctl, (int)pad);
    return ok;
}

/* Half pipe cam unticked (camscene_rise): the camera whose update last
 * read the half pipe's jump as not asked for this frame, for its
 * post-update (sub_000A6100), which the task manager's second walk runs
 * after; 0: none. */
static uint32_t s_rise_cam;

/* For each pad port, the player manager its keys steer for, its skater's
 * camera and the skater, as the camera update last saw them (sub_000A5070),
 * and when: for the manager's update (sub_000A76E0). */
static struct { uint32_t drv, cam, who; double at; } s_keys_cam[4];

/* Input > Behaviour > Camera's log (port/camera_scenes.c): on a camera the
 * pad steers, each of the game's shots the ticks read as it starts, and
 * whether it plays, and the level exit's shot, which no tick touches; once
 * each time one starts, for each camera. kind: the fixed shot's (+0xECC). */
static void camscene_log(uint32_t cam, unsigned shots, unsigned hidden, int exit_shot,
                         uint32_t kind)
{
    static struct { uint32_t cam; unsigned shots; int exit_shot; } said[4];
    static int next_slot;
    static const char *const what[CAMSCENES] = {     /* CAMSCENE_ */
        "the graffiti shot (Graffiti cam)", "the half pipe's shot (Half pipe cam)",
        "a fixed shot of the stage's (Loop cam)" };
    unsigned started;
    int i;
    for (i = 0; i < 4 && said[i].cam != cam; i++) {}
    if (i == 4) {
        i = next_slot++ % 4;
        said[i].cam = cam;
        said[i].shots = 0;
        said[i].exit_shot = 0;
    }
    started = shots & ~said[i].shots;
    if (exit_shot && !said[i].exit_shot)
        fprintf(stderr, "[CAMSCENE] t=%.2f camera %08X: the level exit's shot, as the game has it (no tick's)\n",
                pad_now(), cam);
    for (int k = 0; k < CAMSCENES; k++)
        if (started & CAMSCENE_BIT(k)) {
            char how[24] = "";
            if (k == CAMSCENE_FIXED) snprintf(how, sizeof how, ", kind %u", kind);
            fprintf(stderr, "[CAMSCENE] t=%.2f camera %08X: %s%s, %s\n", pad_now(), cam, what[k], how,
                    hidden & CAMSCENE_BIT(k) ? "not played: its tick is off, so the camera follows"
                                             : "played as the game frames it");
        }
    said[i].shots = shots;
    said[i].exit_shot = exit_shot;
}

extern void sub_000A5070_gen(void);
void sub_000A5070(void)
{
    static int on = -1;
    static uint32_t last[10];
    static double next;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t cam = g_ecx, sprayed = 0, fixed = 0;
    uint32_t *gcam = NULL, *look = NULL, *hpipe = NULL, *fshot = NULL;
    if (on < 0) on = getenv("JSRF_CAM_LOG") != NULL;
    if (s_rise_cam == cam) s_rise_cam = 0;
    /* The camera's mode is chosen here (0xA50BB) from what the skater asks
     * for: the mode it asks for (+0xE58), unless a fixed shot is asked for
     * (+0xEC8: mode 10) or the graffiti camera (+0xE38 and +0xE3C: mode 13).
     *
     * Input > Behaviour > Camera (port/camera_scenes.c) ticks three of the
     * game's own shots; one whose tick is off is read as not asked for, and
     * the camera follows the skater as anywhere else:
     *
     * - Graffiti cam: mode 13, which frames the spot and snaps back behind
     *   the player after. The spray code (sub_0008D280) asks for it with
     *   +0xE3C, and this update reads 0 in its place.
     * - Half pipe cam: mode 6, which the half pipe's jump asks for
     *   (sub_000996A0) as the mode it asks for. This update reads 3, follow,
     *   in its place.
     * - Loop cam: mode 10 for a fixed shot of the stage's, which the
     *   skater's update asks for in the stage's camera areas, a loop's or
     *   any other, while the level exit's camera point (+0xF04) is not set
     *   (0x80F72). This update reads 0 in place of +0xEC8. The level exit's
     *   own shot is mode 10 too, which the skater's update asks for once
     *   the player manager has set that point (0x8085B, sub_000A76E0): no
     *   tick touches it (camscene_asked).
     *
     * And while the free camera (port/freecam.c) is on, the camera never
     * turns to whoever the player talks to. Talking is mode 9, the look: the
     * talk button, or someone nearby who asks for it, sets the mode the
     * player asks for (+0xE58) to 9 (0x8B32A, 0x8B2A5), and switching to it
     * (sub_000A5A00) puts the target 33 units ahead, toward them, and the
     * eye 66 away. This update reads 3 in its place: the follow mode a look
     * goes back to when it ends (0x8B2CF). Coming to it from 9, when freecam
     * is turned on during a look, moves neither eye nor target
     * (sub_000A4BD0), as the end of a look doesn't.
     *
     * Nothing this update calls reads or writes any of these again (the
     * skater's own code does, outside it, and ends a look once +0xE58 is no
     * longer 9, a half pipe's jump once it lands, and a fixed shot once it
     * leaves its area), so each is put back after. Only on the camera of a
     * skater the pad moves (cam_pad_steered): a scene's shot, or its look at
     * someone, stays the game's. */
    if (cam >= 0x10000u && cam + 0x240u < lim && cam_pad_steered(m, cam, 0)) {
        uint32_t who = ((const uint32_t *)(m + cam))[CAM_FOLLOWS / 4];
        if (who >= 0x10000u && who + 0x1000u < lim) {
            uint32_t *chr = (uint32_t *)(uintptr_t)(m + who);
            unsigned shots = camscene_asked(chr[CHR_CAM_MODE / 4], chr[CHR_FIXED_SHOT / 4],
                                            chr[CHR_EXIT_SHOT / 4],
                                            chr[CHR_GRAFFITI_ON / 4] && chr[CHR_GRAFFITI_CAM / 4]);
            unsigned ticks = camscene_ticks(), hidden;
            /* A recording's ticks in place of the player's while it plays
             * (port/playback.c), or one's made before the ticks. */
            playback_camscenes(&ticks);
            if (ticks == PLAYBACK_CAMSCENES_BEFORE) ticks = camscene_ticks_before(freecam_on());
            hidden = camscene_hidden(shots, ticks);
            camscene_log(cam, shots, hidden, chr[CHR_FIXED_SHOT / 4] && chr[CHR_EXIT_SHOT / 4],
                         chr[CHR_FIXED_KIND / 4]);
            /* The free camera keeps its view as a shot that plays starts,
             * and eases back to it after (port/freecam.h). */
            freecam_shot(cam, (shots & ~hidden) != 0);
            if (hidden & CAMSCENE_BIT(CAMSCENE_GRAFFITI)) {
                gcam = &chr[CHR_GRAFFITI_CAM / 4];
                sprayed = *gcam;
                *gcam = 0;
            }
            if (hidden & CAMSCENE_BIT(CAMSCENE_HALF_PIPE)) {
                hpipe = &chr[CHR_CAM_MODE / 4];
                *hpipe = CAMSCENE_MODE_FOLLOW;
            }
            if (hpipe) s_rise_cam = cam;
            if (hidden & CAMSCENE_BIT(CAMSCENE_FIXED)) {
                fshot = &chr[CHR_FIXED_SHOT / 4];
                fixed = *fshot;
                *fshot = 0;
            }
            if (freecam_on() && chr[CHR_CAM_MODE / 4] == CAM_MODE_LOOK) {
                look = &chr[CHR_CAM_MODE / 4];
                *look = CAM_MODE_FOLLOW;
            }
        }
    }
    { static int looked;
      if (look && !looked)
          fprintf(stderr, "[FREECAM] t=%.2f talking: no look-at camera while freecam is on\n", pad_now());
      looked = look != NULL; }
    sub_000A5070_gen();
    if (gcam) *gcam = sprayed;
    if (hpipe) *hpipe = CAMSCENE_MODE_HALF_PIPE;
    if (fshot) *fshot = fixed;
    if (look) *look = CAM_MODE_LOOK;
    /* Keyboard moves (port/keyboard_move.c): the camera's look on the ground
     * and the skater's turn, for the pad's port, which the keys steer by. The
     * main player's manager names DRV_MAIN_PAD, not a port: its port is the
     * game's GAME_MAIN_PORT. And the manager, the camera and the skater, for
     * the manager's update (sub_000A76E0, below). The main player's skater,
     * and the camera's mode, for input playback's checks and idle moments
     * (port/playback.c). */
    if (cam >= 0x10000u && cam + 0x240u < lim && cam_pad_steered(m, cam, 0)) {
        const float *cf = (const float *)(m + cam);
        uint32_t who = ((const uint32_t *)(m + cam))[CAM_FOLLOWS / 4];
        uint32_t drv = *(const uint32_t *)(m + who + CHR_DRIVER);
        int port = kbmove_game_port(*(const uint32_t *)(m + drv + DRV_PAD),
                                    *(const uint32_t *)(m + GAME_MAIN_PORT));
        if (port >= 0) {
            kbmove_view((unsigned)port,
                        cf[CAM_VIEW_TARGET / 4] - cf[CAM_VIEW_EYE / 4],           /* x */
                        cf[CAM_VIEW_TARGET / 4 + 2] - cf[CAM_VIEW_EYE / 4 + 2],   /* z */
                        *(const uint32_t *)(m + who + CHR_TURN));
            s_keys_cam[port].drv = drv;
            s_keys_cam[port].cam = cam;
            s_keys_cam[port].who = who;
            s_keys_cam[port].at = pad_now();
            if ((uint32_t)port == *(const uint32_t *)(m + GAME_MAIN_PORT)) {
                const float look[2] = { cf[CAM_VIEW_TARGET / 4] - cf[CAM_VIEW_EYE / 4],
                                        cf[CAM_VIEW_TARGET / 4 + 2] - cf[CAM_VIEW_EYE / 4 + 2] };
                const float eye[3] = { cf[CAM_VIEW_EYE / 4], cf[CAM_VIEW_EYE / 4 + 1], cf[CAM_VIEW_EYE / 4 + 2] };
                /* The skater's object, for the trace's fingerprint, where all
                 * of it is in guest memory. */
                const uint8_t *skater = who >= 0x10000u && who + PLAYBACK_SKATER_BYTES < lim ? m + who : NULL;
                playback_view((float *)(uintptr_t)(m + who + CHR_POSITION),
                              (uint32_t *)(uintptr_t)(m + who + CHR_TURN),
                              ((const uint32_t *)(m + cam))[CAM_MODE / 4], look, eye, skater, game_random());
            }
        }
    }
    if (cam >= 0x10000u && cam + 0x240u < lim)
        s_cam_mode_now = ((const uint32_t *)(m + cam))[0x208 / 4];
    { static int hooks = -1;
      if (hooks < 0) hooks = (getenv("JSRF_CANS") || getenv("JSRF_BOOST_DUMP")) ? 1 : 0;
      if (hooks && cam >= 0x10000u && cam + 0x240u < lim) {
          uint32_t who = ((const uint32_t *)(m + cam))[0x24 / 4];
          if (who >= 0x10000u && who + 0x1000u < lim)
              test_hooks_step((uint8_t *)(uintptr_t)(m + who));
      } }
    if (cam >= 0x10000u && cam + 0x240u < lim) {
        uint32_t who = ((const uint32_t *)(m + cam))[0x24 / 4];
        if (who >= 0x10000u && who + 0x1000u < lim) {
            const float *chf = (const float *)(m + who);
            s_player_pos[0] = chf[0xCA4 / 4]; s_player_pos[1] = chf[0xCA8 / 4];
            s_player_pos[2] = chf[0xCAC / 4];
            s_player_va = who;
        }
    }
    save_menu_open_step();
    mission_at_step();
    find_proj_step();
    wide_reapply_step();
    if (cam >= 0x10000u && cam + 0x240u < lim) {
        uint32_t who = ((const uint32_t *)(m + cam))[0x24 / 4];
        if (who >= 0x10000u && who + 0x1000u < lim) {
            /* The teleport no longer needs an autopilot to be set as well:
             * it was only ever called from inside that branch. */
            teleport_step((float *)(uintptr_t)(m + who));
            if (s_auto_n != 0)
                auto_step((const float *)(m + cam), (const float *)(m + who));
        }
    }
    if (!on || cam < 0x10000u || cam + 0x240u >= lim) return;
    {
        const uint32_t *c = (const uint32_t *)(m + cam);
        const float *cf = (const float *)(m + cam);
        uint32_t tgt = c[0x24 / 4];          /* sub_00011BD0: the character it follows */
        uint32_t now[10];
        int i, changed = 0;
        now[0] = c[0x208 / 4]; now[1] = c[0x1E0 / 4]; now[2] = c[0x40 / 4];
        now[3] = c[0x98 / 4];  now[4] = c[0x1DC / 4];
        now[5] = now[6] = now[7] = now[8] = now[9] = 0;
        if (tgt >= 0x10000u && tgt + 0x1000u < lim) {
            const uint32_t *t = (const uint32_t *)(m + tgt);
            now[5] = t[0xE58 / 4]; now[6] = t[0xF18 / 4]; now[7] = t[0xEC8 / 4];
            now[8] = t[0xE38 / 4]; now[9] = t[0xE3C / 4];
        }
        for (i = 0; i < 10; i++) if (now[i] != last[i]) changed = 1;
        if (changed || jsrf_now() >= next) {
            next = jsrf_now() + 1.0;
            fprintf(stderr, "[CAM] t=%.2f flip %u cam %08X mode=%u +1E0=%u +40=%08X +98=%u +1DC=%u | char %08X E58=%u F18=%u EC8=%u E38=%u E3C=%u | eye (%.1f %.1f %.1f) tgt (%.1f %.1f %.1f)%s\n",
                    jsrf_now(), nv2a_gl_flips(), cam, now[0], now[1], now[2], now[3], now[4],
                    tgt, now[5], now[6], now[7], now[8], now[9],
                    cf[0x44 / 4], cf[0x48 / 4], cf[0x4C / 4], cf[0x5C / 4], cf[0x60 / 4], cf[0x64 / 4],
                    changed ? "  <- changed" : "");
            fflush(stderr);
            memcpy(last, now, sizeof last);
        }
    }
}

/* ---- the keys' way, at the player manager's update (port/keyboard_move.c)
 * sub_000A76E0 is the player manager's update (class DRV_CLASS, its
 * vtable's second entry). In its pad case (0x000A7940 to 0x000A7B16) it
 * reads the left stick's angle from the pad driver's table (PAD_TABLE, an
 * entry for each port and one for the main pad, which PAD_ENTRY_OF names for
 * each of the manager's pads), and turns it into the way the skater goes,
 * from the camera's heading (CAM_HEADING): within 60 degrees of straight up
 * it squares the angle (0x000A7ABF: a soft centre), and while the camera
 * keeps an old heading (CAM_HOLDS, after it changed mode) it goes the way it
 * had. Its camera is the one that follows its skater, found by its id
 * (sub_000128C0); the camera update has it (s_keys_cam). Those two reads,
 * at 0x000A79BD and 0x000A7A9C, are the only ones of the entry's angle.
 *
 * With Keyboard moves at Skater, the keys' stick (kbmove_pad, at the pad
 * read) pointed the skater's way from the camera's look, which the soft
 * centre then pulled towards the camera's heading each frame: W held while
 * the camera swung round turned the skater with it. Here the angle the
 * update reads is the one that sends the skater the keys' way, worked out
 * from the camera's heading and the skater's turn as the update finds them
 * (kbmove_angle), and the pad's own is put back after. The game's code is
 * right for a stick, and the pad driver's angle is the stick's: it is the
 * keys' way that the stick can't say exactly, so it is put in where the
 * game reads it, not made up for in the device model or the pad driver.
 *
 * The pad driver is a task of its own: it may write the entry while the
 * update runs. Then the update reads the pad's angle, the keys' stick, for
 * that frame, as before; and the pad's angle is put back only if the entry
 * still holds this one. */
#define PAD_ENTRY_OF       0x00215770u /* a manager's pad (DRV_PAD, 0 to DRV_MAIN_PAD): its entry in PAD_TABLE, a word each */
#define PAD_TABLE          0x00251DE4u /* the pad driver's table, an entry for each */
#define PAD_ENTRY_SIZE     0x40u
#define PAD_ENTRIES        5u          /* the four ports' and the main pad's */
#define PAD_ANGLE          0x30u       /* the left stick's angle in an entry, 0x10000 a turn */
#define KEYS_CAM_FRESH_S   0.1         /* a camera not updated for this long is not the one the update uses */

extern void sub_000A76E0_gen(void);
void sub_000A76E0(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t drv = g_ecx, pad = DRV_NO_PAD, entry, cam, who, angle = 0, *at = NULL;
    uint32_t mine = 0, theirs = 0;
    int port = -1;
    if (drv >= 0x10000u && drv + 0x1000u < lim && *(const uint32_t *)(m + drv) == DRV_CLASS) {
        pad = *(const uint32_t *)(m + drv + DRV_PAD);
        port = kbmove_game_port(pad, *(const uint32_t *)(m + GAME_MAIN_PORT));
    }
    if (port >= 0 && s_keys_cam[port].drv == drv && pad_now() - s_keys_cam[port].at < KEYS_CAM_FRESH_S) {
        cam = s_keys_cam[port].cam;
        who = s_keys_cam[port].who;
        entry = *(const uint32_t *)(m + PAD_ENTRY_OF + pad * 4u);
        /* The camera still follows the skater this manager moves, by the pad. */
        if (entry < PAD_ENTRIES && cam >= 0x10000u && cam + 0x240u < lim
            && who >= 0x10000u && who + 0x1000u < lim
            && *(const uint32_t *)(m + cam + CAM_FOLLOWS) == who
            && *(const uint32_t *)(m + who + CHR_DRIVER) == drv && cam_pad_steered(m, cam, 0)
            && kbmove_angle((unsigned)port, *(const uint32_t *)(m + who + CHR_TURN),
                            *(const uint32_t *)(m + cam + CAM_HEADING),
                            *(const uint32_t *)(m + cam + CAM_HOLDS) != 0,
                            *(const uint32_t *)(m + cam + CAM_HELD_HEADING), &angle)) {
            at = (uint32_t *)(uintptr_t)(m + PAD_TABLE + entry * PAD_ENTRY_SIZE + PAD_ANGLE);
            theirs = __atomic_load_n(at, __ATOMIC_RELAXED);
            mine = angle;
            __atomic_store_n(at, mine, __ATOMIC_RELAXED);
        }
    }
    g_ecx = drv;
    sub_000A76E0_gen();
    if (at)
        __atomic_compare_exchange_n(at, &mine, theirs, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

/* ---- the follow camera's walls, and the free camera (port/camera_walls.c,
 * port/freecam.c) -------------------------------------------------------
 * sub_000A6100 is the follow camera's post-update: the one the table at
 * 0x2155AC gives modes 3, 4, 5, 9 and 13, which jumps to sub_000A5B60. When
 * it returns, the working eye (+0x20C) is final for the frame; sub_000A28B0
 * then copies it to the camera's eye (+0x44) and takes the heading the
 * controls follow from it (+0x228). sub_000A5B60 works the working eye out
 * again each frame from its smoothed eye (+0x1E4), and nothing in the
 * follow modes reads it back: only that heading is kept, when a mode is
 * switched to (+0x234, sub_000A58E0 and the like) or the camera is put back
 * behind the player. freecam_orbit moves it around the working target
 * (+0x218), at the distance of the game's smoothed eye (+0x1E4), and the
 * game's own wall test runs again on it. The game's working eye is that
 * smoothed eye raised by the camera's lift (+0xE4, added at 0xA5F56), which
 * freecam leaves out (port/freecam.c says why). Only the camera of a skater
 * the pad moves is freecam's (cam_pad_steered): a scene shown through
 * another skater's camera stays the game's.
 *
 * sub_000A28B0 is the camera's post-update, which the task manager's second
 * walk runs (sub_00011D00) and its pause walks do not, though the update
 * walk may still run the camera's update. freecam_ran hears of each as it
 * ends, so a camera that was not post-updated keeps the free camera's view,
 * and one post-updated in a mode of the game's own starts it again. The
 * sound manager's update, which runs freecam_frame (sub_00116E30), comes
 * between the camera's update and its post-update: in a log of 22 November
 * 2026, with freecam_frame taking up the update's word on a shot
 * (freecam_shot), all 25 shots started the free camera again from the
 * game's view, the word "no shot" taken before the follow camera ran. So
 * the post-update takes it up, from the same frame. It also notes the view
 * it built (+0x44 and +0x5C, freecam_shown), for the free camera's glide
 * into the graffiti shot and out of the game's shots to start from: the
 * follow camera's hook moves the working eye and target for it, which
 * sub_000A5B60 works out again each frame, in every mode it runs, the
 * graffiti shot's too, from the smoothed ones (+0x1E4 and +0x1F0), so
 * nothing the game keeps from frame to frame changes.
 *
 * The L trigger (sub_00080340), and the player's set-up (sub_000836F0), ask
 * for the camera back behind the player: +0xE5C on the one it follows
 * (sub_00011BD0, +0x24). sub_000A5B60 does it -- its smoothed eye snaps
 * behind -- and clears the request; when it was cleared here, freecam starts
 * again from that eye.
 *
 * The wall test is sub_000A5B60's: a line against the world (sub_00018260)
 * and, when it is blocked, the nearest hit (sub_000194B0), which returns 1
 * when that hit is from the record's second list. The scratch is laid out as
 * that function's locals, with the result's pointer at +0, and the line's
 * ends past them; each call returns to where that function's own does.
 *
 * The game tests one line, from the target to the eye, and puts its eye on
 * the nearest hit: once its smoothed eye's move since the last frame crosses
 * a surface, and then each frame until the line is clear (+0xE0, 0xA5F88).
 * freecam's eye goes where the stick puts it, and it tests every frame. But
 * the view starts at the near plane, ahead of the eye, as wide as the
 * projection's angle: both are the camera device's, which the title sets
 * once, at start (DEV_NEAR, DEV_TAN_HALF; the angle spans 640 of the frame's
 * 480 lines, and widescreen widens it as wide_proj does). The camera's own
 * angle (+0x74) is not that one: it reads 0 in play, and only a scene's
 * camera sets it. So a surface that runs along the view -- a half pipe's
 * curve, a bump in the ground -- can cut into it while the line stays clear,
 * or meet it where the eye sits on the surface, as it does wherever the
 * eye's line meets a wall; widescreen widens the view, and so how far it
 * reaches across. camwall_clear (port/camera_walls.c) tests lines across the
 * view where it starts, and when a surface cuts across it, moves the eye
 * across, away from it, or else brings it in to where lines from the target
 * to the view's corners are clear. It keeps the eye from coming nearer the
 * player than where the view starts in front of them, nor from coming nearer
 * than the game draws the player whole (CAM_SKATER_WHOLE, the distance the
 * skaters' draw, sub_00081860, reads from the camera's +0x230 once
 * sub_000A28B0 has worked it out): nearer, the game fades your skater out,
 * and under CAM_SKATER_HIDDEN doesn't draw it. The free camera, turned toward
 * a wall by the player, would come that near; the game's own camera trails
 * the player and lifts when they drop, and comes that near where a wall
 * brings its view in. That hold is Input > Behaviour > Camera's No skater
 * fade (camwall_least), off by default, under Default cam fix, as the
 * game's own eye has it only while that is on; an eye the game itself put
 * nearer stays there. Off, either eye comes as near as its view allows. For
 * the free camera's eye it tests the eye's line first. The game's own eye, every
 * camera's but the one the free camera places, gets the same test of its
 * view, and of its line only where what the line meets hides the whole of
 * where the view starts: a half pipe's curve, or a wall that came between,
 * the eye behind it, which the game's own test misses, as it runs only when
 * the smoothed eye's move crosses a surface. A pole or a rail the game lets
 * pass between the player and the eye still passes, and the camera moves only
 * then or while a surface cuts across its view. It changes only the working
 * eye, so the game's smoothing and following go on as before. The graffiti
 * camera (mode 13) is the one mode it leaves be (camscene_shot,
 * port/camera_scenes.c): the game aims that shot at the spot being sprayed,
 * so the surface is right by the target, and the test would bring the eye
 * in to a first-person view.
 * And Default cam fix (camwall_on, its row in Input > Behaviour) turns it
 * off for the game's own eye altogether; the free camera's is kept out of
 * walls either way.
 * Each camera keeps its own state (camwall_for): split screen's players',
 * and a scene's shown through another skater's camera. This supplies the
 * line test, the game's own. */
extern void sub_00018260(void);
extern void sub_000194B0(void);
/* How much of the line from a to b is open, 0 to 1: up to the nearest hit. */
static double camwall_game_line(uint32_t cam, uint32_t scr, const double a[3], const double b[3])
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    float *ends = (float *)(m + scr + CAM_WALL_LOCALS);
    uint32_t args[6], r, res;
    double len2 = 0, along = 0;
    int k;
    for (k = 0; k < 3; k++) { ends[k] = (float)a[k]; ends[3 + k] = (float)b[k]; }
    args[0] = cam + CAM_WALL_ARG; args[1] = scr + CAM_WALL_LOCALS;
    args[2] = scr + CAM_WALL_LOCALS + 3 * sizeof(float);
    args[3] = scr + CAM_WALL_HITS; args[4] = CAM_WALL_MASK; args[5] = CAM_WALL_ANY;
    g_ecx = CAM_WORLD;
    if (!guest_call_cdecl(sub_00018260, CAM_WALL_LINE_RET, 6, args)) return 1.0;
    *(uint32_t *)(m + scr) = 0;
    args[0] = scr + CAM_WALL_HITS; args[1] = scr;
    g_ecx = CAM_WORLD;
    r = guest_call_cdecl(sub_000194B0, CAM_WALL_NEAR_RET, 2, args);
    res = *(uint32_t *)(m + scr);
    if (res < 0x10000u || res + CAM_WALL_POINT_2 + 3 * sizeof(float) >= lim) {
        static int once;
        if (res && !once++) fprintf(stderr, "[CAMWALL] wall test gave %08X; no wall test\n", res);
        return 1.0;
    }
    {
        const float *hit = (const float *)(m + res + (r ? CAM_WALL_POINT_2 : CAM_WALL_POINT_1));
        for (k = 0; k < 3; k++) {
            len2 += (b[k] - a[k]) * (b[k] - a[k]);
            along += (hit[k] - a[k]) * (b[k] - a[k]);
        }
    }
    if (!(len2 > 0)) return 1.0;
    along /= len2;
    return along < 0 ? 0 : along > 1 ? 1 : along;
}
/* What placed each camera's eye this frame, for the skaters' log
 * (skater_drawn_log) and the skaters' and the areas' draws while the free
 * camera places it (sub_0006AC10, sub_0006A970): the camera's post-update
 * (sub_000A28B0) notes the game's own mode first, and the follow camera's
 * hook below, which it runs, notes what it did; with the free camera, also
 * the eye the game had put there, before the free camera placed its own.
 * A camera the game doesn't post-update (paused) keeps what it last had. */
enum { EYE_SCENE, EYE_GAME, EYE_DCAMFIX, EYE_FREECAM };
static struct { uint32_t cam; int by; float game_eye[3]; } s_eye_by[CAMWALL_CAMERAS];
static void cam_eye_note(uint32_t cam, int by, const float game_eye[3])
{
    static unsigned next;
    unsigned i;
    for (i = 0; i < CAMWALL_CAMERAS && s_eye_by[i].cam != cam; i++) {}
    if (i == CAMWALL_CAMERAS) i = next++ % CAMWALL_CAMERAS;    /* the one noted longest ago */
    s_eye_by[i].cam = cam;
    s_eye_by[i].by = by;
    if (game_eye) memcpy(s_eye_by[i].game_eye, game_eye, sizeof s_eye_by[i].game_eye);
}
/* What placed camera cam's eye (EYE_), or -1 for a camera not seen; game_eye,
 * when not NULL, set to the eye the game put there, for EYE_FREECAM. */
static int cam_eye_placed(uint32_t cam, const float **game_eye)
{
    unsigned i;
    for (i = 0; i < CAMWALL_CAMERAS; i++)
        if (s_eye_by[i].cam == cam) {
            if (game_eye) *game_eye = s_eye_by[i].game_eye;
            return s_eye_by[i].by;
        }
    return -1;
}
static const char *cam_eye_by(uint32_t cam)
{
    static const char *const by_text[] = {                  /* EYE_ */
        "the game's camera, in a mode of its own", "the game's camera, as the game put it",
        "the game's camera, kept out of walls (Default cam fix)", "the free camera" };
    int by = cam_eye_placed(cam, NULL);
    return by < 0 ? "a camera not seen" : by_text[by];
}
/* camwall_clear's line test: the scratch on the guest stack, for this camera. */
struct camwall_line_at { uint32_t cam, scr; };
static double camwall_line(void *ctx, const double a[3], const double b[3])
{
    const struct camwall_line_at *at = (const struct camwall_line_at *)ctx;
    return camwall_game_line(at->cam, at->scr, a, b);
}
/* camwall_for's key for the free camera's eye: no mode's, so the game's own
 * eye and the free camera's never share a camera's state. */
#define CAM_WALLS_FREECAM 0xFFFFFFFFu
/* The eye of camera cam after walls, w its state: the free camera's
 * (freecam 1), whose own line is tested too, or the game's own, where the
 * game's wall test put it. */
static void camera_walls(uint32_t cam, struct camwall *w, int freecam)
{
    static const char *const by_text[] = {           /* camera_walls.h's CAMWALL_ */
        "with nothing in the way", "by the eye's line", "by a corner of the view",
        "by the eye's line (the corners have no room)", "for a corner of the view",
        "as near as it goes (the eye's line meets a wall by the player)" };
    /* CAMWALL_NEAREST with No skater fade on, whose floor is the game's. */
    static const char by_whole[] =
        "held where the game draws the player whole (the eye's line meets a wall nearer)";
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    float *cf = (float *)(m + cam);
    const float *tgt = cf + CAM_TARGET / 4;
    float *eye = cf + CAM_EYE / 4;
    uint32_t sv_ecx = g_ecx;
    struct camwall_line_at at;
    double u[3], need[3], push[3], d, half, ahead, hw, hh, least, open, eased;
    int k, by;
    /* The camera device, as the widescreen re-set checks it: not set yet,
     * the view's size is not known, and only the eye's line is tested. */
    if (s_cam_dev < 0x10000u || s_cam_dev + DEV_TAN_HALF + sizeof(float) >= (64u << 20)
     || *(const uint32_t *)(m + s_cam_dev) != DEV_CLASS)
        half = ahead = 0;
    else {
        half = *(const float *)(m + s_cam_dev + DEV_TAN_HALF);
        ahead = *(const float *)(m + s_cam_dev + DEV_NEAR);
    }
    for (k = 0; k < 3; k++) u[k] = eye[k] - tgt[k];
    d = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (!(d > 0)) return;
    hw = ahead * half / (wide_proj() ? nv2a_wide_squeeze() : 1.0f);
    hh = ahead * half * VIEW_TALL;
    { static double said_w = -1, said_h = -1;
      if (hw != said_w || hh != said_h) {
          said_w = hw; said_h = hh;
          fprintf(stderr, "[CAMWALL] t=%.2f view: %.0f degrees across (4:3), starting %.2f ahead of the eye, %.2f by %.2f either side\n",
                  pad_now(), 2 * atan(half) * 180 / 3.14159265358979323846, ahead, hw, hh);
      } }
    if (!(hw >= 0 && hw < HUGE_VAL && hh >= 0 && hh < HUGE_VAL)) hw = hh = 0;
    /* The eye no nearer the player than the game draws them whole, with No
     * skater fade on (camwall_least): the game's own distance, which the
     * skaters' draw reads. Nearer, the game fades your skater out, and under
     * CAM_SKATER_HIDDEN doesn't draw it. The free camera's eye and the
     * game's alike, the game's only with Default cam fix on, as only then
     * does it come here (sub_000A6100). Off, either has the view's floor
     * alone, and the game fades the player where the eye comes that near, as
     * on the Xbox. */
    least = camwall_least(freecam, *(const float *)(m + CAM_SKATER_WHOLE));
    if (!RECOMP_CHECK(least >= 0 && least < HUGE_VAL, "the game's whole-draw distance reads %g", least))
        least = 0;
    g_esp -= CAM_WALL_LOCALS + CAM_WALL_ENDS;
    at.cam = cam;
    at.scr = g_esp;
    /* How far out it can be, and how far across there; then where it goes
     * this frame -- in at once, back out by degrees after a moment
     * (port/camera_walls.c) -- and, when that is short of it, its view there
     * tested too, with how far across it moves there; then how far across
     * this frame, the same way. */
    open = camwall_clear(tgt, eye, 1.0, freecam, least, ahead, hw, hh, camwall_line, &at, need,
                         &by);
    eased = camwall_share(w, open);
    if (eased < open) {
        double there = camwall_clear(tgt, eye, eased, freecam, least, ahead, hw, hh, camwall_line,
                                     &at, need, &by);
        if (there < eased) eased = camwall_share(w, there);
    }
    camwall_across(w, tgt, eye, eased, need, ahead, hw, hh, camwall_line, &at, push);
    g_ecx = sv_ecx;
    g_esp += CAM_WALL_LOCALS + CAM_WALL_ENDS;
    if (!(eased < 1) && push[0] == 0 && push[1] == 0 && push[2] == 0) return;
    for (k = 0; k < 3; k++) eye[k] = (float)(tgt[k] + u[k] * eased + push[k]);
    {
        static double next[2];
        if (pad_now() >= next[freecam]) {
            next[freecam] = pad_now() + 1.0;
            if (freecam)
                fprintf(stderr, "[FREECAM] t=%.2f wall: ", pad_now());
            else
                fprintf(stderr, "[CAMWALL] t=%.2f camera %08X mode %u: ", pad_now(), cam,
                        ((const uint32_t *)cf)[CAM_MODE / 4]);
            fprintf(stderr, "eye pulled in to %.0f%% of %.1f and %.1f across, %s, at (%.1f %.1f %.1f)\n",
                    eased * 100, d,
                    sqrt(push[0] * push[0] + push[1] * push[1] + push[2] * push[2]),
                    (eased < open || push[0] != need[0] || push[1] != need[1] || push[2] != need[2])
                        && by == CAMWALL_CLEAR ? "waiting or easing back out"
                    : by == CAMWALL_NEAREST && least > 0 ? by_whole : by_text[by],
                    eye[0], eye[1], eye[2]);
        }
    }
}
extern void sub_000A6100_gen(void);
void sub_000A6100(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    const uint32_t lim = 64u << 20;
    uint32_t cam = g_ecx, who = 0, asked = 0, mode;
    float *cf, eye[3], game_eye[3], seye[3];
    int placed = FREECAM_OFF, behind, glided = 0;
    struct camwall *w;
    if (cam >= 0x10000u && cam + 0x240u < lim) {
        who = *(uint32_t *)(m + cam + CAM_FOLLOWS);
        if (who >= 0x10000u && who + 0x1000u < lim) asked = *(uint32_t *)(m + who + CHR_CAM_BEHIND);
    }
    sub_000A6100_gen();
    if (cam < 0x10000u || cam + 0x240u >= lim) return;
    cf = (float *)(m + cam);
    mode = ((const uint32_t *)cf)[CAM_MODE / 4];
    behind = asked && !*(uint32_t *)(m + who + CHR_CAM_BEHIND);
    if (mode != CAMSCENE_MODE_GRAFFITI && cam_pad_steered(m, cam, 0))
        photo_mode_followed(cam);                   /* the player has control */
    /* Half pipe cam unticked: through the half pipe's jump, the target and
     * the eye as much higher as the skater is, and the free camera's orbit
     * with them (port/camera_scenes.h). The heading the controls follow,
     * which sub_000A28B0 takes from them after, is the same. */
    memcpy(seye, cf + CAM_SMOOTH_EYE / 4, sizeof seye);
    if (who >= 0x10000u && who + 0x1000u < lim && !camscene_shot(mode) && cam_pad_steered(m, cam, 0)) {
        static CAMSCENE_RISE rise;
        static uint32_t rise_cam;
        static int said;
        int jump = cam == s_rise_cam;
        double lift;
        if (cam != rise_cam) { memset(&rise, 0, sizeof rise); rise_cam = cam; }
        lift = camscene_rise(&rise, jump, *(const float *)(m + who + CHR_POSITION + 4),
                             cf[CAM_TARGET / 4 + 1]);
        if (jump != said) {
            said = jump;
            fprintf(stderr, "[CAMSCENE] t=%.2f camera %08X: %s\n", pad_now(), cam,
                    jump ? "the half pipe's jump, Half pipe cam off: the camera rises and falls with the skater"
                         : "the half pipe's jump over: letting go of the rise");
        }
        if (lift != 0) {
            cf[CAM_TARGET / 4 + 1] += (float)lift;
            cf[CAM_EYE / 4 + 1] += (float)lift;
            seye[1] += (float)lift;
        }
    }
    /* The graffiti shot is the game's to frame, whatever the free camera
     * (Graffiti cam, port/camera_scenes.c; unticked, the camera update never
     * switches to it on this camera): the free camera glides into it, and
     * out of it back to where the player had it (port/freecam.h). */
    if (cam_pad_steered(m, cam, 1) && !camscene_shot(mode))
        placed = freecam_orbit(cam, mode, cf + CAM_TARGET / 4, seye, behind, eye);
    if (placed != FREECAM_OFF) {
        w = camwall_for(cam, CAM_WALLS_FREECAM);
        if (placed == FREECAM_STARTED) camwall_reset(w);
        memcpy(game_eye, cf + CAM_EYE / 4, sizeof game_eye);  /* the game's, for the areas' draw */
        memcpy(cf + CAM_EYE / 4, eye, sizeof eye);
        camera_walls(cam, w, 1);
        /* Out of one of the game's shots, from its view to this one, after
         * walls: the shot's view is the game's, and what it looks at, the
         * graffiti shot's spot by a wall, would bring the eye in. */
        freecam_view(cam, 0, cf + CAM_EYE / 4, cf + CAM_TARGET / 4);
        cam_eye_note(cam, EYE_FREECAM, game_eye);
        return;
    }
    /* The game's own eye: its state goes with its mode, and starts again
     * when the game puts the camera back behind the player. With Default
     * cam fix off (camwall_on), the eye stands where the game put it, as on
     * the Xbox, and its state starts again, so turning it back on starts
     * from the game's eye. The graffiti shot is left as the game frames it
     * (camscene_shot); its state is still taken, so the follow mode after
     * it starts again, as before. Into it, the free camera glides from its
     * own view to the game's (port/freecam.h, freecam_view). */
    w = camwall_for(cam, mode);
    if (camscene_shot(mode) && cam_pad_steered(m, cam, 0)) {
        memcpy(game_eye, cf + CAM_EYE / 4, sizeof game_eye);
        if (freecam_view(cam, 1, cf + CAM_EYE / 4, cf + CAM_TARGET / 4)) glided = 1;
    }
    cam_eye_note(cam, glided ? EYE_FREECAM : EYE_GAME, glided ? game_eye : NULL);
    if (!camwall_on()) {
        camwall_reset(w);
        return;
    }
    if (behind) camwall_reset(w);
    if (camscene_shot(mode)) {
        static double next;
        if (pad_now() >= next) {
            next = pad_now() + 1.0;
            fprintf(stderr, "[CAMWALL] t=%.2f camera %08X mode %u: the graffiti shot, as the game frames it (no wall test)\n",
                    pad_now(), cam, mode);
        }
        return;
    }
    camera_walls(cam, w, 0);
    cam_eye_note(cam, EYE_DCAMFIX, NULL);
}

/* ---- the sun's glare behind walls (known bug 86, port/sun_flare.h) ---------
 *
 * sub_00044E80 is the sun flare's test, from its update's second slot
 * (sub_00046630, through sub_000464C0): 1 when something stands between the
 * camera's eye and the sun, and the glare gives way to the blocked sprite.
 * sub_00044C90 has cut the line from the eye to the sun's point into pieces
 * and, for each, kept the piece's box and asked the collision world for the
 * surfaces in it. This tests each piece that has any with the game's line
 * test, sub_00018260, as the follow camera's wall test does. It isn't fixed
 * where the pieces are made: the game's own test is the defect, testing each
 * piece from its box's smallest corner to its largest, so the recompiled code
 * matches its instructions, and no other function shows it.
 * The fix's result is steadied (sun_flare_steady), counted over the last
 * second: the glare hides only once nearly all of it was blocked, so a flip
 * for a frame or two at an edge doesn't start the game's fade.
 * With the fix off and its log off, the game's own test runs as it is. */
#include "sun_flare.h"
#define FLARE_STATE        0x048u      /* 1 in sight, 2 blocked, 4 going to blocked, 3 back, 0 off */
#define FLARE_EYE          0x050u      /* the camera's eye: where the line starts */
#define FLARE_SUN          0x084u      /* the sun's point: where it ends */
#define FLARE_PIECES       0x934u      /* how many pieces the line is cut into (sub_00044C90) */
#define FLARE_PIECE        0x13Cu      /* the first piece's record */
#define FLARE_PIECE_SIZE   0x44u       /* and each one's size */
#define PIECE_LO           0x08u       /* the piece's box: its smallest corner */
#define PIECE_HI           0x14u       /* and its largest */
#define PIECE_LIST         0x20u       /* the surfaces in it (sub_00016260): the line test's first argument */
#define PIECE_SURFS_1      0x24u       /* how many in its two parts: with none in either, */
#define PIECE_SURFS_2      0x34u       /* it isn't tested */
#define FLARE_LINE_MASK    0xFFFFu     /* what the flare's line test stops at */
#define FLARE_LINE_ANY     0xFFFFFFFFu /* its last argument, as sub_00044E80 passes it */
#define FLARE_LINE_RET     0x00044ECEu /* where sub_00044E80's line test returns to */
#define FLARE_HITS         0x00u       /* below the stack: the line test's hit record, */
#define FLARE_ENDS         0x10u       /* then the line's two ends, */
#define FLARE_SCRATCH      0x40u       /* in this much */
extern void sub_00044E80_gen(void);
void sub_00044E80(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    const uint32_t flare = g_ecx;
    const int fix = sun_flare_fix_on(), log = sun_flare_log_on();
    static SunFlareHold hold[4];    /* a flare each: one a camera, and split screen has up to 4 */
    static unsigned next;           /* the slot a flare not in it takes, the oldest */
    SunFlareTest test;
    uint32_t scr, args[6];
    float *ends;
    int i, k;
    if (!fix && !log) {
        sub_00044E80_gen();
        return;
    }
    memset(&test, 0, sizeof test);
    test.t = pad_now();
    test.flare = flare;
    test.state = (int)*(uint32_t *)(m + flare + FLARE_STATE);
    test.pieces = (int)*(int32_t *)(m + flare + FLARE_PIECES);
    test.game = test.real = -1;
    test.fix = fix;
    for (k = 0; k < 3; k++) {
        test.eye[k] = *(float *)(m + flare + FLARE_EYE + 4 * k);
        test.dir[k] = *(float *)(m + flare + FLARE_SUN + 4 * k) - test.eye[k];
    }
    g_esp -= FLARE_SCRATCH;
    scr = g_esp;
    ends = (float *)(m + scr + FLARE_ENDS);
    for (i = 0; i < test.pieces && i < SUN_FLARE_PIECES; i++) {
        const uint32_t rec = flare + FLARE_PIECE + (uint32_t)i * FLARE_PIECE_SIZE;
        const float *lo = (const float *)(m + rec + PIECE_LO), *hi = (const float *)(m + rec + PIECE_HI);
        if (!*(uint32_t *)(m + rec + PIECE_SURFS_1) && !*(uint32_t *)(m + rec + PIECE_SURFS_2)) continue;
        test.tested++;
        args[0] = rec + PIECE_LIST;
        args[1] = scr + FLARE_ENDS;
        args[2] = scr + FLARE_ENDS + 3 * sizeof(float);
        args[3] = scr + FLARE_HITS;
        args[4] = FLARE_LINE_MASK;
        args[5] = FLARE_LINE_ANY;
        /* The game's line, from the box's smallest corner to its largest,
         * for the log; the fix on alone doesn't need it. */
        if (log && test.game < 0) {
            memcpy(ends, lo, 3 * sizeof(float));
            memcpy(ends + 3, hi, 3 * sizeof(float));
            g_ecx = CAM_WORLD;
            if (guest_call_cdecl(sub_00018260, FLARE_LINE_RET, 6, args)) test.game = i;
        }
        /* The real line, along the piece. */
        if (test.real < 0) {
            sun_flare_ends(lo, hi, test.dir, ends, ends + 3);
            g_ecx = CAM_WORLD;
            if (guest_call_cdecl(sub_00018260, FLARE_LINE_RET, 6, args)) test.real = i;
        }
        if (test.real >= 0 && (!log || test.game >= 0)) break;
    }
    g_esp += FLARE_SCRATCH;
    if (fix) {
        SunFlareHold *h = NULL;
        for (k = 0; k < 4 && !h; k++)
            if (hold[k].flare == flare) h = &hold[k];
        if (!h) h = &hold[next++ % 4];
        test.shown = sun_flare_steady(h, flare, test.real >= 0);
    } else {
        test.shown = test.game >= 0;
    }
    if (log) sun_flare_note(&test);
    g_eax = (uint32_t)test.shown;
    g_esp += 4;         /* thiscall with nothing on the stack: its return */
}

/* sub_00044F00 gives the flare's two sprites their colours for this frame,
 * by its state and the sun's height: its first argument the glare's, the
 * second the sprite the game draws in its place while the sun is blocked
 * (+0xE8), each faded as the state changes (sub_00044BA0 counts the fade
 * down). With the fix on, the second is nothing, so no sprite shows over
 * what blocks the sun: its blend adds the colour, so nothing adds nothing.
 * Not at the source: the game draws that sprite on purpose; the fix leaves
 * it out. */
#define FLARE_BLOCKED_COLOUR 2      /* the argument with the blocked sprite's colour */
extern void sub_00044F00_gen(void);
void sub_00044F00(void)
{
    const uint32_t blocked = GARG(FLARE_BLOCKED_COLOUR);
    sub_00044F00_gen();
    if (sun_flare_fix_on() && blocked)
        *(uint32_t *)((uint8_t *)g_xbox_mem_offset + blocked) = 0;
}

/* ---- camera call counters (JSRF_CAM_LOG=1): which camera methods and mode
 * handlers actually run, printed every two seconds from the poser. */
static unsigned long jsrf_camcnt[18];
static const uint32_t jsrf_camva[18] = { 0x000A28B0u, 0x000A2960u, 0x000A3A70u, 0x000A3BD0u, 0x000A3D10u, 0x000A59B0u, 0x000A58B0u, 0x000A58C0u, 0x000A58E0u, 0x000A3E50u, 0x000A4100u, 0x000A5A00u, 0x000A5A70u, 0x000A5AC0u, 0x000A5AF0u, 0x000A4890u, 0x000A4D80u, 0x000A4C30u };
/* The heading the controls follow while the free camera glides
 * (port/freecam.h, freecam_ahead): sub_000A28B0 takes it from the view the
 * frame shows, its working eye (+0x20C) and target (+0x218), as the game's
 * sub_0004A7A0 of the look's x and z; here the same, of the view the glide
 * is headed to. As the camera cuts to a shot and back (sub_000A5AF0,
 * sub_000A58E0), the game keeps the heading it had (CAM_HELD_HEADING) and,
 * while CAM_HOLDS is set, the player manager's pad case (sub_000A76E0,
 * 0x000A799E to 0x000A7A80) sends the skater the way it went; it lets go
 * when the stick is let go, 6 frames after it turns over 20 degrees from
 * where it pointed, or once the heading comes within 20 degrees of the kept
 * one (0x000A7A4D). A glide starts from the view the heading was kept from,
 * so that last let go on its first frame, and the stick then went by the
 * gliding view: the game's code is right for its own cuts, and the glide is
 * the free camera's, so the heading is put where the cut would put it. */
extern void sub_0004A7A0(void);
static void cam_heading_ahead(uint32_t cam, const float eye[3], const float tgt[3])
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    float look[2];
    uint32_t args[2];
    look[0] = tgt[0] - eye[0];
    look[1] = tgt[2] - eye[2];
    memcpy(args, look, sizeof args);
    *(uint32_t *)(m + cam + CAM_HEADING) = guest_call_cdecl(sub_0004A7A0, CAM_HEADING_RET, 2, args);
}

/* The game's hold of the skater's way (CAM_HOLDS), on a camera the free
 * camera steers, for the log: as it starts, and as it ends, in degrees. */
static void cam_hold_log(const uint8_t *m, uint32_t cam)
{
    static uint32_t said_cam;
    static int said;
    int holds;
    if (!freecam_on() || !cam_pad_steered(m, cam, 0)) return;
    holds = *(const uint32_t *)(m + cam + CAM_HOLDS) != 0;
    if (cam == said_cam && holds == said) return;
    if (holds)
        fprintf(stderr, "[FREECAM] t=%.2f camera %08X: the skater keeps its way (the game's hold, heading %.0f;"
                " the controls' heading %.0f)\n", pad_now(), cam,
                (int16_t)*(const uint32_t *)(m + cam + CAM_HELD_HEADING) * (360.0 / 65536.0),
                (int16_t)*(const uint32_t *)(m + cam + CAM_HEADING) * (360.0 / 65536.0));
    else if (cam == said_cam)
        fprintf(stderr, "[FREECAM] t=%.2f camera %08X: the game's hold let go; the controls follow heading %.0f\n",
                pad_now(), cam, (int16_t)*(const uint32_t *)(m + cam + CAM_HEADING) * (360.0 / 65536.0));
    said_cam = cam;
    said = holds;
}

extern void sub_000A28B0_gen(void);
void sub_000A28B0(void)
{
    uint32_t cam = g_ecx;
    jsrf_camcnt[0]++;
    cam_eye_note(cam, EYE_SCENE, NULL);     /* the follow camera's hook, which this runs, says more */
    sub_000A28B0_gen();
    freecam_ran(cam);                       /* as it ends: what ran in it is known (port/freecam.h) */
    if (cam >= 0x10000u && cam + 0x240u < (64u << 20)) {
        const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
        const float *cf = (const float *)(m + cam);
        float eye[3], tgt[3];
        if (freecam_ahead(cam, eye, tgt)) cam_heading_ahead(cam, eye, tgt);
        cam_hold_log(m, cam);
        freecam_shown(cam, cf + CAM_VIEW_EYE / 4, cf + CAM_VIEW_TARGET / 4);   /* for a glide from it */
    }
}
/* The follow camera's draw: the camera it is, while it runs, for the set
 * of areas it makes from its eye (sub_0006A970), and whether photo mode
 * placed that eye (port/photo_mode.h): photo mode places the eye and the
 * look point the draw builds the view from first, while it holds the world
 * and this is its camera. */
static uint32_t s_cam_drawing;
static int s_photo_drawing;
extern void sub_000A2960_gen(void);
void sub_000A2960(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    uint32_t was = s_cam_drawing, cam = g_ecx;
    int photo_was = s_photo_drawing;
    jsrf_camcnt[1]++;
    s_cam_drawing = cam;
    s_photo_drawing = 0;
    if (photo_mode_holding() && cam >= 0x10000u && cam + CAM_VIEW_TARGET + 3 * sizeof(float) < (64u << 20)) {
        float *cf = (float *)(m + cam);
        s_photo_drawing = photo_mode_place(cam, cf + CAM_VIEW_EYE / 4, cf + CAM_VIEW_TARGET / 4);
    }
    sub_000A2960_gen();
    s_cam_drawing = was;
    s_photo_drawing = photo_was;
    wide_haze_corners("sub_000A2960");
}
extern void sub_000A3A70_gen(void);
void sub_000A3A70(void) { jsrf_camcnt[2]++; sub_000A3A70_gen(); }
extern void sub_000A3BD0_gen(void);
void sub_000A3BD0(void) { jsrf_camcnt[3]++; sub_000A3BD0_gen(); }
extern void sub_000A3D10_gen(void);
void sub_000A3D10(void) { jsrf_camcnt[4]++; sub_000A3D10_gen(); }
extern void sub_000A59B0_gen(void);
void sub_000A59B0(void) { jsrf_camcnt[5]++; sub_000A59B0_gen(); }
extern void sub_000A58B0_gen(void);
void sub_000A58B0(void) { jsrf_camcnt[6]++; sub_000A58B0_gen(); }
extern void sub_000A58C0_gen(void);
void sub_000A58C0(void) { jsrf_camcnt[7]++; sub_000A58C0_gen(); }
extern void sub_000A58E0_gen(void);
void sub_000A58E0(void) { jsrf_camcnt[8]++; sub_000A58E0_gen(); }
extern void sub_000A3E50_gen(void);
void sub_000A3E50(void) { jsrf_camcnt[9]++; sub_000A3E50_gen(); }
extern void sub_000A4100_gen(void);
void sub_000A4100(void) { jsrf_camcnt[10]++; sub_000A4100_gen(); }
extern void sub_000A5A00_gen(void);
void sub_000A5A00(void) { jsrf_camcnt[11]++; sub_000A5A00_gen(); }
extern void sub_000A5A70_gen(void);
void sub_000A5A70(void) { jsrf_camcnt[12]++; sub_000A5A70_gen(); }
extern void sub_000A5AC0_gen(void);
void sub_000A5AC0(void) { jsrf_camcnt[13]++; sub_000A5AC0_gen(); }
extern void sub_000A5AF0_gen(void);
void sub_000A5AF0(void) { jsrf_camcnt[14]++; sub_000A5AF0_gen(); }
extern void sub_000A4890_gen(void);
void sub_000A4890(void) { jsrf_camcnt[15]++; sub_000A4890_gen(); }
extern void sub_000A4D80_gen(void);
void sub_000A4D80(void) { jsrf_camcnt[16]++; sub_000A4D80_gen(); wide_haze_corners("sub_000A4D80"); }
extern void sub_000A4C30_gen(void);
void sub_000A4C30(void) { jsrf_camcnt[17]++; sub_000A4C30_gen(); }
static void jsrf_cam_counts(void)
{
    static int on = -1; static double next;
    int i; char line[1024]; int len = 0;
    if (on < 0) on = getenv("JSRF_CAM_LOG") != NULL;
    if (!on || jsrf_now() < next) return;
    next = jsrf_now() + 2.0;
    len += snprintf(line + len, sizeof line - len, "[CAMCNT] t=%.1f", jsrf_now());
    for (i = 0; i < (int)(sizeof jsrf_camcnt / sizeof jsrf_camcnt[0]); i++)
        if (jsrf_camcnt[i])
            len += snprintf(line + len, sizeof line - len, " %05X:%lu", jsrf_camva[i], jsrf_camcnt[i]);
    fprintf(stderr, "%s\n", line); fflush(stderr);
}


/* ---- draw distance ------------------------------------------------------------
 * Display > Draw distance (port/draw_distance.c) draws the world's objects
 * further than the game does: 2x, 3x or 4x its own distances, or Unlimited,
 * up to the camera's far plane. The game decides whether to draw each object
 * in a few gates. Each moves the object's centre into view space with the
 * matrix on top of the stack (sub_001BAB20), compares its depth there with
 * distances of the game's own, before or after the device's on-screen test
 * (vtable 0x1E0F00 +0x9C), and fades it out over the last stretch
 * (draw-distance/SEARCH-FINDINGS.md has how each was found):
 *
 *   sub_000C29C0  a person in a crowd: drawn to 400, faded from 300 by its
 *                 caller sub_000C2FE0 (the person's draw); the crowd's two
 *                 updates, sub_000C39E0 and sub_000C3A20, move only the
 *                 people within the same 400
 *   sub_000435B0  the pickups (the spray cans): drawn to 500, faded from 450
 *   sub_0003BCD0  the race's checkpoints: drawn to 400, faded from 300
 *   sub_000B9B50  every prop and car set's object: near to 400, then faded
 *                 by its callers through sub_000B9C70 to 500, then not drawn
 *   sub_000C1340  the Enemy bank's kinds (police, tanks and the rest): three
 *                 distances each, as arguments, a < b < c: faded from b to c
 *   sub_000E3930, sub_000E3BB0  a part attached to one of those: shown to 600
 *
 * A step multiplies every distance an object is cut or faded at, so each
 * kind keeps its own in proportion; Unlimited sets them to the largest float,
 * which no depth reaches, so nothing is cut or faded, and the on-screen test
 * and the far plane are what is left. What only picks a model's detail and
 * hides nothing stays the game's: sub_000C1340's a, a crowd's 15 (nearer, the
 * person isn't drawn). So does what else hides an object -- its area not
 * being drawn, a mission not having made it -- which is not a distance.
 *
 * Nothing is wrong in these functions: their distances are the game's
 * design, and the lifted code reads them from the game's constants at run
 * time as the console's does, so the recompiler, the runtime and the device
 * models aren't involved. Drawing further is a change to the game's own
 * logic, made here. At 1x every override runs the game's body and nothing
 * else.
 *
 * Most gates run as they are, with their constants set to the step's for the
 * length of the call (draw_distance_with). The constants are shared -- the
 * 400.0 at 0x1CA43C is read by twelve functions, the 0.01 at 0x1CA434 by
 * dozens -- but nothing these gates call reads them: they call the
 * transform, fabs (sub_0014C7F0) and the on-screen test (sub_00153C50, which
 * reads only its planes), or nothing; sub_000E3930 and sub_000E3BB0 call more,
 * and none of the 73 functions they reach is another reader of their 600
 * (the calls through vtables, to models and the device, aren't followed by
 * that count). Three that call further are written again instead, at 2x and
 * up: the person's draw, whose fade is followed by the model's draw, which may
 * read the shared 0.01; and the crowd's two updates, which run each person's
 * update. Each override counts what it lets through for the [DRAW] line. */
#include "draw_distance.h"

#define DD_K400         0x1CA43Cu   /* 400.0: crowds', checkpoints' and props' */
#define DD_K300         0x1CA438u   /* 300.0: crowds' and checkpoints' fade start */
#define DD_K1_100       0x1CA434u   /* 0.01 = 1 / (400 - 300), and props' 1 / (500 - 400) */
#define DD_K500         0x1CA6C0u   /* 500.0: pickups' and props' */
#define DD_K450         0x1CA6BCu   /* 450.0: pickups' fade start */
#define DD_K1_50        0x1CA6B8u   /* 0.02 = 1 / (500 - 450) */
#define DD_K600         0x1CED60u   /* 600.0: the attached part's */
#define MATRIX_TOP      0x264C04u   /* -> the matrix stack's top, -> its matrix */
#define MATRIX_ORIGIN_Z 0x38u       /* the matrix's translation z: the origin's depth */
#define DD_GUEST_LO     0x10000u
#define DD_GUEST_HI     (64u << 20)

/* A crowd's person (vtables 0x1CDC60, 0x1CDCB0, 0x1CDCF8) and its group
 * (vtable 0x1CDD60, people in a list from +0x44). */
#define PERSON_MATRIX   0x04u       /* where it stands and faces */
#define PERSON_FLAGS    0x30u
#define PERSON_SHOWN    0x01u       /*   once it has been in range and on screen */
#define PERSON_HIDDEN   0x02u
#define PERSON_TURN     0x40u       /* its heading (0x10000 a turn) */
#define PERSON_DEPTH    0x48u       /* fabs(view z) at its last draw */
#define PERSON_STATE    0x4Cu       /* sub_000C29C0's answer at its last draw */
#define PERSON_MODEL    0x60u       /* its model's number in the People bank */
#define PERSON_LOOK     0x6Cu       /* handed to the model's +0x5C (not traced) */
#define PERSON_DRAW_A   0x7Cu       /* the model's draw's two arguments */
#define PERSON_DRAW_B   0x80u
#define PERSON_NEXT     0xC8u
#define PERSON_UPDATE_A 0x28u       /* vtable slots 10 and 11: its updates */
#define PERSON_UPDATE_B 0x2Cu
#define GROUP_PEOPLE    0x44u
#define CROWD_NEAR      1u          /* sub_000C29C0: within 15, not drawn */
#define CROWD_FADING    3u          /*   past the fade's start */
#define TURN_HALF       0x8000u
#define PEOPLE_BANK     0x257CECu   /* -> the People bank, whose +0xC0 lists its models */
#define BANK_MODELS     0xC0u
#define MODEL_ALPHA     0x58u
#define MODEL_LOOK      0x5Cu
#define MODEL_DRAW      0x14u       /* vtable slot */
/* The return addresses of the calls the person's draw and the updates make,
 * as the game's. */
#define CROWD_PLACE_RET 0x000C2FEBu
#define CROWD_GATE_RET  0x000C3005u
#define CROWD_TURN_RET  0x000C3040u
#define CROWD_DRAW_RET  0x000C306Au
#define CROWD_WALK_RET  0x000C3A07u
#define CROWD_STEP_RET  0x000C3A52u

/* sub_000E3930 and sub_000E3BB0's object: the part's centre in view space
 * from +0x14C, its depth +0x154; bit 24 of +0x90 hides the part. They test it
 * on their layer-8 call. */
#define PART_DEPTH      0x154u
#define PART_FLAGS      0x90u
#define PART_HIDDEN     0x01000000u
#define PART_TEST_LAYER 8u

static int dd_guest(uint32_t a, uint32_t n) { return a >= DD_GUEST_LO && a < DD_GUEST_HI - n; }
static float *dd_float(uint32_t a) { return (float *)((uint8_t *)g_xbox_mem_offset + a); }
static uint32_t dd_u32(uint32_t a) { return *(uint32_t *)((uint8_t *)g_xbox_mem_offset + a); }

static void dd_drawn(int gate, int past)
{
    draw_distance_drawn(gate, past, pad_now(), nv2a_gl_flips());
}

/* The game's gate run with each constant at the step's (draw_distance_with). */
static void dd_run(void (*gen)(void), const uint32_t *at, const unsigned char *per, int n)
{
    float *f[4];
    int i;
    for (i = 0; i < n && i < 4; i++) f[i] = dd_float(at[i]);
    draw_distance_with(f, per, i, gen);
}

/* A call through slot `slot` of self's vtable, thiscall, as the game makes
 * it: g_eax after it, or 0 when the slot isn't the game's code. */
static uint32_t dd_vcall(uint32_t self, uint32_t slot, uint32_t ra, int nargs, const uint32_t *args)
{
    uint32_t va, sv = g_ecx, r;
    recomp_func_t fn;
    va = dd_u32(dd_u32(self) + slot);
    fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    if (!fn) {
        static int said;
        if (!said++) fprintf(stderr, "[DRAW] %08X's slot +%X is %08X, not the game's code\n", self, slot, va);
        return 0;
    }
    g_ecx = self;
    r = guest_call_cdecl(fn, ra, nargs, args);
    g_ecx = sv;
    return r;
}

extern void sub_000C29C0_gen(void);
void sub_000C29C0(void)
{
    static const uint32_t at[] = { DD_K400, DD_K300 };
    static const unsigned char per[] = { 0, 0 };
    uint32_t self = g_ecx;
    if (!draw_distance_step() || !dd_guest(self, PERSON_NEXT + 4)) { sub_000C29C0_gen(); return; }
    dd_run(sub_000C29C0_gen, at, per, 2);
    if (g_eax > CROWD_NEAR) dd_drawn(DRAW_CROWDS, *dd_float(self + PERSON_DEPTH) >= *dd_float(DD_K400));
}

/* The person's model in the People bank. */
static uint32_t crowd_model(uint32_t self)
{
    return dd_u32(dd_u32(dd_u32(PEOPLE_BANK) + BANK_MODELS) + dd_u32(self + PERSON_MODEL) * 4);
}

/* sub_000C2FE0, the person's draw, at 2x and up: the game's, with its fade
 * (400 - d) * 0.01 taken at the step's distances. */
extern void sub_000C2FE0_gen(void);
extern void sub_001BAAA0(void);
extern void sub_001BB330(void);
void sub_000C2FE0(void)
{
    uint32_t self = g_ecx, model, state, a[2];
    if (!draw_distance_step() || !dd_guest(self, PERSON_NEXT + 4) || !dd_guest(dd_u32(PEOPLE_BANK), BANK_MODELS + 4)) {
        sub_000C2FE0_gen();
        return;
    }
    g_ecx = self + PERSON_MATRIX;
    guest_call_cdecl(sub_001BAAA0, CROWD_PLACE_RET, 0, NULL);
    model = crowd_model(self);
    g_ecx = self;
    state = guest_call_cdecl(sub_000C29C0, CROWD_GATE_RET, 1, &model);
    *(uint32_t *)((uint8_t *)g_xbox_mem_offset + self + PERSON_STATE) = state;
    if (state && state != CROWD_NEAR && !(dd_u32(self + PERSON_FLAGS) & PERSON_HIDDEN)) {
        model = crowd_model(self);
        if (dd_guest(model, MODEL_LOOK + 4)) {
            g_ecx = dd_u32(self + PERSON_TURN) + TURN_HALF;
            guest_call_cdecl(sub_001BB330, CROWD_TURN_RET, 0, NULL);
            *(uint32_t *)((uint8_t *)g_xbox_mem_offset + model + MODEL_LOOK) = dd_u32(self + PERSON_LOOK);
            *dd_float(model + MODEL_ALPHA) = state == CROWD_FADING
                ? draw_distance_fade(*dd_float(self + PERSON_DEPTH), *dd_float(DD_K400), *dd_float(DD_K1_100))
                : 1.0f;
            a[0] = dd_u32(self + PERSON_DRAW_A);
            a[1] = dd_u32(self + PERSON_DRAW_B);
            dd_vcall(model, MODEL_DRAW, CROWD_DRAW_RET, 2, a);
        }
    }
    g_esp += 4;   /* ret */
}

/* sub_000C39E0 and sub_000C3A20, the crowd's two updates, at 2x and up: the
 * game's, for each person within the step's distance. The second also leaves
 * out people hidden, and those never yet in range and on screen. */
static void crowd_update(uint32_t group, uint32_t slot, uint32_t ra, int to_edge)
{
    float edge = draw_distance_far(*dd_float(DD_K400));   /* not `far`: <windows.h> empties it */
    uint32_t p, n = 0;
    for (p = dd_u32(group + GROUP_PEOPLE); p && n < 0x10000u; p = dd_u32(p + PERSON_NEXT), n++) {
        float d;
        uint32_t fl;
        if (!dd_guest(p, PERSON_NEXT + 4)) break;
        d = *dd_float(p + PERSON_DEPTH);
        fl = dd_u32(p + PERSON_FLAGS);
        if (to_edge ? !(d <= edge) || (fl & PERSON_HIDDEN) || !(fl & PERSON_SHOWN) : !(d < edge)) continue;
        dd_vcall(p, slot, ra, 0, NULL);
    }
}
extern void sub_000C39E0_gen(void);
void sub_000C39E0(void)
{
    uint32_t self = g_ecx;
    if (!draw_distance_step() || !dd_guest(self, GROUP_PEOPLE + 4)) { sub_000C39E0_gen(); return; }
    crowd_update(self, PERSON_UPDATE_A, CROWD_WALK_RET, 0);
    g_esp += 4;   /* ret */
}
extern void sub_000C3A20_gen(void);
void sub_000C3A20(void)
{
    uint32_t self = g_ecx;
    if (!draw_distance_step() || !dd_guest(self, GROUP_PEOPLE + 4)) { sub_000C3A20_gen(); return; }
    crowd_update(self, PERSON_UPDATE_B, CROWD_STEP_RET, 1);
    g_esp += 4;   /* ret */
}

/* The depth sub_000435B0 and sub_0003BCD0 test: they move the origin, so it
 * is the top matrix's translation z. */
static float dd_origin_depth(void)
{
    uint32_t top = dd_u32(MATRIX_TOP);
    if (!dd_guest(top, 4) || !dd_guest(dd_u32(top), MATRIX_ORIGIN_Z + 4)) return 0.0f;
    return *dd_float(dd_u32(top) + MATRIX_ORIGIN_Z);
}

extern void sub_000435B0_gen(void);
void sub_000435B0(void)
{
    static const uint32_t at[] = { DD_K500, DD_K450, DD_K1_50 };
    static const unsigned char per[] = { 0, 0, 1 };
    float d;
    if (!draw_distance_step()) { sub_000435B0_gen(); return; }
    d = dd_origin_depth();
    dd_run(sub_000435B0_gen, at, per, 3);
    if (g_eax) dd_drawn(DRAW_PICKUPS, d > *dd_float(DD_K500));
}

extern void sub_0003BCD0_gen(void);
void sub_0003BCD0(void)
{
    static const uint32_t at[] = { DD_K400, DD_K300, DD_K1_100 };
    static const unsigned char per[] = { 0, 0, 1 };
    float d;
    if (!draw_distance_step()) { sub_0003BCD0_gen(); return; }
    d = dd_origin_depth();
    dd_run(sub_0003BCD0_gen, at, per, 3);
    if (g_eax) dd_drawn(DRAW_CHECKPOINTS, d > *dd_float(DD_K400));
}

/* sub_000B9B50(object, float *depth): 0 the near model, 1 fading (its caller
 * takes the alpha from sub_000B9C70(depth)), 2 too far, 3 off screen. */
extern void sub_000B9B50_gen(void);
void sub_000B9B50(void)
{
    static const uint32_t at[] = { DD_K400, DD_K500 };
    static const unsigned char per[] = { 0, 0 };
    uint32_t depth = dd_u32(g_esp + 8);
    if (!draw_distance_step()) { sub_000B9B50_gen(); return; }
    dd_run(sub_000B9B50_gen, at, per, 2);
    if (g_eax <= 1 && dd_guest(depth, 4)) dd_drawn(DRAW_PROPS, *dd_float(depth) >= *dd_float(DD_K500));
}

/* sub_000B9C70(depth): 1 - (depth - 400) * 0.01, from 0 to 1. */
extern void sub_000B9C70_gen(void);
void sub_000B9C70(void)
{
    static const uint32_t at[] = { DD_K400, DD_K1_100 };
    static const unsigned char per[] = { 0, 1 };
    if (!draw_distance_step()) { sub_000B9C70_gen(); return; }
    dd_run(sub_000B9C70_gen, at, per, 2);
}

/* sub_000C1340(model, sphere, a, b, c), stdcall: within a the near model,
 * within b the middle one, faded to c by (c - d) / (c - b), past c not drawn.
 * b and c are the step's; a kind whose distances aren't in that order is left
 * as it is. The arguments are the caller's pushes, which it doesn't read
 * again. */
extern void sub_000C1340_gen(void);
void sub_000C1340(void)
{
    float *a, *b, *c, c_game, d = 0;
    uint32_t sphere = dd_u32(g_esp + 8);
    if (!draw_distance_step()) { sub_000C1340_gen(); return; }
    a = dd_float(g_esp + 0xC); b = dd_float(g_esp + 0x10); c = dd_float(g_esp + 0x14);
    c_game = *c;
    if (dd_guest(sphere, 0xC)) d = dd_float(sphere)[2];
    if (*a <= *b && *b < *c) {
        *b = draw_distance_far(*b);
        *c = draw_distance_far(*c);
    }
    sub_000C1340_gen();
    if (g_eax) dd_drawn(DRAW_ENEMIES, d > c_game);
}

static void dd_part(void (*gen)(void))
{
    static const uint32_t at[] = { DD_K600 };
    static const unsigned char per[] = { 0 };
    uint32_t self = g_ecx, layer = dd_u32(g_esp + 4);
    dd_run(gen, at, per, 1);
    if (layer == PART_TEST_LAYER && dd_guest(self, PART_DEPTH + 4) && !(dd_u32(self + PART_FLAGS) & PART_HIDDEN))
        dd_drawn(DRAW_PART, *dd_float(self + PART_DEPTH) > *dd_float(DD_K600));
}
extern void sub_000E3930_gen(void);
void sub_000E3930(void)
{
    if (!draw_distance_step()) { sub_000E3930_gen(); return; }
    dd_part(sub_000E3930_gen);
}
extern void sub_000E3BB0_gen(void);
void sub_000E3BB0(void)
{
    if (!draw_distance_step()) { sub_000E3BB0_gen(); return; }
    dd_part(sub_000E3BB0_gen);
}

/* Far buildings (port/draw_distance.c): the map's buildings aren't cut by
 * distance but by area. Each camera's draw makes the set of areas to draw
 * from the eye (sub_0006A970: the first area holding the eye, then the areas
 * its visible list names, a bit each in 8 words at 0x20C6C8), and the map's
 * passes, and the objects that test their area, draw only what is in it. On,
 * the override adds what the game would draw from the areas as many
 * crossings ahead as Far buildings' step, or every listed area at All
 * (draw_buildings_widen), after the game's own set is made, and the frame's
 * draw (sub_00012C80) puts the game's set back after it, so the next frame's
 * updates see the game's own. The map's shadows (sub_0006BA80, below) are
 * drawn from the game's set widened as far as Far shadows' step, never
 * further than Far buildings'.
 * The game draws its backdrop first, centred on the eye, and it writes depth
 * at its own size, which would hide the areas added beyond it: on, its loop
 * in sub_0006B6A0 draws without depth writes, between the matrix calls before
 * and after it (sub_001BA8F0, sub_001BAA50), known by their return
 * addresses, unless Developer > Far buildings test's Game's backdrop depth
 * is ticked. Off, all of them run the game's body and nothing else. Nothing
 * is wrong in these functions: the set is the game's design, made from its
 * map's data, and only the drawing is widened. */
#define AREA_SET        0x20C6C0u   /* the set's maker: +4 the map, +8 the set */
#define AREA_SET_MAP    0x04u
#define AREA_SET_BITS   0x08u
#define MAP_AREAS       0x04u       /* the map: its areas, and how many */
#define MAP_COUNT       0x08u
#define AREA_SIZE       0x154u
#define AREA_LIST       0x98u       /* the areas it shows, a dword each */
#define AREA_NLIST      0x9Cu
#define AREA_FOG_START  0xDCu       /* floats, to device +0x140 (sub_0006BBA0) */
#define AREA_FOG_END    0xE0u
#define AREA_FOG_DENSITY 0xE4u
#define DD_DEVICE       0x251D6Cu   /* -> the game's device (vtable 0x1E0F00) */
#define DD_ZWRITE_SLOT  0x130u      /* (device, on): NV2A SET_DEPTH_MASK */
#define DD_ZWRITE_NOW   0x19E1E0u   /* what it was set to last */
#define BACKDROP_IN_RET  0x0006B721u   /* sub_0006B6A0's sub_001BA8F0, before the backdrop */
#define BACKDROP_OUT_RET 0x0006B75Bu   /* its sub_001BAA50, after it */
/* The Misc group's objects (vtable 0x1CA558), the best candidate for the
 * birds: one model drawn at many places, in one area. */
#define MISC_MODEL      0x58u
#define MISC_PLACES     0x184u
#define MISC_AREA       0x188u

static uint32_t s_game_set[DRAW_SET_WORDS];
static int s_set_widened;       /* s_game_set is the game's, to put back */

/* The map's areas, as draw_buildings_widen takes them: 0 when the map isn't
 * there. */
static int dd_areas(DrawArea *out)
{
    uint32_t map = dd_u32(AREA_SET + AREA_SET_MAP), table, n, i;
    if (!dd_guest(map, MAP_COUNT + 4)) return 0;
    table = dd_u32(map + MAP_AREAS);
    n = dd_u32(map + MAP_COUNT);
    if (n > DRAW_AREAS_MAX) n = DRAW_AREAS_MAX;
    if (!n || !dd_guest(table, n * AREA_SIZE)) return 0;
    for (i = 0; i < n; i++) {
        uint32_t a = table + i * AREA_SIZE;
        uint32_t list = dd_u32(a + AREA_LIST), nlist = dd_u32(a + AREA_NLIST);
        DrawArea *o = &out[i];
        o->volume = dd_float(a);
        o->nlist = nlist <= DRAW_AREAS_MAX && dd_guest(list, nlist * 4) ? nlist : 0;
        o->list = o->nlist ? (const uint32_t *)((const uint8_t *)g_xbox_mem_offset + list) : NULL;
        o->fog_start = *dd_float(a + AREA_FOG_START);
        o->fog_end = *dd_float(a + AREA_FOG_END);
        o->fog_density = *dd_float(a + AREA_FOG_DENSITY);
    }
    return (int)n;
}

/* ---- the free camera's eye in no area (known bug 61) ----------------------
 * The game makes the set from its camera's eye alone, and a point in no area
 * gives an empty set: nothing of the map is drawn, nor anything that tests
 * its area, your skater among them. The game's own camera trails the player
 * inside the map's areas, and lifts when they drop; the free camera turns the
 * eye around the player at the game's distance, and so can put it outside
 * every area, as beside a tower on the way down (freecam4's tower fall). So,
 * while the free camera places the follow camera's eye (cam_eye_placed) and
 * the set made from it is empty, the set is made again from the eye the game
 * had put there that frame: the level is drawn as from where the game's
 * camera stands, and Far buildings widens that as usual. Only the set: what
 * the call answers, and the fog and light the draw takes from the eye's area
 * after it, stay as the free camera's eye gave them. (An area whose list
 * names none would give an empty set too, and is taken the same way.) Where
 * the game's eye is in no area too, the set made from it is empty as well:
 * as near the bottom of freecam5's tower fall, where the game's camera,
 * trailing the player down beside the tower, leaves the map's areas for
 * about two seconds. Then every area some list names is drawn
 * (draw_set_whole_map), as Far buildings' All draws them, for those frames
 * only. The game's own cameras are left as the game has them.
 * Nothing in sub_0006A970 is wrong: the set is the game's design, made for
 * its own camera, and the free camera is ours. Said in the log at most once a
 * second. */
#define AREA_SET_POINT 16u          /* the point made again from, on the guest stack */
extern void sub_0006A970_gen(void);
static int area_set_empty(const uint32_t set[DRAW_SET_WORDS])
{
    int w;
    for (w = 0; w < DRAW_SET_WORDS; w++)
        if (set[w]) return 0;
    return 1;
}
/* The set made again from point, as the game's own call makes it (ra, the
 * return address it was called from). */
static void area_set_remake(const float point[3], uint32_t ra)
{
    uint32_t a[1], sv_ecx = g_ecx;
    g_esp -= AREA_SET_POINT;
    memcpy((uint8_t *)g_xbox_mem_offset + g_esp, point, 3 * sizeof(float));
    a[0] = g_esp;
    g_ecx = AREA_SET;
    guest_call_cdecl(sub_0006A970_gen, ra, 1, a);
    g_ecx = sv_ecx;
    g_esp += AREA_SET_POINT;
}
static void area_set_from_game_eye(uint32_t set[DRAW_SET_WORDS], uint32_t ra)
{
    static double next;
    static DrawArea areas[DRAW_AREAS_MAX];
    const float *game_eye = NULL;
    uint32_t cam = s_cam_drawing;
    int whole;
    if (!cam || !area_set_empty(set) || cam_eye_placed(cam, &game_eye) != EYE_FREECAM) return;
    area_set_remake(game_eye, ra);
    whole = area_set_empty(set);
    if (whole) draw_set_whole_map(areas, dd_areas(areas), set);
    if (pad_now() >= next) {
        int n = 0, i;
        next = pad_now() + 1.0;
        for (i = 0; i < DRAW_AREAS_MAX; i++) n += (set[i >> 5] >> (i & 31)) & 1;
        if (whole)
            fprintf(stderr, "[FREECAM] t=%.2f camera %08X's eye is in no area, and the game's camera's too, at "
                            "(%.1f %.1f %.1f): the whole map's %d areas are drawn\n",
                    pad_now(), cam, game_eye[0], game_eye[1], game_eye[2], n);
        else
            fprintf(stderr, "[FREECAM] t=%.2f camera %08X's eye is in no area: the areas drawn are the game's "
                            "camera's, from (%.1f %.1f %.1f): %d areas\n",
                    pad_now(), cam, game_eye[0], game_eye[1], game_eye[2], n);
    }
}

/* ---- photo mode's eye in no area -------------------------------------------
 * Photo mode's camera goes through walls, under the floor and out of the
 * map, and a point in no area gives an empty set. While it places the eye
 * (s_photo_drawing), the set is made from that eye, as the game makes it;
 * empty, it is made again from the last point the eye was in an area
 * (photo_mode_area), so going through a wall shows what was seen from where
 * the camera came from, until it comes out in another area. Should that
 * give none too, every area some list names is drawn, as for the free
 * camera. The free camera's way, from the game's camera's eye, would be
 * wrong here: the game's camera stays where photo mode took over. */
static void area_set_from_photo(uint32_t set[DRAW_SET_WORDS], uint32_t eye_at, uint32_t ra)
{
    static DrawArea areas[DRAW_AREAS_MAX];
    float eye[3], from[3];
    if (eye_at < 0x10000u || eye_at + sizeof eye >= (64u << 20)) return;
    memcpy(eye, (uint8_t *)g_xbox_mem_offset + eye_at, sizeof eye);
    if (!photo_mode_area(area_set_empty(set), eye, from)) return;
    area_set_remake(from, ra);
    if (area_set_empty(set)) draw_set_whole_map(areas, dd_areas(areas), set);
}

/* The game's scene now, for the map's rules for one scene (draw_distance.h,
 * draw_buildings_scene): the number the mission's command 227 gave the scene
 * the game manager holds (sub_000524E0 passes the command's first word to
 * sub_00024240, which makes the scene, sub_0001E540, keeping it at +0x8C),
 * from its start to its end, loading and fading in too; DRAW_SCENE_NONE
 * without one. */
#define SCENE_NUMBER 0x8Cu
static uint32_t scene_number(void)
{
    uint32_t gm = MEM32(GAME_MANAGER), scene;
    if (gm < ADX_GUEST_LOW || gm + GM_ACTORS + SCENE_ACTOR * 4u + 4u >= ADX_GUEST_RAM) return DRAW_SCENE_NONE;
    scene = MEM32(gm + GM_ACTORS + SCENE_ACTOR * 4u);
    if (scene < ADX_GUEST_LOW || scene + SCENE_NUMBER + 4u >= ADX_GUEST_RAM || MEM32(scene) != SCENE_CLASS)
        return DRAW_SCENE_NONE;
    return MEM32(scene + SCENE_NUMBER);
}

/* sub_0006A970(eye), thiscall on AREA_SET: the game's set; made again from
 * the game's eye while the free camera's is in no area (above), or from the
 * last point in an area while photo mode's is in none; then, with Far
 * buildings on, the areas added to it, the map's rules taken as the game's
 * scene now has them (scene_number). */
void sub_0006A970(void)
{
    static DrawArea areas[DRAW_AREAS_MAX];
    uint32_t self = g_ecx, ra = GARG(0), eye_at = GARG(1);
    uint32_t *set = (uint32_t *)((uint8_t *)g_xbox_mem_offset + AREA_SET + AREA_SET_BITS);
    int n;
    sub_0006A970_gen();
    if (self != AREA_SET) return;
    if (s_photo_drawing) area_set_from_photo(set, eye_at, ra);
    else area_set_from_game_eye(set, ra);
    if (!draw_far_buildings()) return;
    n = dd_areas(areas);
    if (!n) return;
    memcpy(s_game_set, set, sizeof s_game_set);
    s_set_widened = 1;
    draw_buildings_scene(scene_number());
    draw_buildings_widen(areas, n, set, pad_now(), nv2a_gl_flips());
}

/* sub_00012C80, the frame's draw: the game's set back after it. */
extern void sub_00012C80_gen(void);
void sub_00012C80(void)
{
    sub_00012C80_gen();
    if (s_set_widened) {
        memcpy((uint8_t *)g_xbox_mem_offset + AREA_SET + AREA_SET_BITS, s_game_set, sizeof s_game_set);
        s_set_widened = 0;
    }
}

/* ---- photo mode (port/photo_mode.c): the world held -----------------------
 * The game manager has four holds of its own, a dword each from +0x40, which
 * its main loop's pass (sub_00013A80) takes up from the requests the game's
 * own calls leave (sub_000126D0, sub_000126F0, sub_00012710 and
 * sub_00012730, each on or off), before its update (sub_000123E0). While one
 * is on, the update walks the tasks with that hold's own slots in place of
 * each object's update and post-update, which do nothing for nearly every
 * object, and the frame's draw (sub_000131F0) draws each through that hold's
 * own draw slot. The fourth, at +0x4C, is the one whose draw slot is the
 * camera's usual draw (0xB3DE0, to the vtable's +0xC), so the world stays
 * drawn: the game takes it for its fades (the one at 0x6662F lets it go as a
 * fade from black ends) and its missions' held moments. Photo mode takes it,
 * once a pass, through sub_00012730, only when none of the four is on, and
 * lets it go the same way; going, the camera's view is put back as the game
 * had it. Some objects draw nothing in that hold, so while photo mode has it
 * the frame's draw runs with it down, every object drawn by its usual draw,
 * as in play: nothing has moved since. Nothing in these functions is wrong:
 * the holds are the game's design, and photo mode is ours. */
#define GM_HOLDS        0x40u       /* the game manager's holds, a dword each */
#define GM_HOLD_COUNT   4
#define GM_HOLD_PHOTO   0x4Cu       /* the fades' hold, photo mode's */
#define GM_HOLD_SET_RET 0x00066641u /* where a call of sub_00012730 returns to, at a fade's end */
extern void sub_00012730(void);

/* The map's portals, for photo mode's camera (port/photo_mode.h). The game's
 * portal manager, a static object, keeps each loaded stage's in a list, made
 * from the stage's data as it loads (sub_0006F7B0): each portal its quad as
 * two triangles, of which each keeps its first corner and points to its
 * other two in the stage's data; then the offset crossing it moves a skater
 * by, and the next. Read as they are each game frame, as the game tests
 * them (sub_0006F8C0), so a stage loaded or let go is seen at once. Only a
 * skater's step is tested against them, so photo mode tests its own;
 * nothing in the game's test is wrong: the portals are the game's design,
 * and photo mode's camera is ours. */
#define PORTALS         0x0020CC58u /* the portal manager */
#define PORTALS_FIRST   0x04u       /* its first portal; 0 for none */
#define PORTAL_TRI      0x30u       /* a triangle's size: the two from +0x00 */
#define PORTAL_TRI_B    0x20u       /* a triangle's second corner, a pointer */
#define PORTAL_TRI_C    0x24u       /* and its third */
#define PORTAL_BY       0x60u       /* the offset */
#define PORTAL_NEXT     0x70u
#define PORTAL_SIZE     0x74u

static int photo_in_ram(uint32_t at, uint32_t size)
{
    return at >= 0x10000u && at + size < (64u << 20);
}

static int photo_portals(PHOTO_PORTAL out[PHOTO_PORTALS_MOST])
{
    static int said;
    const uint8_t *m = (const uint8_t *)g_xbox_mem_offset;
    uint32_t p = *(const uint32_t *)(m + PORTALS + PORTALS_FIRST), tri, b, c;
    int n = 0, t;
    for (; p; p = *(const uint32_t *)(m + p + PORTAL_NEXT)) {
        if (!photo_in_ram(p, PORTAL_SIZE)) break;
        if (n == PHOTO_PORTALS_MOST) {
            if (!said) {
                said = 1;
                fprintf(stderr, "[PHOTO] the map has more than %d portals; photo mode's camera goes "
                                "through the first %d\n", PHOTO_PORTALS_MOST, PHOTO_PORTALS_MOST);
            }
            break;
        }
        for (t = 0; t < 2; t++) {
            tri = p + PORTAL_TRI * (uint32_t)t;
            b = *(const uint32_t *)(m + tri + PORTAL_TRI_B);
            c = *(const uint32_t *)(m + tri + PORTAL_TRI_C);
            if (!photo_in_ram(b, sizeof out[n].tri[t][1]) || !photo_in_ram(c, sizeof out[n].tri[t][2])) break;
            memcpy(out[n].tri[t][0], m + tri, sizeof out[n].tri[t][0]);
            memcpy(out[n].tri[t][1], m + b, sizeof out[n].tri[t][1]);
            memcpy(out[n].tri[t][2], m + c, sizeof out[n].tri[t][2]);
        }
        if (t < 2) break;
        memcpy(out[n].by, m + p + PORTAL_BY, sizeof out[n].by);
        n++;
    }
    return n;
}

static void photo_frame(uint32_t gm)
{
    static PHOTO_PORTAL portals[PHOTO_PORTALS_MOST];
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    PLAYBACK_STATE ps = playback_state();
    PHOTO_GAME g;
    uint32_t a[1], cam, sv_ecx;
    float eye[3], look[3];
    int i, act;
    if (gm < 0x10000u || gm + GM_HOLDS + 4 * GM_HOLD_COUNT >= (64u << 20)) return;
    g.holds = 0;
    for (i = 0; i < GM_HOLD_COUNT; i++)
        if (*(const uint32_t *)(m + gm + GM_HOLDS + 4u * i)) g.holds |= 1u << i;
    g.playback = ps == PLAYBACK_RECORDING || ps == PLAYBACK_PLAYING || ps == PLAYBACK_WAITING;
    g.portals = portals;
    g.nportals = photo_mode_ticked() ? photo_portals(portals) : 0;     /* only photo mode's camera needs them */
    act = photo_mode_frame(&g);
    if (act == PHOTO_NOTHING) return;
    if (act == PHOTO_ASK_LET_GO && photo_mode_game_view(&cam, eye, look)
     && cam >= 0x10000u && cam + CAM_VIEW_TARGET + sizeof look < (64u << 20)) {
        memcpy(m + cam + CAM_VIEW_EYE, eye, sizeof eye);
        memcpy(m + cam + CAM_VIEW_TARGET, look, sizeof look);
    }
    a[0] = act == PHOTO_ASK_HOLD;
    sv_ecx = g_ecx;
    g_ecx = gm;
    guest_call_cdecl(sub_00012730, GM_HOLD_SET_RET, 1, a);
    g_ecx = sv_ecx;
}

/* The game's own frame's draw (sub_000131F0_gen), called from that
 * function's hook (with the cutscene prompt's), thiscall on the game
 * manager: as in play while photo mode holds the world (above). */
static void photo_draw(void)
{
    uint32_t gm = g_ecx, *hold, was;
    if (!photo_mode_holding() || gm < 0x10000u || gm + GM_HOLD_PHOTO + 4 >= (64u << 20)) {
        sub_000131F0_gen();
        return;
    }
    hold = (uint32_t *)((uint8_t *)g_xbox_mem_offset + gm + GM_HOLD_PHOTO);
    was = *hold;
    *hold = 0;
    sub_000131F0_gen();
    *hold = was;
}

/* ---- your own skater not drawn whole: the log (known bug 61) ---------------
 * The skaters' draw (sub_00081860, thiscall on the skater) first finds the
 * skater's area (sub_0006AAA0 on AREA_SET, from a point of its own and its
 * last area at +0xA40; -1 for none), and draws nothing of a skater whose area
 * isn't in the set drawn from the eye (sub_0006AC10, which answers 0 for -1
 * too). Then, for the skater whose camera the frame is drawn from (its
 * camera's id at +0xC98 finds it, sub_000128C0, and that camera's +8 is the
 * object manager's +0x87F0), in every state (+0xE50) but 0x14 to 0x16 and
 * 0x1A, it fades the skater by its camera's distance (+0x230): not drawn
 * under CAM_SKATER_HIDDEN, faded by CAM_SKATER_FADE a unit from there to
 * CAM_SKATER_WHOLE, and never past the skater's own +0xC40; at 0, nothing of
 * it is drawn. The free camera's eye came nearer than the game's camera does
 * (known bug 61); this says, at most once a second each, when your own
 * skater isn't drawn whole and why: its area, or its camera's distance and
 * what placed that camera's eye (cam_eye_by). It hooks the area test where
 * the skaters' draw makes it, known by its return address, and reads the
 * rest as the draw does, without calling anything. Nothing in these
 * functions is wrong: the answers are the game's, and this only says them,
 * but for one (sub_0006AC10's hook, below). */
#define GAME_OBJECTS       0x0022FCE0u /* -> the game's object manager */
#define GAME_OBJECT_TABLE  0x098u      /* its objects, by id (sub_000128C0) */
#define GAME_FRAME_CAM     0x87F0u     /* the id the camera the frame is drawn from is known by */
#define CHR_LAST_AREA      0xA40u      /* the area it was last in */
#define CHR_CAM_ID         0xC98u      /* its camera's id */
#define CHR_ALPHA_MOST     0xC40u      /* the most it is drawn at, 0 to 1 */
#define CHR_STATE          0xE50u      /* its state: */
#define CHR_STATE_UNFADED_LO 0x14u     /*   0x14 to 0x16 and 0x1A aren't faded by the camera */
#define CHR_STATE_UNFADED_HI 0x16u
#define CHR_STATE_UNFADED  0x1Au
#define SKATER_AREA_RET    0x000819D5u /* where the skaters' draw's area test returns to */

/* The camera of skater, when it is the camera the frame is drawn from: your
 * own skater's. 0 for another skater. */
static uint32_t skater_frame_cam(uint32_t skater)
{
    uint32_t gm = dd_u32(GAME_OBJECTS), id, at, cam;
    if (!dd_guest(skater, CHR_STATE + 4) || !dd_guest(gm, GAME_FRAME_CAM + 4)) return 0;
    id = dd_u32(skater + CHR_CAM_ID);
    at = gm + GAME_OBJECT_TABLE + id * 4;
    if ((int32_t)id < 0 || at < gm || !dd_guest(at, 4)) return 0;
    cam = dd_u32(at);
    if (!dd_guest(cam, CAM_DISTANCE + 4) || dd_u32(cam + CAM_ID) != dd_u32(gm + GAME_FRAME_CAM))
        return 0;
    return cam;
}

/* Your own skater, whose camera is cam (skater_frame_cam). */
static void skater_drawn_log(uint32_t skater, uint32_t cam, int32_t area, uint32_t in_set)
{
    static double next_area, next_fade;
    uint32_t state;
    float d, whole, hidden, alpha, most;
    if (!in_set) {
        if (pad_now() >= next_area) {
            next_area = pad_now() + 1.0;
            if (area < 0)
                fprintf(stderr, "[SKATER] t=%.2f your skater %08X isn't drawn: it is in no area (it was last in %d)\n",
                        pad_now(), skater, (int32_t)dd_u32(skater + CHR_LAST_AREA));
            else
                fprintf(stderr, "[SKATER] t=%.2f your skater %08X isn't drawn: its area, %d, isn't in the set drawn from the eye (%s)\n",
                        pad_now(), skater, area,
                        s_set_widened ? "widened by Far buildings" : "the game's own");
        }
        return;
    }
    state = dd_u32(skater + CHR_STATE);
    if ((state >= CHR_STATE_UNFADED_LO && state <= CHR_STATE_UNFADED_HI) || state == CHR_STATE_UNFADED)
        return;
    d = *dd_float(cam + CAM_DISTANCE);
    whole = *dd_float(CAM_SKATER_WHOLE);
    hidden = *dd_float(CAM_SKATER_HIDDEN);
    if (!(d < whole) || pad_now() < next_fade) return;
    next_fade = pad_now() + 1.0;
    alpha = d < hidden ? 0 : (d - hidden) * *dd_float(CAM_SKATER_FADE);
    most = *dd_float(skater + CHR_ALPHA_MOST);
    if (alpha > most) alpha = most;
    fprintf(stderr, "[SKATER] t=%.2f your skater %08X %s %.0f%%: its camera %08X's eye is %.1f from it (not drawn under %.0f, whole from %.0f), placed by %s\n",
            pad_now(), skater, alpha > 0 ? "drawn at" : "isn't drawn, at", alpha * 100, cam, d,
            hidden, whole, cam_eye_by(cam));
}

/* sub_0006AC10(area), thiscall on AREA_SET: whether area is in the set.
 * Where the skaters' draw asks it, for your own skater in no area (-1) while
 * the free camera places its camera's eye: yes, so it is drawn. The game
 * hides a skater whose point is in no area, whatever the camera; in
 * freecam4's tower fall your skater's point was in no area for two seconds,
 * as well as the free camera's eye (sub_0006A970, above), and the skater was
 * gone. The free camera turns around your skater, which is what it shows, so
 * it is drawn while the free camera places the eye. Every other skater, and
 * yours with the game's own camera, keep the game's answer. */
extern void sub_0006AC10_gen(void);
void sub_0006AC10(void)
{
    uint32_t from = GARG(0), self = g_ecx, skater = g_ebx, cam;
    int32_t area = (int32_t)GARG(1);
    sub_0006AC10_gen();
    if (from != SKATER_AREA_RET || self != AREA_SET) return;
    cam = skater_frame_cam(skater);
    if (!cam) return;                   /* not the skater the frame is drawn for */
    if (area < 0 && !g_eax && cam_eye_placed(cam, NULL) == EYE_FREECAM) {
        static double next;
        g_eax = 1;
        if (pad_now() >= next) {
            next = pad_now() + 1.0;
            fprintf(stderr, "[SKATER] t=%.2f your skater %08X is in no area: drawn, as the free camera places its camera's eye\n",
                    pad_now(), skater);
        }
    }
    skater_drawn_log(skater, cam, area, g_eax);
}

/* sub_0006BA80, thiscall on the map task's +0x44: the map's shadows. The map
 * task's pass switch (sub_00074CB0) sends its passes 17 and 18 here while
 * task 9 is there; in play the [DRAW] line counts one call a frame while the
 * set is widened. It draws the pieces of type 2 (+0x44) of every area in the
 * set, untextured: the map's shadow volumes, which the stencil counts. With
 * the set widened, they are drawn from the game's set widened as far as Far
 * shadows' step, which is the widened set itself when the two rows are at
 * the same step, and from the game's own with Far shadows off; the widened
 * set is put back after (draw_buildings_shadows), as the far areas' volumes
 * take the shadow off the walls in front of them (known bug 52). Nothing is
 * wrong in this function: the cause is the widening, and this is where Far
 * buildings keeps it from the shadows as far as Far shadows says. */
extern void sub_0006BA80_gen(void);
void sub_0006BA80(void)
{
    if (!s_set_widened) { sub_0006BA80_gen(); return; }
    draw_buildings_shadows((uint32_t *)((uint8_t *)g_xbox_mem_offset + AREA_SET + AREA_SET_BITS),
                           s_game_set, sub_0006BA80_gen);
}

/* The device's depth writes on or off, as the game sets them. */
static void dd_zwrite(uint32_t on, uint32_t ra)
{
    uint32_t dev = dd_u32(DD_DEVICE), va, a[2];
    recomp_func_t fn;
    if (!dd_guest(dev, 4) || !dd_guest(dd_u32(dev), DD_ZWRITE_SLOT + 4)) return;
    va = dd_u32(dd_u32(dev) + DD_ZWRITE_SLOT);
    fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    if (!fn) {
        static int said;
        if (!said++) fprintf(stderr, "[DRAW] the device's depth writes are %08X, not the game's code\n", va);
        return;
    }
    a[0] = dev;
    a[1] = on;
    guest_call_cdecl(fn, ra, 2, a);
}

static int s_backdrop_zwrite = -1;  /* the depth writes to put back after the backdrop */

extern void sub_001BA8F0_gen(void);
void sub_001BA8F0(void)
{
    uint32_t ra = dd_u32(g_esp);
    sub_001BA8F0_gen();
    /* Developer > Far buildings test's Game's backdrop depth leaves them as
     * the game sets them (draw_far_test_backdrop). */
    if (ra == BACKDROP_IN_RET && draw_far_buildings() && !draw_far_test_backdrop() && s_backdrop_zwrite < 0) {
        s_backdrop_zwrite = (int)(dd_u32(DD_ZWRITE_NOW) != 0);
        dd_zwrite(0, ra);
    }
}

extern void sub_001BAA50_gen(void);
void sub_001BAA50(void)
{
    uint32_t ra = dd_u32(g_esp);
    sub_001BAA50_gen();
    if (ra == BACKDROP_OUT_RET && s_backdrop_zwrite >= 0) {
        dd_zwrite((uint32_t)s_backdrop_zwrite, ra);
        s_backdrop_zwrite = -1;
    }
}

/* sub_000402C0 and sub_00040300, the Misc group's draws: with Far buildings
 * on, each object's model, area and places go to the log (draw_misc_drawn),
 * to tell which are the birds. */
static void dd_misc(uint32_t self)
{
    if (!draw_far_buildings() || !dd_guest(self, MISC_AREA + 4)) return;
    draw_misc_drawn(self, dd_u32(self + MISC_MODEL), dd_u32(self + MISC_AREA), dd_u32(self + MISC_PLACES));
}
extern void sub_000402C0_gen(void);
void sub_000402C0(void)
{
    dd_misc(g_ecx);
    sub_000402C0_gen();
}
extern void sub_00040300_gen(void);
void sub_00040300(void)
{
    dd_misc(g_ecx);
    sub_00040300_gen();
}
