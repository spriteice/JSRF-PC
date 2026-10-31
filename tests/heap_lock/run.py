"""Compile the guest heap and allocate from it on several threads at once.

Takes the heap's state and lock (g_heap_next to heap_check) and the allocator
(heap_alloc to xbox_HeapFree, the lookups between included) from
src/kernel/xbox_memory_layout.c, verbatim, over a 64 MB buffer. Four threads
allocate, fill and free blocks of mixed sizes at once, as a new worker
allocating its TIB does beside the thread allocating the next worker's stack,
and every block must still hold what its thread wrote when it is freed. With
RECOMP_HEAP_CHECK=1, the block table is checked after every operation too.
Then the same without the lock, which must lose. POSIX only (the threads and
the lock are pthreads'); requires GCC on PATH.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("heap_lock: skipped, POSIX only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
layout = (root / "src/kernel/xbox_memory_layout.c").read_text(encoding="utf-8")


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


state = cut(layout, "static uint32_t g_heap_next = XBOX_HEAP_BASE;",
            "/*\n * Simulated stacks for spawned threads.")
heap = cut(layout, "static uint32_t heap_alloc(uint32_t size, uint32_t alignment)",
           "HANDLE xbox_GetMappingHandle(void)")


def source(locked):
    return ("""
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef pthread_mutex_t CRITICAL_SECTION;
static void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(cs, &attr);
    pthread_mutexattr_destroy(&attr);
}
""" + ("""
#define EnterCriticalSection(cs) pthread_mutex_lock(cs)
#define LeaveCriticalSection(cs) pthread_mutex_unlock(cs)
""" if locked else """
#define EnterCriticalSection(cs) ((void)(cs))
#define LeaveCriticalSection(cs) ((void)(cs))
""") + """
#define XBOX_HEAP_BASE 0x00F80000u
#define XBOX_HEAP_TOP  0x04000000u
static ptrdiff_t g_memory_offset;
static ptrdiff_t g_xbox_mem_offset;
static uint32_t g_esp;
static size_t g_xbox_total_ram = XBOX_HEAP_TOP;
static uint32_t g_reserve_next;
""" + state + "uint32_t g_heap_next_for_check(void);\n\n" + heap
        + (root / "tests/heap_lock/test_main.c").read_text(encoding="utf-8"))


with tempfile.TemporaryDirectory(prefix="heap_lock-") as directory:
    results = {}
    for locked in (True, False):
        exe = str(pathlib.Path(directory) / ("locked" if locked else "unlocked"))
        command = ["gcc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Wno-unused-function",
                   "-x", "c", "-", "-o", exe, "-pthread"]
        subprocess.run(command, cwd=root, input=source(locked), text=True, check=True)
        try:
            run = subprocess.run([exe], capture_output=True, text=True, timeout=300,
                                 env=dict(os.environ, RECOMP_HEAP_CHECK="1"))
            said = (run.stdout.strip().splitlines()
                    or [line for line in run.stderr.splitlines() if "HEAPCHK" in line]
                    or [f"exit {run.returncode}"])
            results[locked] = run.returncode
        except subprocess.TimeoutExpired:
            said, results[locked] = ["no end in 300 s"], -1
        print(("with the lock: " if locked else "without it: ") + said[-1])
    if results[True] != 0:
        raise SystemExit(f"heap_lock: exit {results[True]} with the lock")
    if results[False] == 0:
        raise SystemExit("heap_lock: the heap held without the lock too, so this tests nothing")
    print("heap_lock: all cases pass")
