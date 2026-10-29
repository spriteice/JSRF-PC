"""The combiner constants as uniforms, from the renderer's front half.

src/nv2a_gl/nv2a_gl.c whole (test_gl.c includes it), with no surface, so it
never starts and no GL call is made. Its GL and the rest of the engine are
left out and their names left unresolved: anything that reached one would
crash, and the test would fail. The shader cache and the device are
stand-ins in test_gl.c. The program key must stay the same through new
constants, the shader cache must be handed one state for them, and the
constants must reach the program when they change. tests/gl_backend's
test_psh_consts.c tests the shaders themselves.

Linux, with the EGL and GL headers (libegl-dev, libgl-dev); requires GCC on
PATH.
"""
import pathlib
import subprocess
import sys
import tempfile

if not sys.platform.startswith("linux"):
    print("psh_consts: skipped, Linux only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
here = root / "tests/psh_consts"

with tempfile.TemporaryDirectory(prefix="psh-consts-") as directory:
    exe = str(pathlib.Path(directory) / "gl")
    subprocess.run(["gcc", "-O2", "-std=gnu11", "-w", "-no-pie", "-I", str(root / "src/nv2a_gl"),
                    "-I", str(root / "src/d3d"), str(here / "test_gl.c"), "-o", exe,
                    "-Wl,--unresolved-symbols=ignore-all", "-lm"], check=True)
    run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    print(run.stdout.strip())
    if run.returncode != 0:
        raise SystemExit(f"psh_consts: exit {run.returncode}\n{run.stderr}")
