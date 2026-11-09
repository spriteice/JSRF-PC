"""The pipeline writes LF on every system.

The five tools that turn default.xbe into the generated code (docs/
building-on-linux.md, step 4) run on the player's PC at the first start, on
Windows too, where a text file opened without newline= ends its lines with
CR LF. The generated code must be the same bytes everywhere, as its hash is
checked against the release's (launcher/kit/build_engine.py), so every file
they write as text says newline="\\n". This reads their source: an open() for
writing text, or a write_text(), without it fails here by file and line.
"""
import ast
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parent
PIPELINE = ["xbe_parser", "disasm", "func_id", "abi_analysis", "recomp"]


def _mode(call):
    if len(call.args) > 1 and isinstance(call.args[1], ast.Constant):
        return call.args[1].value
    for k in call.keywords:
        if k.arg == "mode" and isinstance(k.value, ast.Constant):
            return k.value.value
    return "r"


def _text_writes_without_newline(path):
    tree = ast.parse(path.read_text(encoding="utf-8"), str(path))
    found = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        f = node.func
        name = f.id if isinstance(f, ast.Name) else f.attr if isinstance(f, ast.Attribute) else ""
        has_newline = any(k.arg == "newline" for k in node.keywords)
        if name == "open":
            mode = _mode(node)
            if isinstance(mode, str) and "b" not in mode and set(mode) & set("wax+") and not has_newline:
                found.append("%s:%d: open(..., %r) without newline=" % (path.relative_to(ROOT.parent), node.lineno, mode))
        elif name == "write_text" and not has_newline:
            found.append("%s:%d: write_text() without newline=" % (path.relative_to(ROOT.parent), node.lineno))
    return found


@pytest.mark.parametrize("tool", PIPELINE)
def test_pipeline_writes_lf(tool):
    files = sorted(p for p in (ROOT / tool).rglob("*.py") if not p.name.startswith("test_"))
    assert files
    found = [f for p in files for f in _text_writes_without_newline(p)]
    assert not found, "\n".join(found)
