#!/bin/bash
#
# What the Windows release is built with, cross-compiled on Linux: llvm-mingw
# 20241217 (LLVM 19.1.6, UCRT), SDL2 2.30.0 (its MinGW development package)
# and libepoxy 1.5.10, cross-built with that toolchain by meson 1.12.1.
#
#   launcher/windows/deps.sh <dir>
#
# In the build image (launcher/image.sh windows). Each download is checked by
# its SHA-256, meson's too. The three below are fetched by
# launcher/windows/fetch.sh only when <dir> doesn't have them already, and
# meson each time. In <dir> it makes llvm-mingw/ (the toolchain), deps/ (SDL2
# and libepoxy in one prefix, with include/, lib/ and bin/), and licences/
# (the texts package.sh takes for them, through LICENCES_DIR).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
dir="$(mkdir -p "${1:?the folder for the toolchain and libraries}" && cd "$1" && pwd)"

llvm_mingw=llvm-mingw-20241217-ucrt-ubuntu-20.04-x86_64
"$here/fetch.sh" "$dir/$llvm_mingw.tar.xz" \
    cfe4aac3d245a77d8cd0c41bdcd7971b1002c771d3bba2ac39702733b0fb0c12 \
    "https://github.com/mstorsjo/llvm-mingw/releases/download/20241217/$llvm_mingw.tar.xz"
"$here/fetch.sh" "$dir/SDL2-devel-2.30.0-mingw.tar.gz" \
    d44bd8cd869219f43634a898d7735d0c92d4b2cca129f709dc8b98402b36d5f3 \
    https://github.com/libsdl-org/SDL/releases/download/release-2.30.0/SDL2-devel-2.30.0-mingw.tar.gz
"$here/fetch.sh" "$dir/libepoxy_1.5.10.orig.tar.gz" \
    a7ced37f4102b745ac86d6a70a9da399cc139ff168ba6b8002b4d8d43c900c15 \
    http://archive.ubuntu.com/ubuntu/pool/main/libe/libepoxy/libepoxy_1.5.10.orig.tar.gz

cd "$dir"
rm -rf llvm-mingw deps licences SDL2-2.30.0 libepoxy-1.5.10 epoxy-build meson
tar -xJf "$llvm_mingw.tar.xz"
mv "$llvm_mingw" llvm-mingw
tc="$dir/llvm-mingw"

# SDL2: the 64-bit part of the development package, as it is.
tar -xzf SDL2-devel-2.30.0-mingw.tar.gz
mkdir -p deps licences
cp -a SDL2-2.30.0/x86_64-w64-mingw32/. deps/

# libepoxy: OpenGL through WGL only. The Windows build has no EGL or GLX.
# Meson's version shows in the DLL (1.12 marks it a console DLL, as the
# releases' is, and older ones don't), so it is pinned too.
python3 -m venv meson
echo "meson==1.12.1 --hash=sha256:930bc7542cbd9f57009e182fd014eba48cf1a0180a7b9006d2d32d8f168d8b02" \
    > meson-requirements.txt
meson/bin/pip install --quiet --require-hashes -r meson-requirements.txt
tar -xzf libepoxy_1.5.10.orig.tar.gz
cat > epoxy-cross.txt <<EOF
[binaries]
c = '$tc/bin/x86_64-w64-mingw32-clang'
ar = '$tc/bin/llvm-ar'
strip = '$tc/bin/llvm-strip'
windres = '$tc/bin/x86_64-w64-mingw32-windres'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF
meson/bin/meson setup epoxy-build libepoxy-1.5.10 --cross-file epoxy-cross.txt \
    --prefix="$dir/deps" --buildtype=release \
    -Ddocs=false -Dglx=no -Degl=no -Dx11=false -Dtests=false > epoxy-build.log
ninja -C epoxy-build install >> epoxy-build.log

cp SDL2-2.30.0/COPYING.txt licences/SDL2-COPYING.txt
cp libepoxy-1.5.10/COPYING licences/libepoxy-COPYING.txt
cp llvm-mingw/x86_64-w64-mingw32/share/mingw32/COPYING.winpthreads.txt licences/winpthreads-COPYING.txt
rm -rf SDL2-2.30.0 libepoxy-1.5.10 epoxy-build meson epoxy-cross.txt meson-requirements.txt
echo "deps.sh: llvm-mingw 20241217, SDL2 2.30.0 and libepoxy 1.5.10 in $dir"
