"""Tests for CI's own scripts, run with the tool tests in CI's GCC job
(.github/ci/linux.sh): fetch.py's tries, and how tests/run_all.py names a
sanitizer's leak."""

import hashlib
import http.server
import importlib.util
import pathlib
import sys
import threading
import urllib.error

import pytest

here = pathlib.Path(__file__).resolve().parent
root = here.parents[1]
sys.path.insert(0, str(here))
import fetch  # noqa: E402

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
