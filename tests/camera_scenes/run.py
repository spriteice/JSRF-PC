"""Compile the game's own camera shots (port/camera_scenes.c) and drive them
without the game.

Builds port/camera_scenes.c whole, as the engine builds it (with the
settings file and the in-game menu); test_main.c stands in for the
settings file, the menu's xbox_MenuAdd and camera_walls.h's two settings. camscene_shot: of the modes the
follow camera's post-update serves (3, 4, 5, 9 and 13), only graffiti's
(13) is one of the game's own shots, left as the game frames it. The ticks
(Input > Behaviour > Camera): each on with no key, off with 0 and on with
1, on with a value it doesn't know (and the log says so), each said in the
log; and their row, after Input's Behaviour submenu (with its hint) and in
it: Default cam fix and No skater fade, camera_walls.h's, then Graffiti
cam, Half pipe cam and Loop cam, a hint the strip can hold for the row and
for each tick, set by File > Presets' Xbox mode (the first two off, every
shot on) and left by JSRF-PC default and Max quality, the first two ticks
flipping camera_walls.h's settings, each shot's flipping at once and
alone, kept as its own key, said in the log, a choice out of range
changing nothing. camscene_asked: the graffiti camera, the half pipe's mode
(6), and a fixed shot (+0xEC8) with the level exit's camera point (+0xF04)
not set, whatever the mode, a grind's or none; a fixed shot with that point
set is the level exit's, which no tick reads, and follow modes are none.
camscene_hidden: a shot is hidden exactly while its tick is off, for each
of the ticks' eight states, the player's or a recording's in their place,
and the level exit's never. camscene_ticks_before, for a recording made
before the ticks: the half pipe's and the fixed shots played, and the
graffiti shot only with the free camera off, whatever the player's ticks.
camscene_rise: Half pipe cam unticked, the camera target through the half
pipe's jump as much higher as the skater, and let go smoothly after.
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

SETTINGS_LINE = "[CAMSCENE] Graffiti cam {}, Half pipe cam {}, Loop cam {} (settings)"


def onoff(keys):
    """The start's log line for the keys as test_main.c takes them."""
    return SETTINGS_LINE.format(*("off" if k == "0" else "on" for k in keys))


# Each run: the case, the settings file's keys (test_main.c), and the
# [CAMSCENE] lines it must say, in their order.
runs = [("modes", None, []), ("rise", None, [])]
for keys in ("---", "000", "111", "100", "010", "001", "110", "011", "101"):
    runs.append(("keys", keys, [onoff(keys)]))
    runs.append(("hidden", keys, [onoff(keys)]))
runs += [
    ("keys", "x1-", ["[CAMSCENE] settings: graffiti_cam is not 0 or 1; using 1", onoff("11-")]),
    ("keys", "-0x", ["[CAMSCENE] settings: loop_cam is not 0 or 1; using 1", onoff("-01")]),
    ("asked", None, [onoff("---")]),
    ("before", "000", [onoff("000")]),
    ("pick", None, [onoff("---"), "[CAMSCENE] Graffiti cam off (the menu)",
                    "[CAMSCENE] Half pipe cam off (the menu)", "[CAMSCENE] Loop cam off (the menu)",
                    "[CAMSCENE] Half pipe cam on (the menu)"]),
    ("pick", "010", [onoff("010"), "[CAMSCENE] Graffiti cam on (the menu)",
                     "[CAMSCENE] Half pipe cam off (the menu)", "[CAMSCENE] Loop cam on (the menu)",
                     "[CAMSCENE] Half pipe cam on (the menu)"]),
]

with tempfile.TemporaryDirectory(prefix="camera_scenes-") as directory:
    exe = str(pathlib.Path(directory) / "camera_scenes")
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-DNV2A_GL_USE_SDL=1",
                         "-DXBOXRECOMP_MENU=1", "-Iport", "-Isrc/menu",
                         "port/camera_scenes.c", "tests/camera_scenes/test_main.c", "-o", exe, "-lm"],
                   cwd=root, check=True)
    for name, keys, lines in runs:
        args = [exe, name] + ([keys] if keys else [])
        run = subprocess.run(args, capture_output=True, text=True, timeout=10)
        what = name + (" " + keys if keys else "")
        if run.returncode != 0 or f"ok {name}" not in run.stdout:
            raise SystemExit(f"{what}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        log = [line for line in run.stderr.splitlines() if line.startswith("[CAMSCENE]")]
        if log != lines:
            raise SystemExit(f"{what}: the log said {log}, not {lines}")
        print(f"ok {what}")
print("camera_scenes: all", len(runs), "runs pass")
