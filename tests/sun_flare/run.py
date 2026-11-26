"""Compile the sun flare's fix and its log, and drive them without the game.

Takes port/sun_flare.c as it is; test_main.c stands in for the hooks in
port/recomp_manual.c, with made-up pieces, tests and results. Each case is its own
run, as the switches are read once.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))

# case: (argument, environment, lines the output must have, lines it must not)
cases = {
    "ends": ("ends", {}, ["ends: the fix's line is the piece in all 8 ways; the game's in 1"], []),
    "steady": ("steady", {}, ["steady: an edge and a wall with gaps aren't given, a solid wall is; hidden on the 57th "
                          "blocked test of 60, shown again on the 15th clear"],
               ["FAIL"]),
    "note": ("note", {}, [
        "[SUNFLARE] t=10.00 flare 00ABC000 state 1: eye (100, 20, -300), sun's way (0.53, 0.47, -0.71), "
        "game's line mirrored in z; 12 pieces, 3 tested; game's line clear, real line blocked at piece 5; "
        "fix off, so the glare shows in sight\n",
        "[SUNFLARE] t=12.00 flare 00ABC000 state 1:",
        "[SUNFLARE] t=12.30 flare 00ABC000 state 1: eye (100, 20, -300), sun's way (0.53, 0.47, -0.71), "
        "game's line mirrored in z; 12 pieces, 3 tested; game's line clear, real line clear; fix off, so "
        "the glare shows in sight (changes held back since the last line)\n",
        "[SUNFLARE] t=13.00 flare 00ABC000 state 2: eye (100, 20, -300), sun's way (0.53, 0.47, -0.71), "
        "game's line mirrored in z; 12 pieces, 3 tested; game's line blocked at piece 1, real line "
        "blocked at piece 1; fix on, so the glare shows blocked\n",
        "[SUNFLARE] t=13.30 flare 00ABD000 state 2:",
    ], ["t=10.10", "t=11.90", "t=12.10"]),
    "off": ("switches", {}, ["fix 0, log 0"], ["[SUNFLARE]"]),
    "zero": ("switches", {"JSRF_SUN_FIX": "0", "JSRF_SUN_LOG": "0"}, ["fix 0, log 0"], ["[SUNFLARE]"]),
    "on": ("switches", {"JSRF_SUN_FIX": "1", "JSRF_SUN_LOG": "1"}, [
        "[SUNFLARE] Sun flare fix on (JSRF_SUN_FIX): the flare's line tested along each piece",
        "fix 1, log 1"], []),
    "log_only": ("switches", {"JSRF_SUN_LOG": "1"}, ["fix 0, log 1"], ["[SUNFLARE]"]),
}

with tempfile.TemporaryDirectory(prefix="sun-flare-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    command = cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Iport",
                    "tests/sun_flare/test_main.c", "port/sun_flare.c", "-lm", "-o", exe]
    subprocess.run(command, cwd=root, check=True)
    for name, (arg, extra, want, unwanted) in cases.items():
        env = {k: v for k, v in os.environ.items() if k not in ("JSRF_SUN_FIX", "JSRF_SUN_LOG")}
        env.update(extra)
        run = subprocess.run([exe, arg], env=env, capture_output=True, text=True, timeout=10)
        out = run.stdout + run.stderr
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{out}")
        for line in want:
            if line not in out:
                raise SystemExit(f"{name}: no '{line}' in the output:\n{out}")
        for line in unwanted:
            if line in out:
                raise SystemExit(f"{name}: '{line}' in the output:\n{out}")
print("sun_flare: all", len(cases), "cases pass")
