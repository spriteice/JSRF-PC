#!/bin/sh
#
# The conformance image: a 32-bit x86 Linux, where tools/conformance runs
# each snippet on the CPU and its lifted C beside it (CI's Recompiler
# conformance tests). Run as root in a fresh container of BASE, below
# (Alpine 3.24.1 for i386):
#
#   launcher/image-386.sh conformance
#
# launcher/images.sh runs it, as it runs image.sh for the build images, and
# keeps the image on GitHub's registry, named for this file's SHA-256 and
# made with --platform linux/386. A 64-bit x86 kernel runs it as it is.
#
# It installs the packages below, each at the version named: GCC and
# binutils, the assembler and compiler the snippets go through; bash, for
# .github/ci/docker.sh; and Python, with capstone (the version
# .github/ci/tools-requirements.txt has) in /opt/conformance, by its
# SHA-256. Alpine's archive keeps only each package's newest version, so
# when one moves on this file needs its new version, which names a new
# image; the published one stays as it was.
set -eu

# The base image, by digest, which launcher/images.sh reads from here.
BASE=i386/alpine:3.24.1@sha256:95a35dbffc3da87221f8b4eea3ed90cb52c634fedfdf4d22f3eb50e8883656cd

[ "$*" = conformance ] || { echo "usage: launcher/image-386.sh conformance" >&2; exit 2; }
[ "$(cat /etc/alpine-release)" = 3.24.1 ] || { echo "image-386.sh: Alpine 3.24.1 only" >&2; exit 1; }
[ "$(apk --print-arch)" = x86 ] || { echo "image-386.sh: i386 only (--platform linux/386)" >&2; exit 1; }

apk add --no-cache gcc=15.2.0-r5 musl-dev=1.2.6-r3 binutils=2.45.1-r1 \
    python3=3.14.10-r1 bash=5.3.9-r1
python3 -m venv /opt/conformance
cat > /tmp/requirements.txt <<'EOF'
capstone==5.0.9 --hash=sha256:0cc3b1ec319ab0530efc6b181491b77dd3373c81b6dbcf2f05a80e3d8dd61d5e
EOF
/opt/conformance/bin/pip install --quiet --no-cache-dir --require-hashes -r /tmp/requirements.txt
rm /tmp/requirements.txt
echo "image-386.sh: $* on ${BASE%@*}: gcc $(gcc -dumpfullversion), $(/opt/conformance/bin/python -V)," \
    "capstone $(/opt/conformance/bin/python -c 'import importlib.metadata as m; print(m.version("capstone"))')"
