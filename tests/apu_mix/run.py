"""Compile the voice processor and the fold to stereo, and play one voice
through them both ways.

Takes src/apu/apu_vp.c, apu_dsp.c and apu_channels.c as they are, and plays
one stereo voice into them (test_main.c), its right channel into the front
right mixbin or into the reverb send. With the fold to stereo (the default),
the device gets the mixbins: a channel sent to a bin the fold leaves out is
lost. With RECOMP_APU_MIX=xemu it gets the voice's own left and right, as
xemu plays them with Use DSP off, wherever they are sent, and the same on
every frame. Then "lock": a frame must wait while another thread has the
voice locked, and mix it as that thread left it. Requires GCC on PATH,
including MinGW on Windows.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
full = "left 16383, right -8191"
cases = {
    ("fold", 1): full,
    ("fold", 10): "left 16383, right 0",
    ("xemu", 1): full,
    ("xemu", 10): full,
}
failed = 0
with tempfile.TemporaryDirectory(prefix="apu_mix-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    command = ["gcc", "-std=gnu11", "-Isrc/apu", "-Isrc", "-Isrc/nv2a",
               "src/apu/apu_vp.c", "src/apu/apu_dsp.c",
               "src/apu/apu_channels.c", "tests/apu_mix/test_main.c", "-o", exe]
    if sys.platform != "win32":
        command += ["src/platform/win32_compat.c", "-lm", "-lpthread"]
    subprocess.run(command, cwd=root, check=True)
    env = {k: v for k, v in os.environ.items()
           if not k.startswith("RECOMP_APU_") and k != "RECOMP_MUTE"}
    for (mix, right_bin), want in cases.items():
        run_env = dict(env, RECOMP_APU_MIX=mix) if mix == "xemu" else env
        run = subprocess.run([exe, str(right_bin)], env=run_env, capture_output=True,
                             text=True, timeout=60)
        ok = run.returncode == 0 and f"bin {right_bin}: {want}" in run.stdout
        print(f"{mix}, right channel into bin {right_bin}: "
              f"{'ok' if ok else 'FAILED'} ({run.stdout.strip() or run.returncode})")
        failed += not ok
    run = subprocess.run([exe, "lock"], env=env, capture_output=True, text=True,
                         timeout=60)
    ok = (run.returncode == 0 and "the frame waited for the unlock" in run.stdout
          and "bin 1 -0.2500, bin 10 0.0000" in run.stdout)
    print(f"lock: {'ok' if ok else 'FAILED'} ({run.stdout.strip() or run.returncode})")
    failed += not ok
if failed:
    raise SystemExit(f"apu_mix: {failed} case(s) failed")
print("apu_mix: all cases pass")
