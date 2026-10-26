"""Compile the intro's setting and the settings file code, and drive them without the game.

Takes port/intro.c as it is, built with the in-game menu, and the settings
file code from src/nv2a_gl/nv2a_gl.c verbatim (sdl_cfg_path through
sdl_cfg_set, with nv_window_pref_num between them), as tests/freecam does;
test_main.c stands in for the menu. Each case gets its own settings file,
passed as JSRF_SETTINGS_FILE, and its own run, since the setting is read once.
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
""" + settings + (root / "tests/intro/test_main.c").read_text(encoding="utf-8")

# case: (settings file or None for none, log lines it must print)
cases = {
    "off": (None, ["[INTRO] skip off (settings)"]),
    "on": ("skipintro=1\n", ["[INTRO] skip on (settings)"]),
    "flip": ("game_dir=/games/jsrf\nskipintro=0\n",
             ["[INTRO] skip off (settings)", "[INTRO] skip on (the menu)"]),
}

with tempfile.TemporaryDirectory(prefix="intro-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    command = ["gcc", "-std=c11", "-DNV2A_GL_USE_SDL=1", "-DXBOXRECOMP_MENU=1",
               "-Iport", "-Isrc/menu", "-x", "c", "-", "port/intro.c", "-o", exe]
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
        if name == "flip":
            kept = path.read_text()
            if "skipintro=1\n" not in kept or "game_dir=/games/jsrf\n" not in kept:
                raise SystemExit("flip: the row did not keep skipintro=1 alone:\n" + kept)
        print(run.stdout.strip())
print("intro: all", len(cases), "cases pass")
