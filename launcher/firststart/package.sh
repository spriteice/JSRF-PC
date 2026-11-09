#!/bin/bash
#
# The player packages without the engine (b2 row 4b), and the tools archives:
# the launcher with the setup inside (launcher/setup), the kit
# (launcher/kit/make_kit.sh), and what the engine runs with, but no engine:
# the player's PC builds it at the first start, from their own game.
#
#   launcher/firststart/package.sh linux   <work dir> <version> <kit dir> <tools dir> <out dir>
#   launcher/firststart/package.sh windows <work dir> <version> <kit dir> <tools dir> <out dir>
#
# In the build image for the system (launcher/image.sh linux, or windows),
# from the repository at /src. <kit dir> is make_kit.sh's for that system;
# <tools dir> holds the three downloads the setup would fetch (named as in
# launcher/setup/setup_core.c), from which the tools archive is made: one
# folder, tools, ready as it is.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
system="${1:?linux or windows}"
work="$(mkdir -p "${2:?the work directory}" && cd "$2" && pwd)"
version="${3:?the version}"
kit="$(cd "${4:?the kit}" && pwd)"
dls="$(cd "${5:?the downloads}" && pwd)"
out="$(mkdir -p "${6:?the out directory}" && cd "$6" && pwd)"
stage="$work/stage"
rm -rf "$stage"; mkdir -p "$stage"

licences() {   # dir
    install -m 644 "$repo/LICENSE" "$1/LICENSE.txt"
    install -m 644 "$repo/NOTICE" "$1/NOTICE.txt"
    install -m 644 "$repo/LICENSES/MIT-upstream.txt" "$1/MIT-upstream.txt"
    install -m 644 "$repo/LICENSES/LGPL-2.1.txt" "$1/LGPL-2.1.txt"
    install -m 644 "$repo/LICENSES/imgui-MIT.txt" "$1/imgui-MIT.txt"
    # miniz ships its licence with CR LF: LF here, as the other texts (the
    # Windows package turns them all to CR LF).
    tr -d '\r' < "$repo/third_party/miniz/LICENSE" > "$1/miniz-MIT.txt"
    chmod 644 "$1/miniz-MIT.txt"
}

kit_in() {   # dir
    mkdir -p "$1"
    cp -a "$kit/." "$1/"
    install -m 644 "$repo/launcher/kit/build_engine.py" "$1/build_engine.py"
}

case "$system" in
linux)
    name=JSRF-PC-linux-x86_64
    dir="$stage/$name"
    sdl="$work/sdl"
    [ -f "$sdl/lib/libSDL2-2.0.so.0" ] || {
        [ -f "$work/SDL2-2.30.12.tar.gz" ] || python3 "$repo/.github/ci/fetch.py" \
            https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz \
            "$work/SDL2-2.30.12.tar.gz" "$(sed -n 's/^sha256=//p' "$repo/launcher/linux/sdl.sh")"
        "$repo/launcher/linux/sdl.sh" "$work/SDL2-2.30.12.tar.gz" "$sdl" > "$work/sdl.log" 2>&1
    }
    mkdir -p "$dir/bin" "$dir/lib" "$dir/share" "$dir/licences"
    CC=clang CXX=clang++ "$repo/launcher/build_launcher.sh" linux "$dir/jsrf-pc" "$work/lobj" \
        "$sdl/lib/libSDL2-2.0.so.0" "$sdl/include/SDL2"
    python3 "$here/readme.py" linux "$repo/launcher/linux/README.txt" > "$dir/README.txt"
    install -m 644 "$sdl/lib/libSDL2-2.0.so.0" "$dir/lib/libSDL2-2.0.so.0"
    strip --strip-unneeded "$dir/lib/libSDL2-2.0.so.0" 2>/dev/null || true
    install -m 644 "$repo/launcher/icon/jsrf-pc.png" "$dir/share/jsrf-pc.png"
    printf '%s\n' "$version" > "$dir/VERSION"
    licences "$dir/licences"
    [ -f "$work/SDL2-2.30.12.tar.gz" ] && tar -xzOf "$work/SDL2-2.30.12.tar.gz" SDL2-2.30.12/LICENSE.txt > "$dir/licences/SDL2-LICENSE.txt"
    kit_in "$dir/kit"
    echo "launcher needs:"; objdump -p "$dir/jsrf-pc" | sed -n 's/^ *NEEDED */  /p'
    echo "newest glibc symbol: $(objdump -T "$dir/jsrf-pc" | grep -o 'GLIBC_[0-9.]*' | sort -t. -k2,2n -k3,3n | uniq | tail -n 1)"
    tar -C "$stage" -czf "$out/$name.tar.gz" "$name"
    echo "packaged: $out/$name.tar.gz ($(du -h "$out/$name.tar.gz" | cut -f1))"

    # The tools archive: the setup's downloads, unpacked as the setup does,
    # the wheels put in by the tools' own Python (build_engine.py).
    t="$stage/tools-root"
    mkdir -p "$t/tools" "$t/kit"
    tar -xzf "$dls/python.tar.gz" -C "$t/tools"
    cp "$repo/launcher/kit/build_engine.py" "$t/kit/"
    "$t/tools/python/bin/python3" -E -s -u -X utf8 "$t/kit/build_engine.py" --root "$t" --install-only \
        --install-wheel "$dls/capstone-5.0.9-py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl" \
        --install-wheel "$dls/ziglang-0.13.0-py3-none-manylinux_2_12_x86_64.manylinux2010_x86_64.musllinux_1_1_x86_64.whl"
    printf 'tools=%s\n' "$(sed -n 's/^tools=//p' "$kit/expected.txt")" > "$t/tools/TOOLS.txt"
    find "$t/tools" -name __pycache__ -prune -exec rm -rf {} +
    tar -C "$t" -czf "$out/JSRF-PC-tools-linux-x86_64.tar.gz" tools
    echo "packaged: $out/JSRF-PC-tools-linux-x86_64.tar.gz ($(du -h "$out/JSRF-PC-tools-linux-x86_64.tar.gz" | cut -f1))"
    ;;
windows)
    name=JSRF-PC-windows-x86_64
    dir="$stage/$name"
    "$repo/launcher/windows/deps.sh" "$work" > "$work/deps.log" 2>&1 || { tail -30 "$work/deps.log"; exit 1; }
    tc="$work/llvm-mingw"
    mkdir -p "$dir/engine" "$dir/licences"
    "$repo/launcher/build_launcher.sh" windows "$dir/JSRF-PC.exe" "$work/wobj" "$tc" "$work/deps" "$version"
    install -m 644 "$work/deps/bin/SDL2.dll" "$work/deps/bin/libepoxy-0.dll" "$dir/engine/"
    install -m 644 "$tc/x86_64-w64-mingw32/bin/libwinpthread-1.dll" "$dir/engine/"
    "$tc/bin/llvm-strip" --strip-debug "$dir/engine/libepoxy-0.dll" "$dir/engine/libwinpthread-1.dll"
    crlf() { sed 's/\r*$/\r/' "$1" > "$2"; }
    printf '%s\r\n' "$version" > "$dir/VERSION"
    python3 "$here/readme.py" windows "$repo/launcher/windows/README.txt" > "$work/README.txt"
    crlf "$work/README.txt" "$dir/README.txt"
    licences "$dir/licences"
    for f in "$dir"/licences/*.txt; do crlf "$f" "$f.crlf"; mv "$f.crlf" "$f"; done
    for f in SDL2-COPYING.txt libepoxy-COPYING.txt winpthreads-COPYING.txt; do
        [ -f "$work/licences/$f" ] && crlf "$work/licences/$f" "$dir/licences/$f"
    done
    kit_in "$dir/kit"
    echo "launcher needs:"; "$tc/bin/llvm-objdump" -p "$dir/JSRF-PC.exe" | sed -n 's/^ *DLL Name: */  /p' | grep -v '^  api-ms-win-crt' | tr '\n' ' '; echo
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
    # The tools archive: Python unpacked, the wheels as the setup puts them
    # in (a wheel is a zip of what goes in site-packages), the mark last.
    t="$stage/tools-root"
    mkdir -p "$t/tools"
    tar -xzf "$dls/python.tar.gz" -C "$t/tools"
    for w in "$dls/capstone-5.0.9-py3-none-win_amd64.whl" "$dls/ziglang-0.13.0-py3-none-win_amd64.whl"; do
        python3 -m zipfile -e "$w" "$t/tools/python/Lib/site-packages"
    done
    printf 'tools=%s\r\n' "$(sed -n 's/^tools=//p' "$kit/expected.txt")" > "$t/tools/TOOLS.txt"
    python3 - "$t" "$out/JSRF-PC-tools-windows-x86_64.zip" <<'PY'
import os, sys, zipfile
root, dest = sys.argv[1:3]
with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for r, dirs, files in os.walk(os.path.join(root, "tools")):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(r, f)
            z.write(p, os.path.relpath(p, root))
print("packaged:", dest, os.path.getsize(dest), "bytes")
PY
    ;;
esac

# Last, what was made is checked: what each holds, the line endings, what the
# programs need (launcher/check_package.py).
case "$system" in
linux)   python3 "$repo/launcher/check_package.py" "$out/$name.tar.gz" "$out/JSRF-PC-tools-linux-x86_64.tar.gz" ;;
windows) OBJDUMP="$tc/bin/llvm-objdump" python3 "$repo/launcher/check_package.py" \
             "$out/$name.zip" "$out/JSRF-PC-tools-windows-x86_64.zip" ;;
esac
