#!/bin/bash
#
# CI's Linux jobs, in the build image with the tests' packages
# (.github/ci/docker.sh "linux tests" .github/ci/linux.sh gcc|clang):
#
#   gcc     the engine built with GCC, linked as far as it can be without the
#           game; the engine tests; the tool tests, and CI's own (test_ci.py)
#   clang   the release package, made by launcher/linux/build.sh and checked
#           by launcher/check_package.py; the engine tests built with clang
#
# The generated code is the game's, so CI hasn't got it. The engine is built
# against the toolkit's header alone, and .github/ci/link_check.py checks
# that the link lacks only what the generated code defines, then stubs that
# out so the rest links.
set -euo pipefail
cd "$(dirname "$0")/../.."
work=/work
gen="$work/gen/recomp/gen"
mkdir -p "$gen"
cp templates/runtime/recomp_types.h "$gen/"
# SDL's source, checked by the SHA-256 launcher/linux/sdl.sh checks it by.
python3 .github/ci/fetch.py \
    https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz \
    "$work/SDL2-2.30.12.tar.gz" "$(sed -n 's/^sha256=//p' launcher/linux/sdl.sh)"

case "${1:?gcc or clang}" in
gcc)
    launcher/linux/sdl.sh "$work/SDL2-2.30.12.tar.gz" "$work/sdl"
    CC=gcc CXX=g++ cmake -S port -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DNV2A_GL_CONTEXT=sdl -DOpenGL_GL_PREFERENCE=LEGACY -DCMAKE_PREFIX_PATH="$work/sdl" \
        -DJSRF_GEN_DIR="$gen" -DJSRF_VERSION=ci > "$work/cmake.log" \
        || { cat "$work/cmake.log"; exit 1; }
    python3 .github/ci/link_check.py "$gen" cmake --build "$work/build"
    python3 tests/run_all.py
    python3 -m venv "$work/venv"
    "$work/venv/bin/pip" install --quiet --require-hashes -r .github/ci/tools-requirements.txt
    "$work/venv/bin/python" -m pytest -q -p no:cacheprovider tools .github/ci/test_ci.py
    ;;
clang)
    python3 .github/ci/link_check.py "$gen" \
        launcher/linux/build.sh "$work" -DJSRF_GEN_DIR="$gen" -DJSRF_VERSION=ci
    CC=clang python3 tests/run_all.py
    ;;
*)
    echo "usage: .github/ci/linux.sh gcc|clang" >&2
    exit 2
    ;;
esac
