"""Compile the CPU profile, port/cpuprof.c, into a small busy program and run it.

Linux only, as the profile is. The program keeps two threads busy in two
functions for 2.5 s with a report every second (RECOMP_CPU_PROFILE_SECS=1),
stops the profile as developer_restart does and starts itself again with exec,
as the game does for a new resolution. Each report must give each thread its
own function, and the new process must live through the exec. Requires GCC on
PATH.
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

if not sys.platform.startswith("linux"):
    print("cpuprof: Linux only, skipped")
    raise SystemExit(0)

root = pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="cpuprof-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                    "tests/cpuprof/test_main.c", "port/cpuprof.c", "-o", exe],
                   cwd=root, check=True)
    env = dict(os.environ, RECOMP_CPU_PROFILE="1", RECOMP_CPU_PROFILE_SECS="1")
    run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=20)
    log = run.stderr
    if run.returncode != 0:
        raise SystemExit(f"exit {run.returncode}:\n{log}")
    for line in ("[CPUPROF] on:", "stopped and lived", "started again and lived"):
        if line not in log:
            raise SystemExit(f"no '{line}' in the log:\n{log}")
    if log.count("[CPUPROF] on:") != 1:
        raise SystemExit(f"the profile started again after the exec:\n{log}")
    # Each report: one thread mostly in spin_in_main, another mostly in
    # spin_in_worker, each at about a whole core.
    reports = log.split("[CPUPROF] the last ")[1:]
    full = 0
    for report in reports:
        tops = {}
        for m in re.finditer(r"thread (\d+) \([^)]*\) (?:(\d+)% of a core, )?\d+ samples: (\w+) ([0-9.]+)%", report):
            tops[m.group(3)] = (m.group(1), m.group(2), float(m.group(4)))
        if "spin_in_main" in tops and "spin_in_worker" in tops:
            main, worker = tops["spin_in_main"], tops["spin_in_worker"]
            if main[0] == worker[0]:
                raise SystemExit(f"both functions under one thread:\n{log}")
            if main[2] < 80 or worker[2] < 80:
                raise SystemExit(f"a thread's own function under 80% of its samples:\n{log}")
            for core in (main[1], worker[1]):
                if core is not None and not 70 <= int(core) <= 110:
                    raise SystemExit(f"a busy thread not at about a core:\n{log}")
            full += 1
    if full < 2:
        raise SystemExit(f"fewer than two reports with both threads:\n{log}")
print("cpuprof: pass,", len(reports), "reports,", full, "with both threads")
