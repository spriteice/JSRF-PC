/* Run with: python tests/heap_guard/run.py
 *
 * The heap guard (RECOMP_HEAP_GUARD, src/kernel/xbox_heap.c), over 64 MB of
 * guest memory, as the game's kernel calls use it. "off" is the heap as it
 * is without it: a released block is handed out again at once, and a
 * second release is ignored without a word. "on" is with it, where each of
 * the game's mistakes with released memory is said, once for each place,
 * with where in the game's code it was made and where the block was
 * released before:
 *
 *   - a block released twice, while it is held and after it is let go;
 *   - a read and a write of a held block's pages, after which the test
 *     carries on, as the game does (the fault handler here stands in for
 *     port/main.c's);
 *   - a release through another call than the one that allocated it;
 *   - a write into a small block's bytes after its release, found when the
 *     guard lets it go;
 *   - a release inside a block, and of heap space no block holds.
 *
 * And what the guard must not change: a held block is free space to the
 * hooks (xbox_HeapIsFreeSpace) and has no size (xbox_HeapBlockSize), and
 * every block goes where it would without the guard. A held block stays
 * held while no request wants its space, and is let go for the one that
 * does: the one that takes its space, or would leave a sliver of it beside
 * the new block, and the top comes down past held blocks as it would.
 * "trace" makes the same requests and releases with the guard and without,
 * the title's file loads, stage loads and a random mix, and prints every
 * address the heap gives; run.py checks the two are the same. run.py checks
 * the lines.
 *
 * The "game's code" is a guest stack the test writes, with return addresses
 * between g_xbox_code_lo and g_xbox_code_hi.
 */
#if !defined(_WIN32)
#define _GNU_SOURCE 1   /* REG_ERR */
#endif
#include "kernel.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <ucontext.h>
#endif
#include "xbox_memory_layout.h"
#include "kernel_bridge.h"

/* What the runtime gives the heap: the guest's memory, its size, the
 * calling thread's guest stack, and where the game's code is. */
RECOMP_TLS uint32_t g_esp;
ptrdiff_t g_xbox_mem_offset;
size_t g_xbox_total_ram = 0x04000000u;   /* the heap runs to 64 MB */
size_t g_xbox_map_size;
size_t xbox_GetMappedSize(void) { return g_xbox_total_ram; }
uint32_t g_xbox_code_lo = 0x00011000u, g_xbox_code_hi = 0x001C4000u;

#define STACK  0x00700000u   /* a guest stack, below the heap */
#define MB     (1u << 20)

static int s_failures;

static void fail(const char *what)
{
    printf("heap_guard: %s\n", what);
    fflush(stdout);
    s_failures++;
}

static uint8_t *host(uint32_t va) { return (uint8_t *)((uintptr_t)va + g_xbox_mem_offset); }

/* The kernel call's caller: its return address where the dispatcher leaves
 * it (esp - 4), its caller's above. */
static void from(uint32_t ret, uint32_t caller)
{
    g_esp = STACK;
    memset(host(STACK - 4), 0, 64 * 4);
    memcpy(host(STACK - 4), &ret, 4);
    memcpy(host(STACK + 8), &caller, 4);
}

/* port/main.c's handlers, for the test. */
#if defined(_WIN32)
static LONG WINAPI on_fault(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
    return xbox_HeapGuardFault((uintptr_t)er->ExceptionInformation[1],
                               er->ExceptionInformation[0] <= 1
                               ? (int)er->ExceptionInformation[0] : 2, "the test")
           ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}
static void handle_faults(void) { AddVectoredExceptionHandler(1, on_fault); }
#else
static void on_fault(int sig, siginfo_t *si, void *ctx)
{
    int access = 2;
#if defined(__x86_64__)
    access = (((ucontext_t *)ctx)->uc_mcontext.gregs[REG_ERR] & 2) ? 1 : 0;
#endif
    if (xbox_HeapGuardFault((uintptr_t)si->si_addr, access, "the test"))
        return;
    signal(sig, SIG_DFL);   /* a crash after all: the next fault ends the test */
}
static void handle_faults(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
}
#endif

static void off(void)
{
    uint32_t a, b;

    if (xbox_HeapGuardOn()) fail("the guard is on without RECOMP_HEAP_GUARD");
    from(0x0002D8D0u, 0x0004AC80u);
    a = xbox_HeapAlloc(64 * 1024, 4096, XBOX_HEAP_VIRTUAL);
    xbox_HeapFree(a, XBOX_HEAP_VIRTUAL);
    b = xbox_HeapAlloc(64 * 1024, 4096, XBOX_HEAP_VIRTUAL);
    if (a != b) fail("without the guard, a released block was not handed out again at once");
    xbox_HeapFree(b, XBOX_HEAP_VIRTUAL);
    xbox_HeapFree(b, XBOX_HEAP_VIRTUAL);            /* said nothing */
    a = xbox_HeapAlloc(100, 16, XBOX_HEAP_POOL);
    xbox_HeapFree(a, XBOX_HEAP_VIRTUAL);            /* nor this */
    printf("heap_guard: off: a released block is handed out again at 0x%08X\n", b);
}

static int held(uint32_t va) { return strstr(xbox_HeapDescribe(va), "the guard holds") != NULL; }

static void on(void)
{
    uint32_t m1, m2, n, a, e, g, b, c, d, huge, again;
    volatile uint8_t *p;
    uint8_t seen;
    int i;

    if (!xbox_HeapGuardOn()) fail("RECOMP_HEAP_GUARD did not turn the guard on");
    handle_faults();

    /* Two held blocks, and a request for the first one's space but 8 bytes:
     * it takes that space, as it would without the guard, and the second is
     * let go too, as it would leave a sliver of 8 bytes beside it, which the
     * heap doesn't keep apart. Its address isn't handed out, so a second
     * release of it is still named. */
    from(0x00021000u, 0x00021100u);
    m1 = xbox_HeapAlloc(256, 16, XBOX_HEAP_POOL);
    m2 = xbox_HeapAlloc(256, 16, XBOX_HEAP_POOL);
    (void)xbox_HeapAlloc(16, 16, XBOX_HEAP_POOL);          /* above them */
    xbox_HeapFree(m2, XBOX_HEAP_POOL);
    xbox_HeapFree(m1, XBOX_HEAP_POOL);
    n = xbox_HeapAlloc(248, 16, XBOX_HEAP_POOL);
    if (n != m1) fail("a held block's space did not go to the request that wants it");
    if (held(m1) || held(m2)) fail("a held block in the way of a request was kept");
    from(0x00021200u, 0x00021300u);
    xbox_HeapFree(m2, XBOX_HEAP_POOL);
    (void)xbox_HeapAlloc(264, 4, XBOX_HEAP_POOL);          /* the rest of that space */

    /* Held while nothing wants its space; free space to the hooks, with no
     * size; and a request too large for its space goes elsewhere. */
    from(0x0002D8D0u, 0x0004AC80u);
    a = xbox_HeapAlloc(64 * 1024, 4096, XBOX_HEAP_VIRTUAL);
    (void)xbox_HeapAlloc(256 * 1024, 4096, XBOX_HEAP_VIRTUAL);   /* above it */
    memset(host(a), 0x5A, 64 * 1024);
    xbox_HeapFree(a, XBOX_HEAP_VIRTUAL);
    if (!xbox_HeapIsFreeSpace(a)) fail("a held block is not free space");
    if (xbox_HeapBlockSize(a)) fail("a held block has a size");
    if (!held(a)) fail("a released block is not held");
    e = xbox_HeapAlloc(128 * 1024, 4096, XBOX_HEAP_VIRTUAL);
    if (e == a || !held(a)) fail("a request too large for a held block's space took it");

    /* A held block at the top: the top comes down past it for the next
     * request from there, as it would without the guard. */
    xbox_HeapFree(e, XBOX_HEAP_VIRTUAL);
    g = xbox_HeapAlloc(128 * 1024, 4096, XBOX_HEAP_VIRTUAL);
    if (g != e) fail("the top did not come down past a held block");

    /* Released twice, from another place: said once. */
    from(0x00032C70u, 0x0002DA70u);
    xbox_HeapFree(a, XBOX_HEAP_VIRTUAL);
    xbox_HeapFree(a, XBOX_HEAP_VIRTUAL);

    /* A read, then a write, of its pages: said, and the test carries on. */
    from(0x0002D950u, 0x0004ACB0u);
    p = host(a + 0x1000);
    seen = *p;
    if (seen != 0x5A) fail("a read of a held page did not carry on with what was there");
    from(0x0002D9D0u, 0x0004ACB0u);
    p = host(a + 0x2010);
    *p = 0x11;
    if (*p != 0x11) fail("a write of a held page did not carry on");

    /* Through the wrong call: a's space is wanted now, so a is let go. */
    from(0x00145A2Fu, 0x00145A51u);
    b = xbox_HeapAlloc(256, 16, XBOX_HEAP_POOL);
    if (b != a || held(a + 4096)) fail("a's space did not go to the next request that fits it");
    xbox_HeapFree(b, XBOX_HEAP_VIRTUAL);

    /* Written after its release: a small block's bytes, which can't be
     * protected, found when it is let go for the next request there. */
    from(0x00030490u, 0x00030690u);
    c = xbox_HeapAlloc(100, 16, XBOX_HEAP_POOL);
    xbox_HeapFree(c, XBOX_HEAP_POOL);
    host(c)[40] ^= 0xFF;
    from(0x00013000u, 0x00013100u);
    d = xbox_HeapAlloc(256, 16, XBOX_HEAP_POOL);
    if (d != c || held(c)) fail("a held pool block's space did not go to the next request");

    /* Inside a block, and of space no block holds; outside the heap is not
     * the heap's to say. */
    xbox_HeapFree(d + 16, XBOX_HEAP_POOL);
    xbox_HeapFree(0x03F00000u, XBOX_HEAP_POOL);
    xbox_HeapFree(0x00010000u, XBOX_HEAP_POOL);
    xbox_HeapFree(d, XBOX_HEAP_POOL);

    /* A request no space can serve fails, as it would without the guard,
     * and the guard says how large a block there was room for. */
    from(0x00014000u, 0x00014100u);
    huge = xbox_HeapAlloc(50500000u, 4096, XBOX_HEAP_VIRTUAL);
    if (huge) fail("a request larger than the heap's room was served");

    /* Releases counted: a line every 4096. */
    from(0x00015000u, 0x00015100u);
    for (i = 0; i < 4096; i++)
        xbox_HeapFree(xbox_HeapAlloc(32, 16, XBOX_HEAP_POOL), XBOX_HEAP_POOL);

    /* And the heap still hands out what it has. */
    again = xbox_HeapAlloc(8 * MB, 4096, XBOX_HEAP_VIRTUAL);
    if (!again) fail("the heap could not serve 8 MB after the guard's work");
    printf("heap_guard: on: a=0x%08X b=0x%08X c=0x%08X d=0x%08X\n", a, b, c, d);
}

/* The trace: the same requests and releases whatever the guard, each
 * address the heap gives printed. Each block is written at both ends, so a
 * page the guard left without access would end the test. */
static uint32_t s_seed;

static uint32_t rnd(uint32_t n)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return (s_seed >> 8) % n;
}

static uint32_t take(uint32_t size, uint32_t align, XboxHeapCall call)
{
    uint32_t va = xbox_HeapAlloc(size, align, call);

    printf("%08X\n", va);
    if (va) {
        host(va)[0] = 0xA5;
        host(va)[size - 1] = 0xA5;
    }
    return va;
}

#define LIVE 512

static void trace(void)
{
    static const uint32_t k_files[] = {   /* the title's, about: sizes in bytes */
        740192, 40964, 228352, 45056, 45056, 266240, 1093632, 524288, 98304,
        1556480, 12288, 335872, 61440, 802816, 24576, 155648
    };
    uint32_t live[LIVE], stage[3] = {0}, file, kept = 0;
    uint32_t sizes[LIVE];
    XboxHeapCall calls[LIVE];
    int n_live = 0, area, f, k, round;

    from(0x00011100u, 0x00011200u);

    /* Areas, as JSRF loads them: the area's files one at a time, each read
     * into a buffer of its own that is released once parsed, with some of
     * the parsed blocks kept; then its stage, 8, 4 and 2 MB; then the next
     * area, with the last one's stage and half its kept blocks released. */
    for (area = 0; area < 6; area++) {
        for (k = 0; k < 3; k++)
            if (stage[k]) xbox_HeapFree(stage[k], XBOX_HEAP_VIRTUAL);
        for (k = 0; k < n_live; k += 2)
            xbox_HeapFree(live[k], XBOX_HEAP_POOL);
        for (k = 1, kept = 0; k < n_live; k += 2)
            live[kept++] = live[k];
        n_live = (int)kept;
        for (f = 0; f < 40; f++) {
            file = take(k_files[(f + area * 7) % 16], 4096, XBOX_HEAP_VIRTUAL);
            for (k = 0; k < 2 + f % 5; k++) {
                uint32_t parsed = take(16 + rnd(3000), k % 2 ? 4 : 16, XBOX_HEAP_POOL);
                if (parsed && k % 3 == 0 && n_live < LIVE) live[n_live++] = parsed;
                else if (parsed) xbox_HeapFree(parsed, XBOX_HEAP_POOL);
            }
            if (file) xbox_HeapFree(file, XBOX_HEAP_VIRTUAL);
        }
        stage[0] = take(8 * MB, 4096, XBOX_HEAP_VIRTUAL);
        stage[1] = take(4165632, 4096, XBOX_HEAP_VIRTUAL);
        stage[2] = take(2 * MB, 4096, XBOX_HEAP_VIRTUAL);
        if (!stage[0] || !stage[1] || !stage[2]) fail("trace: an area's stage could not load");
    }
    for (k = 0; k < 3; k++) xbox_HeapFree(stage[k], XBOX_HEAP_VIRTUAL);
    for (k = 0; k < n_live; k++) xbox_HeapFree(live[k], XBOX_HEAP_POOL);
    n_live = 0;

    /* And a random mix, from three seeds: small pool blocks, page blocks and
     * a few large ones, some released at once, some kept a while, up to 36 MB
     * live, which leaves the heap short of a large block now and then. */
    for (round = 0; round < 3; round++) {
        uint32_t live_bytes = 0;
        s_seed = 0x5EED0000u + (uint32_t)round;
        for (k = 0; k < 8000; k++) {
            uint32_t kind = rnd(100), size, align;
            if (kind < 70)      { size = 16 + rnd(2048);         align = rnd(2) ? 16 : 4; }
            else if (kind < 93) { size = 4096 + rnd(256 * 1024); align = rnd(2) ? 4096 : 16; }
            else                { size = 512 * 1024 + rnd(4 * MB); align = 4096; }
            if (n_live < LIVE && live_bytes + size <= 36 * MB &&
                (n_live == 0 || rnd(100) < 52)) {
                calls[n_live] = align == 4096 ? XBOX_HEAP_VIRTUAL : XBOX_HEAP_POOL;
                live[n_live] = take(size, align, calls[n_live]);
                sizes[n_live] = size;
                if (live[n_live]) live_bytes += sizes[n_live++];
            } else if (n_live) {
                uint32_t which = rnd((uint32_t)n_live);
                xbox_HeapFree(live[which], calls[which]);
                live_bytes -= sizes[which];
                n_live--;
                live[which] = live[n_live];
                calls[which] = calls[n_live];
                sizes[which] = sizes[n_live];
            }
        }
        while (n_live) { n_live--; xbox_HeapFree(live[n_live], calls[n_live]); }
    }
}

int main(int argc, char **argv)
{
    void *ram = VirtualAlloc(NULL, g_xbox_total_ram, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    if (!ram) { printf("heap_guard: no memory\n"); return 1; }
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)ram;
    xbox_HeapInit();                            /* as xbox_MemoryLayoutInit does */
    if (argc > 1 && !strcmp(argv[1], "on"))
        on();
    else if (argc > 1 && !strcmp(argv[1], "trace"))
        trace();
    else
        off();
    fflush(stderr);
    if (s_failures) {
        printf("heap_guard: %d failures\n", s_failures);
        return 1;
    }
    printf("heap_guard: all cases pass\n");
    return 0;
}
