"""Compile the guest lock's handling of suspend and resume and drive it.

Takes bridge_resolve_handle, bridge_NtSuspendThread, bridge_NtResumeThread,
guest_lock_hand_over_to_resumed and kernel_call_blocks from
src/kernel/kernel_bridge.c, and xbox_thread_guest_priority,
xbox_thread_is_current, xbox_NtSuspendThread and xbox_NtResumeThread from
src/kernel/kernel_thread.c, all verbatim, and builds them with
src/platform/win32_compat.c's threads, against a guest lock that is a plain
mutex and a hand-over that is recorded rather than made (test_main.c). POSIX
only (win32_compat.c is the Linux and macOS side); requires GCC on PATH.
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("guest_suspend: skipped, POSIX only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
bridge = (root / "src/kernel/kernel_bridge.c").read_text(encoding="utf-8")
thread = (root / "src/kernel/kernel_thread.c").read_text(encoding="utf-8")
here = root / "tests/guest_suspend"


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


def line(text, pattern):
    return re.search(pattern, text, re.M).group(0) + "\n"


source = ("""
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "xbox_winnt.h"
#include "kernel/kernel.h"

/* Guest memory and the guest registers the bridges use. */
static uint8_t g_guest[0x10000];
ptrdiff_t g_xbox_mem_offset;
static __thread uint32_t g_eax, g_esp;
""" + line(bridge, r"^#define BRIDGE_MEM32\(addr\).*$")
    + line(bridge, r"^#define XBOX_TO_NATIVE\(va\).*$")
    + line(bridge, r"^#define STACK_ARG\(n\).*$")
    + cut(bridge, "#ifndef BRIDGE_HANDLE_TAG\n#define BRIDGE_HANDLE_TAG  0x48000000u\n#endif\n"
                  "#define BRIDGE_HANDLE_MASK", "\n\n")
    + """
static uint32_t gcs_caller(void) { return 0; }
void xbox_log(int level, const char *subsystem, const char *fmt, ...)
{
    (void)level; (void)subsystem; (void)fmt;
}

/* The guest lock: a mutex, with each thread's gives and takes counted. */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread int t_guest_holds, t_in_callback;
static __thread int t_releases, t_acquires;
static __thread unsigned char t_irql;
unsigned char xbox_KeCurrentIrql(void) { return t_irql; }
void xbox_guest_lock_acquire(void)
{
    if (t_guest_holds) return;
    pthread_mutex_lock(&g_lock);
    t_guest_holds = 1;
    t_acquires++;
}
void xbox_guest_lock_release(void)
{
    if (!t_guest_holds) return;
    t_guest_holds = 0;
    t_releases++;
    pthread_mutex_unlock(&g_lock);
}

/* The hand-over, recorded. */
static unsigned long g_resume_handoffs, g_resume_handoff_timeouts;
static int g_hand_overs, g_hand_over_even, g_hand_over_counters_ok;
static void guest_lock_hand_over(int even_if_none_queued,
                                 unsigned long *handoffs, unsigned long *timeouts)
{
    g_hand_overs++;
    g_hand_over_even = even_if_none_queued;
    g_hand_over_counters_ok = handoffs == &g_resume_handoffs
                           && timeouts == &g_resume_handoff_timeouts;
}
""" + cut(bridge, "static HANDLE bridge_resolve_handle(uint32_t token)\n{", "static HANDLE bridge_take_handle")
    + cut(bridge, "/* -- NtSuspendThread (ordinal 231, 2 args)", "/* ── ObfDereferenceObject")
    + cut(bridge, "/* After a resume that let go a thread above the caller's priority", "/* Called from the function-entry trace hook")
    + cut(bridge, "/* Kernel calls that can block:", "\n\n/* ---- what each guest thread is doing")
    + "\n"
    + line(thread, r"^static int\s+guest_priority_get\(HANDLE h\)\s+\{ return GetThreadPriority\(h\); \}$")
    + cut(thread, "/* The priority the title gave a thread (the guest lock asks it). */", "LONG __stdcall xbox_KeSetBasePriorityThread")
    + cut(thread, "NTSTATUS __stdcall xbox_NtSuspendThread(", "/* ================================================================\n * Worker stack slices")
    + (here / "test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="guest_suspend-") as directory:
    exe = str(pathlib.Path(directory) / "guest_suspend")
    compat = str(pathlib.Path(directory) / "compat.o")
    subprocess.run(["gcc", "-O2", "-std=gnu11", "-pthread", "-I", "src", "-I", "src/platform", "-c",
                    "src/platform/win32_compat.c", "-o", compat], cwd=root, check=True)
    # -Wno-comment: kernel.h has a "/*" inside a comment, which is upstream's.
    subprocess.run(["gcc", "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-comment",
                    "-pthread", "-I", "src", "-I", "src/platform",
                    "-x", "c", "-", "-x", "none", compat, "-lm", "-o", exe],
                   cwd=root, input=source, text=True, check=True)
    env = {k: v for k, v in os.environ.items()
           if k not in ("RECOMP_SUSPEND_REAL", "RECOMP_SUSPEND_STRICT")}
    try:
        run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("guest_suspend: no end in 60 s (a thread parked holding the guest lock, "
                 "or a resume lost)")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "guest_suspend: all cases pass" not in text:
        sys.exit(f"guest_suspend: exit {run.returncode}")
