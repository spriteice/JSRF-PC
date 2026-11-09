#!/bin/bash
#
# Build the launcher with the setup inside (launcher/setup): the same program
# in the package with the engine and in the one without, which alone has a
# kit (kit/kit.json) and so runs the setup when the engine isn't built yet.
#
#   launcher/build_launcher.sh linux   <out> <obj dir> <libSDL2-2.0.so.0> <SDL include dir>
#   launcher/build_launcher.sh windows <out> <obj dir> <llvm-mingw dir> <deps dir> <version>
#
# Linux, jsrf-pc: built with CC and CXX (cc and c++ unless they are set),
# linked with the SDL given, which the package puts in lib/ (the launcher
# finds it there by its rpath); OpenGL is opened with the setup's window, by
# Dear ImGui's loader, as the engine's menu does. Windows, JSRF-PC.exe: built
# with llvm-mingw, with SDL2 linked in from <deps dir> (launcher/windows/
# deps.sh's), as JSRF-PC.exe starts before it knows where the engine's
# SDL2.dll is, and with the icon, manifest and version information.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
system="${1:?linux or windows}"
out="${2:?the launcher to make}"
obj="${3:?a folder for the objects}"
shift 3
rm -rf "$obj"; mkdir -p "$obj"

srcs=("$repo/launcher/$system/jsrf_launcher.c" "$repo/launcher/setup/setup_core.c"
      "$repo/port/xiso.c" "$repo/port/xbe_check.c")
imgui=("$repo/third_party/imgui/imgui.cpp" "$repo/third_party/imgui/imgui_draw.cpp"
       "$repo/third_party/imgui/imgui_tables.cpp" "$repo/third_party/imgui/imgui_widgets.cpp"
       "$repo/third_party/imgui/backends/imgui_impl_sdl2.cpp" "$repo/third_party/imgui/backends/imgui_impl_opengl3.cpp")
cxxflags=(-std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -O2)

compile() {   # cc cxx sdl-include
    local cc="$1" cxx="$2" inc="$3" f
    for f in "${srcs[@]}"; do
        "$cc" -O2 -Wall -Wextra -I "$inc" -I "$repo/port" -c "$f" -o "$obj/$(basename "$f").o"
    done
    # miniz and Dear ImGui as their authors wrote them: their warnings aren't ours.
    "$cc" -O2 -w -c "$repo/third_party/miniz/miniz.c" -o "$obj/miniz.c.o"
    "$cxx" "${cxxflags[@]}" -Wall -Wextra -I "$inc" -I "$repo/third_party/imgui" \
        -c "$repo/launcher/setup/setup_ui.cpp" -o "$obj/setup_ui.cpp.o"
    for f in "${imgui[@]}"; do
        "$cxx" "${cxxflags[@]}" -w -I "$inc" -I "$repo/third_party/imgui" -c "$f" -o "$obj/$(basename "$f").o"
    done
}

case "$system" in
linux)
    sdl="${1:?the libSDL2-2.0.so.0 to link with}"
    inc="${2:?the SDL include folder, with SDL.h in it}"
    compile "${CC:-cc}" "${CXX:-c++}" "$inc"
    # Linked as C: the C++ is built without exceptions, RTTI or guarded
    # statics, so the launcher needs only libc, libm and the SDL beside it.
    "${CC:-cc}" -o "$out" "$obj"/*.o "$sdl" -ldl -lm -Wl,--as-needed -Wl,-rpath,'$ORIGIN/lib' -s
    ;;
windows)
    tc="${1:?the llvm-mingw toolchain directory}"
    deps="${2:?the deps folder deps.sh made}"
    version="${3:?the version}"
    # X.Y.Z is X.Y.Z.0, X.Y (as 1.0) is X.Y.0.0, a beta's bN.M is 0.0.N.M, and
    # bN is 0.0.N.0, as port/CMakeLists.txt gives the engine.
    num="$(printf '%s' "$version" | sed -n -e 's/^\([0-9]*\)\.\([0-9]*\)\.\([0-9]*\).*/\1,\2,\3,0/p' \
        -e 's/^\([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1,\2,0,0/p' \
        -e 's/^b\([0-9][0-9]*\)\.\([0-9][0-9]*\).*/0,0,\1,\2/p' -e 's/^b\([0-9][0-9]*\).*/0,0,\1,0/p')"
    [ -n "$num" ] || num=0,0,0,0
    printf '#define JSRF_VER_NUM %s\n#define JSRF_VER_STR "%s"\n' "$num" "$version" > "$obj/jsrf_version.h"
    "$tc/bin/x86_64-w64-mingw32-windres" -I "$obj" -I "$repo/launcher/windows" \
        "$repo/launcher/windows/resources.rc" -O coff -o "$obj/launcher.res"
    compile "$tc/bin/x86_64-w64-mingw32-clang" "$tc/bin/x86_64-w64-mingw32-clang++" "$deps/include/SDL2"
    sdl_libs="$(sed -n 's/^Libs.private: *//p' "$deps/lib/pkgconfig/sdl2.pc")"
    # shellcheck disable=SC2086
    "$tc/bin/x86_64-w64-mingw32-clang" -O2 -s -municode -mwindows -o "$out" \
        "$obj"/*.o "$obj/launcher.res" "$deps/lib/libSDL2.a" $sdl_libs \
        -lwinhttp -lshell32 -lole32 -luuid -lcomctl32
    ;;
*)
    echo "build_launcher.sh: linux or windows, not $system" >&2; exit 2 ;;
esac
