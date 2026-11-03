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
 */

#include "kernel.h"
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
static struct { uint32_t addr; uint32_t size; uint8_t free; }
    g_heap_blocks[XBOX_HEAP_MAX_BLOCKS];
static int g_heap_block_count = 0;

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

void xbox_HeapInit(void)
{
    InitializeCriticalSection(&g_heap_lock);
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
            fprintf(stderr, "[HEAPCHK] %s: block %d addr=%08X size=%u free=%d violates rule %d (next=%08X)\n",
                    where, i, a, g_heap_blocks[i].size, g_heap_blocks[i].free, bad,
                    i + 1 < g_heap_block_count ? g_heap_blocks[i + 1].addr : 0);
            fflush(stderr);
            abort();
        }
    }
}

uint32_t g_heap_next_for_check(void);

static uint32_t heap_alloc(uint32_t size, uint32_t alignment)
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
    {
        int best = -1;
        uint32_t best_waste = 0xFFFFFFFFu;
        static int no_reuse = -1;
        if (no_reuse < 0) no_reuse = getenv("RECOMP_HEAP_NO_REUSE") ? 1 : 0;
        for (int i = 0; !no_reuse && i < g_heap_block_count; i++) {
            uint32_t start, end, waste;
            if (!g_heap_blocks[i].free || !g_heap_blocks[i].size)
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
                g_heap_blocks[best].free = 1;
                idx = best + 1;
                g_heap_blocks[idx].addr = start;
                g_heap_blocks[idx].size = size + tail;
                g_heap_blocks[idx].free = 1;
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
                g_heap_blocks[idx + 1].free = 1;
                g_heap_blocks[idx].size = size;
            }
            g_heap_blocks[idx].addr = start;
            g_heap_blocks[idx].free = 0;
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
                    if (g_heap_blocks[i].free || !g_heap_blocks[i].size) continue;
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

    /* Zero-fill the allocated block (Xbox memory is always zeroed) */
    memset((void *)((uintptr_t)result + g_xbox_mem_offset), 0, size);

    if (g_heap_block_count < XBOX_HEAP_MAX_BLOCKS) {
        g_heap_blocks[g_heap_block_count].addr = result;
        g_heap_blocks[g_heap_block_count].size = size;
        g_heap_blocks[g_heap_block_count].free = 0;
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

uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment)
{
    uint32_t va;

    EnterCriticalSection(&g_heap_lock);
    va = heap_alloc(size, alignment);
    LeaveCriticalSection(&g_heap_lock);
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
    for (i = 0; i < g_heap_block_count; i++) {
        if (g_heap_blocks[i].free)
            continue;
        if (xbox_va >= g_heap_blocks[i].addr &&
            xbox_va <  g_heap_blocks[i].addr + g_heap_blocks[i].size) {
            n = g_heap_blocks[i].size - (xbox_va - g_heap_blocks[i].addr);
            break;
        }
    }
    LeaveCriticalSection(&g_heap_lock);
    return n;
}

/* Where an address stands with the heap, as an index into heap_where_text. */
enum {
    HEAP_LIVE_START, HEAP_LIVE_INSIDE, HEAP_FREE_START, HEAP_FREE_INSIDE,
    HEAP_ABOVE_LAST, HEAP_NO_BLOCK, HEAP_RESERVED, HEAP_OUTSIDE
};

static const char *const heap_where_text[] = {
    "a live heap block", "inside a live heap block",
    "the start of free heap space", "inside free heap space",
    "in free heap space above the last block", "in the heap but in no block",
    "in a reservation above RAM", "outside the heap"
};

static int heap_where(uint32_t xbox_va)
{
    int i;

    for (i = 0; i < g_heap_block_count; i++) {
        uint32_t a = g_heap_blocks[i].addr;
        if (xbox_va < a || xbox_va >= a + g_heap_blocks[i].size)
            continue;
        if (g_heap_blocks[i].free)
            return xbox_va == a ? HEAP_FREE_START : HEAP_FREE_INSIDE;
        return xbox_va == a ? HEAP_LIVE_START : HEAP_LIVE_INSIDE;
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
    g_heap_blocks[i].free = 1;

    /* Coalesce with neighbours. Blocks are kept in address order, so
     * adjacency is an end==start test on the next/previous entry. Merged
     * entries are removed from the table (not zeroed in place -- a zeroed
     * entry between two free blocks used to stop them ever merging). */
    if (i + 1 < g_heap_block_count && g_heap_blocks[i + 1].free &&
        g_heap_blocks[i].addr + g_heap_blocks[i].size == g_heap_blocks[i + 1].addr) {
        g_heap_blocks[i].size += g_heap_blocks[i + 1].size;
        memmove(&g_heap_blocks[i + 1], &g_heap_blocks[i + 2],
                (size_t)(g_heap_block_count - i - 2) * sizeof g_heap_blocks[0]);
        g_heap_block_count--;
    }
    if (i > 0 && g_heap_blocks[i - 1].free &&
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

static void heap_free(uint32_t xbox_va)
{
    static int frees = 0, matched = 0;

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
    for (int i = 0; i < g_heap_block_count; i++) {
        if (g_heap_blocks[i].addr != xbox_va || g_heap_blocks[i].free) {
            continue;
        }
        if (++matched % 512 == 0) {
            fprintf(stderr, "  [HEAP] frees=%d matched=%d blocks=%d\n",
                    frees, matched, g_heap_block_count);
            fflush(stderr);
        }
        heap_release(i);
        return;
    }
}

void xbox_HeapFree(uint32_t xbox_va)
{
    EnterCriticalSection(&g_heap_lock);
    heap_free(xbox_va);
    LeaveCriticalSection(&g_heap_lock);
}
