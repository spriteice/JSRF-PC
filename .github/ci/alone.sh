#!/bin/bash
#
# The cloud's run on its own, after its jobs ran side by side before a commit
# (docs/build-and-test-reference.md, CI, Before a commit), in the build image
# with the tests' packages (.github/ci/docker.sh "linux tests"
# .github/ci/alone.sh): the engine tests that start threads or measure time,
# which load on the machine can change and the GCC and clang jobs left out
# there (linux.sh --side-by-side), built with GCC and then with clang, as
# those jobs build them (tests/run_all.py --alone). GitHub's runs don't need
# it, as each of their jobs has a machine to itself.
set -euo pipefail
cd "$(dirname "$0")/../.."
CC=gcc CXX=g++ python3 tests/run_all.py --alone
CC=clang python3 tests/run_all.py --alone
