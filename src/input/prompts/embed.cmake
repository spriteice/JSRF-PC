# The button prompts' pictures, built into the engine: every PNG in this
# folder as bytes, in a C file prompt_art.c reads them from (prompt_art.h,
# prompt_art_files), by name without .png, in strcmp's order.
#
#   cmake -DSRC=<this folder> -DOUT=<the C file> -P embed.cmake
#
# src/input/CMakeLists.txt runs it as the build's step, and tests/prompts
# the same way.
if(NOT SRC OR NOT OUT)
    message(FATAL_ERROR "embed.cmake: -DSRC=<folder> -DOUT=<file>")
endif()
get_filename_component(SRC "${SRC}" ABSOLUTE)
get_filename_component(OUT "${OUT}" ABSOLUTE)
file(GLOB pngs RELATIVE "${SRC}" "${SRC}/*.png")
# CMake's sort is by the names' bytes, as strcmp's.
list(SORT pngs)
set(arrays "")
set(table "")
set(i 0)
foreach(png IN LISTS pngs)
    file(READ "${SRC}/${png}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
    string(REGEX REPLACE "\\.png$" "" name "${png}")
    string(APPEND arrays "static const unsigned char f${i}[] = {${hex}};\n")
    string(APPEND table "    { \"${name}\", f${i}, (unsigned)sizeof f${i} },\n")
    math(EXPR i "${i} + 1")
endforeach()
if(i EQUAL 0)
    message(FATAL_ERROR "embed.cmake: no PNG in ${SRC}")
endif()
file(WRITE "${OUT}.new"
    "/* Made by src/input/prompts/embed.cmake from src/input/prompts/: not to be edited. */\n"
    "#include \"prompt_art.h\"\n"
    "${arrays}"
    "const PromptArtFile prompt_art_files[] = {\n${table}};\n"
    "const int prompt_art_nfiles = ${i};\n")
# Only a change touches the file, so nothing is built again for nothing.
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUT}.new" "${OUT}")
file(REMOVE "${OUT}.new")
