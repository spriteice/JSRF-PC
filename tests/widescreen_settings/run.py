"""Compile the window's widescreen settings and the settings file code, and read them without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c, unchanged,
through nv_window_pref_shape. nv2a_gl.c doesn't link on its own.

And the string settings the input layer's layouts keep in remap.cfg,
beside the settings file (nv_window_file_str and nv_window_file_set_str,
src/input/input_remap.c): a key's last line's text, without its line end;
no line for a key with none, one that only starts the same, or one only in
the settings file; kept in place of the key's lines; and a line gone for
none, the others kept; and the settings file left as it was.

Takes the settings file code from src/nv2a_gl/nv2a_gl.c verbatim
(sdl_cfg_path through nv_window_pref_shape, which nv2a_widescreen() and its
shape ask at start), as tests/intro takes it up to nv_window_pref_widescreen,
linked with src/nv2a_gl/nv2a_wide.c for the shapes' keys, and with
src/nv2a_gl/nv2a_scale.c, which nv2a_wide.c sizes its rasters by. Each case
gets its own settings file, passed as JSRF_SETTINGS_FILE, or none.
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
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
settings = gl[gl.index("static void sdl_cfg_path(char *buf, size_t n)"):
              gl.index("/* The keyboard and the mouse as a gamepad")]
source = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "nv2a_wide.h"
#if defined(_WIN32)
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)
#endif
""" + settings + (root / "tests/widescreen_settings/test_main.c").read_text(encoding="utf-8")

# case: settings file, or None for none
cases = {
    "none": None,
    "other_keys": "game_dir=/games/jsrf\nscale=3\n",
    "old_wide": "widescreen=1\n",
    "old_43": "widescreen=0\n",
    "shape": "widescreen=1\naspect=21:9\r\n",
    "shape_43": "aspect=16:10\nwidescreen=0\n",
    "bad_shape": "widescreen=1\naspect=5:4\n",
    "strings": "key_a=J\nwidescreen=1\npad_b=y\n",
}
# remap.cfg beside the settings file, for a case, and what it holds after:
# written in text mode, as the settings file is, so its line ends are the
# system's (CR LF on Windows)
remap = {"strings": ("key_a=Space\nkey_a=Left Ctrl\r\npad_b=x\n", "pad_b=x\nkey_a_2=none\n")}

with tempfile.TemporaryDirectory(prefix="widescreen-settings-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    command = cc + ["-std=c11", "-DNV2A_GL_USE_SDL=1", "-Isrc/nv2a_gl", "-x", "c", "-",
                    "-x", "none", "src/nv2a_gl/nv2a_wide.c", "src/nv2a_gl/nv2a_scale.c",
                    "-lm", "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, text in cases.items():
        path = work / (name + ".cfg")
        if text is not None:
            path.write_bytes(text.encode())
        if name in remap:
            (work / "remap.cfg").write_bytes(remap[name][0].encode())
        env = dict(os.environ, JSRF_SETTINGS_FILE=str(path))
        run = subprocess.run([exe, name], env=env, capture_output=True, text=True, timeout=10)
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        if name in remap:
            if (work / "remap.cfg").read_bytes() != remap[name][1].replace("\n", os.linesep).encode():
                raise SystemExit(f"{name}: remap.cfg holds {(work / 'remap.cfg').read_bytes()!r}")
            if path.read_bytes() != text.encode():
                raise SystemExit(f"{name}: the settings file changed: {path.read_bytes()!r}")
        print(run.stdout.strip())
print("widescreen settings: all", len(cases), "cases pass")
