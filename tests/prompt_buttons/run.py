"""The message boxes' A and B and the save point's Start drawn flat
(port/prompt_buttons.c), without the game.

Builds port/prompt_buttons.c as it is with test_main.c, which stands in for
guest memory with a made-up display set, matrix stack and push buffer, and
checks the log: one line for each button, in the box and out of it, the
first time it is walked, saying where it was drawn or why not, with each
node's sphere; the Start's ball found, in a model of three nodes and of
one, and not found, and its flat draw given again once its bubble is open,
with its pulse's range; and the make-space check's line once. Requires GCC on PATH,
including MinGW on Windows. CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))

LINES = (
    "[INPUT] button prompts: the message box's A (model 69) drawn as a flat picture: node 1 of 3, "
    "sphere 10.00 at 11.0, -3.0, 1000.5, scale 1.000, on screen at 331.0, 243.0, 28.1 pixels across "
    "(focal 1000.5); node 0 3.00 at 10.0, -5.0, 1000.0, node 1 10.00 at 11.0, -3.0, 1000.5, "
    "node 2 6.00 at 10.0, -5.0, 1000.0",
    "[INPUT] button prompts: a B (model 6A), not a message box's, drawn as a flat picture: node 1 of 3, "
    "sphere 10.00 at -19.0, 2.0, 500.5,",
    "[INPUT] button prompts: an A (model 69), not a message box's, not drawn: 3 nodes and no sphere in "
    "front (focal 500.0); node 0 3.00 at 0.0, 0.0, -100.0,",
    "[INPUT] button prompts: the save point's Start (model FE) has 3 nodes, their spheres' own radii "
    "30.00, 14.00, 8.00: node 2, the smallest, is the ball, drawn flat from now on, the rest as the game "
    "draws them",
    "[INPUT] button prompts: the save point's Start (model FE), not a message box's, drawn as a flat "
    "picture: node 2 of 3, sphere 12.00 at ",
    "[INPUT] button prompts: the save point's Start (model FE), not a message box's, at its flat draw 120, "
    "its sphere 11.20 to 12.80 over its last 60, drawn as a flat picture: node 2 of 3, sphere 12.80 at "
    "40.0, 50.4, 800.0, scale 1.600, on screen at 360.0, 189.6, 21.8 pixels across (focal 800.0); "
    "node 0 (the game's own) 48.00 at 40.0, 60.0, 800.0, node 1 (the game's own) 22.40 at 40.0, 68.0, "
    "800.0, node 2 12.80 at 40.0, 50.4, 800.0",
    "[INPUT] button prompts: the save point's Start (model FE) is one node, sphere 8.00: all of it drawn "
    "flat from now on",
    "[INPUT] button prompts: the save point's Start (model FE): no ball found in 8 draws (0 nodes): left "
    "as the game draws it",
)
# The lines checked whole: the A's first, and the Start's given again.
WHOLE = (LINES[0], LINES[5])

with tempfile.TemporaryDirectory(prefix="prompt_buttons-") as directory:
    exe = str(pathlib.Path(directory) / "prompt_buttons.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror", "-Iport", "-Isrc",
                         "-Isrc/nv2a_gl", "-Isrc/input", "tests/prompt_buttons/test_main.c",
                         "port/prompt_buttons.c", "-lm", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("prompt_buttons: no end in 60 s")
    print(run.stdout + run.stderr, end="")
    if run.returncode != 0 or "prompt_buttons: all cases pass" not in run.stdout:
        sys.exit(f"prompt_buttons: exit {run.returncode}")
    lines = [l for l in run.stderr.splitlines() if l.startswith("[INPUT] button prompts:")]
    for start in LINES:
        mine = [l for l in lines if l.startswith(start)]
        if len(mine) != 1 or (start in WHOLE and mine[0] != start):
            sys.exit(f"prompt_buttons: a line, once, starting:\n{start}\nnot:\n" + "\n".join(lines))
    if len(lines) != len(LINES):
        sys.exit(f"prompt_buttons: {len(LINES)} lines, not:\n" + "\n".join(lines))
    checks = [l for l in run.stderr.splitlines() if l.startswith("[CHECK]")]
    if len(checks) != 1 or "no flat button" not in checks[0]:
        sys.exit("prompt_buttons: the make-space check's line once, not:\n" + "\n".join(checks))
print("prompt_buttons: passed")
