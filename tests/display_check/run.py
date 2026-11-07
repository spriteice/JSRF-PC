"""The presenter's display check at start-up, without a window.

Cut by text: the check's constants, pres_measure and pres_check from
src/nv2a_gl/nv2a_gl.c, unchanged, from "The display check (pres_calibrate)"
up to pres_swap. nv2a_gl.c doesn't link on its own, and its split is b3
row 9's.

Built with the warnings CI counts and driven by test_main.c, whose swap()
plays the display: it hands back the times swaps end at, as a steady
display, one held up by start-up's work, or a driver that doesn't wait.
That the presenter then paces frames by the display is the test build's
step.
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
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
here = root / "tests/display_check"


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


source = ("#include <stdio.h>\n"
          + cut(gl, "/* The display check (pres_calibrate)", "/* One of the check's swaps")
          + (here / "test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="display_check-") as directory:
    exe = str(pathlib.Path(directory) / "display_check.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-x", "c", "-", "-lm", "-o", exe],
                   cwd=root, input=source, text=True, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("display_check: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "display_check: all cases pass" not in text:
        sys.exit(f"display_check: exit {run.returncode}")
