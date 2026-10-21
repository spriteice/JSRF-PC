# The in-game menu (F1, or Back on a controller): Dear ImGui
# (third_party/imgui) over nv2a_gl's SDL window, with its own SDL2 and
# OpenGL 3 backends. Added from the top level when NV2A_GL_MENU is on
# (src/nv2a_gl/CMakeLists.txt): only that window can show it.
#
# The rest of the runtime adds its rows through menu.h, which is C. Linking
# xbox_menu defines XBOXRECOMP_MENU, so a module that may be built without
# the menu adds them under #if defined(XBOXRECOMP_MENU).

set(_imgui ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/imgui)
add_library(xbox_menu STATIC
    menu.cpp
    menu_sdl.cpp
    ${_imgui}/imgui.cpp ${_imgui}/imgui_draw.cpp
    ${_imgui}/imgui_tables.cpp ${_imgui}/imgui_widgets.cpp
    ${_imgui}/backends/imgui_impl_sdl2.cpp ${_imgui}/backends/imgui_impl_opengl3.cpp)
set_target_properties(xbox_menu PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(xbox_menu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(xbox_menu PRIVATE ${_imgui})
target_compile_definitions(xbox_menu PUBLIC XBOXRECOMP_MENU=1)
find_package(SDL2 REQUIRED)
target_link_libraries(xbox_menu PRIVATE SDL2::SDL2 ${CMAKE_DL_LIBS})
# Nothing here may need the C++ runtime library: the engine still links
# against libc alone.
target_compile_options(xbox_menu PRIVATE
    -fno-exceptions -fno-rtti -fno-threadsafe-statics -O2)
target_compile_definitions(xbox_menu PRIVATE
    IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS
    IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
    $<$<BOOL:${NV2A_GLES}>:IMGUI_IMPL_OPENGL_ES3>)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # C++ compilers define _GNU_SOURCE, with which glibc 2.38's headers
    # turn sscanf into __isoc23_sscanf@GLIBC_2.38 -- past the 2.34 floor.
    target_compile_options(xbox_menu PRIVATE -U_GNU_SOURCE -D_DEFAULT_SOURCE)
endif()
