"""launcher/firststart/readme.py on the packages' own README.txt: the package
without the engine's says so where the one with it says otherwise (the
opening, a section on the first start before Controls, the legal note and the
tools' licences), and a README that no longer has a place it changes stops it.
"""
import pathlib
import subprocess
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def _run(system, path):
    return subprocess.run([sys.executable, str(HERE / "readme.py"), system, str(path)],
                          capture_output=True, text=True, encoding="utf-8")


def _paragraph(text, start):
    """The paragraph that starts with `start`, as one line."""
    i = text.index(start)
    j = text.find("\n\n", i)
    return " ".join(text[i:j if j >= 0 else len(text)].split())


@pytest.mark.parametrize("system", ["linux", "windows"])
def test_readme(system):
    src = ROOT / "launcher" / system / "README.txt"
    p = _run(system, src)
    assert p.returncode == 0, p.stderr
    out, before = p.stdout, src.read_text(encoding="utf-8")
    # The opening: no engine in the package, the first start builds it.
    opening = _paragraph(out, "You need your own copy of the game")
    assert "none of its code" in opening and "The first time it starts, it builds the engine" in opening
    # The first start, its own section, right before Controls.
    first = out.index("The first start\n---------------\n")
    assert first < out.index("\nControls\n--------\n")
    assert "No matching binary detected. Build now with local tools?" in out
    assert "No matching binary detected. Download tools now and build?" in out
    assert ("JSRF-PC-tools-windows-x86_64.zip" if system == "windows"
            else "JSRF-PC-tools-linux-x86_64.tar.gz") in out
    # Legal: the package holds none of the game's code.
    legal = _paragraph(out, "Jet Set Radio Future is (c) SEGA.")
    assert "nothing of the game" in legal and "machine-translated" in legal
    assert "This package contains the engine" not in out
    # The tools' licences, before the pointer to the folder.
    lic = _paragraph(out, "Licences:")
    assert lic.index("Python under the PSF licence") < lic.index("See the licences")
    # The rest as it was: every other paragraph of the original is still there.
    changed = ("You need your own copy of the game", "Jet Set Radio Future is (c) SEGA.", "Licences:")
    for par in before.split("\n\n"):
        body = par.strip("\n")
        if body and not any(c in " ".join(body.split()) for c in changed):
            assert body in out
    # Rewrapped at 78 columns, one newline at the end.
    for start in ("You need your own copy of the game", "Jet Set Radio Future is (c) SEGA.", "Licences:"):
        i = out.index(start)
        j = out.find("\n\n", i)
        for line in out[i:j if j >= 0 else len(out)].split("\n"):
            assert len(line) <= 78, line
    assert out.endswith("\n") and not out.endswith("\n\n")


def test_readme_crlf(tmp_path):
    src = ROOT / "launcher" / "windows" / "README.txt"
    crlf = tmp_path / "README.txt"
    crlf.write_bytes(src.read_text(encoding="utf-8").replace("\n", "\r\n").encode("utf-8"))
    assert _run("windows", crlf).stdout == _run("windows", src).stdout


@pytest.mark.parametrize("cut, names", [
    ("\nControls\n--------\n", "controls"),
    ("Jet Set Radio Future is (c) SEGA.", "legal"),
    ("You need your own copy of the game", "opening"),
    ("Licences:", "licences"),
])
def test_readme_stops_when_a_place_is_gone(tmp_path, cut, names):
    text = (ROOT / "launcher" / "linux" / "README.txt").read_text(encoding="utf-8")
    assert cut in text
    changed = tmp_path / "README.txt"
    changed.write_text(text.replace(cut, "\nSomething else\n--------------\n" if cut.startswith("\n") else "Other words."),
                       encoding="utf-8")
    p = _run("linux", changed)
    assert p.returncode != 0 and names in p.stderr
