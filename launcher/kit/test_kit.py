"""The kit's scripts without the game or the tools: build_engine.py's parts
(the generated code's hash, unpacking a wheel, how many compiles at once,
the bar's estimates and step lines, the commands filled in, the programs' environment, and refusing
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
import shlex
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


def test_pipeline_seconds_for_every_step():
    kj = _load("kit_json")
    assert set(be.PIPELINE_SECONDS) == {"tools." + n for n in kj.PIPELINE}
    assert set(be.LINK_SECONDS) == {"linux", "windows"}


def test_device_speed():
    assert be.device_speed([]) == 1.0
    assert be.device_speed([(0.2, 0.1)]) == 1.0                      # too little of the PC's time to say
    assert be.device_speed([(131.5, 90.7)]) == pytest.approx(90.7 / 131.5)
    assert be.device_speed([(0.2, 0.2), (131.5, 206.4)]) == pytest.approx(206.6 / 131.7)


def test_compile_per_s():
    per_s = be.COMPILE_BYTES_PER_S
    assert be.compile_per_s([], 1234.0) == 1234.0
    assert be.compile_per_s([(per_s * 2, 0.1)], 1234.0) == 1234.0        # 2 s of work: too little to say
    assert be.compile_per_s([(per_s * 20, 40.0), (per_s * 2, 0.1)], 1234.0) == pytest.approx(per_s * 22 / 40.1)


def test_left_of():
    assert be.left_of(10.0, 0.0) == 10.0
    assert be.left_of(10.0, 4.0) == 6.0
    # Past its estimate a step still needs a part of what it has run, so the
    # bar never holds still: the PC's first start of 3ddb1a9-bar1 had three
    # files due at once, 4.7 s before the last finished.
    assert be.left_of(10.0, 40.0) == pytest.approx(40.0 * be.OVERRUN)
    assert be.left_of(10.0, 40.0) > 0


def test_compile_left():
    per_s = 100.0
    assert be.compile_left([], [], 8, per_s) == 0
    assert be.compile_left([], [1000], 8, per_s) == 10                   # one file: its own time
    assert be.compile_left([], [1000] * 16, 2, per_s) == 80              # many: two at a time
    assert be.compile_left([], [5000, 100, 100], 8, per_s) == 50         # the biggest decides
    assert be.compile_left([(1000, 4)], [], 8, per_s) == 6               # running: what it has left
    assert be.compile_left([(1000, 30)], [], 8, per_s) == pytest.approx(30 * be.OVERRUN)  # past its time
    assert be.compile_left([(1000, 4), (300, 0)], [500], 2, per_s) == 8  # waiting: on the first free


# b2's release build on the Ally X, at its lowest power (its build.log, from
# test build 22a5376-b2's step 8): translating, and the compile,
# 8 at once, estimated at 73 s and taking 225.5 s. The files aren't here, so
# each one's size is stood in by its compile time on the PC (12 at once, b2's
# step 8), in COMPILE_BYTES_PER_S; the pool's order is the files' own.
ALLY_X_B2_TRANSLATING = [("tools.xbe_parser", 0.2), ("tools.disasm", 206.4), ("tools.func_id", 3.4),
                         ("tools.abi_analysis", 2.4), ("tools.recomp", 132.5)]
# The PC's times for the same translating (b2's), which the Ally X's are
# measured against here: build_engine.py's are for the tools as they are now.
PC_B2_TRANSLATING = {"tools.xbe_parser": 0.2, "tools.disasm": 131.5, "tools.func_id": 1.6,
                     "tools.abi_analysis": 1.3, "tools.recomp": 75.6}
PC_COMPILE_S = [("recomp_0002.c", 44.1), ("recomp_0004.c", 43.6), ("recomp_0001.c", 40.3),
                ("recomp_0000.c", 39.2), ("recomp_0006.c", 28.7), ("recomp_0003.c", 28.0),
                ("recomp_0005.c", 22.4), ("recomp_0008.c", 17.2), ("recomp_0007.c", 12.0),
                ("recomp_0009.c", 8.5), ("recomp_dispatch.c", 0.7), ("recomp_stubs_unresolved.c", 0.5)]


def test_compile_estimated_again_from_its_files():
    """Known bug 46: on b2's Ally X build, the compile's first file finished at
    81.8 s, after its 73 s estimate, and b2 said the rest would take 10 s; it
    took 143.7 s. Now the rest is estimated from that file's own speed."""
    k = be.device_speed([(PC_B2_TRANSLATING[m], t) for m, t in ALLY_X_B2_TRANSLATING])
    assert k == pytest.approx(344.9 / 210.2)
    size = {n: t * be.COMPILE_BYTES_PER_S for n, t in PC_COMPILE_S}
    order = [n for n, _ in PC_COMPILE_S]
    first = be.COMPILE_BYTES_PER_S / k
    assert be.compile_left([], [size[n] for n in order], 8, first) == pytest.approx(44.1 * k)
    now = 81.8
    done = [(size["recomp_0008.c"], 81.8)]
    running = [(size[n], now) for n in order[:8] if n != "recomp_0008.c"]
    waiting = [size[n] for n in order[8:]]
    left = be.compile_left(running, waiting, 8, be.compile_per_s(done, first))
    assert 100 < left < 200


def test_step_lines(capsys):
    be.say_step(131.46, 300.04, "Translating the game (tools.disasm)")
    be.say_step(5.0, 2.0, "Linking the engine")                     # never less left than the step
    assert capsys.readouterr().out == ("step 131.5 300.0 Translating the game (tools.disasm)\n"
                                       "step 5.0 5.0 Linking the engine\n")


def test_steps_said_again_past_their_estimate(capsys, monkeypatch):
    """A step that runs past its estimate has its line said again with what
    its estimate says then; one that doesn't, isn't."""
    monkeypatch.setattr(be, "OVERRUN_EVERY", 0.05)
    calls = []

    def late():
        calls.append(1)
        return (0.01, 0.5) if len(calls) == 1 else (0.02, 0.3)
    steps = be.Steps()
    steps.step(late, "Translating the game (tools.disasm)")
    deadline = be.time.monotonic() + 5
    while len(calls) < 3 and be.time.monotonic() < deadline:
        be.time.sleep(0.01)
    steps.step(lambda: (60.0, 90.0), "Compiling (12 files, 8 at once)")
    be.time.sleep(0.2)
    steps.stop()
    out = capsys.readouterr().out.splitlines()
    assert out[0] == "step 0.0 0.5 Translating the game (tools.disasm)"
    assert out[1:-1] and all(line == "step 0.0 0.3 Translating the game (tools.disasm)" for line in out[1:-1])
    assert out[-1] == "step 60.0 90.0 Compiling (12 files, 8 at once)"   # not due again within 0.2 s


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


def test_translate_jobs(monkeypatch):
    monkeypatch.setattr(be.os, "cpu_count", lambda: 16)
    monkeypatch.setattr(be, "memory_free_mb", lambda: 64000)
    assert be.translate_jobs(0)[0] == be.TRANSLATE_JOBS_MAX == 4
    monkeypatch.setattr(be, "memory_free_mb", lambda: 2 * be.COMPILE_MB + 100)
    assert be.translate_jobs(0)[0] == 2
    monkeypatch.setattr(be.os, "cpu_count", lambda: 1)
    monkeypatch.setattr(be, "memory_free_mb", lambda: 64000)
    assert be.translate_jobs(0)[0] == 1
    assert be.translate_jobs(3)[0] == 3 and be.translate_jobs(9)[0] == 4


# The Windows kit's link (3ddb1a9-bar1), less the engine's own objects.
WIN_LINK = ["-s", "-O3", "-DNDEBUG", "-Wl,--stack,0x800000", "{kit}/obj/main.c.obj", "{gen_objs}",
            "-o", "{out}", "-lwinmm", "-ldbghelp", "{kit}/lib/libxbox_kernel.a", "{kit}/lib/libSDL2.dll.a",
            "-lkernel32", "-lm", "-lopengl32", "-Wl,--subsystem,windows", "-L{kit}/lib",
            "{kit}/lib/libwinpthread.dll.a"]


def test_warm_link_asks_for_what_the_link_asks_for(tmp_path):
    # Zig's cache holds the C runtime by target and flags, so the warm-up
    # links with the engine link's flags and -l libraries, and none of its
    # files: no objects, archives or output of its own. And it links an
    # object, as the engine's link does: a link that compiles a C source
    # builds Windows' MinGW runtime for other settings.
    info = {"link": WIN_LINK, "link_driver": "cc", "target": "x86_64-windows-gnu", "system": "windows"}
    values = {"kit": "K", "root": "R", "gen": "G", "gen_root": "GR"}
    compile_cmd, link_cmd = be.warm_link_commands("zig", info, values, str(tmp_path))
    src, obj, out = (str(tmp_path / n) for n in ("warm_link.c", "warm_link.obj", "warm_link.exe"))
    assert compile_cmd == ["zig", "cc", "-target", "x86_64-windows-gnu", "-c", src, "-o", obj]
    assert link_cmd == ["zig", "cc", "-target", "x86_64-windows-gnu", "-s", "-O3", "-DNDEBUG",
                        "-Wl,--stack,0x800000", "-lwinmm", "-ldbghelp", "-lkernel32", "-lm", "-lopengl32",
                        "-Wl,--subsystem,windows", "-LK/lib", obj, "-o", out]
    assert not any(a.endswith(".c") for a in link_cmd)
    assert "int main" in (tmp_path / "warm_link.c").read_text()


def test_a_warm_link_that_fails_is_only_reported(tmp_path):
    warm = be.WarmLink([[str(tmp_path / "no-zig"), "cc"]], str(tmp_path), {})
    warm.join()
    assert warm.rc != 0 and warm.out
    fail = [sys.executable, "-c", "import sys; print('no'); sys.exit(3)"]
    never = [sys.executable, "-c", "open('ran', 'w')"]
    warm = be.WarmLink([fail, never], str(tmp_path), dict(os.environ))
    warm.join()
    assert warm.rc == 3 and "no" in warm.out
    assert not (tmp_path / "ran").exists()       # the link isn't tried once the compile failed
    ok = [sys.executable, "-c", "print('ok')"]
    warm = be.WarmLink([ok, ok], str(tmp_path), dict(os.environ))
    warm.join()
    assert warm.rc == 0 and warm.took > 0


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


# ---- kit_json.py ----

def test_the_pipeline_is_the_linux_builds():
    # A test build makes its generated code by docs/building-on-linux.md and
    # a player's first start by the kit's commands; the kit's expected hash
    # holds only if the two make the same files.
    kj = _load("kit_json")
    doc = (HERE.parents[1] / "docs" / "building-on-linux.md").read_text(encoding="utf-8")
    text = doc.replace("\\\n", " ")
    steps = ["tools." + name for name in kj.PIPELINE]
    doc_cmds = [shlex.split(line.strip())[2:] for line in text.splitlines()
                if line.strip().startswith("python3 -m tools.")]
    doc_cmds = [cmd for cmd in doc_cmds if cmd[0] in steps]
    fill = {"{xbe}": "game_files/default.xbe", "{gen}": "port/recomp/gen"}
    kit_cmds = [[fill.get(a, a) for a in cmd] for cmd in kj.PIPELINE_COMMANDS]
    # How many processes translate changes nothing in the files.
    for cmd in kit_cmds:
        if "--jobs" in cmd:
            del cmd[cmd.index("--jobs"):cmd.index("--jobs") + 2]
    assert doc_cmds == kit_cmds
