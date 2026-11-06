"""Tests for CI's own scripts, run with the tool tests in CI's GCC job
(.github/ci/linux.sh): fetch.py's tries, how tests/run_all.py names a
sanitizer's leak, checks that each known bug names its release and counts
what each tsan.supp entry hides, how count_warnings.py counts a build's
warnings, how coverage.py adds up the tests' line counts, how
launcher/images.sh names each image, and when llvm_mingw.py uses its copy
again."""

import hashlib
import http.server
import importlib.util
import pathlib
import subprocess
import sys
import threading
import urllib.error

import pytest

here = pathlib.Path(__file__).resolve().parent
root = here.parents[1]
sys.path.insert(0, str(here))
import fetch  # noqa: E402
import llvm_mingw  # noqa: E402
import count_warnings  # noqa: E402
import coverage  # noqa: E402

spec = importlib.util.spec_from_file_location("run_all", root / "tests" / "run_all.py")
run_all = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run_all)

BODY = b"the file CI wants\n" * 1000
SHA256 = hashlib.sha256(BODY).hexdigest()


@pytest.fixture
def server():
    """A server on 127.0.0.1 that answers each GET with the next of its
    answers (a status, or 200 with BODY), and counts the GETs."""
    state = {"answers": [], "gets": 0}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            state["gets"] += 1
            code = state["answers"].pop(0) if state["answers"] else 200
            self.send_response(code)
            self.send_header("Content-Length", str(len(BODY) if code == 200 else 0))
            self.end_headers()
            if code == 200:
                self.wfile.write(BODY)

        def log_message(self, *args):
            pass

    httpd = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    state["url"] = "http://127.0.0.1:%d/file" % httpd.server_address[1]
    yield state
    httpd.shutdown()
    httpd.server_close()


def test_fetch_tries_again_after_a_server_error(server, tmp_path):
    server["answers"] = [504, 503]
    path = tmp_path / "file"
    fetch.fetch(server["url"], str(path), SHA256, pause=0)
    assert path.read_bytes() == BODY
    assert server["gets"] == 3
    assert not (tmp_path / "file.part").exists()


def test_fetch_gives_up_after_its_tries(server, tmp_path):
    server["answers"] = [504] * 5
    with pytest.raises(urllib.error.HTTPError):
        fetch.fetch(server["url"], str(tmp_path / "file"), SHA256, pause=0)
    assert server["gets"] == 5
    assert list(tmp_path.iterdir()) == []


def test_fetch_doesnt_try_again_after_a_404(server, tmp_path):
    server["answers"] = [404]
    with pytest.raises(urllib.error.HTTPError):
        fetch.fetch(server["url"], str(tmp_path / "file"), SHA256, pause=0)
    assert server["gets"] == 1


def test_fetch_refuses_the_wrong_sha256(server, tmp_path):
    with pytest.raises(ValueError):
        fetch.fetch(server["url"], str(tmp_path / "file"), "0" * 64, pause=0)
    assert list(tmp_path.iterdir()) == []


# A leak in apu_mix's lock case (known bug 30), as GCC 11's LeakSanitizer
# reports it in CI's image, and as GCC 13's does on Ubuntu 24.04.
LEAK = """==197==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 48 byte(s) in 1 object(s) allocated from:
    #0 0x7fd35f2bd887 in __interceptor_malloc %s
    #1 0x5655202d524d in cv_lazy_init src/platform/win32_compat.c:129
    #2 0x5655202d6e0d in InitializeConditionVariable src/platform/win32_compat.c:139
    #3 0x5655202d2d66 in qemu_cond_init src/apu/../nv2a/qemu_shim.h:130
    #4 0x5655202d2d66 in main tests/apu_mix/test_main.c:186
    #5 0x7fd35e8edd8f  (/lib/x86_64-linux-gnu/libc.so.6+0x29d8f)

SUMMARY: AddressSanitizer: 48 byte(s) leaked in 1 allocation(s).
"""


@pytest.mark.parametrize("frame0", [
    "libsanitizer/asan/asan_malloc_linux.cpp:145",
    "../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:69",
])
def test_a_leak_is_named_by_its_first_frame_in_the_repository(frame0):
    assert run_all.report_lines(LEAK % frame0) == [
        "SUMMARY: LeakSanitizer: leak from src/platform/win32_compat.c:129 in cv_lazy_init"]


def test_a_leak_with_no_frame_in_the_repository():
    report = LEAK.replace(" src/", " /usr/src/") % "libsanitizer/asan/asan_malloc_linux.cpp:145"
    report = report.replace(" tests/", " /usr/tests/")
    assert run_all.report_lines(report) == ["SUMMARY: LeakSanitizer: leak from outside the repository"]


# A build's output as GCC and clang print it, in the container where the
# repository is warnings.root: our files by absolute path (a header by a
# path through ..), and by relative path; Dear ImGui's, the generated code's
# and SDL's, which aren't counted; and a note and a linker's warning, which
# aren't warnings in a file.
def build_log(repo):
    return "\n".join([
        "[1/9] Building C object xboxrecomp/src/kernel/CMakeFiles/xbox_kernel.dir/kernel_hal.c.o",
        "In file included from %s/src/kernel/kernel_hal.c:3:" % repo,
        "%s/src/kernel/../kernel/kernel.h:480:1: warning: \"/*\" within comment [-Wcomment]" % repo,
        "%s/src/kernel/kernel.h:480:1: warning: \"/*\" within comment [-Wcomment]" % repo,
        "%s/src/kernel/kernel_hal.c:102:13: warning: unused function 'tick' [-Wunused-function]" % repo,
        "%s/src/kernel/kernel_hal.c:102:13: note: declared here" % repo,
        "port/main.c:7:5: warning: unused variable 'x' [-Wunused-variable]",
        "%s/third_party/imgui/imgui.cpp:10:2: warning: something [-Wextra]" % repo,
        "/work/gen/recomp/gen/ci_stubs.c:3:1: warning: generated [-Wpedantic]",
        "/work/sdl-src/src/video/SDL_video.c:5:1: warning: SDL's [-Wunused]",
        "/usr/bin/ld: warning: libfoo.so, needed by libbar.so, not found",
    ])


def test_warnings_counts_ours_once_a_place():
    assert count_warnings.warnings(build_log(count_warnings.root.as_posix())) == [
        "port/main.c:7:5: unused variable 'x' [-Wunused-variable]",
        "src/kernel/kernel.h:480:1: \"/*\" within comment [-Wcomment]",
        "src/kernel/kernel_hal.c:102:13: unused function 'tick' [-Wunused-function]",
    ]


@pytest.mark.parametrize("allowed, code, says", [
    (3, 0, "allows 3"),
    (2, 1, "1 more than allowed"),
    (4, 0, "gcc's line in .github/ci/warnings.txt can come down to 3"),
])
def test_warnings_ratchet(tmp_path, monkeypatch, capsys, allowed, code, says):
    counts = tmp_path / "warnings.txt"
    counts.write_text("# counts\n\ngcc %d\nclang 0\n" % allowed)
    monkeypatch.setattr(count_warnings, "COUNTS", counts)
    log = tmp_path / "build.log"
    log.write_text(build_log(count_warnings.root.as_posix()))
    assert count_warnings.main(["gcc", str(log)]) == code
    assert says in capsys.readouterr().out


def test_warnings_at_zero_fails_on_any(tmp_path, monkeypatch):
    counts = tmp_path / "warnings.txt"
    counts.write_text("gcc 0\n")
    monkeypatch.setattr(count_warnings, "COUNTS", counts)
    clean, dirty = tmp_path / "clean.log", tmp_path / "dirty.log"
    clean.write_text("[1/1] Linking C executable jsrf_recomp\n")
    dirty.write_text("port/main.c:7:5: warning: unused variable 'x' [-Wunused-variable]\n")
    assert count_warnings.main(["gcc", str(clean)]) == 0
    assert count_warnings.main(["gcc", str(dirty)]) == 1


def test_warnings_txt_has_every_job():
    assert set(count_warnings.counts()) == {"gcc", "gcc-debug", "clang", "windows"}


def test_each_known_line_names_its_release(tmp_path, monkeypatch):
    (tmp_path / "tests").mkdir()
    monkeypatch.setattr(run_all, "root", tmp_path)
    known = tmp_path / "tests" / "known-failures.txt"
    known.write_text("# a comment\n\napu_pacing | apu_pacing *: late | b3 | known bug 22\n")
    assert run_all.known("known-failures.txt") == {"apu_pacing": [("apu_pacing *: late", "known bug 22")]}
    for line in ("apu_pacing | apu_pacing *: late | known bug 22",
                 "apu_pacing | apu_pacing *: late | later | known bug 22"):
        known.write_text(line + "\n")
        with pytest.raises(SystemExit):
            run_all.known("known-failures.txt")


def test_a_skipped_test_is_counted_apart(tmp_path, monkeypatch, capsys):
    tests = tmp_path / "tests"
    for name, code in (("passes", 0), ("skips", run_all.SKIPPED)):
        (tests / name).mkdir(parents=True)
        (tests / name / "run.py").write_text("import sys\nprint('%s: here')\nsys.exit(%d)\n" % (name, code))
    for name in ("known-failures.txt", "asan-known.txt", "tsan.supp"):
        (tests / name).write_text("")
    monkeypatch.setattr(run_all, "root", tmp_path)
    assert run_all.main([]) == 0
    out = capsys.readouterr().out
    assert "skip  skips" in out
    assert "run_all: 1 passed, 1 skipped, 0 known failures, 0 failed" in out


def test_each_tsan_group_names_its_release_or_design():
    supp = "\n".join([
        "# The file's own comment, with no entries.",
        "",
        "# Known bug 20. Fixed by b3.",
        "race:^a$",
        "race:^b$",
        "",
        "# By design: a fence word.",
        "race:^c$",
        "",
        "# Known bug 25. Fixed by 1.0, as far as it goes.",
        "race:^d$",
        "",
        "# Known bug 26, no release.",
        "race:^e$",
        "",
        "# Known bug 27. Fixed by someday.",
        "race:^f$",
    ])
    assert [p.split(":")[0] for p in run_all.supp_releases(supp)] == ["14", "17"]
    assert run_all.supp_releases((root / "tests" / "tsan.supp").read_text(encoding="utf-8")) == []


def test_each_tsan_entry_hides_at_most_its_line():
    supp = "\n".join([
        "# Known bug 20. Fixed by b3.",
        "race_top:^a$",
        "#   hides at most: one 3, two 40000",
        "race:^b$",
        "",
        "# hides at most: one 2",
        "race:^c$",
        "#   hides at most: one x",
    ])
    limits, problems = run_all.supp_limits(supp)
    assert limits == {"race_top:^a$": {"one": 3, "two": 40000}, "race:^b$": {}, "race:^c$": {}}
    assert [p.split(":")[0] for p in problems] == ["6", "8"]
    limits, problems = run_all.supp_limits((root / "tests" / "tsan.supp").read_text(encoding="utf-8"))
    assert problems == [] and limits["race_top:^recomp_mmio_write$"]


def test_tsan_hidden_reports_summed_over_a_tests_programs():
    reports = "\n".join([
        "WARNING: ThreadSanitizer: data race (pid=1)",
        "SUMMARY: ThreadSanitizer: data race x.c:1 in f",
        "ThreadSanitizer: Matched 5 suppressions (pid=1):",
        "2 race_top:^recomp_mmio_write$",
        "3 race:^s_hc$",
        "ThreadSanitizer: reported 1 warnings",
        "ThreadSanitizer: Matched 4 suppressions (pid=2):",
        "4 race_top:^recomp_mmio_write$",
    ])
    assert run_all.hidden(reports) == {"race_top:^recomp_mmio_write$": 6, "race:^s_hc$": 3}


def test_coverage_merges_each_tests_counts(tmp_path, monkeypatch):
    # Two tests build src/a.c, each reaching a line of it; port/b.c is
    # built and not run; the generated code and the system's headers aren't
    # ours.
    (tmp_path / "one").mkdir()
    (tmp_path / "two").mkdir()
    (tmp_path / "one" / "a.gcno").write_bytes(b"")
    (tmp_path / "two" / "a.gcno").write_bytes(b"")
    (tmp_path / "two" / "-.gcno").write_bytes(b"")
    cwd = coverage.root.as_posix()
    reports = {
        "one": [{"current_working_directory": cwd, "files": [
            {"file": "src/a.c", "lines": [{"line_number": 1, "count": 3}, {"line_number": 2, "count": 0}]},
            {"file": "/usr/include/stdio.h", "lines": [{"line_number": 9, "count": 1}]}]}],
        "two": [{"current_working_directory": cwd + "/tests", "files": [
            {"file": "../src/a.c", "lines": [{"line_number": 2, "count": 1}, {"line_number": 3, "count": 0}]},
            {"file": "../port/b.c", "lines": [{"line_number": 5, "count": 0}]},
            {"file": "../port/recomp/gen/f.c", "lines": [{"line_number": 1, "count": 1}]}]}],
    }
    monkeypatch.setattr(coverage, "gcov", lambda note: None if note.name == "-.gcno" else reports[note.parent.name])
    found, unread = coverage.coverage(tmp_path)
    assert found == {"src/a.c": (2, 3), "port/b.c": (0, 1)}
    assert unread == ["two/-.gcno"]


@pytest.mark.parametrize("groups, recipe", [
    ("linux tests", "launcher/image.sh"),
    ("conformance", "launcher/image-386.sh"),
])
def test_images_named_for_their_recipe(groups, recipe):
    # launcher/images.sh's name needs no Docker: the groups, and the SHA-256
    # of the recipe that makes the image.
    sha = hashlib.sha256((root / recipe).read_bytes()).hexdigest()
    out = subprocess.run(["bash", str(root / "launcher/images.sh"), "name", groups],
                         capture_output=True, text=True, check=True).stdout.strip()
    assert out == "ghcr.io/spriteice/jsrf-pc-build:%s-%s" % (groups.replace(" ", "-"), sha)


def test_conformance_image_is_alone():
    r = subprocess.run(["bash", str(root / "launcher/images.sh"), "name", "conformance tests"],
                       capture_output=True, text=True)
    assert r.returncode == 2 and "usage" in r.stderr


def test_llvm_mingw_uses_a_whole_checked_copy_again(tmp_path, monkeypatch, capsys):
    # A tiny stand-in for the release's zip, by its own SHA-256.
    import io
    import zipfile
    blob = io.BytesIO()
    with zipfile.ZipFile(blob, "w") as z:
        z.writestr("tc/bin/gcc.exe", b"clang")
    data = blob.getvalue()
    monkeypatch.setattr(llvm_mingw, "NAME", "tc")
    monkeypatch.setattr(llvm_mingw, "SHA256", hashlib.sha256(data).hexdigest())
    fetched = []

    def fake_fetch(url, path, sha256):
        fetched.append(url)
        pathlib.Path(path).write_bytes(data)
    monkeypatch.setattr(llvm_mingw, "fetch", fake_fetch)
    unpacked = []
    real = zipfile.ZipFile.extractall
    monkeypatch.setattr(zipfile.ZipFile, "extractall",
                        lambda self, where: (unpacked.append(where), real(self, where)))
    d = str(tmp_path)
    bin_path = str((tmp_path / "tc" / "bin").resolve())

    assert llvm_mingw.main([d]) == 0                        # fetched, unpacked
    assert (len(fetched), len(unpacked)) == (1, 1)
    assert llvm_mingw.main([d]) == 0                        # both kept
    assert (len(fetched), len(unpacked)) == (1, 1)
    (tmp_path / "tc.unpacked").unlink()                     # an unpack cut short
    (tmp_path / "tc" / "bin" / "stale").write_text("x")
    assert llvm_mingw.main([d]) == 0                        # the zip kept, unpacked afresh
    assert (len(fetched), len(unpacked)) == (1, 2)
    assert not (tmp_path / "tc" / "bin" / "stale").exists()
    (tmp_path / "tc.zip").write_bytes(b"not it")            # a zip without the SHA-256
    assert llvm_mingw.main([d]) == 0                        # the marker still holds
    (tmp_path / "tc.unpacked").write_text("another\n")
    assert llvm_mingw.main([d]) == 0                        # fetched and unpacked again
    assert (len(fetched), len(unpacked)) == (2, 3)
    assert capsys.readouterr().out.split() == [bin_path] * 5
