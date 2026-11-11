"""2D upscaling's rule and its census (src/nv2a_gl/nv2a_upscale.c), as the
engine builds it: optimised, NDEBUG, with the warnings CI counts, driven by
test_main.c.
Requires GCC on PATH, including MinGW on Windows.
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

with tempfile.TemporaryDirectory(prefix="upscale-") as directory:
    exe = str(pathlib.Path(directory) / "upscale.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-Isrc/nv2a_gl", "tests/upscale/test_main.c",
                         "src/nv2a_gl/nv2a_upscale.c", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("upscale: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "upscale: all cases pass" not in text:
        sys.exit(f"upscale: exit {run.returncode}")
