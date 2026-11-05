#!/usr/bin/env python3
"""Run every engine test (each tests/<name>/run.py), as CI does.

    python3 tests/run_all.py [--tsan | --asan] [name...]

With names, only those tests. Each test passes, fails, or fails in a way
listed in tests/known-failures.txt: a known bug, still open, whose failure is
reported but doesn't fail the run. Any other failure of a listed test does.
The exit status is 1 if any test failed.

--tsan runs the tests that start threads (those whose files mention
pthread_create, CreateThread or std::thread) built with the thread
sanitizer: CC, or clang, with -fsanitize=thread. A test fails if the
sanitizer reports anything tests/tsan.supp doesn't list, or if it fails.

--asan runs every test built with the address and undefined-behaviour
sanitizers: CC, or gcc, and CXX, or g++, with -fsanitize=address,undefined.
A test fails if they report anything tests/asan-known.txt doesn't list, a
leak at a program's exit included, or if it fails.

Each sets TEST_SANITIZER (thread or address), and a test whose checks depend
on timing leaves those out under it, as cpuprof, heap_lock and nv2a_queue do.
"""

import fnmatch
import glob
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import time

root = pathlib.Path(__file__).resolve().parents[1]
THREADS = re.compile(r"pthread_create|CreateThread|std::thread")
TIMEOUT = 600   # seconds for one test's run.py, all its cases


def known(name):
    """{test: [(line, why)]} from tests/<name>: known-failures.txt, or
    asan-known.txt, whose lines are the reports' SUMMARY lines."""
    out = {}
    for n, raw in enumerate((root / "tests" / name).read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in raw.split(" | ")]
        if len(parts) != 3 or not all(parts):
            raise SystemExit("tests/%s:%d: not 'test | line | why'" % (name, n))
        out.setdefault(parts[0], []).append((parts[1], parts[2]))
    return out


def matching(lines, entries):
    """The why of each entry that one of lines matches."""
    return [why for line, why in entries if any(fnmatch.fnmatchcase(l, line) for l in lines)]


def report_lines(reports):
    """One line for each of the sanitizers' reports: an error's SUMMARY line,
    or, for a leak (LeakSanitizer's direct leaks, as an indirect one goes
    with its direct one), a line of the same shape naming the first place in
    the repository it was allocated from."""
    out, leak = [], False
    for line in reports.splitlines():
        if re.match(r"SUMMARY: AddressSanitizer: \d+ byte\(s\) leaked in", line):
            continue
        if re.match(r"SUMMARY: (AddressSanitizer|UndefinedBehaviorSanitizer):", line):
            out.append(line)
        elif line.startswith("Direct leak of "):
            leak = True
        elif leak:
            frame = re.match(r"\s*#\d+ 0x[0-9a-f]+ in (\S+) ([^/(\s]\S*)$", line)
            if frame and not frame.group(2).startswith("libsanitizer/"):
                out.append("SUMMARY: LeakSanitizer: leak from %s in %s" % (frame.group(2), frame.group(1)))
                leak = False
            elif not line.strip():
                out.append("SUMMARY: LeakSanitizer: leak from outside the repository")
                leak = False
    return out


def threaded(test):
    for f in test.iterdir():
        if f.suffix in (".py", ".c", ".cpp", ".h") and THREADS.search(f.read_text(encoding="utf-8", errors="replace")):
            return True
    return False


def run(test, env):
    t0 = time.monotonic()
    try:
        p = subprocess.run([sys.executable, str(test / "run.py")], cwd=root, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                           errors="replace", timeout=TIMEOUT)
        code, out = p.returncode, p.stdout
    except subprocess.TimeoutExpired as e:
        out = e.stdout or ""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        code, out = None, out + "\nrun_all: no result after %d s" % TIMEOUT
    return code, out, time.monotonic() - t0


def main(argv):
    tsan = "--tsan" in argv
    asan = "--asan" in argv
    names = [a for a in argv if a not in ("--tsan", "--asan")]
    if (tsan and asan) or any(a.startswith("-") for a in names):
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    tests = sorted(pathlib.Path(p).parent for p in glob.glob(str(root / "tests/*/run.py")))
    failures = known("known-failures.txt")
    reported = known("asan-known.txt")
    for name, entries in (("known-failures.txt", failures), ("asan-known.txt", reported)):
        stale = set(entries) - {t.name for t in tests}
        if stale:
            raise SystemExit("run_all: tests/%s names tests with no run.py: %s"
                             % (name, ", ".join(sorted(stale))))
    if names:
        missing = set(names) - {t.name for t in tests}
        if missing:
            raise SystemExit("run_all: no such test: " + ", ".join(sorted(missing)))
        tests = [t for t in tests if t.name in names]
    if tsan:
        tests = [t for t in tests if threaded(t)]

    env = dict(os.environ)
    supp = root / "tests/tsan.supp"
    if tsan:
        env["CC"] = env.get("CC", "clang") + " -fsanitize=thread -g -O1"
        env["TEST_SANITIZER"] = "thread"
    if asan:
        # The address sanitizer goes on after what it can recover from, as
        # the undefined-behaviour one does, so each of a test's cases runs.
        flags = " -fsanitize=address,undefined -fsanitize-recover=address -fno-omit-frame-pointer -g -O1"
        env["CC"] = env.get("CC", "gcc") + flags
        env["CXX"] = env.get("CXX", "g++") + flags
        env["TEST_SANITIZER"] = "address"
    passed, failed, known_seen = [], [], []
    for test in tests:
        with tempfile.TemporaryDirectory(prefix="sanitizer-") as logs:
            # Reports go to files, whatever run.py does with the test's
            # output; and they don't change its exit status, so each of its
            # cases runs.
            report = "log_path=" + os.path.join(logs, "report")
            if tsan:
                env["TSAN_OPTIONS"] = " ".join([
                    "suppressions=" + str(supp), report,
                    "exitcode=0", "halt_on_error=0", "second_deadlock_stack=1"])
            if asan:
                # Each report ends with a SUMMARY line, its paths from the
                # repository's root.
                common = [report, "halt_on_error=0", "print_summary=1",
                          "strip_path_prefix=%s/" % root.as_posix()]
                env["ASAN_OPTIONS"] = " ".join(common + ["detect_leaks=1", "exitcode=0"])
                env["UBSAN_OPTIONS"] = " ".join(common + ["print_stacktrace=1", "report_error_type=1"])
            code, out, secs = run(test, env)
            reports = "".join(pathlib.Path(f).read_text(encoding="utf-8", errors="replace")
                              for f in sorted(glob.glob(os.path.join(logs, "report*"))))
        if asan:
            summaries = report_lines(reports)
            seen = [matching([l], reported.get(test.name, [])) for l in summaries]
            new = sum(1 for why in seen if not why)
            count, why_reported = len(summaries), sorted({w for why in seen for w in why})
        else:
            new = count = reports.count("WARNING: ThreadSanitizer")
            why_reported = []
        match = matching(out.splitlines(), failures.get(test.name, []))
        if new:
            verdict = "FAIL"
        elif code == 0:
            verdict = "known" if why_reported else "ok"
        elif match:
            verdict = "known"
        else:
            verdict = "FAIL"
        note = "%5.1f s" % secs
        if count:
            note += ", %d sanitizer report%s" % (count, "" if count == 1 else "s")
            if count != new:
                note += ", %d known" % (count - new)
        if code != 0:
            note += ", no result" if code is None else ", exit %d" % code
        print("%-6s%-18s%s" % (verdict, test.name, note))
        if verdict == "known":
            print("      known: " + "; ".join(why_reported + match))
        if verdict != "ok":
            for l in (out + reports).rstrip().splitlines():
                print("      | " + l)
        {"ok": passed, "known": known_seen, "FAIL": failed}[verdict].append(test.name)

    print("run_all%s: %d passed, %d known failures, %d failed%s" % (
        " --tsan" if tsan else " --asan" if asan else "", len(passed), len(known_seen),
        len(failed), ": " + ", ".join(failed) if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
