"""The game's lines naming the A or B button, drawn with the button prompts'
pictures in the letters' places (port/prompt_text.c), without the game.

Builds port/prompt_text.c as it is with test_main.c, which stands in for
guest memory with made-up lines and a made-up font, and checks the log: the
line whose letter isn't where its table says, said once, and nothing else.
Requires GCC on PATH, including MinGW on Windows. CC, when set, is the
compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))

SAID = ["[INPUT] button prompts: the game's line at 00020300 hasn't the letter the table has: "
        "left as it is"]

with tempfile.TemporaryDirectory(prefix="prompt_text-") as directory:
    exe = str(pathlib.Path(directory) / "prompt_text.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror", "-Iport", "-Isrc",
                         "-Isrc/nv2a_gl", "-Isrc/input", "tests/prompt_text/test_main.c",
                         "port/prompt_text.c", "-lm", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("prompt_text: no end in 60 s")
    print(run.stdout + run.stderr, end="")
    if run.returncode != 0 or "prompt_text: all cases pass" not in run.stdout:
        sys.exit(f"prompt_text: exit {run.returncode}")
    lines = run.stderr.splitlines()
    if lines != SAID:
        sys.exit(f"prompt_text: the log says {lines}, not {SAID}")
print("prompt_text: passed")
