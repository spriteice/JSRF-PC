"""Compile src/kernel/kernel_apc.c and deliver file APCs on one thread while
another holds the guest lock as the title (test_main.c): every APC's routine
must run holding the guest lock, and the wait that delivered it must go on
without it. On Linux the threads are src/platform/win32_compat.c's, on
Windows the system's. Requires GCC on PATH, including MinGW on Windows.
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
sources = ["tests/file_apc/test_main.c", "src/kernel/kernel_apc.c"]
if os.name == "nt":
    flags = []
else:
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

with tempfile.TemporaryDirectory(prefix="file_apc-") as directory:
    exe = str(pathlib.Path(directory) / "file_apc.exe")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("file_apc: no end in 120 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "file_apc: all cases pass" not in text:
        sys.exit(f"file_apc: exit {run.returncode}")
