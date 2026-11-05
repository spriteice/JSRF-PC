#!/usr/bin/env python3
"""Fetch llvm-mingw for CI's Windows tests job, on Windows: the release's
toolchain (20241217, UCRT), built to run on Windows.

    python .github/ci/llvm_mingw.py <dir>

Downloads it with fetch.py (tried again after a server's error, and checked
by its SHA-256), unpacks it into <dir>, and prints the folder to put on
PATH, whose gcc.exe (clang, as MinGW's gcc is named) the engine tests
compile with.

A <dir> kept between runs (the PC's Building\\cache\\llvm-mingw-tests) is
used again: a zip there with the SHA-256 isn't downloaded again, and a
copy unpacked from it whole isn't unpacked again. <NAME>.unpacked, written
after the whole unpack, holds the zip's SHA-256; without it, or with
another SHA-256, the folder is unpacked afresh. On GitHub, <dir> is new on
each run, so it is downloaded and unpacked each time.
"""

import hashlib
import os
import shutil
import sys
import zipfile

sys.dont_write_bytecode = True   # nothing left in the checkout
from fetch import fetch  # noqa: E402

NAME = "llvm-mingw-20241217-ucrt-x86_64"
URL = "https://github.com/mstorsjo/llvm-mingw/releases/download/20241217/" + NAME + ".zip"
SHA256 = "f4f3ad8616c4183ce7b0d72df634400945b41ea9816145fc2430df6003455db7"


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main(argv):
    if len(argv) != 1:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    os.makedirs(argv[0], exist_ok=True)
    zip_path = os.path.join(argv[0], NAME + ".zip")
    folder = os.path.join(argv[0], NAME)
    marker = folder + ".unpacked"
    bin_path = os.path.abspath(os.path.join(folder, "bin"))
    if os.path.isfile(marker) and os.path.isdir(bin_path):
        with open(marker, encoding="utf-8") as f:
            if f.read().strip() == SHA256:
                print("llvm_mingw: %s, unpacked whole before from the zip with SHA-256 %s"
                      % (folder, SHA256), file=sys.stderr)
                print(bin_path)
                return 0
    if os.path.isfile(zip_path) and sha256_of(zip_path) == SHA256:
        print("llvm_mingw: %s has the SHA-256 already" % zip_path, file=sys.stderr)
    else:
        try:
            fetch(URL, zip_path, SHA256)
        except ValueError as error:
            print("llvm_mingw: %s" % error, file=sys.stderr)
            return 1
    # A copy that isn't known to be whole goes first, with its marker.
    if os.path.exists(marker):
        os.remove(marker)
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(argv[0])
    with open(marker, "w", encoding="utf-8") as f:
        f.write(SHA256 + "\n")
    print(bin_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
