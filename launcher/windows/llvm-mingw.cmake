# CMake toolchain file: the Windows engine, cross-built with llvm-mingw
# (https://github.com/mstorsjo/llvm-mingw) on Linux -- how the
# release is built (see package.sh).
#
#   cmake -S port -B build-win -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE="$PWD/launcher/windows/llvm-mingw.cmake" \
#         -DLLVM_MINGW=<llvm-mingw dir> -DWIN_DEPS=<deps prefix> ...
#
# LLVM_MINGW is the toolchain (the UCRT x86_64 build); WIN_DEPS the prefix SDL2
# (its MinGW development package, the x86_64-w64-mingw32 part) and libepoxy
# were installed into, with include/, lib/ and bin/. Either can also be given
# as an environment variable of the same name.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

foreach(_v LLVM_MINGW WIN_DEPS)
    if(NOT ${_v} AND DEFINED ENV{${_v}})
        set(${_v} "$ENV{${_v}}")
    endif()
endforeach()
if(NOT LLVM_MINGW)
    message(FATAL_ERROR "llvm-mingw.cmake: set LLVM_MINGW to the llvm-mingw directory")
endif()
# try_compile projects read this file again, without the cache.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW WIN_DEPS)

set(_triple x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   "${LLVM_MINGW}/bin/${_triple}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW}/bin/${_triple}-clang++")
set(CMAKE_RC_COMPILER  "${LLVM_MINGW}/bin/${_triple}-windres")
set(CMAKE_AR           "${LLVM_MINGW}/bin/llvm-ar")
set(CMAKE_RANLIB       "${LLVM_MINGW}/bin/llvm-ranlib")

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW}/${_triple}" ${WIN_DEPS})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
