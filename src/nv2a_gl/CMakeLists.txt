# NV2A -> OpenGL backend.
#
# Separate from nv2a_pb_exec (the software rasteriser): this one turns the same pushbuffer into real
# draw calls. Built only where EGL and GL are available; on a host without
# them the runtime keeps the software path and nothing else changes.
set(NV2A_GL_SOURCES
    nv2a_vsh.c
    nv2a_vsh_interp.c
    nv2a_psh.c
    nv2a_uniforms.c
    nv2a_gl.c
    nv2a_gfx_gl.c
    nv2a_shader_cache.c
    nv2a_smaa.c                 # SMAA's sources and look-up textures (smaa/)
    nv2a_scale.c                # the internal resolution, and where its frame goes
    nv2a_fit.c                  # the frame put in the window, halved first if need be
    nv2a_wide.c                 # widescreen's maths: the squeeze, the 2D overlays, their clip
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The display's refreshes as the kernel counts them, for frame pacing
    # with an SDL window (and nothing without one).
    list(APPEND NV2A_GL_SOURCES nv2a_display_clock.c)
endif()

add_library(xbox_nv2a_gl STATIC ${NV2A_GL_SOURCES})

target_include_directories(xbox_nv2a_gl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../d3d
)

#
# How the context is obtained.
#
# EGL with a surfaceless display is the headless default: no window server
# needed, which is what a build box wants. The game's Linux build gives the
# window and its context to SDL2 instead:
#
#   cmake -DNV2A_GL_CONTEXT=sdl ..
#
if(WIN32 AND NOT DEFINED NV2A_GL_CONTEXT)
    # Windows has no EGL: the window and its context are SDL2's.
    set(NV2A_GL_CONTEXT "sdl")
elseif(NOT DEFINED NV2A_GL_CONTEXT)
    set(NV2A_GL_CONTEXT "egl")
endif()

find_package(OpenGL REQUIRED)

if(NV2A_GL_CONTEXT STREQUAL "sdl")
    find_package(SDL2 REQUIRED)
    target_compile_definitions(xbox_nv2a_gl PUBLIC NV2A_GL_USE_SDL=1)
    target_include_directories(xbox_nv2a_gl PUBLIC ${SDL2_INCLUDE_DIRS})
    if(TARGET SDL2::SDL2)
        target_link_libraries(xbox_nv2a_gl PUBLIC OpenGL::GL SDL2::SDL2 m)
    else()
        target_link_libraries(xbox_nv2a_gl PUBLIC OpenGL::GL ${SDL2_LIBRARIES} m)
    endif()
else()
    target_link_libraries(xbox_nv2a_gl PUBLIC OpenGL::GL EGL m)
endif()
if(WIN32)
    # opengl32.dll exports OpenGL 1.1 and nothing after it; the 3.3 core entry
    # points come through libepoxy, which asks the driver for them.
    find_path(EPOXY_INCLUDE_DIR epoxy/gl.h REQUIRED)
    find_library(EPOXY_LIBRARY epoxy REQUIRED)
    target_include_directories(xbox_nv2a_gl PUBLIC ${EPOXY_INCLUDE_DIR})
    target_link_libraries(xbox_nv2a_gl PUBLIC ${EPOXY_LIBRARY})
endif()
message(STATUS "nv2a_gl context: ${NV2A_GL_CONTEXT}")

target_compile_options(xbox_nv2a_gl PRIVATE -Wall -Wextra -O2)

# The in-game menu (F1, or Back on a controller), src/menu: drawn
# over this SDL window, so built only with it.
if(NV2A_GL_CONTEXT STREQUAL "sdl")
    set(_nv2a_gl_menu_default ON)
else()
    set(_nv2a_gl_menu_default OFF)
endif()
option(NV2A_GL_MENU "nv2a_gl: the in-game menu (Dear ImGui, src/menu)" ${_nv2a_gl_menu_default})
if(NV2A_GL_MENU)
    if(NOT NV2A_GL_CONTEXT STREQUAL "sdl")
        message(FATAL_ERROR "NV2A_GL_MENU needs NV2A_GL_CONTEXT=sdl")
    endif()
    # xbox_menu itself is added from the top level, after this directory.
    target_link_libraries(xbox_nv2a_gl PUBLIC xbox_menu)
    message(STATUS "nv2a_gl: in-game menu (Dear ImGui) on")
endif()
