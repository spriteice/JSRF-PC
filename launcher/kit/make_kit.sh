#!/bin/bash
#
# The kit: everything the player's PC needs to build the engine but the game.
# The engine's own code (port/ and src/), compiled once here with the same Zig
# the player's PC gets, the link's command, the pipeline that translates
# default.xbe, and kit.json saying how they go together. The launcher's setup
# (launcher/setup) runs build_engine.py with it at the player's first start.
#
#   launcher/kit/make_kit.sh <linux|windows> <work dir> <version> <kit dir> <ziglang wheel>
#
# In the build image for the system (launcher/image.sh linux, or windows),
# from the repository at /src, as CI's builds are. <ziglang wheel> is the
# ziglang 0.13.0 wheel for Linux x86-64 (checked here by its SHA-256): it
# compiles for both systems. The Windows kit takes llvm-mingw's winpthreads
# headers and import library, SDL2's MinGW package and the libepoxy deps.sh
# builds, as the release's engine does today.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
system="${1:?linux or windows}"
work="$(mkdir -p "${2:?the work directory}" && cd "$2" && pwd)"
version="${3:?the version}"
kit="${4:?the kit directory}"
wheel="${5:?the ziglang 0.13.0 wheel for Linux}"
zig_sha=3ce0c9f16547e5d61b32e0d226926e9a2552ef4b91fccf7ab5ea1a623a77824b
echo "$zig_sha  $wheel" | sha256sum -c --quiet -

rm -rf "$work/zigpkg" "$work/kbuild" "$work/kgen" "$work/bin" "$work/inc" "$work/zig-cache"
python3 -m zipfile -e "$wheel" "$work/zigpkg"
chmod +x "$work/zigpkg/ziglang/zig"
zig="$work/zigpkg/ziglang/zig"
export ZIG_GLOBAL_CACHE_DIR="$work/zig-cache" ZIG_LOCAL_CACHE_DIR="$work/zig-cache"
echo "make_kit: zig $("$zig" version)"
mkdir -p "$work/bin" "$work/inc"
gen="$work/kgen/recomp/gen"
mkdir -p "$gen"
cp "$repo/templates/runtime/recomp_types.h" "$gen/"

first=()
case "$system" in
linux)
    target=x86_64-linux-gnu.2.34
    [ -f "$work/SDL2-2.30.12.tar.gz" ] || python3 "$repo/.github/ci/fetch.py" \
        https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz \
        "$work/SDL2-2.30.12.tar.gz" "$(sed -n 's/^sha256=//p' "$repo/launcher/linux/sdl.sh")"
    [ -f "$work/sdl/lib/libSDL2-2.0.so.0" ] || "$repo/launcher/linux/sdl.sh" "$work/SDL2-2.30.12.tar.gz" "$work/sdl" > "$work/sdl.log" 2>&1
    # The GL and X11 headers only (Zig brings the C library's for the
    # target; SDL_syswm.h, which the menu's SDL backend includes, wants X11's).
    cp -r /usr/include/GL /usr/include/KHR /usr/include/X11 "$work/inc/"
    extra=(-DNV2A_GL_CONTEXT=sdl -DOpenGL_GL_PREFERENCE=LEGACY -DCMAKE_EXE_LINKER_FLAGS=-Wl,--as-needed
           -DCMAKE_PREFIX_PATH="$work/sdl"
           -DOPENGL_gl_LIBRARY=/usr/lib/x86_64-linux-gnu/libGL.so.1 -DOPENGL_INCLUDE_DIR="$work/inc")
    ;;
windows)
    target=x86_64-windows-gnu
    "$repo/launcher/windows/deps.sh" "$work" > "$work/deps.log" 2>&1 || { tail -30 "$work/deps.log"; exit 1; }
    tc="$work/llvm-mingw"
    # Zig's MinGW has no winpthreads: its headers and import library are
    # llvm-mingw's, and the package ships libwinpthread-1.dll as today. Zig
    # has pthread_time.h and two others without the functions, so these go on
    # the path with -I, ahead of the C library's own folders.
    mkdir -p "$work/pthinc"
    for h in pthread.h pthread_compat.h pthread_signal.h pthread_time.h pthread_unistd.h sched.h semaphore.h; do
        cp "$tc/x86_64-w64-mingw32/include/$h" "$work/pthinc/"
    done
    first=(-I "$work/pthinc")
    mkdir -p "$work/pthlib"
    cp "$tc/x86_64-w64-mingw32/lib/libwinpthread.dll.a" "$work/pthlib/libwinpthread.dll.a"
    cp "$tc/x86_64-w64-mingw32/lib/libwinpthread.dll.a" "$work/pthlib/libpthread.dll.a"
    cat > "$work/zig-windows.cmake" <<CM
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER   "$work/bin/zcc")
set(CMAKE_CXX_COMPILER "$work/bin/zc++")
set(CMAKE_RC_COMPILER  "$tc/bin/x86_64-w64-mingw32-windres")
set(CMAKE_AR           "$tc/bin/llvm-ar")
set(CMAKE_RANLIB       "$tc/bin/llvm-ranlib")
set(CMAKE_FIND_ROOT_PATH "$work/deps" "$work/pthlib")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
CM
    extra=(-DCMAKE_TOOLCHAIN_FILE="$work/zig-windows.cmake" -DCMAKE_C_STANDARD_LIBRARIES="-L$work/pthlib"
           -DCMAKE_CXX_STANDARD_LIBRARIES="-L$work/pthlib")
    ;;
*) echo "make_kit: linux or windows" >&2; exit 1 ;;
esac
printf '#!/bin/sh\nexec %s cc -target %s -g0 %s -isystem %s "$@"\n' "$zig" "$target" "${first[*]:-}" "$work/inc" > "$work/bin/zcc"
printf '#!/bin/sh\nexec %s c++ -target %s -g0 %s -isystem %s "$@"\n' "$zig" "$target" "${first[*]:-}" "$work/inc" > "$work/bin/zc++"
chmod +x "$work/bin/zcc" "$work/bin/zc++"

CC="$work/bin/zcc" CXX="$work/bin/zc++" cmake -S "$repo/port" -B "$work/kbuild" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "${extra[@]}" \
    -DJSRF_GEN_DIR="$gen" -DJSRF_VERSION="$version" > "$work/kcmake.log" 2>&1 || { cat "$work/kcmake.log"; exit 1; }

# Everything the engine's link takes but the link itself.
exe=jsrf_recomp; [ "$system" = windows ] && exe=jsrf_recomp.exe
mapfile -t inputs < <(ninja -C "$work/kbuild" -t query "$exe" |
    sed -n '/^  input:/,/^  outputs:/p' | sed -n 's/^    \(| \)\{0,1\}\([^|].*\)$/\2/p')
ninja -C "$work/kbuild" "${inputs[@]}" > "$work/kbuild.log" 2>&1 || { tail -40 "$work/kbuild.log"; exit 1; }
link="$(ninja -C "$work/kbuild" -t commands "$exe" | tail -n 1)"

rm -rf "$kit"; mkdir -p "$kit"
python3 "$here/kit_json.py" --system "$system" --target "$target" --version "$version" \
    --repo "$repo" --build "$work/kbuild" --gen "$gen" --kit "$kit" --zig "$zig" --link "$link"
cp "$here/build_engine.py" "$kit/build_engine.py"
echo "make_kit: $(du -sh "$kit" | cut -f1) in $kit"
