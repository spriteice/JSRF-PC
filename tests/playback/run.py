"""Compile input playback (port/playback.c) with the pad (port/pad.c) and
the free camera (port/freecam.c), and drive them with a stand-in game,
without the game.

Builds port/playback.c, port/pad.c and port/freecam.c whole, as the engine
builds them, with the in-game menu's rows; test_main.c stands in for the
input layer (src/input), the keyboard's moves, photo mode,
recomp_manual.c's test hooks, the settings file, the menu and the window,
and plays the game: a
loop of passes, each reading the pad twice through XInputGetState's
replacement, running the free camera once a frame and then around the
skater as the follow camera does, but in the cutscene, moving the skater by
what it read the way the view faces, drawing random numbers a number of
times that follows the host rather than the passes, reaching six anchors
(missions, a stage, a cutscene's start and end), and telling playback
where the skater is, but in the cutscene, as the camera update does.

Recorded, then played back with nobody at the pad: the game reads what it
read, pass for pass and read for read; the generator is put back at each
anchor; the idle moment is found and the skater put back there; it ends on
route, and --play's run closes the game. An anchor reached 30 frames late
holds the pad as recorded until it comes and moves the presses after it, not
before; one reached 20 frames early skips to it. Off route: the skater
pushed 100 units off, a stage the recording doesn't have next, and an
anchor never reached. A restart with nothing running says nothing; one
while recording ends the recording, kept, and one while playing stops it,
with its summary. The free camera a play puts in place is in the log
with auto centre's level by name, as [FREECAM] gives it. A real pad held
from the start does nothing, and
pressed once let go of, hands the game back. With the free camera: turned
on by F8, steered by the right stick and the mouse, its sensitivity, invert,
auto centre and mouse sensitivity changed in the menu, off by R3 and on by
F8, recorded with the right stick as the pad gave it and the mouse's moves
as it turned by them, none while it was off; played with the player's free
camera off and set otherwise, Mouse look off among them, the game reads
what it read, the eye and the skater go where they went, and the player's
are back after, the settings file untouched; the real right stick while
the free camera steers, F8, and the player's mouse, hand the game back. A
recording written by hand plays, with the player's free camera until its
first freecam line and the recording's from there, a freecam line without
the mouse's two among them, and its mouse's moves past an early anchor
taken at once; one that isn't a recording, goes back in time, has a line
after its end, a word it doesn't know, or a freecam or mouse line out of
its range or short of a value is refused, saying where. Input > Behaviour >
Camera's ticks, as the camera update reads them: recorded at the first and
when the player changes one, and played with the player's in place
otherwise; by hand, the player's before the first camera line and the
recording's from there, and with none the shots as before the ticks; the
player's change while it plays hands the game back; a camera line out of
its range, short of a tick or back in time is refused.
As the engine starts: JSRF_RECORD, JSRF_PLAY and JSRF_PLAY_MENU taken out
of the environment, a recording made beside the settings file, and
Developer > Playback's rows, each with its hint, Play's choices newest
first, Record and Play restarting with their variable and leaving none
behind.
Requires GCC on PATH, including MinGW on Windows, and POSIX threads there
(winpthreads), whose clock_gettime pad.c reads.
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

with tempfile.TemporaryDirectory(prefix="playback-") as directory:
    exe = str(pathlib.Path(directory) / "playback")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-DXBOXRECOMP_MENU", "-DNV2A_GL_USE_SDL",
                    "-I", "port", "-I", "src", "-I", "src/input", "-I", "src/menu",
                    "port/playback.c", "port/pad.c", "port/freecam.c", "tests/playback/test_main.c",
                    "-pthread", "-lm", "-o", exe], cwd=root, check=True)
    work = pathlib.Path(directory) / "work"
    work.mkdir()
    run = subprocess.run([exe, str(work)], capture_output=True, text=True, timeout=120)
    print(run.stdout, end="")
    # What the log says, for a run's reader: the refusals by line, the
    # anchors met late and early, and the summaries.
    log = run.stderr
    said = ["line 2: can't be read, or goes back in time",
            "line 3: can't be read, or goes back in time",
            "line 3: a line after the end",
            "not a recording (no \"jsrf-pc-recording 1\" first)",
            "frames late", "frames early", "summary: finished on route",
            "summary: off route", "summary: handed back",
            "idle moment 1: the skater put back",
            "the free camera as recorded at frame",
            "auto centre Off, mouse look on at 5",
            "auto centre High, mouse look on at 7",
            "Stop recording: nothing is being recorded",
            "recording ended at frame 40 (the game restarted)",
            "summary: stopped: the game restarted at frame 25;",
            "the free camera was changed (F8, the pad or the menu): the game is yours",
            "the mouse was moved: the game is yours",
            "(yours again, as playback ended)",
            "JSRF_RECORD left out: a recording is played, not made",
            "the camera ticks as recorded at frame",
            "no camera line: the game's own shots as before the Camera ticks",
            "the camera ticks were changed (the menu): the game is yours"]
    missing = [s for s in said if s not in log]
    # The free camera a play puts in place names auto centre's level, as
    # [FREECAM] does, never by its number.
    named = re.findall(r"the free camera as recorded at frame \d+: .*auto centre (\S+),", log)
    if not named or any(n not in ("Off", "Mild", "Medium", "High", "Aggressive") for n in named):
        missing.append(f"auto centre named in each free camera line played (it says {sorted(set(named))})")
    # A restart with nothing running says nothing.
    quiet = "<restart, nothing running>\n</restart>\n" in log
    if not quiet:
        missing.append("nothing between <restart, nothing running> and </restart>")
    if run.returncode != 0 or "playback: all cases pass" not in run.stdout or missing:
        print(log[-6000:])
        sys.exit(f"playback: exit {run.returncode}" + (f", the log doesn't say {missing}" if missing else ""))
