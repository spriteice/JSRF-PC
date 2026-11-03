"""Compile src/kernel/kernel_sync.c and make its handle waits (test_main.c):
each must give the result it always gave, with the guest lock's report on
and off, and with the report on it must say whether it had to wait. On Linux
the objects and threads are src/platform/win32_compat.c's, on Windows the
system's. Requires GCC on PATH, including MinGW on Windows. CC, when set, is
the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
sources = ["tests/wait_report/test_main.c", "src/kernel/kernel_sync.c"]
if os.name == "nt":
    flags = []
else:
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

with tempfile.TemporaryDirectory(prefix="wait_report-") as directory:
    exe = str(pathlib.Path(directory) / "wait_report.exe")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("wait_report: no end in 120 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "wait_report: all cases pass" not in text:
        sys.exit(f"wait_report: exit {run.returncode}")
