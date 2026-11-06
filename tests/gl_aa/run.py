"""The OpenGL backend's anti-aliasing (SMAA, multisampling) on a headless EGL
context: test_main.c with nv2a_gfx_gl.c and nv2a_smaa.c as the engine builds
them, on OpenGL 3.3 core.

Linux, with EGL and Mesa (llvmpipe is enough): libegl-dev and libgl-dev.
Skips where there is no EGL context to be had.
CC, when set, is the compiler instead of cc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("gl_aa: skipped, Linux only (EGL)")
    sys.exit(77)   # skipped (tests/run_all.py)

root = pathlib.Path(__file__).resolve().parents[2]
sources = ["tests/gl_aa/test_main.c", "src/nv2a_gl/nv2a_gfx_gl.c", "src/nv2a_gl/nv2a_smaa.c",
           "src/nv2a_gl/nv2a_shader_cache.c", "src/nv2a_gl/nv2a_uniforms.c"]
cc = shlex.split(os.environ.get("CC", "cc"))
with tempfile.TemporaryDirectory(prefix="gl-aa-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    subprocess.run(cc + ["-std=gnu11", "-O1", "-Wall", "-Wextra", "-Isrc/nv2a_gl"]
                   + sources + ["-o", exe, "-lEGL", "-lGL", "-lm"], cwd=root, check=True)
    r = subprocess.run([exe], timeout=300)
sys.exit(1 if r.returncode != 0 else 0)
