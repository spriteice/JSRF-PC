"""Compile the GPU thread and the game's side of the pushbuffer handshake, and
drive them without the game.

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
of a 512 KB ring behind it, all of which must be walked. Last, the wake-up
itself: software methods raised the way src/kernel/kernel_bridge.c raises them
(its queue and kernel_pgraph_tick, verbatim) and acknowledged by
port/recomp_manual.c's sub_00194210 (verbatim), while the GPU thread above
completes the interrupt registers (vblank_intr_tick, verbatim): none may be
lost. POSIX only (the wait is pthreads'); requires GCC on PATH.
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("nv2a_queue: skipped, POSIX only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
layout = (root / "src/kernel/xbox_memory_layout.c").read_text(encoding="utf-8")
mmio = (root / "src/kernel/kernel_mmio.c").read_text(encoding="utf-8")
types = (root / "templates/runtime/recomp_types.h").read_text(encoding="utf-8")
header = (root / "src/kernel/xbox_memory_layout.h").read_text(encoding="utf-8")
bridge = (root / "src/kernel/kernel_bridge.c").read_text(encoding="utf-8")
manual = (root / "port/recomp_manual.c").read_text(encoding="utf-8")


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


def define(text, name):
    return re.search(r"^#define " + name + r"\b.*$", text, re.M).group(0) + "\n"


registers = "".join(define(bridge, name) for name in (
    "BRIDGE_MEM32", "XBOX_NV2A_REG_BASE", "NV2A_PMC_INTR_0", "NV2A_PMC_INTR_PCRTC",
    "NV2A_PCRTC_INTR_0", "NV2A_PCRTC_INTR_VBLANK", "NV2A_VECTOR", "NV2A_PGRAPH_INTR",
    "NV2A_PGRAPH_NSOURCE", "NV2A_PGRAPH_TRAP_ADDR", "NV2A_PGRAPH_TRAP_DATA",
    "NV2A_PGRAPH_INTR_ERROR", "NV2A_PMC_INTR_PGRAPH", "SWM_QUEUE")) + define(header, "NV2A_VBLANK_PENDING_TAG")


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
#define InterlockedCompareExchange(p, x, c) (*(p))
#define Sleep(ms) sched_yield()
static void *g_nv2a_memory;
static uintptr_t g_memory_offset;
static uint32_t s_tick_sink;
static volatile LONG g_nv2a_ack_stop;
static void *g_mcpx_regs;
static int g_apu_mmio_trapped;
static const uint32_t MCPX_COUNTERS[] = { 0x020010u };
static int s_nv2a_trace;
static DWORD GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
static ptrdiff_t g_xbox_mem_offset;
static uint32_t g_eax;
static uint32_t xbox_GetConnectedInterrupt(uint32_t vector) { (void)vector; return 1; }
static int kernel_raise_interrupt(uint32_t vector);
int nv2a_title_acks_pgraph(void);
static void fence_mirrors_tick(void) { }
static void counter_mirrors_tick(void) { }
static void frame_counters_tick(void) { }
static void framebuffer_probe_tick(void) { }
ptrdiff_t xbox_GetMemoryOffset(void);
void xbox_Nv2aRegisterWritten(uint32_t addr, unsigned size);
""" + registers
          + cut(bridge, "static struct { uint32_t subch, param; } g_swm_q[SWM_QUEUE];", "/* The audio processor's interrupt, delivered.")
          + cut(manual, "int nv2a_title_acks_pgraph(void) { return 1; }", "/* ---- sub_001910E0")
          + cut(layout, "#if !defined(_WIN32) || defined(XBOXRECOMP_WIN_GL)\n#include <pthread.h>",
                "/*\n * NV2A busy-bit acknowledgement.")
          + cut(layout, "static const struct { uint32_t offset; uint32_t busy_mask; } NV2A_ACK[]",
                "/*\n * PFIFO channel DMA pointers.")
          + cut(layout, "#define NV2A_USER_DMA_PUT", "/*\n * Free-running counters in the MCPX aperture.")
          + cut(types, "#ifndef RECOMP_MMIO_LO", "static inline volatile uint8_t *recomp_p8")
          + mmio[mmio.index("uint32_t (*g_recomp_mmio_read_fn)"):]
          + cut(layout, "static void vblank_intr_tick(volatile uint32_t *regs)", "static void xbox_Nv2aAckStart(void)")
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
    "swm": ["wake-ups lost: 0"],
}

with tempfile.TemporaryDirectory(prefix="nv2a_queue-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    command = ["gcc", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread", "-x", "c", "-",
               "src/kernel/nv2a_pb_scan.c", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, lines in want.items():
        env = {k: v for k, v in os.environ.items()
               if k not in ("RECOMP_NV2A_QUEUE", "RECOMP_NV2A_KICK", "RECOMP_PB_SCAN")}
        env["RECOMP_PB_EXEC"] = "1"
        if name in ("on", "wake", "ring"):
            env["RECOMP_NV2A_QUEUE"] = "1"
        for case in ("WAKE", "RING", "SWM"):
            env.pop("QUEUE_TEST_" + case, None)
        env.pop("RECOMP_ACK_SLEEP_US", None)
        if name in ("wake", "ring", "swm"):
            env["QUEUE_TEST_" + name.upper()] = "1"
        if name == "swm":
            env["RECOMP_ACK_SLEEP_US"] = "0"     # the GPU thread's loop flat out
        run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=60)
        text = run.stdout + run.stderr
        if run.returncode != 0:
            raise SystemExit(f"nv2a_queue {name}: exit {run.returncode}\n{text}")
        for line in lines:
            if line not in text:
                raise SystemExit(f"nv2a_queue {name}: no '{line}' in:\n{text}")
        print(f"queue {name}:", "; ".join(l for l in text.splitlines()
                                          if l.startswith(("flush while", "commands run", "D3D's wait",
                                                           "a wrap", "software methods"))))
print("nv2a_queue: all five cases pass")
