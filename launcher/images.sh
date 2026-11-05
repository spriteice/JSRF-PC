#!/bin/bash
#
# The build images, made by launcher/image.sh and kept on GitHub's registry,
# so that a build or a CI job pulls its image instead of making it from
# Ubuntu's snapshot archive each time:
#
#   launcher/images.sh name "<groups>"     print the image's name
#   launcher/images.sh get "<groups>"      have the image here, and print its name
#   launcher/images.sh publish "<groups>"  make it and push it, unless it's there
#
# <groups> are image.sh's, as in "linux tests", or "conformance" alone, the
# 32-bit image launcher/image-386.sh makes (linux/386) for the recompiler's
# conformance tests. An image is named for its groups and for its recipe's
# SHA-256 (image.sh's, or image-386.sh's), which covers everything that
# makes it: the base image (the recipe's BASE), the snapshot and every
# package's version. So a change to a recipe names new images, and an image
# is never replaced once published. Each carries the same two facts as
# labels, which get checks.
#
# get uses the image if Docker has it, or pulls it. If it isn't published
# (image.sh changed in a commit not yet on main; CI's first job publishes
# it on the push to main) or the registry can't be reached, it makes the
# image from BASE with image.sh, as publish does. On a machine that needs more (a
# proxy), CI_BASE names another base image for image.sh's (not the conformance
# image's) and CI_DOCKER_ARGS adds arguments to docker create; an image made
# that way is kept here and never published.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
REGISTRY=ghcr.io/spriteice/jsrf-pc-build

usage() {
    echo "usage: launcher/images.sh name|get|publish \"<launcher/image.sh groups>\"|conformance" >&2
    exit 2
}

action="${1:-}"
groups="${2:-}"
[ $# = 2 ] && [ -n "$groups" ] || usage
recipe="$repo/launcher/image.sh" platform=() ci_base="${CI_BASE:-}"
if [ "$groups" = conformance ]; then
    recipe="$repo/launcher/image-386.sh" platform=(--platform linux/386) ci_base=
else
    for group in $groups; do
        case "$group" in linux|windows|tests) ;; *) usage ;; esac
    done
fi
sha="$(sha256sum "$recipe" | cut -d' ' -f1)"
base="$(sed -n 's/^BASE=//p' "$recipe")"
[ -n "$base" ] || { echo "images.sh: no BASE= line in ${recipe#"$repo"/}" >&2; exit 1; }
name="$REGISTRY:${groups// /-}-$sha"

# The image's labels match this image.sh and these groups.
check() {
    local got
    got="$(docker image inspect -f '{{index .Config.Labels "jsrf-pc.image-sh"}} {{index .Config.Labels "jsrf-pc.groups"}}' "$name")"
    if [ "$got" != "$sha $groups" ]; then
        echo "images.sh: $name is labelled '$got', not '$sha $groups'" >&2
        exit 1
    fi
}

# Make the image: image.sh in a fresh container of the base, committed with
# its labels. Its output goes to stderr, and its last line says what it
# installed.
make_image() {
    local from="${ci_base:-$base}" cid log
    log="$(mktemp)"
    # shellcheck disable=SC2086
    cid="$(docker create "${platform[@]}" ${CI_DOCKER_ARGS:-} -v "$recipe:/image.sh:ro" "$from" /image.sh $groups)"
    if ! docker start -a "$cid" > "$log" 2>&1; then
        cat "$log" >&2
        docker rm "$cid" > /dev/null
        rm -f "$log"
        echo "images.sh: ${recipe##*/} $groups failed in $from" >&2
        exit 1
    fi
    tail -n 1 "$log" >&2
    rm -f "$log"
    docker commit \
        --change "LABEL jsrf-pc.image-sh=$sha" \
        --change "LABEL jsrf-pc.groups=\"$groups\"" \
        --change "LABEL org.opencontainers.image.source=https://github.com/spriteice/JSRF-PC" \
        --change 'CMD ["bash"]' \
        "$cid" "$name" > /dev/null
    docker rm "$cid" > /dev/null
}

case "$action" in
name)
    echo "$name"
    ;;
get)
    if docker image inspect "$name" > /dev/null 2>&1; then
        echo "images.sh: $name, kept here" >&2
    elif docker pull -q "${platform[@]}" "$name" > /dev/null 2>&1; then
        echo "images.sh: $name, pulled" >&2
    else
        echo "images.sh: $name isn't published, or the registry can't be reached: making it" >&2
        make_image
    fi
    check
    echo "$name"
    ;;
publish)
    if docker manifest inspect "$name" > /dev/null 2>&1; then
        echo "images.sh: $name is published already"
        exit 0
    fi
    make_image
    check
    docker push -q "$name"
    echo "images.sh: $name published"
    ;;
*)
    usage
    ;;
esac
