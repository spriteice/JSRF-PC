"""Compile the follow camera's view kept out of walls (port/camera_walls.c)
and drive it without the game.

Takes port/camera_walls.c as it is, with test_main.c's made-up world of
planes and boxes in place of the game's line test, one case to a run.
camwall_clear: the eye's line, and lines across where the view starts,
against the ground, a room's corner, a wall along the view, a block's edge,
a corridor, a block beside the player, a pole, gaps narrower than the view
and a wall the player is right against. camwall_share and camwall_across:
in, or across, at once; held for 20 frames; back by a tenth of the gap a
frame; and all the way out once the camera starts again (camwall_reset).
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

# case: log lines it must print. camwall_clear's by: 0 nothing, 1 the eye's
# line, 2 coming in, 3 no room, 4 moving across, 5 as near as it goes
cases = {
    "wall": [],
    "across": [],
    "clear_open": ["clear_open: share 1.0000, 0.00 across, by 0, 5 lines"],
    "clear_back": ["clear_back: share 0.6000, 0.00 across, by 1"],
    "clear_floor": ["clear_floor: share 1.0000", "by 4"],
    "clear_room_corner": ["clear_room_corner: share 1.0000", "by 4"],
    "clear_alongside": ["clear_alongside: share 1.0000", "by 4"],
    "clear_block_edge": ["clear_block_edge: share 1.0000", "by 4"],
    "clear_corridor": ["clear_corridor: share 0.58", "by 4"],
    "clear_beside_player": ["clear_beside_player: share 1.0000, 0.00 across, by 0, 5 lines"],
    "clear_pole": ["clear_pole: share 1.0000, 0.00 across, by 0, 5 lines"],
    "clear_gap_behind": ["clear_gap_behind: share 0.", "by 2"],
    "clear_narrow": ["clear_narrow: share 1.0000, 0.00 across, by 3"],
    "clear_hugging": ["clear_hugging: share 0.2", "by 5"],
}

with tempfile.TemporaryDirectory(prefix="camera_walls-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Iport", "port/camera_walls.c",
                         "tests/camera_walls/test_main.c", "-o", exe, "-lm"],
                   cwd=root, check=True)
    for name, logs in cases.items():
        run = subprocess.run([exe, name], capture_output=True, text=True, timeout=10)
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        for line in logs:
            if line not in run.stderr:
                raise SystemExit(f"{name}: no '{line}' in the log:\n{run.stderr}")
        print(run.stdout.strip())
print("camera_walls: all", len(cases), "cases pass")
