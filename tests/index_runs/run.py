"""Runs of index words handed over whole, against the same words one at a time.

The walk: src/kernel/nv2a_pb_scan.c as it is, over random commands
(test_walk.c), once with RECOMP_PB_SCAN, where it hands over each word on its
own, and once without, where it hands over runs of index words whole: the
executor must be handed the same words, with DMA_GET where it was, both ways.

The renderer: src/nv2a_gl/nv2a_gl.c whole (test_gl.c includes it), with no
surface, so it never starts and no GL call is made. Its GL and the rest of
the engine are left out and their names left unresolved: anything that
reached one would crash, and the test would fail. A run through
nv2a_gl_indices must leave every byte of its state as the same words through
nv2a_gl_method do.

Linux, with the EGL and GL headers (libegl-dev, libgl-dev); requires GCC on
PATH.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

if not sys.platform.startswith("linux"):
    print("index_runs: skipped, Linux only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
here = root / "tests/index_runs"

with tempfile.TemporaryDirectory(prefix="index-runs-") as directory:
    tmp = pathlib.Path(directory)
    walk = str(tmp / "walk")
    subprocess.run(["gcc", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", str(here / "test_walk.c"),
                    str(root / "src/kernel/nv2a_pb_scan.c"), "-o", walk], check=True)
    logs = {}
    for name in ("scan", "whole"):
        env = {k: v for k, v in os.environ.items() if k not in ("RECOMP_PB_SCAN", "RECOMP_PB_NOCALL")}
        env["RECOMP_PB_EXEC"] = "1"
        if name == "scan":
            env["RECOMP_PB_SCAN"] = "1"
        log = tmp / (name + ".log")
        run = subprocess.run([walk, str(log), name], env=env, capture_output=True, text=True, timeout=120)
        if run.returncode != 0:
            raise SystemExit(f"index_runs walk ({name}): exit {run.returncode}\n{run.stdout}")
        print("index_runs walk,", run.stdout.strip())
        logs[name] = log.read_text().splitlines()
    for n, (a, b) in enumerate(zip(logs["scan"], logs["whole"])):
        if a != b:
            raise SystemExit(f"index_runs walk: line {n} differs: one at a time '{a}', whole '{b}'")
    if len(logs["scan"]) != len(logs["whole"]):
        raise SystemExit(f"index_runs walk: {len(logs['scan'])} lines one at a time, "
                         f"{len(logs['whole'])} whole")
    print("index_runs walk: the same", len(logs["scan"]), "lines of log both ways")

    gl = str(tmp / "gl")
    subprocess.run(["gcc", "-O2", "-std=gnu11", "-w", "-no-pie", "-I", str(root / "src/nv2a_gl"),
                    "-I", str(root / "src/d3d"), str(here / "test_gl.c"), "-o", gl,
                    "-Wl,--unresolved-symbols=ignore-all", "-lm"], check=True)
    run = subprocess.run([gl], capture_output=True, text=True, timeout=120)
    print(run.stdout.strip())
    if run.returncode != 0:
        raise SystemExit(f"index_runs renderer: exit {run.returncode}\n{run.stderr}")
print("index_runs: all cases pass")
