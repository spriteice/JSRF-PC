#!/usr/bin/env python3
"""Check a release package: what it holds, its text files' line endings, and
what its programs and libraries need from the system.

    python3 launcher/check_package.py <package>...

A .tar.gz is the Linux package (launcher/linux/package.sh), a .zip the
Windows one (launcher/windows/package.sh), named as package.sh names them or
renamed with the version. Each check is printed, and the exit status is 1 if
any failed. Both build.sh scripts run it on the package they make.

OBJDUMP, when set, is the objdump to use instead of objdump: binutils' and
LLVM's each read both platforms' binaries.
docs/build-and-test-reference.md ("Compatibility checks") says why each
check is there.
"""

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
    "licences/SDL2-COPYING.txt": "text",
    "licences/libepoxy-COPYING.txt": "text",
    "licences/winpthreads-COPYING.txt": "text",
})

# Linux: the libraries each binary may need from the system, as package.sh
# says, and the oldest glibc it must run on (the package's README.txt).
LINUX_NEEDS = {
    "bin/jsrf_recomp": {"libc.so.6", "libm.so.6", "libGL.so.1", "libSDL2-2.0.so.0"},
    "lib/libSDL2-2.0.so.0": {"libc.so.6", "libm.so.6"},
    "jsrf-pc": {"libc.so.6"},
}
GLIBC_FLOOR = (2, 34)

# Windows: the DLLs a binary may import besides those bundled beside it.
# These are part of Windows 10 and 11, as is the Universal C Runtime's
# api-ms-win-crt-*.dll.
WINDOWS_DLLS = {
    "advapi32.dll", "comctl32.dll", "gdi32.dll", "imm32.dll", "kernel32.dll",
    "msvcrt.dll", "ole32.dll", "oleaut32.dll", "setupapi.dll", "shell32.dll",
    "user32.dll", "version.dll", "winmm.dll",
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
    top, files = WINDOWS if windows else LINUX
    print("%s: the %s package" % (package, "Windows" if windows else "Linux"))

    # What it holds: every file listed, nothing else, and nothing but files
    # and folders.
    got = members(package)
    want = {top + "/" + f for f in files}
    folders = {top} | {top + "/" + os.path.dirname(f) for f in files if "/" in f}
    have = {p for p, (d, _) in got.items() if d is False}
    odd = sorted(p for p, (d, _) in got.items()
                 if d is None or (d and p not in folders))
    r.check(have == want and not odd,
            "contents: the %d files listed in launcher/check_package.py" % len(files)
            + "".join("\n      missing: " + p for p in sorted(want - have))
            + "".join("\n      not listed: " + p for p in sorted(have - want))
            + "".join("\n      not listed: " + p for p in odd))
    if not want <= have or odd:
        return

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
