"""Compile the pad's stand-in for the game's pad API (port/pad.c) and drive it
without the game.

Builds port/pad.c whole, as the engine builds it; test_main.c stands in for
the input layer (src/input), the free camera, the keyboard's moves, input
playback (port/playback.c, which tests/playback drives with pad.c) and
recomp_manual.c's test hooks, and lays the game's blocks out in a guest
memory of its own.

The devices: XGetDevices seeds a gamepad's descriptor as the console has it
by then (present, reported, nothing pending) and XGetDeviceChanges then
reports no change, as JSRF's enumeration needs; any other type has no
ports, and type 0 writes nothing. XInputOpen hands out port 0's handle
alone. XInputGetCapabilities refuses a handle that isn't one and fills the
gamepad's block. XInputGetState refuses a handle that isn't one; writes the
host's pad at XINPUT_STATE's offsets, after the free camera and the
keyboard's moves have had the sticks; gives port 0 an idle pad when the
host has none, and says the other ports have none; takes the test hooks'
pad over the host's while they drive it; and tells input playback the pad
with the keys' moves, before the free camera takes the right stick, and
writes what playback gives in its place, a pad or none, with the free
camera given the stick from it, or told the port has none. The input layer
and the test hooks are started once, at the first read.
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

with tempfile.TemporaryDirectory(prefix="pad-") as directory:
    exe = str(pathlib.Path(directory) / "pad")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-I", "port", "-I", "src", "-I", "src/input",
                    "port/pad.c", "tests/pad/test_main.c",
                    "-pthread", "-o", exe], cwd=root, check=True)
    run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    print(run.stdout + run.stderr, end="")
    if run.returncode != 0 or "pad: all cases pass" not in run.stdout:
        sys.exit(f"pad: exit {run.returncode}")
