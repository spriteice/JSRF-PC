"""Compile src/kernel/xbox_heap.c and make each of the game's mistakes with
released memory that the heap guard (RECOMP_HEAP_GUARD) is to find, without
it and with it (test_main.c). Without it, the heap hands a released block out
again at once and says nothing. With it, each mistake is said once, with the
guest return addresses of where it was made and of the release before, and
the test carries on past a touch of a held page, as the game does. Then the
same trace of requests and releases with the guard and without: every
address has to be the same, as the guard must not move the game's memory,
and the guard has to have held blocks and let them go for their space. On
Linux the memory calls are src/platform/win32_compat.c's, on Windows the
system's. Requires GCC on PATH, including MinGW on Windows. CC, when set, is
the compiler instead of gcc.
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
sources = ["tests/heap_guard/test_main.c", "src/kernel/xbox_heap.c"]
if os.name == "nt":
    flags = []
else:
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

# What the guard must say with it on, in the order the test makes the
# mistakes. Each is one line, and none is said twice.
# The count line, every 4096 releases.
COUNT = (r"\[HEAPGUARD\] (?P<n>\d+) releases: (?P<held>\d+) blocks held \(\d+ KB\); let go "
         r"(?P<wanted>\d+) as their space was handed out, \d+ as their records were the "
         r"oldest, \d+ for the block table's room; the heap's top 0x[0-9A-F]{8}, at most "
         r"0x[0-9A-F]{8}, of 0x04000000, and the largest block it could hand out \d+ KB")

EXPECTED = [
    ("on", r"\[HEAPGUARD\] on: a released block is held, .* until the heap hands its space "
           r"out again, .* each block goes where it would with the guard off"),
    ("released twice, let go",
     r"\[HEAPGUARD\] released twice: 0x00F80100 \(256 bytes from ExAllocatePool\), by "
     r"ExFreePool at 00021200 00021300 on thread \d+; released before by ExFreePool at "
     r"00021000 00021100 on thread \d+ \(let go since\)"),
    ("released twice, held",
     r"\[HEAPGUARD\] released twice: 0x(?P<a>[0-9A-F]{8}) \(65536 bytes from "
     r"NtAllocateVirtualMemory\), by NtFreeVirtualMemory at 00032C70 0002DA70 on thread \d+; "
     r"released before by NtFreeVirtualMemory at 0002D8D0 0004AC80 on thread \d+ \(still held\)"),
    ("a read",
     r"\[HEAPGUARD\] a read of released memory: 0x[0-9A-F]{8}, 4096 bytes into "
     r"0x{a} \(65536 bytes from NtAllocateVirtualMemory\), from the test at "
     r"0002D950 0004ACB0 on thread \d+; released by NtFreeVirtualMemory at 0002D8D0 "
     r"0004AC80 on thread \d+\. The page is given its access back"),
    ("a write",
     r"\[HEAPGUARD\] a write of released memory: 0x[0-9A-F]{8}, 8208 bytes into "
     r"0x{a} .* from the test at 0002D9D0 0004ACB0"),
    ("the wrong call",
     r"\[HEAPGUARD\] released through the wrong call: 0x{a} \(256 bytes from "
     r"ExAllocatePool\), by NtFreeVirtualMemory at 00145A2F 00145A51 on thread \d+"),
    ("written after its release",
     r"\[HEAPGUARD\] written after its release: 0x{a} \(100 bytes from "
     r"ExAllocatePool\), released by ExFreePool at 00030490 00030690 on thread \d+: "
     r"bytes of it that share a page with another block have changed"),
    ("inside a block",
     r"\[HEAPGUARD\] a release inside a block: 0x[0-9A-F]{8} is 16 bytes into 0x{a} "
     r"\(256 bytes from ExAllocatePool, live\), by ExFreePool at 00013000 00013100"),
    ("no block",
     r"\[HEAPGUARD\] a release of heap space no block holds: 0x03F00000, by ExFreePool "
     r"at 00013000 00013100 on thread \d+, and none of the last 16384 releases was of it"),
    ("cannot serve",
     r"\[HEAPGUARD\] cannot serve 50500000 bytes: the largest block the heap could hand "
     r"out is \d+ KB, and \d+ blocks are held \(\d+ KB\)"),
    ("the count", COUNT),
]

with tempfile.TemporaryDirectory(prefix="heap_guard-") as directory:
    exe = str(pathlib.Path(directory) / "heap_guard.exe")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter",
                         "-Wno-comment", *flags, "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    texts = {}
    for mode, argument, guard in (("off", "off", False), ("on", "on", True),
                                  ("trace off", "trace", False), ("trace on", "trace", True)):
        env = dict(os.environ)
        env.pop("RECOMP_HEAP_GUARD", None)
        if guard:
            env["RECOMP_HEAP_GUARD"] = "1"
        try:
            run = subprocess.run([exe, argument], capture_output=True, text=True, timeout=120,
                                 env=env)
        except subprocess.TimeoutExpired:
            sys.exit(f"heap_guard: {mode}: no end in 120 s")
        texts[mode] = run.stdout + run.stderr
        if mode.startswith("trace"):
            texts[mode + " addresses"] = [line for line in run.stdout.splitlines()
                                          if re.fullmatch(r"[0-9A-F]{8}", line)]
        print(f"--- {mode}")
        print("\n".join(line for line in texts[mode].splitlines()
                        if ("HEAPGUARD" in line or line.startswith("heap_guard")) and
                        not (mode.startswith("trace") and "cannot serve" in line)))
        if mode.startswith("trace"):
            print(f"{texts[mode].count('xbox_HeapAlloc: cannot serve')} requests the heap "
                  "couldn't serve")
        if run.returncode != 0 or "heap_guard: all cases pass" not in run.stdout:
            sys.exit(f"heap_guard: {mode}: exit {run.returncode}")

    for mode in ("off", "trace off"):
        if "[HEAPGUARD]" in texts[mode]:
            sys.exit(f"heap_guard: {mode}: the guard said something without RECOMP_HEAP_GUARD")
    said = [line for line in texts["on"].splitlines() if line.startswith("[HEAPGUARD]")]
    if len(said) != len(EXPECTED):
        sys.exit(f"heap_guard: on: {len(said)} [HEAPGUARD] lines, not {len(EXPECTED)}: "
                 "one is missing, said twice, or said that shouldn't be")
    found = {}
    for (name, pattern), line in zip(EXPECTED, said):
        # {a} is the first block's address, from the first line that has it.
        m = re.match(pattern.replace("{a}", found.get("a", "........")), line)
        if not m:
            sys.exit(f"heap_guard: on: not the line for {name}: {line}")
        found.update({k: v for k, v in m.groupdict().items() if v})

    # The trace: every address the same, guard or not.
    off_at, on_at = texts["trace off addresses"], texts["trace on addresses"]
    if len(off_at) < 10000 or len(off_at) != len(on_at):
        sys.exit(f"heap_guard: trace: {len(off_at)} addresses without the guard, "
                 f"{len(on_at)} with it")
    for k, (was, now) in enumerate(zip(off_at, on_at)):
        if was != now:
            sys.exit(f"heap_guard: trace: request {k + 1} got {now} with the guard, "
                     f"{was} without it")
    # And the guard did its work in it: no finding, and blocks held and let go
    # for their space.
    said = [line for line in texts["trace on"].splitlines() if line.startswith("[HEAPGUARD]")]
    # A request the heap can't serve fails with the guard as without it (the
    # addresses above), and the guard says so.
    counts = [re.match(COUNT, line) for line in said[1:]
              if not line.startswith("[HEAPGUARD] cannot serve ")]
    if not said or not said[0].startswith("[HEAPGUARD] on:") or not all(counts) or not counts:
        sys.exit("heap_guard: trace: not only the guard's start, count and cannot-serve "
                 "lines: " + next((line for line in said[1:] if not re.match(COUNT, line) and
                                   not line.startswith("[HEAPGUARD] cannot serve ")), "none"))
    if not any(int(m["held"]) and int(m["wanted"]) for m in counts):
        sys.exit("heap_guard: trace: the guard held nothing, or let nothing go for its space")
    print(f"heap_guard: trace: {len(off_at)} addresses, the same with the guard and without")
    print("heap_guard: all cases pass")
