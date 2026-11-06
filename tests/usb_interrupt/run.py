"""Compile src/usb/ohci.c and src/kernel/kernel_interrupt.c, and deliver the
USB controller's interrupt beside the display's (test_main.c): every
interrupt routine and DPC must run holding the guest lock, one at a time,
and every DPC queued must run once (known bug 16); an interrupt object or
DPC at guest address 0 is turned away with a [CHECK] line. On Linux the
threads are src/platform/win32_compat.c's, on Windows the system's. Requires
GCC on PATH, including MinGW on Windows.
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
sources = ["tests/usb_interrupt/test_main.c", "src/usb/ohci.c", "src/kernel/kernel_interrupt.c"]
flags = []
if os.name != "nt":
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

with tempfile.TemporaryDirectory(prefix="usb_interrupt-") as directory:
    exe = str(pathlib.Path(directory) / "usb_interrupt.exe")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel", "-Isrc/usb",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("usb_interrupt: no end in 120 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "usb_interrupt: all cases pass" not in text:
        sys.exit(f"usb_interrupt: exit {run.returncode}")
    for what in ("a KINTERRUPT at guest address 0", "a KDPC at guest address 0"):
        if what not in text:
            sys.exit(f"usb_interrupt: no [CHECK] line for {what}")
