# Xbox Input → the host's pads, through SDL2's GameController API

# The button prompts' pictures (prompt_art.h): the PNGs in prompts/, built in
# as a C file made from them, and decoded with miniz (third_party/miniz,
# only its inflate used).
file(GLOB _prompt_pngs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/prompts/*.png)
set(_prompt_data ${CMAKE_CURRENT_BINARY_DIR}/prompt_art_data.c)
add_custom_command(OUTPUT ${_prompt_data}
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/prompts -DOUT=${_prompt_data}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/prompts/embed.cmake
    DEPENDS ${_prompt_pngs} ${CMAKE_CURRENT_SOURCE_DIR}/prompts/embed.cmake
    COMMENT "The button prompts' pictures, built in")
set(_miniz ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/miniz/miniz.c)

add_library(xbox_input STATIC xinput_device.c input_layout.c input_remap.c input_prompts.c
            prompt_art.c ${_prompt_data} ${_miniz} rumble_hold.c input_press.c stick_shape.c)
# miniz's own warnings aren't ours; it reads no file and writes no archive.
set_source_files_properties(${_miniz} PROPERTIES COMPILE_OPTIONS "-w"
    COMPILE_DEFINITIONS "MINIZ_NO_STDIO;MINIZ_NO_ARCHIVE_APIS;MINIZ_NO_TIME")

target_include_directories(xbox_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(SDL2 REQUIRED)
target_link_libraries(xbox_input PUBLIC platform SDL2::SDL2)
# stick_shape.c's sqrt and lround.
target_link_libraries(xbox_input PRIVATE m)
# The layouts' pages in the in-game menu (input_remap.c), where there is one:
# the top level adds src/menu before this directory.
if(TARGET xbox_menu)
    target_link_libraries(xbox_input PRIVATE xbox_menu)
endif()

target_compile_options(xbox_input PRIVATE -Wall -Wextra -O2)
