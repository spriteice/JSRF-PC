"""Compile the characters' voice lines kept on them (port/voice_follow.c)
and drive it without the game.

Takes port/voice_follow.c as it is, with src/apu/apu_gpfx.c for its switch,
and plays it made-up sounds at made-up characters, one case to a run
(test_main.c). A line played from a character's voice bank, at where that
character is, is moved to it each frame while it plays, by its own handle,
and let go once it ends, or once its speaker is gone. One not playing yet
on its first frame is given one more. A sound of another bank, one that
played nothing, one with no place or a place outside memory, and one placed
on the caller's stack are not kept, and the same line played again is kept
once. With more lines than are kept, the newest takes the place of the one
kept longest. With RECOMP_APU_STATS=1 the log says the lines are kept, and
for each one let go how long it was kept and how far its speaker went.
Unset, nothing is printed. With RECOMP_APU_GPFX unset, nothing is kept.
Requires GCC on PATH, including MinGW on Windows.
"""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]

KEPT = ("[APU] effects: the characters' voice lines kept on them as they move, as the game"
        " keeps its looped one (RECOMP_APU_GPFX)\n")
LINE = "[APU] effects: a voice line kept on its speaker for "
# case: (lines the log must have, in order, and lines it must not)
cases = {
    "follow": ([KEPT, LINE + "1.5 s, who went up to 180 from where it began\n",
                LINE + "1.5 s, who went up to 0 from where it began\n"],
               ["gone"]),
    "not_voice": ([], ["kept"]),
    "gone": ([KEPT, LINE + "0.5 s, who went up to 60 from where it began; its speaker gone\n"],
             []),
    "late": ([KEPT, LINE + "1.5 s, who went up to 182 from where it began\n",
              LINE + "0.0 s, who went up to 0 from where it began\n"],
             ["gone"]),
    "again": ([KEPT, LINE + "1.5 s, who went up to 180 from where it began\n"],
              ["gone"]),
    "full": ([KEPT], ["gone"]),
}

failed = 0
with tempfile.TemporaryDirectory(prefix="voice_follow-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Iport", "-Isrc/apu",
                    "port/voice_follow.c", "src/apu/apu_gpfx.c",
                    "tests/voice_follow/test_main.c", "-o", exe, "-lm"], cwd=root, check=True)
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
        if log.count(KEPT) > 1:
            problems.append("said more than once: " + KEPT)
        print(f"{case}: {'ok' if not problems else 'FAILED'}")
        for problem in problems:
            print(f"  {problem}")
        failed += bool(problems)
    # name: (case, switches, a check line it must print)
    quiet = {"follow_quiet": ("follow", {"RECOMP_APU_GPFX": "1"},
                              "moved each frame it played ok"),
             "off": ("off", {"RECOMP_APU_STATS": "1"},
                     "nothing kept with the switch off ok")}
    for name, (case, switches, line) in quiet.items():
        run = subprocess.run([exe, case], env=dict(env, **switches), capture_output=True,
                             text=True, timeout=60)
        ok = (run.returncode == 0 and not run.stderr and "FAILED" not in run.stdout
              and line in run.stdout)
        print(f"{name}: {'ok' if ok else 'FAILED'}")
        failed += not ok
if failed:
    raise SystemExit(f"voice_follow: {failed} case(s) failed")
print("voice_follow: all cases pass")
