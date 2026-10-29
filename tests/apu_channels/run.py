"""Compile the channel log (Developer > Logging > RECOMP_APU_CHANNELS) and
drive it without the APU.

Takes src/apu/apu_channels.c as it is, and feeds it a made-up output, one
case to a run (test_main.c). With RECOMP_APU_CHANNELS=1, a side 20 dB or
more below the other for 0.2 s gets a line, and the voices and mixbins of the
tenth of a second after it, once in 10 s; a tenth on its own is only counted
in the summary; nothing playing is never one-sided. Unset, nothing is printed.
Requires GCC on PATH, including MinGW on Windows.
"""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]

SUMMARY = "[AUDIO-CH] the last 10 s: peaks left -6 dBFS, right -6 dBFS; "
# case: (lines the log must have, in order, and lines it must not)
cases = {
    "even": ([SUMMARY + "one-sided for 0.0 s, quiet spells: 0; the widest gap 0 dB"],
             ["went quiet", "voice 68"]),
    "right_gone": (["[AUDIO-CH] after 2.0 s of sound: the right side went quiet:"
                    " left -6 dBFS, right silent",
                    "[AUDIO-CH]   in the next 0.1 s, mixbins: 0:-6 dBFS",
                    "[AUDIO-CH]   voice 68, stereo: channel 0 0 dBFS (-1.000..1.000);"
                    " channel 1 0 dBFS (-1.000..1.000); into bin 0 (channel 0) -6.0 dB",
                    "[AUDIO-CH] after 3.7 s of sound: the right side is back after 1.5 s:"
                    " left -6 dBFS, right -6 dBFS",
                    SUMMARY + "one-sided for 1.5 s, quiet spells: 1; the widest gap 84 dB"
                    " (left -6 dBFS, right silent)"],
                   ["the left side"]),
    "left_low": (["the left side went quiet: left -28 dBFS, right -6 dBFS",
                  "mixbins: 0:-28 1:-6 dBFS",
                  "into bin 0 (channel 0) -28.0 dB, bin 1 (channel 1) -6.0 dB",
                  "the left side is back after 1.0 s",
                  SUMMARY + "one-sided for 1.0 s, quiet spells: 1; the widest gap 22 dB"],
                 ["the right side"]),
    "blip": ([SUMMARY + "one-sided for 0.1 s, quiet spells: 0"],
             ["went quiet", "voice 68"]),
    "twice": (["after 2.0 s of sound: the right side went quiet", "voice 68",
               "after 3.2 s of sound: the right side is back after 1.0 s",
               "after 5.0 s of sound: the right side went quiet",
               "after 6.2 s of sound: the right side is back after 1.0 s",
               SUMMARY + "one-sided for 2.0 s, quiet spells: 2"],
              []),
    "silence": (["[AUDIO-CH] the last 10 s: peaks left silent, right silent;"
                 " one-sided for 0.0 s, quiet spells: 0; nothing playing"],
                ["went quiet"]),
}

failed = 0
with tempfile.TemporaryDirectory(prefix="apu_channels-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Isrc/apu",
                    "src/apu/apu_channels.c", "tests/apu_channels/test_main.c",
                    "-o", exe, "-lm"], cwd=root, check=True)
    env = {k: v for k, v in os.environ.items() if k != "RECOMP_APU_CHANNELS"}
    for case, (must, must_not) in cases.items():
        run = subprocess.run([exe, case], env=dict(env, RECOMP_APU_CHANNELS="1"),
                             capture_output=True, text=True, timeout=60)
        log, at = run.stdout + run.stderr, 0
        problems = [] if run.returncode == 0 else [f"exit {run.returncode}"]
        for line in must:
            found = log.find(line, at)
            if found < 0:
                problems.append(f"missing (or out of order): {line}")
            else:
                at = found + len(line)
        problems += [f"unexpected: {line}" for line in must_not if line in log]
        print(f"{case}: {'ok' if not problems else 'FAILED'}")
        for problem in problems:
            print(f"  {problem}")
        failed += bool(problems)
    run = subprocess.run([exe, "right_gone"], env=env, capture_output=True,
                         text=True, timeout=60)
    off = run.returncode == 0 and not (run.stdout + run.stderr)
    print(f"off: {'ok' if off else 'FAILED'}")
    failed += not off
if failed:
    raise SystemExit(f"apu_channels: {failed} case(s) failed")
print("apu_channels: all cases pass")
