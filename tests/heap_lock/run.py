"""Compile src/kernel/xbox_heap.c and allocate from it on several threads at once.

Four threads allocate, fill and free blocks of mixed sizes at once, as a new
worker allocating its TIB does beside the thread allocating the next worker's
stack, and every block must still hold what its thread wrote when it is
freed (test_main.c). With RECOMP_HEAP_CHECK=1, the block table is checked
after every operation too. Then the same without the lock, which must lose:
the heap's file is built with its two lock calls named heap_lock_enter and
heap_lock_leave, which test_main.c passes on to the lock or not. Then with
the lock and the heap guard (RECOMP_HEAP_GUARD), which must hold as well and
find nothing wrong, as nothing is: it says only that it is on and how many
releases it has seen. POSIX only
(the threads are pthreads, and the lock src/platform/win32_compat.c's);
requires GCC on PATH. CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("heap_lock: skipped, POSIX only")
    sys.exit(77)   # skipped (tests/run_all.py)

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
flags = ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter", "-pthread",
         "-Isrc", "-Isrc/platform", "-Isrc/kernel"]

# Without the lock the threads race on purpose, so under the thread sanitizer
# (tests/run_all.py --tsan sets TEST_SANITIZER) only the heap with it runs.
sanitizer = os.environ.get("TEST_SANITIZER")

with tempfile.TemporaryDirectory(prefix="heap_lock-") as directory:
    heap = str(pathlib.Path(directory) / "xbox_heap.o")
    exe = str(pathlib.Path(directory) / "heap_lock")
    subprocess.run(cc + flags + ["-DEnterCriticalSection=heap_lock_enter",
                                 "-DLeaveCriticalSection=heap_lock_leave",
                                 "-c", "src/kernel/xbox_heap.c", "-o", heap],
                   cwd=root, check=True)
    subprocess.run(cc + flags + ["tests/heap_lock/test_main.c", "src/platform/win32_compat.c",
                                 heap, "-lm", "-o", exe], cwd=root, check=True)
    # (name, argument, the guard on)
    runs = [("with the lock", "locked", False), ("without it", "unlocked", False),
            ("with the lock and the guard", "locked", True)]
    results = {}
    for name, argument, guard in runs:
        if sanitizer and argument == "unlocked":
            continue
        env = dict(os.environ, RECOMP_HEAP_CHECK="1")
        env.pop("RECOMP_HEAP_GUARD", None)
        if guard:
            env["RECOMP_HEAP_GUARD"] = "1"
        try:
            run = subprocess.run([exe, argument], capture_output=True, text=True,
                                 timeout=300, env=env)
            said = (run.stdout.strip().splitlines()
                    or [line for line in run.stderr.splitlines() if "HEAPCHK" in line]
                    or [f"exit {run.returncode}"])
            results[name] = run.returncode
            if guard and run.returncode == 0:
                guard_said = [line for line in run.stderr.splitlines()
                              if line.startswith("[HEAPGUARD]")]
                found = [line for line in guard_said
                         if not re.match(r"\[HEAPGUARD\] (on:|\d+ releases:)",
                                         line)]
                if not guard_said or not guard_said[0].startswith("[HEAPGUARD] on:"):
                    said, results[name] = ["the guard did not say it was on"], 1
                elif found:
                    said, results[name] = [f"the guard found what isn't there: {found[0]}"], 1
        except subprocess.TimeoutExpired:
            said, results[name] = ["no end in 300 s"], -1
        print(f"{name}: {said[-1]}")
    for name in ("with the lock", "with the lock and the guard"):
        if results[name] != 0:
            raise SystemExit(f"heap_lock: exit {results[name]} {name}")
    if not sanitizer and results["without it"] == 0:
        raise SystemExit("heap_lock: the heap held without the lock too, so this tests nothing")
    print("heap_lock: all cases pass")
