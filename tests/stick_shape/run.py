"""Compile Stick shape (src/input/stick_shape.c) and drive it without the game.

Builds src/input/stick_shape.c whole, as the engine builds it (with the
in-game menu); test_main.c stands in for the settings file and the menu's
xbox_MenuAdd.

The square (stick_shape_square): a stick on an axis unchanged, either way
along it, at any push and at the axis's ends; a full push on each of the
four diagonals to the square's corner, and half a push half way to it; a
stick past the circle, as spriteice's pad reaches on its diagonals, and
-32768 on both axes, held to the axes' ends; -32768 on one axis with a
little on the other; the centre; and all the way round, at a full push and
half of one, the direction kept and the larger axis the push's length. The
pad (stick_shape_pad): Round leaves all four axes as they came, Square
takes each stick on its own, and a stick on an axis or at the centre is the
same either way. The setting: Square with no stick_shape=, with 0, and with
a value it doesn't know (and the log says so), Round with 1, each said in
the log; and its row, after Input's Behaviour submenu (with its hint) and
in it, Square then Round, with a hint the strip can hold, at Square in each
of File > Presets' presets, taking effect at once and kept in the settings,
said in the log, a choice out of range changing nothing.
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

with tempfile.TemporaryDirectory(prefix="stick_shape-") as directory:
    exe = str(pathlib.Path(directory) / "stick_shape")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DXBOXRECOMP_MENU",
                    "-I", "src", "-I", "src/input", "-I", "src/menu",
                    "src/input/stick_shape.c", "tests/stick_shape/test_main.c",
                    "-lm", "-o", exe], cwd=root, check=True)
    # Each case, and the lines it must say in the log, in their order.
    said = {
        "square": [],
        "default": ["[PAD] stick shape: Square (settings)",
                    "[PAD] stick shape: Round (the menu)",
                    "[PAD] stick shape: Square (the menu)"],
        "round": ["[PAD] stick shape: Round (settings)"],
        "squared": ["[PAD] stick shape: Square (settings)"],
        "unknown": ["[PAD] settings: stick_shape is not 0 or 1; using 0",
                    "[PAD] stick shape: Square (settings)"],
    }
    for case, lines in said.items():
        run = subprocess.run([exe, case], capture_output=True, text=True, timeout=60)
        print(run.stdout + run.stderr, end="")
        if run.returncode != 0 or f"stick_shape {case}: pass" not in run.stdout:
            sys.exit(f"stick_shape {case}: exit {run.returncode}")
        log = [line for line in run.stderr.splitlines() if line.startswith("[PAD]")]
        if log != lines:
            sys.exit(f"stick_shape {case}: the log said {log}, not {lines}")
print("stick_shape: all cases pass")
