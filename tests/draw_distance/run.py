"""Compile draw distance's setting, steps and log and the settings file code, and drive them without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c,
unchanged. nv2a_gl.c doesn't link on its own.

Takes port/draw_distance.c as it is, built with the in-game menu, and the
settings file code from src/nv2a_gl/nv2a_gl.c verbatim (sdl_cfg_path
through sdl_cfg_set, with nv_window_pref_num between them), as tests/intro
does; test_main.c stands in for the menu. Each case gets its own settings
file, passed as JSRF_SETTINGS_FILE, and its own run, since the setting is
read once. The gates are the game's code: test_main.c's stand-ins read
their constants as the game's do (recomp_manual.c, "draw distance").
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

# case: (settings file or None for none, log lines it must print)
cases = {
    "off": (None, ["[DRAW] objects at 1x (settings)"]),
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
    "log": ("drawdistance=1\n", [LOG, "props and traffic 2.0 (0.0), enemies 0.0 (0.0), "
                                      "attached part 0.0 (0.0)"]),
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
        if name == "log" and run.stderr.count("[DRAW] t=") != 1:
            raise SystemExit("log: not one [DRAW] line in five seconds:\n" + run.stderr)
        if name == "pick":
            kept = path.read_text()
            if "drawdistance=3\n" not in kept or "game_dir=/games/jsrf\n" not in kept:
                raise SystemExit("pick: the row did not keep drawdistance=3 alone:\n" + kept)
        print(run.stdout.strip())
print("draw distance: all", len(cases), "cases pass")
