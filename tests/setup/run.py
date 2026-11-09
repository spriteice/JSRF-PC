"""Compile the launcher's setup without its window (launcher/setup/setup_core.c,
with port/xiso.c, port/xbe_check.c and miniz), and run test_main.c's checks.

Makes the archives the unpacking is tried on (tar/): one laid out as the
tools' Python is, with a long name each way (GNU's and pax's), links inside
it, and a folder the archive never lists; and one for each way out of the
folder (a "..", an absolute path, links pointing out, through another link,
or to an absolute path, a hard link to a ".."), which must each be refused
with nothing written. On Linux, also the stand-in for the tools' Python the
whole first start is run with (the console's way: the answers in the
environment), a Python script that writes down what it was given and its
environment, and prints the build's lines as launcher/kit/build_engine.py
does. Nothing here touches the network.

Requires GCC on PATH, including MinGW on Windows. CC, when set, is the
compiler instead of gcc.
"""
import hashlib
import io
import os
import pathlib
import shlex
import subprocess
import sys
import tarfile
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))

STAND_IN = r'''#!%s -I
"""The tools' Python, stood in for (tests/setup/run.py)."""
import hashlib, os, sys
args = sys.argv[1:]
root = args[args.index("--root") + 1]
build = os.path.join(root, "build")
with open(os.path.join(build, "py-args.txt"), "w") as f:
    f.write(" ".join(args) + "\n")
with open(os.path.join(build, "py-env.txt"), "w") as f:
    f.write("".join("%%s=%%s\n" %% kv for kv in sorted(os.environ.items())))
mode = open(os.path.join(root, "mode.txt")).read().strip()
print("step 0 0.5 2 Translating")
if mode == "error":
    print("error: the stand-in stopped")
    sys.exit(1)
os.makedirs(os.path.join(build, "generated"), exist_ok=True)
with open(os.path.join(build, "generated", "x.c"), "w") as f:
    f.write("/* made up */\n")
print("step 0.5 1 2 Compiling")
os.makedirs(os.path.join(root, "bin"), exist_ok=True)
engine = b"the stand-in engine\n"
with open(os.path.join(root, "bin", "jsrf_recomp"), "wb") as f:
    f.write(engine)
sha = hashlib.sha256(engine).hexdigest()
want = dict(l.split("=", 1) for l in open(os.path.join(root, "kit", "expected.txt")).read().split())
print("done %%s %%s %%s match" %% (sha, "match" if want.get("engine_sha256") == sha else "mismatch", "0" * 64))
'''


def add(t, name, data=None, kind=tarfile.REGTYPE, link="", mode=0o644):
    i = tarfile.TarInfo(name)
    i.type = kind
    i.linkname = link
    i.mode = mode
    if data is not None:
        i.size = len(data)
        t.addfile(i, io.BytesIO(data))
    else:
        t.addfile(i)


def archive(path, members, fmt=tarfile.GNU_FORMAT):
    with tarfile.open(path, "w:gz", format=fmt) as t:
        for m in members:
            add(t, *m[:2], **m[2] if len(m) > 2 else {})


def make_archives(work):
    tar = work / "tar"
    tar.mkdir()
    d = tarfile.DIRTYPE
    long_gnu = "d123456789" * 10 + "x"
    long_pax = "p123456789" * 10 + "y"
    archive(tar / "good.tar.gz", [
        ("python/bin", None, {"kind": d, "mode": 0o755}),
        ("python/bin/python3.13", b"#!/bin/sh\n", {"mode": 0o755}),
        ("python/bin/python3", None, {"kind": tarfile.SYMTYPE, "link": "python3.13"}),
        ("python/lib/a/b.txt", b"bee\n"),
        ("python/lib/up.txt", None, {"kind": tarfile.SYMTYPE, "link": "../bin/python3.13"}),
        ("python/lib/hard.txt", None, {"kind": tarfile.LNKTYPE, "link": "python/lib/a/b.txt"}),
        ("python/%s/deep.txt" % long_gnu, b"deep\n"),
    ])
    archive(tar / "pax.tar.gz", [("python/%s/pax.txt" % long_pax, b"pax\n")], tarfile.PAX_FORMAT)
    out = work / "untar"
    sym, hard = tarfile.SYMTYPE, tarfile.LNKTYPE
    bad = {
        "dotdot": [("../evil.txt", b"evil\n")],
        "absolute": [(str(out / "absolute/evil.txt"), b"evil\n")],
        "inner-dotdot": [("python/../../evil.txt", b"evil\n")],
        "link-out": [("python/out", None, {"kind": sym, "link": "../.."}),
                     ("python/out/evil.txt", b"evil\n")],
        "link-absolute": [("python/abs", None, {"kind": sym, "link": str(out / "link-absolute")}),
                          ("python/abs/evil.txt", b"evil\n")],
        "link-through": [("python/b", None, {"kind": d, "mode": 0o755}),
                         ("python/a", None, {"kind": sym, "link": "b/../../.."}),
                         ("python/a/evil.txt", b"evil\n")],
        "hardlink-out": [("python/h", None, {"kind": hard, "link": "../evil-src"})],
    }
    for name, members in bad.items():
        archive(tar / (name + ".tar.gz"), members)
    good = (tar / "good.tar.gz").read_bytes()
    (tar / "cut.tar.gz").write_bytes(good[:len(good) // 2])


def make_stand_in(work):
    py = work / "stand-in/tools/python/bin/python3"
    py.parent.mkdir(parents=True)
    py.write_text(STAND_IN % sys.executable)
    py.chmod(0o755)
    (work / "stand-in/tools/TOOLS.txt").write_text(
        "tools=python-3.13.0-20241016+capstone-5.0.9+ziglang-0.13.0\n")


with tempfile.TemporaryDirectory(prefix="setup-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    srcs = ["tests/setup/test_main.c", "launcher/setup/setup_core.c", "port/xiso.c", "port/xbe_check.c"]
    objs = []
    for s in srcs:
        o = str(work / (pathlib.Path(s).name + ".o"))
        subprocess.run(cc + ["-std=gnu11", "-O1", "-Wall", "-Wextra", "-Iport", "-Ilauncher/setup",
                             "-c", s, "-o", o], cwd=root, check=True)
        objs.append(o)
    o = str(work / "miniz.c.o")
    m = subprocess.run(cc + ["-O1", "-w", "-c", "third_party/miniz/miniz.c", "-o", o], cwd=root,
                       capture_output=True, text=True)   # its #pragma message, every time
    if m.returncode:
        raise SystemExit("setup: miniz.c didn't compile\n" + m.stdout + m.stderr)
    objs.append(o)
    libs = ["-lwinhttp"] if sys.platform == "win32" else ["-ldl", "-lm"]
    subprocess.run(cc + objs + ["-o", exe] + libs, cwd=root, check=True)

    make_archives(work)
    if sys.platform != "win32":
        make_stand_in(work)
    env = dict(os.environ)
    for k in list(env):
        if k.startswith("JSRF_"):
            del env[k]
    p = subprocess.run([exe, str(work)], env=env, capture_output=True, text=True, timeout=120)
    sys.stdout.write(p.stdout)
    sys.stderr.write(p.stderr)
    out = p.stdout
    bad = []
    if sys.platform != "win32":
        # The questions, in Aden's words (2026-11-09), as the console asks them.
        for q in ("No matching binary detected. Build now with local tools?",
                  "No matching binary detected. Download tools now and build?",
                  "The engine built here isn't the one this release builds. Play anyway?",
                  "Delete the tools and the code made from your game"):
            if q not in out:
                bad.append("never asked %r" % q)
    for b in bad:
        print("FAIL " + b)
    if p.returncode or bad:
        raise SystemExit("setup: failed")
