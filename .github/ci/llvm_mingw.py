#!/usr/bin/env python3
"""Fetch llvm-mingw for CI's Windows tests job, on Windows: the release's
toolchain (20241217, UCRT), built to run on Windows.

    python .github/ci/llvm_mingw.py <dir>

Checks the download by its SHA-256, unpacks it into <dir>, and prints the
folder to put on PATH, whose gcc.exe (clang, as MinGW's gcc is named) the
engine tests compile with.
"""

import hashlib
import os
import sys
import urllib.request
import zipfile

NAME = "llvm-mingw-20241217-ucrt-x86_64"
URL = "https://github.com/mstorsjo/llvm-mingw/releases/download/20241217/" + NAME + ".zip"
SHA256 = "f4f3ad8616c4183ce7b0d72df634400945b41ea9816145fc2430df6003455db7"


def main(argv):
    if len(argv) != 1:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    os.makedirs(argv[0], exist_ok=True)
    zip_path = os.path.join(argv[0], NAME + ".zip")
    urllib.request.urlretrieve(URL, zip_path)
    with open(zip_path, "rb") as f:
        got = hashlib.sha256(f.read()).hexdigest()
    if got != SHA256:
        print("llvm_mingw: %s has SHA-256 %s, not %s" % (zip_path, got, SHA256), file=sys.stderr)
        return 1
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(argv[0])
    print(os.path.abspath(os.path.join(argv[0], NAME, "bin")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
