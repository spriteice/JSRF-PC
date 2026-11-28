"""Compile src/kernel/xbox_heap.c and read its figures for the minute's [MEM] line.

test_main.c allocates and frees blocks through two of the heap's calls (the
pool and the system memory), and xbox_heap_usage must give each call's live
bytes, its peak and its live blocks at each step: nothing at the start, the
peak kept once blocks are freed, a block freed twice counted once, and a line
too long for its buffer cut short, by whole calls and "...". Then the same with RECOMP_MMTRACE: a
[MMTRACE] line for each block handed out and taken back, with the time and
the guest's caller test_main.c gives in place of the engine's (which the heap
asks for weakly, so tests/heap_guard and tests/heap_lock link without them);
the free of a block that isn't live has its line too, with the size 0, as
the game asked for it. Without the switch, no line.

The contiguous window's figures (xbox_memory_layout.c, which doesn't build
alone) and the [MEM] lines themselves (kernel_bridge.c's report thread) are
read in a run's log instead.

On Linux the memory calls are src/platform/win32_compat.c's, on Windows the
system's. Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
sources = ["tests/heap_usage/test_main.c", "src/kernel/xbox_heap.c"]
if os.name == "nt":
    flags = []
else:
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

WANT = [
    "start: nothing yet",
    "allocated: pool 16 KB (peak 16) in 3, system 16 KB (peak 16) in 1",
    "freed: pool 8 KB (peak 16) in 2, system 0 KB (peak 16) in 0",
    "freed twice: pool 8 KB (peak 16) in 2, system 0 KB (peak 16) in 0",
    "again: pool 12 KB (peak 16) in 3, system 0 KB (peak 16) in 0",
    "all freed: pool 0 KB (peak 16) in 0, system 0 KB (peak 16) in 0",
    "cut: 28 [pool 0 KB (peak 16) in 0 ...]",
    "cut to none: 3 [...]",
]

ADDRESS = r"0x[0-9A-F]{8}"
TRACE = [
    ("ExAllocatePool", 4096, 16), ("ExAllocatePool", 8192, 16), ("ExAllocatePool", 4096, 16),
    ("MmAllocateSystemMemory", 16384, 4096),
    ("ExFreePool", 8192, 0), ("MmFreeSystemMemory", 16384, 0), ("MmFreeSystemMemory", 0, 0),
    ("ExAllocatePool", 4096, 16),
    ("ExFreePool", 4096, 0), ("ExFreePool", 4096, 0), ("ExFreePool", 4096, 0),
]


def line(what, size, alignment):
    aligned = f" \\(aligned to {alignment}\\)" if alignment else ""
    return (rf"  \[MMTRACE\] t=12\.500s {re.escape(what)} {size} bytes{aligned} at {ADDRESS}, "
            r"from guest 0x00123456")


with tempfile.TemporaryDirectory(prefix="heap_usage-") as directory:
    exe = str(pathlib.Path(directory) / "heap_usage.exe")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    for traced in (False, True):
        env = {k: v for k, v in os.environ.items()
               if k not in ("RECOMP_MMTRACE", "RECOMP_HEAP_GUARD", "RECOMP_HEAP_CHECK")}
        if traced:
            env["RECOMP_MMTRACE"] = "1"
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60, env=env)
        name = "with RECOMP_MMTRACE" if traced else "without it"
        if run.returncode:
            raise SystemExit(f"heap_usage: {name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        got = run.stdout.splitlines()
        if got != WANT:
            raise SystemExit(f"heap_usage: {name}: the figures are\n  " + "\n  ".join(got)
                             + "\nnot\n  " + "\n  ".join(WANT))
        traces = [l for l in run.stderr.splitlines() if "[MMTRACE]" in l]
        if not traced:
            if traces:
                raise SystemExit(f"heap_usage: without RECOMP_MMTRACE, lines:\n" + "\n".join(traces))
        else:
            if len(traces) != len(TRACE) or not all(
                    re.fullmatch(line(*want), l) for want, l in zip(TRACE, traces)):
                raise SystemExit("heap_usage: the [MMTRACE] lines are\n" + "\n".join(traces))
        print(f"heap_usage: {name}: ok")
print("heap_usage: all passed")
