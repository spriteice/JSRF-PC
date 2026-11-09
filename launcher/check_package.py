#!/usr/bin/env python3
"""Check a release package: what it holds, its text files' line endings, and
what its programs and libraries need from the system.

    python3 launcher/check_package.py <package>...

A .tar.gz is the Linux package (launcher/linux/package.sh), a .zip the
Windows one (launcher/windows/package.sh), named as package.sh names them or
renamed with the version. A package with a kit/ folder is the one without the
engine, which the player's PC builds (launcher/firststart/package.sh), and
the engine may be there too (a test build's, built from the kit). JSRF-PC-tools-*.tar.gz and .zip are the tools archives
made beside it. Each check is printed, and the exit status is 1 if any failed.
The build.sh scripts and firststart/package.sh run it on what they make.

OBJDUMP, when set, is the objdump to use instead of objdump: binutils' and
LLVM's each read both platforms' binaries.
docs/build-and-test-reference.md ("Compatibility checks") says why each
check is there.
"""

import json
import os
import re
import shlex
import subprocess
import sys
import tarfile
import tempfile
import zipfile

# Everything each package holds, in its one top folder, and what each file is:
#   text    the platform's line endings once (Linux LF, Windows CR LF)
#   script  text, and executable
#   elf     an x86_64 ELF program or library; "elf exec" is executable
#   pe      an x86_64 Windows program or DLL
#   data    anything else
LINUX = ("JSRF-PC-linux-x86_64", {
    "jsrf-pc": "elf exec",
    "README.txt": "text",
    "VERSION": "text",
    "bin/jsrf_recomp": "elf exec",
    "lib/libSDL2-2.0.so.0": "elf",
    "share/jsrf-pc.png": "data",
    "licences/LICENSE.txt": "text",
    "licences/NOTICE.txt": "text",
    "licences/MIT-upstream.txt": "text",
    "licences/LGPL-2.1.txt": "text",
    "licences/imgui-MIT.txt": "text",
    "licences/miniz-MIT.txt": "text",
    "licences/SDL2-LICENSE.txt": "text",
})
WINDOWS = ("JSRF-PC-windows-x86_64", {
    "JSRF-PC.exe": "pe",
    "README.txt": "text",
    "VERSION": "text",
    "engine/jsrf_recomp.exe": "pe",
    "engine/SDL2.dll": "pe",
    "engine/libepoxy-0.dll": "pe",
    "engine/libwinpthread-1.dll": "pe",
    "licences/LICENSE.txt": "text",
    "licences/NOTICE.txt": "text",
    "licences/MIT-upstream.txt": "text",
    "licences/LGPL-2.1.txt": "text",
    "licences/imgui-MIT.txt": "text",
    "licences/miniz-MIT.txt": "text",
    "licences/SDL2-COPYING.txt": "text",
    "licences/libepoxy-COPYING.txt": "text",
    "licences/winpthreads-COPYING.txt": "text",
})

# The packages without the engine: the same launcher, the kit it builds the
# engine with, and the engine only where a test build has put the one built
# from the kit beside it.
LINUX_FS = ("JSRF-PC-linux-x86_64",
            {k: v for k, v in LINUX[1].items() if k != "bin/jsrf_recomp"})
WINDOWS_FS = ("JSRF-PC-windows-x86_64",
              {k: v for k, v in WINDOWS[1].items() if k != "engine/jsrf_recomp.exe"})
ENGINE = {"linux": ("bin/jsrf_recomp", "elf exec"), "windows": ("engine/jsrf_recomp.exe", "pe")}
# The kit's own files, which the setup reads; the rest of kit/ is its build
# (objects, libraries, headers, the translator), checked by the setup itself.
KIT = {"kit/kit.json": "text lf", "kit/expected.txt": "text lf", "kit/build_engine.py": "text lf"}
KIT_EXPECTED = ("gen_sha256", "engine_sha256", "tools")

# Linux: the libraries each binary may need from the system, as package.sh
# says, and the oldest glibc it must run on (the package's README.txt). The
# launcher opens OpenGL itself, with the setup's window.
LINUX_NEEDS = {
    "bin/jsrf_recomp": {"libc.so.6", "libm.so.6", "libGL.so.1", "libSDL2-2.0.so.0"},
    "lib/libSDL2-2.0.so.0": {"libc.so.6", "libm.so.6"},
    "jsrf-pc": {"libc.so.6", "libm.so.6", "libSDL2-2.0.so.0"},
}
GLIBC_FLOOR = (2, 34)

# Windows: the DLLs a binary may import besides those bundled beside it.
# These are part of Windows 10 and 11, as is the Universal C Runtime's
# api-ms-win-crt-*.dll.
WINDOWS_DLLS = {
    "advapi32.dll", "comctl32.dll", "gdi32.dll", "imm32.dll", "kernel32.dll",
    "msvcrt.dll", "ole32.dll", "oleaut32.dll", "setupapi.dll", "shell32.dll",
    "user32.dll", "version.dll", "winhttp.dll", "winmm.dll",
}
UCRT = re.compile(r"api-ms-win-crt-[a-z]+-l1-1-0\.dll")

ELF_X86_64 = 62        # e_machine
PE_X86_64 = 0x8664     # the COFF header's Machine


class Report:
    def __init__(self):
        self.failed = 0
        self.passed = 0

    def check(self, ok, what):
        print(("ok    " if ok else "FAIL  ") + what)
        if ok:
            self.passed += 1
        else:
            self.failed += 1
        return ok


def objdump(path):
    cmd = shlex.split(os.environ.get("OBJDUMP", "objdump"))
    return subprocess.run(cmd + ["-p", path], check=True, capture_output=True,
                          text=True).stdout


def members(package):
    """The package's files and folders: {path: (is_dir, mode)}."""
    out = {}
    if package.endswith(".zip"):
        with zipfile.ZipFile(package) as z:
            for i in z.infolist():
                out[i.filename.rstrip("/")] = (i.is_dir(), i.external_attr >> 16)
    else:
        with tarfile.open(package) as t:
            for m in t.getmembers():
                if not (m.isfile() or m.isdir()):
                    out[m.name] = (None, m.mode)   # a link or a device: never
                else:
                    out[m.name.rstrip("/")] = (m.isdir(), m.mode)
    return out


def endings(data, windows):
    """What is wrong with a text file's line endings, or None."""
    lf = data.count(b"\n")
    crlf = data.count(b"\r\n")
    cr = data.count(b"\r")
    if lf == 0:
        return "no lines"
    if windows:
        doubled = data.count(b"\r\r\n")
        if doubled:
            return "\\r\\r\\n on %d of %d lines" % (doubled, lf)
        if crlf != lf:
            return "LF without CR on %d of %d lines" % (lf - crlf, lf)
        if cr != crlf:
            return "%d CR not before LF" % (cr - crlf)
        if not data.endswith(b"\r\n"):
            return "the last line has no line ending"
    else:
        if cr:
            return "CR on %d of %d lines" % (crlf, lf) if crlf else "%d CR" % cr
        if not data.endswith(b"\n"):
            return "the last line has no line ending"
    return None


def machine(data, kind):
    """The binary's format and machine, if not the one expected, or None."""
    if kind == "elf":
        if data[:4] != b"\x7fELF" or data[4] != 2:
            return "not a 64-bit ELF file"
        if int.from_bytes(data[18:20], "little") != ELF_X86_64:
            return "not x86_64"
        return None
    if data[:2] != b"MZ":
        return "not a Windows binary"
    pe = int.from_bytes(data[0x3C:0x40], "little")
    if data[pe:pe + 4] != b"PE\0\0":
        return "not a Windows binary"
    if int.from_bytes(data[pe + 4:pe + 6], "little") != PE_X86_64:
        return "not x86_64"
    return None


def glibc_versions(dump):
    return {tuple(int(n) for n in v.split("."))
            for v in re.findall(r"\bGLIBC_([0-9]+(?:\.[0-9]+)+)\b", dump)}


def check(package, r):
    windows = package.endswith(".zip")
    if not windows and not package.endswith(".tar.gz"):
        r.check(False, "%s: neither a .tar.gz (Linux) nor a .zip (Windows)" % package)
        return
    system = "windows" if windows else "linux"
    name = os.path.basename(package)
    if name.startswith("JSRF-PC-tools-"):
        check_tools(package, system, r)
        return
    got = members(package)
    top = (WINDOWS if windows else LINUX)[0]
    firststart = top + "/kit/kit.json" in got
    if firststart:
        top, files = WINDOWS_FS if windows else LINUX_FS
        files = dict(files)
        engine, kind = ENGINE[system]
        built = top + "/" + engine in got
        if built:
            files[engine] = kind
        print("%s: the %s package without the engine%s" % (
            package, "Windows" if windows else "Linux",
            ", with a test build's engine" if built else ""))
    else:
        top, files = WINDOWS if windows else LINUX
        print("%s: the %s package" % (package, "Windows" if windows else "Linux"))

    # What it holds: every file listed, nothing else, and nothing but files
    # and folders; and in the package without the engine, the kit, its own
    # files listed and the rest only files and folders.
    kit_top = top + "/kit"
    in_kit = {p: v for p, v in got.items() if firststart and (p == kit_top or p.startswith(kit_top + "/"))}
    rest = {p: v for p, v in got.items() if p not in in_kit}
    want = {top + "/" + f for f in files}
    folders = {top} | {top + "/" + os.path.dirname(f) for f in files if "/" in f}
    if firststart:
        folders |= {top + "/bin"} if not windows else set()
    have = {p for p, (d, _) in rest.items() if d is False}
    odd = sorted(p for p, (d, _) in rest.items()
                 if d is None or (d and p not in folders))
    r.check(have == want and not odd,
            "contents: the %d files listed in launcher/check_package.py" % len(files)
            + "".join("\n      missing: " + p for p in sorted(want - have))
            + "".join("\n      not listed: " + p for p in sorted(have - want))
            + "".join("\n      not listed: " + p for p in odd))
    if not want <= have or odd:
        return
    if firststart:
        links = sorted(p for p, (d, _) in in_kit.items() if d is None)
        kit_want = {top + "/" + f for f in KIT}
        r.check(kit_want <= set(in_kit) and not links,
                "kit: %s there, and only files and folders (%d entries)" % (
                    ", ".join(sorted(KIT)), len(in_kit))
                + "".join("\n      missing: " + p for p in sorted(kit_want - set(in_kit)))
                + "".join("\n      not a file or folder: " + p for p in links))
        if not kit_want <= set(in_kit) or links:
            return
        check_kit(package, top, system, r)

    with tempfile.TemporaryDirectory() as tmp:
        if windows:
            with zipfile.ZipFile(package) as z:
                z.extractall(tmp, sorted(want))
        else:
            with tarfile.open(package) as t:
                safe = {"filter": "data"} if hasattr(tarfile, "data_filter") else {}
                t.extractall(tmp, [t.getmember(m) for m in sorted(want)], **safe)
        for f, kind in files.items():
            path = os.path.join(tmp, top, f)
            with open(path, "rb") as fh:
                data = fh.read()
            if kind in ("text", "script"):
                bad = endings(data, windows)
                r.check(bad is None, "%s: %s line endings once%s" % (
                    f, "CR LF" if windows else "LF", "" if bad is None else ": " + bad))
            if f == "VERSION":
                lines = data.decode("utf-8", "replace").splitlines()
                r.check(len(lines) == 1 and lines[0].strip() != "",
                        "VERSION: one line, the version (%r)" % data[:40])
            if kind in ("script", "elf exec"):
                mode = got[top + "/" + f][1]
                r.check(mode & 0o111 == 0o111, "%s: executable (%o)" % (f, mode & 0o777))
            if kind.startswith("elf") or kind == "pe":
                bad = machine(data, kind.split()[0])
                r.check(bad is None, "%s: x86_64%s" % (f, "" if bad is None else ": " + bad))
                if bad is not None:
                    continue
                dump = objdump(path)
                if windows:
                    beside = {os.path.basename(g).lower() for g in files
                              if os.path.dirname(g) == os.path.dirname(f) and g.endswith(".dll")}
                    dlls = re.findall(r"DLL Name:\s*(\S+)", dump)
                    extra = sorted(d for d in dlls if d.lower() not in WINDOWS_DLLS
                                   and d.lower() not in beside and not UCRT.fullmatch(d.lower()))
                    r.check(dlls and not extra, "%s: imports only Windows' DLLs and those beside it%s"
                            % (f, ": also " + ", ".join(extra) if extra else ""))
                else:
                    needed = set(re.findall(r"^\s*NEEDED\s+(\S+)", dump, re.M))
                    extra = sorted(needed - LINUX_NEEDS[f])
                    r.check(needed and not extra, "%s: needs only %s%s" % (
                        f, ", ".join(sorted(LINUX_NEEDS[f])),
                        ": also " + ", ".join(extra) if extra else ""))
                    newest = max(glibc_versions(dump), default=None)
                    r.check(newest is not None and newest <= GLIBC_FLOOR,
                            "%s: glibc %s or older (newest needed: %s)" % (
                                f, ".".join(map(str, GLIBC_FLOOR)),
                                ".".join(map(str, newest)) if newest else "none found"))
                    cxx = sorted(set(re.findall(r"\b((?:GLIBCXX|CXXABI)_[0-9.]+)", dump)))
                    r.check(not cxx, "%s: no C++ runtime needed (it is linked in)%s"
                            % (f, ": needs " + ", ".join(cxx) if cxx else ""))


def read_member(package, path):
    if package.endswith(".zip"):
        with zipfile.ZipFile(package) as z:
            return z.read(path)
    with tarfile.open(package) as t:
        return t.extractfile(path).read()


def setup_tools_id():
    """The tools the setup builds with (launcher/setup/setup_core.c)."""
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "setup", "setup_core.c")
    with open(src, encoding="utf-8") as f:
        m = re.search(r'#define JS_TOOLS_ID "([^"]+)"', f.read())
    return m.group(1) if m else None


def check_kit(package, top, system, r):
    """The kit's own files: LF line endings on both systems (the setup's
    Python reads them as they are), kit.json for this system, and
    expected.txt naming the tools the setup gets."""
    for f in KIT:
        data = read_member(package, top + "/" + f)
        bad = endings(data, False)
        r.check(bad is None, "%s: LF line endings once%s" % (f, "" if bad is None else ": " + bad))
    try:
        info = json.loads(read_member(package, top + "/kit/kit.json").decode("utf-8"))
    except ValueError as e:
        r.check(False, "kit/kit.json: JSON (%s)" % e)
        return
    engine = ENGINE[system][0]
    r.check(info.get("engine") == engine, "kit/kit.json: builds %s (%r)" % (engine, info.get("engine")))
    version = read_member(package, top + "/VERSION").decode("utf-8", "replace").strip()
    r.check(info.get("version") == version, "kit/kit.json: version %r, the package's VERSION (%r)"
            % (info.get("version"), version))
    expected = {}
    for line in read_member(package, top + "/kit/expected.txt").decode("utf-8", "replace").splitlines():
        k, _, v = line.partition("=")
        expected[k] = v
    r.check(set(KIT_EXPECTED) <= set(expected), "kit/expected.txt: %s" % ", ".join(KIT_EXPECTED))
    for k in ("gen_sha256", "engine_sha256"):
        v = expected.get(k, "")
        r.check(v == "" or re.fullmatch(r"[0-9a-f]{64}", v) is not None,
                "kit/expected.txt: %s empty or a SHA-256 (%s)" % (k, v or "empty"))
    want = setup_tools_id()
    r.check(want is not None and expected.get("tools") == want,
            "kit/expected.txt: tools=%s, the setup's (%s)" % (expected.get("tools"), want))


def check_tools(package, system, r):
    """A tools archive: one folder, tools, ready as it is, with the tools
    the setup gets, its mark last, and no link that leads out of it."""
    print("%s: the %s tools archive" % (package, "Windows" if system == "windows" else "Linux"))
    got = members(package)
    outside = sorted(p for p in got if p != "tools" and not p.startswith("tools/"))
    r.check(not outside, "contents: one folder, tools" + "".join("\n      outside: " + p for p in outside[:10]))
    python = "tools/python/python.exe" if system == "windows" else "tools/python/bin/python3"
    site = "tools/python/Lib/site-packages" if system == "windows" else "tools/python/lib/python3.13/site-packages"
    zig = site + ("/ziglang/zig.exe" if system == "windows" else "/ziglang/zig")
    need = ["tools/TOOLS.txt", python, zig, site + "/capstone/__init__.py"]
    missing = [p for p in need if p not in got]
    r.check(not missing, "has %s" % ", ".join(need) + "".join("\n      missing: " + p for p in missing))
    if missing:
        return
    mark = dict(l.partition("=")[::2] for l in read_member(package, "tools/TOOLS.txt").decode(
        "utf-8", "replace").splitlines() if "=" in l)
    want = setup_tools_id()
    r.check(want is not None and mark.get("tools") == want,
            "tools/TOOLS.txt: tools=%s, the setup's (%s)" % (mark.get("tools"), want))
    if not package.endswith(".zip"):
        bad = []
        with tarfile.open(package) as t:
            for m in t.getmembers():
                if m.issym() or m.islnk():
                    to = os.path.normpath(os.path.join(os.path.dirname(m.name), m.linkname)) \
                        if m.issym() else os.path.normpath(m.linkname)
                    if os.path.isabs(m.linkname) or not (to == "tools" or to.startswith("tools/")):
                        bad.append("%s -> %s" % (m.name, m.linkname))
                elif not (m.isfile() or m.isdir()):
                    bad.append(m.name)
        r.check(not bad, "links stay inside tools, and nothing but files, folders and links"
                + "".join("\n      " + b for b in bad[:10]))
    mode = got[python][1]
    if system == "linux":
        r.check(mode & 0o111 == 0o111 or mode == 0, "%s: executable (%o)" % (python, mode & 0o777))


def main(argv):
    if not argv:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    r = Report()
    for package in argv:
        check(package, r)
    print("check_package: %d checks passed, %d failed" % (r.passed, r.failed))
    return 1 if r.failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
