"""Compile src/kernel/xbox_heap.c and allocate from it on several threads at once.

Four threads allocate, fill and free blocks of mixed sizes at once, as a new
worker allocating its TIB does beside the thread allocating the next worker's
stack, and every block must still hold what its thread wrote when it is
freed (test_main.c). With RECOMP_HEAP_CHECK=1, the block table is checked
after every operation too. Then the same without the lock, which must lose:
the heap's file is built with its two lock calls named heap_lock_enter and
heap_lock_leave, which test_main.c passes on to the lock or not. POSIX only
(the threads are pthreads, and the lock src/platform/win32_compat.c's);
requires GCC on PATH. CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("heap_lock: skipped, POSIX only")
    sys.exit(0)

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
    results = {}
    for locked in ((True,) if sanitizer else (True, False)):
        try:
            run = subprocess.run([exe, "locked" if locked else "unlocked"],
                                 capture_output=True, text=True, timeout=300,
                                 env=dict(os.environ, RECOMP_HEAP_CHECK="1"))
            said = (run.stdout.strip().splitlines()
                    or [line for line in run.stderr.splitlines() if "HEAPCHK" in line]
                    or [f"exit {run.returncode}"])
            results[locked] = run.returncode
        except subprocess.TimeoutExpired:
            said, results[locked] = ["no end in 300 s"], -1
        print(("with the lock: " if locked else "without it: ") + said[-1])
    if results[True] != 0:
        raise SystemExit(f"heap_lock: exit {results[True]} with the lock")
    if not sanitizer and results[False] == 0:
        raise SystemExit("heap_lock: the heap held without the lock too, so this tests nothing")
    print("heap_lock: all cases pass")
