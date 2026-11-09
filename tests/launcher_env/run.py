"""Run the Linux launcher, and see what it hands the engine and what it does.

Builds launcher/linux/jsrf_launcher.c, and runs it in a package's layout
(bin/, lib/, VERSION) with a stand-in engine that writes down its arguments
and environment, answers --check-game, --check-iso and --unpack-iso as each
case asks, and exits as asked; and a stand-in game folder (default.xbe and
Media).

What it hands the engine: the paths and the player's settings
(RECOMP_GL_SCALE, RECOMP_FULLSCREEN) and no other RECOMP_ switch: what the
game needs to work is the engine's own default (docs/project-rules.md, Where
code goes 5). Up to b1.3 the launcher set twelve of them. Played with, as
given, with a scale and full screen, and in portable mode; with each end of
the scales, 1 to 6, given and saved, and one past them, which --scale
refuses and a saved one drops to the default 2.

What it does, as the shell script before it did (b2 row 4b): a default.xbe
given for its folder; a disc image, or a folder with one in it, checked and
unpacked once by the engine, and found unpacked the next time; a folder with
two refused; a disc image or a game the engine's check refuses named and
forgotten; one that can't be unpacked said so; the game remembered (in
portable mode, relative to the folder, and its saves copied in); the last
five logs kept; the engine's own refusal and a crash said so, with the exit
status; --install, refused in portable mode; --version; and no game and no
one to ask: nothing.

The Windows launcher can't run here (it is a windowed program, and CI builds
it), so the switches it names in launcher/windows/jsrf_launcher.c are
checked instead: only those two.

The run needs Linux, and CC (or gcc). LAUNCHER, when set, is a launcher
program to run instead of building one, to compare.
"""
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]

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

# The stand-in engine. Each answer comes from a file beside bin/, if there:
# check-game, check-iso, unpack (its exit code on the first line, what it
# says after), and play (the exit code, and lines for the log).
ENGINE = r"""#!/bin/sh
here="$(cd "$(dirname "$0")/.." && pwd)"
answer() {   # file: the exit code first, then what to print
    [ -f "$here/$1" ] || exit 0
    sed -n '2,$p' "$here/$1"
    exit "$(sed -n 1p "$here/$1")"
}
case "$1" in
--check-game) echo "$2" >> "$here/checked.txt"; answer check-game ;;
--check-iso) answer check-iso ;;
--version) echo "engine-version"; exit 0 ;;
--unpack-iso)
    echo "$2 -> $3" >> "$here/unpacked.txt"
    if [ -f "$here/unpack" ]; then answer unpack; fi
    mkdir -p "$3/Media" && : > "$3/default.xbe"
    echo "progress 1000000000 2000000000"
    echo "progress 2000000000 2000000000"
    echo ok
    exit 0 ;;
esac
printf '%s\n' "$@" > "$here/args.txt"
env > "$here/env.txt"
pwd > "$here/cwd.txt"
if [ -f "$here/play" ]; then sed -n '2,$p' "$here/play"; exit "$(sed -n 1p "$here/play")"; fi
"""


def build(out):
    """The launcher, as the package builds it."""
    cc = shlex.split(os.environ.get("CC", "gcc"))
    exe = out / "jsrf-pc"
    subprocess.run(cc + ["-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(root / "port"),
                         "-o", str(exe), str(root / "launcher/linux/jsrf_launcher.c"),
                         str(root / "port/xiso.c"), str(root / "port/xbe_check.c")], check=True)
    return exe


launcher_bin = None
compare = os.environ.get("LAUNCHER")
build_dir = tempfile.TemporaryDirectory(prefix="launcher-build-")
if not compare:
    launcher_bin = build(pathlib.Path(build_dir.name))


class Case:
    """A package, a home folder and a game, in a folder of their own."""

    def __init__(self, portable=False, saved=None):
        self.tmp = tempfile.TemporaryDirectory(prefix="launcher-env-")
        self.work = pathlib.Path(self.tmp.name)
        self.pkg = self.work / "JSRF-PC-linux-x86_64"
        (self.pkg / "bin").mkdir(parents=True)
        (self.pkg / "lib").mkdir()
        if compare:
            shutil.copy2(compare, self.pkg / "jsrf-pc")
        else:
            shutil.copy2(launcher_bin, self.pkg / "jsrf-pc")
        (self.pkg / "bin/jsrf_recomp").write_text(ENGINE)
        (self.pkg / "bin/jsrf_recomp").chmod(0o755)
        (self.pkg / "VERSION").write_text("test\n")
        if portable:
            (self.pkg / "portable").mkdir()
        self.game = self.make_game(self.work / "game")
        self.home = self.work / "home"
        self.home.mkdir()
        self.cfg = (self.pkg / "portable/settings.cfg") if portable else \
            self.home / ".config/jsrf-pc/settings"
        self.state = (self.pkg / "portable/logs") if portable else \
            self.home / ".local/state/jsrf-pc"
        self.logs = self.state if portable else self.state / "logs"
        if saved is not None:
            self.cfg.parent.mkdir(parents=True, exist_ok=True)
            self.cfg.write_text("scale=%s\n" % saved)

    @staticmethod
    def make_game(d):
        (d / "Media").mkdir(parents=True)
        (d / "default.xbe").write_bytes(b"")
        return d

    def answer(self, what, code, text=""):
        (self.pkg / what).write_text("%d\n%s" % (code, text))

    def run(self, args):
        for f in ("args.txt", "env.txt", "cwd.txt"):
            (self.pkg / f).unlink(missing_ok=True)
        # Nothing from this environment: no desktop to show a dialog on,
        # and no RECOMP_ switch already set.
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(self.home), "LANG": "C"}
        cmd = [str(self.pkg / "jsrf-pc")] + [str(a) for a in args]
        self.p = subprocess.run(cmd, env=env, stdin=subprocess.DEVNULL, capture_output=True,
                                text=True, timeout=60)
        return self.p

    def started(self):
        return (self.pkg / "env.txt").exists()

    def handed(self):
        env = dict(line.split("=", 1) for line in
                   (self.pkg / "env.txt").read_text().splitlines() if "=" in line)
        return env, (self.pkg / "args.txt").read_text().splitlines()

    def setting(self, key):
        if not self.cfg.exists():
            return None
        vals = [line.split("=", 1)[1] for line in self.cfg.read_text().splitlines()
                if line.startswith(key + "=")]
        return vals[-1] if vals else None


failed = 0
runs = 0


def result(name, bad, good):
    global failed, runs
    runs += 1
    if bad:
        failed += 1
        print("launcher_env %s: FAILED: %s" % (name, "; ".join(bad)))
    else:
        print("launcher_env %s: %s" % (name, good))


def check(name, args, scale, fullscreen, portable=False, saved=None):
    c = Case(portable, saved)
    p = c.run([c.game] + args)
    if p.returncode != 0 or not c.started():
        return result(name, ["the launcher didn't start the engine (exit %d): %s"
                             % (p.returncode, (p.stdout + p.stderr).strip())], "")
    env, argv = c.handed()
    game = str(c.game)
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
        bad.append("the game folder %r and arguments %r, not %r" % (env.get("JSRF_GAME_DIR"), argv, game))
    if not env.get("LD_LIBRARY_PATH", "").startswith(str(c.pkg / "lib")):
        bad.append("LD_LIBRARY_PATH %r doesn't start with the package's lib" % env.get("LD_LIBRARY_PATH"))
    result(name, bad, "the paths and %s, and no other switch"
           % ", ".join(k + "=" + v for k, v in sorted(want.items())))


def refused(name, args, says, case=None, prepare=None):
    """A run the launcher must stop, saying `says`, before the engine."""
    c = case or Case()
    if prepare:
        prepare(c)
    p = c.run([a if not callable(a) else a(c) for a in args])
    bad = []
    if p.returncode == 0:
        bad.append("exit 0")
    if c.started():
        bad.append("the engine started")
    # As the player reads it: the dialogs' lines are folded at 72.
    if " ".join(says.split()) not in " ".join(p.stderr.split()):
        bad.append("said %r, not %r" % (p.stderr.strip(), says))
    result(name, bad, "refused, %r" % says)
    return c


check("as given", [], 2, False)
check("with a scale and full screen", ["--scale", "3", "--fullscreen"], 3, True)
check("portable", [], 2, False, portable=True)
check("the largest scale", ["--scale", "6"], 6, False)
check("the smallest scale", ["--scale", "1"], 1, False)
refused("one scale too many", [lambda c: c.game, "--scale", "7"], "--scale takes 1 to 6.")
refused("no scale at all", [lambda c: c.game, "--scale", "0"], "--scale takes 1 to 6.")
refused("a scale with no number", [lambda c: c.game, "--scale"], "--scale takes 1 to 6.")
check("the largest scale, saved", [], 6, False, saved=6)
check("one scale too many, saved", [], 2, False, saved=7)

# ---- what it does besides

# A default.xbe given: its folder; remembered for the next start, with no
# argument; and where the engine runs from (the game folder).
c = Case()
c.run([c.game / "default.xbe"])
first = c.handed()[1] if c.started() else None
c.run([])
again = c.handed()[1] if c.started() else None
cwd = (c.pkg / "cwd.txt").read_text().strip() if c.started() else None
result("default.xbe, then remembered", [b for b in [
    first != [str(c.game)] and "given default.xbe: %r" % first,
    again != [str(c.game)] and "the next start: %r" % again,
    c.setting("game_dir") != str(c.game) and "game_dir=%r" % c.setting("game_dir"),
    cwd != str(c.game) and "ran in %r" % cwd] if b],
    "its folder, remembered, and run from it")

# A folder with one disc image: checked, unpacked into the data folder,
# and the next time found unpacked (not again).
c = Case()
isos = c.work / "isos"
isos.mkdir()
(isos / "JSRF (USA).iso").write_bytes(b"x")
p1 = c.run([isos])
unpacked = c.home / ".local/share/jsrf-pc/JSRF (USA)"
first = c.handed()[1] if c.started() else None
c.run([isos / "JSRF (USA).iso"])
second = c.handed()[1] if c.started() else None
times = len((c.pkg / "unpacked.txt").read_text().splitlines()) if (c.pkg / "unpacked.txt").exists() else 0
result("a folder with one disc image", [b for b in [
    first != [str(unpacked)] and "played %r, not %r (%s)" % (first, str(unpacked), p1.stderr.strip()),
    second != [str(unpacked)] and "the image itself, next: %r" % second,
    times != 1 and "unpacked %d times" % times,
    "Unpacking JSRF (USA).iso -- this happens once." not in p1.stderr and "said %r" % p1.stderr] if b],
    "unpacked once, into the data folder, and played from there")

# An .xiso: the name without both endings; portable: into portable/games.
c = Case(portable=True)
(c.work / "Game.xiso.iso").write_bytes(b"x")
c.run([c.work / "Game.xiso.iso"])
want = c.pkg / "portable/games/Game"
got = c.handed()[1] if c.started() else None
result("a disc image, portable", [b for b in [got != [str(want)] and "played %r, not %r" % (got, str(want))] if b],
       "unpacked into portable/games, named without .xiso.iso")

# Two disc images: which one?
def two(c):
    d = c.work / "two"
    d.mkdir()
    (d / "a.iso").write_bytes(b"x")
    (d / "b.ISO").write_bytes(b"x")
refused("a folder with two disc images", [lambda c: c.work / "two"],
        "That folder has 2 disc images in it: give the one of Jet Set Radio Future.", prepare=two)

# The engine's check of a disc image refuses it: named, and forgotten.
def bad_iso(c):
    (c.work / "other.iso").write_bytes(b"x")
    c.cfg.parent.mkdir(parents=True, exist_ok=True)
    c.cfg.write_text("game_dir=%s\n" % c.game)
    c.answer("check-iso", 3, "That is another game's disc.\n")
c = refused("a disc image the engine refuses", [lambda c: c.work / "other.iso"],
            "These files won't work with JSRF-PC.\n\nThat is another game's disc.", prepare=bad_iso)
result("  and forgotten", [b for b in [c.setting("game_dir") != "" and "game_dir=%r" % c.setting("game_dir")] if b],
       "game_dir emptied")

# One that can't be unpacked: said, and nothing left behind.
def broken(c):
    (c.work / "broken.iso").write_bytes(b"x")
    c.answer("unpack", 1, "The image ends early.\n")
c = refused("a disc image that can't be unpacked", [lambda c: c.work / "broken.iso"],
            "The disc image could not be unpacked.\n\nThe image ends early.", prepare=broken)
left = c.home / ".local/share/jsrf-pc/broken"
result("  and nothing left", [b for b in [left.exists() and "%s is there" % left] if b], "its folder removed")

# The engine's check of the game folder refuses it.
c = refused("a game the engine refuses", [lambda c: c.game],
            "These files won't work with JSRF-PC.\n\nAnother build of the game.",
            prepare=lambda c: c.answer("check-game", 3, "Another build of the game.\n"))

# Not the game's files, and nowhere.
refused("a folder without the game", [lambda c: c.work], "That folder has no default.xbe and Media in it")
refused("a folder that isn't there", [lambda c: c.work / "nowhere"], "That folder does not exist.")

# No game given, none remembered, and no one to ask (no desktop, no
# terminal): nothing happens.
c = Case()
p = c.run([])
result("nothing to go on", [b for b in [
    p.returncode != 0 and "exit %d" % p.returncode, c.started() and "the engine started"] if b],
    "exit 0, the engine not started")

# The engine stops itself (exit 3, its [XBE] lines in the log): said, and
# the game forgotten. A crash: said, with the log, and its exit status.
c = Case()
c.answer("play", 3, "[XBE] Not the standalone US disc.\n[XBE] Another line.\nother output\n")
p = c.run([c.game])
result("the engine's own refusal", [b for b in [
    p.returncode != 3 and "exit %d" % p.returncode,
    "These files won't work with JSRF-PC.\n\nNot the standalone US disc.\nAnother line." not in p.stderr
    and "said %r" % p.stderr,
    c.setting("game_dir") != "" and "game_dir=%r" % c.setting("game_dir")] if b],
    "said what the files are, forgot them, exit 3")
c = Case()
c.answer("play", 5, "something went wrong\n")
p = c.run([c.game])
log = c.state / "last-run.log"
result("a crash", [b for b in [
    p.returncode != 5 and "exit %d" % p.returncode,
    ("JSRF-PC stopped unexpectedly (exit 5).\n\nThe log is at:\n%s\n\nIf you report the problem, please attach it."
     % log) not in p.stderr and "said %r" % p.stderr,
    (not log.exists() or "something went wrong" not in log.read_text()) and "the log lacks the engine's output"] if b],
    "said so, with the log's path, exit 5")

# The logs: the last five runs kept beside the fresh one. (A second apart,
# as they are named by the second.)
c = Case()
for i in range(7):
    c.run([c.game])
    for j, f in enumerate(sorted(c.logs.glob("run-*.log"))):
        os.utime(f, (1000 + i * 10 + j, 1000 + i * 10 + j))
        f.rename(c.logs / ("run-old-%d-%d.log" % (i, j)))
kept = sorted(c.logs.glob("run-*.log"))
result("the logs", [b for b in [
    len(kept) != 5 and "%d kept: %s" % (len(kept), [k.name for k in kept]),
    not (c.state / "last-run.log").exists() and "no last-run.log"] if b],
    "the last five kept, and last-run.log")

# Portable: a game inside the folder remembered relative to it, and its
# saves copied in once, the originals left.
c = Case(portable=True)
inside = c.make_game(c.pkg / "games/JSRF")
(inside / "UDATA/4d530004").mkdir(parents=True)
(inside / "UDATA/4d530004/save.dat").write_bytes(b"progress")
c.run([inside])
copied = c.pkg / "portable/saves/UDATA/4d530004/save.dat"
(inside / "UDATA/4d530004/save.dat").write_bytes(b"newer")
c.run([])
again = c.handed()[1] if c.started() else None
result("portable, a game inside it", [b for b in [
    c.setting("game_dir") != "games/JSRF" and "game_dir=%r" % c.setting("game_dir"),
    again != [str(inside)] and "the next start played %r" % again,
    (not copied.exists() or copied.read_bytes() != b"progress") and "the saves not copied once",
    not (inside / "UDATA/4d530004/save.dat").exists() and "the original moved",
    ] if b], "remembered as games/JSRF, its saves copied once")

# --install: the menu entry, pointing here; not in portable mode.
c = Case()
p = c.run(["--install"])
desk = c.home / ".local/share/applications/jsrf-pc.desktop"
text = desk.read_text() if desk.exists() else ""
result("--install", [b for b in [
    p.returncode != 0 and "exit %d" % p.returncode,
    'Exec="%s/jsrf-pc"' % c.pkg not in text and "the entry: %r" % text,
    "Icon=%s/share/jsrf-pc.png" % c.pkg not in text and "no icon line",
    "Added JSRF-PC to your applications menu" not in p.stdout and "said %r" % p.stdout] if b],
    "the applications menu's entry, pointing at the package")
refused("--install, portable", ["--install"], "does not add itself to the", case=Case(portable=True))

c = Case()
p = c.run(["--version"])
result("--version", [b for b in [p.stdout != "test\n" and "said %r" % p.stdout] if b], "VERSION's line")

if failed:
    raise SystemExit("launcher_env: %d of %d runs failed" % (failed, runs))
print("launcher_env: %d runs passed" % runs)
