#!/bin/bash
#
# CI's thread sanitizer job, in the build image with the tests' packages
# (.github/ci/docker.sh "linux tests" .github/ci/tsan.sh): the tests that
# start threads, built with clang's ThreadSanitizer (tests/run_all.py --tsan,
# which says what fails). llvm-symbolizer, from LLVM 14, names the functions
# in its reports.
set -euo pipefail
cd "$(dirname "$0")/../.."
PATH="/usr/lib/llvm-14/bin:$PATH" CC=clang python3 tests/run_all.py --tsan
