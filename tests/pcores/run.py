"""Compile Use P cores, port/pcores.c, and drive it without the game.

Linux only, as the switch is. test_main.c is built with pcores.c against a
processor of the test's making (PCORES_CPU_DIR, in place of
/sys/devices/system/cpu). Each case is its own run:

- kinds: the processors of today with two kinds of core and with one, a CPU
  taken offline, and none listed;
- cores: the game thread on the P cores this may run on, a thread it starts
  back on all of them, and the stop; started on some CPUs only; one kind of
  core; and the switch off, 0, or another word.

Requires GCC on PATH.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

if not sys.platform.startswith("linux"):
    print("pcores: Linux only, skipped")
    raise SystemExit(0)

root = pathlib.Path(__file__).resolve().parents[2]


def processor(cpu_dir, khz):
    """cpu0, cpu1, ... with each one's highest clock, None for one offline."""
    if cpu_dir.exists():
        shutil.rmtree(cpu_dir)
    for cpu, k in enumerate(khz):
        (cpu_dir / f"cpu{cpu}").mkdir(parents=True)
        if k is not None:
            (cpu_dir / f"cpu{cpu}/cpufreq").mkdir()
            (cpu_dir / f"cpu{cpu}/cpufreq/cpuinfo_max_freq").write_text(f"{k}\n")


def cpu_list(cpus):
    """A set of CPUs as the kernel writes one: 0-3,8."""
    out, cpus = [], sorted(cpus)
    i = 0
    while i < len(cpus):
        j = i
        while j + 1 < len(cpus) and cpus[j + 1] == cpus[j] + 1:
            j += 1
        out.append(str(cpus[i]) if i == j else f"{cpus[i]}-{cpus[j]}")
        i = j + 1
    return ",".join(out)


def check(name, text, want, unwanted=()):
    for line in want:
        if line not in text:
            raise SystemExit(f"{name}: no '{line}' in the log:\n{text}")
    for line in unwanted:
        if line in text:
            raise SystemExit(f"{name}: '{line}' in the log:\n{text}")


with tempfile.TemporaryDirectory(prefix="pcores-") as directory:
    work = pathlib.Path(directory)
    cpu_dir = work / "cpu"
    exe = str(work / "test")
    subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                    f'-DPCORES_CPU_DIR="{cpu_dir}"', "tests/pcores/test_main.c", "port/pcores.c", "-o", exe],
                   cwd=root, check=True)
    base = {k: v for k, v in os.environ.items() if k != "RECOMP_P_CORES"}
    count = 0

    def run(name, args, switch=None, **popen):
        env = dict(base, **({"RECOMP_P_CORES": switch} if switch is not None else {}))
        done = subprocess.run([exe, *args], env=env, capture_output=True, text=True, timeout=20, **popen)
        if done.returncode != 0:
            raise SystemExit(f"{name}: exit {done.returncode}\n{done.stderr}")
        return done.stderr

    # ---- kinds ----
    ally_x = [5000000 if cpu % 8 < 3 else 3300000 for cpu in range(16)]   # 3 Zen 5, 5 Zen 5c, two threads each
    ally_x_smt_off = [5100000] * 3 + [3300000] * 5                            # SMT off: one thread each
    raptor_lake = [5800000 if cpu in (4, 5) else 5500000 for cpu in range(16)] + [4300000] * 16
    meteor_lake = [4800000] * 12 + [3800000] * 8 + [2500000] * 2              # P, E and low-power E
    preferred = [5881000, 5760000, 5650000, 5600000, 5550000, 5500000, 5450000, 5450000] * 2
    x3d = ([5250000] * 8 + [5700000] * 8) * 2                                 # the cache die, the other
    offline = [5000000, 5000000, None, 3300000, 3300000, 3300000]
    kinds = {
        # name: (clocks, kinds, the P cores' CPUs, each of -1 to N as pcores_is_p gives it)
        "ally_x": (ally_x, 2, "0-2,8-10", [-1] + [1 if c % 8 < 3 else 0 for c in range(16)] + [0]),
        "ally_x_smt_off": (ally_x_smt_off, 2, "0-2", [-1, 1, 1, 1, 0, 0, 0, 0, 0, 0]),
        "raptor_lake": (raptor_lake, 2, "0-15", [-1] + [1] * 16 + [0] * 16 + [0]),
        "meteor_lake": (meteor_lake, 2, "0-11", [-1] + [1] * 12 + [0] * 10 + [0]),
        "preferred_cores": (preferred, 1, "", [-1] * 18),
        "x3d": (x3d, 1, "", [-1] * 34),
        "offline": (offline, 2, "0-1", [-1, 1, 1, 0, 0, 0, 0, 0]),
        "no_clocks": ([None] * 4, 0, "", [-1] * 6),
        "none": ([], 0, "", [-1, -1]),
    }
    for name, (khz, n_kinds, p, each) in kinds.items():
        processor(cpu_dir, khz)
        text = run(name, ["kinds", str(len(khz))])
        check(name, text, [f"[TEST] kinds {n_kinds}, P cores '{p}', each: " + " ".join(map(str, each))])
        count += 1

    # ---- cores ----
    # This machine's CPUs: the first half P cores, the rest compact.
    allowed = sorted(os.sched_getaffinity(0))
    if len(allowed) < 2:
        print("pcores: one CPU here, the cores cases skipped")
        print("pcores: all", count, "cases pass")
        raise SystemExit(0)
    everything = cpu_list(allowed)
    p = allowed[:len(allowed) // 2]
    two_kinds = [5000000 if cpu in p else 3300000 for cpu in range(allowed[-1] + 1)]
    processor(cpu_dir, two_kinds)
    text = run("cores", ["cores"], "1")
    check("cores", text, [
        f"[PCORES] RECOMP_P_CORES: the P cores are CPUs {cpu_list(p)}, up to 5.0 GHz, the rest up to 3.3 GHz",
        "[PCORES] the game's thread (", f"[TEST] start: {everything}", f"[TEST] held: {cpu_list(p)}",
        f"[TEST] child: {cpu_list(p)}", f"[TEST] child unpinned: {everything}", f"[TEST] stopped: {everything}"])
    # Started on some CPUs only (taskset): on the P cores of those, and given
    # back those.
    some = [p[0], allowed[-1]]
    text = run("cores_some", ["cores"], "1", preexec_fn=lambda: os.sched_setaffinity(0, some))
    check("cores_some", text, [f"[TEST] start: {cpu_list(some)}", f"[TEST] held: {p[0]}",
                               f"[TEST] child unpinned: {cpu_list(some)}", f"[TEST] stopped: {cpu_list(some)}"])
    # Started on compact cores only: none of the P cores to go to.
    compact = [allowed[-1]]
    text = run("cores_none_p", ["cores"], "1", preexec_fn=lambda: os.sched_setaffinity(0, compact))
    check("cores_none_p", text, ["[PCORES] the game's thread left as it was: none of the P cores is the game's to use",
                                 f"[TEST] held: {cpu_list(compact)}"], ["the game's thread ("])
    # One kind of core: nothing moves.
    processor(cpu_dir, [5000000] * (allowed[-1] + 1))
    text = run("cores_one_kind", ["cores"], "1")
    check("cores_one_kind", text, ["[PCORES] RECOMP_P_CORES: one kind of core here, up to 5.0 GHz; nothing changed",
                                   f"[TEST] held: {everything}", f"[TEST] child unpinned: {everything}"],
          ["the game's thread"])
    count += 4
    # The switch off, 0, or a word: anything but "" and 0 is on, as for the
    # other switches.
    processor(cpu_dir, two_kinds)
    for name, switch, on in (("off", None, False), ("zero", "0", False), ("empty", "", False), ("word", "yes", True)):
        text = run(name, ["cores"], switch)
        if on:
            check(name, text, [f"[TEST] held: {cpu_list(p)}"])
        else:
            check(name, text, [f"[TEST] held: {everything}"], ["[PCORES]"])
        count += 1
print("pcores: all", count, "cases pass")
