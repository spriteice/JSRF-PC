"""Compile the actual path and file layers, and drive the partition devices.

Takes src/kernel/kernel_path.c and kernel_file.c as they are; only logging and
the symbolic-link table are stubbed (test_main.c). The game and save folders
are fresh temporary ones. Requires GCC on PATH, including MinGW on Windows.
"""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="partition-images-") as directory:
    work = pathlib.Path(directory)
    (work / "game").mkdir()
    (work / "save").mkdir()
    exe = str(work / "test.exe")
    command = ["gcc", "-std=gnu11", "-Isrc", "-Isrc/platform",
               "tests/partition_images/test_main.c",
               "src/kernel/kernel_path.c", "src/kernel/kernel_file.c", "-o", exe]
    if sys.platform == "win32":
        command += ["-lshell32"]
    else:
        command += ["src/platform/win32_compat.c", "-lm", "-lpthread"]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([exe, str(work / "game"), str(work / "save")], check=True, timeout=30)
