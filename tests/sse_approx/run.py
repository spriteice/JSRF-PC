"""recomp_rcpss and recomp_rsqrtss (templates/runtime/recomp_types.h), the
runtime's RCPSS and RSQRTSS, give Intel's answers, the Xbox's, on any
processor: test_main.c holds them, read from an Intel processor. Built as
the engine is, optimised and with NDEBUG. Requires GCC on PATH, including
MinGW on Windows; CC, when set, is the compiler instead.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
flags = ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra",
         "-Itemplates/runtime", "-Isrc/kernel"]

with tempfile.TemporaryDirectory(prefix="sse_approx-") as directory:
    exe = str(pathlib.Path(directory) / "sse_approx.exe")
    subprocess.run(cc + [*flags, "tests/sse_approx/test_main.c", "-lm", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("sse_approx: no end in 120 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "sse_approx: all cases pass" not in text:
        sys.exit(f"sse_approx: exit {run.returncode}")
