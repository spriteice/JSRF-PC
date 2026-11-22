"""Compile the game's own camera shots (port/camera_scenes.c) and drive them
without the game.

Builds port/camera_scenes.c whole, as the engine builds it, with
test_main.c. camscene_shot: of the modes the follow camera's post-update
serves (3, 4, 5, 9 and 13), only graffiti's (13) is one of the game's own
shots, left as the game frames it.
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

cases = ["modes"]

with tempfile.TemporaryDirectory(prefix="camera_scenes-") as directory:
    exe = str(pathlib.Path(directory) / "camera_scenes")
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Iport",
                         "port/camera_scenes.c", "tests/camera_scenes/test_main.c", "-o", exe],
                   cwd=root, check=True)
    for name in cases:
        run = subprocess.run([exe, name], capture_output=True, text=True, timeout=10)
        if run.returncode != 0:
            raise SystemExit(f"{name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        print(run.stdout.strip())
print("camera_scenes: all", len(cases), "cases pass")
