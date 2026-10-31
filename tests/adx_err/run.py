"""Compile the stream error log (Developer > Logging > JSRF_ADX_ERR_LOG) and
drive it without the game.

Takes port/adx_err.c as it is, and plays its hooks' calls, one case to a run
(test_main.c): the sound manager's update on one thread, a watchdog recovery
on the ADX server's, and the guest lock handed between them. With
JSRF_ADX_ERR_LOG=1, each recovery gets a line when it ends, with every
hand-over while it ran, the server's own retakes as "back"; an error the
game raises says whether a recovery was running; a recovery inside another
is left out. CRI's lock is counted over each hold, from the outermost lock to
the unlock that lets it go: every 10 s of the run's clock, the holds, who
held them, and each take of the guest lock by another thread by when in the
hold it came, with a line for each of the first 20 takes by the sound
update's thread and entries into a held lock. Unset, nothing is printed and
the runtime is given nothing.
Requires GCC on PATH. Linux only: the thread ids are faked through the Win32
shim's GetCurrentThreadId, which Windows takes from <windows.h>.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
if not sys.platform.startswith("linux"):
    print("adx_err: skipped (Linux only)")
    raise SystemExit(0)

UPDATE = "[ADXREC] the sound manager's update runs on tid 2"
LINE = ("[ADXREC] t=535.92 handle 0027D1C8 code -1: restart on tid 7, from 0013D41A,"
        " CRI's lock 0 deep; file 002738D0 (13 sectors); ")
# case: (lines the log must have, in order, lines it must not, and lines it must
# have so many times)
cases = {
    "handover": ([UPDATE, LINE,
                  "[ADXREC]   the guest lock changed hands 2 times meanwhile: tid 2 at +",
                  " ms (priority 0, the sound update's), back at +", " ms (priority 15)\n",
                  "[ADXREC]   the sound manager's update started 1 time meanwhile"],
                 ["no other thread"]),
    "quiet": ([UPDATE, LINE, "[ADXREC]   no other thread took the guest lock meanwhile"],
              ["changed hands", "update started"]),
    "raised_in": (["[ADXREC] t=536.00 code -1 raised on tid 2 (the sound update's), ",
                   " ms into tid 7's restart of handle 0027D1C8 for code -1"],
                  ["no recovery running", "[ADXREC] t=535.92"]),
    "raised_out": (["[ADXREC] t=536.00 code -1 raised on tid 2 (the sound update's),"
                    " with no recovery running"],
                   ["ms into"]),
    "nested": ([LINE, "[ADXREC]   the guest lock changed hands 2 times meanwhile: tid 9 at +",
                " ms (priority 2), back at +"],
               ["0027D270"]),
    "many": ([LINE, "[ADXREC]   the guest lock changed hands 16 times meanwhile: tid 9 at +",
              ", and 4 more\n"],
             []),
    "after": ([LINE, "[ADXREC]   no other thread took the guest lock meanwhile",
               LINE, "[ADXREC]   no other thread took the guest lock meanwhile"],
              ["changed hands"]),
    "crs_quiet": (["[ADXLOCK] the last 10.5 s: CRI's lock was held 3 times, the longest ",
                   " ms; another thread took the guest lock during 0 of them, and entered CRI's"
                   " lock itself during 0\n",
                   "[ADXLOCK]   held by tid 7 (priority 1) 3 times, 0 with a take\n",
                   "[ADXLOCK]   no other thread took the guest lock while it was held\n"],
                  ["taken by", "hold of"]),
    "crs_take": (["[ADXLOCK] t=11.00 the sound update's thread (tid 2, priority 0) took the guest"
                  " lock ", " ms into tid 7's hold of CRI's lock, as the lock was taken\n",
                  "[ADXLOCK] t=11.00 the sound update's thread (tid 2, priority 0) took the guest"
                  " lock ", " ms into tid 7's hold of CRI's lock, as it was let go\n",
                  "[ADXLOCK] the last 10.0 s: CRI's lock was held 2 times, the longest ",
                  " ms; another thread took the guest lock during 1 of them, and entered CRI's"
                  " lock itself during 0\n",
                  "[ADXLOCK]   held by tid 7 (priority 1) 2 times, 1 with a take\n",
                  "[ADXLOCK]   taken by tid 2 (priority 0, the sound update's) 2 times: 1 as the"
                  " lock was taken, 0 while it was held, 1 as it was let go; tid 9 (priority 2)"
                  " 1 time: 0 as the lock was taken, 1 while it was held, 0 as it was let go\n"],
                 ["no other thread", "while it was held\n[ADXLOCK] t=", "entered CRI's lock 0"]),
    "crs_entered": (["[ADXLOCK] t=11.00 the sound update's thread (tid 2, priority 0) took the"
                     " guest lock ", " ms into tid 7's hold of CRI's lock, as the lock was taken\n",
                     "[ADXLOCK] t=11.00 tid 2 (priority 0, the sound update's) entered CRI's lock ",
                     " ms into tid 7's hold of it, as the lock was taken\n",
                     "[ADXLOCK] the last 10.0 s: CRI's lock was held 2 times, the longest ",
                     " ms; another thread took the guest lock during 1 of them, and entered"
                     " CRI's lock itself during 1\n",
                     "[ADXLOCK]   held by tid 7 (priority 1) 2 times, 1 with a take\n",
                     "[ADXLOCK]   taken by tid 2 (priority 0, the sound update's) 3 times: 1 as"
                     " the lock was taken, 2 while it was held, 0 as it was let go\n"],
                    ["tid 2 (priority 0, the sound update's) 1 time,"]),
    "crs_nested": (["[ADXLOCK] the last 10.0 s: CRI's lock was held 2 times, the longest ",
                    " and entered CRI's lock itself during 0\n",
                    "[ADXLOCK]   held by tid 7 (priority 1) 2 times, 0 with a take\n",
                    "[ADXLOCK]   no other thread took the guest lock while it was held\n"],
                   ["hold of it", "taken by"]),
    "crs_period": (["[ADXLOCK] the last 10.1 s: CRI's lock was held 3 times, ",
                    "[ADXLOCK]   held by tid 7 (priority 1) 3 times, 0 with a take\n",
                    "[ADXLOCK] the last 10.1 s: CRI's lock was held 2 times, ",
                    "[ADXLOCK]   held by tid 7 (priority 1) 2 times, 0 with a take\n"],
                   ["held 5 times", "held 4 times"]),
    "crs_lines": (["[ADXLOCK] t=10.00 the sound update's thread (tid 2, priority 0) took the guest"
                   " lock ", " ms into tid 7's hold of CRI's lock, as the lock was taken\n",
                   "[ADXLOCK] t=11.90 the sound update's thread"],
                  ["[ADXLOCK] t=12.00", "the last "],
                  {"the sound update's thread (tid 2, priority 0) took the guest lock": 20}),
}

failed = 0
with tempfile.TemporaryDirectory(prefix="adx_err-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Iport",
                    "port/adx_err.c", "tests/adx_err/test_main.c", "-o", exe],
                   cwd=root, check=True)
    env = {k: v for k, v in os.environ.items() if k != "JSRF_ADX_ERR_LOG"}
    for case, (must, must_not, *counts) in cases.items():
        run = subprocess.run([exe, case], env=dict(env, JSRF_ADX_ERR_LOG="1"),
                             capture_output=True, text=True, timeout=60)
        log, at = run.stdout + run.stderr, 0
        problems = [] if run.returncode == 0 else [f"exit {run.returncode}"]
        for line in must:
            found = log.find(line, at)
            if found < 0:
                problems.append(f"missing (or out of order): {line}")
            else:
                at = found + len(line)
        problems += [f"unexpected: {line}" for line in must_not if line in log]
        for line, n in (counts[0] if counts else {}).items():
            if log.count(line) != n:
                problems.append(f"{log.count(line)} times, not {n}: {line}")
        print(f"{case}: {'ok' if not problems else 'FAILED'}")
        for problem in problems:
            print(f"  {problem}")
        if problems:
            print(log)
        failed += bool(problems)
    for case in cases:
        run = subprocess.run([exe, case], env=env, capture_output=True, text=True, timeout=60)
        if run.returncode != 0 or run.stdout + run.stderr:
            print(f"off ({case}): FAILED\n{run.stdout}{run.stderr}")
            failed += 1
    print(f"off: {'ok' if not failed else 'see above'}")
if failed:
    raise SystemExit(f"adx_err: {failed} case(s) failed")
print("adx_err: all cases pass")
