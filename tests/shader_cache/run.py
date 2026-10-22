"""The binaries files that other drivers and engines left in the shader
cache's folder.

The shader cache (src/nv2a_gl/nv2a_shader_cache.c) as it is, with test_main.c,
on a fresh folder: nv2a_sc_sweep removes every binaries file but the one this
run keeps, and leaves the program and recipe files, and anything that isn't
the cache's.

Requires a C compiler on PATH: cc, or CC.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "cc"))
with tempfile.TemporaryDirectory(prefix="shader-cache-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    threads = [] if sys.platform == "win32" else ["-pthread"]
    subprocess.run(cc + ["-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Isrc/nv2a_gl",
                         "tests/shader_cache/test_main.c", "src/nv2a_gl/nv2a_shader_cache.c",
                         "-o", exe] + threads, cwd=root, check=True)
    (work / "shaders").mkdir()
    subprocess.run([exe, str(work / "shaders")], check=True, timeout=30)
