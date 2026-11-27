"""Compile JSRF-PC Tools' save manager (launcher/tools/save_store.c), with
the save core it backs up through (jsrf_save.c) and launcher/setup/
setup_core.c's file helpers, and check it (test_main.c): what a title folder
holds, dates in words, the 8 save slots (saved into, named, listed, loaded
back, cleared), the backups (listed newest first, named, restored,
deleted), a load or delete always after a backup, names kept to one line and
cut at a whole character, saves deleted only by a save folder's name, the
.new and .old a stopped run leaves, and a design's preview. No game data:
the saves are made up.
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
ours = ["tests/save_store/test_main.c", "launcher/tools/save_store.c", "launcher/tools/jsrf_save.c",
        "launcher/tools/kernel_crypto_host.c", "src/kernel/kernel_crypto.c", "launcher/setup/setup_core.c",
        "port/xiso.c", "port/xbe_check.c"]   # setup_core.c's game check, linked though not used here
libs = ["-lwinhttp", "-lshell32", "-lole32"] if os.name == "nt" else ["-ldl"]

with tempfile.TemporaryDirectory(prefix="save_store-") as directory:
    out = pathlib.Path(directory)
    exe = str(out / "save_store.exe")
    miniz = out / "miniz.o"
    m = subprocess.run(cc + ["-O1", "-w", "-c", str(root / "third_party/miniz/miniz.c"), "-o", str(miniz)],
                       capture_output=True, text=True)   # its #pragma message, every time
    if m.returncode:
        sys.exit("save_store: miniz.c didn't compile\n" + m.stdout + m.stderr)
    objs = []
    for i, src in enumerate(ours):
        obj = str(out / f"{i}.o")
        # kernel_crypto.c with the engine's headers; the rest as the tool builds them.
        inc = ["-Isrc", "-Isrc/platform", "-Isrc/kernel", "-Ilauncher/tools"] if "kernel" in src else \
              ["-Ilauncher/tools", "-Ilauncher/setup", "-Iport"]
        subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *inc, "-c", src, "-o", obj],
                       cwd=root, check=True)
        objs.append(obj)
    subprocess.run(cc + [*objs, str(miniz), *libs, "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe, directory], capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        sys.exit("save_store: no end in 300 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "save_store: all cases pass" not in text:
        sys.exit(f"save_store: exit {run.returncode}")
