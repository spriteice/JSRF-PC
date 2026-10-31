"""A frame the title spends loading is shown at its flip, not held.

Takes the M_FLIP_STALL case from src/nv2a_gl/nv2a_gl.c verbatim and has
test_main.c flip it through JSRF's frames: two display buffers it alternates
and an off-screen surface it draws into. On a normal frame the title copies
the last off-screen frame into a display buffer, clears the off-screen
surface (which ends a pass) and draws the scene. On a frame spent loading it
copies and clears but draws no scene, so nothing is drawn after the pass
ends. The copied buffer is still the console's next picture: those flips
must be shown, and a flip with nothing drawn at all, not even a copy, must
still be held and paced. When the loading frames end, one line says how
many there were and what was shown.
Requires a C compiler on PATH (cc, or CC).
"""
import os
import pathlib
import re
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "cc"))
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
case = gl[gl.index("    case M_FLIP_STALL:\n"):
          gl.index("    default:\n        /* RECOMP_GL_UNHANDLED=1")]
define = re.search(r"^#define M_FLIP_STALL\b.*\n", gl, re.M).group(0)
head = """#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
""" + define + """
#define SURF_CACHE 8
static struct { int used; uint32_t offset, w, h, fmt, last_draw; } g_surf[SURF_CACHE];
static struct { int seen_flip_stall; uint32_t color_offset, clip_w, clip_h, surf_format,
                draws, since_present, clear_color; } S;
static int g_present_slot = -1;
static uint32_t g_last_flip_draws;
static int g_flip_drawn;
static int shown, shown_slot, paced;
static void fliplog(const char *why) { (void)why; }
static void frame_pace(void) { paced++; }
static void present(int is_frame)
{
    if (is_frame) { shown++; shown_slot = g_present_slot; }
    S.since_present = 0;
}
static void flip_stall(void)
{
    switch (M_FLIP_STALL) {
""" + case + """    }
}
"""
main = (root / "tests/flip_load/test_main.c").read_text(encoding="utf-8")

with tempfile.TemporaryDirectory(prefix="flip-load-") as tmp:
    exe = str(pathlib.Path(tmp) / "test")
    subprocess.run(cc + ["-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-variable",
                         "-x", "c", "-", "-o", exe],
                   input=head + main, text=True, check=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
    run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=60)
    lines = run.stderr.splitlines()
    if run.returncode != 0:
        raise SystemExit("flip_load: " + "\n".join(l for l in lines if l.startswith("[FAIL]")))
    burst = [l for l in lines if "[GL]" in l]
    if len(burst) != 1 or "3 frame(s) in a row drew no scene" not in burst[0] \
            or "surface 00200000" not in burst[0]:
        raise SystemExit("flip_load: expected one line for the 3 loading frames, "
                         "ending on surface 00200000, got:\n" + "\n".join(burst))
print("flip_load: loading frames shown with the buffer they copied into, "
      "an empty flip held and paced, one line for the run")
