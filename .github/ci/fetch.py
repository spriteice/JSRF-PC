#!/usr/bin/env python3
"""Download a file CI needs, and check it by its SHA-256.

    python3 .github/ci/fetch.py <URL> <file> <SHA-256>

GitHub's release downloads time out now and then (an HTTP 504), so a
server's error (5xx, 408 or 429), a dropped connection or a timeout is
tried again: 5 tries in all, with pauses of 10, 20, 30 and 40 s between
them. Any other answer, such as a 404, fails at once, and so does a file
whose SHA-256 is wrong. The file is written whole or not at all.
llvm_mingw.py fetches with it too, and launcher/windows/fetch.sh tries
deps.sh's downloads the same way.
"""

import hashlib
import http.client
import os
import shutil
import sys
import time
import urllib.error
import urllib.request

TRIES = 5
PAUSE = 10      # seconds, times the number of the try that failed
TIMEOUT = 120   # seconds without a byte before a try is given up


def passing(error):
    """Whether a failed try is worth another: the server's trouble, not the request's."""
    if isinstance(error, urllib.error.HTTPError):
        return error.code >= 500 or error.code in (408, 429)
    return isinstance(error, (urllib.error.URLError, http.client.HTTPException, ConnectionError, TimeoutError))


def fetch(url, path, sha256, tries=TRIES, pause=PAUSE):
    part = path + ".part"
    for n in range(1, tries + 1):
        try:
            with urllib.request.urlopen(url, timeout=TIMEOUT) as answer, open(part, "wb") as f:
                shutil.copyfileobj(answer, f, 1 << 20)
            break
        except Exception as error:
            if os.path.exists(part):
                os.remove(part)
            if n == tries or not passing(error):
                raise
            print("fetch: %s: try %d of %d failed (%s), again in %d s" % (url, n, tries, error, pause * n),
                  file=sys.stderr, flush=True)
            time.sleep(pause * n)
    digest = hashlib.sha256()
    with open(part, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    if digest.hexdigest() != sha256:
        os.remove(part)
        raise ValueError("%s has SHA-256 %s, not %s" % (url, digest.hexdigest(), sha256))
    os.replace(part, path)


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    try:
        fetch(*argv)
    except (OSError, ValueError, urllib.error.URLError, http.client.HTTPException) as error:
        print("fetch: %s: %s" % (argv[0], error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
