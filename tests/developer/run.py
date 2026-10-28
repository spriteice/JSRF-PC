"""Compile the Developer menu's start-up and restart steps, and drive them without the game.

Takes port/developer.c as it is, and the settings file code from
src/nv2a_gl/nv2a_gl.c verbatim, as tests/freecam does. Each case gets its own
folder with a settings file, passed as JSRF_SETTINGS_FILE, and developer.cfg
beside it, and its own run, with its output going to a log file as the
launchers' does. Requires GCC on PATH, including MinGW on Windows.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
settings = gl[gl.index("static void sdl_cfg_path(char *buf, size_t n)"):
              gl.index("/* nv2a_widescreen() asks this when RECOMP_WIDESCREEN is not set. */")]
source = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)
#endif
""" + settings + (root / "tests/developer/test_main.c").read_text(encoding="utf-8")

# case: (settings file, developer.cfg, environment, lines the log must have,
# lines it must not)
switches = ("RECOMP_GL_PROFILE", "RECOMP_LOCK_WATCH", "RECOMP_GL_TEX_ONESAMPLER",
            "RECOMP_GL_TITLE_MIPS", "RECOMP_GL_CURTAIN")
cases = {
    "none": (None, None, {},
             [f"{when}: {name}=-" for when in ("started", "restarting") for name in switches],
             ["[DEV]"]),
    "switches": (None, "RECOMP_GL_PROFILE=1\nRECOMP_LOCK_WATCH=0\n", {},
                 ["[DEV] RECOMP_GL_PROFILE=1 from developer.cfg",
                  "started: RECOMP_GL_PROFILE=1", "started: RECOMP_LOCK_WATCH=-",
                  "restarting: RECOMP_GL_PROFILE=-", "restarting: RECOMP_LOCK_WATCH=-"], []),
    "environment": (None, "RECOMP_GL_PROFILE=1\n", {"RECOMP_GL_PROFILE": "2"},
                    ["started: RECOMP_GL_PROFILE=2", "restarting: RECOMP_GL_PROFILE=2"],
                    ["from developer.cfg"]),
    # Engine switches: one ticked to 1, one ticked to 0 (RECOMP_GL_TITLE_MIPS=0).
    "engine": (None, "RECOMP_GL_TEX_ONESAMPLER=1\nRECOMP_GL_TITLE_MIPS=0\n", {},
               ["[DEV] RECOMP_GL_TEX_ONESAMPLER=1 from developer.cfg",
                "[DEV] RECOMP_GL_TITLE_MIPS=0 from developer.cfg",
                "started: RECOMP_GL_TEX_ONESAMPLER=1", "started: RECOMP_GL_TITLE_MIPS=0",
                "restarting: RECOMP_GL_TEX_ONESAMPLER=-", "restarting: RECOMP_GL_TITLE_MIPS=-"], []),
    # Unticked: off for one set to 1, and the engine's own 1 for one set to 0.
    "engine_unticked": (None, "RECOMP_GL_TEX_ONESAMPLER=0\nRECOMP_GL_TITLE_MIPS=1\n", {},
                        ["started: RECOMP_GL_TEX_ONESAMPLER=-", "started: RECOMP_GL_TITLE_MIPS=-"],
                        ["[DEV]"]),
    # The one switch whose value is a word: ticked (1 in developer.cfg), skip.
    "curtain": (None, "RECOMP_GL_CURTAIN=1\n", {},
                ["[DEV] RECOMP_GL_CURTAIN=skip from developer.cfg",
                 "started: RECOMP_GL_CURTAIN=skip", "restarting: RECOMP_GL_CURTAIN=-"], []),
    "curtain_unticked": (None, "RECOMP_GL_CURTAIN=0\n", {},
                         ["started: RECOMP_GL_CURTAIN=-"], ["[DEV]"]),
    "log_off": ("game_dir=/games/jsrf\nlogging=0\n", None, {},
                ["Log to file off", "restarting:"], ["started:"]),
    "log_on": ("logging=1\n", None, {}, ["started:", "restarting:"], ["Log to file off"]),
}
# The CPU profile (port/cpuprof.c), Linux's and Windows' only: started from
# developer.cfg, with its last report as the restart stops it.
linux = sys.platform.startswith("linux")
profiled = linux or sys.platform == "win32"
cases["cpu_profile"] = (None, "RECOMP_CPU_PROFILE=1\n", {},
                        ["[DEV] RECOMP_CPU_PROFILE=1 from developer.cfg", "[CPUPROF] on:",
                         "[CPUPROF] the last "] if profiled else ["started:"],
                        [] if profiled else ["CPU_PROFILE", "[CPUPROF]"])

# The GPU queue, the engine's way since b1.2 and a switch under Performance
# that turns it off: ticked, 0 in developer.cfg, into the environment. A developer.cfg
# from b1.1, where it was an experiment, keeps what was chosen there: ticked
# (1) is the engine's way now, so nothing is set.
cases["queue_off"] = (None, "RECOMP_NV2A_QUEUE=0\n", {},
                      ["[DEV] RECOMP_NV2A_QUEUE=0 from developer.cfg", "started:"], [])
cases["queue_from_b1_1"] = (None, "RECOMP_NV2A_QUEUE=1\n", {}, ["started:"], ["[DEV]"])

# Use P cores (port/pcores.c), Linux's only and on by default: with nothing
# in developer.cfg, or the 1 that b1.2's test builds kept when it was off by
# default, it starts; unticked, 0 in developer.cfg, it doesn't (tests/pcores
# tests the rest).
cases["p_cores"] = (None, None, {}, ["[PCORES] RECOMP_P_CORES"] if linux else ["started:"],
                    ["[DEV]"] if linux else ["P_CORES", "[PCORES]"])
cases["p_cores_kept_1"] = (None, "RECOMP_P_CORES=1\n", {},
                           ["[PCORES] RECOMP_P_CORES"] if linux else ["started:"],
                           ["[DEV]"] if linux else ["P_CORES", "[PCORES]"])
cases["p_cores_off"] = (None, "RECOMP_P_CORES=0\n", {},
                        ["[DEV] RECOMP_P_CORES=0 from developer.cfg", "started:"] if linux else ["started:"],
                        ["[PCORES]"] if linux else ["P_CORES", "[PCORES]"])

# Display pacing (nv2a_display_clock.c), Linux's only and on by default: a
# switch under Performance that turns it off, ticked, 0 in developer.cfg
# (tests/display_clock tests the rest).
cases["pace_display_off"] = (None, "RECOMP_PACE_DISPLAY=0\n", {},
                             ["[DEV] RECOMP_PACE_DISPLAY=0 from developer.cfg", "started:"] if linux
                             else ["started:"],
                             [] if linux else ["PACE_DISPLAY"])

# The game's camera log, the one switch in Logging that port/recomp_manual.c
# reads: from developer.cfg into the environment like the others.
cases["camera_log"] = (None, "JSRF_CAM_LOG=1\n", {},
                       ["[DEV] JSRF_CAM_LOG=1 from developer.cfg", "started:"], [])

with tempfile.TemporaryDirectory(prefix="developer-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    menu_exe = str(work / "menu.exe")
    # -Wno-unused-function: the settings code has helpers only the rest of
    # nv2a_gl.c calls.
    command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-DNV2A_GL_USE_SDL=1",
               "-x", "c", "-", "port/developer.c", "-x", "none", "port/cpuprof.c", "port/pcores.c"]
    if linux:
        command.insert(1, "-pthread")
    subprocess.run([*command, "-o", exe], cwd=root, input=source, text=True, check=True)
    subprocess.run([*command, "-DXBOXRECOMP_MENU=1", "-Isrc/menu", "-o", menu_exe],
                   cwd=root, input=source, text=True, check=True)
    for name, (settings_text, dev_text, extra, want, unwanted) in cases.items():
        folder = work / name
        folder.mkdir()
        if settings_text is not None:
            (folder / "settings.cfg").write_text(settings_text)
        if dev_text is not None:
            (folder / "developer.cfg").write_text(dev_text)
        env = {k: v for k, v in os.environ.items()
               if k not in (*switches, "RECOMP_CPU_PROFILE", "RECOMP_NV2A_QUEUE", "RECOMP_P_CORES",
                            "RECOMP_PACE_DISPLAY", "JSRF_CAM_LOG")}
        env.update(extra, JSRF_SETTINGS_FILE=str(folder / "settings.cfg"))
        log = folder / "last-run.log"
        with open(log, "w") as out:
            run = subprocess.run([exe], env=env, stdout=out, stderr=out, timeout=10)
        text = log.read_text()
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{text}")
        for line in want:
            if line not in text:
                raise SystemExit(f"{name}: no '{line}' in the log:\n{text}")
        for line in unwanted:
            if line in text:
                raise SystemExit(f"{name}: '{line}' in the log:\n{text}")
        if settings_text is not None and (folder / "settings.cfg").read_text() != settings_text:
            raise SystemExit(f"{name}: the settings file changed:\n" + (folder / "settings.cfg").read_text())

    # The menu's pages, in the order they are shown: Logging, Engine switches,
    # then Performance, whose first row is Use P cores and stays so, and no
    # Experimental while it has no row. Performance's rows are what the engine
    # does by default, all ticked with nothing in developer.cfg; the GPU
    # queue's 0 there unticks its row alone.
    def menu(dev_text):
        folder = work / f"menu-{len(dev_text)}"
        folder.mkdir()
        if dev_text:
            (folder / "developer.cfg").write_text(dev_text)
        env = {k: v for k, v in os.environ.items() if not k.startswith(("RECOMP_", "JSRF_"))}
        env.update(JSRF_SETTINGS_FILE=str(folder / "menu.cfg"))
        run = subprocess.run([menu_exe], env=env, capture_output=True, text=True, timeout=10)
        pages, ticks = {}, {}
        for line in run.stderr.splitlines():
            if line.startswith("[MENU] "):
                label, _, rows = line[len("[MENU] "):].partition(": ")
                pages[label] = rows.split(" | ")
            elif line.startswith("[TICKS] "):
                label, _, bits = line[len("[TICKS] "):].partition(": ")
                ticks[label] = int(bits)
        if run.returncode != 0 or list(pages) != ["Logging", "Engine switches", "Performance"]:
            raise SystemExit(f"menu: exit {run.returncode}, pages {list(pages)}\n{run.stderr}")
        return pages, ticks
    pages, ticks = menu("")
    perf = ["Use P cores (Restart)"] if linux else []
    pace = ["Display pacing (Restart)"] if linux else []
    if pages["Performance"] != [*perf, "GPU queue (Restart)", *pace]:
        raise SystemExit(f"menu: Performance's rows are {pages['Performance']}")
    every = (1 << len(pages["Performance"])) - 1
    if ticks["Performance"] != every or ticks["Engine switches"] or ticks["Logging"]:
        raise SystemExit(f"menu: with nothing changed, the ticks are {ticks}")
    queue = 1 << len(perf)
    pages, ticks = menu("RECOMP_NV2A_QUEUE=0\n")
    if ticks["Performance"] != every & ~queue:
        raise SystemExit(f"menu: with the GPU queue off, Performance's ticks are {ticks['Performance']}")
    if any(name in row for row in pages["Engine switches"]
           for name in ("RECOMP_NV2A_QUEUE", "RECOMP_P_CORES", "RECOMP_PACE_DISPLAY")):
        raise SystemExit(f"menu: Engine switches' rows are {pages['Engine switches']}")
print("developer: all", len(cases), "cases and the menu's pages pass")
