"""Compile Keyboard moves (port/keyboard_move.c) and drive it without the game.

Builds port/keyboard_move.c whole, as the engine builds it (with the
settings file and the in-game menu); test_main.c stands in for the settings
file, the menu's xbox_MenuAdd and the input layer's movement keys.

The steering (kbmove_stick), with the camera looking along +z and along +x,
and the skater facing each way round: W straight on, A and D a quarter turn
to their side, W with A or D an eighth, all re-read as the skater turns; S
the way behind the skater as it went down, kept while the keys stay, and
taken again once they change; a camera with no look on the ground gives no
stick. Each pad read (kbmove_pad): the keys steer only with Skater chosen,
a key held and a view from the camera update, and Camera leaves the stick
as the keys made it. The port (kbmove_game_port): the main player's
manager names the game's pad 4, which is no port, so its view goes to the
port the game keeps for it; 0 to 3 are ports, and anything else none. The
game's soft centre (kbmove_soft_centre) against its own sums at every angle
it softens, done as the recompiled x87 does them from its two floats, and
its undoing (kbmove_soft_centre_undo): for every way the game can soften
to, an angle the game takes as near it as any can be. The angle for the
player manager's update (kbmove_angle): 0 unless the keys steered at the
last read; W, A, D and W with A, the skater and the camera's heading each
way round, and the camera just off the skater's way, where the soft centre
pulls hardest (the second test build's fault): the game goes the keys' way
from the skater's turn, within one of the game's 0x10000 a turn; S the way
behind the skater as it went down, however it turns; and with the camera
holding a heading, the angle from that heading is the keys' way. The
setting: Camera with no keyboard_moves=, Skater with 1, Camera with a value
it doesn't know (and the log says so), and its row, after Input's
Behaviour submenu (with its hint) and in it, Camera then Skater, with a
hint the strip can hold, at Camera in each of File > Presets' presets,
taking effect at once and kept in the settings.
Requires GCC on PATH, including MinGW on Windows, and POSIX threads there
(winpthreads), whose clock_gettime keyboard_move.c reads.
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

with tempfile.TemporaryDirectory(prefix="keyboard_move-") as directory:
    exe = str(pathlib.Path(directory) / "keyboard_move")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-DXBOXRECOMP_MENU", "-DNV2A_GL_USE_SDL",
                    "-I", "port", "-I", "src", "-I", "src/input", "-I", "src/menu",
                    "port/keyboard_move.c", "tests/keyboard_move/test_main.c",
                    "-lm", "-pthread", "-o", exe], cwd=root, check=True)
    for case in ("default", "skater", "unknown", "steer", "port", "soft", "angle"):
        run = subprocess.run([exe, case], capture_output=True, text=True, timeout=60)
        print(run.stdout + run.stderr, end="")
        if run.returncode != 0 or f"keyboard_move {case}: pass" not in run.stdout:
            sys.exit(f"keyboard_move {case}: exit {run.returncode}")
print("keyboard_move: all cases pass")
