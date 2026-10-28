"""Compile the CPU profile, port/cpuprof.c, into a small busy program and run it.

Linux only: Windows' way needs Windows to run (Wine's QueryThreadCycleTime
does nothing), so it is tried in the game. The program keeps two threads busy
in two functions for 2.5 s with a report every second
(RECOMP_CPU_PROFILE_SECS=1), stops the profile as developer_restart does and
starts itself again with exec, as the game does for a new resolution. Each
report must give each thread its own function, and the new process must live
through the exec. Requires GCC on PATH.

The profile is built with a processor of the test's making, in place of
/sys/devices/system/cpu (port/pcores.c's PCORES_CPU_DIR): the first CPU this
may run on is a P core and the rest are compact. The worker runs on that
first CPU and the main thread on the last, and each report's line on the P
cores must say so.
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
allowed = sorted(os.sched_getaffinity(0))
if len(allowed) < 2:
    raise SystemExit(f"cpuprof: two CPUs needed for two busy threads, this may use {allowed}")
p_cpu, compact_cpu = allowed[0], allowed[-1]
with tempfile.TemporaryDirectory(prefix="cpuprof-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test")
    for cpu in range(compact_cpu + 1):
        (work / f"cpu/cpu{cpu}/cpufreq").mkdir(parents=True)
        (work / f"cpu/cpu{cpu}/cpufreq/cpuinfo_max_freq").write_text(
            "5000000\n" if cpu == p_cpu else "3300000\n")
    subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                    f'-DPCORES_CPU_DIR="{work / "cpu"}"',
                    "tests/cpuprof/test_main.c", "port/cpuprof.c", "port/pcores.c", "-o", exe],
                   cwd=root, check=True)
    env = dict(os.environ, RECOMP_CPU_PROFILE="1", RECOMP_CPU_PROFILE_SECS="1",
               CPUPROF_TEST_CPUS=f"{p_cpu},{compact_cpu}")
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
            # The worker all on the P core, the main thread none.
            m = re.search(r"on the P cores \(CPUs (\S+)\), share of each thread's samples:(.*)", report)
            shares = dict(re.findall(r"thread (\d+) (\d+)%", m.group(2))) if m else {}
            if (not m or m.group(1) != str(p_cpu) or int(shares.get(worker[0], -1)) < 90
                    or not 0 <= int(shares.get(main[0], -1)) <= 10):
                raise SystemExit(f"the worker not on CPU {p_cpu}, the P core, "
                                 f"or the main thread not off it:\n{log}")
            full += 1
    if full < 2:
        raise SystemExit(f"fewer than two reports with both threads:\n{log}")
print("cpuprof: pass,", len(reports), "reports,", full, "with both threads")
