"""Compile JSRF-PC Tools' picture reading (launcher/tools/graffiti_image.c,
with the picture libraries in launcher/tools/graffiti_libs.txt, built as the
tool builds them) with launcher/setup/setup_core.c's file helpers, and check
them (test_main.c says what). No game data: the pictures
(tests/graffiti/pictures, made by make_pictures.py) are patterns of a few
colours.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import concurrent.futures
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
ours = ["tests/graffiti/test_main.c", "launcher/tools/graffiti_image.c", "launcher/setup/setup_core.c",
        "port/xiso.c", "port/xbe_check.c"]
libs = ["-lwinhttp", "-lshell32", "-lole32"] if os.name == "nt" else ["-ldl", "-pthread"]
system = "windows" if os.name == "nt" else "linux"
# The headers graffiti_image.c reads the libraries through.
picture_inc = ["-Ithird_party/stb", "-Ithird_party/nanosvg", "-Ithird_party/libwebp/src",
               "-Ithird_party/libavif/include", "-Ithird_party/libtiff/libtiff", "-Ilauncher/tools/libconfig/tiff"]


def library_sources():
    """(group, flags, file) for each source in graffiti_libs.txt for this system."""
    group, flags = None, []
    for line in (root / "launcher/tools/graffiti_libs.txt").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("@group "):
            parts = line.split()
            group, flags = parts[1], parts[2:]
            continue
        parts = line.split()
        if len(parts) == 2 and parts[1] != system:
            continue
        yield group, flags, parts[0]


with tempfile.TemporaryDirectory(prefix="graffiti-") as directory:
    out = pathlib.Path(directory)
    exe = str(out / "graffiti.exe")
    miniz = out / "miniz.o"
    m = subprocess.run(cc + ["-O1", "-w", "-c", str(root / "third_party/miniz/miniz.c"), "-o", str(miniz)],
                       capture_output=True, text=True)   # its #pragma message, every time
    if m.returncode:
        sys.exit("graffiti: miniz.c didn't compile\n" + m.stdout + m.stderr)
    jobs = []
    inc = ["-Ilauncher/tools", "-Ilauncher/setup", "-Iport", *picture_inc]
    for i, src in enumerate(ours):
        jobs.append(cc + ["-O2", "-DNDEBUG", "-std=gnu11", "-Wall", "-Wextra", "-Werror", *inc, "-c", src,
                          "-o", str(out / f"ours{i}.o")])
    for i, (group, flags, src) in enumerate(library_sources()):
        jobs.append(cc + ["-O2", "-DNDEBUG", "-w", *flags, "-c", src, "-o", str(out / f"{group}{i}.o")])

    def build(job):
        return subprocess.run(job, cwd=root, capture_output=True, text=True)

    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 2) as pool:
        for job, done in zip(jobs, pool.map(build, jobs)):
            if done.returncode:
                sys.exit(f"graffiti: {job[-3]} didn't compile\n" + done.stdout + done.stderr)
    objs = [str(miniz)] + [job[-1] for job in jobs]
    link = subprocess.run(cc + [*objs, *libs, "-lm", "-o", exe], cwd=root, capture_output=True, text=True)
    if link.returncode:
        sys.exit("graffiti: the test didn't link\n" + link.stdout + link.stderr)
    try:
        run = subprocess.run([exe, directory, str(root / "tests/graffiti/pictures")], capture_output=True,
                             text=True, timeout=300)
    except subprocess.TimeoutExpired:
        sys.exit("graffiti: no end in 300 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "graffiti: all cases pass" not in text:
        sys.exit(f"graffiti: exit {run.returncode}")

