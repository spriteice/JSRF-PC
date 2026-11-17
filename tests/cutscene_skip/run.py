"""Compile Skip cutscenes and the settings file code, and drive them without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c,
unchanged. nv2a_gl.c doesn't link on its own.

Takes port/cutscene_skip.c as it is, built with the in-game menu, and the
settings file code from src/nv2a_gl/nv2a_gl.c verbatim (sdl_cfg_path through
sdl_cfg_set, with nv_window_pref_num between them), as tests/intro does;
test_main.c stands in for the menu, and drives the two presses, the prompt's
fades and its places with made-up times. Each case gets its own settings
file, passed as JSRF_SETTINGS_FILE, and its own run, since the setting is
read once.
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
""" + settings + (root / "tests/cutscene_skip/test_main.c").read_text(encoding="utf-8")

# case: (settings file or None for none, log lines it must print)
cases = {
    "off": (None, ["[SKIP] skip cutscenes off (settings)",
                   "[SKIP] t=20.00 the prompt shown for a try (JSRF_SKIP_PROMPT_AT)"]),
    "on": ("skipcutscenes=1\n", ["[SKIP] skip cutscenes on (settings)",
                                 "A pressed in the scene at 00A00000: the prompt shows",
                                 "A pressed again: the scene at 00A00000 ends"]),
    "flip": ("game_dir=/games/jsrf\nskipcutscenes=0\n",
             ["[SKIP] skip cutscenes off (settings)", "[SKIP] skip cutscenes on (the menu)"]),
}

with tempfile.TemporaryDirectory(prefix="cutscene-skip-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    command = cc + ["-std=c11", "-DNV2A_GL_USE_SDL=1", "-DXBOXRECOMP_MENU=1",
               "-Iport", "-Isrc/menu", "-x", "c", "-", "port/cutscene_skip.c", "-o", exe]
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
            if "skipcutscenes=1\n" not in kept or "game_dir=/games/jsrf\n" not in kept:
                raise SystemExit("flip: the row did not keep skipcutscenes=1 alone:\n" + kept)
        print(run.stdout.strip())
print("cutscene_skip: all", len(cases), "cases pass")
