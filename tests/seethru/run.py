"""The see-through fix (src/nv2a_gl/nv2a_seethru.c), as the engine builds it:
optimised, NDEBUG, with the warnings CI counts, linked with the uniform table
(nv2a_uniforms.c) and driven by test_main.c on a device of its own. Run three
times: as built, with RECOMP_GL_SEETHRU_HOLD=0, and with
RECOMP_GL_SEETHRU_LOG=1, whose census is checked here.
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

# mode: (environment, lines the log must have, each once)
modes = {
    "hold": ({}, ["what shows through see-through surfaces is drawn behind them",
                  "a texture changed while a draw was held back: the rest of it drawn in place"]),
    "inplace": ({"RECOMP_GL_SEETHRU_HOLD": "0"},
                ["drawn over them: their see-through pixels in place (RECOMP_GL_SEETHRU_HOLD=0)"]),
    "census": ({"RECOMP_GL_SEETHRU_LOG": "1"},
               ["census on: one whole frame in 600",
                "[SEETHRU] frame 600: 4 draws, 2 see-through with depth writes: 2 split"
                " (0 of them drawn in place, 1 using the stencil), kept whole 0 for an outline",
                " 0 for their depth function, 0 with the fix off;"
                " 1 more held back whole; 1 held-back lists drawn (1 at a clear),"
                " 3 draws drawn again,",
                "draws 1-1 (1): see-through with depth writes, surface 00001000, programs 1,"
                " 3 verts, 1 textured; blend 0302 0303, alpha test on, depth func 203, split",
                "draws 2-2 (1): no depth test, surface 00001000, programs 3, 3 verts,"
                " 1 textured, held back",
                "draws 3-3 (1): see-through with depth writes, stencil func 205 ref 01"
                " masks FF FF ops 1E00 1E00 1E01, surface 00001000, programs 1, 3 verts,"
                " 1 textured; blend 0302 0303, alpha test on, depth func 203, split",
                "at draw 3: 3 held back drawn, at a clear",
                "at draw 3: depth clear, surface 00001000"]),
}

with tempfile.TemporaryDirectory(prefix="seethru-") as directory:
    exe = str(pathlib.Path(directory) / "seethru.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-Isrc/nv2a_gl", "tests/seethru/test_main.c",
                         "src/nv2a_gl/nv2a_seethru.c", "src/nv2a_gl/nv2a_uniforms.c",
                         "-o", exe],
                   cwd=root, check=True)
    failed = []
    for mode, (env, want) in modes.items():
        e = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_GL_SEETHRU_")}
        e.update(env)
        try:
            run = subprocess.run([exe, mode], capture_output=True, text=True, timeout=60, env=e)
        except subprocess.TimeoutExpired:
            sys.exit(f"seethru {mode}: no end in 60 s")
        text = run.stdout + run.stderr
        print(text, end="")
        if run.returncode != 0 or f"seethru: all cases pass ({mode})" not in text:
            failed.append(f"{mode}: exit {run.returncode}")
        for line in want:
            if text.count(line) != 1:
                failed.append(f"{mode}: {text.count(line)} lines with {line!r}, not 1")
    if failed:
        sys.exit("seethru: " + "; ".join(failed))
    print("seethru: all modes pass")
