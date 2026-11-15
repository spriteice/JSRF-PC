"""Compile the pad's vibration, both halves, and drive them without the game.

Cut by text: from port/recomp_manual.c, the XInputSetState replacement
(sub_001C3E85, with its offsets) and GARG, unchanged. That file doesn't
link on its own; src/input/rumble_hold.c is built whole.

Known bug 54. test_hook.c lays a feedback block out in guest memory as JSRF's
pad update does (sub_00167180: the block zeroed, both motors' speeds written
at +0x42 in one go) and checks that the replacement hands the input layer
the left and right speeds, each from its own place. test_hold.c drives
rumble_hold.c the way xbox_InputSetState and each pad read do, and checks
that a vibration the game set once is sent again before SDL's time for it
runs out, for as long as it runs, and goes to a pad found later.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
here = root / "tests/rumble"
manual = (root / "port/recomp_manual.c").read_text(encoding="utf-8")


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


garg = re.search(r"^#define GARG\(n\).*$", manual, re.M).group(0)
hook_source = ("""
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "xinput_xbox.h"
static ptrdiff_t g_xbox_mem_offset;
static uint32_t g_eax, g_esp;
DWORD xbox_InputSetState(DWORD port, const XBOX_VIBRATION *v);
""" + garg + "\n"
    + cut(manual, "/* XInputSetState(h, feedback)", "/* XInputPoll(h) */")
    + (here / "test_hook.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="rumble-") as directory:
    hook_exe = str(pathlib.Path(directory) / "hook")
    hold_exe = str(pathlib.Path(directory) / "hold")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-I", "src", "-I", "src/input", "-x", "c", "-",
                    "-o", hook_exe], cwd=root, input=hook_source, text=True, check=True)
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", "src/input",
                    "src/input/rumble_hold.c", "tests/rumble/test_hold.c",
                    "-o", hold_exe], cwd=root, check=True)
    for name, exe in (("hook", hook_exe), ("hold", hold_exe)):
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
        print(run.stdout + run.stderr, end="")
        if run.returncode != 0:
            sys.exit(f"rumble {name}: exit {run.returncode}")
print("rumble: both pass")
