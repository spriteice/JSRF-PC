"""The internal resolution and where its frame goes in the window
(src/nv2a_gl/nv2a_scale.c), as the engine builds it: optimised, NDEBUG, with
the warnings CI counts, driven by test_main.c. Requires GCC on PATH,
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

with tempfile.TemporaryDirectory(prefix="render_scale-") as directory:
    exe = str(pathlib.Path(directory) / "render_scale.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-Isrc/nv2a_gl", "tests/render_scale/test_main.c",
                         "src/nv2a_gl/nv2a_scale.c", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("render_scale: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "render_scale: all cases pass" not in text:
        sys.exit(f"render_scale: exit {run.returncode}")
