"""The renderer's timeline probe, off unless RECOMP_GL_TIMELINE asks for it.

Takes the [TIMELINE] probe from do_draw_body in src/nv2a_gl/nv2a_gl.c, and
vc_debug_switch, verbatim, and has test_main.c play 30 seconds of frames
through the probe: 300 draws a frame, with skinned batches, small quads and
a change of surface among them. With the switch not set, 0 or empty, the probe
must print nothing; with it 1, a whole frame every five seconds, as upstream's
did, and the vertex cache must stay on, since the probe reads nothing back.
Requires a C compiler on PATH (cc, or CC).
"""
import os
import pathlib
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "cc"))
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
probe = gl[gl.index("      /* [TIMELINE] -- the order things happen inside one frame."):
           gl.index("      /* The x-ray itself:")]
switch = gl[gl.index("#if defined(_WIN32)\n#  define VC_ENVIRON"):gl.index("static int vc_on(void)")]
head = """#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct { uint32_t frames, draws, color_offset; } S;
static double g_probe_now;
""" + switch + """
static void probe(int is_mover, int slot, uint32_t out_n, int curtain_skip)
{
""" + probe + """}
"""
main = (root / "tests/timeline/test_main.c").read_text(encoding="utf-8")

with tempfile.TemporaryDirectory(prefix="timeline-") as tmp:
    exe = str(pathlib.Path(tmp) / "test")
    subprocess.run(cc + ["-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
                         "-x", "c", "-", "-o", exe],
                   input=head + main, text=True, check=True)
    base = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
    for name, value, on in (("unset", None, False), ("zero", "0", False), ("empty", "", False),
                            ("on", "1", True)):
        env = dict(base)
        if value is not None:
            env["RECOMP_GL_TIMELINE"] = value
        run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=60)
        if run.returncode != 0:
            raise SystemExit(f"timeline {name}: exit {run.returncode}\n{run.stderr}")
        lines = run.stderr.splitlines()
        probed = [l for l in lines if "[TIMELINE]" in l]
        starts = [l for l in probed if "---- frame" in l]
        ends = [l for l in probed if "frame ends at draw" in l]
        if "[TEST] vc_debug_switch: none" not in lines:
            raise SystemExit(f"timeline {name}: the vertex cache would be turned off:\n"
                             + "\n".join(l for l in lines if l.startswith("[TEST]")))
        if not on:
            if probed:
                raise SystemExit(f"timeline {name}: the probe printed {len(probed)} lines, as:\n"
                                 + "\n".join(probed[:5]))
            continue
        # 30 s from a clock at 0: frames at about 5, 10, 15, 20 and 25 s, each
        # whole (it starts at the frame's first draw and says where it ends).
        if len(starts) != 5 or len(ends) != 5:
            raise SystemExit(f"timeline {name}: {len(starts)} frames begun and {len(ends)} ended:\n"
                             + "\n".join(probed[:20]))
        firsts = [probed[probed.index(s) + 1] for s in starts]
        if not all("now drawing into surface" in f for f in firsts):
            raise SystemExit(f"timeline {name}: a frame that doesn't start at its first draw:\n"
                             + "\n".join(firsts))
        each = len(probed) // len(starts)
        if each != 2 + 2 + 6 + 3:
            raise SystemExit(f"timeline {name}: {each} lines a frame, not 13:\n" + "\n".join(probed[:14]))
    # Any other RECOMP_GL_ switch still turns the cache off, so the check above
    # is the list's doing.
    env = dict(base, RECOMP_GL_SOMETHING_NEW="1")
    run = subprocess.run([exe, "switch"], env=env, capture_output=True, text=True, timeout=60)
    if "[TEST] vc_debug_switch: RECOMP_GL_SOMETHING_NEW" not in run.stderr:
        raise SystemExit("timeline: vc_debug_switch let an unknown switch through:\n" + run.stderr)
print("timeline: off unless RECOMP_GL_TIMELINE=1, and then a whole frame every 5 s; the vertex cache stays on")
