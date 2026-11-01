#!/usr/bin/env python3
"""Run every engine test (each tests/<name>/run.py), as CI does.

    python3 tests/run_all.py [--tsan] [name...]

With names, only those tests. Each test passes, fails, or fails in a way
listed in tests/known-failures.txt: a known bug, still open, whose failure is
reported but doesn't fail the run. Any other failure of a listed test does.
The exit status is 1 if any test failed.

--tsan runs the tests that start threads (those whose files mention
pthread_create, CreateThread or std::thread) built with the thread
sanitizer: CC, or clang, with -fsanitize=thread. A test fails if the
sanitizer reports anything tests/tsan.supp doesn't list, or if it fails. It
sets TEST_SANITIZER=thread, and a test whose checks depend on timing leaves
those out under it, as cpuprof, heap_lock and nv2a_queue do.
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


def known_failures():
    """{test: [(line, why)]} from known-failures.txt."""
    out = {}
    for n, raw in enumerate((root / "tests/known-failures.txt").read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in raw.split(" | ")]
        if len(parts) != 3 or not all(parts):
            raise SystemExit("tests/known-failures.txt:%d: not 'test | line | why'" % n)
        out.setdefault(parts[0], []).append((parts[1], parts[2]))
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
    names = [a for a in argv if a != "--tsan"]
    if any(a.startswith("-") for a in names):
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    tests = sorted(pathlib.Path(p).parent for p in glob.glob(str(root / "tests/*/run.py")))
    known = known_failures()
    stale = set(known) - {t.name for t in tests}
    if stale:
        raise SystemExit("run_all: tests/known-failures.txt names tests with no run.py: "
                         + ", ".join(sorted(stale)))
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
    passed, failed, known_seen = [], [], []
    for test in tests:
        with tempfile.TemporaryDirectory(prefix="tsan-") as logs:
            if tsan:
                # Reports go to files, whatever run.py does with the test's
                # output; and they don't change its exit status, so each of
                # its cases runs.
                env["TSAN_OPTIONS"] = " ".join([
                    "suppressions=" + str(supp), "log_path=" + os.path.join(logs, "report"),
                    "exitcode=0", "halt_on_error=0", "second_deadlock_stack=1"])
            code, out, secs = run(test, env)
            reports = "".join(pathlib.Path(f).read_text(encoding="utf-8", errors="replace")
                              for f in sorted(glob.glob(os.path.join(logs, "report*"))))
        races = reports.count("WARNING: ThreadSanitizer")
        lines = out.splitlines()
        match = [why for line, why in known.get(test.name, [])
                 if any(fnmatch.fnmatchcase(l, line) for l in lines)]
        if races:
            verdict = "FAIL"
        elif code == 0:
            verdict = "ok"
        elif match:
            verdict = "known"
        else:
            verdict = "FAIL"
        note = "%5.1f s" % secs
        if races:
            note += ", %d sanitizer report%s" % (races, "" if races == 1 else "s")
        if code != 0:
            note += ", no result" if code is None else ", exit %d" % code
        print("%-6s%-18s%s" % (verdict, test.name, note))
        if verdict == "known":
            print("      known: " + "; ".join(match))
        if verdict != "ok":
            for l in (out + reports).rstrip().splitlines():
                print("      | " + l)
        {"ok": passed, "known": known_seen, "FAIL": failed}[verdict].append(test.name)

    print("run_all%s: %d passed, %d known failures, %d failed%s" % (
        " --tsan" if tsan else "", len(passed), len(known_seen), len(failed),
        ": " + ", ".join(failed) if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
