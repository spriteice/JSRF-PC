cmake_minimum_required(VERSION 3.20)

# JSRF Unleashed -- the game, built from this repository.
#
# The generated code is not in the repository (it is derived from the game's
# executable): produce it from your own default.xbe with the pipeline in
# tools/recomp, into port/recomp/gen (the folder .gitignore keeps out of git),
# or point JSRF_GEN_DIR at a folder named recomp/gen elsewhere. Then:
#
#   cmake -S port -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# On Linux add -DNV2A_GL_CONTEXT=sdl (a window, sound and pads through SDL2);
# the Mac uses its own frameworks by default.
#
# The version is the release tag this tree is at (git describe: "0.1.2", or
# "0.1.2-3-gabc1234-dirty" between releases); a tree without its git history
# (an export) passes it with -DJSRF_VERSION=. The engine prints it on its
# first line and answers --version; the launchers take it from there.

# The Mac build is Apple Silicon only. Said before project(), so that a cmake
# running under Rosetta (an Intel Homebrew's) does not quietly build x86_64.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Mac architecture" FORCE)
endif()

# C++ only for Dear ImGui, the in-game menu's (src/menu).
project(jsrf_recomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    RECOMP_ICALL_FEEDBACK
    RECOMP_ABI_CHECK
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

get_filename_component(JSRF_REPO "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(JSRF_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/recomp/gen" CACHE PATH
    "The generated code: a folder named recomp/gen")
if(NOT EXISTS "${JSRF_GEN_DIR}/recomp_types.h")
    message(FATAL_ERROR "No generated code in ${JSRF_GEN_DIR}. Generate it from your "
                        "own default.xbe with tools/recomp (see docs/GETTING_STARTED.md), "
                        "or set JSRF_GEN_DIR.")
endif()
# recomp_manual.c includes "recomp/gen/recomp_types.h": the folder two
# levels above the generated code goes on the include path.
get_filename_component(JSRF_GEN_ROOT "${JSRF_GEN_DIR}/../.." ABSOLUTE)

set(JSRF_VERSION "" CACHE STRING "Version (default: git describe)")
if(NOT JSRF_VERSION)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*" --dirty
                        WORKING_DIRECTORY "${JSRF_REPO}"
                        OUTPUT_VARIABLE _jsrf_describe OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE _jsrf_describe_rc)
        if(_jsrf_describe_rc EQUAL 0)
            string(REGEX REPLACE "^v" "" JSRF_VERSION "${_jsrf_describe}")
        endif()
    endif()
endif()
if(NOT JSRF_VERSION)
    set(JSRF_VERSION "dev")
endif()
message(STATUS "JSRF Unleashed version: ${JSRF_VERSION}")

# Windows (MinGW): the runtime's OpenGL / SDL2 host layers, as on Linux and the
# Mac, rather than its Direct3D 11 ones -- the title draws through its own D3D
# and the pushbuffer, which is what nv2a_gl renders everywhere else.
if(WIN32)
    set(XBOXRECOMP_WIN_GL ON)
    add_compile_definitions(XBOXRECOMP_WIN_GL=1)
endif()

# Android: plain char is signed on x86 and on Apple's arm64, everywhere this
# has run so far; the Arm ABI that Android follows makes it unsigned. Signed,
# as it was tested.
if(ANDROID)
    add_compile_options(-fsigned-char)
endif()

# The runtime: everything in this repository outside port/ and launcher/.
add_subdirectory("${JSRF_REPO}" "${CMAKE_BINARY_DIR}/xboxrecomp")

file(GLOB JSRF_GEN_SOURCES CONFIGURE_DEPENDS "${JSRF_GEN_DIR}/*.c")

if(ANDROID)
    # Android: the engine is libmain.so, which SDL's Java side loads and whose
    # SDL_main it calls (main.c; android/jsrf_android.c sets the scene). The
    # app around it -- launcher/android -- is built by launcher/android/build_apk.sh.
    add_library(jsrf_recomp SHARED
        main.c
        recomp_manual.c
        freecam.c
        xbe_check.c
        android/jsrf_android.c
        ios/xiso.c
        ${JSRF_GEN_SOURCES}
    )
    set_target_properties(jsrf_recomp PROPERTIES OUTPUT_NAME main)
else()
    add_executable(jsrf_recomp
        main.c
        recomp_manual.c
        freecam.c
        xbe_check.c
        ios/xiso.c
        ${JSRF_GEN_SOURCES}
    )
endif()
if(IOS)
    # The iPhone / iPad app around the engine (ios/jsrf_ios.m: the window,
    # the controls, the game in Documents; the disc-image unpacker, ios/xiso.c,
    # is in every build -- the Mac and Linux apps use it through the engine). The
    # bundle -- Info.plist, icons, signature -- is put together by
    # launcher/ios/package_ios.sh from this executable.
    target_sources(jsrf_recomp PRIVATE ios/jsrf_ios.m)
    set_source_files_properties(ios/jsrf_ios.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(jsrf_recomp PROPERTIES MACOSX_BUNDLE OFF)
    target_link_libraries(jsrf_recomp PRIVATE
        "-framework UIKit" "-framework GameController" "-framework AVFoundation"
        "-framework Metal" "-framework QuartzCore" "-framework CoreGraphics"
        "-framework Foundation")
endif()
target_compile_definitions(jsrf_recomp PRIVATE JSRF_VERSION="${JSRF_VERSION}")
target_link_libraries(jsrf_recomp PRIVATE xboxrecomp)
target_include_directories(jsrf_recomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${JSRF_GEN_ROOT}"
    "${JSRF_GEN_DIR}"
    "${JSRF_REPO}/include"
)
if(WIN32 AND XBOXRECOMP_WIN_GL)
    # The icon, a manifest (UTF-8 file names, a sharp window on a high-DPI
    # screen) and version information, shared with the launcher
    # (launcher/windows).
    enable_language(RC)
    string(REGEX MATCH "^([0-9]+)[.]([0-9]+)[.]([0-9]+)" _jsrf_v "${JSRF_VERSION}")
    if(_jsrf_v)
        set(_jsrf_vnum "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
    else()
        set(_jsrf_vnum "0,0,0,0")
    endif()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jsrf_version.h"
         "#define JSRF_VER_NUM ${_jsrf_vnum}\n#define JSRF_VER_STR \"${JSRF_VERSION}\"\n")
    target_sources(jsrf_recomp PRIVATE "${JSRF_REPO}/launcher/windows/resources.rc")
    target_include_directories(jsrf_recomp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    set_source_files_properties("${JSRF_REPO}/launcher/windows/resources.rc" PROPERTIES
        COMPILE_DEFINITIONS "JSRF_ENGINE")
    # An 8 MB stack for the main thread and for the threads the title creates
    # (CreateThread with size 0 takes the image's), as a Linux main thread has:
    # recompiled functions nest as deeply as the guest's calls, and Windows'
    # default is 1 MB.
    find_package(Threads REQUIRED)
    target_link_libraries(jsrf_recomp PRIVATE Threads::Threads winmm dbghelp)
    target_link_options(jsrf_recomp PRIVATE -Wl,--stack,0x800000)
    # The linker map names the function at any jsrf_recomp.exe+offset a crash
    # report prints (kept with a release build, not shipped).
    target_link_options(jsrf_recomp PRIVATE "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/jsrf_recomp.map")
elseif(WIN32)
    target_link_libraries(jsrf_recomp PRIVATE d3d11 dxgi dxguid xinput winmm dbghelp)
elseif(ANDROID)
    # pthreads are in bionic's libc; log and android for the glue.
    target_include_directories(jsrf_recomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ios")
    target_link_libraries(jsrf_recomp PRIVATE m dl log android)
    # -Bsymbolic: the game's functions call one another directly rather than
    # through the PLT, as in an executable (nothing ever interposes on them);
    # they stay in the dynamic symbol table, which is what names them in a
    # crash report.
    target_link_options(jsrf_recomp PRIVATE -Wl,--no-undefined -Wl,-Bsymbolic)
else()
    target_link_libraries(jsrf_recomp PRIVATE pthread m dl)
    target_link_options(jsrf_recomp PRIVATE -rdynamic)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET xbox_menu)
    # The menu's C++ needs nothing from the C++ runtime, but CMake links a C
    # program with a C++ library against libstdc++.so anyway: taken
    # statically, nothing of it is pulled in and the engine needs no
    # libstdc++ of the machine it runs on.
    target_link_options(jsrf_recomp PRIVATE -static-libstdc++ -static-libgcc)
endif()
if(MSVC)
    target_link_options(jsrf_recomp PRIVATE /LARGEADDRESSAWARE)
    target_compile_options(jsrf_recomp PRIVATE /bigobj)
endif()
