"""The boost's heat haze, widened with the camera (known bug 2), without the game.

Cut by text: from port/recomp_manual.c, which doesn't link on its own:
wide_haze_corners and its two constants, verbatim, built with the warnings
CI counts against stand-ins for the guest memory and the widening
(test_main.c), which play the game's part: placing the quad's corners, and
leaving them. The game's own placing, and that the haze then fills the
screen, need the game and the whole renderer (b3 row 9); the test build's
boost step checks those.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
manual = (root / "port/recomp_manual.c").read_text(encoding="utf-8")
here = root / "tests/boost_haze"


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


source = ("""
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
static ptrdiff_t g_xbox_mem_offset;
static int s_wide_on;
static float s_squeeze = 1.0f;
static unsigned s_logged;
static int wide_proj(void) { return s_wide_on; }
static float nv2a_wide_squeeze(void) { return s_squeeze; }
static double pad_now(void) { s_logged++; return 0.0; }
static uint32_t nv2a_gl_flips(void) { return 0; }
""" + cut(manual, "#define HAZE_VERTS", "/* ---- a graffiti mesh's draw")
    + (here / "test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="boost_haze-") as directory:
    exe = str(pathlib.Path(directory) / "boost_haze.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-x", "c", "-", "-lm", "-o", exe],
                   cwd=root, input=source, text=True, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("boost_haze: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "boost_haze: all cases pass" not in text:
        sys.exit(f"boost_haze: exit {run.returncode}")
