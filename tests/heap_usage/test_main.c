/*
 * tests/heap_usage: the heap's figures for the minute's [MEM] line
 * (xbox_heap_usage) and its RECOMP_MMTRACE lines, from src/kernel/xbox_heap.c
 * as it is. See run.py.
 */
#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xbox_memory_layout.h"
#include "kernel_bridge.h"

/* What the runtime gives the heap, as tests/heap_guard gives it. */
RECOMP_TLS uint32_t g_esp;
ptrdiff_t g_xbox_mem_offset;
size_t g_xbox_total_ram = 0x04000000u;
size_t g_xbox_map_size;
size_t xbox_GetMappedSize(void) { return g_xbox_total_ram; }
uint32_t g_xbox_code_lo = 0x00011000u, g_xbox_code_hi = 0x001C4000u;

/* The engine's clock and the guest's caller, which the heap asks for weakly:
 * here, so the lines show they are passed on. */
double host_info_seconds(void) { return 12.5; }
uint32_t kernel_guest_caller(void) { return 0x00123456u; }

static void usage(const char *when)
{
    char out[512];
    xbox_heap_usage(out, sizeof out);
    printf("%s: %s\n", when, out);
}

int main(void)
{
    void *ram = VirtualAlloc(NULL, g_xbox_total_ram, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    uint32_t a, b, c, d;
    char small[40], tiny[20];

    if (!ram) { printf("no memory\n"); return 1; }
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)ram;
    xbox_HeapInit();

    usage("start");
    a = xbox_HeapAlloc(4096, 16, XBOX_HEAP_POOL);
    b = xbox_HeapAlloc(8192, 16, XBOX_HEAP_POOL);
    c = xbox_HeapAlloc(4096, 16, XBOX_HEAP_POOL);
    d = xbox_HeapAlloc(16384, 4096, XBOX_HEAP_SYSTEM);
    if (!a || !b || !c || !d) { printf("an allocation failed\n"); return 1; }
    usage("allocated");
    xbox_HeapFree(b, XBOX_HEAP_POOL);
    xbox_HeapFree(d, XBOX_HEAP_SYSTEM);
    usage("freed");
    xbox_HeapFree(d, XBOX_HEAP_SYSTEM);          /* not live: counted once */
    usage("freed twice");
    b = xbox_HeapAlloc(4096, 16, XBOX_HEAP_POOL); /* the peak stays */
    usage("again");
    xbox_HeapFree(a, XBOX_HEAP_POOL);
    xbox_HeapFree(b, XBOX_HEAP_POOL);
    xbox_HeapFree(c, XBOX_HEAP_POOL);
    usage("all freed");

    /* Too small for the line: whole calls, then "...", never past its end. */
    xbox_heap_usage(small, sizeof small);
    printf("cut: %zu [%s]\n", strlen(small), small);
    xbox_heap_usage(tiny, sizeof tiny);
    printf("cut to none: %zu [%s]\n", strlen(tiny), tiny);
    return 0;
}
