"""Run the Linux launcher, and see what it hands the engine and what it does.

Builds launcher/linux/jsrf_launcher.c (with the setup's core, the renderer's
resolution choices, and a stand-in for its window, which a package with no
kit/ folder never opens), and runs it
in a package's layout (bin/, lib/, VERSION) with a stand-in engine that
writes down its arguments and environment, answers --check-game,
--check-iso and --unpack-iso as each case asks, and exits as asked; and a
stand-in game folder (default.xbe and Media).

What it hands the engine: the paths and the player's settings
(RECOMP_FULLSCREEN, RECOMP_MINIMIZED) and no other RECOMP_ switch: what the
game needs to work is the engine's own default (docs/project-rules.md,
Where code goes 5). Up to b1.3 the launcher set twelve of them. Played
with, as given, with a resolution and full screen, minimized (--minimized),
alone and with full screen, and with a portable folder from before. Every
time, the paths are the data folder's beside the launcher (settings.cfg,
saves/ as JSRF_USER_DIR, cache/), and nothing is written in the home folder.

The resolution isn't handed over: the engine reads resolution= from the
settings file (src/nv2a_gl/nv2a_scale.h). --resolution saves each of its
names there, and --scale N from before saves the resolution it maps to (1
to 6: Default, 1080p, 1440p, 2160p, 2160p, 2160p), over one saved already;
a name or a scale that isn't one, or none, is refused, and nothing saved.
One saved is left as it is, and a scale= from before too, for the engine
to map.

What it hands the engine for --play: the recording's folder, by its whole
path, as JSRF_PLAY, and none without it.

What it does, as the shell script before it did (b2 row 4b): a default.xbe
given for its folder; a disc image, or a folder with one in it, checked and
unpacked once by the engine into data/games, and found unpacked the next
time; a folder with two refused; a disc image or a game the engine's check
refuses named and forgotten; one that can't be unpacked said so; the game
remembered (one inside the launcher's folder relative to it), and its saves
copied into data/saves once; the last five logs kept; the engine's own
refusal and a crash said so, with the exit status; --install; --version; and
no game and no one to ask: nothing.

Where things are kept (launcher/common/user_files.h): a portable folder from
before renamed to data, with a game inside it found again there; both
folders refused, with nothing changed; what b2.2 kept in the home folder (or
$XDG_CONFIG_HOME) carried over into a new data folder once, its game still
played from where it was unpacked, and nothing there changed; and, not as
root, a portable folder that can't be renamed and a data folder that can't
be written refused.

The Windows launcher can't run here (it is a windowed program, and CI builds
it), so the switches it names in launcher/windows/jsrf_launcher.c are
checked instead: only those two.

Which package it is: one with a kit/ folder (the package without the
engine) opens the setup when the engine is missing or isn't the kit's, and
plays when it is; one without (all the runs above) never opens it.

The run needs Linux, and CC (or gcc). LAUNCHER, when set, is a launcher
program to run instead of building one, to compare.
"""
import hashlib
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
SETTINGS = {"RECOMP_FULLSCREEN", "RECOMP_MINIMIZED"}

# resolution='s names, Display > Resolution's order (nv2a_scale_key), and
# what --scale's 1 to 6 save (nv2a_scale_of_old).
RESOLUTIONS = ["default", "720", "900", "1080", "1440", "1600", "2160", "match"]
OLD_SCALES = {1: "default", 2: "1080", 3: "1440", 4: "2160", 5: "2160", 6: "2160"}

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

STUB_UI = """#include <stdio.h>
#include "setup.h"
/* The window: here it only says it was opened, and that the player closed it. */
int jsrf_setup_run(jsrf_setup *s)
{
    fprintf(stderr, "stub: the setup, in %s\\n", s->root);
    return JSRF_SETUP_CLOSED;
}
"""


def build(out):
    """The launcher, as the packages build it but for the window."""
    cc = shlex.split(os.environ.get("CC", "gcc"))
    stub = out / "stub_ui.c"
    stub.write_text(STUB_UI)
    exe = out / "jsrf-pc"
    srcs = [root / "launcher/linux/jsrf_launcher.c", root / "launcher/common/user_files.c",
            root / "launcher/setup/setup_core.c",
            root / "port/xiso.c", root / "port/xbe_check.c", root / "src/nv2a_gl/nv2a_scale.c", stub]
    objs = []
    for s in srcs:
        o = out / (s.name + ".o")
        subprocess.run(cc + ["-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
                             "-I", str(root / "port"), "-I", str(root / "launcher/setup"),
                             "-c", str(s), "-o", str(o)], check=True)
        objs.append(o)
    o = out / "miniz.c.o"
    m = subprocess.run(cc + ["-O1", "-w", "-c", str(root / "third_party/miniz/miniz.c"), "-o", str(o)],
                       capture_output=True, text=True)   # its #pragma message, every time
    if m.returncode:
        raise SystemExit("launcher_env: miniz.c didn't compile\n" + m.stdout + m.stderr)
    subprocess.run(cc + [str(x) for x in objs + [o]] + ["-o", str(exe), "-ldl", "-lm"], check=True)
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
        self.data = self.pkg / "data"
        self.cfg = self.data / "settings.cfg"
        self.state = self.logs = self.data / "logs"
        if saved is not None:
            to = (self.pkg / "portable/settings.cfg") if portable else self.cfg
            to.parent.mkdir(parents=True, exist_ok=True)
            to.write_text(saved)

    @staticmethod
    def make_game(d):
        (d / "Media").mkdir(parents=True)
        (d / "default.xbe").write_bytes(b"")
        return d

    def answer(self, what, code, text=""):
        (self.pkg / what).write_text("%d\n%s" % (code, text))

    def run(self, args, outside=None):
        for f in ("args.txt", "env.txt", "cwd.txt"):
            (self.pkg / f).unlink(missing_ok=True)
        # Nothing from this environment: no desktop to show a dialog on,
        # and no RECOMP_ switch already set, but for a case's `outside`.
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(self.home), "LANG": "C"}
        env.update(outside or {})
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


def tree(top):
    """Every file and folder under top, with each file's bytes: what a run changed."""
    top = pathlib.Path(top)
    return sorted((str(p.relative_to(top)), p.read_bytes() if p.is_file() else None) for p in top.rglob("*"))


def result(name, bad, good):
    global failed, runs
    runs += 1
    if bad:
        failed += 1
        print("launcher_env %s: FAILED: %s" % (name, "; ".join(bad)))
    else:
        print("launcher_env %s: %s" % (name, good))


def check(name, args, resolution, fullscreen, portable=False, saved=None, outside=None, minimized=False):
    """resolution: the resolution= the settings file holds after, None for
    none. saved: the settings file's text before."""
    c = Case(portable, saved)
    p = c.run([c.game] + args, outside)
    if p.returncode != 0 or not c.started():
        return result(name, ["the launcher didn't start the engine (exit %d): %s"
                             % (p.returncode, (p.stdout + p.stderr).strip())], "")
    env, argv = c.handed()
    game = str(c.game)
    switches = sorted(k for k in env if k.startswith("RECOMP_"))
    extra = [k + "=" + env[k] for k in switches if k not in SETTINGS]
    want = {}
    if fullscreen:
        want["RECOMP_FULLSCREEN"] = "1"
    if minimized:
        want["RECOMP_MINIMIZED"] = "1"
    got = {k: env[k] for k in switches if k in SETTINGS}
    want_paths = {"JSRF_SAVE_DIR": str(c.data / "cache"), "JSRF_SETTINGS_FILE": str(c.cfg),
                  "JSRF_USER_DIR": str(c.data / "saves")}
    paths = ["%s=%r" % (k, env.get(k)) for k, v in sorted(want_paths.items()) if env.get(k) != v]
    bad = []
    if paths:
        bad.append("not the data folder's: " + ", ".join(paths))
    if portable and (c.pkg / "portable").exists():
        bad.append("the portable folder not renamed")
    if tree(c.home):
        bad.append("written in the home folder: %s" % [n for n, _ in tree(c.home)])
    if extra:
        bad.append("switches the engine should default: " + ", ".join(extra))
    if got != want:
        bad.append("settings %s, not %s" % (got, want))
    if c.setting("resolution") != resolution:
        bad.append("resolution=%r saved, not %r" % (c.setting("resolution"), resolution))
    if c.cfg.exists() and sum(line.startswith("resolution=") for line in c.cfg.read_text().splitlines()) > 1:
        bad.append("resolution= saved twice")
    if env.get("JSRF_GAME_DIR") != game or argv != [game]:
        bad.append("the game folder %r and arguments %r, not %r" % (env.get("JSRF_GAME_DIR"), argv, game))
    if not env.get("LD_LIBRARY_PATH", "").startswith(str(c.pkg / "lib")):
        bad.append("LD_LIBRARY_PATH %r doesn't start with the package's lib" % env.get("LD_LIBRARY_PATH"))
    result(name, bad, "the data folder's paths%s, and no other switch; %s"
           % ("".join(" and " + k + "=" + v for k, v in sorted(want.items())),
              "resolution=%s saved" % resolution if resolution else "no resolution= (the engine's own)"))


def refused(name, args, says, case=None, prepare=None, unsaved=None):
    """A run the launcher must stop, saying `says`, before the engine; and
    with `unsaved`, a key it saves nothing to."""
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
    if unsaved and c.setting(unsaved) is not None:
        bad.append("%s=%r saved" % (unsaved, c.setting(unsaved)))
    result(name, bad, "refused, %r" % says)
    return c


check("as given", [], None, False)
check("with a resolution and full screen", ["--resolution", "1080", "--fullscreen"], "1080", True)
check("minimized", ["--minimized"], None, False, minimized=True)
check("minimized, and full screen", ["--fullscreen", "--minimized"], None, True, minimized=True)
check("a portable folder from before", [], None, False, portable=True)
check("a portable folder from before, with a resolution", ["--resolution", "720"], "720", False, portable=True)
for key in RESOLUTIONS:
    check("--resolution " + key, ["--resolution", key], key, False)
for n, key in sorted(OLD_SCALES.items()):
    check("--scale %d" % n, ["--scale", str(n)], key, False)
check("--scale over a resolution saved", ["--scale", "2"], "1080", False, saved="resolution=match\n")
check("a resolution saved", [], "720", False, saved="resolution=720\n")
RES_TAKES = "--resolution takes default, 720, 900, 1080, 1440, 1600, 2160 or match."
refused("a resolution that isn't one", [lambda c: c.game, "--resolution", "4k"], RES_TAKES, unsaved="resolution")
refused("a scale given as a resolution", [lambda c: c.game, "--resolution", "3"], RES_TAKES, unsaved="resolution")
refused("a resolution with no name", [lambda c: c.game, "--resolution"], RES_TAKES, unsaved="resolution")
refused("one scale too many", [lambda c: c.game, "--scale", "7"], "--scale takes 1 to 6.", unsaved="resolution")
refused("no scale at all", [lambda c: c.game, "--scale", "0"], "--scale takes 1 to 6.", unsaved="resolution")
refused("a scale with no number", [lambda c: c.game, "--scale"], "--scale takes 1 to 6.", unsaved="resolution")
refused("a resolution given as a scale", [lambda c: c.game, "--scale", "1080"], "--scale takes 1 to 6.",
        unsaved="resolution")

# A scale= from before, saved: left for the engine to map (nv2a_scale_of_old).
c = Case(saved="scale=6\n")
c.run([c.game])
result("a scale from before, saved", [b for b in [
    not c.started() and "the engine didn't start",
    c.started() and "RECOMP_GL_SCALE" in c.handed()[0] and "RECOMP_GL_SCALE handed over",
    c.setting("scale") != "6" and "scale=%r" % c.setting("scale"),
    c.setting("resolution") is not None and "resolution=%r saved" % c.setting("resolution")] if b],
    "left as it is, for the engine, and nothing handed over")

# ---- what it does besides

# A default.xbe given: its folder; remembered for the next start, with no
# argument; and where the engine runs from (the logs, where what it writes
# by a relative name goes).
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
    cwd != str(c.logs) and "ran in %r" % cwd] if b],
    "its folder, remembered, and run from the logs")

# A folder with one disc image: checked, unpacked into the data folder,
# and the next time found unpacked (not again).
c = Case()
isos = c.work / "isos"
isos.mkdir()
(isos / "JSRF (USA).iso").write_bytes(b"x")
p1 = c.run([isos])
unpacked = c.data / "games/JSRF (USA)"
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

# An .xiso: the name without both endings.
c = Case()
(c.work / "Game.xiso.iso").write_bytes(b"x")
c.run([c.work / "Game.xiso.iso"])
want = c.data / "games/Game"
got = c.handed()[1] if c.started() else None
result("an .xiso", [b for b in [got != [str(want)] and "played %r, not %r" % (got, str(want))] if b],
       "unpacked into data/games, named without .xiso.iso")

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
left = c.data / "games/broken"
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

# A game inside the launcher's folder remembered relative to it, and its
# saves copied in once, the originals left.
c = Case()
inside = c.make_game(c.pkg / "games/JSRF")
(inside / "UDATA/4d530004").mkdir(parents=True)
(inside / "UDATA/4d530004/save.dat").write_bytes(b"progress")
c.run([inside])
copied = c.data / "saves/UDATA/4d530004/save.dat"
(inside / "UDATA/4d530004/save.dat").write_bytes(b"newer")
c.run([])
again = c.handed()[1] if c.started() else None
result("a game inside the folder", [b for b in [
    c.setting("game_dir") != "games/JSRF" and "game_dir=%r" % c.setting("game_dir"),
    again != [str(inside)] and "the next start played %r" % again,
    (not copied.exists() or copied.read_bytes() != b"progress") and "the saves not copied once",
    not (inside / "UDATA/4d530004/save.dat").exists() and "the original moved",
    ] if b], "remembered as games/JSRF, its saves copied once")

# --install: the menu entry, pointing here, beside the data folder (b2.2
# refused it in portable mode).
c = Case(portable=True)
p = c.run(["--install"])
desk = c.home / ".local/share/applications/jsrf-pc.desktop"
text = desk.read_text() if desk.exists() else ""
result("--install", [b for b in [
    p.returncode != 0 and "exit %d" % p.returncode,
    'Exec="%s/jsrf-pc"' % c.pkg not in text and "the entry: %r" % text,
    "Icon=%s/share/jsrf-pc.png" % c.pkg not in text and "no icon line",
    "Added JSRF-PC to your applications menu" not in p.stdout and "said %r" % p.stdout,
    not c.data.is_dir() and "no data folder"] if b],
    "the applications menu's entry, pointing at the package")

c = Case()
p = c.run(["--version"])
result("--version", [b for b in [p.stdout != "test\n" and "said %r" % p.stdout] if b], "VERSION's line")

# --play: a recording's folder (Developer > Playback), handed over as
# JSRF_PLAY by its whole path, since the engine runs in another folder; none
# without it, even one in the launcher's own environment; a folder that
# isn't there refused.
c = Case()
(c.home / "run").mkdir()
c.run([c.game, "--play", c.home / "run/../run"])
played = c.handed()[0].get("JSRF_PLAY") if c.started() else None
c.run([c.game], outside={"JSRF_PLAY": str(c.home / "run")})
again = c.handed()[0].get("JSRF_PLAY", "") if c.started() else None
result("--play", [b for b in [
    played != str(c.home / "run") and "handed JSRF_PLAY=%r" % played,
    again != "" and "without it, JSRF_PLAY=%r" % again] if b],
    "the recording's whole path, and none without it")
refused("--play, no such folder", [lambda c: c.game, "--play", lambda c: c.home / "none"],
        "--play takes a recording's folder.")
refused("--play, no folder", [lambda c: c.game, "--play"], "--play takes a recording's folder.")

# ---- where things are kept (launcher/common/user_files.h)

# A portable folder from before, renamed to data: a game unpacked into it
# found again there (game_dir=portable/games/Game becomes data/games/Game),
# with the settings and the saves it held.
c = Case(portable=True, saved="resolution=720\ngame_dir=portable/games/Game\n")
was = c.make_game(c.pkg / "portable/games/Game")
(c.pkg / "portable/saves/UDATA/4d530004").mkdir(parents=True)
(c.pkg / "portable/saves/UDATA/4d530004/save.dat").write_bytes(b"progress")
p = c.run([])
played = c.handed()[1] if c.started() else None
result("a portable folder renamed", [b for b in [
    p.returncode != 0 and "exit %d: %s" % (p.returncode, (p.stdout + p.stderr).strip()),
    (c.pkg / "portable").exists() and "the portable folder still there",
    played != [str(c.data / "games/Game")] and "played %r" % played,
    c.setting("game_dir") != "data/games/Game" and "game_dir=%r" % c.setting("game_dir"),
    c.setting("resolution") != "720" and "resolution=%r" % c.setting("resolution"),
    not (c.data / "saves/UDATA/4d530004/save.dat").exists() and "its saves not in data/saves",
    ] if b], "data now, game_dir=data/games/Game, its settings and saves kept")

# Both folders: refused, naming both, with nothing changed.
c = Case(portable=True, saved="resolution=720\n")
c.cfg.parent.mkdir(parents=True)
c.cfg.write_text("resolution=1080\n")
before = tree(c.pkg)
p = c.run([c.game])
result("both folders", [b for b in [
    p.returncode == 0 and "exit 0",
    c.started() and "the engine started",
    "can't tell which one has your progress" not in " ".join(p.stderr.split()) and "said %r" % p.stderr.strip(),
    (str(c.data) not in p.stderr or str(c.pkg / "portable") not in p.stderr) and "both not named",
    tree(c.pkg) != before and "something changed"] if b], "refused, both named, nothing changed")

# What b2.2 kept in the home folder, carried over into a new data folder
# once: the settings (its game, unpacked there, still played from there),
# developer.cfg, remap.cfg and recordings; not the cache or the logs; the
# game's saves copied in from its folder; nothing in the home folder changed.
# At the next start, nothing carried over again.
for name, outside, config in [
        ("the home folder carried over", None, ".config/jsrf-pc"),
        ("$XDG_CONFIG_HOME carried over", {"XDG_CONFIG_HOME": "xdg"}, "xdg/jsrf-pc")]:
    c = Case()
    if outside:
        outside = {"XDG_CONFIG_HOME": str(c.home / "xdg")}
    conf = c.home / config
    unpacked = c.make_game(c.home / ".local/share/jsrf-pc/JSRF (USA)")
    (unpacked / "UDATA/4d530004").mkdir(parents=True)
    (unpacked / "UDATA/4d530004/save.dat").write_bytes(b"progress")
    (c.home / ".local/share/jsrf-pc/save").mkdir()
    (c.home / ".local/share/jsrf-pc/save/cache.bin").write_bytes(b"cache")
    (c.home / ".local/state/jsrf-pc/logs").mkdir(parents=True)
    (c.home / ".local/state/jsrf-pc/last-run.log").write_text("log\n")
    (conf / "recordings/run-1").mkdir(parents=True)
    (conf / "recordings/run-1/input.bin").write_bytes(b"\x01")
    (conf / "settings").write_text("resolution=1080\ngame_dir=%s\n" % unpacked)
    (conf / "developer.cfg").write_text("free_camera=1\n")
    (conf / "remap.cfg").write_text("a=b\n")
    before = tree(c.home)
    p = c.run([], outside)
    played = c.handed()[1] if c.started() else None
    kept = sorted(n for n, _ in tree(c.data) if not n.startswith(("logs", "saves", "cache", "games")))
    first = (c.data / "remap.cfg").read_bytes() if (c.data / "remap.cfg").exists() else None
    (conf / "remap.cfg").write_text("c=d\n")
    (conf / "settings").write_text("resolution=720\n")
    p2 = c.run([], outside)
    result(name, [b for b in [
        p.returncode != 0 and "exit %d: %s" % (p.returncode, (p.stdout + p.stderr).strip()),
        played != [str(unpacked)] and "played %r" % played,
        c.setting("resolution") != "1080" and "resolution=%r" % c.setting("resolution"),
        kept != ["developer.cfg", "recordings", "recordings/run-1", "recordings/run-1/input.bin", "remap.cfg",
                 "settings.cfg"] and "the data folder holds %s" % kept,
        first != b"a=b\n" and "remap.cfg %r" % first,
        not (c.data / "saves/UDATA/4d530004/save.dat").exists() and "the game's saves not copied in",
        (c.pkg / "data.new").exists() and "data.new left",
        sorted(x for x in tree(c.home) if x[0] not in (config + "/remap.cfg", config + "/settings")) !=
        sorted(x for x in before if x[0] not in (config + "/remap.cfg", config + "/settings"))
        and "the home folder changed",
        p2.returncode != 0 and "the next start: exit %d" % p2.returncode,
        (c.data / "remap.cfg").read_bytes() != b"a=b\n" and "carried over again",
        ] if b], "its settings, developer.cfg, remap.cfg and recordings, once; the game played from where it was")

# Folders that can't be written: refused, with nothing changed (not as root,
# whom nothing refuses).
if os.geteuid() == 0:
    print("launcher_env folders that can't be written: not tried, as root")
else:
    for name, portable, lock, says in [
            ("a portable folder that can't be renamed", True, lambda c: c.pkg, "couldn't be renamed to data"),
            ("a data folder that can't be written", False, lambda c: c.data, "The data folder can't be written to")]:
        c = Case(portable=portable, saved="resolution=720\n")
        if not portable:
            for x in ("saves", "logs", "cache"):
                (c.data / x).mkdir()
        before = tree(c.pkg)
        lock(c).chmod(0o555)
        try:
            p = c.run([c.game])
        finally:
            lock(c).chmod(0o755)
        for f in ("args.txt", "env.txt", "cwd.txt"):
            (c.pkg / f).unlink(missing_ok=True)
        result(name, [b for b in [
            p.returncode == 0 and "exit 0",
            says not in " ".join(p.stderr.split()) and "said %r" % p.stderr.strip(),
            tree(c.pkg) != before and "something changed"] if b], "refused, nothing changed")

# A package with a kit: the setup when the engine isn't the kit's, else play.
def kit_case(engine_sha256, engine=True):
    c = Case()
    (c.pkg / "kit").mkdir()
    (c.pkg / "kit/kit.json").write_text("{}\n")
    (c.pkg / "kit/expected.txt").write_text("engine_sha256=%s\n" % engine_sha256)
    if not engine:
        (c.pkg / "bin/jsrf_recomp").unlink()
    return c, c.run([c.game])


def setup_opened(name, c, p):
    result(name, [b for b in [
        p.returncode != 0 and "exit %d" % p.returncode,
        c.started() and "the engine started",
        "stub: the setup, in %s" % c.pkg not in p.stderr and "no setup: %r" % p.stderr.strip(),
        ] if b], "the setup opened (and closed): nothing played")


ours = hashlib.sha256(ENGINE.encode()).hexdigest()
setup_opened("a kit and no engine", *kit_case(ours, engine=False))
setup_opened("a kit and another engine", *kit_case("0" * 64))
c, p = kit_case(ours)
result("a kit and its engine", [b for b in [
    p.returncode != 0 and "exit %d" % p.returncode,
    not c.started() and "the engine didn't start",
    "stub:" in p.stderr and "the setup opened",
    ] if b], "played, no setup")

if failed:
    raise SystemExit("launcher_env: %d of %d runs failed" % (failed, runs))
print("launcher_env: %d runs passed" % runs)
