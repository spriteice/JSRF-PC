#!/bin/bash
#
# Build JSRF-PC Tools (launcher/tools): one program, beside the launcher in
# both packages, with the same SDL and Dear ImGui as the launcher's setup
# window (launcher/build_launcher.sh, whose arguments these are).
#
#   launcher/tools/build_tools.sh linux   <out> <obj dir> <libSDL2-2.0.so.0> <SDL include dir>
#   launcher/tools/build_tools.sh windows <out> <obj dir> <llvm-mingw dir> <deps dir> <version>
#
# The kernel's HMAC-SHA1 (src/kernel/kernel_crypto.c) is built with the
# engine's headers, as tests/kernel_crypto builds it; the rest as the
# launcher's code is. On Windows, JSRF-PC-Tools.exe has the launcher's icon,
# its own manifest (launcher/windows/tools.manifest: UTF-8 as the program's
# code page, as the launcher's, so plain fopen opens any path) and version
# information.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
system="${1:?linux or windows}"
out="${2:?the program to make}"
obj="${3:?a folder for the objects}"
shift 3
rm -rf "$obj"; mkdir -p "$obj"

srcs=("$repo/launcher/tools/jsrf_save.c" "$repo/launcher/tools/save_sources.c" "$repo/launcher/tools/save_import.c"
      "$repo/launcher/tools/xbox_disk.c" "$repo/launcher/tools/file_dialog.c" "$repo/launcher/tools/mission_walk.c"
      "$repo/launcher/setup/setup_core.c" "$repo/port/xiso.c" "$repo/port/xbe_check.c")
kernel=("$repo/launcher/tools/kernel_crypto_host.c" "$repo/src/kernel/kernel_crypto.c")
cxx_srcs=("$repo/launcher/tools/tools_main.cpp" "$repo/launcher/tools/tools_saves.cpp" "$repo/launcher/tools/tools_missions.cpp"
          "$repo/src/menu/menu_style.cpp")
imgui=("$repo/third_party/imgui/imgui.cpp" "$repo/third_party/imgui/imgui_draw.cpp"
       "$repo/third_party/imgui/imgui_tables.cpp" "$repo/third_party/imgui/imgui_widgets.cpp"
       "$repo/third_party/imgui/backends/imgui_impl_sdl2.cpp" "$repo/third_party/imgui/backends/imgui_impl_opengl3.cpp")
cxxflags=(-std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -O2)
inc_tools=(-I "$repo/launcher/tools" -I "$repo/launcher/setup" -I "$repo/port")

compile() {   # cc cxx sdl-include
    local cc="$1" cxx="$2" inc="$3" f
    for f in "${srcs[@]}"; do
        "$cc" -std=gnu11 -O2 -Wall -Wextra -I "$inc" "${inc_tools[@]}" -c "$f" -o "$obj/$(basename "$f").o"
    done
    for f in "${kernel[@]}"; do
        "$cc" -std=gnu11 -O2 -Wall -Wextra -I "$repo/src" -I "$repo/src/platform" -I "$repo/src/kernel" \
            -I "$repo/launcher/tools" -c "$f" -o "$obj/$(basename "$f").o"
    done
    "$cc" -O2 -w -c "$repo/third_party/miniz/miniz.c" -o "$obj/miniz.c.o"
    for f in "${cxx_srcs[@]}"; do
        "$cxx" "${cxxflags[@]}" -Wall -Wextra -I "$inc" "${inc_tools[@]}" -I "$repo/third_party/imgui" \
            -I "$repo/src/menu" -c "$f" -o "$obj/$(basename "$f").o"
    done
    for f in "${imgui[@]}"; do
        "$cxx" "${cxxflags[@]}" -w -I "$inc" -I "$repo/third_party/imgui" -c "$f" -o "$obj/$(basename "$f").o"
    done
}

case "$system" in
linux)
    sdl="${1:?the libSDL2-2.0.so.0 to link with}"
    inc="${2:?the SDL include folder, with SDL.h in it}"
    compile "${CC:-cc}" "${CXX:-c++}" "$inc"
    "${CC:-cc}" -o "$out" "$obj"/*.o "$sdl" -ldl -lm -Wl,--as-needed -Wl,-rpath,'$ORIGIN/lib' -s
    ;;
windows)
    tc="${1:?the llvm-mingw toolchain directory}"
    deps="${2:?the deps folder deps.sh made}"
    version="${3:?the version}"
    # The version's four numbers, as launcher/build_launcher.sh makes them.
    num="$(printf '%s' "$version" | sed -n -e 's/^\([0-9]*\)\.\([0-9]*\)\.\([0-9]*\).*/\1,\2,\3,0/p' \
        -e 's/^\([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1,\2,0,0/p' \
        -e 's/^b\([0-9][0-9]*\)\.\([0-9][0-9]*\).*/0,0,\1,\2/p' -e 's/^b\([0-9][0-9]*\).*/0,0,\1,0/p')"
    [ -n "$num" ] || num=0,0,0,0
    printf '#define JSRF_VER_NUM %s\n#define JSRF_VER_STR "%s"\n' "$num" "$version" > "$obj/jsrf_version.h"
    "$tc/bin/x86_64-w64-mingw32-windres" -DJSRF_TOOLS -I "$obj" -I "$repo/launcher/windows" \
        "$repo/launcher/windows/resources.rc" -O coff -o "$obj/tools.res"
    compile "$tc/bin/x86_64-w64-mingw32-clang" "$tc/bin/x86_64-w64-mingw32-clang++" "$deps/include/SDL2"
    sdl_libs="$(sed -n 's/^Libs.private: *//p' "$deps/lib/pkgconfig/sdl2.pc")"
    # shellcheck disable=SC2086
    "$tc/bin/x86_64-w64-mingw32-clang" -O2 -s -municode -mwindows -o "$out" \
        "$obj"/*.o "$obj/tools.res" "$deps/lib/libSDL2.a" $sdl_libs -lwinhttp -lshell32 -lole32 -luuid -lcomdlg32
    ;;
*)
    echo "build_tools.sh: linux or windows, not $system" >&2; exit 2 ;;
esac
