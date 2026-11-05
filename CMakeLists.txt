cmake_minimum_required(VERSION 3.20)
# C++ only for Dear ImGui, the in-game menu's (src/menu).
project(xboxrecomp VERSION 0.8.0 LANGUAGES C CXX)

# Xbox Static Recompilation Runtime Library
# Provides link-time libraries for statically recompiled Xbox games.
# Pattern: extract emulator subsystems (xemu) into reusable static libraries.
#
# Libraries:
#   xbox_kernel    - Xbox kernel replacement layer (147 imports → Win32)
#   xbox_d3d8      - D3D8 headers: the types, and texture swizzling
#   xbox_input     - Xbox gamepad → SDL2 GameController
#   xbox_apu       - MCPX APU audio emulation (extracted from xemu)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Common definitions for all targets
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ── Runtime Libraries ─────────────────────────────────────────

# Platform: Win32/NT type vocabulary + POSIX implementation of generic
# Win32 host primitives. Carries no Xbox semantics; provides the foundation
# the kernel HLE and the graphics/audio/input layers build on.
add_subdirectory(src/platform)

# Core: Xbox kernel replacement (memory layout, file I/O, threading, sync)
add_subdirectory(src/kernel)

# Graphics: the D3D8 headers (the title brings its own D3D, and nv2a_gl draws
# what it writes to the pushbuffer)
add_subdirectory(src/d3d)

# Audio: MCPX APU hardware emulation (voice processor, DSP)
add_subdirectory(src/apu)

# NV2A -> OpenGL translation
add_subdirectory(src/nv2a_gl)

# The in-game menu (Dear ImGui), over nv2a_gl's SDL window. Before the
# libraries that add rows to it, so they can find it (if(TARGET xbox_menu)).
if(NV2A_GL_MENU)
    add_subdirectory(src/menu)
endif()

# USB: MCPX OHCI host controllers. A title's XAPI is statically linked and
# drives these registers directly, so a pad only exists if they do.
add_subdirectory(src/usb)

# Input: Xbox gamepad → SDL2 GameController
add_subdirectory(src/input)

# ── Umbrella target ───────────────────────────────────────────

# Convenience target that links all runtime libraries
add_library(xboxrecomp INTERFACE)
target_link_libraries(xboxrecomp INTERFACE
    xbox_kernel
    xbox_d3d8
    xbox_input
    xbox_usb
    xbox_apu
    xbox_nv2a_gl
)

