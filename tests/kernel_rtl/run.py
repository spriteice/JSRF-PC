"""Compile src/kernel/kernel_rtl.c and check what the game calls of it
against known answers (test_main.c): RtlTimeFieldsToTime and
RtlTimeToTimeFields both ways for dates whose 100 ns counts since 1601 are
known, weekdays and leap days among them, and RtlEqualString with and
without case. On Linux the host calls under them are
src/platform/win32_compat.c's, on Windows the system's. Requires GCC on
PATH, including MinGW on Windows.
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
sources = ["tests/kernel_rtl/test_main.c", "src/kernel/kernel_rtl.c"]
if os.name == "nt":
    flags = []
else:
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

with tempfile.TemporaryDirectory(prefix="kernel_rtl-") as directory:
    exe = str(pathlib.Path(directory) / "kernel_rtl.exe")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("kernel_rtl: no end in 120 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "kernel_rtl: all cases pass" not in text:
        sys.exit(f"kernel_rtl: exit {run.returncode}")
