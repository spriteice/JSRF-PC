"""Compile the layouts (src/input/input_layout.c) and drive them without SDL.

Builds src/input/input_layout.c whole, as the engine builds it; test_main.c
keeps the input layer's fixed tables as they were before the layouts
(xinput_device.c's pad table and kb_merge, and xbox_InputKeyboardMove's
keys), as what the defaults must give.

The defaults: each control's inputs, the pad's at their Xbox places with
the bumpers as Black and White, and the keys as nv2a_gl.c lists them. The
pad: every one of the fourteen buttons the table read, on or off together,
with the triggers at their edges and half way and the menu's Back either
way, gives what the table gave, and SDL's other buttons change nothing. The
keys: every one of the window's twenty, held or not together, over three
pads, gives what kb_merge gave, and says a key was held when one was. The
movement keys: what xbox_InputKeyboardMove gave for every one of the
twenty. A layout's other uses: two inputs on a control, the most of them;
a trigger on Start from half way; an input out of range, or none, pressing
nothing; a key no control uses, not held as far as the game knows.
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

with tempfile.TemporaryDirectory(prefix="input_layout-") as directory:
    exe = str(pathlib.Path(directory) / "input_layout")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-I", "src", "-I", "src/input",
                    "src/input/input_layout.c", "tests/input_layout/test_main.c",
                    "-o", exe], cwd=root, check=True)
    for case in ("defaults", "pad", "keys", "move", "binds"):
        run = subprocess.run([exe, case], capture_output=True, text=True, timeout=120)
        print(run.stdout + run.stderr, end="")
        if run.returncode != 0 or f"input_layout {case}: pass" not in run.stdout:
            sys.exit(f"input_layout {case}: exit {run.returncode}")
print("input_layout: all cases pass")
