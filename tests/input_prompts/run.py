"""Compile the button prompts' choices (src/input/input_prompts.c) with the
layouts (src/input/input_layout.c), and drive them without SDL.

Builds both whole, as the engine builds them, with the in-game menu's rows
(XBOXRECOMP_MENU); test_main.c stands in for the settings file
(button_prompts=) and the menu's xbox_MenuAdd.

The cases: a pad's kind from what SDL reports (PlayStation's types, the
Switch's, and Xbox for the rest, the PC's pad and the Deck's and the Ally
X's controls among them); the art for each style, kind and last
input, and the in-game menu's strip's style; each control's icon from the layouts, at the defaults and moved; the
prompts in use as presses and styles change, and the count the renderer
watches; and at start, the submenu and its row, Default in each preset,
with no line (Default), a style's line, Default's line and a line that is
no style, which the log says.
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

with tempfile.TemporaryDirectory(prefix="input_prompts-") as directory:
    exe = str(pathlib.Path(directory) / "input_prompts")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DXBOXRECOMP_MENU",
                    "-I", "src", "-I", "src/input", "-I", "src/menu",
                    "src/input/input_layout.c", "src/input/input_prompts.c",
                    "tests/input_prompts/test_main.c", "-o", exe], cwd=root, check=True)
    # Each case, its arguments, and the lines it must say in the log, in their order.
    cases = [
        (["kind"], []), (["art"], []), (["icon"], []),
        (["state"], ["[INPUT] button prompts: Auto", "[INPUT] button prompts: Xbox",
                     "[INPUT] button prompts: Default", "[INPUT] button prompts: Xbox"]),
        (["start"], ["[INPUT] button prompts: Nintendo"]),
        (["start", "2"], ["[INPUT] button prompts: Xbox", "[INPUT] button prompts: Nintendo"]),
        (["start", "0"], ["[INPUT] button prompts: Auto", "[INPUT] button prompts: Nintendo"]),
        (["start", "1"], ["[INPUT] button prompts: Nintendo"]),
        (["start", "6"], ["[INPUT] settings: button_prompts=6 is no style; Default kept",
                          "[INPUT] button prompts: Nintendo"]),
    ]
    for args, said in cases:
        name = " ".join(args)
        run = subprocess.run([exe] + args, capture_output=True, text=True, timeout=120)
        print(run.stdout + run.stderr, end="")
        if run.returncode != 0 or f"input_prompts {args[0]}: pass" not in run.stdout:
            sys.exit(f"input_prompts {name}: exit {run.returncode}")
        lines = run.stderr.splitlines()
        if lines != said:
            sys.exit(f"input_prompts {name}: the log says {lines}, not {said}")
print("input_prompts: all cases pass")
