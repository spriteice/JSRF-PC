#!/bin/bash
#
# CI's address and undefined-behaviour sanitizer job, in the build image with
# the tests' packages (.github/ci/docker.sh "linux tests" .github/ci/asan.sh):
# every engine test, built with GCC's AddressSanitizer and
# UndefinedBehaviorSanitizer (tests/run_all.py --asan, which says what
# fails). GCC, as three of the renderer's tests build only with it.
set -euo pipefail
cd "$(dirname "$0")/../.."
CC=gcc CXX=g++ python3 tests/run_all.py --asan
