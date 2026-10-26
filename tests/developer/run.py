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
# The CPU profile (port/cpuprof.c), Linux's only: started from developer.cfg,
# with its last report as the restart stops it.
linux = sys.platform.startswith("linux")
cases["cpu_profile"] = (None, "RECOMP_CPU_PROFILE=1\n", {},
                        ["[DEV] RECOMP_CPU_PROFILE=1 from developer.cfg", "[CPUPROF] on:",
                         "[CPUPROF] the last "] if linux else ["started:"],
                        [] if linux else ["CPU_PROFILE", "[CPUPROF]"])

# A switch in Developer > Experimental, the last of the list: from developer.cfg
# into the environment like the others.
cases["experiment"] = (None, "RECOMP_NV2A_QUEUE=1\n", {},
                       ["[DEV] RECOMP_NV2A_QUEUE=1 from developer.cfg", "started:"], [])

# The game's camera log, the one switch in Logging that port/recomp_manual.c
# reads: from developer.cfg into the environment like the others.
cases["camera_log"] = (None, "JSRF_CAM_LOG=1\n", {},
                       ["[DEV] JSRF_CAM_LOG=1 from developer.cfg", "started:"], [])

with tempfile.TemporaryDirectory(prefix="developer-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    # -Wno-unused-function: the settings code has helpers only the rest of
    # nv2a_gl.c calls.
    command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-DNV2A_GL_USE_SDL=1",
               "-x", "c", "-", "port/developer.c", "-x", "none", "port/cpuprof.c", "-o", exe]
    if linux:
        command.insert(1, "-pthread")
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, (settings_text, dev_text, extra, want, unwanted) in cases.items():
        folder = work / name
        folder.mkdir()
        if settings_text is not None:
            (folder / "settings.cfg").write_text(settings_text)
        if dev_text is not None:
            (folder / "developer.cfg").write_text(dev_text)
        env = {k: v for k, v in os.environ.items()
               if k not in (*switches, "RECOMP_CPU_PROFILE", "RECOMP_NV2A_QUEUE", "JSRF_CAM_LOG")}
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
print("developer: all", len(cases), "cases pass")
