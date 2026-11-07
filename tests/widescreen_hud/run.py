"""The HUD's marks (port/widescreen_hud.c), as the engine builds it:
optimised, NDEBUG, with the warnings CI counts, linked with the widescreen
unit (src/nv2a_gl/nv2a_wide.c) whose marks it writes, and driven by
test_main.c against a guest memory of its own that holds D3D's device and
push buffer. That the renderer then moves the piece needs the game and the
whole renderer: the test build's HUD step.
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
LOG = "[WIDE] HUD piece 0, the health bar (sub_001060B0): marked for the right edge"

with tempfile.TemporaryDirectory(prefix="widescreen_hud-") as directory:
    exe = str(pathlib.Path(directory) / "widescreen_hud.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-Iport", "-Isrc", "-Isrc/nv2a_gl", "tests/widescreen_hud/test_main.c",
                         "port/widescreen_hud.c", "src/nv2a_gl/nv2a_wide.c",
                         "src/nv2a_gl/nv2a_scale.c", "-lm", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("widescreen_hud: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "widescreen_hud: all cases pass" not in text:
        sys.exit(f"widescreen_hud: exit {run.returncode}")
    # The piece is named once, at its first mark, and not before; the check
    # on make-space says once that it left no room.
    if text.count(LOG) != 1:
        sys.exit(f"widescreen_hud: the piece's log line came {text.count(LOG)} times, not once")
    if text.count("[CHECK]") != 1 or "no mark" not in text:
        sys.exit(f"widescreen_hud: the make-space check said {text.count('[CHECK]')} lines, not one")
