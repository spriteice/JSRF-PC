"""The [APU] line that says what the sound device took, without SDL.

src/apu/apu_sdl.c asks SDL for 48 kHz stereo s16 in 512-frame pulls and
lets it convert, so what SDL hands back is always that, and the back-end
line said so on every device (the sound audit's O4). apu_device_line.h's
line adds what the default device itself mixes at, as SDL says. This builds
test_main.c with the header and makes the line for: Windows' shared mode
(48 kHz float, no buffer size said), a Linux sound server at 44.1 kHz with
its buffer, a device already in the ring's format, one whose format word
has no name here, and one SDL said nothing of, with no driver named. Each
must be the line given, whole, and say SDL converts only where the two
differ.
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

RING = "the ring is pulled at 48000 Hz, 2 channels, s16, 512 frames (2048 bytes) a pull"
cases = [
    ("Windows, shared mode",
     ["wasapi", "48000", "2", "8010", "512", "2048", "1", "48000", "2", "8120", "0"],
     f"[APU] the sound device as opened: the wasapi driver; the device mixes at 48000 Hz, "
     f"2 channels, f32; {RING}, SDL converting between them"),
    ("a Linux sound server",
     ["pulseaudio", "48000", "2", "8010", "512", "2048", "1", "44100", "2", "8010", "1024"],
     f"[APU] the sound device as opened: the pulseaudio driver; the device mixes at 44100 Hz, "
     f"2 channels, s16, 1024 frames a buffer; {RING}, SDL converting between them"),
    ("the ring's own format",
     ["pipewire", "48000", "2", "8010", "512", "2048", "1", "48000", "2", "8010", "0"],
     f"[APU] the sound device as opened: the pipewire driver; the device mixes at 48000 Hz, "
     f"2 channels, s16; {RING}"),
    ("a format with no name here",
     ["alsa", "48000", "2", "8010", "512", "2048", "1", "96000", "6", "1234", "0"],
     f"[APU] the sound device as opened: the alsa driver; the device mixes at 96000 Hz, "
     f"6 channels, format 0x1234; {RING}, SDL converting between them"),
    ("nothing said, no driver",
     ["-", "48000", "2", "8010", "512", "2048", "0", "0", "0", "0", "0"],
     f"[APU] the sound device as opened: the driver not named; the device's own format not "
     f"said by SDL; {RING}"),
]

ok = True
with tempfile.TemporaryDirectory(prefix="apu_device_line-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-Isrc/apu",
                         "tests/apu_device_line/test_main.c", "-o", exe], cwd=root, check=True)
    for name, args, want in cases:
        run = subprocess.run([exe, *args], capture_output=True, text=True, timeout=30)
        got = run.stdout.rstrip("\n")
        good = run.returncode == 0 and got == want
        print(f"{'ok  ' if good else 'FAIL'}  {name}"
              + ("" if good else f"\n      got  {got!r}\n      want {want!r}"))
        ok = ok and good
if not ok:
    sys.exit(1)
print("apu_device_line: all cases pass")
