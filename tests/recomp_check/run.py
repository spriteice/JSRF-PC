"""RECOMP_CHECK (src/platform/recomp_check.h), the check that stays in
release builds, compiled as every build is, with NDEBUG (test_main.c and,
for the menu's C++, test_cpp.cpp): a check that holds says nothing; one that
fails says so in the log once for its place, with the file, line, function,
condition and message, and the code goes on; its condition is evaluated
once, and its message only when written; eight threads failing one check
at once write one line. On Linux the threads are src/platform/win32_compat.c's
(CreateThread), on Windows the system's. Requires GCC and a C++ compiler on
PATH, including MinGW on Windows.
CC and CXX, when set, are the compilers instead of gcc and c++.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
cxx = shlex.split(os.environ.get("CXX", "c++"))
sources = ["tests/recomp_check/test_main.c"]
if os.name == "nt":
    flags = []
else:
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]
# As the engine builds: optimised, NDEBUG, and the menu's C++ flags.
release = ["-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Isrc", "-Isrc/platform"]

with tempfile.TemporaryDirectory(prefix="recomp_check-") as directory:
    obj = str(pathlib.Path(directory) / "test_cpp.o")
    exe = str(pathlib.Path(directory) / "recomp_check.exe")
    log = str(pathlib.Path(directory) / "stderr.txt")
    subprocess.run(cxx + ["-std=c++11", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                          *release, "-c", "tests/recomp_check/test_cpp.cpp", "-o", obj],
                   cwd=root, check=True)
    subprocess.run(cc + ["-std=gnu11", *release, *flags, *sources, obj, "-lm", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe, log], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("recomp_check: no end in 120 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "recomp_check: all cases pass" not in text:
        sys.exit(f"recomp_check: exit {run.returncode}")
