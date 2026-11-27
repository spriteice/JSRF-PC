"""Compile JSRF-PC Tools' graffiti conversion (launcher/tools/graffiti_fit.c,
graffiti_save.c and graffiti_image.c, with the picture libraries in
launcher/tools/graffiti_libs.txt, built as the tool builds them) with the
save core it writes through (jsrf_save.c, src/kernel/kernel_crypto.c's HMAC-SHA1
through kernel_crypto_host.c) and launcher/setup/setup_core.c's file
helpers, and check them (test_main.c says what). Then, apart from the C:
each size's save.dat that test_main.c wrote is checked here against the
layout as Python reads it, and its seven signatures against Python's own
HMAC-SHA1 with the zero key; and the save folders it printed against
Python's own working of XAPI's name hash. No game data: the designs are
made up, the pictures (tests/graffiti/pictures, made by make_pictures.py)
are patterns of a few colours, and the icon is a stand-in file.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import concurrent.futures
import hashlib
import hmac
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
ours = ["tests/graffiti/test_main.c", "launcher/tools/graffiti_fit.c", "launcher/tools/graffiti_save.c",
        "launcher/tools/graffiti_image.c", "launcher/tools/jsrf_save.c", "launcher/tools/kernel_crypto_host.c",
        "src/kernel/kernel_crypto.c", "launcher/setup/setup_core.c", "port/xiso.c", "port/xbe_check.c"]
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

# The blocks after the header and the seven signatures, by size class, as
# docs/formats/graffiti-save.md gives them from the game's table at 0x2285B0:
# the preview, the design, the editor's canvas, the wall texture, the
# stickers and the last 16 bytes.
BLOCKS = {
    "SS": (2048, 32768, 65536, 16384, 720, 16),
    "S": (2048, 32768, 65536, 16384, 720, 16),
    "M": (3072, 262144, 524288, 131072, 720, 16),
    "L": (4096, 262144, 524288, 131072, 720, 16),
    "XL": (8192, 524288, 1048576, 262144, 720, 16),
}
ZERO_KEY = bytes(16)


def folder(name):
    """XAPI's folder for a save's name: v = (v * 65536 + c) mod
    0xFFFFFFFFFFC5 over its UTF-16 units, as 12 upper-case hex digits."""
    v = 0
    for c in name.encode("utf-16-le")[::2]:
        v = (v * 65536 + c) % 0xFFFFFFFFFFC5
    return "%012X" % v


with tempfile.TemporaryDirectory(prefix="graffiti-") as directory:
    out = pathlib.Path(directory)
    exe = str(out / "graffiti.exe")
    miniz = out / "miniz.o"
    m = subprocess.run(cc + ["-O1", "-w", "-c", str(root / "third_party/miniz/miniz.c"), "-o", str(miniz)],
                       capture_output=True, text=True)   # its #pragma message, every time
    if m.returncode:
        sys.exit("graffiti: miniz.c didn't compile\n" + m.stdout + m.stderr)
    jobs = []
    for i, src in enumerate(ours):
        # kernel_crypto.c with the engine's headers; the rest as the tool builds them.
        inc = ["-Isrc", "-Isrc/platform", "-Isrc/kernel", "-Ilauncher/tools"] if "kernel" in src else \
              ["-Ilauncher/tools", "-Ilauncher/setup", "-Iport", *picture_inc]
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

    for size, (klass, blocks) in zip(BLOCKS, enumerate(BLOCKS.values())):
        data = (out / f"save-{size}.dat").read_bytes()
        if len(data) != 16 + 7 * 20 + sum(blocks):
            sys.exit(f"graffiti: {size}'s save.dat is {len(data)} bytes")
        if data[0] != klass or data[4:8] != b"JSRF":
            sys.exit(f"graffiti: {size}'s header is {data[:16].hex()}")
        pieces = [data[:16]]
        at = 16 + 7 * 20
        for n in blocks:
            pieces.append(data[at:at + n])
            at += n
        for i, piece in enumerate(pieces):
            want = hmac.new(ZERO_KEY, piece, hashlib.sha1).digest()
            got = data[16 + 20 * i:36 + 20 * i]
            if got != want:
                sys.exit(f"graffiti: {size}'s signature {i} isn't HMAC-SHA1 with the zero key")
    print("graffiti: every size's seven signatures are HMAC-SHA1 with the zero key")

    shown = re.findall(r"^folder (GRAFFITI\d{3}) ([0-9A-F]{12})$", text, re.M)
    if len(shown) != 7:
        sys.exit(f"graffiti: {len(shown)} folders shown, not 7")
    for name, got in shown:
        if folder(name) != got:
            sys.exit(f"graffiti: {name}'s folder is {got}, not {folder(name)}")
    print("graffiti: the slots' folders are XAPI's hash of their names")
