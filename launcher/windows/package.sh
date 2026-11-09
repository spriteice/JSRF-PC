#!/bin/bash
#
# Package the Windows build: "JSRF-PC.exe" (the launcher, built here
# with the same MinGW toolchain) beside engine\jsrf_recomp.exe and the DLLs
# it needs, zipped.
#
#   launcher/windows/package.sh <jsrf_recomp.exe> <llvm-mingw dir> <deps dir> <version> [out dir]
#
# <deps dir> is deps.sh's: SDL2.dll and libepoxy-0.dll from its bin, and the
# SDL2 the launcher links in (launcher/build_launcher.sh) from its include
# and lib; libwinpthread-1.dll comes from the toolchain. Licence texts are
# taken from LICENCES_DIR when it is set (SDL2-COPYING.txt,
# libepoxy-COPYING.txt, winpthreads-COPYING.txt).
#
# The release is built by launcher/windows/build.sh, which makes the
# toolchain and the libraries with deps.sh, builds the engine, then runs this;
# nothing here needs Windows. Every text file in the package ends its lines
# with CR LF once, whatever its source used (SDL's COPYING.txt is already
# CR LF); launcher/check_package.py checks.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
engine="${1:?the jsrf_recomp.exe to package}"
tc="${2:?the llvm-mingw toolchain directory}"
deps="${3:?the deps folder deps.sh made}"
version="${4:?the version}"
out="${5:-$PWD}"
name=JSRF-PC-windows-x86_64
stage="$(mktemp -d)"
dir="$stage/$name"
trap 'rm -rf "$stage"' EXIT

mkdir -p "$dir/engine" "$dir/licences"

# The launcher, with the setup inside, the icon, the manifest and the
# version information.
"$repo/launcher/build_launcher.sh" windows "$dir/JSRF-PC.exe" "$stage/obj" "$tc" "$deps" "$version"

# The engine and what it loads.
install -m 755 "$engine" "$dir/engine/jsrf_recomp.exe"
"$tc/bin/llvm-strip" --strip-debug "$dir/engine/jsrf_recomp.exe"
install -m 644 "$deps/bin/SDL2.dll" "$deps/bin/libepoxy-0.dll" "$dir/engine/"
install -m 644 "$tc/x86_64-w64-mingw32/bin/libwinpthread-1.dll" "$dir/engine/"
"$tc/bin/llvm-strip" --strip-debug "$dir/engine/libepoxy-0.dll" "$dir/engine/libwinpthread-1.dll"

# Text files: each line ends with one CR LF, whether it had LF or CR LF.
crlf() { sed 's/\r*$/\r/' "$1" > "$2"; }
printf '%s\r\n' "$version" > "$dir/VERSION"
crlf "$here/README.txt" "$dir/README.txt"
for f in LICENSE NOTICE; do crlf "$repo/$f" "$dir/licences/$f.txt"; done
crlf "$repo/LICENSES/MIT-upstream.txt" "$dir/licences/MIT-upstream.txt"
crlf "$repo/LICENSES/LGPL-2.1.txt" "$dir/licences/LGPL-2.1.txt"
crlf "$repo/LICENSES/imgui-MIT.txt" "$dir/licences/imgui-MIT.txt"
crlf "$repo/third_party/miniz/LICENSE" "$dir/licences/miniz-MIT.txt"
if [ -n "${LICENCES_DIR:-}" ]; then
    for f in SDL2-COPYING.txt libepoxy-COPYING.txt winpthreads-COPYING.txt; do
        [ -f "$LICENCES_DIR/$f" ] && crlf "$LICENCES_DIR/$f" "$dir/licences/$f"
    done
fi

echo "launcher needs:"; "$tc/bin/llvm-objdump" -p "$dir/JSRF-PC.exe" | sed -n 's/^ *DLL Name: */  /p' | grep -v '^  api-ms-win-crt' | tr '\n' ' '; echo
echo "engine needs:";   "$tc/bin/llvm-objdump" -p "$dir/engine/jsrf_recomp.exe" | sed -n 's/^ *DLL Name: */  /p' | grep -v '^  api-ms-win-crt' | tr '\n' ' '; echo

python3 - "$stage" "$name" "$out/$name.zip" <<'PY'
import os, sys, zipfile
stage, name, dest = sys.argv[1:4]
with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(os.path.join(stage, name)):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, stage))
print("packaged:", dest, os.path.getsize(dest), "bytes")
PY
