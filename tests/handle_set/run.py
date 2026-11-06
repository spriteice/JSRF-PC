"""Compile src/platform/win32_compat.c's handle set and see that a handle
closed is forgotten (known bug 23; test_main.c). POSIX only (win32_compat.c
is the Linux side); requires GCC on PATH.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("handle_set: skipped, POSIX only")
    sys.exit(77)   # skipped (tests/run_all.py)

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
with tempfile.TemporaryDirectory(prefix="handle_set-") as directory:
    exe = str(pathlib.Path(directory) / "handle_set")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
                    "-I", "src", "-I", "src/platform", "tests/handle_set/test_main.c",
                    "src/platform/win32_compat.c", "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("handle_set: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "handle_set: all cases pass" not in text:
        sys.exit(f"handle_set: exit {run.returncode}")
