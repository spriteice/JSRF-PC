"""Compile the follow camera's view kept out of walls (port/camera_walls.c),
its setting and the settings file code, and drive them without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c,
unchanged, for the setting's cases. nv2a_gl.c doesn't link on its own;
port/camera_walls.c is built whole, once, as the engine builds it (with the
settings file and the in-game menu), and linked into both programs: under
run_all.py --coverage a second build of it would replace the first's counts.

Takes port/camera_walls.c as it is, with test_main.c's made-up world of
planes and boxes in place of the game's line test, one case to a run.
camwall_clear: the eye's line, and lines across where the view starts,
against the ground, a room's corner, a wall along the view, a block's edge,
a corridor, a block beside the player, a pole, gaps narrower than the view
and a wall the player is right against. The game's own eye, where its wall
test put it, at 4:3, 16:9 and 21:9: open ground, and a pole between the
player and the eye, half way or right behind the player, leave it be; a
wall it sits on, turned 50 degrees, cuts the view only once widescreen
widens it, and at 70 degrees at 4:3 too; a wall along the view 0.9 away
only in widescreen; each cut view is cleared by moving across; and under a
half pipe's wall, or behind a wall that came between, it comes in to the
surface. camwall_share and camwall_across: in, or across, at
once; held for 20 frames; back by a tenth of the gap a frame; and all the
way out once the camera starts again (camwall_reset). camwall_for: each
camera's own, started again when its key changes, the one seen longest ago
given up for a new one. camwall_mode_on: every mode the follow camera's
post-update serves kept out of walls, but graffiti's (13).
Default cam fix (dcamfix=): the same port/camera_walls.c, with the
settings file code from src/nv2a_gl/nv2a_gl.c verbatim (sdl_cfg_path
through sdl_cfg_set, with nv_window_pref_num between them), as tests/intro
does; test_setting.c stands in for the menu. On with no dcamfix= and with
dcamfix=1, off with dcamfix=0, and its Controller row, under Free camera,
turning it off at once and keeping dcamfix=0, and off in File > Presets'
Xbox mode, on in the other two. Each case gets its own settings file,
passed as JSRF_SETTINGS_FILE, and its own run, since the setting is read
once.
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
setting_source = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)
#endif
""" + settings + (root / "tests/camera_walls/test_setting.c").read_text(encoding="utf-8")

# case: log lines it must print. camwall_clear's by: 0 nothing, 1 the eye's
# line, 2 coming in, 3 no room, 4 moving across, 5 as near as it goes
cases = {
    "wall": [],
    "across": [],
    "clear_open": ["clear_open: share 1.0000, 0.00 across, by 0, 5 lines"],
    "clear_back": ["clear_back: share 0.6000, 0.00 across, by 1"],
    "clear_floor": ["clear_floor: share 1.0000", "by 4"],
    "clear_room_corner": ["clear_room_corner: share 1.0000", "by 4"],
    "clear_alongside": ["clear_alongside: share 1.0000", "by 4"],
    "clear_block_edge": ["clear_block_edge: share 1.0000", "by 4"],
    "clear_corridor": ["clear_corridor: share 0.58", "by 4"],
    "clear_beside_player": ["clear_beside_player: share 1.0000, 0.00 across, by 0, 5 lines"],
    "clear_pole": ["clear_pole: share 1.0000, 0.00 across, by 0, 5 lines"],
    "clear_gap_behind": ["clear_gap_behind: share 0.", "by 2"],
    "clear_narrow": ["clear_narrow: share 1.0000, 0.00 across, by 3"],
    "clear_hugging": ["clear_hugging: share 0.2", "by 5"],
    # the game's own eye (eye_line 0) at 4:3, 16:9 and 21:9
    "game_open": ["game_open 0: share 1.0000, 0.00 across, by 0, 5 lines",
                  "game_open 2: share 1.0000, 0.00 across, by 0, 5 lines"],
    "game_pole": ["game_pole 0: share 1.0000, 0.00 across, by 0, 6 lines",
                  "game_pole 2: share 1.0000, 0.00 across, by 0, 6 lines"],
    "game_pole_near": ["game_pole_near 0: share 1.0000, 0.00 across, by 0, 6 lines",
                       "game_pole_near 2: share 1.0000, 0.00 across, by 0, 6 lines"],
    "game_half_pipe": ["game_half_pipe 0: share 0.4979, 0.00 across, by 1",
                       "game_half_pipe 2: share 0.4979, 0.00 across, by 1"],
    "game_wall_between": ["game_wall_between 0: share 0.5000, 0.00 across, by 1",
                          "game_wall_between 2: share 0.5000, 0.00 across, by 1"],
    "game_wall_50": ["game_wall_50 0: share 1.0000, 0.00 across, by 0",
                     "game_wall_50 1: share 1.0000", "game_wall_50 2: share 1.0000"],
    "game_wall_70": ["game_wall_70 0: share 1.0000", "game_wall_70 2: share 1.0000"],
    "game_alongside": ["game_alongside 0: share 1.0000, 0.00 across, by 0",
                       "game_alongside 1: share 1.0000", "game_alongside 2: share 1.0000"],
    "cameras": [],
    "modes": [],
}

# Default cam fix: case: (settings file or None for none, log lines it must print)
setting_cases = {
    "default": (None, ["[CAMWALL] default cam fix on (settings)"]),
    "on": ("dcamfix=1\n", ["[CAMWALL] default cam fix on (settings)"]),
    "off": ("dcamfix=0\n", ["[CAMWALL] default cam fix off (settings)"]),
    "flip": ("game_dir=/games/jsrf\n",
             ["[CAMWALL] default cam fix on (settings)", "[CAMWALL] default cam fix off (the menu)"]),
}

with tempfile.TemporaryDirectory(prefix="camera_walls-") as directory:
    work = pathlib.Path(directory)
    walls = str(work / "camera_walls.o")
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-DNV2A_GL_USE_SDL=1", "-DXBOXRECOMP_MENU=1",
                         "-Iport", "-Isrc/menu", "-c", "port/camera_walls.c", "-o", walls],
                   cwd=root, check=True)
    exe = str(work / "test.exe")
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Iport", "-Isrc/menu",
                         "tests/camera_walls/test_main.c", walls, "-o", exe, "-lm"],
                   cwd=root, check=True)
    for name, logs in cases.items():
        run = subprocess.run([exe, name], capture_output=True, text=True, timeout=10)
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        for line in logs:
            if line not in run.stderr:
                raise SystemExit(f"{name}: no '{line}' in the log:\n{run.stderr}")
        print(run.stdout.strip())
    exe = str(work / "setting.exe")
    # The settings file code cut from nv2a_gl.c has functions no case calls.
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Wno-unused-function",
                         "-DNV2A_GL_USE_SDL=1", "-DXBOXRECOMP_MENU=1",
                         "-Iport", "-Isrc/menu", "-x", "c", "-", "-x", "none", walls, "-o", exe, "-lm"],
                   cwd=root, input=setting_source, text=True, check=True)
    for name, (text, logs) in setting_cases.items():
        path = work / (name + ".cfg")
        if text is not None:
            path.write_bytes(text.encode())
        env = dict(os.environ, JSRF_SETTINGS_FILE=str(path))
        run = subprocess.run([exe, name], env=env, capture_output=True, text=True, timeout=10)
        if run.returncode != 0:
            raise SystemExit(f"setting {name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        for line in logs:
            if line not in run.stderr:
                raise SystemExit(f"setting {name}: no '{line}' in the log:\n{run.stderr}")
        if name == "flip":
            kept = path.read_text()
            if "dcamfix=0\n" not in kept or "game_dir=/games/jsrf\n" not in kept:
                raise SystemExit("setting flip: the row did not keep dcamfix=0 alone:\n" + kept)
        print(run.stdout.strip())
print("camera_walls: all", len(cases) + len(setting_cases), "cases pass")
