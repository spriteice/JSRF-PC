"""The OpenGL backend (src/nv2a_gl/nv2a_gfx_gl.c) on a real GL, without a window.

Compiled as the engine builds it, with the uniform table and shader cache it
links against, and driven through its device interface by each test_*.c here
on a headless EGL context (context.h): OpenGL 3.3 core (Mesa's llvmpipe will
do). Requires a C compiler on PATH (cc, or CC) and the EGL and GL headers and
libraries: Linux.
"""
import os
import pathlib
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
gl = root / "src/nv2a_gl"
cc = shlex.split(os.environ.get("CC", "cc"))

with tempfile.TemporaryDirectory(prefix="gl-backend-") as tmp:
    for test in sorted((root / "tests/gl_backend").glob("test_*.c")):
        exe = str(pathlib.Path(tmp) / test.stem)
        subprocess.run(cc + ["-std=c11", "-O1", "-Wall", "-Wextra", "-D_POSIX_C_SOURCE=200809L",
                             "-I" + str(gl), "-o", exe,
                             str(test), str(gl / "nv2a_gfx_gl.c"),
                             str(gl / "nv2a_uniforms.c"), str(gl / "nv2a_shader_cache.c"),
                             "-lEGL", "-lGL"],
                       check=True)
        subprocess.run([exe], check=True, timeout=60)
