"""The renderer's content hash, without the game.

Takes vc_hash from src/nv2a_gl/nv2a_gl.c, verbatim, with the wider hash it
hands 256 bytes and more to (src/nv2a_gl/nv2a_hash.h), and checks the wider
hash's SSE2 and AVX2 ways against the same steps in plain C, that vc_hash's
own loop is unchanged under 256 bytes, that every changed bit and every
64 bytes moved change the hash, and that nothing is read past either end.

x86-64 Linux; requires GCC on PATH.
"""
import pathlib
import platform
import subprocess
import sys
import tempfile

if not sys.platform.startswith("linux") or platform.machine() not in ("x86_64", "AMD64"):
    print("vc_hash: skipped, x86-64 Linux only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")

# vc_hash as it is, and its own loop alone: the same text without the hand-over.
hash_fn = gl[gl.index("static uint64_t vc_rotl("):gl.index("static uint64_t vc_mix(")]
hand_over = hash_fn[hash_fn.index("#if defined(NV2A_HASH_WIDE)\n"):]
hand_over = hand_over[:hand_over.index("#endif\n") + len("#endif\n")]
loop_fn = hash_fn.replace(hand_over, "").replace("static uint64_t vc_hash(", "static uint64_t vc_hash_loop(")
loop_fn = loop_fn[loop_fn.index("static uint64_t vc_hash_loop("):]

source = ("""
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_hash.h"
""" + hash_fn + "\n" + loop_fn + "\n" + (root / "tests/vc_hash/test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="vc_hash-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    command = ["gcc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src/nv2a_gl"),
               "-x", "c", "-", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    run = subprocess.run([exe], capture_output=True, text=True, timeout=300)
    print(run.stdout + run.stderr, end="")
    if run.returncode != 0:
        sys.exit(f"vc_hash: exit {run.returncode}")
