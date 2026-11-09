"""Run the Linux launcher, and see what it hands the engine.

Takes launcher/linux/jsrf-pc as it is, in a package's layout (with bin/
and VERSION beside it), with a stand-in engine that
writes down its arguments and environment, and a stand-in game folder
(default.xbe and Media). The engine must be handed the paths and the
player's settings (RECOMP_GL_SCALE, RECOMP_FULLSCREEN) and no other
RECOMP_ switch: what the game needs to work is the engine's own default
(docs/project-rules.md, Where code goes 5). Up to b1.3 the launcher set
twelve of them. Played with, as given, with a scale and full screen, and
in portable mode; with each end of the scales, 1 to 6, given and saved,
and one past them, which --scale refuses and a saved one drops to the
default 2. The Windows launcher can't run here (it is a windowed
program, and CI builds it), so the switches it names in
launcher/windows/jsrf_launcher.c are checked instead: only those two.

The run needs Linux, with bash.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
launcher = root / "launcher/linux/jsrf-pc"

# The player's settings: the only switches a launcher may pass.
SETTINGS = {"RECOMP_GL_SCALE", "RECOMP_FULLSCREEN"}

# Windows: every switch the launcher's source names.
windows = sorted(set(re.findall(r'L"(RECOMP_[A-Z0-9_]+)',
                 (root / "launcher/windows/jsrf_launcher.c").read_text(encoding="utf-8"))))
if set(windows) != SETTINGS:
    raise SystemExit("launcher_env windows: jsrf_launcher.c names %s, not only %s"
                     % (", ".join(windows), ", ".join(sorted(SETTINGS))))
print("launcher_env windows: jsrf_launcher.c names only " + ", ".join(windows))

if not sys.platform.startswith("linux"):
    print("launcher_env: the runs skipped, Linux only")
    sys.exit(0)

# The stand-in engine: the launcher's checks of the game pass, and the run
# writes down what it was given, beside bin/.
ENGINE = """#!/bin/sh
case "$1" in --check-game|--check-iso) exit 0 ;; esac
here="$(dirname "$0")/.."
printf '%s\\n' "$@" > "$here/args.txt"
env > "$here/env.txt"
"""


def package(work, portable):
    pkg = work / "JSRF-PC-linux-x86_64"
    (pkg / "bin").mkdir(parents=True)
    shutil.copy2(launcher, pkg / "jsrf-pc")
    (pkg / "bin/jsrf_recomp").write_text(ENGINE)
    (pkg / "bin/jsrf_recomp").chmod(0o755)
    (pkg / "VERSION").write_text("test\n")
    if portable:
        (pkg / "portable").mkdir()
    return pkg


def launch(work, args, portable, saved):
    """The launcher run in a package in `work`, with the scale `saved` in its
    settings, if any: the run, the package and the game folder."""
    pkg = package(work, portable)
    game = work / "game"
    (game / "Media").mkdir(parents=True)
    (game / "default.xbe").write_bytes(b"")
    (work / "home").mkdir()
    if saved is not None:
        cfg = work / "home/.config/jsrf-pc/settings"
        cfg.parent.mkdir(parents=True)
        cfg.write_text("scale=%s\n" % saved)
    # Nothing from this environment: no desktop to show a dialog on,
    # and no RECOMP_ switch already set.
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(work / "home"),
           "LANG": "C"}
    p = subprocess.run(["bash", str(pkg / "jsrf-pc"), str(game)] + args, env=env,
                       stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
    return p, pkg, game


def run(name, args, portable=False, saved=None):
    """What the engine was handed: {variable: value} and its arguments."""
    with tempfile.TemporaryDirectory(prefix="launcher-env-") as directory:
        p, pkg, game = launch(pathlib.Path(directory), args, portable, saved)
        if p.returncode != 0 or not (pkg / "env.txt").exists():
            raise SystemExit("launcher_env %s: the launcher didn't start the engine (exit %d)\n%s"
                             % (name, p.returncode, p.stdout + p.stderr))
        handed = dict(line.split("=", 1) for line in
                      (pkg / "env.txt").read_text().splitlines() if "=" in line)
        return handed, (pkg / "args.txt").read_text().splitlines(), str(game)


failed = 0
runs = 0


def check(name, args, scale, fullscreen, portable=False, saved=None):
    global failed, runs
    runs += 1
    env, argv, game = run(name, args, portable, saved)
    switches = sorted(k for k in env if k.startswith("RECOMP_"))
    extra = [k + "=" + env[k] for k in switches if k not in SETTINGS]
    want = {"RECOMP_GL_SCALE": str(scale)}
    if fullscreen:
        want["RECOMP_FULLSCREEN"] = "1"
    got = {k: env[k] for k in switches if k in SETTINGS}
    paths = ["JSRF_GAME_DIR", "JSRF_SAVE_DIR", "JSRF_SETTINGS_FILE"] + (
        ["JSRF_USER_DIR"] if portable else [])
    missing = [k for k in paths if not env.get(k)]
    bad = []
    if extra:
        bad.append("switches the engine should default: " + ", ".join(extra))
    if got != want:
        bad.append("settings %s, not %s" % (got, want))
    if missing:
        bad.append("no " + ", ".join(missing))
    if env.get("JSRF_GAME_DIR") != game or argv != [game]:
        bad.append("the game folder %r and arguments %r, not %r" % (
            env.get("JSRF_GAME_DIR"), argv, game))
    if bad:
        failed += 1
        print("launcher_env %s: %s" % (name, "; ".join(bad)))
    else:
        print("launcher_env %s: the paths and %s, and no other switch" % (
            name, ", ".join(k + "=" + v for k, v in sorted(want.items()))))


def refused(name, args, says):
    """A run the launcher must stop, saying `says`, before the engine."""
    global failed, runs
    runs += 1
    with tempfile.TemporaryDirectory(prefix="launcher-env-") as directory:
        p, pkg, _ = launch(pathlib.Path(directory), args, False, None)
        if p.returncode == 0 or (pkg / "env.txt").exists() or says not in p.stderr:
            failed += 1
            print("launcher_env %s: exit %d, the engine %s, and %r, not %r" % (
                name, p.returncode, "started" if (pkg / "env.txt").exists() else "not started",
                p.stderr.strip(), says))
        else:
            print("launcher_env %s: refused, %r" % (name, says))


check("as given", [], 2, False)
check("with a scale and full screen", ["--scale", "3", "--fullscreen"], 3, True)
check("portable", [], 2, False, portable=True)
check("the largest scale", ["--scale", "6"], 6, False)
check("the smallest scale", ["--scale", "1"], 1, False)
refused("one scale too many", ["--scale", "7"], "--scale takes 1 to 6.")
refused("no scale at all", ["--scale", "0"], "--scale takes 1 to 6.")
check("the largest scale, saved", [], 6, False, saved=6)
check("one scale too many, saved", [], 2, False, saved=7)
if failed:
    raise SystemExit("launcher_env: %d of %d runs handed the engine more than it should, "
                     "or a scale it shouldn't" % (failed, runs))
