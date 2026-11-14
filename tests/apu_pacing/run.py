"""Pace the APU's frame thread against a device, without the APU.

Cut by text: from src/apu/apu_core.c and src/apu/apu_sdl.c, with two
defines from src/apu/apu_regs.h, unchanged. Neither file links on its own.

Takes ring_target_frames, ring_ahead and throttle from src/apu/apu_core.c,
and the SDL output ring with its fake sink (a thread that pulls 512 frames
every 10.7 ms, as the device does) from src/apu/apu_sdl.c, verbatim, and
runs mcpx_apu_frame_thread's loop with the work left out, for three seconds,
with the ring (the default) and on the wall clock (the argument "wall": no
ring, as with XAudio2). The voice processor must make its frames at 48 kHz,
close together: DirectSound waits for the next one each time it restarts a
sound. With the ring, the device must never run dry.

A stall of the machine is set aside and counted apart, with the tenth of a
second after it (test_main.c says how one is told from the pacing): known
bug 22 was a container stalling for 44 to 90 ms, about one run in six, and
the test failing on that one gap. So each case runs twice: as it is, and
with the whole test stopped for STALL_S twice (SIGSTOP, as a container
stops it), which must be set aside and leave the same pacing. POSIX only
(the fake sink is pthreads'); requires GCC on PATH.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import re
import shlex
import signal
import subprocess
import sys
import tempfile
import time

if os.name == "nt":
    print("apu_pacing: skipped, POSIX only")
    sys.exit(77)   # skipped (tests/run_all.py)

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
core = (root / "src/apu/apu_core.c").read_text(encoding="utf-8")
sdl = (root / "src/apu/apu_sdl.c").read_text(encoding="utf-8")
regs = (root / "src/apu/apu_regs.h").read_text(encoding="utf-8")


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


def define(text, name):
    return re.search(r"^#define " + name + r"\b.*$", text, re.M).group(0) + "\n"


source = ("""
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef pthread_mutex_t QemuMutex;
typedef pthread_cond_t QemuCond;
typedef struct {
    QemuMutex lock;
    QemuCond cond;
    bool pause_requested;
    int ep_frame_div;
    int sleep_acc_us;
    int64_t next_frame_time_us;
} MCPXAPUState;
#define QEMU_CLOCK_REALTIME 1
static int64_t qemu_clock_get_us(int type)
{
    struct timespec ts;
    (void)type;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
/* What the test notes of throttle's waits, to tell a stall of the machine
 * from the pacing (test_main.c): how long it has asked to wait, in all, and
 * the longest the device had gone without a pull when a wait ended. */
static int64_t g_asked_us;
static int g_pull_overdue_ms;
int xa2_ms_since_pull(void);
static void qemu_cond_timedwait(QemuCond *c, QemuMutex *m, int ms)
{
    struct timespec ts;
    int since;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += (long)ms * 1000000L;
    while (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }
    pthread_cond_timedwait(c, m, &ts);
    g_asked_us += (int64_t)ms * 1000;
    since = xa2_ms_since_pull();
    if (since > g_pull_overdue_ms) g_pull_overdue_ms = since;
}
typedef uint32_t SDL_AudioDeviceID;
typedef uint8_t Uint8;
int g_apu_throttle_resets;
int64_t g_apu_throttle_lost_us;
""" + define(regs, "NUM_SAMPLES_PER_FRAME") + define(regs, "EP_FRAME_US")
          + cut(sdl, "#define SA_RATE", "int xa2_init(void)")
          + sdl[sdl.index("int xa2_get_buffer_size(void)"):]
          + cut(core, "static int ring_target_frames(void)",
                "/* ============================================================\n * se_frame")
          + (root / "tests/apu_pacing/test_main.c").read_text(encoding="utf-8"))

# Under the thread sanitizer (tests/run_all.py --tsan sets TEST_SANITIZER),
# which slows every access, the timings aren't checked.
sanitizer = os.environ.get("TEST_SANITIZER")

STALL_S = 0.07          # how long the test is stopped: bug 22's were 44 to 90 ms
STALL_AT = (1.0, 2.0)   # seconds into its three, after the half second it warms up in
MEASURED_S = 1.25       # at least this much of the 2.5 s left to measure


def run_case(exe, args, env, stalled):
    """The test's exit code and what it printed; when stalled, stopped whole
    for STALL_S at each of STALL_AT."""
    process = subprocess.Popen([exe] + args, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    start = time.monotonic()
    for at in STALL_AT if stalled else ():
        time.sleep(max(0.0, start + at - time.monotonic()))
        process.send_signal(signal.SIGSTOP)
        try:
            time.sleep(STALL_S)
        finally:
            process.send_signal(signal.SIGCONT)
    try:
        text = process.communicate(timeout=60)[0]
    except subprocess.TimeoutExpired:
        process.kill()
        raise
    return process.returncode, text


with tempfile.TemporaryDirectory(prefix="apu_pacing-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    command = cc + ["-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread", "-x", "c", "-",
               "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, args, stalled in (("ring", [], False), ("wall clock", ["wall"], False),
                                ("ring, the machine stalled", [], True),
                                ("wall clock, the machine stalled", ["wall"], True)):
        env = {k: v for k, v in os.environ.items()
               if k not in ("RECOMP_APU_RING_MS", "RECOMP_APU_SINK_RAW", "RECOMP_APU_STATS")}
        code, text = run_case(exe, args, env, stalled)
        if code != 0:
            raise SystemExit(f"apu_pacing {name}: exit {code}\n{text}")
        stalls, measured = re.search(r"stalls of the machine set aside: (\d+), .*; ([\d.]+) s measured",
                                     text).groups()
        rate = float(re.search(r"frames a second: (\d+)", text).group(1))
        wait = float(re.search(r"wait for the next frame: ([\d.]+) ms", text).group(1))
        p99 = float(re.search(r"gaps: 99% under ([\d.]+) ms", text).group(1))
        under = int(re.search(r"underruns after the first half second: (\d+)", text).group(1))
        print(f"apu_pacing {name}:", "; ".join(text.splitlines()))
        if sanitizer:
            continue
        if stalled and int(stalls) < len(STALL_AT):
            raise SystemExit(f"apu_pacing {name}: stopped {len(STALL_AT)} times for "
                             f"{STALL_S * 1000:.0f} ms, and {stalls} set aside")
        if float(measured) < MEASURED_S:
            raise SystemExit(f"apu_pacing {name}: the machine stalled {stalls} times, and only "
                             f"{measured} s of the pacing could be measured")
        # 48 kHz in frames of 32 samples
        if abs(rate - 1500) > 30:
            raise SystemExit(f"apu_pacing {name}: {rate:.0f} frames a second, not 1500")
        # the waits are whole milliseconds: a frame or two at each
        if wait > 1.0 or p99 > 2.5:
            raise SystemExit(f"apu_pacing {name}: frames too far apart "
                             f"({wait:.2f} ms to the next on average, 99% of gaps under {p99:.2f} ms)")
        if not args and under:
            raise SystemExit(f"apu_pacing {name}: the device ran dry {under} times")
print("apu_pacing: all four cases ran; timings not checked under the sanitizer" if sanitizer
      else "apu_pacing: all four cases pass")
