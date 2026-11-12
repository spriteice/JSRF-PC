"""Compile the freeze watch, the report thread it runs on and the [THREADS]
report, and stop the picture (test_main.c).

Cut by text from src/kernel/kernel_bridge.c, unchanged: kernel_report_thread,
which carries the guest lock's reports and the freeze watch,
kernel_freeze_watch, and the table of what each guest thread is doing with
kthread_report. Built with src/platform/win32_compat.c's threads, against
stubs for the guest lock's reports and the counts the freeze report prints.
POSIX only (win32_compat.c is the Linux side); requires GCC on PATH.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("thread_reports: skipped, POSIX only")
    sys.exit(77)   # skipped (tests/run_all.py)

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
bridge = (root / "src/kernel/kernel_bridge.c").read_text(encoding="utf-8")
here = root / "tests/thread_reports"


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


source = ((here / "prelude.c").read_text(encoding="utf-8")
          + cut(bridge, "#define KTHREAD_SLOTS 32", "/* Exposed so the crash handler")
          + "static void kthread_report(void);\n"
          + cut(bridge, "#define KERNEL_REPORT_MS 20", "void xbox_guest_lock_release(void)")
          + cut(bridge, "/* Say so when the picture stops", "/* How long the guest has been stuck")
          + (here / "test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="thread_reports-") as directory:
    c = pathlib.Path(directory) / "thread_reports.c"
    c.write_text(source, encoding="utf-8")
    exe = str(pathlib.Path(directory) / "thread_reports")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-function",
                         "-Wno-unused-parameter", "-Wno-comment", "-Werror", "-pthread",
                         "-I", "src", "-I", "src/platform", "-I", "src/kernel", str(c),
                         "src/platform/win32_compat.c", "-lm", "-o", exe], cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("thread_reports: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "thread_reports: all cases pass" not in text:
        sys.exit(f"thread_reports: exit {run.returncode}")
