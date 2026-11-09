"""The kit's scripts without the game or the tools: build_engine.py's parts
(the generated code's hash, unpacking a wheel, how many compiles at once and
for how long, the commands filled in, the programs' environment, and refusing
any Python but the tools'), and set_expected.py putting a build's hashes in a
kit. The whole build runs with the game on a PC (launcher/kit/make_kit.sh,
launcher/firststart/package.sh, docs/release-checklist.md), and the setup that
runs it is tested by tests/setup.
"""
import hashlib
import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import zipfile

import pytest

HERE = pathlib.Path(__file__).resolve().parent


def _load(name):
    spec = importlib.util.spec_from_file_location("kit_" + name, HERE / (name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


be = _load("build_engine")


# ---- build_engine.py ----

def test_gen_hash_is_the_shell_formula(tmp_path):
    """As `find . -maxdepth 1 -type f -exec sha256sum {} + | LC_ALL=C sort -k2 |
    sha256sum` gives it: the measurements' hash (players-pc-build/measure)."""
    files = {"recomp_0000.c": b"int a;\n", "B.h": b"#pragma once\n", "recomp_types.h": b"/* t */\n",
             "a b.c": b"spaced\n"}
    for n, data in files.items():
        (tmp_path / n).write_bytes(data)
    (tmp_path / "sub").mkdir()
    (tmp_path / "sub" / "ignored.c").write_bytes(b"not counted\n")
    lines = sorted("%s  ./%s\n" % (hashlib.sha256(d).hexdigest(), n) for n, d in files.items())
    lines.sort(key=lambda l: l.split("  ", 1)[1].encode())
    want = hashlib.sha256("".join(lines).encode()).hexdigest()
    assert be.gen_hash(str(tmp_path)) == want
    if shutil.which("bash") and shutil.which("sha256sum") and sys.platform != "win32":
        shell = subprocess.run(
            ["bash", "-c", "find . -maxdepth 1 -type f -exec sha256sum {} + | LC_ALL=C sort -k2 | sha256sum"],
            cwd=tmp_path, capture_output=True, text=True, check=True).stdout.split()[0]
        assert shell == want


def _wheel(path, members):
    with zipfile.ZipFile(path, "w") as z:
        for name, data, mode in members:
            info = zipfile.ZipInfo(name)
            info.external_attr = mode << 16
            z.writestr(info, data)


def test_install_wheel_keeps_modes(tmp_path):
    wheel = tmp_path / "w.whl"
    _wheel(wheel, [("ziglang/__init__.py", b"", 0o644), ("ziglang/zig", b"#!/bin/sh\n", 0o755),
                   ("ziglang-0.13.0.dist-info/", b"", 0o755)])
    site = tmp_path / "site"
    site.mkdir()
    be.install_wheel(str(wheel), str(site))
    assert (site / "ziglang/__init__.py").is_file()
    assert (site / "ziglang-0.13.0.dist-info").is_dir()
    if sys.platform != "win32":
        assert (site / "ziglang/zig").stat().st_mode & 0o777 == 0o755


@pytest.mark.parametrize("name", ["../evil.py", "/evil.py", "pkg/../../evil.py"])
def test_install_wheel_refuses_outside(tmp_path, capsys, name):
    wheel = tmp_path / "w.whl"
    _wheel(wheel, [(name, b"evil\n", 0o644)])
    site = tmp_path / "a" / "site"
    site.mkdir(parents=True)
    with pytest.raises(SystemExit):
        be.install_wheel(str(wheel), str(site))
    assert "a file outside the wheel" in capsys.readouterr().out
    assert not (tmp_path / "evil.py").exists() and not (tmp_path / "a" / "evil.py").exists()


def test_compile_jobs(monkeypatch):
    assert be.compile_jobs(3, 14) == (3, "as asked")
    monkeypatch.setattr(be.os, "cpu_count", lambda: 16)
    monkeypatch.setattr(be, "memory_free_mb", lambda: 2000)
    assert be.compile_jobs(0, 14)[0] == 2            # 0.75 GB a compile
    monkeypatch.setattr(be, "memory_free_mb", lambda: 64000)
    assert be.compile_jobs(0, 14)[0] == 14           # never more than the files
    assert be.compile_jobs(0, 40)[0] == 16           # nor the cores
    monkeypatch.setattr(be, "memory_free_mb", lambda: 0)
    jobs, why = be.compile_jobs(0, 40)
    assert jobs == 16 and "memory unknown" in why
    monkeypatch.setattr(be, "memory_free_mb", lambda: 100)
    assert be.compile_jobs(0, 40)[0] == 1


def test_compile_estimate():
    per_s = be.COMPILE_BYTES_PER_S
    assert be.compile_estimate([], 8) == 1
    assert be.compile_estimate([per_s * 10], 8) == 11                       # one file: its own time
    assert be.compile_estimate([per_s * 10] * 16, 2) == 81                  # many: shared out
    assert be.compile_estimate([per_s * 50, per_s, per_s], 8) == 51         # the biggest decides


def test_fill():
    values = {"kit": "/k", "gen": "/g", "gen_objs": ["/o/a.o", "/o/b.o"], "out": "/e"}
    assert be.fill(["-I{kit}/inc", "{gen_objs}", "-o", "{out}", "-DX={gen}"], values) == \
        ["-I/k/inc", "/o/a.o", "/o/b.o", "-o", "/e", "-DX=/g"]


def test_clean_env_has_nothing_of_the_system(monkeypatch, tmp_path):
    monkeypatch.setenv("JSRF_DECOY", "1")
    monkeypatch.setenv("PYTHONPATH", "/somewhere")
    monkeypatch.setenv("CC", "gcc")
    env = be.clean_env(str(tmp_path), str(tmp_path / "tmp"))
    assert not {"JSRF_DECOY", "PYTHONPATH", "CC"} & set(env)
    assert env["SOURCE_DATE_EPOCH"] == "1"
    assert env["HOME"] == env["TMP"] == str(tmp_path / "tmp")
    assert env["ZIG_GLOBAL_CACHE_DIR"] == os.path.join(str(tmp_path), "tools", "zig-cache")
    if sys.platform != "win32":
        assert env["PATH"] == "/nonexistent" and env["LC_ALL"] == "C"


def test_refuses_another_python(tmp_path):
    (tmp_path / "tools").mkdir()
    p = subprocess.run([sys.executable, str(HERE / "build_engine.py"), "--root", str(tmp_path), "--xbe", "x"],
                       capture_output=True, text=True)
    assert p.returncode == 2
    assert "error: run with the tools' own Python" in p.stdout


# ---- set_expected.py ----

A, B, C = "a" * 64, "b" * 64, "c" * 64


def _kit(tmp_path, expected, version="v1", engine="bin/jsrf_recomp"):
    kit = tmp_path / "kit"
    kit.mkdir()
    (kit / "kit.json").write_text(json.dumps({"version": version, "engine": engine}))
    (kit / "expected.txt").write_bytes(expected.encode())
    return kit


def _stamp(tmp_path, **over):
    s = {"kit": "v1", "engine": "bin/jsrf_recomp", "gen_sha256": A, "engine_sha256": B}
    s.update(over)
    p = tmp_path / "engine.stamp"
    p.write_text(json.dumps(s))
    return p


def _set(kit, stamp):
    return subprocess.run([sys.executable, str(HERE / "set_expected.py"), str(kit), str(stamp)],
                          capture_output=True, text=True)


def test_set_expected_fills_and_keeps_the_rest(tmp_path):
    kit = _kit(tmp_path, "gen_sha256=\nengine_sha256=\ntools=t1\n")
    p = _set(kit, _stamp(tmp_path))
    assert p.returncode == 0, p.stderr
    assert (kit / "expected.txt").read_bytes() == ("gen_sha256=%s\nengine_sha256=%s\ntools=t1\n" % (A, B)).encode()
    assert "(new)" in p.stdout
    p = _set(kit, _stamp(tmp_path))          # again: the same build, nothing changes
    assert p.returncode == 0 and "(new)" not in p.stdout
    assert (kit / "expected.txt").read_bytes() == ("gen_sha256=%s\nengine_sha256=%s\ntools=t1\n" % (A, B)).encode()


def test_set_expected_adds_missing_keys_first(tmp_path):
    kit = _kit(tmp_path, "tools=t1\r\n")
    assert _set(kit, _stamp(tmp_path)).returncode == 0
    assert (kit / "expected.txt").read_bytes() == ("gen_sha256=%s\nengine_sha256=%s\ntools=t1\n" % (A, B)).encode()


@pytest.mark.parametrize("over, says", [
    ({"engine_sha256": C}, "not the same build"),
    ({"kit": "v2"}, "the stamp is from kit"),
    ({"engine": "engine/jsrf_recomp.exe"}, "the other system's"),
    ({"gen_sha256": "A" * 64}, "isn't a SHA-256"),
    ({"engine_sha256": ""}, "isn't a SHA-256"),
])
def test_set_expected_refuses(tmp_path, over, says):
    before = "gen_sha256=%s\nengine_sha256=%s\ntools=t1\n" % (A, B)
    kit = _kit(tmp_path, before)
    p = _set(kit, _stamp(tmp_path, **over))
    assert p.returncode != 0 and says in p.stderr
    assert (kit / "expected.txt").read_text() == before
