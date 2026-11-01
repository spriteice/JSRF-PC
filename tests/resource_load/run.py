"""Compile the game's resource loads, checked (port/resource_load.c), and
drive it without the game.

Takes port/resource_load.c as it is and runs it on made-up guest memory, one
case to a run (test_main.c). A character file buffer the made-up heap says
was released is forgotten and logged, one a live allocation holds is kept,
and an empty one is left alone without asking the heap. A flow that has
asked for its stage is logged with what it asked for, and said to have had
its file written over when the file's first word no longer matches the copy
made as it loaded; one still waiting to ask is not logged.
Requires GCC on PATH, including MinGW on Windows.
"""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]

FORGOT = ("[LOAD] t=229.5 the character loader released its file buffer 00008000 and kept it:"
          " forgotten, so a character unload can't release it again\n")
ASKS = "[LOAD] t=229.5 mission 000401A9 asks for stage 00020000 (its file at 00008000)\n"
OVER = ("[LOAD] t=229.5 mission 000401A9 asks for stage 544C554D, but its file at 00008000"
        " said 00020000 when it loaded: something wrote over it since\n")
# case: the whole log it must print
cases = {
    "kept": FORGOT,
    "live": "",
    "none": "",
    "stage": ASKS,
    "written_over": OVER,
    "waiting": "",
}

failed = 0
with tempfile.TemporaryDirectory(prefix="resource_load-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Iport",
                    "port/resource_load.c", "tests/resource_load/test_main.c", "-o", exe],
                   cwd=root, check=True)
    for case, log in cases.items():
        run = subprocess.run([exe, case], capture_output=True, text=True, timeout=60)
        problems = [] if run.returncode == 0 else [f"exit {run.returncode}"]
        problems += [line for line in run.stdout.splitlines() if "FAILED" in line]
        if run.stderr != log:
            problems.append(f"log was {run.stderr!r}, wanted {log!r}")
        print(f"{case}: {'ok' if not problems else 'FAILED'}")
        for problem in problems:
            print(f"  {problem}")
        failed += bool(problems)
if failed:
    raise SystemExit(f"resource_load: {failed} case(s) failed")
print("resource_load: all cases pass")
