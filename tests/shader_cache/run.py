"""The shader cache's files across starts.

The shader cache (src/nv2a_gl/nv2a_shader_cache.c) as it is, with test_main.c,
each start a run of its own: on a fresh folder, nv2a_sc_sweep removes the files
other drivers and engines left; on another, the NV2A states and programs one
start keeps come back at the next, and nv2a_sc_keep and nv2a_sc_state_keep
forget programs and states on disk too. See test_main.c for each step.

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
    subprocess.run([exe, str(work / "shaders"), "sweep"], check=True, timeout=30)
    (work / "states").mkdir()
    for phase in ("record", "keep", "reload", "damaged"):
        subprocess.run([exe, str(work / "states"), phase], check=True, timeout=30)
