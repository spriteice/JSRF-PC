"""The [APU] line that names the sound back end.

apu_core.c's mcpx_apu_monitor_init opens the sound back end, apu_sdl.c
(SDL2's audio), behind apu_xaudio2.h's entry points, and said "Using XAudio2
audio backend" when it opened and "XAudio2 unavailable" when it didn't: the
toolkit's XAudio2 output, which JSRF-PC doesn't have. This builds apu_core.c
with the voice processor it links, as the engine does, with test_main.c
standing in for apu_sdl.c, and checks the line when the back end opens, and
on Linux and macOS when it doesn't (on Windows the fallback would open the
PC's own sound device with waveOut, so that case is left out there): it
names apu_sdl.c, and never XAudio2.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))

cases = [("open", "[APU] Sound back end open (apu_sdl.c)")]
if sys.platform != "win32":
    cases.append(("fail", "[APU] Sound back end (apu_sdl.c) didn't open, falling back to waveOut"))

ok = True
with tempfile.TemporaryDirectory(prefix="apu_backend_line-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    command = cc + ["-std=gnu11", "-O2", "-Isrc/apu", "-Isrc", "-Isrc/nv2a",
                    "tests/apu_backend_line/test_main.c", "src/apu/apu_core.c",
                    "src/apu/apu_vp.c", "src/apu/apu_dsp.c",
                    "src/apu/apu_channels.c", "src/apu/apu_gpfx.c", "-o", exe]
    if sys.platform != "win32":
        command += ["src/platform/win32_compat.c", "-lm", "-lpthread"]
    else:
        command += ["-lwinmm"]  # apu_core.c's waveOut output, as src/apu/CMakeLists.txt links it
    subprocess.run(command, cwd=root, check=True)
    for arg, want in cases:
        run = subprocess.run([exe, arg], capture_output=True, text=True, timeout=60)
        lines = run.stderr.splitlines()
        good = run.returncode == 0 and lines[:1] == [want] and "XAudio2" not in run.stderr
        print(f"{'ok  ' if good else 'FAIL'}  back end {arg}: {lines[:1]}"
              + ("" if good else f"\n      exit {run.returncode}, wanted {want!r}\n{run.stdout}{run.stderr}"))
        ok = ok and good
if not ok:
    sys.exit(1)
print("apu_backend_line: all cases pass")
