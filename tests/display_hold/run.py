"""Compile the skip prompt's hold on the game's letting go of its display set,
and drive it without the game.

Cut by text: from port/recomp_manual.c, which doesn't link on its own:
GARG, guest_call_cdecl, GAME_MANAGER and GM_ACTORS, the display set's
names (GAME_DEVICE to BOX_ZERO_RA), s_let_go_held with disp_holder, and
disp_loading to sub_00025740 (with disp_let_go_held), unchanged.

test_main.c calls the hook as the game's code does (the group and word
pushed, the return address, the call, then the caller takes the two off),
against a stand-in of the game's own sub_00025740 that ends in a plain
`ret`, and checks the stack after each call: where the game's runs, where
the letting go is held back while the set loads (known bug 87), and the
held letting go done once the load is over. Requires GCC on PATH, or CC.
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
manual = (root / "port/recomp_manual.c").read_text(encoding="utf-8")
here = root / "tests/display_hold"


def cut(start, end):
    return manual[manual.index(start):manual.index(end, manual.index(start))]


garg = re.search(r"^#define GARG\(n\).*$", manual, re.M).group(0)
source = ("""
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static ptrdiff_t g_xbox_mem_offset;
static uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi;
static int g_fp_top;
#define MEM32(a) (*(uint32_t *)((uint8_t *)g_xbox_mem_offset + (uint32_t)(a)))
static double jsrf_now(void) { return 1.0; }
""" + garg + "\n"
    + cut("static uint32_t guest_call_cdecl(", "extern void sub_000681C0(void);")
    + re.search(r"^#define GAME_MANAGER .*$", manual, re.M).group(0) + "\n"
    + re.search(r"^#define GM_ACTORS .*$", manual, re.M).group(0) + "\n"
    + cut("#define GAME_DEVICE ", "extern void sub_001BAA00(void);")
    + cut("static int s_let_go_held;", "static uint32_t disp_models(void)\n{")
    + cut("static int disp_loading(uint32_t holder)", "/* The display set's A")
    + (here / "test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="display_hold-") as directory:
    exe = str(pathlib.Path(directory) / "display_hold")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                         "-x", "c", "-", "-o", exe],
                   cwd=root, input=source, text=True, check=True)
    run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "display_hold: all cases pass" not in text:
        sys.exit(f"display_hold: exit {run.returncode}")
