# The D3D8 headers the runtime still includes, and nothing to build:
# d3d8_xbox.h, the D3D8 types, and d3d8_swizzle.h, the NV2A's texture
# swizzling and DXT helpers (nv2a_gl and the pushbuffer executor). A title
# brings its own D3D (JSRF's is linked into its executable) and reaches the GPU
# through the pushbuffer, which nv2a_gl draws.
add_library(xbox_d3d8 INTERFACE)
target_include_directories(xbox_d3d8 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xbox_d3d8 INTERFACE platform)
