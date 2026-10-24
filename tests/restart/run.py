"""Compile the game's restart, and see what the new run is handed.

Takes recomp_restart_self from port/main.c verbatim, with the helpers before
it (from "A restart hands the new run" to game_dir). The first run opens files
as the engine does and restarts; the second checks that none of them came
across, and that standard output -- the launchers' log -- did. Off Windows it
runs twice more: with descriptors marked one at a time, as where close_range
is missing, and with a restart that fails, which must leave the files open.
Requires GCC on PATH, including MinGW on Windows.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
main = (root / "port/main.c").read_text(encoding="utf-8")
restart = main[main.index("/* A restart hands the new run"):
               main.index("static const char *game_dir(void)")]
source = """
#define _GNU_SOURCE 1
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif
#if defined(RESTART_TEST_LOOP)
#  undef CLOSE_RANGE_CLOEXEC
#endif
static char **g_argv;
static int g_scale_env_ours;
void developer_restart(void) { }
""" + restart + (root / "tests/restart/test_main.c").read_text(encoding="utf-8")

# variant: (extra compiler flags, case, lines its output must have)
variants = {"restart": ([], "restart", ["second run: nothing came across"])}
if sys.platform != "win32":
    variants["restart_loop"] = (["-DRESTART_TEST_LOOP"], "restart",
                                ["first run: marked one at a time",
                                 "second run: nothing came across"])
    variants["failed"] = ([], "failed", ["first run: all still open"])
    if sys.platform.startswith("linux"):
        variants["restart"][2].insert(0, "first run: marked by close_range")

with tempfile.TemporaryDirectory(prefix="restart-") as directory:
    work = pathlib.Path(directory)
    for name, (flags, case, want) in variants.items():
        exe = str(work / (name + ".exe"))
        command = ["gcc", "-std=c11", "-Wall", "-Wextra", *flags, "-x", "c", "-", "-o", exe]
        subprocess.run(command, cwd=root, input=source, text=True, check=True)
        env = dict(os.environ, RESTART_TEST_DIR=directory)
        env.pop("RESTART_TEST_RUN", None)
        run = subprocess.run([exe, case], env=env, capture_output=True, text=True, timeout=60)
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        for line in want:
            if line not in run.stdout:
                raise SystemExit(f"{name}: no '{line}' in the output:\n{run.stdout}{run.stderr}")
        print(f"ok {name}")
print("restart: all", len(variants), "cases pass")
