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
Unset, nothing is printed. With RECOMP_APU_GPFX=1, the 3D bins of made-up
mixbins go through their limiter: a frame late and as they were while
quiet, and while loud under full scale, with one gain for the four that
glides, and comes back over 150 ms. The other bins are left alone, and so
is a frame of another size. Unset, nothing runs. Requires GCC on PATH,
including MinGW on Windows.
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
    "limit": (["[APU] effects: the 3D bins (6 to 9) through a limiter of their own, at"
               " full scale, before the fold (RECOMP_APU_GPFX)",
               "[APU] effects: the 3D bins peaked at 2.70 of full scale, and were turned"
               " down for 50% of the second, by up to 8.6 dB\n",
               "[APU] effects: the 3D bins peaked at 0.50 of full scale, and weren't"
               " turned down\n"],
              ["image loaded"]),
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
                    "-o", exe, "-lm"], cwd=root, check=True)
    env = {k: v for k, v in os.environ.items()
           if k not in ("RECOMP_APU_STATS", "RECOMP_APU_GPFX")}
    for case, (must, must_not) in cases.items():
        run = subprocess.run([exe, case],
                             env=dict(env, RECOMP_APU_STATS="1", RECOMP_APU_GPFX="1"),
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
    # name: (case, switches, a check line it must print)
    quiet = {"off": ("studied", {}, "every effect as loaded, and started ok"),
             "limit_quiet": ("limit", {"RECOMP_APU_GPFX": "1"},
                             "loud ones under full scale, one gain for the four, gliding ok"),
             "limit_off": ("limit_off", {"RECOMP_APU_STATS": "1"},
                           "nothing run with the switch off ok")}
    for name, (case, switches, line) in quiet.items():
        run = subprocess.run([exe, case], env=dict(env, **switches), capture_output=True,
                             text=True, timeout=60)
        ok = (run.returncode == 0 and not run.stderr and "FAILED" not in run.stdout
              and line in run.stdout)
        print(f"{name}: {'ok' if ok else 'FAILED'}")
        failed += not ok
if failed:
    raise SystemExit(f"apu_gpfx: {failed} case(s) failed")
print("apu_gpfx: all cases pass")
