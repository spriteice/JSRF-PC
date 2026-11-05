#!/bin/bash
#
# Run one of CI's job scripts in the build image, as GitHub does:
#
#   .github/ci/docker.sh "<launcher/image.sh groups>" <command>...
#
# as in .github/ci/docker.sh "linux tests" .github/ci/linux.sh gcc. The image
# is the published one for this launcher/image.sh, pulled, or made here if it
# isn't published yet (launcher/images.sh get). The command runs in a fresh
# container of it, with the repository at /src (read-only) and /work for what
# it builds. On a machine that needs more (a proxy), CI_BASE names another
# base image for an image made here, and CI_DOCKER_ARGS adds arguments to
# docker run and docker create.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
groups="${1:?the launcher/image.sh groups}"
shift
image="$("$repo/launcher/images.sh" get "$groups")"
echo "image: $(docker image inspect -f '{{if .RepoDigests}}{{index .RepoDigests 0}}{{else}}{{.Id}}, made here{{end}}' "$image")"
# shellcheck disable=SC2086
exec docker run --rm ${CI_DOCKER_ARGS:-} -v "$repo:/src:ro" -w /src \
    -e PYTHONDONTWRITEBYTECODE=1 "$image" bash -c 'mkdir -p /work; exec "$@"' bash "$@"
