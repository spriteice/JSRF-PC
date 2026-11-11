"""The menus laid out for widescreen (port/widescreen_menus.c), as the engine
builds it: optimised, NDEBUG, with the warnings CI counts, linked with the
HUD's marks (port/widescreen_hud.c), which write its marks, and the
widescreen unit (src/nv2a_gl/nv2a_wide.c), and driven by test_main.c
against a guest memory of its own that holds D3D's device and push buffer
and a menu's object: the laid-out menus' parts and marks, and the plain
menus' marks for the side fill. How each menu then looks needs the game and
the whole renderer: the test build's menu step.
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
    "[WIDE] menu part 86 in the other pause menu, the mode's emblem (bank 0xB model 0x10) "
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
    "[WIDE] Roboy's garage menu (sub_0011C070) drew an object of class 001D9090, not 001D8638: "
    "left as it is",
    "[WIDE] the garage's system menu (sub_0011FEB0) drew an object of class 001D2A00, "
    "not 001D9090: left as it is",
    "[WIDE] the save and load menu (sub_0011DD10): its frames' sides filled, piece 208, "
    "its tan strip hidden",
    "[WIDE] the garage's character select (sub_0011AC50): its frames' sides filled, piece 216, "
    "its tan strip hidden",
    "[WIDE] the garage's graffiti menu (sub_00127080): its frames' sides filled, piece 217, "
    "its tan strip hidden",
    "[WIDE] the loading screen (sub_00066550): its frames' sides filled, piece 218",
    "[WIDE] the GG notebook (sub_00115030): laid out for the wide screen",
    "[WIDE] menu part 194 in the GG notebook, the area name (a sign, flags 80000000): "
    "marked for the left edge",
    "[WIDE] the garage's system menu: 2 moving rectangles in its model 00010000 (animation "
    "00011000), piece 197, stretched across over the filled sides",
    "[WIDE] menu part 198, the title's top-left \"please press start\" (its pack's model 1): "
    "marked for the left edge",
    "[WIDE] the title (class 001CCEF8) in its state 3: widened from its first "
    "\"please press start\" on",
    "[WIDE] the save graffiti's character select (sub_00115A40): laid out for the wide screen",
    "[WIDE] menu part 199, the title's logo at the bottom right (its pack's model 0): "
    "marked for the right edge",
    "[WIDE] menu part 200, the title's own draws in its state 0: its bars across "
    "(in every state but the city's)",
    "[WIDE] t=73.0 the title's state 3",
    "[WIDE] a moving rectangle in the garage's system menu drawn through sub_0005BD80",
    "[WIDE] a moving rectangle in the garage's options menu drawn through sub_0005C990",
    "[WIDE] the garage's options menu: 2 moving rectangles in its model 00010000 (animation "
    "00011000) by the wider rule, piece 197, stretched across over the filled sides",
    "[WIDE] the garage's options menu: no moving rectangles in its model 00010040 (animation "
    "00011000), by either rule; its nodes:",
    "[WIDE]   node 3, depth 1: no mesh, track 00, its translation not keyed",
    "[WIDE] the garage's test run menu: 6 moving rectangles in its model 00010020 (animation "
    "00011000), piece 197, stretched across over the filled sides; 2 of them in a chain of "
    "sliding parts, with what carries it, stretched alike about its origin; its nodes:",
    "[WIDE]   node 1, depth 1: a mesh, track 01, x -40.00..50.00 y 0.00..0.00 z 0.00..0.00: "
    "stretched across, with the band\n",
    "[WIDE]   node 4, depth 1: a mesh, track 01, x 0.00..0.00 y 0.00..0.00 z 0.00..0.00: "
    "carries a chain of sliding parts: stretched with it, about its own origin\n",
    "[WIDE]   node 5, depth 2: a mesh, track 01, x 0.00..12.00 y 0.00..0.00 z 0.00..0.00: "
    "in a chain of sliding parts: stretched alike, about its carrier's origin\n",
    "[WIDE]   node 6, depth 3: a mesh, track 01, x 0.00..12.00 y 0.00..0.00 z 0.00..0.00: "
    "in a chain of sliding parts: stretched alike, about its carrier's origin\n",
    "[WIDE] the garage's practice menu: 7 moving rectangles in its model 00010060 (animation "
    "00011000) by the wider rule, piece 197, stretched across over the filled sides; 2 of them "
    "in a chain of sliding parts, stretched alike; its nodes:",
    "[WIDE]   node 4, depth 2: a mesh, track 10, its translation not keyed: stretched across, "
    "with the band\n",
    "[WIDE]   node 5, depth 1: a mesh, track 11, x 0.00..0.00 y 0.00..0.00 z 0.00..0.00\n",
    "[WIDE]   node 6, depth 2: a mesh, track 11, x -40.00..50.00 y 0.00..0.00 z 0.00..0.00: "
    "stretched across, with the band\n",
    "[WIDE]   node 7, depth 2: a mesh, track 11, x 0.00..12.00 y 0.00..0.00 z 0.00..0.00: "
    "in a chain of sliding parts: stretched alike\n",
    "[WIDE]   node 8, depth 3: a mesh, track 11, x 0.00..12.00 y 0.00..0.00 z 0.00..0.00: "
    "in a chain of sliding parts: stretched alike\n",
    "[WIDE] menu part 194 on its own, with no menu drawing, the area name (a sign, flags 80000000): "
    "marked for the left edge",
    "[WIDE] menu part 194 under the arrow to another area, the area name (a sign, flags 00000000, "
    "its parent of class 001D1DD0): marked for the left edge",
    "[WIDE] a sign with flags 00000000 (class 00000000): left where it is",
    "[WIDE] a sign with flags 58000000 (class 00000000): left where it is",
    "[WIDE] a sign with flags 40000000 (class 00000000): left where it is",
    "[WIDE] menu part 203, a result screen's banner (a sign, flags 14000000): moved to the "
    "right edge",
    "[WIDE] a result screen's banner, type 7 (+0x54), flags 14000000",
    "[WIDE] a result screen's banner, type 0 (+0x54), flags 14000000",
    "[WIDE] a sign with flags 15000000 (class 00000000): left where it is",
    "[WIDE] the garage's music menu: its moving rectangles left as drawn, not stretched",
    "[WIDE] the race preview (sub_00111320): laid out for the wide screen",
    "[WIDE] menu part 128 in the race preview, the backdrop and its grids (model 0x80) "
    "(+0x60): marked for two halves, moved out by whole cells",
    "[WIDE] menu part 130 in the race preview, the grey bar with Press the A Button (model 4) "
    "(+0x4C): marked for the right edge",
    "[WIDE] menu part 195 in the race preview, the header (the race's name, class 001D0770): "
    "marked for the left edge",
    "[WIDE] menu part 196, the race preview's ticker (class 001D07F8): across the whole width",
    "[WIDE] the garage's system menu: 1 moving part in its model 00010040 (animation "
    "00011000), too few for a band of rectangles (a knob or a cursor): left as drawn",
    "[WIDE] menu part 194 in the race preview, the area name (a sign, flags 80000000): "
    "marked for the left edge",
    "[WIDE] menu part 201, the pause map (mode 0, sub_00114A80): shown in the light grid, "
    "not over the dark grid at the top, its yellow lines carried to the screen's edges",
    "[WIDE] menu part 201, the pause map (mode 1, sub_001137B0): shown in the light grid, "
    "not over the dark grid at the top, its yellow lines carried to the screen's edges",
    "[WIDE] menu part 202, the race preview's course map (sub_00112470): shown in the light "
    "grid, not over the dark grid at the top",
    "[WIDE] menu part 204, the result screen's return to title and restart icons and static bar "
    "(sub_0010F670): moved to the right edge",
    "[WIDE] menu part 204, the result screen's return to title and restart icons and static bar: "
    "its label drawn at once, at the right edge",
    "[WIDE] the result screen's return to title and restart icons and static bar (sub_0010F670) "
    "drew an object of class 001D2538, not 001D24F0: left as it is",
    "[WIDE] the result screen's race time (sub_0010EC10) drew an object of class 001D24F0, "
    "not 001D23F0: left as it is",
    "[WIDE] the result screen's race time (sub_0010EC10) drew in mode 2 (+0x160), not the "
    "result screen's: left as it is",
    "[WIDE] menu part 205, the result screen's race time (sub_0010EC10): moved to the right edge",
    "[WIDE] menu part 205, the result screen's race time, mode 6: its right end at 389.0, moved "
    "164.0 further, under the banner's end (553)",
    "[WIDE] menu part 205, the result screen's race time, mode 4: its right end at 402.0, moved "
    "151.0 further, under the banner's end (553)",
    "[WIDE] menu part 205, the result screen's race time, mode 4: placed for a split screen "
    "(flags 80000000 at +0x44, y 0.0 at +0x14C): not moved further",
    "[WIDE] menu part 205, the result screen's race time, mode 6: its last digit's place 560.0: "
    "not moved further",
    "[WIDE] VS mode's setup screen (sub_00071100): its frames' sides filled, piece 219",
)
# Whole lines that come a set number of times: a node in each listing of
# the models whose nodes the log lists (a plain menu's first models, each
# once), as the same rig is listed for several.
COUNTED = {
    # the climbing n5, in the system menu's two listings and options' two
    "[WIDE]   node 5, depth 2: a mesh, track 01, x -40.00..50.00 y 0.00..3.00 z 1.50..1.50\n": 4,
}
PLAINS = 12
PARTS = 13 + 8 + 3 + 3 + 5

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
    for line, times in COUNTED.items():
        if text.count(line) != times:
            sys.exit(f"widescreen_menus: {line!r} came {text.count(line)} times, not {times}")
    marked = [l for l in text.splitlines() if l.startswith("[WIDE] menu part ") and "(+0x" in l]
    if len(marked) != PARTS or len(set(marked)) != PARTS:
        sys.exit(f"widescreen_menus: {len(marked)} part lines, not one for each of {PARTS}")
    if text.count("drew an object of class") != 5:
        sys.exit("widescreen_menus: a wrong class was named more than once")
    filled = [l for l in text.splitlines() if "its frames' sides filled" in l]
    if len(filled) != PLAINS or len(set(filled)) != PLAINS:
        sys.exit(f"widescreen_menus: {len(filled)} plain menus named, not each of {PLAINS} once")
    if "[CHECK]" in text:
        sys.exit("widescreen_menus: a check failed in the marks")
