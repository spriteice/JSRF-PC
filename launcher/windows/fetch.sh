#!/bin/bash
#
# One of deps.sh's downloads, checked by its SHA-256:
#
#   launcher/windows/fetch.sh <file> <SHA-256> <URL>
#
# A <file> that is there already isn't downloaded again, only checked. The
# download is tried 5 times, with pauses of 10, 20, 30 and 40 s, as GitHub's
# release downloads time out now and then (.github/ci/fetch.py does the
# same).
set -euo pipefail
file="${1:?the file}" sha="${2:?its SHA-256}" url="${3:?its URL}"
if [ ! -f "$file" ]; then
    for try in 1 2 3 4 5; do
        python3 -c 'import sys, urllib.request; urllib.request.urlretrieve(sys.argv[1], sys.argv[2])' \
            "$url" "$file.part" && break
        rm -f "$file.part"
        [ "$try" -lt 5 ] || { echo "fetch.sh: $url: 5 tries failed" >&2; exit 1; }
        echo "fetch.sh: $url: try $try of 5 failed, again in $((try * 10)) s" >&2
        sleep $((try * 10))
    done
    mv "$file.part" "$file"
fi
echo "$sha  $file" | sha256sum -c --quiet -
