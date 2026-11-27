"""Compile JSRF-PC Tools' save core (launcher/tools/jsrf_save.c, with
src/kernel/kernel_crypto.c's HMAC-SHA1 through kernel_crypto_host.c) and its
sources (save_sources.c, xbox_disk.c), with launcher/setup/setup_core.c's
file helpers, and check them (test_main.c): the zero key's signatures
against Python's hmac, Cxbx-Reloaded's key from a made-up XBE, the folder
names XAPI gives saves, SaveMeta.xbx, a save written only where none is or
swapped in whole when asked, the title's own files, the dated backup, saves
found in Cxbx-Reloaded's and xbased's folders, xemu.toml's hdd_path, and the
saves in a qcow2 image of xemu's (tests/xbox_disk's builder). No game data:
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
ours = ["tests/save_import/test_main.c", "launcher/tools/jsrf_save.c", "launcher/tools/save_sources.c", "launcher/tools/save_import.c",
        "launcher/tools/xbox_disk.c", "launcher/tools/kernel_crypto_host.c", "src/kernel/kernel_crypto.c",
        "launcher/setup/setup_core.c", "port/xiso.c", "port/xbe_check.c"]
libs = ["-lwinhttp", "-lshell32", "-lole32"] if os.name == "nt" else ["-ldl"]

with tempfile.TemporaryDirectory(prefix="save_import-") as directory:
    out = pathlib.Path(directory)
    exe = str(out / "save_import.exe")
    miniz = out / "miniz.o"
    m = subprocess.run(cc + ["-O1", "-w", "-c", str(root / "third_party/miniz/miniz.c"), "-o", str(miniz)],
                       capture_output=True, text=True)   # its #pragma message, every time
    if m.returncode:
        sys.exit("save_import: miniz.c didn't compile\n" + m.stdout + m.stderr)
    objs = []
    for i, src in enumerate(ours):
        obj = str(out / f"{i}.o")
        # kernel_crypto.c with the engine's headers; the rest as the tool builds them.
        inc = ["-Isrc", "-Isrc/platform", "-Isrc/kernel", "-Ilauncher/tools"] if "kernel" in src else \
              ["-Ilauncher/tools", "-Ilauncher/setup", "-Iport", "-Itests/xbox_disk"]
        subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *inc, "-c", src, "-o", obj],
                       cwd=root, check=True)
        objs.append(obj)
    subprocess.run(cc + [*objs, str(miniz), *libs, "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe, directory], capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        sys.exit("save_import: no end in 300 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "save_import: all cases pass" not in text:
        sys.exit(f"save_import: exit {run.returncode}")
