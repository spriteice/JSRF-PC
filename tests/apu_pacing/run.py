"""Pace the APU's frame thread against a device, without the APU.

Takes ring_target_frames, ring_ahead and throttle from src/apu/apu_core.c,
and the SDL output ring with its fake sink (a thread that pulls 512 frames
every 10.7 ms, as the device does) from src/apu/apu_sdl.c, verbatim, and
runs mcpx_apu_frame_thread's loop with the work left out, for three seconds,
with the ring (the default) and on the wall clock (RECOMP_APU_RING_MS=0, as
with XAudio2). The voice processor must make its frames at 48 kHz, close
together: DirectSound waits for the next one each time it restarts a sound.
With the ring, the device must never run dry. POSIX only (the fake sink is
pthreads'); requires GCC on PATH.
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("apu_pacing: skipped, POSIX only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
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
static void qemu_cond_timedwait(QemuCond *c, QemuMutex *m, int ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += (long)ms * 1000000L;
    while (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }
    pthread_cond_timedwait(c, m, &ts);
}
typedef uint32_t SDL_AudioDeviceID;
typedef uint8_t Uint8;
int g_apu_throttle_resets;
int64_t g_apu_throttle_lost_us;
""" + define(regs, "NUM_SAMPLES_PER_FRAME") + define(regs, "EP_FRAME_US")
          + cut(sdl, "#define SA_RATE", "int xa2_init(void)")
          + cut(sdl, "int xa2_get_buffer_size(void)", "#endif /* !_WIN32 && !__APPLE__ */")
          + cut(core, "static int ring_target_frames(void)",
                "/* ============================================================\n * se_frame")
          + (root / "tests/apu_pacing/test_main.c").read_text(encoding="utf-8"))

with tempfile.TemporaryDirectory(prefix="apu_pacing-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    command = ["gcc", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread", "-x", "c", "-",
               "-o", exe]
    subprocess.run(command, cwd=root, input=source, text=True, check=True)
    for name, ring_ms in (("ring", None), ("wall clock", "0")):
        env = {k: v for k, v in os.environ.items()
               if k not in ("RECOMP_APU_RING_MS", "RECOMP_APU_SINK_RAW", "RECOMP_APU_STATS")}
        if ring_ms is not None:
            env["RECOMP_APU_RING_MS"] = ring_ms
        run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=60)
        text = run.stdout + run.stderr
        if run.returncode != 0:
            raise SystemExit(f"apu_pacing {name}: exit {run.returncode}\n{text}")
        rate = float(re.search(r"frames a second: (\d+)", text).group(1))
        wait = float(re.search(r"wait for the next frame: ([\d.]+) ms", text).group(1))
        p99 = float(re.search(r"gaps: 99% under ([\d.]+) ms", text).group(1))
        under = int(re.search(r"underruns after the first half second: (\d+)", text).group(1))
        print(f"apu_pacing {name}:", "; ".join(text.splitlines()))
        # 48 kHz in frames of 32 samples
        if abs(rate - 1500) > 30:
            raise SystemExit(f"apu_pacing {name}: {rate:.0f} frames a second, not 1500")
        # the waits are whole milliseconds: a frame or two at each
        if wait > 1.0 or p99 > 2.5:
            raise SystemExit(f"apu_pacing {name}: frames too far apart "
                             f"({wait:.2f} ms to the next on average, 99% of gaps under {p99:.2f} ms)")
        if ring_ms is None and under:
            raise SystemExit(f"apu_pacing {name}: the device ran dry {under} times")
print("apu_pacing: both cases pass")
