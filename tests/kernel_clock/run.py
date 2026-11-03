"""Compile src/kernel/kernel_timer.c and start the kernel's timer thread
alone, as xbox_kernel_bridge_init does (test_main.c): KeTickCount must keep
up with the host's millisecond clock with no GPU thread running, and a timer
set with KeSetTimer must fire its DPC once. On Linux the thread is
src/platform/win32_compat.c's, on Windows the system's. Requires GCC on PATH,
including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
sources = ["tests/kernel_clock/test_main.c", "src/kernel/kernel_timer.c"]
if os.name == "nt":
    flags = []
else:
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

with tempfile.TemporaryDirectory(prefix="kernel_clock-") as directory:
    exe = str(pathlib.Path(directory) / "kernel_clock.exe")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("kernel_clock: no end in 120 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "kernel_clock: all cases pass" not in text:
        sys.exit(f"kernel_clock: exit {run.returncode}")
