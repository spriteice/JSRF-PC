"""Compile draw distance's and Far buildings' settings, steps, areas and log and the settings file code, and drive them without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c,
unchanged. nv2a_gl.c doesn't link on its own.

Takes port/draw_distance.c as it is, built with the in-game menu, and the
settings file code from src/nv2a_gl/nv2a_gl.c verbatim (sdl_cfg_path
through sdl_cfg_set, with nv_window_pref_num between them), as tests/intro
does; test_main.c stands in for the menu. Each case gets its own settings
file, passed as JSRF_SETTINGS_FILE, and its own run, since the setting is
read once. The gates are the game's code: test_main.c's stand-ins read
their constants as the game's do (recomp_manual.c, "draw distance"); the
areas are a made-up map's.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
settings = gl[gl.index("static void sdl_cfg_path(char *buf, size_t n)"):
              gl.index("/* nv2a_widescreen() asks this when RECOMP_WIDESCREEN is not set. */")]
source = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)
#endif
""" + settings + (root / "tests/draw_distance/test_main.c").read_text(encoding="utf-8")

LOG = "[DRAW] t=5.1 2x, a frame's objects (past 1x's distance): crowds 3.0 (1.0), pickups 0.0 (0.0)"
AREAS = "Far buildings, a set's areas: the game's 2.0, added 3.0, copies left out 1.0"
MAP = ["[DRAW] Far buildings: the map has 7 areas; lists name 6 of them, 1 a copy of another; no list names 5 (a copy of area 0, (0, 500, 0) away)",
       "[DRAW] area 0: fog 100 to 1000, density 1",
       "[DRAW] area 4: fog 500 to 5000, density 1, a copy of area 0, (400, 0, 0) away",
       "[DRAW] area 6: fog 700 to 7000, density 1"]

# The Misc group's log (test_main.c, check_misc): object 0's first line, and
# the line that says a map's log stops.
MISC_FIRST = "[DRAW] Misc group 01E5E1C0: model 2C40, in area 0, at 0 places"
MISC_STOP = "[DRAW] Misc group: 2048 lines on this map, the most; no more until the next map"
MISC_SECOND = "[DRAW] Misc group 01E5E760: model 2C50, in area 1, at 0 places"

# case: (settings file or None for none, log lines it must print[, environment])
cases = {
    "default": (None, ["[DRAW] objects at 2x (settings)", "[DRAW] Far buildings on (settings)"]),
    "off": ("drawdistance=0\nfarbuildings=0\n", ["[DRAW] objects at 1x (settings)"]),
    "2x": ("drawdistance=1\n", ["[DRAW] objects at 2x (settings)"]),
    "3x": ("drawdistance=2\n", ["[DRAW] objects at 3x (settings)"]),
    "4x": ("drawdistance=3\n", ["[DRAW] objects at 4x (settings)"]),
    "unlimited": ("drawdistance=4\r\n", ["[DRAW] objects at Unlimited (settings)"]),
    "bad_far": ("drawdistance=5\n", ["drawdistance=5 is not 0 to 4; using 1",
                                     "[DRAW] objects at 2x (settings)"]),
    "bad_half": ("drawdistance=2.5\n", ["drawdistance=2.5 is not 0 to 4; using 1"]),
    "bad_below": ("drawdistance=-1\n", ["drawdistance=-1 is not 0 to 4; using 1"]),
    "bad_word": ("drawdistance=far\n", ["drawdistance is not a number; using 1"]),
    "bad_nan": ("drawdistance=nan\n", ["drawdistance=nan is not 0 to 4; using 1"]),
    "pick": ("game_dir=/games/jsrf\ndrawdistance=0\n",
             ["[DRAW] objects at 1x (settings)", "[DRAW] objects at 4x (the menu)"]),
    "log": ("drawdistance=1\nfarbuildings=0\n", [LOG, "props and traffic 2.0 (0.0), enemies 0.0 (0.0), "
                                      "attached part 0.0 (0.0)"]),
    "far_off": ("farbuildings=0\n", ["[DRAW] Far buildings off (settings)"]),
    "far_on": ("farbuildings=1\n", ["[DRAW] Far buildings on (settings)", *MAP,
                                    "[DRAW] Far buildings: the map has 6 areas; lists name 5 of them, "
                                    "1 a copy of another; no list names 5 (a copy of area 0, (0, 500, 0) away)"]),
    "far_copies": ("farbuildings=1\n", ["[DRAW] JSRF_DRAW_COPIES: an area's copies drawn too", *MAP],
                   {"JSRF_DRAW_COPIES": "1"}),
    "far_flip": ("game_dir=/games/jsrf\nfarbuildings=0\n",
                 ["[DRAW] Far buildings off (settings)", "[DRAW] Far buildings on (the menu)", MAP[0]]),
    "far_log": ("drawdistance=0\nfarbuildings=1\n", ["[DRAW] t=5.1 1x, " + AREAS]),
    "far_log_2x": ("drawdistance=1\nfarbuildings=1\n",
                   ["[DRAW] t=5.1 2x, a frame's objects (past 1x's distance): crowds 1.0 (0.0), pickups 0.0 (0.0)",
                    "attached part 0.0 (0.0); " + AREAS]),
    "far_misc": ("farbuildings=1\n", [MISC_FIRST, MISC_FIRST.replace("at 0", "at 1"),
                                       MISC_FIRST.replace("at 0", "at 2"),
                                       "[DRAW] Misc group 01E80EA0: model 3250, in area 6, at 1 places",
                                       "[DRAW] Misc group 01E80EA0: model 3250, in area 6, at 2 places",
                                       "[DRAW] Misc group 01E5FDE0: model 2C90, in area 6, at 0 places",
                                       MISC_STOP, "[DRAW] Far buildings off (the menu)",
                                       "[DRAW] Far buildings on (the menu)"]),
}
# Cases that must not print a line: none of the map's or the Misc group's for
# Far buildings off, and no copies drawn without JSRF_DRAW_COPIES.
unwanted = {
    "far_off": ["[DRAW] Far buildings:", "[DRAW] area", "[DRAW] Misc group"],
    "far_on": ["JSRF_DRAW_COPIES"],
    "off": ["[DRAW] Far buildings:"],
}

with tempfile.TemporaryDirectory(prefix="draw-distance-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    command = cc + ["-std=c11", "-DNV2A_GL_USE_SDL=1", "-DXBOXRECOMP_MENU=1",
                    "-Iport", "-Isrc/menu", "-x", "c", "-", "port/draw_distance.c", "-lm", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, (text, logs, *extra) in cases.items():
        path = work / (name + ".cfg")
        if text is not None:
            path.write_bytes(text.encode())
        env = {k: v for k, v in os.environ.items() if k != "JSRF_DRAW_COPIES"}
        env.update(*extra, JSRF_SETTINGS_FILE=str(path))
        run = subprocess.run([exe, name], env=env, capture_output=True, text=True, timeout=10)
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        for line in logs:
            if line not in run.stderr:
                raise SystemExit(f"{name}: no '{line}' in the log:\n{run.stderr}")
        for line in unwanted.get(name, []):
            if line in run.stderr:
                raise SystemExit(f"{name}: '{line}' in the log:\n{run.stderr}")
        if name in ("log", "far_log", "far_log_2x") and run.stderr.count("[DRAW] t=") != 1:
            raise SystemExit(f"{name}: not one [DRAW] line in five seconds:\n" + run.stderr)
        if name == "far_on" and run.stderr.count("[DRAW] Far buildings: the map") != 2:
            raise SystemExit("far_on: the map not logged once each:\n" + run.stderr)
        if name == "far_misc":
            # On the first map 99 models and areas once each and 68 new highs;
            # on the second the most, 2048, and the line saying so once; then
            # the first map again, object 0 named again; then, Far buildings
            # off and on, object 1 named once on the second map, after Far
            # buildings has read it. b2's log, which remembered 64 objects,
            # wrote each object every frame: 18,000 lines on the first map.
            lines = run.stderr.splitlines()
            named = sum(line.startswith("[DRAW] Misc group 0") for line in lines)
            if (named, lines.count(MISC_STOP), lines.count(MISC_FIRST)) != (167 + 2048 + 1 + 1, 1, 3):
                raise SystemExit(f"far_misc: {named} Misc group lines, {lines.count(MISC_STOP)} stops and "
                                 f"{lines.count(MISC_FIRST)} of object 0's first; wanted 2217, 1 and 3")
            if lines.count(MISC_SECOND) != 3:
                raise SystemExit(f"far_misc: object 1 named {lines.count(MISC_SECOND)} times; wanted 3, "
                                 "once a map, and once after Far buildings was turned on")
            for high in ("at 1 places", "at 2 places"):
                if lines.count(MISC_FIRST.replace("at 0 places", high)) != 1:
                    raise SystemExit(f"far_misc: object 0 not named once {high}")
        if name == "pick":
            kept = path.read_text()
            if "drawdistance=3\n" not in kept or "game_dir=/games/jsrf\n" not in kept:
                raise SystemExit("pick: the row did not keep drawdistance=3 alone:\n" + kept)
        if name == "far_flip":
            kept = path.read_text()
            if "farbuildings=1\n" not in kept or "game_dir=/games/jsrf\n" not in kept:
                raise SystemExit("far_flip: the row did not keep farbuildings=1 alone:\n" + kept)
        print(run.stdout.strip())
print("draw distance: all", len(cases), "cases pass")
