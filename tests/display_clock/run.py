"""Frame pacing in step with the display's refreshes as the kernel counts
them, without a display.

src/nv2a_gl/nv2a_display_clock.c as it is, with the kernel's side of it
(open, ioctl, close and the clock) played by test_main.c: cards, the displays
on them and when they refresh. With it, the part of frame_pace that paces by
it, taken from src/nv2a_gl/nv2a_gl.c as it is. A game hands over 60 frames a
second to a compositor that shows the newest finished one at each refresh;
each case says how many refreshes the frames stayed on screen.

Linux, with the kernel's headers (linux-libc-dev); requires GCC on PATH.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

if not sys.platform.startswith("linux"):
    print("display_clock: skipped, Linux only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")

# The block of frame_pace that sets the next deadline, from the presenter's
# lock and the display's to the game's own period.
start = gl.index("                double ref = 0.0, refresh = 0.0, at = 0.5")
end = gl.index("            }\n#else\n            deadline += period;\n#endif", start)
lock = gl[start:end]

source = (root / "tests/display_clock/test_main.c").read_text(encoding="utf-8")
source = source.replace("/* FRAME_PACE_LOCK */\n", lock)

with tempfile.TemporaryDirectory(prefix="display-clock-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    subprocess.run(["gcc", "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DNV2A_GL_USE_SDL=1",
                    "-I", str(root / "src/nv2a_gl"), "-x", "c", "-", "-o", exe, "-lm"],
                   cwd=root, input=source, text=True, check=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_PACE")}
    run = subprocess.run([exe], capture_output=True, text=True, timeout=120, env=env)
    print(run.stdout + run.stderr, end="")
    if run.returncode != 0:
        raise SystemExit(f"display_clock: exit {run.returncode}")
