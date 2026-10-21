"""Compile the actual free camera and settings code, and drive it without the game.

Takes port/freecam.c as it is, and the settings file code from
src/nv2a_gl/nv2a_gl.c verbatim (sdl_cfg_path through sdl_cfg_set, with
nv_window_pref_num between them). Each case gets its own settings file, passed
as JSRF_SETTINGS_FILE, and its own run, since the settings are read once.
Requires GCC on PATH, including MinGW on Windows.
"""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
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
""" + settings + (root / "tests/freecam/test_main.c").read_text(encoding="utf-8")

# case: (settings file or None for none, log lines it must print)
cases = {
    "off": (None, ["[FREECAM] off (settings)"]),
    "turn": ("freecam=1\n", ["[FREECAM] on (settings)", "following camera 00401000 (mode 3)"]),
    "invert": ("freecam=1\ninvert_x=1\ninvert_y=1\n", ["invert x 1 y 1"]),
    "sensitivity": ("freecam=1\nsensitivity_x=10\nsensitivity_y=2.5\r\n",
                    ["sensitivity x 10 y 2.5"]),
    "deadzone": ("freecam=1\ndeadzone=0.5\n", ["deadzone 0.5"]),
    "deadzone_all": ("freecam=1\ndeadzone=1\n", ["deadzone 1"]),
    "fallback": ("freecam=1\nsensitivity_x=2\nsensitivity_x=11\nsensitivity_y=abc\n"
                 "deadzone=-0.1\ninvert_x=yes\ninvert_y=nan\n",
                 ["sensitivity_x=11 is outside 1 to 10; using 5",
                  "sensitivity_y is not a number; using 5",
                  "deadzone=-0.1 is outside 0 to 1; using 0.05",
                  "invert_x is not a number; using 0",
                  "invert_y=nan is outside",
                  "invert x 0 y 0, sensitivity x 5 y 5, deadzone 0.05"]),
    "pitch": ("freecam=1\n", []),
    "steer": ("freecam=1\n", ["steering the camera", "idle: the follow camera is not running"]),
    "reset": ("freecam=1\n", ["from heading -90, pitch 0, as the game put it back behind the player"]),
    "toggle": ("game_dir=/games/jsrf\nfreecam=0\nsensitivity_x=7\n",
               ["[FREECAM] on (F8)", "[FREECAM] off (F8)"]),
}

with tempfile.TemporaryDirectory(prefix="freecam-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    command = ["gcc", "-std=c11", "-DNV2A_GL_USE_SDL=1", "-Iport", "-x", "c", "-",
               "port/freecam.c", "-o", exe, "-lm"]
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
        if name == "toggle" and "game_dir=/games/jsrf\n" not in path.read_text():
            raise SystemExit("toggle: F8 lost a line it does not own:\n" + path.read_text())
        print(run.stdout.strip())
print("freecam: all", len(cases), "cases pass")
