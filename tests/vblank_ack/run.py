"""Compile the display interrupt's acknowledgement and the loop back edge, and
drive them without the game.

Takes vblank_intr_tick and xbox_nv2a_vblank_service from
src/kernel/xbox_memory_layout.c, and xbox_GuestCpuPreempt from
src/kernel/kernel_bridge.c, verbatim, with the registers and bits the bridge
raises a vblank with. Requires GCC on PATH, including MinGW on Windows.
"""
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
source = ("""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define RECOMP_TLS
#define DISPATCH_LEVEL 2
#define APU_VECTOR 5u
""" + define(header, "NV2A_VBLANK_PENDING_TAG") + define(bridge, "NV2A_PMC_INTR_0")
          + define(bridge, "NV2A_PMC_INTR_PCRTC") + define(bridge, "NV2A_PCRTC_INTR_0")
          + define(bridge, "NV2A_PCRTC_INTR_VBLANK") + """
static void *g_nv2a_memory;
int xbox_nv2a_swm_in_flight(void) { return 0; }
""" + vblank + """
static RECOMP_TLS int t_guest_holds, t_in_callback;
static volatile uint32_t g_lock_owner_edge;
static volatile unsigned g_lock_owner_irql;
static unsigned long g_slice_held_raised, g_edge_vblank_checks;
unsigned g_recomp_tick;
unsigned char xbox_KeCurrentIrql(void) { return 0; }
uint32_t xbox_GetConnectedInterrupt(uint32_t vector) { (void)vector; return 0; }
static int kernel_raise_interrupt_here(uint32_t vector) { (void)vector; return 0; }
static int s_offers;
void xbox_guest_lock_yield_now(void) { s_offers++; }
""" + preempt + (root / "tests/vblank_ack/test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="vblank_ack-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-x", "c", "-", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    print(run.stdout + run.stderr, end="")
    if run.returncode != 0:
        raise SystemExit(f"vblank_ack: exit {run.returncode}")
