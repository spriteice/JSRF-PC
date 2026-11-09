#!/bin/bash
#
# The Windows release package, start to finish, cross-compiled on Linux: the
# toolchain and libraries (deps.sh), the engine, then package.sh.
#
#   launcher/windows/build.sh <work dir> <version> [CMake options for the engine]
#
# In the build image (launcher/image.sh windows). The engine is given the
# version, as the Linux one takes it from git (docs/versioning.md). The
# generated code is the repository's port/recomp/gen, or the folder named by
# -DJSRF_GEN_DIR=. The package goes to <work dir>/out, and
# launcher/check_package.py checks it.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="$(mkdir -p "${1:?the work directory}" && cd "$1" && pwd)"
version="${2:?the version}"
shift 2

"$here/deps.sh" "$work"
rm -rf "$work/build-win" "$work/out"
cmake -S "$repo/port" -B "$work/build-win" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/llvm-mingw.cmake" \
    -DLLVM_MINGW="$work/llvm-mingw" -DWIN_DEPS="$work/deps" \
    -DCMAKE_BUILD_TYPE=Release -DJSRF_VERSION="$version" "$@"
ninja -C "$work/build-win" jsrf_recomp
mkdir -p "$work/out"
LICENCES_DIR="$work/licences" "$here/package.sh" "$work/build-win/jsrf_recomp.exe" \
    "$work/llvm-mingw" "$work/deps" "$version" "$work/out"
OBJDUMP="$work/llvm-mingw/bin/llvm-objdump" \
    python3 "$repo/launcher/check_package.py" "$work/out/JSRF-PC-windows-x86_64.zip"
