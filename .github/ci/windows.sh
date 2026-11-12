#!/bin/bash
#
# CI's Windows build, cross-compiled in the Windows build image
# (.github/ci/docker.sh windows .github/ci/windows.sh): the release package,
# made by launcher/windows/build.sh and checked by launcher/check_package.py,
# with the engine linked as far as it can be without the game (see
# .github/ci/linux.sh), then where deps.sh's downloads came from, and its
# warnings in our code counted (.github/ci/count_warnings.py). lld stops
# listing what is missing at 20 unless told.
set -euo pipefail
cd "$(dirname "$0")/../.."
work=/work
gen="$work/gen/recomp/gen"
mkdir -p "$gen"
cp templates/runtime/recomp_types.h "$gen/"
python3 .github/ci/link_check.py --log "$work/build.log" "$gen" launcher/windows/build.sh "$work" ci \
    -DJSRF_GEN_DIR="$gen" -DCMAKE_EXE_LINKER_FLAGS=-Wl,--error-limit=0
# launcher/windows/fetch.sh's lines, from the first build (link_check.py),
# which made the downloads and is printed only if it fails.
grep '^fetch\.sh: ' "$work/build.log" || true
python3 .github/ci/count_warnings.py windows "$work/build.log"
