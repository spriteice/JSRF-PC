cmake_minimum_required(VERSION 3.20)
# C++ only for Dear ImGui, the in-game menu's (src/menu).
project(xboxrecomp VERSION 0.8.0 LANGUAGES C CXX)

# Xbox Static Recompilation Runtime Library
# Provides link-time libraries for statically recompiled Xbox games.
# Pattern: extract emulator subsystems (xemu) into reusable static libraries.
#
# Libraries:
#   xbox_kernel    - Xbox kernel replacement layer (147 imports → Win32)
#   xbox_d3d8      - D3D8 → D3D11 graphics compatibility
#   xbox_dsound    - DirectSound → software mixer compatibility
#   xbox_input     - Xbox gamepad → XInput compatibility
#   xbox_apu       - MCPX APU audio emulation (extracted from xemu)
#   xbox_nv2a      - NV2A GPU emulation (extracted from xemu)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Common definitions for all targets
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Windows with the host layers the Mac and Linux builds use -- the OpenGL
# renderer (nv2a_gl) through SDL2 and libepoxy, SDL2's sound and controllers --
# instead of Direct3D 11, XAudio2 and XInput. JSRF-PC's Windows build
# (MinGW) turns it on: its title brings its own D3D and draws through the
# pushbuffer, which is nv2a_gl's job on every other host.
option(XBOXRECOMP_WIN_GL "Windows: OpenGL/SDL2 host layers instead of D3D11/XAudio2/XInput" OFF)
if(WIN32 AND XBOXRECOMP_WIN_GL)
    add_compile_definitions(XBOXRECOMP_WIN_GL=1)
endif()

# ── Runtime Libraries ─────────────────────────────────────────

# Platform: Win32/NT type vocabulary + POSIX implementation of generic
# Win32 host primitives. Carries no Xbox semantics; provides the foundation
# the kernel HLE and the graphics/audio/input layers build on.
add_subdirectory(src/platform)

# Core: Xbox kernel replacement (memory layout, file I/O, threading, sync)
add_subdirectory(src/kernel)

# Graphics: D3D8 → D3D11 compatibility layer
add_subdirectory(src/d3d)

# Audio: DirectSound → software mixer compatibility
add_subdirectory(src/audio)

# Audio: MCPX APU hardware emulation (voice processor, DSP)
add_subdirectory(src/apu)

# GPU: NV2A hardware emulation (register handlers, MMIO, push buffer)
add_subdirectory(src/nv2a)

# NV2A -> OpenGL translation (POSIX, and Windows with XBOXRECOMP_WIN_GL)
if(NOT WIN32 OR XBOXRECOMP_WIN_GL)
    add_subdirectory(src/nv2a_gl)
endif()

# The in-game menu (Dear ImGui), over nv2a_gl's SDL window. Before the
# libraries that add rows to it, so they can find it (if(TARGET xbox_menu)).
if(NV2A_GL_MENU)
    add_subdirectory(src/menu)
endif()

# USB: MCPX OHCI host controllers. A title's XAPI is statically linked and
# drives these registers directly, so a pad only exists if they do.
add_subdirectory(src/usb)

# Input: Xbox gamepad → XInput
add_subdirectory(src/input)

# Shared FMV player (Media Foundation -> D3D8 texture)
add_subdirectory(src/video)

# ── Umbrella target ───────────────────────────────────────────

# Convenience target that links all runtime libraries
add_library(xboxrecomp INTERFACE)
target_link_libraries(xboxrecomp INTERFACE
    xbox_kernel
    xbox_d3d8
    xbox_dsound
    xbox_input
    xbox_usb
    xbox_apu
    xbox_nv2a
    $<$<OR:$<NOT:$<BOOL:${WIN32}>>,$<BOOL:${XBOXRECOMP_WIN_GL}>>:xbox_nv2a_gl>
    xbox_video
)

