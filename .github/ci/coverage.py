#!/usr/bin/env python3
"""The engine tests' line coverage of our code, for information only.

    python3 .github/ci/coverage.py <counts dir>

<counts dir> is the folder tests/run_all.py --coverage kept each test's
counts in (GCC's .gcno and .gcda files, a folder for each test). gcov reads
each, and a line of a file in src/ or port/ counts as reached if any test
ran it. The report gives each such file's lines reached and lines with code,
and the files no test builds at all, then the totals. It is printed and
written to <counts dir>/coverage.txt. A test that ends without returning
from main (an abort, _exit, or a timeout) writes no counts, so its lines
count as not reached; and a test that builds a copy of a file's text (as
nv2a_queue builds pieces of src/kernel/kernel_mmio.c from its standard
input) counts towards the copy, not the file. A .gcno file gcov can't read
is named in the report and left out.
"""

import gzip
import json
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[2]
OURS = ("src", "port")
SOURCES = (".c", ".cpp")
# The generated code, where a checkout has it, isn't ours.
GENERATED = "port/recomp/gen/"


def ours(path, cwd):
    """The path from the repository's root, for a file of src/ or port/, or None."""
    p = pathlib.Path(os.path.normpath(pathlib.Path(cwd) / path))
    try:
        rel = p.relative_to(root)
    except ValueError:
        return None
    if not rel.parts or rel.parts[0] not in OURS or rel.as_posix().startswith(GENERATED):
        return None
    return rel.as_posix()


def gcov(note):
    """gcov's JSON for one .gcno (and its .gcda, when the test wrote one), or
    None if gcov can't read it. The name goes as ./name, as a program built
    from its standard input has counts named -.gcno."""
    r = subprocess.run(["gcov", "--json-format", "--stdout", "./" + note.name], cwd=note.parent,
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    out = r.stdout
    if out[:2] == b"\x1f\x8b":
        out = gzip.decompress(out)
    try:
        reports = [json.loads(line) for line in out.decode("utf-8").splitlines() if line.strip()]
    except ValueError:
        reports = []
    return reports if r.returncode == 0 and reports else None


def coverage(counts):
    """{path: (lines reached, lines with code)} over every test's counts, and
    the .gcno files gcov couldn't read."""
    lines, unread = {}, []
    for note in sorted(pathlib.Path(counts).rglob("*.gcno")):
        reports = gcov(note)
        if reports is None:
            unread.append(note.relative_to(counts).as_posix())
            continue
        for report in reports:
            cwd = report.get("current_working_directory", str(root))
            for f in report.get("files", []):
                rel = ours(f["file"], cwd)
                if not rel:
                    continue
                seen = lines.setdefault(rel, {})
                for line in f.get("lines", []):
                    n = line["line_number"]
                    seen[n] = seen.get(n, False) or line["count"] > 0
    return {p: (sum(v.values()), len(v)) for p, v in lines.items()}, unread


def main(argv):
    if len(argv) != 1:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    counts = pathlib.Path(argv[0])
    found, unread = coverage(counts)
    unbuilt = sorted(p.relative_to(root).as_posix() for d in OURS
                     for p in (root / d).rglob("*") if p.suffix in SOURCES
                     and not p.relative_to(root).as_posix().startswith(GENERATED)
                     and p.relative_to(root).as_posix() not in found)
    report = ["Line coverage of src/ and port/ by the engine tests "
              "(tests/run_all.py --coverage), for information only:", ""]
    for path in sorted(found):
        reached, total = found[path]
        report.append("%5.1f%%  %6d of %6d lines  %s" % (100.0 * reached / total if total else 0.0,
                                                       reached, total, path))
    reached = sum(r for r, _ in found.values())
    total = sum(t for _, t in found.values())
    report += ["", "Not built by any test (%d files):" % len(unbuilt)] + ["  " + p for p in unbuilt]
    if unread:
        report += ["", "Counts gcov couldn't read, left out (%d):" % len(unread)] + ["  " + p for p in unread]
    report += ["", "coverage: %d of %d lines with code reached (%.1f%%) in the %d files the tests build; "
               "%d more files of src/ and port/ aren't built by any test"
               % (reached, total, 100.0 * reached / total if total else 0.0, len(found), len(unbuilt))]
    text = "\n".join(report) + "\n"
    (counts / "coverage.txt").write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
