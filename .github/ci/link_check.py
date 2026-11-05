#!/usr/bin/env python3
"""Link the engine without the generated code, which CI doesn't have.

    python3 .github/ci/link_check.py [--log <file>] <gen dir> <build command>...

<gen dir> is the folder the engine was configured with (-DJSRF_GEN_DIR=),
holding only the toolkit's templates/runtime/recomp_types.h. The build
command is run once: the engine's own files must all compile, and the link
may lack only what the generated code defines (GENERATED below). Any other
error fails. Then <gen dir>/ci_stubs.c defines each of those as a function
that aborts, and the build command is run again, and must link: the result
is the engine with everything but the game's code, which package.sh can
package and launcher/check_package.py can check. It never runs a game.
That second build's output is printed; the first's only if it failed
otherwise. With --log, the first build's output, which has every compile's
warnings, is also written to <file>, for .github/ci/count_warnings.py.

Linkers stop listing undefined symbols at 20 by default (LLVM's lld), so the
engine is configured with -Wl,--error-limit=0 where the linker is lld.
"""

import os
import re
import subprocess
import sys

# What the generated code defines: each translated function, by address,
# with and without _gen, and its lookup table and entry point.
GENERATED = re.compile(r"sub_[0-9A-F]{8}(_gen)?|recomp_lookup|recomp_dispatch_init|xbe_entry_point")

# GNU ld, and lld for ELF and for Windows.
UNDEFINED = re.compile(r"undefined reference to [`']([^'`]+)'|undefined symbol: ([^\s(]+)")


def build(cmd):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


def main(argv):
    log = None
    if argv[:1] == ["--log"]:
        log, argv = argv[1:2], argv[2:]
    if len(argv) < 2 or log == []:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    gen, cmd = argv[0], argv[1:]
    stubs = os.path.join(gen, "ci_stubs.c")
    if os.path.exists(stubs):
        os.remove(stubs)

    code, out = build(cmd)
    if log:
        with open(log[0], "w") as f:
            f.write(out)
    missing = sorted({a or b for a, b in UNDEFINED.findall(out)})
    other = [s for s in missing if not GENERATED.fullmatch(s)]
    if code == 0:
        print("link_check: linked with no generated code at all: is %s really empty?" % gen)
        return 1
    if not missing or other:
        print(out)
        if other:
            print("link_check: the link lacks %d symbols the generated code doesn't define:" % len(other))
            for s in other:
                print("  " + s)
        else:
            print("link_check: the build failed before the link")
        return 1

    with open(stubs, "w") as f:
        f.write("/* Made by .github/ci/link_check.py for CI: what the generated code\n"
                " * defines, so that the engine links without it. Never called. */\n"
                "#include <stdlib.h>\n")
        for s in missing:
            f.write("void %s(void) { abort(); }\n" % s)
    code, out = build(cmd)
    print(out)
    if code != 0:
        print("link_check: the build failed with the %d stubs in %s" % (len(missing), stubs))
        return 1
    print("link_check: the link lacked only the generated code's %d symbols, "
          "and links with them stubbed" % len(missing))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
