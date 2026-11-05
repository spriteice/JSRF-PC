#!/usr/bin/env python3
"""Count the compilers' warnings in our code, for CI's ratchet.

    python3 .github/ci/count_warnings.py <job> <build log>

<build log> is the build's output, with every compile's warnings (as
.github/ci/link_check.py --log writes it). Each warning in a file of this
repository is counted once per place (its file, line, column and text), as a
header's warning shows in every file that includes it. Third-party code
(third_party/), the generated code and SDL's source aren't ours, and aren't
counted.

.github/ci/warnings.txt gives each job's count. The job fails if its count is
above it, so a new warning fails CI. A count below it passes, and says so:
the line is lowered in a later change to CI, so a code commit never has to
change .github/. At 0, any warning fails the job, as -Werror would.
"""

import os
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2]
NAME = ".github/ci/warnings.txt"
COUNTS = root / NAME
# GCC's and clang's own form: file:line:column: warning: text [-Wflag]
WARNING = re.compile(r"^(?P<path>[^\s:][^:]*):(?P<line>\d+):(?P<col>\d+): warning: (?P<text>.*)$")
NOT_OURS = ("third_party",)


def ours(path):
    """The path from the repository's root, for a file of ours, or None."""
    p = pathlib.Path(os.path.normpath(path))
    if not p.is_absolute():
        p = pathlib.Path(os.path.normpath(root / p))
    try:
        rel = p.relative_to(root)
    except ValueError:
        return None
    if rel.parts and rel.parts[0] in NOT_OURS:
        return None
    return rel.as_posix()


def warnings(log):
    """Each distinct warning in our code, as 'path:line:col: text', sorted."""
    found = set()
    for line in log.splitlines():
        m = WARNING.match(line.rstrip())
        if not m:
            continue
        rel = ours(m.group("path"))
        if rel:
            found.add("%s:%s:%s: %s" % (rel, m.group("line"), m.group("col"), m.group("text").strip()))
    return sorted(found, key=lambda w: (w.split(":")[0], [int(n) for n in w.split(":")[1:3]], w))


def counts():
    """{job: count} from .github/ci/warnings.txt."""
    out = {}
    for n, raw in enumerate(COUNTS.read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        parts = raw.split()
        if len(parts) != 2 or not parts[1].isdigit() or parts[0] in out:
            raise SystemExit("%s:%d: not '<job> <count>'" % (NAME, n))
        out[parts[0]] = int(parts[1])
    return out


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    job, log = argv
    allowed = counts()
    if job not in allowed:
        print("warnings: %s has no line for %s" % (NAME, job))
        return 1
    found = warnings(pathlib.Path(log).read_text(encoding="utf-8", errors="replace"))
    for w in found:
        print("  " + w)
    n, limit = len(found), allowed[job]
    print("warnings: %d in our code for %s, and %s allows %d" % (n, job, NAME, limit))
    if n > limit:
        print("warnings: %d more than allowed: fix the new ones (listed above with the rest)" % (n - limit))
        return 1
    if n < limit:
        print("warnings: %d fewer than allowed: %s's line in %s can come down to %d"
              % (limit - n, job, NAME, n))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
