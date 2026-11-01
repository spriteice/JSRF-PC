#!/bin/bash
#
# The SDL the Linux package carries: SDL 2.30.12 from source, shared, with
# every backend loaded at run time, so it needs nothing but libc and libm. A
# distribution's own SDL2 links its backends directly and would not load on a
# system missing one of them.
#
#   launcher/linux/sdl.sh <SDL2-2.30.12.tar.gz> <install prefix>
#
# In the build image (launcher/image.sh linux). The source is SDL's release
# tarball (libsdl.org, or the release on SDL's GitHub), checked by its SHA-256.
#
# SDL's CMake turns a backend on by itself when it finds its headers, so every
# option that depends on what is installed is named here, and a package added
# to the image can't change what is built: installing libibus-1.0-dev once
# turned IBus on in the bundled SDL. The options left out are fixed by SDL's
# version and the platform. Then the configuration SDL made, its SDL_config.h,
# must be the one recorded beside this file (sdl-config.h); if it isn't, this
# stops and shows the difference.
#
# The source is unpacked and built beside the prefix (SDL2-2.30.12/ and
# sdl-build/), whose path ends up in the library's assertion messages: the
# release packages have been built with the prefix /work/sdl.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tarball="${1:?SDL2-2.30.12.tar.gz}"
prefix="${2:?the install prefix}"
sha256=ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea

echo "$sha256  $tarball" | sha256sum -c --quiet -
mkdir -p "$prefix"
prefix="$(cd "$prefix" && pwd)"
work="$(dirname "$prefix")"
rm -rf "$work/SDL2-2.30.12" "$work/sdl-build"
tar -xzf "$tarball" -C "$work"

options=(
    -DCMAKE_BUILD_TYPE=Release
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_INSTALL_TESTS=OFF
    -DSDL_RPATH=ON -DSDL_CCACHE=OFF
    # Video: X11 and Wayland, both loaded at run time, KMS/DRM, OpenGL, GLES
    # and Vulkan; none of the embedded boards' drivers.
    -DSDL_X11=ON -DSDL_X11_SHARED=ON -DSDL_X11_XCURSOR=ON -DSDL_X11_XDBE=ON
    -DSDL_X11_XFIXES=ON -DSDL_X11_XINPUT=ON -DSDL_X11_XRANDR=ON
    -DSDL_X11_XSCRNSAVER=ON -DSDL_X11_XSHAPE=ON
    -DSDL_WAYLAND=ON -DSDL_WAYLAND_SHARED=ON -DSDL_WAYLAND_LIBDECOR=ON
    -DSDL_WAYLAND_LIBDECOR_SHARED=ON -DSDL_WAYLAND_QT_TOUCH=ON
    -DSDL_KMSDRM=ON -DSDL_KMSDRM_SHARED=ON
    -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_VULKAN=ON
    -DSDL_OFFSCREEN=ON -DSDL_DUMMYVIDEO=ON
    -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF -DSDL_VIVANTE=OFF
    # Sound: ALSA, PulseAudio and PipeWire, loaded at run time, and OSS;
    # libsamplerate, loaded at run time; none of the rest.
    -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON -DSDL_PULSEAUDIO=ON -DSDL_PULSEAUDIO_SHARED=ON
    -DSDL_PIPEWIRE=ON -DSDL_PIPEWIRE_SHARED=ON -DSDL_OSS=ON
    -DSDL_DISKAUDIO=ON -DSDL_DUMMYAUDIO=ON
    -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_ESD=OFF -DSDL_ARTS=OFF -DSDL_NAS=OFF
    -DSDL_FUSIONSOUND=OFF
    -DSDL_LIBSAMPLERATE=ON -DSDL_LIBSAMPLERATE_SHARED=ON
    # Input and the rest: D-Bus and udev, SDL's own HIDAPI without libusb,
    # the C library's iconv, and no IBus.
    -DSDL_DBUS=ON -DSDL_LIBUDEV=ON
    -DSDL_HIDAPI=ON -DSDL_HIDAPI_JOYSTICK=ON -DSDL_HIDAPI_LIBUSB=OFF
    -DSDL_SYSTEM_ICONV=ON -DSDL_LIBICONV=OFF
    -DSDL_IBUS=OFF
)
CC=clang cmake -S "$work/SDL2-2.30.12" -B "$work/sdl-build" -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$prefix" "${options[@]}" > "$work/sdl-build.log" \
    || { cat "$work/sdl-build.log"; exit 1; }
cmake --build "$work/sdl-build" >> "$work/sdl-build.log"
cmake --install "$work/sdl-build" >> "$work/sdl-build.log"

if ! diff -u "$here/sdl-config.h" "$prefix/include/SDL2/SDL_config.h"; then
    echo "sdl.sh: SDL's configuration is not the recorded one (above: - recorded, + this build)." >&2
    echo "        Something in the build image changed what SDL builds." >&2
    exit 1
fi
echo "sdl.sh: SDL 2.30.12 in $prefix, configured as recorded"
