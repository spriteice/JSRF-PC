"""Compile music info in pause's setting, its reading of the sound manager, its song lookup and lines, and the settings file code, and drive them without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c,
unchanged. nv2a_gl.c doesn't link on its own.

Takes port/music_info.c as it is, built with the in-game menu, included in
test_main.c so its cases can walk the table of the street mixes, and the
settings file code from src/nv2a_gl/nv2a_gl.c verbatim (sdl_cfg_path
through sdl_cfg_set, with nv_window_pref_num between them), as tests/intro
does; test_main.c stands in for the menu, and for the game's memory with
one of its own. Each case gets its own settings file, passed as
JSRF_SETTINGS_FILE, and its own run, since the setting is read once. The
hooks that read the game's play position and draw the lines with its font
need the game: the test build's steps.
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
""" + settings + (root / "tests/music_info/test_main.c").read_text(encoding="utf-8")

# Each check the game's case makes fail, once: the place and what it says.
CHECKS = [
    "in music_info_now: known_id(now->id) failed: music id 77",
    "in music_info_now: current <= 1 failed: the music player heard is 2",
    "in music_info_now: in_guest(player, ADXT_SIZE, top) failed: music player 0 at 003FFFF8",
    "in music_info_now: in_guest(sound, MUSIC_SOUND_SIZE, top) failed: the sound manager at 00000000",
    "failed: the object manager at 00000000",
    "in music_info_song: tscale > 0 && ncount >= 0 failed: mix 3C's play time 44100 / 0",
    "failed: song 5's title at 00000000 or artist",
]
PICK = [
    "[MUSIC] t=1.0 the street pause menu shows the music: mix 3C at 0:00.0 (0 / 44100)",
    "[MUSIC] t=1.0 showing nothing: mix 3C at 0:00.0 (0 / 44100), before its first song",
    "[MUSIC] t=3.0 the other pause menu shows the music: mix 3C at ",
    "[MUSIC] t=4.0 showing No. 1: song id 1E",
]

# case: (settings file or None for none, log lines it must print)
cases = {
    "default": (None, ["[MUSIC] info in pause on (settings)"]),
    "off": ("musicinpause=0\n", ["[MUSIC] info in pause off (settings)"]),
    "on": ("musicinpause=1\n", ["[MUSIC] info in pause on (settings)"]),
    "flip": ("game_dir=/games/jsrf\nmusicinpause=0\n",
             ["[MUSIC] info in pause off (settings)", "[MUSIC] info in pause on (the menu)"]),
    "game": (None, CHECKS + PICK),
}

with tempfile.TemporaryDirectory(prefix="music_info-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    command = cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-DNV2A_GL_USE_SDL=1", "-DXBOXRECOMP_MENU=1",
                    "-Iport", "-Isrc", "-Isrc/menu", "-x", "c", "-", "-lm", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, (text, logs) in cases.items():
        path = work / (name + ".cfg")
        if text is not None:
            path.write_bytes(text.encode())
        env = dict(os.environ, JSRF_SETTINGS_FILE=str(path))
        run = subprocess.run([exe, name], env=env, capture_output=True, text=True, timeout=60)
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        for line in logs:
            if line not in run.stderr:
                raise SystemExit(f"{name}: no '{line}' in the log:\n{run.stderr}")
        # Each failed check says so once, however often its place is reached.
        if run.stderr.count("[CHECK]") != len(CHECKS if name == "game" else []):
            raise SystemExit(f"{name}: {run.stderr.count('[CHECK]')} [CHECK] lines:\n{run.stderr}")
        if name == "flip":
            kept = path.read_text()
            if "musicinpause=1\n" not in kept or "game_dir=/games/jsrf\n" not in kept:
                raise SystemExit("flip: the row did not keep musicinpause=1 alone:\n" + kept)
        print(run.stdout.strip())
print("music_info: all", len(cases), "cases pass")
