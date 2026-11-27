"""Compile launcher/tools/xbox_disk.c and read disk images built by
test_main.c: raw images with a FATX partition of 16-bit and of 32-bit FAT;
qcow2 images, version 3 and 2, of a retail-sized disk with the E: partition
at its retail place, their clusters stored plain, compressed and as zeros;
qcow2 images the reader must refuse (a backing file, encryption, an external
data file, extended L2 entries, zstd, version 4) and one it must not (dirty);
and damaged partitions (a FAT chain that loops, names that would leave a
folder or run past their field, no FATX, a root outside the partition).
A real xemu image is checked on the PC, by the test list.
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

with tempfile.TemporaryDirectory(prefix="xbox_disk-") as directory:
    out = pathlib.Path(directory)
    exe = str(out / "xbox_disk.exe")
    miniz = out / "miniz.o"
    m = subprocess.run(cc + ["-O1", "-w", "-c", str(root / "third_party/miniz/miniz.c"), "-o", str(miniz)],
                       capture_output=True, text=True)   # its #pragma message, every time
    if m.returncode:
        sys.exit("xbox_disk: miniz.c didn't compile\n" + m.stdout + m.stderr)
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Ilauncher/tools",
                         "tests/xbox_disk/test_main.c", "launcher/tools/xbox_disk.c",
                         str(miniz), "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe, directory], capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        sys.exit("xbox_disk: no end in 300 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "xbox_disk: all cases pass" not in text:
        sys.exit(f"xbox_disk: exit {run.returncode}")
