#!/usr/bin/env python3
"""The compiler for tests/run_all.py --coverage: each call's line counts
named apart.

    tests/coverage_cc.py --coverage-counts=<dir> --coverage-compiler=<cc> <cc's arguments>

run_all.py --coverage makes CC and CXX this, with the test's counts folder
and its compiler (CC, or gcc; CXX, or g++). It runs the compiler with the
other arguments and -dumpdir <dir>/<prefix>-, the prefix new for each call,
so that each compile's counts (the .gcno file it writes, and the .gcda file
its program writes) have names no other compile's have. With one -dumpdir
for a whole test, a test that builds several programs from the same files
(as gl_backend builds 15 from src/nv2a_gl's, and developer two from
port/developer.c's) put each program's counts under the same names, and
GCC's runtime in CI (GCC 11) throws away counts written by a program from
another compile ("overwriting an existing profile data with a different
timestamp"): only the last program's were left. A program built once and
run several times still adds its runs up in one file, and
.github/ci/coverage.py adds up a file's lines from every compile of it.

Its own two options may stand anywhere among the arguments, as a run.py may
put one of its own straight after the compiler's name (developer's
-pthread). The compiler is a command, as CC is: shlex splits it.
"""

import os
import shlex
import sys
import uuid

COUNTS = "--coverage-counts="
COMPILER = "--coverage-compiler="


def command(argv):
    """The compiler's command for this call: argv, the arguments after this
    program's name, without its own two options, and the call's -dumpdir."""
    counts, compiler, rest = None, None, []
    for arg in argv:
        if arg.startswith(COUNTS):
            counts = arg[len(COUNTS):]
        elif arg.startswith(COMPILER):
            compiler = shlex.split(arg[len(COMPILER):])
        else:
            rest.append(arg)
    if not counts or not compiler:
        raise SystemExit("coverage_cc: usage: %s<dir> %s<compiler> <its arguments>"
                         % (COUNTS, COMPILER))
    return compiler + rest + ["-dumpdir", os.path.join(counts, uuid.uuid4().hex[:12] + "-")]


if __name__ == "__main__":
    cmd = command(sys.argv[1:])
    os.execvp(cmd[0], cmd)
