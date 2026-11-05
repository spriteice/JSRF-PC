# Xbox Input → the host's pads, through SDL2's GameController API

add_library(xbox_input STATIC xinput_device.c)

target_include_directories(xbox_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(SDL2 REQUIRED)
target_link_libraries(xbox_input PUBLIC platform SDL2::SDL2)

target_compile_options(xbox_input PRIVATE -Wall -Wextra -O2)
