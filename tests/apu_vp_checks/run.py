"""Compile the voice processor and give it numbers past the end of its own
state, as the game could (test_main.c): a current voice past the last, and
one of 2^31 and more; VOICE_ON past the last voice; an HRTF entry past the
last; and a frame with a voice past the last in its list. Each must leave
the voice processor's state as it was and say so in the log once, with one
[CHECK] line for each check (src/platform/recomp_check.h), however often
it's reached; and the same methods for a voice that exists must still work.
Takes src/apu/apu_vp.c with what it links, as tests/apu_mix does. Requires
GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import collections
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))

# Each check the cases reach: (function, condition) and how many places
# have it. The two SSL methods have one each, as do SET_HRIR and _X.
want = collections.Counter({
    ("fe_method", "current_voice < MCPX_HW_MAX_VOICES"): 2,
    ("voice_lock", "v < MCPX_HW_MAX_VOICES"): 1,
    ("fe_method", "selected_handle < MCPX_HW_MAX_VOICES"): 1,
    ("fe_method", "(unsigned)d->vp.hrtf.current_entry < HRTF_ENTRY_COUNT"): 2,
    ("fe_method", "handle < HRTF_ENTRY_COUNT"): 1,
    ("is_voice_locked", "v < MCPX_HW_MAX_VOICES"): 1,
    ("voice_process", "v < MCPX_HW_MAX_VOICES"): 1,
})

with tempfile.TemporaryDirectory(prefix="apu_vp_checks-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    command = cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Isrc/apu", "-Isrc", "-Isrc/nv2a",
                    "src/apu/apu_vp.c", "src/apu/apu_dsp.c",
                    "src/apu/apu_channels.c", "src/apu/apu_gpfx.c",
                    "tests/apu_vp_checks/test_main.c", "-o", exe]
    if sys.platform != "win32":
        command += ["src/platform/win32_compat.c", "-lm", "-lpthread"]
    subprocess.run(command, cwd=root, check=True)
    env = {k: v for k, v in os.environ.items()
           if not k.startswith("RECOMP_APU_") and k != "RECOMP_MUTE"}
    try:
        run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("apu_vp_checks: no end in 60 s")
    print(run.stdout, end="")
    lines = run.stderr.splitlines()
    checks = [l for l in lines if l.startswith("[CHECK]")]
    for line in checks:
        print(line)
    # The second round must add nothing.
    second = lines.index("apu_vp_checks: round 2") if "apu_vp_checks: round 2" in lines else -1
    late = [l for l in lines[second + 1:] if l.startswith("[CHECK]")] if second >= 0 else ["(no round 2)"]
    got = collections.Counter()
    for line in checks:
        m = re.match(r"\[CHECK\] apu_vp\.c:\d+ in (\w+): (.*) failed: ", line)
        got[(m.group(1), m.group(2)) if m else ("?", line)] += 1
    ok = True
    if run.returncode != 0 or "apu_vp_checks: all cases pass" not in run.stdout:
        print(f"apu_vp_checks: exit {run.returncode}")
        ok = False
    if got != want:
        print("apu_vp_checks: [CHECK] lines, wanted", dict(want), "got", dict(got))
        ok = False
    if late:
        print("apu_vp_checks: the second round said again:", late)
        ok = False
    if not ok:
        sys.exit(1)
