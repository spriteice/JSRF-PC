"""Compile the button prompts' art (src/input/prompt_art.c, with the layouts'
src/input/input_layout.c) and the icons drawn in the game's textures
(src/nv2a_gl/nv2a_prompts.c), and drive them without SDL or a device.

The pictures are built in as the engine builds them: src/input/prompts/
embed.cmake makes the C file from src/input/prompts/'s PNGs (so this needs
CMake on PATH, as the build does), and miniz (third_party/miniz) decodes
them, built as src/input/CMakeLists.txt builds it.

The cases: every picture a set asks for is built in and decodes, and every
one built in is asked for (art); no picture mostly white, the face buttons
in their colours and the rest dark grey, the Xbox and PlayStation ones the
Nintendo ones' grey, the PlayStation D-pad's lit arms and left stick's
arrows outlined (colours); PNGs decoded, with each row filter, and refused when they
aren't the kinds the decoder reads (png); the table's textures found
by their size, format and content alone, said once each, and the engine's
own table in order (find); the icons drawn at each level, as the game's own
art of another control, the packs' pictures, or kept (draw); a key, the
PlayStation's Mute or a paddle, with no picture, on the blank key with its name (label); the names' own font
(text); a picture fitted to a
rectangle, down, up and keeping its shape (fit); an upscaled copy's
icons (icon_at); the message boxes' flat pictures, the packs' or the
Xbox set's for the game's art (control_at); where a flat picture goes on the frame (place); a pad's
whole D-pad or left stick, flat, for the graffiti editor's help lines (group_at), and a picture cut
to that editor's ticker's window (window_cut); and the
menus' turned or slanted buttons: which icon a triangle's texels are on (tri_icon); the faces found
among a draw's triangles, a button's front apart from its back and side, as a list, a strip or a fan,
and in perspective (faces); where a face goes upright (face_place), and a smaller copy of one at the
same place in a frame laid as it (twin), and the rest of a turned button within its
upright picture's reach, to be made empty (reach); the positions that put it
there through a draw's transform, in 2D and in perspective (solve); and for Upscaling's Buttons row,
every picture small enough for a copy of the whole of it, and a copy read back (copy), and the
pictures drawn from the window's copies where it gives them (copies).
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
cmake = shutil.which("cmake")
if not cmake:
    print("prompts: SKIPPED, no cmake on PATH to build the pictures in")
    sys.exit(77)
MINIZ = ["-DMINIZ_NO_STDIO", "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_TIME"]

with tempfile.TemporaryDirectory(prefix="prompts-") as directory:
    work = pathlib.Path(directory)
    data = work / "prompt_art_data.c"
    subprocess.run([cmake, "-DSRC=" + str(root / "src/input/prompts"), "-DOUT=" + str(data),
                    "-P", str(root / "src/input/prompts/embed.cmake")], check=True)
    miniz = work / "miniz.o"
    subprocess.run(cc + ["-O2", "-w"] + MINIZ + ["-c", "third_party/miniz/miniz.c", "-o", str(miniz)],
                   cwd=root, check=True)
    exe = str(work / "prompts")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DNDEBUG",
                    "-I", "src", "-I", "src/input", "-I", "src/nv2a_gl",
                    "src/input/input_layout.c", "src/input/prompt_art.c", str(data),
                    "src/nv2a_gl/nv2a_prompts.c", "tests/prompts/test_main.c", str(miniz),
                    "-o", exe, "-lm"], cwd=root, check=True)
    # Each case, and the lines it must say in the log, in their order.
    cases = {
        "art": ["[INPUT] button prompts: no picture No_Such_Picture built in"],
        "colours": [],
        "png": [],
        "find": ["  [GL] button prompts: 64x32 format 0E head %s key %s: Made/Up.dat 0x20, its icons drawn",
                 "  [GL] button prompts: 64x32 format 0E head %s key %s: not in the table",
                 "  [GL] button prompts: 64x32 format 0E head %s: not in the table"],
        "draw": [], "label": [], "text": [], "fit": [], "icon_at": [], "control_at": [], "place": [],
        "group_at": [], "window_cut": [],
        "tri_icon": [], "faces": [], "face_place": [], "twin": [], "reach": [], "solve": [], "copy": [], "copies": [],
    }
    for name, said in cases.items():
        run = subprocess.run([exe, name], capture_output=True, text=True, timeout=120)
        print(run.stdout + run.stderr, end="")
        if run.returncode != 0 or f"prompts {name}: pass" not in run.stdout:
            sys.exit(f"prompts {name}: exit {run.returncode}")
        lines = run.stderr.splitlines()
        # The hashes are the made-up texture's: any eight hex digits.
        shape = [re.sub(r"\b[0-9A-F]{8}\b", "%s", line) for line in lines]
        if shape != said:
            sys.exit(f"prompts {name}: the log says {lines}, not {said}")
print("prompts: all cases pass")
