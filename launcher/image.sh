#!/bin/bash
#
# The build image: what the release packages are built in, and CI's jobs run
# in. Run as root in a fresh container of BASE, below (Ubuntu 22.04, so glibc
# 2.35, and the Linux package runs on 2.34 or later):
#
#   launcher/image.sh linux|windows [tests]
#
# launcher/images.sh runs it, and the images it makes are kept on GitHub's
# registry, named for this file's SHA-256.
#
# It installs the packages below from Ubuntu's snapshot archive as it was at
# SNAPSHOT, each at the version named, so the image is the same whenever and
# wherever it is made, and nothing else. The snapshot archive is often slow,
# so each download is tried up to 5 times, and Ubuntu's own archive is tried
# after it (below). A package added here can change what SDL builds:
# launcher/linux/sdl.sh checks.
#
#   linux   the Linux package: clang, CMake and Ninja, and the headers SDL and
#           the engine build against
#   windows the Windows package's host: CMake, Ninja, and Python for meson;
#           the toolchain and the libraries come from launcher/windows/deps.sh
#   tests   what the tests need on top, in CI only: GCC, Mesa's EGL for the
#           renderer's tests, Python's venv for the tools, and llvm-symbolizer
#           for the thread sanitizer's reports
set -euo pipefail

# The base image, by digest, which launcher/images.sh reads from here.
# shellcheck disable=SC2034
BASE=ubuntu:22.04@sha256:076e7bf82319619f28dd0aa9c02ab34b055ad37100395b4f49a2ffba04fb5e11
SNAPSHOT=20261101T000000Z

linux=(
    clang=1:14.0-55~exp2 cmake=3.22.1-1ubuntu1.22.04.2 ninja-build=1.10.1-1
    make=4.3-4.1build1 pkg-config=0.29.2-1ubuntu3 git=1:2.34.1-1ubuntu1.18
    file=1:5.41-3ubuntu0.1 binutils=2.38-4ubuntu2.12 python3=3.10.6-1~22.04.1
    libgl-dev=1.4.0-1 libegl-dev=1.4.0-1 libgles-dev=1.4.0-1
    libx11-dev=2:1.7.5-1ubuntu0.4 libxext-dev=2:1.3.4-1build1
    libxrandr-dev=2:1.5.2-1build1 libxcursor-dev=1:1.2.0-2build4
    libxi-dev=2:1.8-1build1 libxfixes-dev=1:6.0.0-1 libxss-dev=1:1.2.3-1build2
    libxxf86vm-dev=1:1.1.4-1build3 libxinerama-dev=2:1.1.4-3
    libxkbcommon-dev=1.4.0-1
    libwayland-dev=1.20.0-1ubuntu0.1 wayland-protocols=1.25-1
    libdecor-0-dev=0.1.0-3build1
    libpulse-dev=1:15.99.1+dfsg1-1ubuntu2.2 libpipewire-0.3-dev=0.3.48-1ubuntu3.2
    libasound2-dev=1.2.6.1-1ubuntu1.2 libsamplerate0-dev=0.2.2-1build1
    libdrm-dev=2.4.113-2~ubuntu0.22.04.1 libgbm-dev=23.2.1-1ubuntu3.1~22.04.4
    libudev-dev=249.11-0ubuntu3.23 libdbus-1-dev=1.12.20-2ubuntu4.1
    libssl-dev=3.0.2-0ubuntu1.30 libepoxy-dev=1.5.10-1
)
windows=(
    cmake=3.22.1-1ubuntu1.22.04.2 ninja-build=1.10.1-1 python3=3.10.6-1~22.04.1
    python3-venv=3.10.6-1~22.04.1 xz-utils=5.2.5-2ubuntu1.2
)
tests=(
    gcc=4:11.2.0-1ubuntu1 g++=4:11.2.0-1ubuntu1
    libegl-mesa0=23.2.1-1ubuntu3.1~22.04.4 libgl1-mesa-dri=23.2.1-1ubuntu3.1~22.04.4
    python3-venv=3.10.6-1~22.04.1 llvm-14=1:14.0.0-1ubuntu1.1
)

[ "$(. /etc/os-release && echo "$VERSION_ID")" = 22.04 ] || { echo "image.sh: Ubuntu 22.04 only" >&2; exit 1; }
want=()
for group in "$@"; do
    case "$group" in
        linux) want+=("${linux[@]}") ;;
        windows) want+=("${windows[@]}") ;;
        tests) want+=("${tests[@]}") ;;
        *) echo "usage: launcher/image.sh linux|windows [tests]" >&2; exit 2 ;;
    esac
done
[ "${#want[@]}" -gt 0 ] || { echo "usage: launcher/image.sh linux|windows [tests]" >&2; exit 2; }

export DEBIAN_FRONTEND=noninteractive
apt=(apt-get -o Acquire::Retries=5)
# The snapshot archive is HTTPS only, and the base image has no CA
# certificates. They come first from the release pocket, which never changes,
# over plain HTTP; apt checks every package's signature either way.
echo "deb http://archive.ubuntu.com/ubuntu jammy main" > /etc/apt/sources.list
"${apt[@]}" update
"${apt[@]}" install -y --no-install-recommends ca-certificates=20211016
# The packages come from the snapshot. If it fails, the same versions come
# from Ubuntu's own archive, while it still has them; either way they are
# the same files.
from=
for archive in "https://snapshot.ubuntu.com/ubuntu/$SNAPSHOT" http://archive.ubuntu.com/ubuntu; do
    printf 'deb %s %s main universe\n' "$archive" jammy "$archive" jammy-updates \
        "$archive" jammy-security > /etc/apt/sources.list
    if "${apt[@]}" update && "${apt[@]}" install -y --no-install-recommends \
            ca-certificates=20260816ubuntu0.22.04.1 "${want[@]}"; then
        from="$archive"
        break
    fi
    echo "image.sh: the packages couldn't be installed from $archive" >&2
done
[ -n "$from" ] || { echo "image.sh: no archive had them" >&2; exit 1; }
rm -rf /var/lib/apt/lists/*
# The repository is mounted, owned by someone else: let git read it.
if command -v git > /dev/null; then git config --global --add safe.directory '*'; fi
echo "image.sh: $* at the snapshot of $SNAPSHOT, from $from; glibc $(ldd --version | sed -n '1s/.* //p')"
