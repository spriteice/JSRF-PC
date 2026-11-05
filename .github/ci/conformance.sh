#!/bin/bash
#
# CI's recompiler conformance job, in the 32-bit conformance image
# (.github/ci/docker.sh conformance .github/ci/conformance.sh): each of
# tools/conformance's snippets assembled by GCC and run on the CPU, then
# lifted by the recompiler and the C run beside it, and their results
# compared (python3 -m tools.conformance). The corpus phase, which needs
# MSVC, says it is skipped.
set -euo pipefail
cd "$(dirname "$0")/../.."
/opt/conformance/bin/python -m tools.conformance
