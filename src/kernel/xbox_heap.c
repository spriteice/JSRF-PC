/**
 * xbox_heap.c - The guest heap
 *
 * The allocator behind the kernel's memory calls: NtAllocateVirtualMemory,
 * the pool, system memory, kernel stacks, device objects and I/O mappings,
 * and the runtime's own thread stacks and TIBs. Best fit over an
 * address-ordered block table, with a bump pointer above it, under a lock of
 * its own. Reservations larger than the heap come from the mapped space above
 * RAM (xbox_ReserveAlloc).
 *
 * Moved out of xbox_memory_layout.c so that it can be linked and tested on
 * its own.
 *
 * RECOMP_HEAP_GUARD=1 (Developer > Logging) holds each released block, with
 * its whole pages' access taken away, until the heap hands its space out
 * again, and says when the game releases a block twice, touches one it
 * released, releases one through a call other than the one that allocated
 * it, or writes into one's shared edges after its release. Each line says
 * where in the game's code: the guest return addresses on its stack. Where
 * the heap puts each block is the same with the guard on as off. See "The
 * guard" below.
 */

#include "kernel.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xbox_memory_layout.h"
#include "kernel_bridge.h"

/* Bump allocator for pure address-space reservations, above RAM.
 *
 * A MEM_RESERVE costs no memory on real hardware -- it takes address space out
 * of a 4 GB range, not pages out of the 64 MB the console has -- so titles
 * reserve far more than exists and commit a fraction. Satisfying that out of
 * the RAM heap does not work: Half-Life 2 asks for 128 MB and then 200 MB, and
 * clamping those to what the heap can back left it sub-allocating across a
 * range it believed it owned, walking past the top of RAM and aliasing low
 * memory through the mirrors.
 *
 * So reservations come from the mapped space *above* RAM instead. Those pages
 * are already backed and distinct, nothing else hands them out, and a commit
 * inside one is a no-op because it is real memory already.
 *
 * Returns 0 when the mapping is no larger than RAM -- the default for titles
 * that never call xbox_SetMapSize -- which leaves the old behaviour untouched.
 *
 * ponytail: a bump allocator with no free. A reservation is address space, the
 * range is large, and a title that reserves and releases repeatedly would need
 * a real allocator; none has yet.
 */
static uint32_t g_reserve_next;

uint32_t xbox_ReserveAlloc(uint32_t size, uint32_t align)
{
    uint32_t base;

    if (xbox_GetMappedSize() <= g_xbox_total_ram || size == 0)
        return 0;
    if (!align)
        align = 4096;
    if (!g_reserve_next)
        g_reserve_next = (uint32_t)g_xbox_total_ram;

    base = (g_reserve_next + align - 1) & ~(align - 1);
    if ((size_t)base + size > xbox_GetMappedSize())
        return 0;
    g_reserve_next = base + size;
    return base;
}

/* ── Dynamic heap allocator ────────────────────────────────
 *
 * Simple bump allocator for MmAllocateContiguousMemory and similar.
 * Returns Xbox VAs within the mapped region so MEM32() works correctly.
 * No free support (bump-only for now).
 */
static uint32_t g_heap_next = XBOX_HEAP_BASE;
uint32_t g_heap_next_for_check(void) { return g_heap_next; }

static int g_heap_alloc_count = 0;

/* Block table backing xbox_HeapFree. A bump pointer alone never reclaims,
 * which is fine for a title that allocates once and fatal for a debug build
 * that churns. Flat array rather than an intrusive list: allocations come back
 * in bump order, so index order is address order and coalescing is a
 * neighbour check. */
#define XBOX_HEAP_MAX_BLOCKS 65536
/* A block's state. Held is the guard's: released, but not handed out again
 * yet (guard_hold). */
#define HEAP_LIVE 0
#define HEAP_FREE 1
#define HEAP_HELD 2
static struct {
    uint32_t addr, size;
    uint8_t state, call;
    uint32_t release;   /* a held block's release, by number (g_guard_ring) */
} g_heap_blocks[XBOX_HEAP_MAX_BLOCKS];
static int g_heap_block_count = 0;

/* The block holding the address, or -1. The table is in address order with
 * no overlap (heap_check's rules), so a binary search finds it. */
static int heap_find(uint32_t xbox_va)
{
    int lo = 0, hi = g_heap_block_count - 1, found = -1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_heap_blocks[mid].addr <= xbox_va) { found = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    if (found >= 0 && xbox_va - g_heap_blocks[found].addr >= g_heap_blocks[found].size)
        found = -1;
    return found;
}

/* One thread at a time in the block table and the bump pointer.
 *
 * The guest lock covered this for most callers, as the kernel calls that
 * allocate run under it, but not for all. A new worker allocates its TIB
 * before it takes the guest lock (bridge_thread_main in kernel_bridge.c),
 * while the thread that spawned it, holding the lock, allocates the next
 * worker's stack; the timer thread and the USB controller's thread
 * allocate their TIBs with no lock at all. Two allocations at once read the
 * same bump pointer and return the same address.
 *
 * JSRF hung on a loading screen that way. A worker's 4,500-byte TIB and the
 * next worker's 512 KB stack both came back at 0x0126D000, and the bump
 * pointer kept only the TIB's end, so the stack after that was laid over the
 * second worker's and zeroed. That wiped the start routine off the second
 * worker's stack before its startup code read it: it called address 0 and
 * ended, and it was the worker that serves the game's file reads.
 *
 * A lock of its own rather than the guest lock, so it holds for any caller.
 * Nothing that takes another lock is called with it held. Initialised in
 * xbox_MemoryLayoutInit (xbox_HeapInit), before anything can allocate. */
static CRITICAL_SECTION g_heap_lock;

/* ---- The guard (RECOMP_HEAP_GUARD) ----------------------------------------
 *
 * The heap hands a released block out again at once, so a pointer kept after
 * its release reads and writes whatever was allocated there next, and a
 * second release frees a block that is someone else's by then. Neither is
 * seen where it happens. JSRF's character loader and its graffiti cache
 * builder each kept a buffer's pointer after releasing it and released it
 * again later, when the space could be another allocation's
 * (port/resource_load.c forgets those pointers now).
 *
 * With the guard on, a released block is held instead: kept out of the free
 * list, with the access to its whole pages taken away, until the heap hands
 * its space out again. Then it is let go, and becomes free space as before.
 *
 * Where each block goes stays exactly as it is with the guard off
 * (guard_place). To choose, a held block counts as the free space it would
 * be, run together with its free and held neighbours, and the top is where
 * it would have come down to; the held blocks in the way are let go just
 * before the request takes their space. So the guard can't refuse memory
 * the heap would have given, nor move the game's blocks. It used to hold
 * blocks until 16 MB were held, and that moved them: each file JSRF read at
 * the title took new space, as the last one's buffer was held, the heap's
 * top reached its end in a second, and the space the held buffers were let
 * go from went to the game's small blocks. Its first area after the garage
 * then needed 4 MB in one piece for StgObj13.dat, and with everything let
 * go there was none: the game asked again until it was closed.
 *
 * A block is also let go when its record is the oldest of the last
 * HEAP_GUARD_RECORDS releases and another is made, or when the block table
 * is short of room. While a block is held:
 *
 *   - a release of it is a release twice, and so is a release of an address
 *     that was released before and has not been handed out since, held or
 *     not, while its record is among the last HEAP_GUARD_RECORDS;
 *   - a read or write of its whole pages faults, and the fault handler
 *     (port/main.c) asks xbox_HeapGuardFault, which says so, gives that one
 *     page its access back and lets the game carry on;
 *   - the bytes it shares a page with a neighbour (its edges, which can't be
 *     protected) are checksummed, and checked again when it is let go;
 *
 * and a release through a call other than the one that allocated the block
 * (ExFreePool of NtAllocateVirtualMemory's memory, say) is said when it is
 * made.
 *
 * Each finding names the game's code on the stack where it happened, and
 * for a block released before, where it was released and on which thread;
 * it is said once for each place (guard_first). The lines are made under the
 * heap's lock and written after it, as writing takes the C library's lock.
 *
 * ponytail: the pages are the guest's own view's, not its mirrors' or the
 * tiled aperture's (xbox_memory_layout.c), and a host call that writes into
 * a held page for the game -- a file read into a released buffer -- fails
 * rather than faults, so it is not said. */
#define HEAP_PAGE               4096u
#define HEAP_GUARD_RECORDS      16384        /* releases remembered, held ones among them */
#define HEAP_GUARD_TABLE_ROOM   1024         /* block table entries kept for the game's own */
#define HEAP_GUARD_CHAIN        4            /* guest return addresses kept for a release */
#define HEAP_GUARD_STACK_WORDS  64           /* guest stack words scanned for them */
#define HEAP_GUARD_COUNT_EVERY  4096         /* releases between count lines */
#define HEAP_GUARD_SAID         512          /* places remembered as said */
#define HEAP_GUARD_SAY          4096         /* bytes of lines one call can make */
#define FNV64_OFFSET            0xCBF29CE484222325ull
#define FNV64_PRIME             0x00000100000001B3ull

#define HEAP_HOST(va)     ((uint8_t *)((uintptr_t)(va) + g_xbox_mem_offset))
#define PAGE_UP(va)       (((va) + HEAP_PAGE - 1) & ~(HEAP_PAGE - 1))
#define PAGE_DOWN(va)     ((va) & ~(HEAP_PAGE - 1))

/* One release, as the guard keeps it. */
typedef struct HeapRelease {
    uint32_t addr, size;
    uint32_t chain[HEAP_GUARD_CHAIN];  /* guest return addresses, innermost first */
    uint32_t thread;                   /* the releasing thread's id */
    uint64_t sum;                      /* the edges' checksum (guard_edge_sum) */
    uint8_t  call;                     /* the call that released it */
    uint8_t  by;                       /* the call that allocated it */
    uint8_t  held;                     /* still held */
} HeapRelease;

/* A ring of the last HEAP_GUARD_RECORDS releases; NULL while the guard is
 * off. Release n is g_guard_ring[n % HEAP_GUARD_RECORDS]. None before
 * g_guard_oldest is held; a held block is let go before its record is
 * written over, so every held block's is in the ring. */
static HeapRelease *g_guard_ring;
static uint32_t g_guard_releases, g_guard_oldest;
static uint32_t g_guard_held, g_guard_held_bytes;
static uint32_t g_guard_wanted, g_guard_aged, g_guard_crowded;   /* let go, by why */
static uint32_t g_guard_high;          /* the bump pointer's highest */
static uint64_t g_guard_said[HEAP_GUARD_SAID];
static int g_guard_said_n;

/* What the guard has to say, made under the lock and written after it. */
static RECOMP_TLS char t_say[HEAP_GUARD_SAY];
static RECOMP_TLS int t_said;
static RECOMP_TLS uintptr_t t_retried;  /* xbox_HeapGuardFault's one retry */

/* The calls, by XboxHeapCall, for the lines. */
static const char *const k_alloc_name[XBOX_HEAP_CALLS] = {
    "the runtime", "ExAllocatePool", "NtAllocateVirtualMemory",
    "MmAllocateSystemMemory", "MmCreateKernelStack", "MmMapIoSpace",
    "IoCreateDevice", "MmAllocateContiguousMemory"
};
static const char *const k_free_name[XBOX_HEAP_CALLS] = {
    "the runtime", "ExFreePool", "NtFreeVirtualMemory", "MmFreeSystemMemory",
    "MmDeleteKernelStack", "MmUnmapIoSpace", "IoDeleteDevice",
    "MmFreeContiguousMemory"
};

/* The findings, for guard_first. */
enum { FIND_TWICE, FIND_TOUCHED, FIND_WRONG_CALL, FIND_WRITTEN, FIND_INSIDE,
       FIND_UNKNOWN };

static void guard_say(const char *fmt, ...)
{
    va_list ap;
    int n;

    if (t_said >= HEAP_GUARD_SAY - 1) return;
    va_start(ap, fmt);
    n = vsnprintf(t_say + t_said, (size_t)(HEAP_GUARD_SAY - t_said), fmt, ap);
    va_end(ap);
    if (n > 0) t_said = t_said + n < HEAP_GUARD_SAY - 1 ? t_said + n : HEAP_GUARD_SAY - 1;
}

/* After the lock: write what was said. */
static void guard_flush(void)
{
    if (!t_said) return;
    fputs(t_say, stderr);
    fflush(stderr);
    t_said = 0;
}

static uint64_t fnv_bytes(uint64_t h, const uint8_t *p, size_t n)
{
    while (n--) { h ^= *p++; h *= FNV64_PRIME; }
    return h;
}

/* The bytes of a block that share a page with something else: from its
 * start to the first page boundary, and from the last one to its end; the
 * whole block when it has no whole page. */
static uint64_t guard_edge_sum(uint32_t addr, uint32_t size)
{
    uint32_t end = addr + size, lo = PAGE_UP(addr), hi = PAGE_DOWN(end);
    uint64_t h = FNV64_OFFSET;

    if (lo >= hi) return fnv_bytes(h, HEAP_HOST(addr), size);
    h = fnv_bytes(h, HEAP_HOST(addr), lo - addr);
    return fnv_bytes(h, HEAP_HOST(hi), end - hi);
}

/* The whole pages inside a block, which nothing else shares. */
static void guard_pages(uint32_t addr, uint32_t size, DWORD protect)
{
    uint32_t lo = PAGE_UP(addr), hi = PAGE_DOWN(addr + size);
    DWORD old;

    if (lo < hi) VirtualProtect(HEAP_HOST(lo), hi - lo, protect, &old);
}

/* This thread's guest return addresses, innermost first: the words from
 * esp - 4 (where a kernel call's own return address is, the dispatcher
 * having taken it off) that fall in the game's code. A scan, as
 * print_guest_context's in port/main.c is, so it can name a caller that has
 * returned already; a host thread with no guest stack has none. */
static void guard_chain(uint32_t chain[HEAP_GUARD_CHAIN])
{
    uint32_t from = g_esp - 4, to = from + 4 * HEAP_GUARD_STACK_WORDS;
    int n = 0, k, a, b;

    memset(chain, 0, HEAP_GUARD_CHAIN * sizeof chain[0]);
    if (g_esp < 8 || !g_xbox_mem_offset || g_xbox_code_hi <= g_xbox_code_lo ||
        to < from || to > XBOX_HEAP_TOP)
        return;
    /* A stack in a held block would fault here; one smaller than the scan
     * has no whole page to fault on. */
    a = heap_find(from);
    b = heap_find(to - 1);
    if ((a >= 0 && g_heap_blocks[a].state == HEAP_HELD) ||
        (b >= 0 && g_heap_blocks[b].state == HEAP_HELD))
        return;
    for (k = 0; k < HEAP_GUARD_STACK_WORDS && n < HEAP_GUARD_CHAIN; k++) {
        uint32_t v;
        memcpy(&v, HEAP_HOST(from + 4u * (uint32_t)k), sizeof v);
        if (v > g_xbox_code_lo && v < g_xbox_code_hi) chain[n++] = v;
    }
}

/* "at 0002D8D0 0002E000", or that there was none. */
static const char *guard_chain_text(const uint32_t chain[HEAP_GUARD_CHAIN], char *buf,
                                    size_t room)
{
    size_t used;
    int k;

    if (!chain || !chain[0]) return "with no game code on its stack";
    used = (size_t)snprintf(buf, room, "at");
    for (k = 0; k < HEAP_GUARD_CHAIN && chain[k] && used < room; k++)
        used += (size_t)snprintf(buf + used, room - used, " %08X", chain[k]);
    return buf;
}

/* Whether a finding is new: a kind, the earlier release's place and this
 * one's, by their two innermost return addresses. */
static int guard_first(int kind, const uint32_t *before, const uint32_t *now)
{
    uint32_t key[5];
    uint64_t h;
    int k;

    key[0] = (uint32_t)kind;
    key[1] = before ? before[0] : 0;
    key[2] = before ? before[1] : 0;
    key[3] = now ? now[0] : 0;
    key[4] = now ? now[1] : 0;
    h = fnv_bytes(FNV64_OFFSET, (const uint8_t *)key, sizeof key);
    for (k = 0; k < g_guard_said_n; k++)
        if (g_guard_said[k] == h) return 0;
    if (g_guard_said_n == HEAP_GUARD_SAID) {
        static int full;
        if (!full) {
            full = 1;
            guard_say("[HEAPGUARD] %d places found; any more are not said\n",
                      HEAP_GUARD_SAID);
        }
        return 0;
    }
    g_guard_said[g_guard_said_n++] = h;
    return 1;
}

/* The newest release of an address among those the ring keeps. */
static const HeapRelease *guard_record(uint32_t addr)
{
    uint32_t s, first = g_guard_releases > HEAP_GUARD_RECORDS
                      ? g_guard_releases - HEAP_GUARD_RECORDS : 0;

    for (s = g_guard_releases; s-- > first; )
        if (g_guard_ring[s % HEAP_GUARD_RECORDS].addr == addr)
            return &g_guard_ring[s % HEAP_GUARD_RECORDS];
    return NULL;
}

/* ", released by NtFreeVirtualMemory at ... on thread 4321" */
static void guard_say_release(const char *lead, const HeapRelease *r)
{
    char at[64];

    if (!r) { guard_say("%sno release of it on record", lead); return; }
    guard_say("%s%s %s on thread %u", lead, k_free_name[r->call],
              guard_chain_text(r->chain, at, sizeof at), r->thread);
}

static void heap_release(int i);

/* Let a held block go, by its release's record: its pages back, its edges
 * checked, and free space as any released block is. */
static void guard_let_go(HeapRelease *r)
{
    int i = heap_find(r->addr);

    if (!r->held) return;
    r->held = 0;
    g_guard_held--;
    g_guard_held_bytes -= r->size;
    if (i < 0 || g_heap_blocks[i].addr != r->addr || g_heap_blocks[i].state != HEAP_HELD)
        return;   /* cannot happen: nothing else takes a held block */
    guard_pages(r->addr, r->size, PAGE_READWRITE);
    if (guard_edge_sum(r->addr, r->size) != r->sum && guard_first(FIND_WRITTEN, r->chain, NULL)) {
        guard_say("[HEAPGUARD] written after its release: 0x%08X (%u bytes from %s)",
                  r->addr, r->size, k_alloc_name[r->by]);
        guard_say_release(", released by ", r);
        guard_say(": bytes of it that share a page with another block have changed\n");
    }
    heap_release(i);
}

/* Let held block i go, as the space it is in is handed out. */
static void guard_let_go_block(int i)
{
    g_guard_wanted++;
    guard_let_go(&g_guard_ring[g_heap_blocks[i].release % HEAP_GUARD_RECORDS]);
}

/* Let the oldest held block go; 0 when none is held. */
static int guard_let_go_oldest(void)
{
    if (g_guard_releases - g_guard_oldest > HEAP_GUARD_RECORDS)
        g_guard_oldest = g_guard_releases - HEAP_GUARD_RECORDS;
    while (g_guard_oldest != g_guard_releases &&
           !g_guard_ring[g_guard_oldest % HEAP_GUARD_RECORDS].held)
        g_guard_oldest++;
    if (g_guard_oldest == g_guard_releases)
        return 0;
    guard_let_go(&g_guard_ring[g_guard_oldest % HEAP_GUARD_RECORDS]);
    return 1;
}

/* The top as it would be with the guard off: below the released blocks
 * under the bump pointer, which a release off would have given back to it.
 * *below is the number of blocks under that. */
static uint32_t guard_top(int *below)
{
    uint32_t top = g_heap_next;
    int n = g_heap_block_count;

    while (n > 0 && g_heap_blocks[n - 1].state != HEAP_LIVE &&
           g_heap_blocks[n - 1].addr + g_heap_blocks[n - 1].size == top)
        top = g_heap_blocks[--n].addr;
    *below = n;
    return top;
}

/* The released space from block i on, as one free block it would be off:
 * [*lo, *hi), and the index after it. */
static int guard_run(int i, int below, uint32_t *lo, uint32_t *hi)
{
    *lo = g_heap_blocks[i].addr;
    *hi = *lo + g_heap_blocks[i].size;
    while (i + 1 < below && g_heap_blocks[i + 1].state != HEAP_LIVE &&
           g_heap_blocks[i + 1].addr == *hi)
        *hi += g_heap_blocks[++i].size;
    return i + 1;
}

/* The largest block the heap could hand out, guard or not. */
static uint32_t guard_largest(void)
{
    uint32_t lo, hi, most;
    int below, i = 0;

    most = XBOX_HEAP_TOP - guard_top(&below);
    while (i < below) {
        if (g_heap_blocks[i].state == HEAP_LIVE) { i++; continue; }
        i = guard_run(i, below, &lo, &hi);
        if (hi - lo > most) most = hi - lo;
    }
    return most;
}

/* Where a request goes with the guard on: where it would go off. heap_alloc's
 * best fit, over the runs of released space as they would be off, below the
 * top as it would be. The held blocks in the way are let go: those the block
 * covers, and those that would leave less than 16 bytes between them and it,
 * which heap_alloc would take into the block rather than leave apart; so it
 * is split as it would be off. Returns the free block that heap_alloc is to
 * split, or -1 for the bump pointer, which a block that fits there has
 * brought down to the top it would be at off. */
static int guard_place(uint32_t size, uint32_t alignment)
{
    uint32_t lo = 0, hi = 0, best_lo = 0, best_hi = 0, best_waste = 0xFFFFFFFFu, top, result;
    int below, i = 0, found = 0;

    while (g_heap_block_count > XBOX_HEAP_MAX_BLOCKS - HEAP_GUARD_TABLE_ROOM &&
           guard_let_go_oldest())
        g_guard_crowded++;
    top = guard_top(&below);
    while (i < below) {
        uint32_t start, waste;
        if (g_heap_blocks[i].state == HEAP_LIVE) { i++; continue; }
        i = guard_run(i, below, &lo, &hi);
        start = (lo + alignment - 1) & ~(alignment - 1);
        if (start < lo || start + size > hi || start + size < start)
            continue;
        waste = (hi - lo) - size;
        if (waste < best_waste) { best_waste = waste; found = 1; best_lo = lo; best_hi = hi; }
        if (waste == 0) break;
    }
    if (found) {
        uint32_t start = (best_lo + alignment - 1) & ~(alignment - 1), end = start + size;
        int head_apart = start - best_lo >= 16, tail_apart = best_hi - end >= 16;
        for (i = heap_find(best_lo); i >= 0 && i < g_heap_block_count &&
                                     g_heap_blocks[i].addr < best_hi; ) {
            uint32_t a = g_heap_blocks[i].addr, e = a + g_heap_blocks[i].size;
            int in_the_way =
                (a < end && e > start) ||                                 /* under it */
                (e <= start && (!head_apart || (e != start && start - e < 16))) ||
                (a >= end && (!tail_apart || (a != end && a - end < 16)));
            if (g_heap_blocks[i].state == HEAP_HELD && in_the_way) {
                guard_let_go_block(i);   /* free now, merged with its free neighbours */
                i = heap_find(a);
            }
            i++;
        }
        return heap_find(start);
    }
    /* None: the bump pointer, if the request fits above the top. */
    result = (top + alignment - 1) & ~(alignment - 1);
    if (size <= XBOX_HEAP_TOP - XBOX_HEAP_BASE && result + size >= result &&
        result + size <= XBOX_HEAP_TOP)
        while (g_heap_next != top && g_heap_block_count > 0 &&
               g_heap_blocks[g_heap_block_count - 1].state == HEAP_HELD)
            guard_let_go_block(g_heap_block_count - 1);
    return -1;
}

/* Hold block i, released through call, instead of freeing it. */
static void guard_hold(int i, XboxHeapCall call)
{
    uint32_t addr = g_heap_blocks[i].addr, size = g_heap_blocks[i].size;
    HeapRelease *r = &g_guard_ring[g_guard_releases % HEAP_GUARD_RECORDS];
    char at[64];

    /* Its record first: the oldest kept goes, and its block with it if that
     * is still held, which can move this one's entry. */
    if (r->held) {
        g_guard_aged++;
        guard_let_go(r);
        i = heap_find(addr);
    }
    memset(r, 0, sizeof *r);
    r->addr = addr;
    r->size = size;
    guard_chain(r->chain);
    r->thread = (uint32_t)GetCurrentThreadId();
    r->call = (uint8_t)call;
    r->by = g_heap_blocks[i].call;
    r->held = 1;
    if (r->by != call && guard_first(FIND_WRONG_CALL, NULL, r->chain))
        guard_say("[HEAPGUARD] released through the wrong call: 0x%08X (%u bytes from %s),"
                  " by %s %s on thread %u\n", addr, size, k_alloc_name[r->by],
                  k_free_name[call], guard_chain_text(r->chain, at, sizeof at), r->thread);
    r->sum = guard_edge_sum(addr, size);
    g_heap_blocks[i].state = HEAP_HELD;
    g_heap_blocks[i].release = g_guard_releases;
    guard_pages(addr, size, PAGE_NOACCESS);
    g_guard_releases++;
    g_guard_held++;
    g_guard_held_bytes += size;

    /* The table short of room: the oldest go, but never the one just held. */
    while (g_guard_held > 1 &&
           g_heap_block_count > XBOX_HEAP_MAX_BLOCKS - HEAP_GUARD_TABLE_ROOM &&
           guard_let_go_oldest())
        g_guard_crowded++;
    if (g_guard_releases % HEAP_GUARD_COUNT_EVERY == 0)
        guard_say("[HEAPGUARD] %u releases: %u blocks held (%u KB); let go %u as their space"
                  " was handed out, %u as their records were the oldest, %u for the block"
                  " table's room; the heap's top 0x%08X, at most 0x%08X, of 0x%08X, and"
                  " the largest block it could hand out %u KB\n", g_guard_releases,
                  g_guard_held, g_guard_held_bytes / 1024, g_guard_wanted, g_guard_aged,
                  g_guard_crowded, g_heap_next, g_guard_high, (unsigned)XBOX_HEAP_TOP,
                  guard_largest() / 1024);
}

static int heap_where(uint32_t xbox_va);
enum {
    HEAP_LIVE_START, HEAP_LIVE_INSIDE, HEAP_FREE_START, HEAP_FREE_INSIDE,
    HEAP_ABOVE_LAST, HEAP_NO_BLOCK, HEAP_RESERVED, HEAP_OUTSIDE,
    HEAP_HELD_START, HEAP_HELD_INSIDE
};

/* A release of an address that is not a live block's start. Off, the heap
 * ignores it, and so does the guard, after saying what it was. */
static void guard_stray(uint32_t xbox_va, XboxHeapCall call)
{
    uint32_t now[HEAP_GUARD_CHAIN];
    const HeapRelease *r = NULL;
    char at[64];
    int w = heap_where(xbox_va), i = heap_find(xbox_va);

    if (w == HEAP_RESERVED || w == HEAP_OUTSIDE) return;
    guard_chain(now);
    if (w != HEAP_LIVE_INSIDE)
        r = guard_record(xbox_va);
    if (r) {
        if (!guard_first(FIND_TWICE, r->chain, now)) return;
        guard_say("[HEAPGUARD] released twice: 0x%08X (%u bytes from %s), by %s %s on"
                  " thread %u", xbox_va, r->size, k_alloc_name[r->by], k_free_name[call],
                  guard_chain_text(now, at, sizeof at), (uint32_t)GetCurrentThreadId());
        guard_say_release("; released before by ", r);
        guard_say(r->held ? " (still held)\n" : " (let go since)\n");
    } else if (w == HEAP_LIVE_INSIDE || w == HEAP_HELD_INSIDE) {
        if (!guard_first(FIND_INSIDE, NULL, now)) return;
        guard_say("[HEAPGUARD] a release inside a block: 0x%08X is %u bytes into 0x%08X"
                  " (%u bytes from %s, %s), by %s %s on thread %u\n", xbox_va,
                  xbox_va - g_heap_blocks[i].addr, g_heap_blocks[i].addr,
                  g_heap_blocks[i].size, k_alloc_name[g_heap_blocks[i].call],
                  w == HEAP_HELD_INSIDE ? "released" : "live", k_free_name[call],
                  guard_chain_text(now, at, sizeof at), (uint32_t)GetCurrentThreadId());
    } else {
        if (!guard_first(FIND_UNKNOWN, NULL, now)) return;
        guard_say("[HEAPGUARD] a release of heap space no block holds: 0x%08X, by %s %s on"
                  " thread %u, and none of the last %d releases was of it\n", xbox_va,
                  k_free_name[call], guard_chain_text(now, at, sizeof at),
                  (uint32_t)GetCurrentThreadId(), HEAP_GUARD_RECORDS);
    }
}

void xbox_HeapInit(void)
{
    InitializeCriticalSection(&g_heap_lock);
    if (getenv("RECOMP_HEAP_GUARD") && !g_guard_ring) {
        g_guard_ring = calloc(HEAP_GUARD_RECORDS, sizeof *g_guard_ring);
        g_guard_high = g_heap_next;
        if (g_guard_ring)
            fprintf(stderr, "[HEAPGUARD] on: a released block is held, with its whole pages'"
                            " access taken away, until the heap hands its space out again,"
                            " and the last %d releases are kept; each block goes where it"
                            " would with the guard off\n", HEAP_GUARD_RECORDS);
        else
            fprintf(stderr, "[HEAPGUARD] off: no memory for its records\n");
        fflush(stderr);
    }
}

int xbox_HeapGuardOn(void)
{
    return g_guard_ring != NULL;
}

/* RECOMP_HEAP_CHECK=1: verify the block table after every operation --
 * address order, no overlap, nothing past the bump pointer. */
static void heap_check(const char *where)
{
    static int on = -1;
    extern uint32_t g_heap_next_for_check(void);
    if (on < 0) on = getenv("RECOMP_HEAP_CHECK") ? 1 : 0;
    if (!on) return;
    for (int i = 0; i < g_heap_block_count; i++) {
        uint32_t a = g_heap_blocks[i].addr, e = a + g_heap_blocks[i].size;
        int bad = 0;
        if (g_heap_blocks[i].size == 0) bad = 1;
        if (i + 1 < g_heap_block_count && e > g_heap_blocks[i + 1].addr) bad = 2;
        if (e > g_heap_next_for_check()) bad = 3;
        if (bad) {
            fprintf(stderr, "[HEAPCHK] %s: block %d addr=%08X size=%u state=%d violates rule %d (next=%08X)\n",
                    where, i, a, g_heap_blocks[i].size, g_heap_blocks[i].state, bad,
                    i + 1 < g_heap_block_count ? g_heap_blocks[i + 1].addr : 0);
            fflush(stderr);
            abort();
        }
    }
}

uint32_t g_heap_next_for_check(void);

static uint32_t heap_alloc(uint32_t size, uint32_t alignment, XboxHeapCall call)
{
    uint32_t result;

    if (alignment < 4) alignment = 4;
    heap_check("alloc-entry");

    /* Enforce minimum allocation size.
     * The Xbox D3D8 code sometimes computes resource sizes from GPU
     * capabilities that return 0 (since we don't have real NV2A hardware),
     * resulting in zero-size allocations. With a bump allocator, these all
     * return the same address, causing overlapping structures. Enforce a
     * minimum of 4096 bytes so each allocation gets its own memory. */
    if (size < 16) size = 16;

    /* Reuse a freed block first. Without this the heap only ever grows: Halo's
     * debug build allocates and releases heavily through init, exhausted all
     * 48 MB in 4,726 allocations, and its second D3D CreateDevice then failed
     * with E_OUTOFMEMORY -- which the title reports by clearing
     * global_d3d_device, so the rasterizer asserts and startup stops. */
    /* Best fit over the free list, splitting the remainder off as its own
     * free block. First-fit-whole-block was what ran before, and it handed a
     * 16-byte request the first free block that fit -- a released 4 MB file
     * buffer, say -- so the heap fragmented into a few huge "live" blocks
     * holding tiny objects and JSRF's next 8 MB stage load found 45 MB in use
     * and nothing contiguous. Blocks stay in address order (the split
     * remainder is inserted right after its parent) so coalescing in
     * xbox_HeapFree keeps working by index. */
    /* With the guard on, guard_place makes the same choice over what would be
     * free space without it, and lets go the held blocks in the way. */
    {
        int best = g_guard_ring ? guard_place(size, alignment) : -1;
        uint32_t best_waste = 0xFFFFFFFFu;
        for (int i = 0; !g_guard_ring && i < g_heap_block_count; i++) {
            uint32_t start, end, waste;
            if (g_heap_blocks[i].state != HEAP_FREE || !g_heap_blocks[i].size)
                continue;
            start = (g_heap_blocks[i].addr + alignment - 1) & ~(alignment - 1);
            end = g_heap_blocks[i].addr + g_heap_blocks[i].size;
            if (start < g_heap_blocks[i].addr || start + size > end || start + size < start)
                continue;
            waste = g_heap_blocks[i].size - size;
            if (waste < best_waste) { best_waste = waste; best = i; }
            if (waste == 0) break;
        }
        if (best >= 0) {
            uint32_t start = (g_heap_blocks[best].addr + alignment - 1) & ~(alignment - 1);
            uint32_t bend = g_heap_blocks[best].addr + g_heap_blocks[best].size;
            uint32_t head = start - g_heap_blocks[best].addr;
            uint32_t tail = bend - (start + size);
            int idx = best;
            if (head >= 16 && g_heap_block_count < XBOX_HEAP_MAX_BLOCKS) {
                /* leading alignment slack stays free, block moves after it */
                memmove(&g_heap_blocks[best + 2], &g_heap_blocks[best + 1],
                        (size_t)(g_heap_block_count - best - 1) * sizeof g_heap_blocks[0]);
                g_heap_block_count++;
                g_heap_blocks[best].size = head;
                g_heap_blocks[best].state = HEAP_FREE;
                idx = best + 1;
                g_heap_blocks[idx].addr = start;
                g_heap_blocks[idx].size = size + tail;
                g_heap_blocks[idx].state = HEAP_FREE;
            } else if (head) {
                /* too small to track: absorb into this block */
                size += head;
                start = g_heap_blocks[best].addr;
                tail = bend - (start + size);
            }
            if (tail >= 16 && g_heap_block_count < XBOX_HEAP_MAX_BLOCKS) {
                memmove(&g_heap_blocks[idx + 2], &g_heap_blocks[idx + 1],
                        (size_t)(g_heap_block_count - idx - 1) * sizeof g_heap_blocks[0]);
                g_heap_block_count++;
                g_heap_blocks[idx + 1].addr = start + size;
                g_heap_blocks[idx + 1].size = tail;
                g_heap_blocks[idx + 1].state = HEAP_FREE;
                g_heap_blocks[idx].size = size;
            }
            g_heap_blocks[idx].addr = start;
            g_heap_blocks[idx].state = HEAP_LIVE;
            g_heap_blocks[idx].call = (uint8_t)call;
            result = start;
            memset((void *)((uintptr_t)result + g_xbox_mem_offset), 0, g_heap_blocks[idx].size);
            heap_check("alloc-reuse");
            return result;
        }
    }

    /* Align the next pointer */
    result = (g_heap_next + alignment - 1) & ~(alignment - 1);

    /* Three ways this request cannot be served, and the middle one used to
     * crash rather than fail.
     *
     * `result + size > XBOX_HEAP_TOP` is the honest bound, but it is computed
     * in 32-bit unsigned arithmetic and XBOX_HEAP_TOP is the top of the RAM
     * mirror -- 1856 MB, or 0x74000000. A size near 0xFFFFFFFF makes
     * result + size WRAP to just below result, the comparison says the block
     * fits, and the memset below then zeroes four gigabytes from the top of
     * the heap. It walks off the end of the mapping and faults on the first
     * unmapped page, which is 0x74000000 exactly -- the crash reported as
     * "signal 10 (si_code 1: misaligned access), fault addr=0x374000000 in
     * __bzero", from JSRF's VirtualAlloc wrapper at sub_00145A2F while it
     * builds the level cache.
     *
     * The diagnostic for a corrupt size was already written, a dozen lines
     * below -- it just sat inside the branch the overflow skipped.
     *
     * Failing here is safe: the guest's own wrapper handles a NULL return
     * (sub_00145A51 sets the error and returns zero), which is what the
     * console would do with a request this size. The reuse path above already
     * guards its own arithmetic with `start + size < start`; this is the same
     * check the bump path was missing. */
    if (size > XBOX_HEAP_TOP - XBOX_HEAP_BASE ||
        result + size < result ||
        result + size > XBOX_HEAP_TOP) {
        fprintf(stderr, "xbox_HeapAlloc: cannot serve %u bytes (0x%08X) at "
                "0x%08X align %u -- used %u/%u%s\n",
                size, size, result, alignment, g_heap_next - XBOX_HEAP_BASE,
                (unsigned)(XBOX_HEAP_TOP - XBOX_HEAP_BASE),
                (result + size < result) ? "  [the size overflows the address"
                                           " space: a corrupt request]" : "");
        /* The guard's: how large a block there was room for, which "used"
         * (the top) doesn't say. Off, the request fails just the same. */
        if (g_guard_ring)
            guard_say("[HEAPGUARD] cannot serve %u bytes: the largest block the heap could"
                      " hand out is %u KB, and %u blocks are held (%u KB)\n", size,
                      guard_largest() / 1024, g_guard_held, g_guard_held_bytes / 1024);
        /* A request larger than the console ever had is not an exhausted heap,
         * it is a corrupt size -- and then the block histogram below answers a
         * question nobody asked. What is wanted is who asked for it, so scan
         * the caller's stack for code addresses and name the callers. */
        if (size > (XBOX_HEAP_TOP - XBOX_HEAP_BASE)) {
            const uint32_t *sp = (const uint32_t *)((uint8_t *)g_xbox_mem_offset
                                                    + g_esp);
            int i, shown = 0;
            fprintf(stderr, "  request exceeds the whole heap: corrupt size."
                            " Guest callers:\n");
            for (i = 0; i < 400 && shown < 24; i++)
                if (sp[i] > 0x11000 && sp[i] < 0x1C4000) {
                    fprintf(stderr, "    [esp+%03X] %08X\n", i * 4, sp[i]);
                    shown++;
                }
            fflush(stderr);
        }
        /* Who ate the heap? Group live blocks by size -- an exhausted heap is
         * nearly always one request size repeated, and the count names it. */
        {
            static int dumped = 0;
            static struct { uint32_t size; int n; } hist[256];
            if (!dumped) {
                int used = 0;
                dumped = 1;
                for (int i = 0; i < g_heap_block_count; i++) {
                    int j = 0;
                    if (g_heap_blocks[i].state != HEAP_LIVE || !g_heap_blocks[i].size) continue;
                    while (j < used && hist[j].size != g_heap_blocks[i].size) j++;
                    if (j == used) {
                        if (used == 256) continue;   /* ponytail: 256 distinct sizes is plenty */
                        hist[used].size = g_heap_blocks[i].size;
                        hist[used++].n = 0;
                    }
                    hist[j].n++;
                }
                for (int j = 0; j < used; j++) {
                    if ((uint64_t)hist[j].n * hist[j].size < 1024 * 1024) continue;
                    fprintf(stderr, "  [HEAP] %d live blocks of %u bytes (%u KB)\n",
                            hist[j].n, hist[j].size,
                            (unsigned)((uint64_t)hist[j].n * hist[j].size / 1024));
                }
                fflush(stderr);
            }
        }
        return 0;
    }

    g_heap_next = result + size;
    if (g_guard_ring && g_heap_next > g_guard_high) g_guard_high = g_heap_next;

    /* Zero-fill the allocated block (Xbox memory is always zeroed) */
    memset((void *)((uintptr_t)result + g_xbox_mem_offset), 0, size);

    if (g_heap_block_count < XBOX_HEAP_MAX_BLOCKS) {
        g_heap_blocks[g_heap_block_count].addr = result;
        g_heap_blocks[g_heap_block_count].size = size;
        g_heap_blocks[g_heap_block_count].state = HEAP_LIVE;
        g_heap_blocks[g_heap_block_count].call = (uint8_t)call;
        g_heap_block_count++;
    }

    g_heap_alloc_count++;
    /* Rate-limited: a debug title makes thousands of these and the log is a
     * diagnostic, not a transaction record. */
    if (g_heap_alloc_count <= 32 || (g_heap_alloc_count % 512) == 0) {
        fprintf(stderr, "  [HEAP] #%d: size=%u align=%u → 0x%08X..0x%08X (used %u/%u)\n",
                g_heap_alloc_count, size, alignment, result, result + size,
                g_heap_next - XBOX_HEAP_BASE,
                (unsigned)(XBOX_HEAP_TOP - XBOX_HEAP_BASE));
        fflush(stderr);
    }

    return result;
}

uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment, XboxHeapCall call)
{
    uint32_t va;

    EnterCriticalSection(&g_heap_lock);
    va = heap_alloc(size, alignment, call);
    LeaveCriticalSection(&g_heap_lock);
    guard_flush();
    return va;
}

/* How big is the block at this guest address?
 *
 * MmQueryAllocationSize and ExQueryPoolBlockSize both ask this, and both used
 * to answer 0 -- ExQueryPoolBlockSize by returning a literal, and
 * MmQueryAllocationSize by having no bridge at all. The host cannot answer it:
 * VirtualQuery on the translated address reports the size of the whole 64 MB
 * guest mapping, which is a worse answer than none. The block table already
 * has the real one, and it is the same table xbox_HeapFree matches against.
 *
 * Interior addresses count: a title that asks about a pointer it has walked
 * forward is asking about the block that contains it. Returns 0 for an address
 * this heap never handed out, which is what "not one of mine" has to look like.
 */
uint32_t xbox_HeapBlockSize(uint32_t xbox_va)
{
    uint32_t n = 0;
    int i;

    if (!xbox_va)
        return 0;
    EnterCriticalSection(&g_heap_lock);
    i = heap_find(xbox_va);
    if (i >= 0 && g_heap_blocks[i].state == HEAP_LIVE)
        n = g_heap_blocks[i].size - (xbox_va - g_heap_blocks[i].addr);
    LeaveCriticalSection(&g_heap_lock);
    return n;
}

/* Where an address stands with the heap (the enum above guard_stray), as
 * an index into heap_where_text. A block the guard holds is free space to
 * xbox_HeapIsFreeSpace, as it is a released one. */
static const char *const heap_where_text[] = {
    "a live heap block", "inside a live heap block",
    "the start of free heap space", "inside free heap space",
    "in free heap space above the last block", "in the heap but in no block",
    "in a reservation above RAM", "outside the heap",
    "a released heap block the guard holds", "inside a released heap block the guard holds"
};

static int heap_where(uint32_t xbox_va)
{
    int i = heap_find(xbox_va);

    if (i >= 0) {
        int start = xbox_va == g_heap_blocks[i].addr;
        if (g_heap_blocks[i].state == HEAP_FREE)
            return start ? HEAP_FREE_START : HEAP_FREE_INSIDE;
        if (g_heap_blocks[i].state == HEAP_HELD)
            return start ? HEAP_HELD_START : HEAP_HELD_INSIDE;
        return start ? HEAP_LIVE_START : HEAP_LIVE_INSIDE;
    }
    /* xbox_ReserveAlloc's range shares the top of the mapping with the heap. */
    if (xbox_va >= g_xbox_total_ram && xbox_va < g_reserve_next)
        return HEAP_RESERVED;
    if (xbox_va >= XBOX_HEAP_BASE && xbox_va < XBOX_HEAP_TOP)
        return xbox_va >= g_heap_next ? HEAP_ABOVE_LAST : HEAP_NO_BLOCK;
    return HEAP_OUTSIDE;
}

const char *xbox_HeapDescribe(uint32_t xbox_va)
{
    int w;

    EnterCriticalSection(&g_heap_lock);
    w = heap_where(xbox_va);
    LeaveCriticalSection(&g_heap_lock);
    return heap_where_text[w];
}

int xbox_HeapIsFreeSpace(uint32_t xbox_va)
{
    int w;

    EnterCriticalSection(&g_heap_lock);
    w = heap_where(xbox_va);
    LeaveCriticalSection(&g_heap_lock);
    return w != HEAP_LIVE_START && w != HEAP_LIVE_INSIDE &&
           w != HEAP_RESERVED && w != HEAP_OUTSIDE;
}

/* Give block i back: free, merged with its free neighbours, and its space
 * handed back to the bump pointer when it is the topmost. */
static void heap_release(int i)
{
    g_heap_blocks[i].state = HEAP_FREE;

    /* Coalesce with neighbours. Blocks are kept in address order, so
     * adjacency is an end==start test on the next/previous entry. Merged
     * entries are removed from the table (not zeroed in place -- a zeroed
     * entry between two free blocks used to stop them ever merging). */
    if (i + 1 < g_heap_block_count && g_heap_blocks[i + 1].state == HEAP_FREE &&
        g_heap_blocks[i].addr + g_heap_blocks[i].size == g_heap_blocks[i + 1].addr) {
        g_heap_blocks[i].size += g_heap_blocks[i + 1].size;
        memmove(&g_heap_blocks[i + 1], &g_heap_blocks[i + 2],
                (size_t)(g_heap_block_count - i - 2) * sizeof g_heap_blocks[0]);
        g_heap_block_count--;
    }
    if (i > 0 && g_heap_blocks[i - 1].state == HEAP_FREE &&
        g_heap_blocks[i - 1].addr + g_heap_blocks[i - 1].size == g_heap_blocks[i].addr) {
        g_heap_blocks[i - 1].size += g_heap_blocks[i].size;
        memmove(&g_heap_blocks[i], &g_heap_blocks[i + 1],
                (size_t)(g_heap_block_count - i - 1) * sizeof g_heap_blocks[0]);
        g_heap_block_count--;
        i--;
    }
    /* A free block at the very top of the bump region gives its space
     * back to the bump pointer, so big requests can come from there. */
    if (i == g_heap_block_count - 1 &&
        g_heap_blocks[i].addr + g_heap_blocks[i].size == g_heap_next) {
        g_heap_next = g_heap_blocks[i].addr;
        g_heap_block_count--;
    }
}

static void heap_free(uint32_t xbox_va, XboxHeapCall call)
{
    static int frees = 0, matched = 0;
    int i;

    if (!xbox_va) {
        return;
    }
    heap_check("free-entry");
    frees++;
    if (frees <= 8) {
        fprintf(stderr, "  [HEAP] free #%d va=0x%08X blocks=%d\n",
                frees, xbox_va, g_heap_block_count);
        fflush(stderr);
    }
    i = heap_find(xbox_va);
    if (i < 0 || g_heap_blocks[i].addr != xbox_va || g_heap_blocks[i].state != HEAP_LIVE) {
        if (g_guard_ring)
            guard_stray(xbox_va, call);
        return;
    }
    if (++matched % 512 == 0) {
        fprintf(stderr, "  [HEAP] frees=%d matched=%d blocks=%d\n",
                frees, matched, g_heap_block_count);
        fflush(stderr);
    }
    if (g_guard_ring)
        guard_hold(i, call);
    else
        heap_release(i);
}

void xbox_HeapFree(uint32_t xbox_va, XboxHeapCall call)
{
    EnterCriticalSection(&g_heap_lock);
    heap_free(xbox_va, call);
    LeaveCriticalSection(&g_heap_lock);
    guard_flush();
}

/* A fault at a native address (port/main.c's handlers): 1 when it was the
 * guard's -- a held block's page, said and given its access back, or a page
 * that may have been let go since the fault, tried once more -- and the
 * faulting access can run again, 0 when it is a crash after all. */
int xbox_HeapGuardFault(uintptr_t native, int access, const char *where)
{
    static const char *const k_access[] = { "read", "write", "read or write" };
    uint32_t va, page, addr, end;
    int i, handled = 0;

    if (!g_guard_ring || native < (uintptr_t)g_xbox_mem_offset ||
        native - (uintptr_t)g_xbox_mem_offset >= XBOX_HEAP_TOP)
        return 0;
    va = (uint32_t)(native - (uintptr_t)g_xbox_mem_offset);
    if (va < XBOX_HEAP_BASE)
        return 0;
    page = PAGE_DOWN(va);
    EnterCriticalSection(&g_heap_lock);
    i = heap_find(va);
    if (i >= 0 && g_heap_blocks[i].state == HEAP_HELD) {
        addr = g_heap_blocks[i].addr;
        end = addr + g_heap_blocks[i].size;
        if (page >= PAGE_UP(addr) && page + HEAP_PAGE <= PAGE_DOWN(end)) {
            const HeapRelease *r =
                &g_guard_ring[g_heap_blocks[i].release % HEAP_GUARD_RECORDS];
            uint32_t now[HEAP_GUARD_CHAIN];
            char at[64];
            DWORD old;
            guard_chain(now);
            if (guard_first(FIND_TOUCHED, r ? r->chain : NULL, now)) {
                guard_say("[HEAPGUARD] a %s of released memory: 0x%08X, %u bytes into 0x%08X"
                          " (%u bytes from %s), from %s %s on thread %u",
                          k_access[access < 0 || access > 2 ? 2 : access], va, va - addr,
                          addr, end - addr, k_alloc_name[g_heap_blocks[i].call],
                          where ? where : "?", guard_chain_text(now, at, sizeof at),
                          (uint32_t)GetCurrentThreadId());
                guard_say_release("; released by ", r);
                guard_say(". The page is given its access back and the game carries on\n");
            }
            VirtualProtect(HEAP_HOST(page), HEAP_PAGE, PAGE_READWRITE, &old);
            handled = 1;
        }
    }
    LeaveCriticalSection(&g_heap_lock);
    guard_flush();
    if (handled) {
        t_retried = 0;
        return 1;
    }
    /* Not held now, but it may have been at the fault and let go before the
     * lock was had: the access once more, and a crash if it faults again. */
    if (t_retried != native) {
        t_retried = native;
        return 1;
    }
    t_retried = 0;
    return 0;
}
