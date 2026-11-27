"""Compile where the launchers keep the player's files (launcher/common/user_files.c),
as the launchers link it, and check it.

test_main.c's layouts: the layout from what was looked at, for Linux and for
Windows (the portable folder, the home folder's places with and without
XDG's, %LOCALAPPDATA%'s), and this system's game_dir, kept relative to the
launcher's folder in the portable layout. Then on real folders, here: what
js_files_find says beside an empty folder, a portable folder, and a
settings file remembering a game that is there or gone, each time writing
nothing at all; and what js_files_prepare makes, or refuses (a portable folder
that can't be written: not as root, whom nothing refuses).

Requires GCC on PATH, including MinGW on Windows. CC, when set, is the
compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
windows = sys.platform == "win32"
bad = []


def tree(top):
    """Every file and folder under top, with each file's size: what a run changed."""
    top = pathlib.Path(top)
    if not top.exists():
        return None
    return sorted((str(p.relative_to(top)), p.stat().st_size if p.is_file() else -1) for p in top.rglob("*"))


with tempfile.TemporaryDirectory(prefix="user-files-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    srcs = ["tests/user_files/test_main.c", "launcher/common/user_files.c", "launcher/setup/setup_core.c",
            "port/xiso.c", "port/xbe_check.c"]
    objs = []
    for s in srcs:
        o = str(work / (pathlib.Path(s).name + ".o"))
        subprocess.run(cc + ["-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Iport", "-Ilauncher/common",
                             "-Ilauncher/setup", "-c", s, "-o", o], cwd=root, check=True)
        objs.append(o)
    o = str(work / "miniz.c.o")
    m = subprocess.run(cc + ["-O1", "-w", "-c", "third_party/miniz/miniz.c", "-o", o], cwd=root,
                       capture_output=True, text=True)   # its #pragma message, every time
    if m.returncode:
        raise SystemExit("user_files: miniz.c didn't compile\n" + m.stdout + m.stderr)
    objs.append(o)
    libs = ["-lwinhttp", "-lshell32", "-lole32", "-luuid"] if windows else ["-ldl", "-lm"]
    subprocess.run(cc + objs + ["-o", exe] + libs, cwd=root, check=True)

    p = subprocess.run([exe, "layouts"], capture_output=True, text=True, timeout=60)
    sys.stdout.write(p.stdout + p.stderr)
    if p.returncode or "FAIL" in p.stdout:
        bad.append("the layouts (above)")

    home = work / "home"
    home.mkdir()
    env = {k: v for k, v in os.environ.items() if not k.startswith(("XDG_", "JSRF_"))}
    if not windows:
        env["HOME"] = str(home)
    # %LOCALAPPDATA%\JSRF-PC is the real one on Windows (its known folder can't be moved
    # for a test): find must leave it as it was too.
    local = pathlib.Path(os.environ.get("LOCALAPPDATA", str(home))) / "JSRF-PC"

    def run(what, here, extra=None):
        e = dict(env)
        e.update(extra or {})
        r = subprocess.run([exe, what, str(here)], env=e, capture_output=True, text=True, timeout=60)
        if r.returncode:
            bad.append("%s %s: exit %d: %s" % (what, here, r.returncode, r.stderr.strip()))
        return dict(line.split("=", 1) for line in r.stdout.splitlines() if "=" in line)

    def find(name, here, want, extra=None):
        """find's answer beside here, with nothing written anywhere."""
        before = (tree(work), tree(local))
        got = run("find", here, extra)
        if (tree(work), tree(local)) != before:
            bad.append("%s: find wrote something" % name)
        miss = ["%s=%r, not %r" % (k, got.get(k), v) for k, v in want.items() if got.get(k) != v]
        if miss:
            bad.append("%s: %s" % (name, "; ".join(miss)))
        else:
            print("ok   %s" % name)
        return got

    sep = "\\" if windows else "/"
    pkg = work / "pkg"
    pkg.mkdir()

    # Beside an empty folder: the home layout, which isn't there yet here.
    if windows:
        got = find("an empty folder", pkg, {"layout": "home", "root": "", "saves": ""})
        if not got.get("settings", "").endswith("\\JSRF-PC\\settings"):
            bad.append("an empty folder: settings %r" % got.get("settings"))
    else:
        h = str(home)
        find("an empty folder", pkg, {
            "rc": "0", "layout": "home", "found": "0", "root": "", "saves": "",
            "settings": h + "/.config/jsrf-pc/settings", "cache": h + "/.local/share/jsrf-pc/save",
            "games": h + "/.local/share/jsrf-pc", "log": h + "/.local/state/jsrf-pc/last-run.log",
            "logs": h + "/.local/state/jsrf-pc/logs", "game_state": "unset", "game": ""})
        xdg = {"XDG_CONFIG_HOME": str(work / "c"), "XDG_DATA_HOME": str(work / "d"),
               "XDG_STATE_HOME": str(work / "s")}
        find("an empty folder, with XDG's", pkg, {
            "layout": "home", "settings": str(work / "c/jsrf-pc/settings"),
            "cache": str(work / "d/jsrf-pc/save"), "log": str(work / "s/jsrf-pc/last-run.log")}, xdg)

        # The home layout's settings, remembering a game: found, and the game
        # there, then gone.
        cfg = home / ".config/jsrf-pc/settings"
        cfg.parent.mkdir(parents=True)
        game = work / "JSRF"
        game.mkdir()
        cfg.write_text("resolution=720\ngame_dir=%s\n" % game)
        find("the home layout, its game there", pkg,
             {"rc": "1", "found": "1", "game_state": "there", "game": str(game)})
        game.rmdir()
        find("the home layout, its game gone", pkg, {"found": "1", "game_state": "gone", "game": str(game)})
        cfg.write_text("game_dir=\n")
        find("the home layout, no game", pkg, {"found": "1", "game_state": "unset", "game": ""})

    # A portable folder: its layout, found, its game relative to the folder.
    port = pkg / "portable"
    port.mkdir()
    pp = str(port) + sep
    find("a portable folder", pkg, {
        "rc": "1", "layout": "portable", "found": "1", "root": str(port), "settings": pp + "settings.cfg",
        "saves": pp + "saves", "cache": pp + "cache", "games": pp + "games",
        "log": pp + "logs" + sep + "last-run.log", "logs": pp + "logs", "game_state": "unset", "game": ""})
    (port / "settings.cfg").write_text("game_dir=games%sJSRF\n" % sep)
    find("a portable folder, its game gone", pkg,
         {"game_state": "gone", "game": str(pkg / "games" / "JSRF")})
    (pkg / "games" / "JSRF").mkdir(parents=True)
    find("a portable folder, its game there", pkg,
         {"game_state": "there", "game": str(pkg / "games" / "JSRF")})

    # prepare: the portable folder's own folders made.
    got = run("prepare", pkg)
    made = ["logs", "saves"] + ([] if windows else ["cache"])
    missing = [d for d in made if not (port / d).is_dir()]
    if got.get("rc") != "0" or missing:
        bad.append("prepare, portable: rc %r, %s not made" % (got.get("rc"), ", ".join(missing) or "nothing"))
    else:
        print("ok   prepare, portable: " + ", ".join(made) + " made")

    if not windows:
        # prepare: the home layout's folders, under HOME.
        other = work / "other"
        other.mkdir()
        got = run("prepare", other)
        made = [home / ".config/jsrf-pc", home / ".local/share/jsrf-pc/save", home / ".local/state/jsrf-pc/logs"]
        missing = [str(d) for d in made if not d.is_dir()]
        if got.get("rc") != "0" or missing:
            bad.append("prepare, home: rc %r, not made: %s" % (got.get("rc"), ", ".join(missing)))
        else:
            print("ok   prepare, home: its three folders made")

        # A portable folder that can't be written: refused, by name.
        if os.geteuid() == 0:
            print("--   prepare, a portable folder that can't be written: not tried, as root")
        else:
            port.chmod(0o555)
            try:
                got = run("prepare", pkg)
            finally:
                port.chmod(0o755)
            if got.get("rc") != "-1" or "can't be written to" not in got.get("err", "") or \
                    str(port) not in got.get("err", ""):
                bad.append("prepare, read-only portable: rc %r, %r" % (got.get("rc"), got.get("err")))
            else:
                print("ok   prepare, a portable folder that can't be written: refused")

if bad:
    print("user_files: FAILED\n  " + "\n  ".join(bad))
    sys.exit(1)
print("user_files: passed")
