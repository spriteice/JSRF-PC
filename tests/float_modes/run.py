"""Compile src/kernel/kernel_float.c and enter it from threads that start with
other float modes (test_main.c): each must leave with the Xbox's MXCSR
modes, 0x1F80, and each [SSE] line it prints must be the one that says what
it found. On Linux the threads are src/platform/win32_compat.c's, on Windows
the system's. Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
sources = ["tests/float_modes/test_main.c", "src/kernel/kernel_float.c"]
flags = []
if os.name != "nt":
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

XBOX = "0x1F80"
FIRST = "[SSE] thread {t} enters the game's code: MXCSR {found}, "
CHANGED = ("[SSE] thread {t}: MXCSR changed to 0x9F80 outside the game's code "
           "(flush to zero); set back to " + XBOX)
# Each case's [SSE] lines, in order, for the thread that ran it.
EXPECTED = {
    1: [FIRST + "not the Xbox's modes (flush to zero, denormals are zero); set to " + XBOX],
    2: [FIRST.replace("{found}", "0x1FA0") + "the Xbox's modes"],
    3: [FIRST + "not the Xbox's modes (rounding towards zero); set to " + XBOX],
    4: [FIRST.replace("{found}", XBOX) + "the Xbox's modes", CHANGED],
    5: [FIRST.replace("{found}", XBOX) + "the Xbox's modes"],
    # Changes 2 to 5 of the run said in full (case 4's was the first).
    6: [FIRST.replace("{found}", XBOX) + "the Xbox's modes"] + [CHANGED] * 4,
}
FOUND = {1: "0x9FC0", 3: "0x7F80"}
# The lines that name no thread, in order.
OTHERS = [
    "[SSE] float modes at the start: MXCSR 0x1F80, the Xbox's modes",
    "[SSE] float modes at the start: MXCSR 0x9FC0, not the Xbox's modes (flush to "
    "zero, denormals are zero); each thread gets 0x1F80 where it enters the game's code",
    "[SSE] further changes are counted, and the count said at each power of ten",
    "[SSE] MXCSR changed outside the game's code 10 times so far, on any thread; "
    "each set back to " + XBOX,
    "[SSE] MXCSR changed outside the game's code 100 times so far, on any thread; "
    "each set back to " + XBOX,
]

with tempfile.TemporaryDirectory(prefix="float_modes-") as directory:
    exe = str(pathlib.Path(directory) / "float_modes.exe")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("float_modes: no end in 120 s")
    print(run.stdout + run.stderr, end="")
    if run.returncode != 0 or "float_modes: all cases pass" not in run.stdout:
        sys.exit(f"float_modes: exit {run.returncode}")

    threads = {int(c): t for c, t in
               re.findall(r"^float_modes: case (\d) on thread (\d+)$", run.stdout, re.M)}
    if sorted(threads) != sorted(EXPECTED):
        sys.exit(f"float_modes: cases {sorted(threads)} said their threads, not {sorted(EXPECTED)}")
    if len(set(threads.values())) != len(threads):
        sys.exit("float_modes: two cases ran on threads with the same id")
    lines = [l for l in run.stderr.splitlines() if l.startswith("[SSE]")]
    for case, want in EXPECTED.items():
        t = threads[case]
        want = [w.format(t=t, found=FOUND.get(case, "")) for w in want]
        got = [l for l in lines if l.startswith(f"[SSE] thread {t}:")
               or l.startswith(f"[SSE] thread {t} ")]
        if got != want:
            sys.exit(f"float_modes: case {case}'s lines are\n  " + "\n  ".join(got)
                     + "\nnot\n  " + "\n  ".join(want))
    others = [l for l in lines if not l.startswith("[SSE] thread ")]
    if others != OTHERS:
        sys.exit("float_modes: the other [SSE] lines are\n  " + "\n  ".join(others)
                 + "\nnot\n  " + "\n  ".join(OTHERS))
