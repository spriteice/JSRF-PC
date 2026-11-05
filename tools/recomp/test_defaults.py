"""
Self-check for the recompiler's default paths.

Run: py -3 tools/recomp/test_defaults.py

--gen-dir and --exclude-manual's defaults named src/game/recomp/gen and
src/game/recomp/recomp_manual.c, the toolkit's layout, which this repository
doesn't have: split output written without --gen-dir went to a folder the
build never reads, and --exclude-manual with no file scanned nothing, so the
functions written by hand were generated too and linked twice. They must be
where the build reads them.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from tools.recomp import __main__ as recomp  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def test_manual_default_is_the_file():
    assert os.path.isfile(os.path.join(ROOT, recomp.DEFAULT_MANUAL)), recomp.DEFAULT_MANUAL


def test_gen_dir_default_is_the_builds():
    """port/CMakeLists.txt reads the generated code from its JSRF_GEN_DIR."""
    with open(os.path.join(ROOT, "port", "CMakeLists.txt"), encoding="utf-8") as fh:
        m = re.search(r'set\(JSRF_GEN_DIR "\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^"]+)"', fh.read())
    assert m, "port/CMakeLists.txt no longer sets JSRF_GEN_DIR as this test reads it"
    assert os.path.normpath(recomp.DEFAULT_GEN_DIR) == \
        os.path.normpath(os.path.join(ROOT, "port", m.group(1)))


if __name__ == "__main__":
    test_manual_default_is_the_file()
    test_gen_dir_default_is_the_builds()
    print("all passed (2 checks)")
