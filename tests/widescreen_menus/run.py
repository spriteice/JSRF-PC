"""The menus laid out for widescreen (port/widescreen_menus.c), as the engine
builds it: optimised, NDEBUG, with the warnings CI counts, linked with the
HUD's marks (port/widescreen_hud.c), which write its marks, and the
widescreen unit (src/nv2a_gl/nv2a_wide.c), and driven by test_main.c
against a guest memory of its own that holds D3D's device and push buffer
and a menu's object. How each menu then looks needs the game and the whole
renderer: the test build's menu step.
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
# Each named once: the menus at their first begin, the wrong class at the
# first object of it, and the parts, the text's sides, the sign and the
# header at the first time each is told apart.
ONCE = (
    "[WIDE] the street pause menu (sub_001129E0): laid out for the wide screen",
    "[WIDE] the other pause menu (sub_001133A0): laid out for the wide screen",
    "[WIDE] the street pause menu (sub_001129E0) drew an object of class 001D2AA8, "
    "not 001D2A00: left as it is",
    "[WIDE] menu part 64 in the street pause menu, the backdrop and its grids (model 0x80) "
    "(+0x7C): marked for two halves, moved out by whole cells",
    "[WIDE] menu part 65 in the street pause menu, the grey top bar (model 3) "
    "(+0x44): marked for across the width",
    "[WIDE] menu part 74 in the street pause menu, ROTATE MAP's button (model 0x100) "
    "(+0x90): marked for the left edge",
    "[WIDE] menu part 102 in the other pause menu, the mode's emblem (bank 0xB model 0x10) "
    "(+0x6C): marked for the left edge",
    "[WIDE] menu part 192 in the street pause menu, the text on the left: drawn at once, "
    "at the left edge",
    "[WIDE] menu part 193 in the street pause menu, the text on the right: drawn at once, "
    "at the right edge",
    "[WIDE] menu part 194 in the street pause menu, the area name (a sign, flags D8000000): "
    "marked for the left edge",
    "[WIDE] menu part 195 in the other pause menu, the header (its child, class 001D0770): "
    "marked for the left edge",
    "[WIDE] menu part 196, the pause menus' ticker (class 001D3770): across the whole width",
)
PARTS = 13 + 8

with tempfile.TemporaryDirectory(prefix="widescreen_menus-") as directory:
    exe = str(pathlib.Path(directory) / "widescreen_menus.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-Iport", "-Isrc", "-Isrc/nv2a_gl", "tests/widescreen_menus/test_main.c",
                         "port/widescreen_menus.c", "port/widescreen_hud.c",
                         "src/nv2a_gl/nv2a_wide.c", "src/nv2a_gl/nv2a_scale.c", "-lm", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("widescreen_menus: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "widescreen_menus: all cases pass" not in text:
        sys.exit(f"widescreen_menus: exit {run.returncode}")
    for line in ONCE:
        if text.count(line) != 1:
            sys.exit(f"widescreen_menus: {line!r} came {text.count(line)} times, not once")
    marked = [l for l in text.splitlines() if l.startswith("[WIDE] menu part ") and "(+0x" in l]
    if len(marked) != PARTS or len(set(marked)) != PARTS:
        sys.exit(f"widescreen_menus: {len(marked)} part lines, not one for each of {PARTS}")
    if text.count("drew an object of class") != 1:
        sys.exit("widescreen_menus: the wrong class was named more than once")
    if "[CHECK]" in text:
        sys.exit("widescreen_menus: a check failed in the marks")
