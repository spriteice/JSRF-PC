#!/bin/bash
#
# The Linux release package, start to finish: the bundled SDL, the engine,
# then package.sh.
#
#   launcher/linux/build.sh <work dir> [CMake options for the engine]
#
# In the build image (launcher/image.sh linux), with no network. <work dir>
# holds SDL2-2.30.12.tar.gz; the SDL, the engine's build and the package
# (out/JSRF-PC-linux-x86_64.tar.gz) go there too. The generated code is the
# repository's port/recomp/gen, or the folder named by -DJSRF_GEN_DIR=, and
# the version comes from git unless -DJSRF_VERSION= gives one.
#
# The engine needs only libc, libm, libGL.so.1 and SDL2: --as-needed and the
# legacy libGL leave out the rest, and the menu's C++ runtime is linked in
# (port/CMakeLists.txt).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="$(cd "${1:?the work directory}" && pwd)"
shift

rm -rf "$work/sdl" "$work/build" "$work/out"
"$here/sdl.sh" "$work/SDL2-2.30.12.tar.gz" "$work/sdl"
CC=clang CXX=clang++ cmake -S "$repo/port" -B "$work/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DNV2A_GL_CONTEXT=sdl -DOpenGL_GL_PREFERENCE=LEGACY \
    -DCMAKE_EXE_LINKER_FLAGS=-Wl,--as-needed -DCMAKE_PREFIX_PATH="$work/sdl" "$@"
cmake --build "$work/build"
mkdir -p "$work/out"
tar -xzOf "$work/SDL2-2.30.12.tar.gz" SDL2-2.30.12/LICENSE.txt > "$work/out/SDL2-LICENSE.txt"
SDL_LICENSE="$work/out/SDL2-LICENSE.txt" "$here/package.sh" \
    "$work/build/jsrf_recomp" "$work/sdl/lib/libSDL2-2.0.so.0" "$work/out"
rm "$work/out/SDL2-LICENSE.txt"
echo "build image: $(ldd --version | sed -n 1p)"
