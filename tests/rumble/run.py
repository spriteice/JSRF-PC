"""Compile the pad's vibration, both halves, and drive them without the game.

Builds port/pad.c and src/input/rumble_hold.c whole, as the engine builds
them; test_hook.c stands in for the input layer, the free camera, the
keyboard's moves, input playback and recomp_manual.c's test hooks.

Known bug 54. test_hook.c lays a feedback block out in guest memory as JSRF's
pad update does (sub_00167180: the block zeroed, both motors' speeds written
at +0x42 in one go) and checks that XInputSetState's replacement
(pad_set_state) hands the input layer the left and right speeds, each from
its own place. test_hold.c drives rumble_hold.c the way xbox_InputSetState
and each pad read do, and checks that a vibration the game set once is sent
again before SDL's time for it runs out, for as long as it runs, and goes
to a pad found later.
Requires GCC on PATH, including MinGW on Windows, and POSIX threads there
(winpthreads), whose clock_gettime pad.c reads.
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

with tempfile.TemporaryDirectory(prefix="rumble-") as directory:
    hook_exe = str(pathlib.Path(directory) / "hook")
    hold_exe = str(pathlib.Path(directory) / "hold")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-I", "port", "-I", "src", "-I", "src/input",
                    "port/pad.c", "tests/rumble/test_hook.c",
                    "-pthread", "-o", hook_exe], cwd=root, check=True)
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", "src/input",
                    "src/input/rumble_hold.c", "tests/rumble/test_hold.c",
                    "-o", hold_exe], cwd=root, check=True)
    for name, exe in (("hook", hook_exe), ("hold", hold_exe)):
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
        print(run.stdout + run.stderr, end="")
        if run.returncode != 0:
            sys.exit(f"rumble {name}: exit {run.returncode}")
print("rumble: both pass")
