"""Compile photo mode (port/photo_mode.c) and drive it without the game.

Builds port/photo_mode.c and port/freecam.c whole, as the engine builds them
with its window (NV2A_GL_USE_SDL); test_main.c stands in for the pads, the
HUD's hiding (nv2a_gl.c's nv2a_hud_hold), the settings file's numbers, the
window's keys and mouse look, the controller layout's hotkey and input
playback's hooks, and drives photo mode as recomp_manual.c's hooks do once
a game frame, with a game camera looking along +z. Each case is its own run.

The start: JSRF_START_PHOTO=1 ticks it, anything else leaves it off and
says so, and either way it is taken out of the environment. The wait: never
while unticked; ticked, the hold is asked for once the follow camera has
followed the player for PHOTO_SETTLE_FRAMES frames in a row with none of
the game's holds on, and a hold (the pause menu's or another), or a frame
with no control (a scene), starts the wait again; then the HUD is hidden,
once. The flight, from the game's view on the first draw: the left stick
forward, back and sideways along the view (forward tilted goes up too,
sideways stays level), the R and L triggers straight up and down, R1 (the
game's White) four times as fast, L1 (Black) a quarter, both one, A and X
nothing, the look point PHOTO_LOOK_AHEAD ahead. The
look, by Input > Free camera's settings (freecam.c's freecam_settings,
freecam_turn and freecam_deadzone): the right stick turns right and tilts
up at the sensitivity's steps, both inverted with Invert X and Y, and at
sensitivity 10; nothing inside the deadzone, and past it scaled from its
edge; the tilt stops at 89 degrees either way, the look point still more
than PHOTO_VIEW_LEVEL across there; the mouse turns and tilts by
Mouse sensitivity's steps with Mouse look on, asked of the window only
while held, a move from before the hold dropped, and nothing with it off.
Y goes back to where photo mode started, once a press. The map's portals,
made from a quad's corners as the game makes them: crossed against the
face, the eye moves by the offset, through and back by a pair, and Y after
a crossing goes back to the start; beside the quad, or crossed the other
way, nothing; once a frame at most, the first crossed, one with no offset
moving nothing; and each hold says how many the map has. Unticked: the hold
let go, the HUD back, the camera left to the game, the game's view given
back once; ticked again, held again. The game letting go of the hold: taken
again every frame, said at most once a second. Input playback: never while
it records or plays, let go when it starts. The areas: from the last point
the eye was in one, or from the game's eye at the start, again at each new
hold. The pad: the game's emptied only while held, its packet kept; and
another camera's draw is left alone.
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

# case: (JSRF_START_PHOTO or None, lines the log must have, each with the
# times it must be there)
START_OFF = "[PHOTO] photo mode off at start\n"
TAKEN = "the game let go of the hold; taking it again"
HELD = "(the player has control)"
PORTALS = "the map has {} portal{}\n"
THROUGH = "through portal {} of {}, the camera moved by ({})\n"
cases = {
    "off": (None, {START_OFF: 1}),
    "start": ("1", {"[PHOTO] photo mode ticked at start, carried over a pick in Developer > Missions\n": 1}),
    "start_bad": ("yes", {"[PHOTO] JSRF_START_PHOTO=yes isn't 1; photo mode off\n": 1, START_OFF: 1}),
    "wait": (None, {HELD: 1, "[PHOTO] Developer > Missions > Photo mode ticked\n": 1}),
    "fly": (None, {"from the game's view at (0.0 5.0 -10.0), heading 0, pitch 0": 1}),
    "turn": (None, {}),
    "invert": (None, {"invert x 1 y 1": 1}),
    "sensitivity": (None, {"sensitivity x 10 y 10": 1}),
    "deadzone": (None, {"deadzone 0.5": 1}),
    "pitch": (None, {}),
    "portal": (None, {PORTALS.format(2, "s"): 1, THROUGH.format(1, 2, "100.0 0.0 50.0"): 2,
                      THROUGH.format(2, 2, "-100.0 0.0 -50.0"): 1}),
    "portal_once": (None, {PORTALS.format(3, "s"): 1, "through portal 1 of 3": 0,
                           THROUGH.format(1, 2, "100.0 0.0 50.0"): 1, "through portal 2 of 2": 0}),
    "back": (None, {"Y, back to where photo mode started": 1}),
    "untick": (None, {"(unticked: the world goes on)": 1, HELD: 2}),
    "retake": (None, {TAKEN: 2}),
    "playback": (None, {"(input playback started: the world goes on)": 1}),
    "area": (None, {"is in no area": 1}),
    "mouse": (None, {}),
    "mouse_off": (None, {"mouse look off": 1}),
    "pad": (None, {}),
}

failed = 0
with tempfile.TemporaryDirectory(prefix="photo_mode-") as directory:
    exe = str(pathlib.Path(directory) / "photo_mode")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DNV2A_GL_USE_SDL",
                    "-I", "port", "-I", "src", "-I", "src/input",
                    "port/photo_mode.c", "port/freecam.c", "tests/photo_mode/test_main.c",
                    "-lm", "-o", exe], cwd=root, check=True)
    for case, (start, lines) in cases.items():
        env = {k: v for k, v in os.environ.items() if not k.startswith(("JSRF_", "RECOMP_"))}
        if start is not None:
            env["JSRF_START_PHOTO"] = start
        run = subprocess.run([exe, case], capture_output=True, text=True, timeout=60, env=env)
        problems = [] if run.returncode == 0 else [f"exit {run.returncode}"]
        problems += [line for line in run.stdout.splitlines() if line.startswith("FAIL")]
        if f"photo_mode {case}: pass" not in run.stdout:
            problems.append("no pass line")
        for line, times in lines.items():
            if run.stderr.count(line) != times:
                problems.append(f"{line.strip()!r} {run.stderr.count(line)} times in the log, not {times}")
        # Each hold says how many portals the map has: none but in the
        # portal cases.
        portal_lines = run.stderr.count(": the map has ")
        if portal_lines != run.stderr.count(HELD):
            problems.append(f"the map's portals said {portal_lines} times, for {run.stderr.count(HELD)} holds")
        if not case.startswith("portal") and portal_lines != run.stderr.count(PORTALS.format(0, "s")):
            problems.append("portals in a case without any")
        print(f"{case}: {'ok' if not problems else 'FAILED'}")
        for problem in problems:
            print(f"  {problem}")
        if problems:
            print(run.stderr, end="")
        failed += bool(problems)
if failed:
    sys.exit(f"photo_mode: {failed} case(s) failed")
print(f"photo_mode: all {len(cases)} cases pass")
