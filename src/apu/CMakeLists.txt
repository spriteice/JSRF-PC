# MCPX APU Emulation Library
# Extracted from xemu (https://github.com/xemu-project/xemu)
#
# Voice Processor (VP): ADPCM/PCM decode, sample mixing, envelopes, filters
# Global Processor (GP): Stub - effects bypass
# Encode Processor (EP): Stub - direct mixbin passthrough

set(XBOX_APU_SOURCES
    apu_core.c
    apu_vp.c
    apu_dsp.c
    apu_mmio_hook.c
    apu_channels.c
    apu_gpfx.c
    # SDL2's audio, answering the sound back end's entry points
    # (apu_xaudio2.h), on Linux and Windows.
    apu_sdl.c
)

add_library(xbox_apu STATIC ${XBOX_APU_SOURCES})

target_include_directories(xbox_apu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# qemu_shim.h, the QEMU types xemu's APU code is written against, is in nv2a/
target_include_directories(xbox_apu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../nv2a
)

# The platform layer (on Linux, Win32 primitives on POSIX), SDL2's audio and
# libm.
find_package(SDL2 REQUIRED)
target_link_libraries(xbox_apu PRIVATE platform SDL2::SDL2 m)
if(WIN32)
    target_link_libraries(xbox_apu PRIVATE kernel32 winmm)
endif()

target_compile_options(xbox_apu PRIVATE -Wall -Wextra)
