#!/bin/bash
#
# Package the Linux build: a folder with the engine, the launcher, a portable
# SDL2 and the licences, as JSRF-PC-linux-x86_64.tar.gz.
#
#   launcher/linux/package.sh <jsrf_recomp> <libSDL2-2.0.so.0> [out dir]
#
# The release build, from the repository at the release tag, is
# launcher/linux/build.sh, in the build image launcher/image.sh makes (Ubuntu
# 22.04, so glibc 2.35, and the result runs on glibc 2.34 or later): the
# bundled SDL (launcher/linux/sdl.sh), the engine with the release flags,
# then this. The engine needs only libc, libm, libGL.so.1 and SDL2, and the
# SDL beside it nothing but libc and libm.
#
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
engine="${1:?the jsrf_recomp to package}"
sdl="${2:?a portable libSDL2-2.0.so.0}"
out="${3:-$PWD}"
name=JSRF-PC-linux-x86_64
stage="$(mktemp -d)"
dir="$stage/$name"

mkdir -p "$dir/bin" "$dir/lib" "$dir/share" "$dir/licences"
install -m 755 "$here/jsrf-unleashed" "$dir/jsrf-pc"
install -m 644 "$here/README.txt" "$dir/README.txt"
install -m 755 "$engine" "$dir/bin/jsrf_recomp"
# The version is the engine's own (port/CMakeLists.txt builds it in from the
# release tag); the launcher's --version reads it from here.
"$engine" --version > "$dir/VERSION"
echo "version: $(cat "$dir/VERSION")"
install -m 644 "$sdl" "$dir/lib/libSDL2-2.0.so.0"
strip --strip-unneeded "$dir/lib/libSDL2-2.0.so.0" 2>/dev/null || true
install -m 644 "$repo/launcher/icon/AppIcon.iconset/icon_256x256.png" "$dir/share/jsrf-pc.png"
install -m 644 "$repo/LICENSE" "$dir/licences/LICENSE.txt"
install -m 644 "$repo/NOTICE" "$dir/licences/NOTICE.txt"
install -m 644 "$repo/LICENSES/LGPL-2.1.txt" "$dir/licences/LGPL-2.1.txt"
install -m 644 "$repo/LICENSES/imgui-MIT.txt" "$dir/licences/imgui-MIT.txt"
[ -n "${SDL_LICENSE:-}" ] && install -m 644 "$SDL_LICENSE" "$dir/licences/SDL2-LICENSE.txt"

# What the engine needs from the system, said once at packaging time.
echo "engine needs:"; objdump -p "$dir/bin/jsrf_recomp" | sed -n 's/^ *NEEDED */  /p'
echo "newest glibc symbol:"; objdump -T "$dir/bin/jsrf_recomp" | grep -o 'GLIBC_[0-9.]*' | sort -t. -k2,2n -k3,3n | uniq | tail -n 1 | sed 's/^/  /'

tar -C "$stage" -czf "$out/$name.tar.gz" "$name"
rm -rf "$stage"
echo "packaged: $out/$name.tar.gz"
