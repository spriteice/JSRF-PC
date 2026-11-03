"""Compile src/kernel/nv2a_intr.c, the NV2A's interrupt registers and the
interrupts the runtime raises on them, with src/kernel/kernel_mmio.c, the
device window's register access, and drive them with JSRF's D3D written out
in C (test_main.c), with no GPU thread. Software methods are delivered for
every parameter (RECOMP_NV2A_SWM=all), so that each is told from the next.
On Linux the threads and the clock are src/platform/win32_compat.c's, on
Windows the system's. Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
sources = ["tests/vblank_ack/test_main.c", "src/kernel/nv2a_intr.c", "src/kernel/kernel_mmio.c"]
if os.name == "nt":
    flags = []
else:
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

env = {k: v for k, v in os.environ.items()
       if k not in ("RECOMP_VBLANK", "RECOMP_VBLANK_HZ", "RECOMP_MMIO_TRACE")}
env["RECOMP_NV2A_SWM"] = "all"

with tempfile.TemporaryDirectory(prefix="vblank_ack-") as directory:
    exe = str(pathlib.Path(directory) / "vblank_ack.exe")
    # -Wno-comment: kernel.h has a "/*" inside a comment, which is upstream's.
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Wno-comment", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=300, env=env)
    except subprocess.TimeoutExpired:
        sys.exit("vblank_ack: no end in 300 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "vblank_ack: all cases pass" not in text:
        sys.exit(f"vblank_ack: exit {run.returncode}")
