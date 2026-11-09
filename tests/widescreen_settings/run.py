"""Compile the window's widescreen settings and the settings file code, and read them without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c, unchanged,
through nv_window_pref_shape. nv2a_gl.c doesn't link on its own.

Takes the settings file code from src/nv2a_gl/nv2a_gl.c verbatim
(sdl_cfg_path through nv_window_pref_shape, which nv2a_widescreen() and its
shape ask at start), as tests/intro takes it up to nv_window_pref_widescreen,
linked with src/nv2a_gl/nv2a_wide.c for the shapes' keys. Each case gets its
own settings file, passed as JSRF_SETTINGS_FILE, or none.
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
              gl.index("/* The keyboard as a gamepad, by key POSITION")]
source = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "nv2a_wide.h"
#if defined(_WIN32)
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)
#endif
""" + settings + (root / "tests/widescreen_settings/test_main.c").read_text(encoding="utf-8")

# case: settings file, or None for none
cases = {
    "none": None,
    "other_keys": "game_dir=/games/jsrf\nscale=3\n",
    "old_wide": "widescreen=1\n",
    "old_43": "widescreen=0\n",
    "shape": "widescreen=1\naspect=21:9\r\n",
    "shape_43": "aspect=16:10\nwidescreen=0\n",
    "bad_shape": "widescreen=1\naspect=5:4\n",
}

with tempfile.TemporaryDirectory(prefix="widescreen-settings-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    command = cc + ["-std=c11", "-DNV2A_GL_USE_SDL=1", "-Isrc/nv2a_gl", "-x", "c", "-",
                    "-x", "none", "src/nv2a_gl/nv2a_wide.c", "-lm", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, text in cases.items():
        path = work / (name + ".cfg")
        if text is not None:
            path.write_bytes(text.encode())
        env = dict(os.environ, JSRF_SETTINGS_FILE=str(path))
        run = subprocess.run([exe, name], env=env, capture_output=True, text=True, timeout=10)
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        print(run.stdout.strip())
print("widescreen settings: all", len(cases), "cases pass")
