#!/usr/bin/env python3
"""Run every engine test (each tests/<name>/run.py), as CI does.

    python3 tests/run_all.py [--tsan | --asan | --coverage <dir>] [name...]

With names, only those tests. Each test passes, fails, fails in a way
listed in tests/known-failures.txt (a known bug, still open, whose failure is
reported but doesn't fail the run; any other failure of a listed test does),
or is skipped: it can't run on this machine, says why, and exits with
SKIPPED (77), and the summary counts it apart from those that passed.
The exit status is 1 if any test failed.

Every line of tests/known-failures.txt and tests/asan-known.txt, and every
entry in tests/tsan.supp, names the release that is to fix its bug (or, in
tsan.supp, says it is there by design), and the run stops if one doesn't.
The release checks them at its step 4 (docs/release-checklist.md).

--tsan runs the tests that start threads (those whose files mention
pthread_create, CreateThread or std::thread) built with the thread
sanitizer: CC, or clang, with -fsanitize=thread. A test fails if the
sanitizer reports anything tests/tsan.supp doesn't list, if an entry there
hides more of its reports than the entry's "hides at most" line allows (or
any, in a test that line doesn't name), or if it fails.

--asan runs every test built with the address and undefined-behaviour
sanitizers: CC, or gcc, and CXX, or g++, with -fsanitize=address,undefined.
A test fails if they report anything tests/asan-known.txt doesn't list, a
leak at a program's exit included, or if it fails.

Each sets TEST_SANITIZER (thread or address), and a test whose checks depend
on timing leaves those out under it, as cpuprof, heap_lock and nv2a_queue do.

--coverage runs every test built with GCC's line counts: CC, or gcc, and CXX,
or g++, with --coverage, each test's counts kept in <dir>/<test>/ for
.github/ci/coverage.py's report. It is for information only: the tests'
results are shown, and the exit status is 0 whatever they are.
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
SKIPPED = 77    # a run.py's exit status when its test can't run here


# A release's name, as docs/versioning.md gives them: b2, b2.1, 1.0.
RELEASE = re.compile(r"(b\d+|\d+)(\.\d+)*")


def known(name):
    """{test: [(line, why)]} from tests/<name>: known-failures.txt, or
    asan-known.txt, whose lines are the reports' SUMMARY lines. Each names
    the release that is to fix it."""
    out = {}
    for n, raw in enumerate((root / "tests" / name).read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in raw.split(" | ")]
        if len(parts) != 4 or not all(parts):
            raise SystemExit("tests/%s:%d: not 'test | line | fixed by | why'" % (name, n))
        if not RELEASE.fullmatch(parts[2]):
            raise SystemExit("tests/%s:%d: %r is not a release, as b3 or 1.0" % (name, n, parts[2]))
        out.setdefault(parts[0], []).append((parts[1], parts[3]))
    return out


def supp_releases(text):
    """Check that each group of entries in tests/tsan.supp (its lines between
    blank lines) has a comment that names the release that is to fix it
    ("Fixed by b3.") or says it is there by design ("By design"). Returns the
    problems, each as 'line: what'."""
    problems = []
    lines = text.splitlines()
    group = []
    for n, raw in enumerate(lines + [""], 1):
        if raw.strip():
            group.append((n, raw.strip()))
            continue
        comment = " ".join(l.lstrip("#").strip() for _, l in group if l.startswith("#"))
        entries = [(m, l) for m, l in group if not l.startswith("#")]
        group = []
        if not entries:
            continue
        fixed = re.search(r"\bFixed by (\S+?)[.,;:]", comment)
        if fixed and RELEASE.fullmatch(fixed.group(1)):
            continue
        if not fixed and re.search(r"\bBy design\b", comment):
            continue
        problems.append("%d: %s has no \"Fixed by <release>.\" or \"By design\" in its comment"
                        % (entries[0][0], entries[0][1]))
    return problems


def supp_limits(text):
    """How many reports each entry of tests/tsan.supp may hide in each test,
    from the "hides at most" line under it: ({entry: {test: count}}, the
    problems, each as 'line: what'). An entry with no such line may hide
    none."""
    limits, problems, entry = {}, [], None
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            entry = None
        elif not line.startswith("#"):
            entry = line
            limits.setdefault(entry, {})
        else:
            m = re.match(r"#\s*hides at most:\s*(.*)$", line)
            if not m:
                continue
            pairs = [p.split() for p in m.group(1).split(",")]
            if entry is None or not all(len(p) == 2 and p[1].isdigit() for p in pairs):
                problems.append("%d: not \"# hides at most: <test> <count>, ...\" under an entry" % n)
                continue
            for test, count in pairs:
                limits[entry][test] = int(count)
    return limits, problems


def hidden(reports):
    """{entry: reports it hid} from the sanitizer's print_suppressions lines
    ("ThreadSanitizer: Matched 3 suppressions", then "3 race:^s_hc$"), summed
    over the test's programs."""
    out, counting = {}, False
    for line in reports.splitlines():
        if line.startswith("ThreadSanitizer: Matched "):
            counting = True
            continue
        m = re.match(r"(\d+) (\S+:\S.*)$", line) if counting else None
        if m:
            out[m.group(2)] = out.get(m.group(2), 0) + int(m.group(1))
        else:
            counting = False
    return out


def matching(lines, entries):
    """The why of each entry that one of lines matches."""
    return [why for line, why in entries if any(fnmatch.fnmatchcase(l, line) for l in lines)]


def in_repository(path):
    """Whether a stack frame's path (strip_path_prefix makes the repository's
    own relative), its line and column left off, is a file in the repository.
    The sanitizer's own frames aren't, however its GCC names them
    (libsanitizer/asan/... or ../../../../src/libsanitizer/asan/...)."""
    f = (root / re.sub(r"(:\d+)+$", "", path)).resolve()
    return root in f.parents and f.is_file()


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
            if frame and in_repository(frame.group(2)):
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
    argv = list(argv)
    coverage = None
    if "--coverage" in argv:
        i = argv.index("--coverage")
        coverage = argv[i + 1] if i + 1 < len(argv) else ""
        del argv[i:i + 2]
    tsan = "--tsan" in argv
    asan = "--asan" in argv
    names = [a for a in argv if a not in ("--tsan", "--asan")]
    if (tsan + asan + (coverage is not None) > 1 or coverage == ""
            or any(a.startswith("-") for a in names)):
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    tests = sorted(pathlib.Path(p).parent for p in glob.glob(str(root / "tests/*/run.py")))
    failures = known("known-failures.txt")
    reported = known("asan-known.txt")
    supp_text = (root / "tests/tsan.supp").read_text(encoding="utf-8")
    limits, problems = supp_limits(supp_text)
    problems += supp_releases(supp_text)
    if problems:
        raise SystemExit("run_all: tests/tsan.supp:" + "\n  tests/tsan.supp:".join(problems))
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
    if coverage is not None:
        cc, cxx = env.get("CC", "gcc"), env.get("CXX", "g++")
    passed, failed, known_seen, skipped = [], [], [], []
    for test in tests:
        if coverage is not None:
            # Each test's counts in a folder of its own, as its programs'
            # names (test.exe) and sources repeat from test to test.
            counts = pathlib.Path(coverage, test.name).resolve()
            counts.mkdir(parents=True, exist_ok=True)
            flags = " --coverage -fprofile-update=atomic -dumpdir %s/" % counts.as_posix()
            env["CC"], env["CXX"] = cc + flags, cxx + flags
        with tempfile.TemporaryDirectory(prefix="sanitizer-") as logs:
            # Reports go to files, whatever run.py does with the test's
            # output; and they don't change its exit status, so each of its
            # cases runs.
            report = "log_path=" + os.path.join(logs, "report")
            if tsan:
                env["TSAN_OPTIONS"] = " ".join([
                    "suppressions=" + str(supp), report, "print_suppressions=1",
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
            # Each entry's hidden reports against its "hides at most" line.
            # How many there are varies from run to run, so the lines allow
            # about twice the most seen; a new race an entry hides adds to it.
            hid = hidden(reports) if tsan else {}
            for entry, n in sorted(hid.items()):
                most = limits.get(entry, {}).get(test.name, 0)
                if n > most:
                    new += 1
                    reports += ("run_all: tests/tsan.supp's %s hid %d reports in %s, and its "
                                "\"hides at most\" line allows %d\n" % (entry, n, test.name, most))
        match = matching(out.splitlines(), failures.get(test.name, []))
        if new:
            verdict = "FAIL"
        elif code == 0:
            verdict = "known" if why_reported else "ok"
        elif code == SKIPPED:
            verdict = "skip"
        elif match:
            verdict = "known"
        else:
            verdict = "FAIL"
        note = "%5.1f s" % secs
        if tsan and hid:
            note += ", %d hidden by tsan.supp" % sum(hid.values())
        if count:
            note += ", %d sanitizer report%s" % (count, "" if count == 1 else "s")
            if count != new:
                note += ", %d known" % (count - new)
        if code not in (0, SKIPPED):
            note += ", no result" if code is None else ", exit %d" % code
        print("%-6s%-18s%s" % (verdict, test.name, note))
        if verdict == "known":
            print("      known: " + "; ".join(why_reported + match))
        if verdict != "ok":
            for l in (out + reports).rstrip().splitlines():
                print("      | " + l)
        {"ok": passed, "known": known_seen, "FAIL": failed, "skip": skipped}[verdict].append(test.name)

    print("run_all%s: %d passed, %d skipped, %d known failures, %d failed%s" % (
        " --tsan" if tsan else " --asan" if asan else " --coverage" if coverage else "",
        len(passed), len(skipped), len(known_seen), len(failed),
        ": " + ", ".join(failed) if failed else ""))
    if coverage is not None:
        return 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
