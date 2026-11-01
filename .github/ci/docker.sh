#!/bin/bash
#
# Run one of CI's job scripts in a fresh build image, as GitHub does:
#
#   .github/ci/docker.sh "<launcher/image.sh groups>" <command>...
#
# as in .github/ci/docker.sh "linux tests" .github/ci/linux.sh gcc. The base
# is the build image's, by digest; image.sh sets it up, then the command runs
# with the repository at /src (read-only) and /work for what it builds. On a
# machine that needs more (a proxy), CI_BASE names another base image and
# CI_DOCKER_ARGS adds arguments to docker run.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
base="${CI_BASE:-ubuntu:22.04@sha256:076e7bf82319619f28dd0aa9c02ab34b055ad37100395b4f49a2ffba04fb5e11}"
groups="${1:?the launcher/image.sh groups}"
shift
# shellcheck disable=SC2086
exec docker run --rm ${CI_DOCKER_ARGS:-} -v "$repo:/src:ro" -w /src \
    -e PYTHONDONTWRITEBYTECODE=1 "$base" bash -c '
        mkdir -p /work
        launcher/image.sh '"$groups"' > /work/image.log 2>&1 || { cat /work/image.log; exit 1; }
        tail -n 1 /work/image.log
        exec "$@"' bash "$@"
