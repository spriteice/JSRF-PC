#!/usr/bin/env python3
"""Fetch llvm-mingw for CI's Windows tests job, on Windows: the release's
toolchain (20241217, UCRT), built to run on Windows.

    python .github/ci/llvm_mingw.py <dir>

Downloads it with fetch.py (tried again after a server's error, and checked
by its SHA-256), unpacks it into <dir>, and prints the folder to put on
PATH, whose gcc.exe (clang, as MinGW's gcc is named) the engine tests
compile with.
"""

import os
import sys
import zipfile

sys.dont_write_bytecode = True   # nothing left in the checkout
from fetch import fetch  # noqa: E402

NAME = "llvm-mingw-20241217-ucrt-x86_64"
URL = "https://github.com/mstorsjo/llvm-mingw/releases/download/20241217/" + NAME + ".zip"
SHA256 = "f4f3ad8616c4183ce7b0d72df634400945b41ea9816145fc2430df6003455db7"


def main(argv):
    if len(argv) != 1:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    os.makedirs(argv[0], exist_ok=True)
    zip_path = os.path.join(argv[0], NAME + ".zip")
    try:
        fetch(URL, zip_path, SHA256)
    except ValueError as error:
        print("llvm_mingw: %s" % error, file=sys.stderr)
        return 1
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(argv[0])
    print(os.path.abspath(os.path.join(argv[0], NAME, "bin")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
