"""The APU's line in the [APU], [LOCK] and [FROZEN] reports, written whole.

kernel_bridge.c's three reports had the line written into a buffer of their
own, 320 or 256 bytes, which cut every line at 319 or 255 characters, losing
the voice counts at its end. The APU now writes it to the log itself
(mcpx_apu_debug_print). This builds src/apu/apu_core.c with the voice
processor it links, as the engine does, takes the three reports' prefixes
from src/kernel/kernel_bridge.c, and has the function write the line at its
longest, every counter at its largest, after each prefix (test_main.c):
each must come out whole, once, on one line. apu_core.c's sound output,
apu_sdl.c, needs SDL, so test_main.c stands in for it.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
bridge = (root / "src/kernel/kernel_bridge.c").read_text(encoding="utf-8")

prefixes = re.findall(r'mcpx_apu_debug_print\(stderr, "([^"\\]*)"\)', bridge)
if sorted(prefixes) != sorted(["  [APU] ", "  [LOCK] APU: ", "  [FROZEN] APU: "]):
    sys.exit(f"apu_debug_line: kernel_bridge.c's reports print the APU's line after {prefixes}, "
             "not '  [APU] ', '  [LOCK] APU: ' and '  [FROZEN] APU: '")

with tempfile.TemporaryDirectory(prefix="apu_debug_line-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    command = cc + ["-std=gnu11", "-O2", "-Isrc/apu", "-Isrc",
                    "tests/apu_debug_line/test_main.c", "src/apu/apu_core.c",
                    "src/apu/apu_vp.c", "src/apu/apu_dsp.c",
                    "src/apu/apu_channels.c", "src/apu/apu_gpfx.c", "-o", exe]
    if sys.platform != "win32":
        command += ["src/platform/win32_compat.c", "-lm", "-lpthread"]
    else:
        command += ["-lwinmm"]  # apu_core.c's waveOut output, as src/apu/CMakeLists.txt links it
    subprocess.run(command, cwd=root, check=True)
    run = subprocess.run([exe, *prefixes], capture_output=True, text=True, timeout=60)

lines = run.stdout.split("\n")
ok = run.returncode == 0 and len(lines) == len(prefixes) + 3 and lines[-1] == ""
if not ok:
    print(f"apu_debug_line: exit {run.returncode}, {len(lines) - 1} lines:\n{run.stdout}{run.stderr}")
    sys.exit(1)
big, small = lines[0].split()[1:]
want_end = f"voices processed {big} ({big} in stream format, {big} paused)"
if lines[1] != "no state: no APU":
    print(f"apu_debug_line: with no APU, {lines[1]!r}")
    ok = False
for prefix, line in zip(prefixes, lines[2:]):
    whole = (line.startswith(prefix + "SECTL=0xFFFFFFFF(xcnt=")
             and " TRAPPED HALTED -> pipeline STOPPED; " in line
             and f"the title has sent {small} methods ({small} VOICE_ON, {small} VOICE_OFF)" in line
             and line.endswith(want_end))
    print(f"{'ok  ' if whole else 'FAIL'}  {prefix.strip().rstrip(':')}: {len(line)} characters"
          + ("" if whole else f"\n      {line}"))
    ok = ok and whole
if not ok:
    sys.exit(1)
print("apu_debug_line: all cases pass")
