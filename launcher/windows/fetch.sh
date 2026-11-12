#!/bin/bash
#
# One of deps.sh's downloads, checked by its SHA-256:
#
#   launcher/windows/fetch.sh <file> <SHA-256> <URL>...
#
# A <file> that is there already isn't downloaded again, only checked. Each
# URL is a source of the same file: the SHA-256 says so, and a source that
# gives another file is passed over. The download is tried 5 times, with
# pauses of 10, 20, 30 and 40 s, as GitHub's release downloads time out now
# and then (.github/ci/fetch.py does the same), and each try goes to each
# source in turn, so a server that is down for longer (archive.ubuntu.com,
# which GitHub's runners couldn't find for at least half an hour on 11
# November 2026) costs one failed request a try. The file is written whole
# or not at all, and a line says which source gave it.
set -euo pipefail
file="${1:?the file}" sha="${2:?its SHA-256}"
shift 2
[ $# -gt 0 ] || { echo "usage: launcher/windows/fetch.sh <file> <SHA-256> <URL>..." >&2; exit 2; }
if [ ! -f "$file" ]; then
    for try in 1 2 3 4 5; do
        for url in "$@"; do
            # One request, given up after 120 s without a byte, so a server
            # that hangs doesn't keep the next source from its turn; why it
            # failed in one line.
            if python3 -c '
import shutil, sys, urllib.request
try:
    with urllib.request.urlopen(sys.argv[1], timeout=120) as answer, open(sys.argv[2], "wb") as f:
        shutil.copyfileobj(answer, f, 1 << 20)
except Exception as error:
    sys.exit("fetch.sh: %s: %s" % (sys.argv[1], error))' "$url" "$file.part"; then
                if echo "$sha  $file.part" | sha256sum -c --quiet - > /dev/null 2>&1; then
                    mv "$file.part" "$file"
                    echo "fetch.sh: ${file##*/} from $url"
                    break 2
                fi
                echo "fetch.sh: $url: not the file with SHA-256 $sha" >&2
            fi
            rm -f "$file.part"
        done
        [ "$try" -lt 5 ] || { echo "fetch.sh: ${file##*/}: 5 tries at each source failed" >&2; exit 1; }
        echo "fetch.sh: ${file##*/}: try $try of 5 failed, again in $((try * 10)) s" >&2
        sleep $((try * 10))
    done
fi
echo "$sha  $file" | sha256sum -c --quiet -
