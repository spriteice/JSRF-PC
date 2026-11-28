"""The vertex epilogue's depth on the CPU (src/nv2a_gl/nv2a_vsh.c,
nv2a_vsh_ndc_z), which a line's pictures are drawn at, their letters' depth
(src/nv2a_gl/nv2a_prompts.h, NV2A_PROMPT_LINE_TAG), as the engine builds it:
optimised, NDEBUG, with the warnings CI counts, driven by test_main.c. And,
by text, the GLSL it mirrors: each line of the programmable epilogue's depth
in nv2a_vsh.c as many times as it was when the mirror was written, so a
change to the one without the other fails here. That a picture is then
drawn at its letters' depth needs the game and the renderer: the test
build's step on the graffiti load error box.
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

# The GLSL lines the mirror follows, each with how many times nv2a_vsh.c has it
# (the forward clip-range lines are the fixed-function half's too).
GLSL = {
    r'"          ? (zClip.y - oPos.z) / (zClip.y - zClip.x)\n"': 1,
    r'"          : (vpOff.z + (vpScale.z != 0.0 ? vpScale.z : 1.0) - oPos.z)\n"': 1,
    r'"          ? (oPos.z - zClip.x) / (zClip.y - zClip.x)\n"': 2,
    r'"          : (oPos.z - vpOff.z) / (vpScale.z != 0.0 ? vpScale.z : 1.0);\n"': 2,
    r'"        ndc.z = (16777215.0 - oPos.z) / 16777215.0;\n"': 1,
    r'"        ndc.z = oPos.z / 16777215.0;\n"': 1,
    r'"        gl_Position = vec4(oPos.xy, oPos.w - oPos.z, oPos.w);\n"': 1,
    r'"        gl_Position = vec4(oPos.xy, 2.0 * oPos.z - oPos.w, oPos.w);\n"': 1,
    r'"    ndc.z = (nv_away0(oPos.w) - oPos.z) / nv_away0(oPos.w);\n"': 1,
    r'"    ndc.z = oPos.z / nv_away0(oPos.w);\n"': 1,
    r'"    ndc.z = ndc.z * 2.0 - 1.0;   // D3D depth [0,1] -> GL [-1,1]\n"': 2,
    r'"#define NV2A_CLIP_Z01 0\n"': 1,
}
vsh = (root / "src/nv2a_gl/nv2a_vsh.c").read_text(encoding="utf-8")
for line, times in GLSL.items():
    if vsh.count(line) != times:
        sys.exit(f"vsh_ndc_z: {line} is in nv2a_vsh.c {vsh.count(line)} times, not {times}: "
                 "change nv2a_vsh_ndc_z with it")

with tempfile.TemporaryDirectory(prefix="vsh_ndc_z-") as directory:
    exe = str(pathlib.Path(directory) / "vsh_ndc_z.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-Isrc/nv2a_gl", "tests/vsh_ndc_z/test_main.c",
                         "src/nv2a_gl/nv2a_vsh.c", "-lm", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("vsh_ndc_z: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "vsh_ndc_z: all cases pass" not in text:
        sys.exit(f"vsh_ndc_z: exit {run.returncode}")
