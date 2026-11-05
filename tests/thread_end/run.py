"""Compile src/platform/win32_compat.c's threads and see what one leaves
behind once it has ended (test_main.c). POSIX only (win32_compat.c is the
Linux side); requires GCC on PATH.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("thread_end: skipped, POSIX only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
with tempfile.TemporaryDirectory(prefix="thread_end-") as directory:
    exe = str(pathlib.Path(directory) / "thread_end")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
                    "-I", "src", "-I", "src/platform", "tests/thread_end/test_main.c",
                    "src/platform/win32_compat.c", "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("thread_end: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "thread_end: all cases pass" not in text:
        sys.exit(f"thread_end: exit {run.returncode}")
