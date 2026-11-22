"""nv2a_vsh_interp (src/nv2a_gl/nv2a_vsh_interp.c), the vertex program run on
the CPU, which the button prompts' upright buttons run for every vertex of a
face, every frame: a transform's position comes out right, and its trace
switch, RECOMP_GL_INTERP_TRACE, is looked up once, not on every instruction
(the graffiti menus at 25 fps on Windows, prompts17). Two cases: "once", the
switch set in the environment after the first run, which must trace nothing;
and "trace", the switch set from the start, which traces the first two runs
and no more. Built as the engine is, optimised and with NDEBUG. Requires GCC
on PATH, including MinGW on Windows; CC, when set, is the compiler instead.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
flags = ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror", "-Isrc/nv2a_gl"]
SWITCH = "RECOMP_GL_INTERP_TRACE"
END = "  [I] --- end of program"

with tempfile.TemporaryDirectory(prefix="vsh_interp-") as directory:
    exe = str(pathlib.Path(directory) / "vsh_interp.exe")
    subprocess.run(cc + [*flags, "src/nv2a_gl/nv2a_vsh_interp.c", "tests/vsh_interp/test_main.c",
                         "-lm", "-o", exe], cwd=root, check=True)
    # Each case: the switch in its environment from the start, and the runs
    # it must trace.
    for case, start, traced in (("once", None, 0), ("trace", "1", 2)):
        env = {k: v for k, v in os.environ.items() if k != SWITCH}
        if start is not None:
            env[SWITCH] = start
        try:
            run = subprocess.run([exe, case], capture_output=True, text=True, timeout=120, env=env)
        except subprocess.TimeoutExpired:
            sys.exit(f"vsh_interp {case}: no end in 120 s")
        print(run.stdout, end="")
        ends = sum(1 for line in run.stderr.splitlines() if line.startswith(END))
        lines = sum(1 for line in run.stderr.splitlines() if line.startswith("  [I] "))
        if run.returncode != 0 or f"vsh_interp {case}: pass" not in run.stdout:
            print(run.stderr, end="")
            sys.exit(f"vsh_interp {case}: exit {run.returncode}")
        if ends != traced or (traced == 0 and lines):
            sys.exit(f"vsh_interp {case}: {ends} runs traced ({lines} trace lines), not {traced}")
        print(f"vsh_interp {case}: {ends} runs traced, as it should")
