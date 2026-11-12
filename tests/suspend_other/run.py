"""Compile src/kernel/kernel_thread.c and src/kernel/xbox_heap.c, and suspend
one thread from another (test_main.c): a suspend of another thread is
counted, and the thread stops only at a checkpoint, so one suspended inside
the guest heap's lock can't hold it (known bug 49). The first run must pass.
The second, with RECOMP_SUSPEND_REAL=1, which stops a thread where it is,
must find the main thread's allocation waiting, or the first run tests
nothing; under the thread sanitizer (tests/run_all.py --tsan sets
TEST_SANITIZER) it's left out. On Linux the threads are
src/platform/win32_compat.c's, on Windows the system's. Requires GCC on PATH,
including MinGW on Windows.
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
sources = ["tests/suspend_other/test_main.c", "src/kernel/kernel_thread.c", "src/kernel/xbox_heap.c"]
flags = []
if os.name != "nt":
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

with tempfile.TemporaryDirectory(prefix="suspend_other-") as directory:
    exe = str(pathlib.Path(directory) / "suspend_other.exe")
    # -Wno-comment: kernel.h has a "/*" inside a comment, which is upstream's.
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Wno-comment",
                         "-Wno-unused-parameter", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    env = {k: v for k, v in os.environ.items() if k not in ("RECOMP_SUSPEND_REAL",)}
    try:
        run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("suspend_other: no end in 120 s")
    text = run.stdout + run.stderr
    # The heap's own lines on stderr only when it fails: it logs its first
    # allocations and every 512th.
    print(run.stdout if run.returncode == 0 else text, end="")
    if run.returncode != 0 or "suspend_other: all cases pass" not in text:
        sys.exit(f"suspend_other: exit {run.returncode}")

    if os.environ.get("TEST_SANITIZER"):
        print("suspend_other: the run with real suspends left out under the sanitizer")
        sys.exit(0)
    env["RECOMP_SUSPEND_REAL"] = "1"
    try:
        run = subprocess.run([exe, "real"], env=env, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("suspend_other: with real suspends, no end in 120 s")
    text = run.stdout + run.stderr
    said = [line for line in text.splitlines() if line.startswith("FAIL")]
    print("with real suspends:", said[0] if said else f"exit {run.returncode}")
    if run.returncode == 0 or not said or "known bug 49" not in said[0]:
        sys.exit("suspend_other: with real suspends no allocation waited, so this tests nothing")
    print("suspend_other: pass")
