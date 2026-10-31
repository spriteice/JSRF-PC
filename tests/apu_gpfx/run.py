"""Compile the GP's settings mirror (src/apu/apu_gpfx.c) and drive it
without the APU.

Takes src/apu/apu_gpfx.c as it is, and rings it a made-up image's commands,
one case to a run (test_main.c). The GP's memory must hold what the GP's
kernel would have copied there: every effect's state when an image loads,
and then only the words a change sends, not those written and not yet sent.
An image laid out as JSRF's is told from another, a new image clears the
last one's changes, and a command that reaches past the window or the GP's
memory changes nothing. An effect is found where the table says, as
DirectSound rewrites it for the download, and one the table doesn't place
in the effects' memory is left with no state. Each effect is started, as
its code starts itself the first time it runs. Given a window, the copy is
kept there too, as the CPU would read the GP's memory back. With
RECOMP_APU_STATS=1 the log says what loaded and which words changed.
Unset, nothing is printed. Requires GCC on PATH, including MinGW on
Windows.
"""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]

LOADED = ("[APU] effects: an image loaded, 20 effects in 64 words of code and 827 of state,"
          " 20 of them found in the GP's memory; ")
# case: (lines the log must have, in order, and lines it must not)
cases = {
    "studied": ([LOADED + "laid out as JSRF's",
                 "[APU] effects: effect 0's settings, words 4 to 135 sent, 67 changed:",
                 "[APU] effects:   4=500004 70=500046 71=500047 72=500048",
                 "[APU] effects:   133=500085 134=500086 135=500087\n"],
                ["not laid out", "outside every effect", "not followed", " 5=", " 69=",
                 "kept at"]),
    "other": (["[APU] effects: an image loaded, 3 effects in 64 words of code and 80 of state,"
               " 3 of them found in the GP's memory; not laid out as JSRF's",
               "[APU] effects: effect 2's settings, words 0 to 9 sent, 10 changed:"],
              ["not followed", "kept at"]),
    "reload": ([LOADED + "laid out as JSRF's",
                "[APU] effects: effect 1's settings, words 0 to 92 sent, 93 changed:",
                LOADED + "laid out as JSRF's"],
               ["not followed", "kept at"]),
    "window": ([LOADED + "laid out as JSRF's",
                "[APU] effects: kept at 0xFE830000 too, where DirectSound reads the GP's"
                " memory back",
                "[APU] effects: effect 0's settings, words 4 to 135 sent, 67 changed:",
                "[APU] effects: effect 1's settings, words 0 to 0 sent, 1 changed:"],
               ["not followed", "outside every effect"]),
    "bad": ([LOADED + "laid out as JSRF's",
             "[APU] effects: command 2 not followed: 512 words from 0x918 in scratch"
             " to X:0xF80 are not all there",
             "[APU] effects: command 2 not followed: 4 words from 0x20000 in scratch"
             " to X:0x80 are not all there",
             "[APU] effects: an image loaded, 0 effects in 64 words of code and 827 of"
             " state, 0 of them found in the GP's memory; not laid out as JSRF's",
             "[APU] effects: an image loaded, 20 effects in 64 words of code and 827 of"
             " state, 16 of them found in the GP's memory; not laid out as JSRF's"],
            ["changed", "kept at"]),
}

failed = 0
with tempfile.TemporaryDirectory(prefix="apu_gpfx-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Isrc/apu",
                    "src/apu/apu_gpfx.c", "tests/apu_gpfx/test_main.c",
                    "-o", exe], cwd=root, check=True)
    env = {k: v for k, v in os.environ.items() if k != "RECOMP_APU_STATS"}
    for case, (must, must_not) in cases.items():
        run = subprocess.run([exe, case], env=dict(env, RECOMP_APU_STATS="1"),
                             capture_output=True, text=True, timeout=60)
        log, at = run.stderr, 0
        problems = [] if run.returncode == 0 else [f"exit {run.returncode}"]
        problems += [line for line in run.stdout.splitlines() if "FAILED" in line]
        for line in must:
            found = log.find(line, at)
            if found < 0:
                problems.append(f"missing (or out of order): {line}")
            else:
                at = found + len(line)
        problems += [f"unexpected: {line}" for line in must_not if line in log]
        print(f"{case}: {'ok' if not problems else 'FAILED'}")
        for problem in problems:
            print(f"  {problem}")
        failed += bool(problems)
    run = subprocess.run([exe, "studied"], env=env, capture_output=True,
                         text=True, timeout=60)
    off = run.returncode == 0 and not run.stderr and "FAILED" not in run.stdout
    print(f"off: {'ok' if off else 'FAILED'}")
    failed += not off
if failed:
    raise SystemExit(f"apu_gpfx: {failed} case(s) failed")
print("apu_gpfx: all cases pass")
