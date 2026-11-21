"""The engine's build refuses a generated recomp_types.h that isn't the runtime's.

The pipeline copies templates/runtime/recomp_types.h into the generated code
(translator.py, beside recomp_funcs.h), and the engine compiles against that
copy. A kept copy made before a change to the header would build the engine
with the old runtime and nothing to say so, so port/CMakeLists.txt compares
the two when it configures. These configure the real port/CMakeLists.txt
against a folder holding only the header's copy, as CI's jobs do: a stale copy
stops it at the check, with its message; the same copy, or the same with CRLF
line endings, goes past it (the version line comes after the check), whatever
fails later for want of SDL.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
HEADER = REPO / "templates" / "runtime" / "recomp_types.h"
SAYS = "isn't templates/runtime/recomp_types.h"
PAST_IT = "JSRF-PC version: types-check"


def configure(tmp_path, copy):
    if shutil.which("cmake") is None:
        pytest.skip("no cmake")
    gen = tmp_path / "gen" / "recomp" / "gen"
    gen.mkdir(parents=True)
    (gen / "recomp_types.h").write_bytes(copy)
    done = subprocess.run(
        ["cmake", "-S", str(REPO / "port"), "-B", str(tmp_path / "build"),
         "-DJSRF_GEN_DIR=%s" % gen, "-DJSRF_VERSION=types-check"],
        capture_output=True, text=True, timeout=300)
    # CMake wraps its messages at spaces: the words, one space apart.
    return done.returncode, " ".join((done.stdout + done.stderr).split())


def test_a_stale_copy_stops_the_build(tmp_path):
    rc, out = configure(tmp_path, b"/* an older header */\n" + HEADER.read_bytes())
    assert rc != 0
    assert SAYS in out, out
    assert "docs/building-on-linux.md" in out, out
    assert PAST_IT not in out, out


def test_the_same_copy_goes_on(tmp_path):
    rc, out = configure(tmp_path, HEADER.read_bytes())
    assert SAYS not in out, out
    assert PAST_IT in out, out


def test_line_endings_dont_count(tmp_path):
    lf = HEADER.read_bytes().replace(b"\r\n", b"\n")
    rc, out = configure(tmp_path, lf.replace(b"\n", b"\r\n"))
    assert SAYS not in out, out
    assert PAST_IT in out, out
