"""Compile draw distance's, Far buildings' and Far shadows' settings, steps, areas and log, Developer > Far buildings test's rows, and the settings file code, and drive them without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c,
unchanged. nv2a_gl.c doesn't link on its own.

Takes port/draw_distance.c as it is, built with the in-game menu, and the
settings file code from src/nv2a_gl/nv2a_gl.c verbatim (sdl_cfg_path
through sdl_cfg_set, with nv_window_pref_num between them), as tests/intro
does; test_main.c stands in for the menu. Each case gets its own settings
file, passed as JSRF_SETTINGS_FILE, and its own run, since the setting is
read once. The gates are the game's code: test_main.c's stand-ins read
their constants as the game's do (recomp_manual.c, "draw distance"); the
areas are a made-up map's, and the shadow passes' set is checked as the
game's memory would hold it.
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
# Far buildings 3x and Far shadows Off from the bottom of test_main.c's map:
# the game's 2 areas, 4 added (areas 2, 3, 4 and the side street, 9), and the
# shadows from the game's own.
AREAS = ("Far buildings 3x, a set's areas: the game's 2.0, added 4.0; "
         "Far shadows Off, shadow passes a frame 2.0, each adding 0.0 areas")


def map_lines(step):
    """The lines test_main.c's map gives at a step: the map, with the one area
    no list names, and the fog of each listed area, the first and the last."""
    return [f"[DRAW] Far buildings {step}: the map has 11 areas; lists name 10 of them; no list names 10",
            "[DRAW] area 0: fog 100 to 1000, density 1",
            "[DRAW] area 9: fog 1000 to 10000, density 1"]


# What the [DRAW] line adds while Far buildings is on, from the bottom at 3x:
# the eye's area, its areas since the line before, and what Far buildings adds.
EYE = "; the eye in area 0, in 0 since the last line; Far buildings adding 2, 3, 4, 9"
# Developer > Far buildings test (test_main.c, check_far_test): each press
# with the eye's area and what Far buildings adds there.
TEST = "(the menu); the eye in area 0, Far buildings 3x adding 2, 3, 4, 9"

# The Misc group's log (test_main.c, check_misc): object 0's first line, and
# the line that says a map's log stops.
MISC_FIRST = "[DRAW] Misc group 01E5E1C0: model 2C40, in area 0, at 0 places"
MISC_STOP = "[DRAW] Misc group: 2048 lines on this map, the most; no more until the next map"
MISC_SECOND = "[DRAW] Misc group 01E5E760: model 2C50, in area 1, at 0 places"

# case: (settings file or None for none, log lines it must print)
cases = {
    "default": (None, ["[DRAW] objects at 1x (settings)", "[DRAW] Far buildings 1x (settings)",
                       "[DRAW] Far shadows 1x (settings)"]),
    "off": ("drawdistance=0\nfarbuildingsahead=0\n", ["[DRAW] objects at 1x (settings)"]),
    "2x": ("drawdistance=1\n", ["[DRAW] objects at 2x (settings)"]),
    "3x": ("drawdistance=2\n", ["[DRAW] objects at 3x (settings)"]),
    "4x": ("drawdistance=3\n", ["[DRAW] objects at 4x (settings)"]),
    "unlimited": ("drawdistance=4\r\n", ["[DRAW] objects at Unlimited (settings)"]),
    "bad_far": ("drawdistance=5\n", ["drawdistance=5 is not 0 to 4; using 0",
                                     "[DRAW] objects at 1x (settings)"]),
    "bad_half": ("drawdistance=2.5\n", ["drawdistance=2.5 is not 0 to 4; using 0"]),
    "bad_below": ("drawdistance=-1\n", ["drawdistance=-1 is not 0 to 4; using 0"]),
    "bad_word": ("drawdistance=far\n", ["drawdistance is not a number; using 0"]),
    "bad_nan": ("drawdistance=nan\n", ["drawdistance=nan is not 0 to 4; using 0"]),
    "pick": ("game_dir=/games/jsrf\ndrawdistance=0\n",
             ["[DRAW] objects at 1x (settings)", "[DRAW] objects at 4x (the menu)"]),
    "log": ("drawdistance=1\nfarbuildingsahead=0\n", [LOG, "props and traffic 2.0 (0.0), enemies 0.0 (0.0), "
                                                      "attached part 0.0 (0.0)"]),
    # Far buildings' and Far shadows' steps, each from its own key.
    "far_off": ("farbuildingsahead=0\n", ["[DRAW] Far buildings Off (settings)"]),
    "reach_1x": ("farbuildingsahead=1\n", ["[DRAW] Far buildings 1x (settings)", *map_lines("1x")]),
    "reach_2x": ("farbuildingsahead=2\n", ["[DRAW] Far buildings 2x (settings)", *map_lines("2x")]),
    "reach_3x": ("farbuildingsahead=3\nfarshadowsahead=2\n",
                 ["[DRAW] Far buildings 3x (settings)", "[DRAW] Far shadows 2x (settings)", *map_lines("3x"),
                  "[DRAW] Far buildings 3x: the map has 10 areas; lists name 10 of them\n"]),
    "reach_all": ("farbuildingsahead=4\nfarshadowsahead=4\n",
                  ["[DRAW] Far buildings All (settings)", "[DRAW] Far shadows All (settings)",
                   *map_lines("All")]),
    # A key the rows don't have: 1x, the default, and the log says so.
    "far_bad": ("farbuildingsahead=5\nfarshadowsahead=1.5\n",
                ["[DRAW] settings: farbuildingsahead=5 is not 0 to 4; using 1",
                 "[DRAW] settings: farshadowsahead=1.5 is not 0 to 4; using 1",
                 "[DRAW] Far buildings 1x (settings)", "[DRAW] Far shadows 1x (settings)"]),
    "far_bad_word": ("farbuildingsahead=far\nfarbuildings=0\n",
                     ["[DRAW] settings: farbuildingsahead is not a number; using 1",
                      "[DRAW] Far buildings 1x (settings)"]),
    # The toggles kept before b2.1-hotfix, with no key of the new: 0 Off, and
    # 1, which drew every listed area, 3x, as b2.1-hotfix read it, though the
    # default is 1x since b2.2.
    "legacy_on": ("farbuildings=1\nfarshadows=1\n",
                  ["[DRAW] Far buildings 3x (settings)", "[DRAW] Far shadows 3x (settings)"]),
    "legacy_off": ("farbuildings=0\nfarshadows=0\n",
                   ["[DRAW] Far buildings Off (settings)", "[DRAW] Far shadows Off (settings)"]),
    "legacy_mixed": ("farbuildings=1\nfarshadows=0\n",
                     ["[DRAW] Far buildings 3x (settings)", "[DRAW] Far shadows Off (settings)"]),
    "far_pick": ("game_dir=/games/jsrf\nfarbuildingsahead=0\nfarbuildings=1\n",
                 ["[DRAW] Far buildings Off (settings)", "[DRAW] Far buildings 2x (the menu)", *map_lines("2x")]),
    "shadows_pick": ("game_dir=/games/jsrf\n",
                     ["[DRAW] Far shadows 1x (settings)", "[DRAW] Far shadows 2x (the menu)"]),
    "shadows": ("game_dir=/games/jsrf\n",
                ["[DRAW] Far buildings 1x (the menu)", "[DRAW] Far shadows All (the menu)",
                 "[DRAW] Far buildings All (the menu)", "[DRAW] Far shadows Off (the menu)"]),
    "far_log": ("drawdistance=0\nfarbuildingsahead=3\nfarshadowsahead=0\n", ["[DRAW] t=5.1 1x, " + AREAS + EYE]),
    "far_log_shadows": ("drawdistance=0\nfarbuildingsahead=3\nfarshadowsahead=3\n",
                        ["[DRAW] t=5.1 1x, " + AREAS.replace("Off, shadow passes a frame 2.0, each adding 0.0",
                                                             "3x, shadow passes a frame 2.0, each adding 4.0")]),
    "far_log_1x": ("drawdistance=0\nfarbuildingsahead=3\nfarshadowsahead=1\n",
                   ["[DRAW] t=5.1 1x, " + AREAS.replace("Off, shadow passes a frame 2.0, each adding 0.0",
                                                        "1x, shadow passes a frame 2.0, each adding 1.0")]),
    "far_log_2x": ("drawdistance=1\nfarbuildingsahead=3\nfarshadowsahead=0\n",
                   ["[DRAW] t=5.1 2x, a frame's objects (past 1x's distance): crowds 1.0 (0.0), pickups 0.0 (0.0)",
                    "attached part 0.0 (0.0); " + AREAS]),
    "far_misc": ("farbuildingsahead=3\n", [MISC_FIRST, MISC_FIRST.replace("at 0", "at 1"),
                                           MISC_FIRST.replace("at 0", "at 2"),
                                           "[DRAW] Misc group 01E80EA0: model 3250, in area 6, at 1 places",
                                           "[DRAW] Misc group 01E80EA0: model 3250, in area 6, at 2 places",
                                           "[DRAW] Misc group 01E5FDE0: model 2C90, in area 6, at 0 places",
                                           MISC_STOP, "[DRAW] Far buildings Off (the menu)",
                                           "[DRAW] Far buildings 3x (the menu)"]),
    "far_test": ("farbuildingsahead=3\nfarshadowsahead=1\n",
                 ["[DRAW] the eye at t=0.00: area 0, from -1; Far buildings adds 2, 3, 4, 9 there",
                  *(f"[DRAW] Far buildings test: leaving out area {a} " + TEST for a in (2, 3, 4, 9)),
                  "[DRAW] Far buildings test: leaving out none " + TEST,
                  "[DRAW] the eye at t=0.00: area 1, from 0; Far buildings adds 3, 4, 5, 9 there",
                  "[DRAW] Far buildings test: a new map, so area 2 is no longer left out",
                  "[DRAW] Far buildings test: the backdrop's depth writes as the game sets them (the menu)",
                  "[DRAW] Far buildings test: the backdrop's depth writes off, as Far buildings sets them "
                  "(the menu)"]),
    # Dogenzaka Hill's two rules (test_main.c, check_hill): each logged on the
    # hill's map, and left out, saying so, on a map of another size after its
    # stage.
    "far_hill": ("farbuildingsahead=1\nfarshadowsahead=1\n",
                 ["[DRAW] Far buildings: Dogenzaka Hill (stage 00010002), area 13 and what lies past it never "
                  "added from areas 0 and 16, where the game hides them",
                  "[DRAW] Far buildings: Dogenzaka Hill (stage 00010002), areas 0 to 14 and what lies past them "
                  "never added from areas 4 and 6, where the game hides them",
                  "[DRAW] the eye at t=0.00: area 0, from -1; Far buildings adds 19 there",
                  "[DRAW] the eye at t=0.00: area 16, from 0; Far buildings adds none there",
                  "[DRAW] Far buildings test: leaving out area 19 (the menu); the eye in area 0, "
                  "Far buildings 1x adding 19",
                  "[DRAW] Far buildings: stage 00010002 has 29 areas, not Dogenzaka Hill's 30, so its rule "
                  "is left out"]),
    # Chuo Street's rule for Far shadows (test_main.c, check_chuo): logged on
    # its map, and left out, saying so, on a map of another size after its
    # stage.
    "far_chuo": ("farbuildingsahead=2\nfarshadowsahead=2\n",
                 ["[DRAW] Far shadows: Chuo Street (stage 00010001), the shadows of areas 47, 48 and 50 never "
                  "added from any area, where they make patches of light",
                  "[DRAW] Far buildings: Chuo Street (stage 00010001), areas 49 and 51 and what lies past them "
                  "never added from areas 24, 25 and 28, where the game hides them",
                  "[DRAW] Far buildings: stage 00010001 has 58 areas, not Chuo Street's 59, so its rule is "
                  "left out"]),
    # Hikage Street's rule (test_main.c, check_hikage): logged on its map,
    # its areas as a run and the areas it hides them from as runs, and left
    # out, saying so, on a map of another size after its stage.
    "far_hikage": ("farbuildingsahead=4\nfarshadowsahead=4\n",
                   ["[DRAW] Far buildings: Hikage Street (stage 00010003), areas 14 to 16 and what lies past them "
                    "never added from areas 0, 2 to 6, 8 to 11 and 17, where the game hides them",
                    "[DRAW] Far buildings: stage 00010003 has 45 areas, not Hikage Street's 46, so its rule is "
                    "left out"]),
    # Kibogaoka Hill's three rules (test_main.c, check_kibogaoka): each logged
    # on its map, and each left out on a map of another size.
    "far_kibogaoka": ("farbuildingsahead=1\nfarshadowsahead=1\n",
                      ["[DRAW] Far buildings: Kibogaoka Hill (stage 00020002), areas 4, 6 and 56 to 121 and what "
                       "lies past them never added from areas 23, 27, 31 and 32, where the game hides them",
                       "[DRAW] Far shadows: Kibogaoka Hill (stage 00020002), the shadows of areas 115 to 117 never "
                       "added from any area, where they make patches of light",
                       "[DRAW] Far buildings: Kibogaoka Hill (stage 00020002), areas 96, 105 and 107 and what lies "
                       "past them never added from areas 7, 9, 13 and 20, where the game hides them",
                       "[DRAW] Far buildings: stage 00020002 has 123 areas, not Kibogaoka Hill's 124, so its rule "
                       "is left out"]),
    # 99th Street's rule for its first scene (test_main.c, check_99th):
    # logged on its map, a line as the scene starts and as it is over, and
    # left out on a map of another size.
    "far_99th": ("farbuildingsahead=1\nfarshadowsahead=1\n",
                 ["[DRAW] Far buildings: 99th Street (stage 00030000), areas 0 to 62 never added from any area "
                  "while the game's scene 36 plays, whose shots it frames without them",
                  "[DRAW] t=0.00 the game's scene 36 plays: 99th Street's rule for it holds",
                  "[DRAW] t=0.00 the game's scene 36 is over: 99th Street's rule for it no longer holds",
                  "[DRAW] Far buildings: stage 00030000 has 62 areas, not 99th Street's 63, so its rule is "
                  "left out"]),
    "whole_map": ("farbuildingsahead=0\n", ["[DRAW] Far buildings Off (settings)"]),
    "far_eye": ("farbuildingsahead=1\nfarshadowsahead=0\n",
                ["[DRAW] the eye at t=0.00: area 0, from -1; Far buildings adds 2 there",
                 "[DRAW] the eye at t=0.10: area 8, from 0; Far buildings adds 6 there",
                 "[DRAW] the eye at t=0.10: area 0, from 8; Far buildings adds 2 there",
                 "; the eye in area 4, in 0, 4, 8 since the last line (some changes not logged); "
                 "Far buildings adding 2, 6, 9",
                 "; the eye in area 4, in 4 since the last line; Far buildings adding 2, 6, 9",
                 "[DRAW] the eye at t=10.30: no area's list is the set, from 4; Far buildings adds 4, 9",
                 "[DRAW] the eye at t=0.00: area 0 (another area's list is the set too), from -1; "
                 "Far buildings adds none there"]),
}
# Cases that must not print a line: none of the map's or the Misc group's for
# Far buildings off, and no fog line for the area no list names.
unwanted = {
    "far_off": ["[DRAW] Far buildings Off:", "[DRAW] area", "[DRAW] Misc group"],
    "off": ["[DRAW] Far buildings Off:"],
    "reach_1x": ["[DRAW] area 10:"],
    "reach_all": ["[DRAW] area 10:"],
    "whole_map": ["[DRAW] area", ": the map has"],
    "legacy_on": ["[DRAW] settings:"],
}
# The pickers: each keeps its own key, and nothing else of Far buildings' or
# Far shadows', with the file's other keys as they were.
kept_after = {
    "far_pick": (["farbuildingsahead=2\n", "farbuildings=1\n", "game_dir=/games/jsrf\n"], ["farshadowsahead"]),
    "shadows_pick": (["farshadowsahead=2\n", "game_dir=/games/jsrf\n"], ["farbuildingsahead"]),
    "shadows": (["farbuildingsahead=4\n", "farshadowsahead=0\n", "game_dir=/games/jsrf\n"], []),
    # Developer > Far buildings test's rows aren't kept.
    "far_test": (["farbuildingsahead=3\nfarshadowsahead=1\n"], ["leave", "backdrop", "test"]),
}

with tempfile.TemporaryDirectory(prefix="draw-distance-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    command = cc + ["-std=c11", "-DNV2A_GL_USE_SDL=1", "-DXBOXRECOMP_MENU=1",
                    "-Iport", "-Isrc/menu", "-x", "c", "-", "port/draw_distance.c", "-lm", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, (text, logs) in cases.items():
        path = work / (name + ".cfg")
        if text is not None:
            path.write_bytes(text.encode())
        env = dict(os.environ, JSRF_SETTINGS_FILE=str(path))
        run = subprocess.run([exe, name], env=env, capture_output=True, text=True, timeout=10)
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        for line in logs:
            if line not in run.stderr:
                raise SystemExit(f"{name}: no '{line}' in the log:\n{run.stderr}")
        for line in unwanted.get(name, []):
            if line in run.stderr:
                raise SystemExit(f"{name}: '{line}' in the log:\n{run.stderr}")
        if name.startswith("far_log") and run.stderr.count("[DRAW] t=") != 1:
            raise SystemExit(f"{name}: not one [DRAW] line in five seconds:\n" + run.stderr)
        if name == "far_eye":
            # 51 changes of the eye's area before the first [DRAW] line, 40 of
            # them logged; then the line, and three more after it.
            eyes = run.stderr.count("[DRAW] the eye at t=")
            if eyes != 40 + 2 or run.stderr.count("[DRAW] t=") != 2:
                raise SystemExit(f"far_eye: {eyes} lines of the eye's area, wanted 42, and "
                                 f"{run.stderr.count('[DRAW] t=')} [DRAW] lines, wanted 2:\n" + run.stderr)
        if name.startswith("reach_") and run.stderr.count(": the map has") != 2:
            raise SystemExit(f"{name}: the two maps not logged once each:\n" + run.stderr)
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
        if name in kept_after:
            kept = path.read_text()
            want, never = kept_after[name]
            if any(line not in kept for line in want) or any(key in kept for key in never):
                raise SystemExit(f"{name}: the file is not {want} without {never}:\n" + kept)
        print(run.stdout.strip())
print("draw distance: all", len(cases), "cases pass")
