"""Compile the GPU thread and the game's side of the pushbuffer handshake, and
drive them without the game.

Cut by text: from src/kernel/xbox_memory_layout.c, src/kernel/kernel_mmio.c
and templates/runtime/recomp_types.h. xbox_memory_layout.c doesn't link on
its own. One change: Sleep is sched_yield, which is what the GPU thread's
Sleep(0) does on Windows.

Takes nv2a_ack_thread, its wake (xbox_nv2a_kick and _hurry), the busy-bit
tables and the queue (RECOMP_NV2A_QUEUE) from src/kernel/xbox_memory_layout.c,
recomp_mmio_read and _write from src/kernel/kernel_mmio.c, and the device
window from templates/runtime/recomp_types.h, verbatim, and builds
src/kernel/nv2a_pb_scan.c, the walk, as it is; the executor is a stand-in in
which one command takes 30 ms. Runs it with the queue off and on: off,
KickOff's flush waits for the walk; on, the flush is answered at the write,
and DMA_GET is at each command as it runs (D3D patches a NOP ahead of GET to
be woken when the GPU reaches it, so GET must never be behind what the walk
has read). Then D3D's wait for the GPU against the walk, and a wrap with most
of a 512 KB ring behind it, all of which must be walked. recomp_mmio_read and
_write ask the NV2A's interrupt registers first, so src/kernel/nv2a_intr.c is
built as it is too (and src/platform/win32_compat.c, which it needs): none of
these registers is one of its. POSIX only (the wait is pthreads'); requires
GCC on PATH.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("nv2a_queue: skipped, POSIX only")
    sys.exit(77)   # skipped (tests/run_all.py)

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
layout = (root / "src/kernel/xbox_memory_layout.c").read_text(encoding="utf-8")
mmio = (root / "src/kernel/kernel_mmio.c").read_text(encoding="utf-8")
types = (root / "templates/runtime/recomp_types.h").read_text(encoding="utf-8")


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


source = ("""
#define _GNU_SOURCE
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef uint32_t DWORD;
typedef void *LPVOID;
typedef long LONG;
#define WINAPI
#define XBOX_NV2A_BASE 0xFD000000u
#define XBOX_NV2A_SIZE (16u * 1024u * 1024u)
#define XBOX_CONTIG_BASE 0x80000000u
#define XBOX_KERNEL_DATA_BASE 0u
#define KDATA_TICK_COUNT 0u
#define InterlockedCompareExchange(p, x, c) __sync_val_compare_and_swap((p), (c), (x))
#define Sleep(ms) sched_yield()
static void *g_nv2a_memory;
static uintptr_t g_memory_offset;
static uint32_t s_tick_sink;
static volatile LONG g_nv2a_ack_stop;
static void *g_mcpx_regs;
static const uint32_t MCPX_COUNTERS[] = { 0x020010u };
static int s_nv2a_trace;
static DWORD GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
/* What src/kernel/nv2a_intr.c needs, for interrupts no case raises. */
ptrdiff_t g_xbox_mem_offset;
uint32_t xbox_GetConnectedInterrupt(uint32_t vector) { (void)vector; return 0; }
int kernel_raise_interrupt(uint32_t vector) { (void)vector; return -1; }
int xbox_nv2a_intr_read(uint32_t addr, unsigned size, uint32_t *val);
int xbox_nv2a_intr_write(uint32_t addr, uint32_t val, unsigned size);
static void fence_mirrors_tick(void) { }
static void counter_mirrors_tick(void) { }
static void frame_counters_tick(void) { }
static void framebuffer_probe_tick(void) { }
ptrdiff_t xbox_GetMemoryOffset(void);
void xbox_Nv2aRegisterWritten(uint32_t addr, unsigned size);
"""
          + cut(layout, "#include <pthread.h>\n#include <sched.h>\nstatic int s_kick_on",
                "/*\n * NV2A busy-bit acknowledgement.")
          + cut(layout, "static const struct { uint32_t offset; uint32_t busy_mask; } NV2A_ACK[]",
                "/*\n * PFIFO channel DMA pointers.")
          + cut(layout, "#define NV2A_USER_DMA_PUT", "/*\n * Free-running counters in the MCPX aperture.")
          + cut(types, "#ifndef RECOMP_MMIO_LO", "static inline volatile uint8_t *recomp_p8")
          + mmio[mmio.index("uint32_t (*g_recomp_mmio_read_fn)"):]
          + cut(layout, "static DWORD WINAPI nv2a_ack_thread(LPVOID param)", "static void xbox_Nv2aAckStart(void)")
          + (root / "tests/nv2a_queue/test_main.c").read_text(encoding="utf-8"))

want = {
    "off": ["window: KickOff's flush in, DMA_GET in, the pushbuffer out",
            "flush with the GPU thread idle: at once",
            "flush while the GPU thread walks: after the walk"],
    "on": ["window: KickOff's flush in, DMA_GET in, the pushbuffer out",
           "flush with the GPU thread idle: at once",
           "flush while the GPU thread walks: at once",
           "commands run: 8192; DMA_GET at the command each time: yes",
           "[NV2A] queue: 2 flushes complete at the write; 2 walks, 32 KB on average, the most 32 KB at once"],
    "wake": ["wake-ups lost: 0"],
    "ring": ["commands run: 65279 of 65279; DMA_GET at the command each time: yes"],
}

# Under the thread sanitizer (tests/run_all.py --tsan sets TEST_SANITIZER),
# which slows every access, how many flushes are answered at the write
# depends on timing, so that count isn't checked.
sanitizer = os.environ.get("TEST_SANITIZER")

with tempfile.TemporaryDirectory(prefix="nv2a_queue-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    command = cc + ["-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread", "-x", "c", "-",
               "src/kernel/nv2a_pb_scan.c", "-o", exe]
    # nv2a_intr.c and win32_compat.c build as the runtime builds them, with
    # the runtime's headers and without -Werror.
    objects = []
    for name in ("src/kernel/nv2a_intr.c", "src/platform/win32_compat.c"):
        obj = str(pathlib.Path(directory) / (pathlib.Path(name).stem + ".o"))
        subprocess.run(cc + ["-O2", "-std=gnu11", "-pthread", "-Isrc", "-Isrc/platform",
                             "-Isrc/kernel", "-c", name, "-o", obj], cwd=root, check=True)
        objects.append(obj)
    command[command.index("-o"):command.index("-o")] = ["-x", "none", *objects, "-lm"]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, lines in want.items():
        env = {k: v for k, v in os.environ.items()
               if k not in ("RECOMP_NV2A_QUEUE", "RECOMP_NV2A_KICK", "RECOMP_PB_SCAN")}
        env["RECOMP_PB_EXEC"] = "1"
        if name not in ("on", "wake", "ring"):
            env["RECOMP_NV2A_QUEUE"] = "0"       # the queue is on unless set to 0
        for case in ("WAKE", "RING"):
            env.pop("QUEUE_TEST_" + case, None)
        env.pop("RECOMP_ACK_SLEEP_US", None)
        if name in ("wake", "ring"):
            env["QUEUE_TEST_" + name.upper()] = "1"
        run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=60)
        text = run.stdout + run.stderr
        if run.returncode != 0:
            raise SystemExit(f"nv2a_queue {name}: exit {run.returncode}\n{text}")
        for line in lines:
            if sanitizer and line.startswith("[NV2A] queue:"):
                continue
            if line not in text:
                raise SystemExit(f"nv2a_queue {name}: no '{line}' in:\n{text}")
        print(f"queue {name}:", "; ".join(l for l in text.splitlines()
                                          if l.startswith(("flush while", "commands run", "D3D's wait",
                                                           "a wrap"))))
print("nv2a_queue: all four cases pass")
