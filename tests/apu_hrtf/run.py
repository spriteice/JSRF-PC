"""Compile the placing of the 3D voices from their HRTF settings
(src/apu/apu_hrtf.h) and play voices through it.

Each ear's level must follow the size of its taps, the far ear turned down
by three quarters of the setting's difference in dB, with the louder ear
exactly the sound as it is; the delay must be on the quieter ear, whole or
between two samples, and no later than the most there is, all through a long
sound; levels and delays must glide to a new setting through two stages of
25 ms, half-way in about 42 ms, landing where the two stages put them at
each frame's end, in a straight line within the frame and with no corner,
then arrive and stay, each change counted once for the log; and a voice
starting again must start where its new setting places it. Then the time it
takes for 32 voices, shown. Built with the address and undefined behaviour
sanitizers where GCC has them, to catch a read outside the history, and
again without them for the timing. Requires GCC on PATH, including MinGW on
Windows.
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
cases = ["levels", "delay", "long", "glide", "moving", "start"]
failed = 0
with tempfile.TemporaryDirectory(prefix="apu_hrtf-") as directory:
    def build(name, flags):
        exe = str(pathlib.Path(directory) / name)
        # apu_state.h brings the APU's shims, which leave these two unused
        subprocess.run(cc + ["-std=gnu11", "-O2", "-Wall", "-Wextra",
                        "-Wno-unused-variable", "-Wno-cast-function-type", *flags,
                        "-Isrc/apu", "-Isrc", "-Isrc/nv2a", "tests/apu_hrtf/test_main.c",
                        "-o", exe, "-lm"], cwd=root, check=True)
        return exe

    checked = build("checked.exe", [] if sys.platform == "win32"
                    else ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"])
    timed = build("timed.exe", [])
    for case in cases:
        run = subprocess.run([checked, case], capture_output=True, text=True, timeout=120)
        problems = [] if run.returncode == 0 else [f"exit {run.returncode}"]
        problems += [line for line in run.stdout.splitlines() if "FAILED" in line]
        problems += [line for line in run.stderr.splitlines() if "runtime error" in line
                     or "AddressSanitizer" in line]
        if "ok" not in run.stdout:
            problems.append("no checks ran")
        print(f"{case}: {'ok' if not problems else 'FAILED'}")
        for line in run.stdout.splitlines():
            if not line.startswith("check:"):
                print(f"  {line}")
        for problem in problems:
            print(f"  {problem}")
        failed += bool(problems)
    run = subprocess.run([timed, "cost"], capture_output=True, text=True, timeout=120)
    for line in run.stdout.splitlines():
        print(f"  {line}")
    if run.returncode:
        print("cost: FAILED")
        failed += 1
if failed:
    raise SystemExit(f"apu_hrtf: {failed} case(s) failed")
print("apu_hrtf: all cases pass")
