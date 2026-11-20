# Xbox Input → the host's pads, through SDL2's GameController API

add_library(xbox_input STATIC xinput_device.c input_layout.c input_remap.c rumble_hold.c input_press.c)

target_include_directories(xbox_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(SDL2 REQUIRED)
target_link_libraries(xbox_input PUBLIC platform SDL2::SDL2)
# The layouts' pages in the in-game menu (input_remap.c), where there is one:
# the top level adds src/menu before this directory.
if(TARGET xbox_menu)
    target_link_libraries(xbox_input PRIVATE xbox_menu)
endif()

target_compile_options(xbox_input PRIVATE -Wall -Wextra -O2)
