"""Compile the input latency log (RECOMP_INPUT_LATENCY) and drive it without
the game.

Builds src/input/input_press.c and src/nv2a_gl/nv2a_latency.c whole, as the
engine builds them; test_main.c plays the game's reads of the first pad and
the GPU thread's flips, hand-overs and swaps, each with its time and
address, and keeps the lines the log writes.

What counts as a press on the pad: a button, an analog button or trigger at
half way, a stick half way from its centre round the circle, each apart. A
key or mouse button is a press of its own when the keyboard's layout uses it
(tests/input_layout checks which those are).
Each press to its frame: a flip written before the read isn't its frame and
the first past it is; a flip with nothing drawn passes it on to the next
frame shown; the ring's end, with the title wrapped before the read and
after it; a frame the presenter replaced, timed by the newer frame's swap; a
key's pump and the pump before it; a press no frame shows, given up after
2 s with a line; the summary every 10 s, each stage's median and worst, and
nothing when there was nothing; presses the game read faster than the GPU
thread took them, counted. And the switch: on with RECOMP_INPUT_LATENCY=1,
off with 0.
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

with tempfile.TemporaryDirectory(prefix="input_latency-") as directory:
    exe = str(pathlib.Path(directory) / "input_latency")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-I", "src", "-I", "src/input", "-I", "src/nv2a_gl",
                    "src/input/input_press.c", "src/nv2a_gl/nv2a_latency.c",
                    "tests/input_latency/test_main.c", "-o", exe], cwd=root, check=True)
    for case, value in (("on", "1"), ("off", "0")):
        env = dict(os.environ, RECOMP_INPUT_LATENCY=value)
        run = subprocess.run([exe, case], capture_output=True, text=True, timeout=60, env=env)
        print(run.stdout + run.stderr, end="")
        if run.returncode != 0 or f"input_latency {case}: pass" not in run.stdout:
            sys.exit(f"input_latency {case}: exit {run.returncode}")
print("input_latency: all cases pass")
