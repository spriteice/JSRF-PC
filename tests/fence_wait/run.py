"""Compile the game thread's wait for a fence and drive it without the game.

Takes nv2a_pb_wait_fence and fence_written from src/kernel/nv2a_pb_exec.c,
verbatim, and builds them with src/platform/win32_compat.c's events, against
a GPU thread that writes fences the way the executor does (test_main.c). Run
once as it ships and once with RECOMP_FENCE_WAIT=0. Then port/recomp_manual.c's
sub_00191440 and guest_call_cdecl, verbatim, against stand-ins for the game
functions it calls (test_wrapper.c). POSIX only (win32_compat.c is the Linux
and macOS side); requires GCC on PATH.
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("fence_wait: skipped, POSIX only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
pb_exec = (root / "src/kernel/nv2a_pb_exec.c").read_text(encoding="utf-8")
manual = (root / "port/recomp_manual.c").read_text(encoding="utf-8")
here = root / "tests/fence_wait"


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


wait_source = ("""
#define _GNU_SOURCE
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "xbox_winnt.h"
typedef UCHAR KIRQL;
#define DISPATCH_LEVEL 2
static uint8_t g_guest[0x2000];
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)(uintptr_t)g_guest; }
static __thread KIRQL t_irql;
KIRQL xbox_KeCurrentIrql(void) { return t_irql; }
static pthread_mutex_t g_guest_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread int t_holds;
int xbox_guest_lock_drop(void)
{
    if (!t_holds) return 0;
    t_holds = 0;
    pthread_mutex_unlock(&g_guest_lock);
    return 1;
}
void xbox_guest_lock_retake(int held)
{
    if (held) { pthread_mutex_lock(&g_guest_lock); t_holds = 1; }
}
""" + cut(pb_exec, "/*\n * A guest thread waiting for a fence", "/* NV097_NO_OPERATION with a parameter")
    + (here / "test_main.c").read_text(encoding="utf-8"))

garg = re.search(r"^#define GARG\(n\).*$", manual, re.M).group(0)
wrapper_source = ("""
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static ptrdiff_t g_xbox_mem_offset;
static uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi;
static int g_fp_top;
#define MEM32(a) (*(uint32_t *)((uint8_t *)g_xbox_mem_offset + (uint32_t)(a)))
#define MEMW32(a, v) (MEM32(a) = (uint32_t)(v))
#define DEV   0x00200000u
#define FENCE 0x00300000u
#define STACK 0x003F0000u
void sub_001910E0(void);
""" + garg + "\n"
    + cut(manual, "static uint32_t guest_call_cdecl(", "extern void sub_000681C0(void);")
    + cut(manual, "/* ---- sub_00191440: D3D's wait", "/* ---- sub_00013A80")
    + (here / "test_wrapper.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="fence_wait-") as directory:
    wait_exe = str(pathlib.Path(directory) / "wait")
    wrapper_exe = str(pathlib.Path(directory) / "wrapper")
    compat = str(pathlib.Path(directory) / "compat.o")
    subprocess.run(["gcc", "-O2", "-std=gnu11", "-pthread", "-I", "src", "-I", "src/platform", "-c",
                    "src/platform/win32_compat.c", "-o", compat], cwd=root, check=True)
    subprocess.run(["gcc", "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
                    "-I", "src", "-I", "src/platform", "-x", "c", "-", "-x", "none", compat,
                    "-lm", "-o", wait_exe], cwd=root, input=wait_source, text=True, check=True)
    subprocess.run(["gcc", "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-",
                    "-o", wrapper_exe], cwd=root, input=wrapper_source, text=True, check=True)
    runs = [("on", [wait_exe], "fence_wait: all cases pass"),
            ("off", [wait_exe, "off"], "fence_wait off: all cases pass"),
            ("wrapper", [wrapper_exe], "fence_wait wrapper: all cases pass")]
    for name, command, want in runs:
        env = {k: v for k, v in os.environ.items() if k != "RECOMP_FENCE_WAIT"}
        if name == "off":
            env["RECOMP_FENCE_WAIT"] = "0"
        try:
            run = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired:
            sys.exit(f"fence_wait {name}: no end in 120 s (a thread stuck: a wake-up lost, or a lock)")
        text = run.stdout + run.stderr
        print(text, end="")
        if run.returncode != 0 or want not in text:
            sys.exit(f"fence_wait {name}: exit {run.returncode}")
print("fence_wait: all three runs pass")
