"""The GPU time profile's line for the graphics card on a headless EGL context.

Takes card_mark and card_report, and the profile's clock, from
src/nv2a_gl/nv2a_gl.c verbatim, and drives them from test_main.c with
tests/gl_backend's context: OpenGL 3.3 core (Mesa's llvmpipe will do). Every
frame shown must be read once, finished after it was handed over and soon
after, and a 60th of a second from the last on the card's clock; frames shown
with every query out must be counted as untimed. Linux, with EGL and Mesa:
libegl-dev, libgl-dev. Requires a C compiler on PATH (cc, or CC).
"""
import os
import pathlib
import re
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
cc = shlex.split(os.environ.get("CC", "cc"))


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


source = ("""
#define _GNU_SOURCE
#include "context.h"
#include <stdio.h>
#include <time.h>
static int g_is_gl = 1;
#define PROF_ON() 1
""" + cut(gl, "static double prof_now(void)", "#define PROF_ON()")
          + cut(gl, "#define CARD_QUERIES", "#else\nstatic void card_mark(void) {}")
          + (root / "tests/gl_card/test_main.c").read_text(encoding="utf-8"))

line = re.compile(r"the card, by its own clock: (\d+) frames finished (-?[0-9.]+) ms after hand-over "
                  r"on average, (-?[0-9.]+) at worst; ([0-9.]+) ms apart on average, ([0-9.]+) at the "
                  r"most; (\d+) still out(; (\d+) frames not timed, every query out)?")

with tempfile.TemporaryDirectory(prefix="gl-card-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    subprocess.run(cc + ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(root / "tests/gl_backend"),
                         "-x", "c", "-", "-o", exe, "-lEGL", "-lGL"],
                   input=source, text=True, check=True)
    run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
log = run.stderr
if run.returncode != 0 or "frames shown: " not in log:
    raise SystemExit(f"gl_card: exit {run.returncode}\n{run.stdout}{log}")
shown, rest = log.split("frames shown: ", 1)
frames = int(rest.split()[0])
reports = [m.groups() for m in line.finditer(shown)]
if len(reports) < 2:
    raise SystemExit(f"gl_card: fewer than two reports:\n{log}")
read = sum(int(r[0]) for r in reports)
if read != frames or int(reports[-1][5]) != 0:
    raise SystemExit(f"gl_card: {read} frames read of {frames} shown:\n{log}")
for n, lag, worst, apart, most, out, _, untimed in reports:
    if not -1.0 <= float(lag) <= 20.0 or float(worst) > 100.0:
        raise SystemExit(f"gl_card: a frame finished {lag} ms after hand-over on average, {worst} at worst:\n{log}")
    if int(n) > 1 and not 15.0 <= float(apart) <= 25.0:
        raise SystemExit(f"gl_card: frames {apart} ms apart on average, not a 60th of a second:\n{log}")
    if untimed:
        raise SystemExit(f"gl_card: frames untimed with the queries read each second:\n{log}")
full = [m.groups() for m in line.finditer(rest)]
if len(full) != 1 or full[0][7] != "2" or int(full[0][0]) != 128:
    raise SystemExit(f"gl_card: two frames over the 128 queries not counted as untimed:\n{log}")
print("gl_card:", len(reports), "reports;", "; ".join(
    f"{r[0]} frames, {r[1]} ms after hand-over, {r[3]} ms apart" for r in reports))
