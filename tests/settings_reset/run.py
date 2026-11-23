"""Compile File > Presets > Reset's start, and drive it without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c, unchanged,
with nv_window_settings_reset in it, and the start that calls it
(sdl_reset_env and nv_window_reset_at_start); nv2a_gl.c doesn't link on its
own.

Each case gets its own folder with a settings file, passed as
JSRF_SETTINGS_FILE, and files beside it, and its own run, told to reset
(RECOMP_RESET_SETTINGS=1, as nv_window_reset restarts the game) or not. A
reset keeps only the settings file's game_dir= lines, in order, as they
were (Windows' line ends too; on Windows, each with Windows' line end, as
the game writes the file there), and takes out the files beside it that
hold settings, remap.cfg and developer.cfg, leaving the rest
(start-states.txt, recordings) and nothing of its own behind; and the next
start, File > Restart's, isn't told to reset again. Not told, or told with
anything but 1, nothing changes.

And by text: that port/main.c resets first, before anything reads a
setting; that the files it takes out are every file beside the settings
file that the code keeps settings in (each nv_window_file_get, _set and
_str call's); and that the line it keeps is the launchers' game_dir=.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")


def cut(text, start, end):
    at = text.index(start)
    return text[at:text.index(end, at)]


settings = cut(gl, "static void sdl_cfg_path(char *buf, size_t n)",
               "/* nv2a_widescreen() asks this when RECOMP_WIDESCREEN is not set. */")
start = (cut(gl, "#define SDL_RESET_ENV", "void nv_window_reset(void);")
         + cut(gl, "void nv_window_reset_at_start(void);", "\n}\n") + "\n}\n")
source = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)
#endif
""" + settings + start + (root / "tests/settings_reset/test_main.c").read_text(encoding="utf-8")

# By text: port/main.c resets first in host_main, before the Developer
# menu's switches and everything else it starts.
main = (root / "port/main.c").read_text(encoding="utf-8")
host = cut(main, "static int host_main(void)", "\n}\n")
calls = re.findall(r"\{ extern void (\w+)\(void\); \1\(\); \}", host)
if not calls or calls[0] != "nv_window_reset_at_start" or "developer_start" not in calls:
    sys.exit(f"settings_reset: host_main starts with {calls[:3]}, not nv_window_reset_at_start")

# The files beside the settings file that hold settings: each file the code
# hands nv_window_file_get, _set or _str, by its #define in that file.
side = re.findall(r'"([^"]+)"', cut(gl, "static const char *const k_cfg_side[] = {", "};"))
used = {}
for path in [*(root / "port").glob("*.c"), *(root / "src").rglob("*.c"), *(root / "src").rglob("*.cpp")]:
    if path.name == "nv2a_gl.c":
        continue
    text = path.read_text(encoding="utf-8", errors="replace")
    # The calls' (a declaration's first word is const).
    for name in re.findall(r"nv_window_file_(?:get|set|str)\((?!const\b)(\w+)", text):
        value = re.search(r'#define\s+' + name + r'\s+"([^"]+)"', text)
        if not value:
            sys.exit(f"settings_reset: {path.relative_to(root)} names its file {name}, not by a #define")
        used[value.group(1)] = path.relative_to(root)
if not used or sorted(used) != sorted(side):
    sys.exit(f"settings_reset: Reset takes out {side}; the code keeps settings in {sorted(used)}")

# The line kept: SDL_CFG_KEEP, the launchers' key for where the game is.
keep = re.search(r'#define SDL_CFG_KEEP "(\w+)"', gl).group(1)
for launcher in ("launcher/linux/jsrf_launcher.c", "launcher/windows/jsrf_launcher.c"):
    if f'cfg_set("{keep}", ' not in (root / launcher).read_text(encoding="utf-8"):
        sys.exit(f"settings_reset: {launcher} keeps no {keep}=")

told = "RECOMP_RESET_SETTINGS"
settings_text = ("fullscreen=0\ngame_dir=/games/jsrf\nmsaa=8\ngame_directory=/elsewhere\n"
                 " game_dir=/indented\ngame_dir=/games/other jsrf\nresolution=match\n")
kept_text = "game_dir=/games/jsrf\ngame_dir=/games/other jsrf\n"
# The reset writes the file as the settings code's own writer, sdl_cfg_write,
# does: in the C library's text mode, so on Windows each line it keeps ends
# in "\r\n" there, as every line the game writes there does.
if sys.platform == "win32":
    kept_text = kept_text.replace("\n", "\r\n")
beside = {"remap.cfg": "pad_a=b\n", "developer.cfg": "RECOMP_GL_PROFILE=1\nmission_skater=3\n",
          "start-states.txt": "1 2 3\n", "recordings/run.rec": "steps\n"}
# case: (settings file, or None; the environment's RECOMP_RESET_SETTINGS,
# or None; the settings file after, or None for none; the files beside it
# after; whether the log says it reset)
cases = {
    "reset": (settings_text, "1", kept_text,
              ["start-states.txt", "recordings/run.rec"], True),
    "reset_crlf": ("msaa=8\r\ngame_dir=C:\\Games\\JSRF\r\nhudscale=1.5\r\n", "1", "game_dir=C:\\Games\\JSRF\r\n",
                   ["start-states.txt", "recordings/run.rec"], True),
    "reset_nothing_kept": ("msaa=8\n", "1", "", ["start-states.txt", "recordings/run.rec"], True),
    "reset_no_settings": (None, "1", None, ["start-states.txt", "recordings/run.rec"], True),
    "not_told": (settings_text, None, settings_text, list(beside), False),
    "after_a_reset": (settings_text, "", settings_text, list(beside), False),
    "told_otherwise": (settings_text, "0", settings_text, list(beside), False),
}

with tempfile.TemporaryDirectory(prefix="settings_reset-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    # -Wno-unused-function: the settings code has helpers only the rest of
    # nv2a_gl.c calls. setenv is POSIX's.
    command = cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-DNV2A_GL_USE_SDL=1", "-x", "c", "-", "-o", exe]
    if sys.platform != "win32":
        command.insert(1, "-D_POSIX_C_SOURCE=200809L")
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, (before, env_value, after, files_after, says) in cases.items():
        folder = work / name
        folder.mkdir()
        if before is not None:
            with open(folder / "settings", "w", encoding="utf-8", newline="") as f:
                f.write(before)
        for file, text in beside.items():
            (folder / file).parent.mkdir(exist_ok=True)
            (folder / file).write_text(text, encoding="utf-8")
        env = {k: v for k, v in os.environ.items() if k != told}
        env["JSRF_SETTINGS_FILE"] = str(folder / "settings")
        if env_value is not None:
            env[told] = env_value
        run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=10)
        if run.returncode != 0:
            sys.exit(f"settings_reset: {name}: exit {run.returncode}\n{run.stderr}")
        if "after: RECOMP_RESET_SETTINGS=" not in run.stderr or "after: RECOMP_RESET_SETTINGS=1" in run.stderr:
            sys.exit(f"settings_reset: {name}: the next start would be told to reset\n{run.stderr}")
        if ("[WIN] settings reset to their defaults" in run.stderr) != says:
            sys.exit(f"settings_reset: {name}: the log {'lacks' if says else 'has'} the reset\n{run.stderr}")
        settings_file = folder / "settings"
        got = None
        if settings_file.exists():
            # As written, line ends and all (Path.read_text takes no newline before Python 3.13).
            with open(settings_file, encoding="utf-8", newline="") as f:
                got = f.read()
        if got != after:
            sys.exit(f"settings_reset: {name}: the settings file is {got!r}, not {after!r}")
        left = sorted(p.relative_to(folder).as_posix() for p in folder.rglob("*")
                      if p.is_file() and p.name != "settings")
        if left != sorted(files_after):
            sys.exit(f"settings_reset: {name}: the files beside the settings are {left}, not {sorted(files_after)}")
print("settings_reset: ok")
