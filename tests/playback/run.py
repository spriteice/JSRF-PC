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
anchor; each check keeps the generator and its draws since the check
before, on the game's thread and on others, and on playback, drawn a number
of times that follows the host and once every seventh frame on another
thread, it is said where they weren't as recorded and put back at each
check and at the idle moment; the idle moment is found and the skater put
back there; it ends on route, and --play's run closes the game. An anchor reached 30 frames late
holds the pad as recorded until it comes and moves the presses after it, not
before; one reached 20 frames early skips to it. Off route: the skater
pushed 100 units off, a stage the recording doesn't have next, and an
anchor never reached. A restart with nothing running says nothing; one
while recording ends the recording, kept, and one while playing stops it,
with its summary. A run started from Developer > Missions: its recording
starts with a line saying where, before its first frame, and Record is
ticked from then; about to be played, it puts that start in Missions' and
photo mode's variables, and plays on route there, at a mission and at Load
Game; started elsewhere, with other progress, or from the game's own start,
it is off route at its first frame, and so is a recording from the game's
own start played in a run from a mission; a start line that can't be read,
comes twice or after a frame is refused. Checks by hand: one without the
generator leaves it as it is, one with it puts it back; a check with the
generator but no draws, and an idle moment with draws, are refused. A run's folder: --play's with a
profile/ copied into a folder of the play's own in its plays/, steps.txt's
setting and developer lines written into the copy, the settings file and
both save folders pointed there and the player's untouched; played with the
window flipping once a pass, its menu rows looked for at the first flip,
each step at its moment (a mark at a frame, a free camera row, a screenshot
and a mark at the idle moment, which hold the recording seven frames, the
row's change not the player's, and a screenshot at a frame), on route to its
end, the screenshots in the play's folder. A step that isn't one, names an
idle moment or a frame the recording doesn't have, comes before the one
above it, has nothing after it, or is a menu row or a setting with no
profile/ copied, is refused before anything is played; a menu row that
isn't there stops the play at the first flip, and steps never run (no
window) stop it once their wait runs out. Play from the menu leaves a
profile/ out, and runs marks and screenshots. The free camera a play puts in
place is in the log with auto centre's level by name, as [FREECAM] gives
it. A real pad held from the start does nothing, and
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
The settings that change how a run plays (Default cam fix, No skater fade,
the skips, the draw distances): recorded at the first and when the player
changes one, used for the play only, said where the player's differs, and
the player's back as it ends, the settings file untouched. The free
camera's exact heading and pitch as it takes over and at each check:
played, said where they weren't as recorded and put back; turned 0.01 off
at frame 130, the trace from the first press finds it at the next frame
and the check after puts it back. The mark a recording plays: in at its
first draw, green, red or blue as the play ends, held, out and gone.
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
                    "port/playback.c", "port/playback_mark.c", "port/pad.c", "port/freecam.c",
                    "tests/playback/test_main.c",
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
            "this run started at mission 0252, skater 6, progress 1A2B3C4D, photo mode (Developer > Missions):"
            " so will its plays",
            "the recording to play started at mission 0252, skater 6, progress 1A2B3C4D, photo mode: the game"
            " starts there, as Developer > Missions starts it",
            "the recording to play started at Load Game: the game starts there",
            "JSRF_START_MISSION or JSRF_START_LOAD replaced by the recording's start",
            "frame 1: off route: the recording started at mission 0252",
            "a start line that can't be read, comes twice or after a frame",
            "Developer > Missions > Record unticked",
            "the free camera was changed (F8, the pad or the menu): the game is yours",
            "the mouse was moved: the game is yours",
            "(yours again, as playback ended)",
            "JSRF_RECORD left out: a recording is played, not made",
            "the camera ticks as recorded at frame",
            "no camera line: the game's own shots as before the Camera ticks",
            "the camera ticks were changed (the menu): the game is yours",
            "on the game's thread since the check before (recorded ", "on others (recorded ",
            "where they return to: 00015530 ", "00130BD1 0+9", "put back as recorded",
            "(no more said: the summary counts them)",
            "the game's random numbers at the checks: as recorded at ", ", first not at recorded frame ",
            "for this play, in ", "yours aren't opened",
            "steps.txt, line 2: mute=1 in the copy's settings.cfg",
            "steps.txt, line 3: RECOMP_GL_PROFILE=1 in the copy's developer.cfg",
            "steps.txt: 5 steps, 1 of them menu rows, 2 screenshots, into ",
            "each menu row in steps.txt is there",
            "mark: before the idle moment (steps.txt, line 4)",
            "menu Input > Free camera > Invert Y (steps.txt, line 5)",
            "/shot-6.bmp (steps.txt, line 6)",
            "mark: at the idle moment (steps.txt, line 7)",
            "/shot-8.bmp (steps.txt, line 8)",
            "steps run 5 of 5 (steps.txt)",
            "steps.txt, line 1: not a step; nothing is played",
            "steps.txt, line 1: the recording has no such idle moment; nothing is played",
            "steps.txt, line 1: past the recording's end; nothing is played",
            "steps.txt, line 2: comes before the step above it in the recording; nothing is played",
            "steps.txt, line 1: a menu row changes settings: it needs a copy of the run's profile/, which only"
            " --play makes; nothing is played",
            "steps.txt, line 1: a setting goes into a copy of the run's profile/, which only --play makes;"
            " nothing is played",
            "steps.txt, line 1: not key=value; nothing is played",
            "steps.txt, line 1: menu Graphics > No such row: nothing there has that label: the play stops",
            "steps.txt's menu rows not looked for within 600 frames (no picture shown): the play stops",
            "profile left out: only --play plays on a copy of a run's profile; Play plays on your own settings"
            " and saves",
            "the settings as recorded at frame 1, for this play: ", "more settings as recorded at frame 302",
            "(yours differs)", "(changed)", "your settings back, as the play ends: ",
            "the free camera took over at recorded frame", "the free camera's heading",
            "the free camera taking over: as recorded at ", "the free camera at the checks: as recorded at ",
            "first not at recorded frame 181; put back at each",
            "trace at recorded frame 131: not as recorded",
            "the trace from the first press: as recorded at 240 of 240 frames"]
    missing = [s for s in said if s not in log]
    # The free camera a play puts in place names auto centre's level, as
    # [FREECAM] does, never by its number.
    named = re.findall(r"the free camera as recorded at frame \d+: .*auto centre (\S+),", log)
    if not named or any(n not in ("Off", "Mild", "Medium", "High", "Aggressive") for n in named):
        missing.append(f"auto centre named in each free camera line played (it says {sorted(set(named))})")
    # The idle moment's steps held the recording: the row, five flips for it
    # to show, the screenshot and the mark, so the next anchor came seven
    # frames early.
    held = re.search(r"steps\.txt: 5 steps.*?anchor 4 of 6 met \(scene 00000000\), recorded at frame 401: (\d+)"
                     r" frames early", log, re.S)
    if not held or held.group(1) != "7":
        missing.append(f"the idle moment's steps holding the recording 7 frames (it says {held and held.group(1)})")
    # A restart with nothing running says nothing.
    quiet = "<restart, nothing running>\n</restart>\n" in log
    if not quiet:
        missing.append("nothing between <restart, nothing running> and </restart>")
    if run.returncode != 0 or "playback: all cases pass" not in run.stdout or missing:
        print(log[-6000:])
        sys.exit(f"playback: exit {run.returncode}" + (f", the log doesn't say {missing}" if missing else ""))
