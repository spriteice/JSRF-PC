"""Compile the log's [RUN] and [HOST] lines, src/platform/host_info.c, and drive them without the game.

test_main.c is built with host_info.c as it is, and run once a case:

- switches: the RECOMP_ and JSRF_ variables from environments of the
  test's making: none of them, some among others in no order (named in name
  order, and RECOMPILE= left out), more than the line holds (cut short with
  "..."), and no environment at all;
- power: the power state read from a tree of /sys and /proc files of the
  test's making, in place of the system's own: a desktop with no power
  supply listed; a handheld on mains with its battery charging, a pad's
  battery beside it (a device's, so not the system's), a platform profile,
  and a thread whose name has spaces and parentheses; on battery, with a
  thread whose /proc file is gone; and a thread on a core with no cpufreq.

Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))


def write(base, path, text):
    p = base / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text + "\n")


def supply(base, name, **files):
    for key, value in files.items():
        write(base, f"sys/class/power_supply/{name}/{key}", str(value))


def thread(base, tid, comm, cpu):
    """/proc/self/task/<tid>/stat with the core it last ran on in field 39."""
    fields = ["S"] + [str(n) for n in range(4, 53)]
    fields[39 - 3] = str(cpu)
    write(base, f"proc/self/task/{tid}/stat", f"{tid} ({comm}) " + " ".join(fields))


def core(base, cpu, khz=None, governor=None, epp=None):
    d = f"sys/devices/system/cpu/cpu{cpu}/cpufreq"
    if khz is not None:
        write(base, f"{d}/scaling_cur_freq", str(khz))
    if governor is not None:
        write(base, f"{d}/scaling_governor", governor)
    if epp is not None:
        write(base, f"{d}/energy_performance_preference", epp)


def run(exe, *args):
    out = subprocess.run([exe, *args], capture_output=True, text=True, timeout=30)
    if out.returncode:
        raise SystemExit(f"{' '.join(args)}: exit {out.returncode}\n{out.stdout}{out.stderr}")
    return out.stdout


def expect(name, got, want):
    if got != want:
        raise SystemExit(f"{name}:\n  got  {got!r}\n  want {want!r}")
    print(f"host_info: {name}: ok")


with tempfile.TemporaryDirectory(prefix="host_info-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{root / 'src'}",
                         str(root / "tests/host_info/test_main.c"),
                         str(root / "src/platform/host_info.c"), "-o", exe],
                   check=True)

    lines = run(exe, "switches").splitlines()
    expect("switches, none", lines[0], "none: 0 [none]")
    expect("switches, some", lines[1],
           "some: 5 [JSRF_= JSRF_SETTINGS_FILE=/x/settings.cfg RECOMP_A=2 "
           "RECOMP_APU_STATS=1 RECOMP_LOCK_WATCH=1]")
    expect("switches, cut short", lines[2], "cut: 3 [RECOMP_AAAA=0123456789 ...] 26")
    expect("switches, no environment", lines[3], "null: 0 [none]")

    desktop = work / "desktop"
    thread(desktop, 100, "jsrf_recomp", 3)
    core(desktop, 3, khz=4700000, governor="performance")
    expect("power, a desktop", run(exe, "power", str(desktop), "100", "0").strip(),
           "no power supply listed; the game's thread on cpu 3, 4700 MHz (performance); "
           "the renderer's not started")

    handheld = work / "handheld"
    supply(handheld, "ACAD", type="Mains", online=1)
    supply(handheld, "BAT0", type="Battery", capacity=87, status="Charging")
    supply(handheld, "hid-pad-battery", type="Battery", scope="Device", capacity=10,
           status="Discharging")
    write(handheld, "sys/firmware/acpi/platform_profile", "balanced")
    thread(handheld, 200, "jsrf (main) x", 2)
    thread(handheld, 201, "jsrf_recomp", 5)
    core(handheld, 2, khz=4801000, governor="powersave", epp="balance_performance")
    expect("power, a handheld on mains", run(exe, "power", str(handheld), "200", "201").strip(),
           "on mains (battery 87%, charging); platform profile balanced; the game's thread on "
           "cpu 2, 4801 MHz (powersave, epp balance_performance); the renderer's on cpu 5, "
           "its clock not known")

    battery = work / "battery"
    supply(battery, "ACAD", type="Mains", online=0)
    supply(battery, "BAT1", type="Battery", capacity=55, status="Discharging")
    expect("power, on battery", run(exe, "power", str(battery), "300", "0").strip(),
           "on battery (battery 55%, discharging); the game's thread on a core not known; "
           "the renderer's not started")

print("host_info: all passed")
