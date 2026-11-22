"""The message boxes' A and B drawn flat (port/prompt_buttons.c), without the
game.

Builds port/prompt_buttons.c as it is with test_main.c, which stands in for
guest memory with a made-up display set, matrix stack and push buffer, and
checks the log: one line for each button, in the box and out of it, the
first time it is walked, saying where it was drawn or why not, with each
node's sphere; and the make-space check's line once. Requires GCC on PATH,
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
)

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
        if len(mine) != 1 or (start == LINES[0] and mine[0] != start):
            sys.exit(f"prompt_buttons: a line, once, starting:\n{start}\nnot:\n" + "\n".join(lines))
    if len(lines) != len(LINES):
        sys.exit(f"prompt_buttons: {len(LINES)} lines, not:\n" + "\n".join(lines))
    checks = [l for l in run.stderr.splitlines() if l.startswith("[CHECK]")]
    if len(checks) != 1 or "no flat button" not in checks[0]:
        sys.exit("prompt_buttons: the make-space check's line once, not:\n" + "\n".join(checks))
print("prompt_buttons: passed")
