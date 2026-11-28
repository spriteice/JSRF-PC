"""Compile src/kernel/kernel_timer.c and start the kernel's timer thread
alone, as xbox_kernel_bridge_init does (test_main.c): KeTickCount must keep
up with the host's millisecond clock with no GPU thread running, a timer
set with KeSetTimer must fire its DPC once, and a timer at guest address 0
is turned away with a [CHECK] line. Run again with no worker stack for the
timer thread, and then with no TIB, the thread must not start, and say so
once. On Linux the thread is src/platform/win32_compat.c's, on Windows the
system's; the clock the timer's lateness figures read is
src/platform/host_info.c's. Requires GCC on PATH, including MinGW on Windows.
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
sources = ["tests/kernel_clock/test_main.c", "src/kernel/kernel_timer.c",
           "src/platform/host_info.c"]
if os.name == "nt":
    flags = []
else:
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

with tempfile.TemporaryDirectory(prefix="kernel_clock-") as directory:
    exe = str(pathlib.Path(directory) / "kernel_clock.exe")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)

    def run(*mode):
        name = " ".join(["kernel_clock", *mode])
        try:
            done = subprocess.run([exe, *mode], capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired:
            sys.exit(f"{name}: no end in 120 s")
        text = done.stdout + done.stderr
        print(text, end="")
        if done.returncode != 0 or f"{name}: all cases pass" not in text:
            sys.exit(f"{name}: exit {done.returncode}")
        return text

    text = run()
    if "[CHECK] kernel_timer.c:" not in text or "a KTIMER at guest address 0" not in text:
        sys.exit("kernel_clock: no [CHECK] line for the timer at guest address 0")
    for mode, what in (("no-stack", "no worker stack"), ("no-tib", "no TIB")):
        line = f"timer thread has {what}; it doesn't start: no clock, timers or DPCs"
        if run(mode).count(line) != 1:
            sys.exit(f"kernel_clock {mode}: \"{line}\" not said once")
