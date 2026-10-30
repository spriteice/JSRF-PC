"""Compile the display interrupt's acknowledgement and the loop back edge, and
drive them without the game.

Takes vblank_intr_tick and xbox_nv2a_vblank_service from
src/kernel/xbox_memory_layout.c, and xbox_GuestCpuPreempt and
kernel_vblank_tick from src/kernel/kernel_bridge.c, verbatim, with the
registers and bits the bridge raises a vblank with. Requires GCC on PATH,
including MinGW on Windows, and POSIX threads (winpthreads there).
"""
import os
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
layout = (root / "src/kernel/xbox_memory_layout.c").read_text(encoding="utf-8")
header = (root / "src/kernel/xbox_memory_layout.h").read_text(encoding="utf-8")
bridge = (root / "src/kernel/kernel_bridge.c").read_text(encoding="utf-8")


def define(text, name):
    return re.search(r"^#define " + name + r"\b.*$", text, re.M).group(0) + "\n"


vblank = layout[layout.index("static void vblank_intr_tick(volatile uint32_t *regs)"):
                layout.index("static DWORD WINAPI nv2a_ack_thread")]
preempt = bridge[bridge.index("void xbox_GuestCpuPreempt(uint32_t va)\n{"):
                 bridge.index("/* Give the guest lock up across a wait that is not a kernel call.")]
raise_ = bridge[bridge.index("unsigned long g_vblank_count;"):
                bridge.index("/* The GPU's software methods, delivered.")]
source = ("""
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define RECOMP_TLS
#define DISPATCH_LEVEL 2
#define APU_VECTOR 5u
""" + define(header, "NV2A_VBLANK_PENDING_TAG") + define(bridge, "NV2A_PMC_INTR_0")
          + define(bridge, "NV2A_PMC_INTR_PCRTC") + define(bridge, "NV2A_PCRTC_INTR_0")
          + define(bridge, "NV2A_PCRTC_INTR_VBLANK") + define(layout, "NV2A_PMC_INTR_PGRAPH")
          + define(bridge, "BRIDGE_MEM32") + define(bridge, "XBOX_NV2A_REG_BASE")
          + define(bridge, "NV2A_VECTOR") + """
static void *g_nv2a_memory;
static ptrdiff_t g_xbox_mem_offset;
int xbox_nv2a_swm_on(void) { return 0; }
uint32_t xbox_GetConnectedInterrupt(uint32_t vector) { return vector == NV2A_VECTOR; }
/* Every call a tick that is due. */
static unsigned long long s_tick_ms;
static unsigned long long GetTickCount64(void) { return s_tick_ms += 17; }
static int kernel_raise_interrupt(uint32_t vector);
""" + vblank + raise_ + """
static RECOMP_TLS int t_guest_holds, t_in_callback;
static volatile uint32_t g_lock_owner_edge;
static volatile unsigned g_lock_owner_irql;
static unsigned long g_slice_held_raised, g_edge_vblank_checks;
unsigned g_recomp_tick;
unsigned char xbox_KeCurrentIrql(void) { return 0; }
static int kernel_raise_interrupt_here(uint32_t vector) { (void)vector; return 0; }
static int s_offers;
void xbox_guest_lock_yield_now(void) { s_offers++; }
""" + preempt + (root / "tests/vblank_ack/test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="vblank_ack-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-pthread", "-x", "c", "-", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    run = subprocess.run([exe], capture_output=True, text=True, timeout=60,
                         env=dict(os.environ, RECOMP_VBLANK="1"))
    print(run.stdout + run.stderr, end="")
    if run.returncode != 0:
        raise SystemExit(f"vblank_ack: exit {run.returncode}")
