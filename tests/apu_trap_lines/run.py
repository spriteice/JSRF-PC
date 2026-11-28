"""The voice processor's trap, stop wait and watch lines, for b3's baseline.

Compiles src/apu/apu_vp.c with what it links, as tests/apu_vp_checks does,
with the two lines' ten-second report made every frame's
(-DAPU_G1_REPORT_MS=0), and drives it as the game does (test_main.c says
the steps): line B started, its LBO set by a method and its CBO written in
memory, the frame that passes sample 11647, a look at its position and its
VOICE_OFF; a trap with a method run in it before the handler reads
FEDECPARAM; voices out of their lists after one report and after two; and a
voice turned off and started again before a frame finds it out. With
RECOMP_APU_STATS and RECOMP_APU_WATCH set, each step's lines must be the
ones it makes; with neither, there must be none.
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


def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


ZERO = f"peak 0, rms 0 decoded, hash {fnv(bytes(396)):08X}, all zero"
FRONT = ("[APU] the front end, the last 10 s: {} traps for an idle voice, {} of the title's methods "
         "run while one was set; FEDECPARAM read {} times while trapped, {} of them not the voice "
         "it trapped for; {} idle voices out of their lists, after 1 report {}, 2 {}, 3 or more {} "
         "(the most {}), none 0; {} started again first")
OFFS = (r"\[APU\] the title's VOICE_OFFs, the last 10 s: {} out of their lists, the longest "
        r"[\d.]+ ms from VOICE_OFF to out of its list, [\d.]+ ms on average \(what DirectSound's "
        r"stop wait spins on\); {} started again before a frame found them out")

# Each step's lines, as text (exact) or patterns (re.fullmatch), in order.
want = {
    "1 line B on": [
        f"[APUWATCH] t=0.000s line B (voice 5, ba 0x010000) on: the 396 bytes after its line "
        f"(from ba + 6552): {ZERO}"],
    "2 LBO by a method": [
        "[APUWATCH] t=0.000s voice 5's LBO 0 -> 100, by a method, 1 frames after VOICE_ON"],
    "2 CBO in memory": [
        "[APUWATCH] t=0.000s voice 5's CBO 128 -> 11640, in memory, by no method, 2 frames after "
        "VOICE_ON",
        f"[APUWATCH] t=0.000s line B (voice 5, ba 0x010000) passing its end: the 396 bytes after "
        f"its line (from ba + 6552): {ZERO}"],
    "3 a look, then off": [
        re.compile(r"\[APUWATCH\] t=0\.000s line B \(voice 5\) off 1 frames \([\d.]+ ms\) after it "
                   r"passed sample 11647; the game's first look at it after that \(a VOICE_LOCK "
                   r"with nothing changed\) 0 frames \([\d.]+ ms\) after, at sample 11768")],
    "4 trapped": [FRONT.format(1, 0, 0, 0, 0, 0, 0, 0, 0, 0)],
    "4 out": [FRONT.format(0, 1, 1, 1, 1, 1, 0, 0, 1, 0), re.compile(OFFS.format(1, 0))],
    "5 out": [FRONT.format(0, 0, 0, 0, 1, 0, 1, 0, 2, 0), re.compile(OFFS.format(1, 0))],
    "6 on again": [re.compile(OFFS.format(0, 1))],
}


def sections(text):
    out, name = {}, None
    for line in text.splitlines():
        if line.startswith("== "):
            name = line[3:]
            out[name] = []
        elif name and (line.startswith("[APUWATCH]") or line.startswith("[APU] the ")):
            out[name].append(line)
    return out


def found(lines, pattern):
    for line in lines:
        if line == pattern if isinstance(pattern, str) else pattern.fullmatch(line):
            return True
    return False


ok = True
with tempfile.TemporaryDirectory(prefix="apu_trap_lines-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    command = cc + ["-std=gnu11", "-O2", "-DAPU_G1_REPORT_MS=0", "-Isrc/apu", "-Isrc",
                    "src/apu/apu_vp.c", "src/apu/apu_dsp.c",
                    "src/apu/apu_channels.c", "src/apu/apu_gpfx.c",
                    "tests/apu_trap_lines/test_main.c", "-o", exe]
    if sys.platform != "win32":
        command += ["src/platform/win32_compat.c", "-lm", "-lpthread"]
    subprocess.run(command, cwd=root, check=True)
    base = {k: v for k, v in os.environ.items()
            if not k.startswith("RECOMP_APU_") and k != "RECOMP_MUTE"}
    for name, env in (("on", dict(base, RECOMP_APU_STATS="1", RECOMP_APU_WATCH="1")),
                      ("off", base)):
        run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=60)
        if run.returncode:
            sys.exit(f"apu_trap_lines {name}: exit {run.returncode}\n{run.stdout}{run.stderr}")
        got = sections(run.stderr)
        if name == "off":
            extra = [l for lines in got.values() for l in lines]
            good = not extra
            print(f"{'ok  ' if good else 'FAIL'}  switches off: {len(extra)} lines")
            ok = ok and good
            continue
        for step, patterns in want.items():
            lines = got.get(step, [])
            missing = [p if isinstance(p, str) else p.pattern for p in patterns if not found(lines, p)]
            print(f"{'ok  ' if not missing else 'FAIL'}  {step}"
                  + "".join(f"\n      wanted {m!r}" for m in missing)
                  + ("".join(f"\n      got    {l!r}" for l in lines) if missing else ""))
            ok = ok and not missing
        changes = [l for lines in got.values() for l in lines if "'s " in l and " -> " in l]
        good = len(changes) == 2
        print(f"{'ok  ' if good else 'FAIL'}  two change lines in all" + ("" if good else f", not {len(changes)}"))
        ok = ok and good
if not ok:
    sys.exit(1)
print("apu_trap_lines: all cases pass")
