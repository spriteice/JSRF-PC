"""Compile the Audio menu's setting and the settings file code, and drive them without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c,
unchanged. nv2a_gl.c doesn't link on its own.

Takes port/audio.c as it is, built with the in-game menu, and the settings
file code from src/nv2a_gl/nv2a_gl.c verbatim (sdl_cfg_path through
sdl_cfg_set, with nv_window_pref_num between them), as tests/intro does;
test_main.c stands in for the menu, the effects and the restart. Each case
gets its own settings file, passed as JSRF_SETTINGS_FILE, and its own run,
since the setting is read once: on with no audioeffects= or with 1, off
with 0, and RECOMP_APU_GPFX in the environment winning over it. The row
keeps its change in the settings file and asks for a restart; a preset
(File > Presets) keeps it through the row's keep, which doesn't. Requires GCC
on PATH, including MinGW on Windows.
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
""" + settings + (root / "tests/audio/test_main.c").read_text(encoding="utf-8")

# case: (settings file or None for none, RECOMP_APU_GPFX or None, log lines
# it must print, what the settings file must hold after)
cases = {
    "default": (None, None, ["[AUDIO] effects on (settings)"], None),
    "on": ("audioeffects=1\n", None, ["[AUDIO] effects on (settings)"], None),
    "off": ("audioeffects=0\n", None, ["[AUDIO] effects off (settings)"], None),
    "env_off": ("audioeffects=1\n", "0",
                ["[AUDIO] effects off (RECOMP_APU_GPFX=0 in the environment, over audioeffects=1)"],
                None),
    "env_on": ("audioeffects=0\n", "1",
               ["[AUDIO] effects on (RECOMP_APU_GPFX=1 in the environment, over audioeffects=0)"],
               None),
    "flip_off": ("game_dir=/games/jsrf\n", None,
                 ["[AUDIO] effects on (settings)", "[AUDIO] effects off from the next start (the menu)"],
                 "game_dir=/games/jsrf\naudioeffects=0\n"),
    "flip_on": ("game_dir=/games/jsrf\naudioeffects=0\n", None,
                ["[AUDIO] effects off (settings)", "[AUDIO] effects on from the next start (the menu)"],
                "game_dir=/games/jsrf\naudioeffects=1\n"),
    "keep_off": ("game_dir=/games/jsrf\n", None,
                 ["[AUDIO] effects on (settings)", "[AUDIO] effects off from the next start (the menu)"],
                 "game_dir=/games/jsrf\naudioeffects=0\n"),
}

with tempfile.TemporaryDirectory(prefix="audio-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    command = cc + ["-std=c11", "-Wall", "-Wextra", "-Wno-unused-function",
               "-DNV2A_GL_USE_SDL=1", "-DXBOXRECOMP_MENU=1", "-Iport", "-Isrc/menu", "-Isrc/apu",
               "-x", "c", "-", "port/audio.c", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, (text, switch, logs, kept) in cases.items():
        path = work / (name + ".cfg")
        if text is not None:
            path.write_bytes(text.encode())
        env = {k: v for k, v in os.environ.items() if k != "RECOMP_APU_GPFX"}
        env["JSRF_SETTINGS_FILE"] = str(path)
        if switch is not None:
            env["RECOMP_APU_GPFX"] = switch
        run = subprocess.run([exe, name], env=env, capture_output=True, text=True, timeout=10)
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        for line in logs:
            if line not in run.stderr:
                raise SystemExit(f"{name}: no '{line}' in the log:\n{run.stderr}")
        if kept is not None and path.read_text() != kept:
            raise SystemExit(f"{name}: the settings file holds:\n" + path.read_text())
        if kept is None and text is not None and path.read_text() != text:
            raise SystemExit(f"{name}: the settings file changed:\n" + path.read_text())
        print(run.stdout.strip())
print("audio: all", len(cases), "cases pass")
