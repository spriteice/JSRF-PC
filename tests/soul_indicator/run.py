"""Compile the graffiti soul indicator's setting, its reading of a mission's challenge, its state from frame to frame and its quad, and the settings file code, and drive them without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c,
unchanged. nv2a_gl.c doesn't link on its own.

Takes port/soul_indicator.c as it is, built with the in-game menu, and the
settings file code from src/nv2a_gl/nv2a_gl.c verbatim (sdl_cfg_path
through sdl_cfg_set, with nv_window_pref_num between them), as tests/intro
does; test_main.c stands in for the menu, and for the game's memory with
one of its own. Each case gets its own settings file, passed as
JSRF_SETTINGS_FILE, and its own run, since the setting is read once. The
hooks that hand it the mission flow and draw the quad after the gauge need
the game: the test build's steps. What they draw with is here: the quad's
vertices, and the once a pass of the game's loop it draws in.
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
""" + settings + (root / "tests/soul_indicator/test_main.c").read_text(encoding="utf-8")

# Each check the game's case makes fail, once: what it says.
CHECKS = [
    "in soul_indicator_read: in_guest(flow, SOUL_FLOW_SIZE, top) failed: the mission flow at 003FFFF8",
    "failed: mission 0009039D's script at 00000000",
    "failed: mission 0009039D's triggers at 00320000, 5000 of them",
    "in flag_list: n <= SOUL_LIST_MOST && in_guest(*at, 4 * n, top) failed: trigger 00320054's flags at ",
    "in course_record: c->course < count && c->player < SOUL_PLAYERS failed: course 3 of 1, player 0",
    "failed: course 0's record at 003FFFFC",
    "in first_step_fails: in_guest(entry, 8, top) failed: course 00340100's steps at 00000000",
]
# The hooks' side, in order, each once.
GAME = [
    "[SOUL] t=1.0 mssn0910: 13 sheets (trigger 14: MF 33 34 35 36 37 3D 3E 3F 40 41 47 48 49), "
    "its award RT[C4]",
    "[SOUL] t=3.0 shown: under way and on track",
    "[SOUL] t=4.0 a pulse: 2 of 13 sheets",
    "[SOUL] t=5.0 failed: red, then out",
    "[SOUL] t=7.0 mssn0925: a course (trigger 1: course 0, player 0), its award RT[C5]",
    "[SOUL] t=8.0 the course under way, at step 1",
    "[SOUL] t=8.0 shown: under way and on track",
    "[SOUL] t=8.6 the game failed the course, at step 1",
    "[SOUL] t=8.6 failed: red, then out",
    "[SOUL] t=8.7 the course under way, at step 0",
    "[SOUL] t=8.7 shown: under way and on track",
    "[SOUL] t=8.8 the course on to step 1",
    "[SOUL] t=8.9 the course done, at step 1",
    "[SOUL] t=9.0 done: the soul awarded, out",
    "[SOUL] t=10.0 mssn0935: no soul challenge to show",
]
# Once a pass of the game's loop: the first draw's pass, and the gauge's
# second draw in one loop pass, each said once.
ONCE = [
    "[SOUL] the gauge drew, on its pass 0: the badge goes with its first draw in each pass of "
    "the game's loop",
    "[SOUL] the gauge drew again in one pass of the game's loop, on its pass 3 after 0: the "
    "badge drawn with the first only",
]

# case: (settings file or None for none, log lines it must print)
cases = {
    "default": (None, ["[SOUL] visual indicator off (settings)",
                       "[SOUL] t=1.0 mssn0910: 13 sheets"]),
    "off": ("soulindicator=0\n", ["[SOUL] visual indicator off (settings)"]),
    "on": ("soulindicator=1\n", ["[SOUL] visual indicator on (settings)"]),
    "flip": ("game_dir=/games/jsrf\nsoulindicator=0\n",
             ["[SOUL] visual indicator off (settings)", "[SOUL] visual indicator on (the menu)"]),
    "game": ("soulindicator=1\n", CHECKS + GAME + ONCE),
}

with tempfile.TemporaryDirectory(prefix="soul_indicator-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    command = cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-DNV2A_GL_USE_SDL=1",
                    "-DXBOXRECOMP_MENU=1", "-Iport", "-Isrc", "-Isrc/menu", "-x", "c", "-",
                    "-x", "none", "port/soul_indicator.c", "-lm", "-o", exe]
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
            if run.stderr.count(line) != 1:
                raise SystemExit(f"{name}: '{line}' came {run.stderr.count(line)} times in the "
                                 f"log, not once:\n{run.stderr}")
        # Each failed check says so once, however often its place is reached.
        if run.stderr.count("[CHECK]") != len(CHECKS if name == "game" else []):
            raise SystemExit(f"{name}: {run.stderr.count('[CHECK]')} [CHECK] lines:\n{run.stderr}")
        if name == "game":
            # Nothing said but the lines above, the setting's and the checks'.
            said = [l for l in run.stderr.splitlines() if l.startswith("[SOUL] t=")]
            if said != GAME:
                raise SystemExit("game: the [SOUL] lines were:\n" + "\n".join(said))
        if name == "default" and ("shown" in run.stderr or "the gauge drew" in run.stderr):
            raise SystemExit("default: shown with the setting off:\n" + run.stderr)
        if name == "flip":
            kept = path.read_text()
            if "soulindicator=1\n" not in kept or "game_dir=/games/jsrf\n" not in kept:
                raise SystemExit("flip: the row did not keep soulindicator=1 alone:\n" + kept)
        print(run.stdout.strip())
print("soul_indicator: all", len(cases), "cases pass")
