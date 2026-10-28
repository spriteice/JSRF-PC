"""Compile the texture decode and check it texel for texel, without the game.

Takes decode_texel, decode_level and the loops it uses from
src/nv2a_gl/nv2a_gl.c, verbatim, and compares every format they decode,
swizzled and linear, against one texel at a time as they were decoded
before. Requires GCC on PATH.
"""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")

texel = gl[gl.index("static int fmt_is_dxt(uint32_t f)"):gl.index("/* A format with no alpha bits")]
level = gl[gl.index("static int g_upload_bias_bg;"):gl.index("/*\n * The title's own mipmap chain")]
source = ("""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d8_swizzle.h"
""" + texel + "\n" + level + (root / "tests/tex_decode/test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="tex_decode-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    command = ["gcc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-I", str(root / "src/d3d"),
               "-x", "c", "-", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    print(run.stdout + run.stderr, end="")
    if run.returncode != 0:
        sys.exit(f"tex_decode: exit {run.returncode}")
