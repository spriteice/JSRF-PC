"""A batch longer than 16384 indices kept whole, every way the title sends one.

src/nv2a_gl/nv2a_gl.c whole (test_main.c includes it), with no surface, as
tests/index_runs builds it: no GL call is made, and the rest of the engine
is left unresolved. A batch of 35580 indices, the longest seen (in the
Fortified Residential Zone), goes in as 16-bit and 32-bit index words one at
a time and in runs, as DRAW_ARRAYS ranges, and as inline vertices; each way
must keep all of it, in order. Up to upstream's 9d21bd8 the renderer kept the
first 16384 and dropped the rest, so rails, rafters, walkways and houses
were missing on Kibogaoka Hill and in the Fortified Residential Zone.

Linux, with the EGL and GL headers (libegl-dev, libgl-dev); requires GCC on
PATH.
"""
import pathlib
import subprocess
import sys
import tempfile

if not sys.platform.startswith("linux"):
    print("big_batch: skipped, Linux only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
here = root / "tests/big_batch"

with tempfile.TemporaryDirectory(prefix="big-batch-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    subprocess.run(["gcc", "-O2", "-std=gnu11", "-w", "-no-pie", "-I", str(root / "src/nv2a_gl"),
                    "-I", str(root / "src/d3d"), str(here / "test_main.c"), "-o", exe,
                    "-Wl,--unresolved-symbols=ignore-all", "-lm"], check=True)
    run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
print(run.stdout.strip())
if run.returncode != 0:
    raise SystemExit(f"big_batch: exit {run.returncode}\n{run.stderr}")
