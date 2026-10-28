"""Compile frame pacing's spin margin and its absolute sleep, and drive them
without the game.

Takes pace_margin, pace_margin_learn and pace_sleep_until from
src/nv2a_gl/nv2a_gl.c, verbatim, with the pacing report's counters. Linux
only, as they are. Requires GCC on PATH.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")

if not sys.platform.startswith("linux"):
    print("pace_spin: skipped (Linux only)")
    raise SystemExit(0)

stats = gl[gl.index("static struct {\n    uint64_t frames, late, restarts"):]
stats = stats[:stats.index("} g_pace_st;") + len("} g_pace_st;")]
spin = gl[gl.index("#if defined(__linux__)\n#include <errno.h>\n#define PACE_ABSOLUTE 1"):
          gl.index("static void frame_pace(void)")]
source = ("""
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double mono_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
""" + stats + "\n" + spin + (root / "tests/pace_spin/test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="pace_spin-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-x", "c", "-", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for fixed in (None, "1200"):
        env = dict(os.environ)
        env.pop("RECOMP_PACE_SPIN_US", None)
        if fixed:
            env["RECOMP_PACE_SPIN_US"] = fixed
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60, env=env)
        print(run.stdout + run.stderr, end="")
        if run.returncode != 0:
            raise SystemExit(f"pace_spin: exit {run.returncode}")
