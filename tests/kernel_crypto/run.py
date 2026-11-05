"""Compile src/kernel/kernel_crypto.c and check the kernel's SHA-1, HMAC-SHA1
and RC4 against known answers (test_main.c): FIPS 180's and RFC 3174's for
SHA-1, RFC 2202's for HMAC-SHA1, and RFC 6229's and the classic three for
RC4, each also fed in pieces, as a title calls them. The game signs and
checks its saves with them. On Linux SecureZeroMemory is
src/platform/win32_compat.c's, on Windows the system's. Requires GCC on PATH,
including MinGW on Windows.
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
sources = ["tests/kernel_crypto/test_main.c", "src/kernel/kernel_crypto.c"]
if os.name == "nt":
    flags = []
else:
    sources.append("src/platform/win32_compat.c")
    flags = ["-pthread"]

with tempfile.TemporaryDirectory(prefix="kernel_crypto-") as directory:
    exe = str(pathlib.Path(directory) / "kernel_crypto.exe")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", *flags,
                         "-Isrc", "-Isrc/platform", "-Isrc/kernel",
                         *sources, "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        sys.exit("kernel_crypto: no end in 120 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "kernel_crypto: all cases pass" not in text:
        sys.exit(f"kernel_crypto: exit {run.returncode}")
