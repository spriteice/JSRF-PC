"""Compile where the launchers keep the player's files (launcher/common/user_files.c),
as the launchers link it, and check it.

test_main.c's layouts: the layout from what was looked at, for Linux and for
Windows (the data folder, the portable folder, both, the home folder's
places with and without XDG's, %LOCALAPPDATA%'s), and this system's
game_dir, kept relative to the launcher's folder beside it. Then on real
folders, here: what js_files_find says beside an empty folder, the home
layout's settings remembering a game that is there or gone, a portable
folder, both folders and a data folder, each time writing nothing at all.
What js_files_prepare does: a data folder used; a portable folder renamed to
data, with a game_dir inside it changed to the same place in data; both
folders, and a portable folder that can't be renamed, refused with nothing
changed; the home folder of an earlier version carried over into a new data
folder (its settings, the files beside them but the logs, recordings/), with
nothing there changed and no second copy at the next start; a first start
with nothing to carry over, and one after a data folder left half made. The
game folder's saves copied in once (js_files_saves_in). And, not on Windows
nor as root, folders that can't be written refused with nothing changed: the
portable folder, the launcher's folder with a portable folder or with
nothing beside it, and the data folder; and a carry-over that stops half way
(a file in the home folder that can't be read), refused with no data folder
and no data.new left behind.

Requires GCC on PATH, including MinGW on Windows. CC, when set, is the
compiler instead of gcc.
"""
import os
import pathlib
import shlex
import shutil
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
    # for a test): find must leave it as it was, and prepare is given a folder of the
    # test's own for it (test_main.c), so it is never read for a carry-over either.
    local = pathlib.Path(os.environ.get("LOCALAPPDATA", str(home))) / "JSRF-PC"
    local_before = tree(local)

    def run(what, *args, extra=None):
        e = dict(env)
        e.update(extra or {})
        r = subprocess.run([exe, what] + [str(a) for a in args], env=e, capture_output=True, text=True,
                           timeout=60)
        if r.returncode:
            bad.append("%s %s: exit %d: %s" % (what, args, r.returncode, r.stderr.strip()))
        return dict(line.split("=", 1) for line in r.stdout.splitlines() if "=" in line)

    def find(name, here, want, extra=None):
        """find's answer beside here, with nothing written anywhere."""
        before = (tree(work), tree(local))
        got = run("find", here, extra=extra)
        if (tree(work), tree(local)) != before:
            bad.append("%s: find wrote something" % name)
        miss = ["%s=%r, not %r" % (k, got.get(k), v) for k, v in want.items() if got.get(k) != v]
        if miss:
            bad.append("%s: %s" % (name, "; ".join(miss)))
        else:
            print("ok   %s" % name)
        return got

    def check(name, problems, good):
        problems = [p for p in problems if p]
        if problems:
            bad.append("%s: %s" % (name, "; ".join(problems)))
        else:
            print("ok   %s: %s" % (name, good))

    def files(top):
        """Every file under top, with its bytes."""
        top = pathlib.Path(top)
        return {str(p.relative_to(top)): p.read_bytes() for p in sorted(top.rglob("*")) if p.is_file()}

    sep = "\\" if windows else "/"
    pkg = work / "pkg"
    pkg.mkdir()

    # Beside an empty folder: the home layout, which isn't there yet here.
    if windows:
        got = find("an empty folder", pkg, {"layout": "home", "root": "", "saves": "", "both": "0"})
        if not got.get("settings", "").endswith("\\JSRF-PC\\settings"):
            bad.append("an empty folder: settings %r" % got.get("settings"))
    else:
        h = str(home)
        find("an empty folder", pkg, {
            "rc": "0", "layout": "home", "found": "0", "both": "0", "root": "", "saves": "",
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
        shutil.rmtree(home / ".config")

    # A portable folder, not yet renamed: its layout, found, its game relative
    # to the launcher's folder.
    port = pkg / "portable"
    port.mkdir()
    pp = str(port) + sep
    find("a portable folder", pkg, {
        "rc": "1", "layout": "portable", "found": "1", "both": "0", "root": str(port),
        "settings": pp + "settings.cfg", "saves": pp + "saves", "cache": pp + "cache", "games": pp + "games",
        "log": pp + "logs" + sep + "last-run.log", "logs": pp + "logs", "game_state": "unset", "game": ""})
    (port / "settings.cfg").write_text("game_dir=games%sJSRF\n" % sep)
    find("a portable folder, its game gone", pkg,
         {"game_state": "gone", "game": str(pkg / "games" / "JSRF")})
    (pkg / "games" / "JSRF").mkdir(parents=True)
    find("a portable folder, its game there", pkg,
         {"game_state": "there", "game": str(pkg / "games" / "JSRF")})

    # A data folder beside the portable one: the data folder's layout, and
    # both said; then the data folder alone.
    data = pkg / "data"
    data.mkdir()
    dp = str(data) + sep
    find("both folders", pkg, {"rc": "1", "layout": "data", "found": "1", "both": "1", "root": str(data)})
    shutil.rmtree(port)
    find("a data folder", pkg, {
        "rc": "1", "layout": "data", "found": "1", "both": "0", "root": str(data), "settings": dp + "settings.cfg",
        "saves": dp + "saves", "cache": dp + "cache", "games": dp + "games",
        "log": dp + "logs" + sep + "last-run.log", "logs": dp + "logs", "game_state": "unset", "game": ""})
    (data / "settings.cfg").write_text("game_dir=games%sJSRF\n" % sep)
    find("a data folder, its game there", pkg,
         {"game_state": "there", "game": str(pkg / "games" / "JSRF")})

    def folder(name):
        d = work / name
        d.mkdir()
        return d

    def made(d):
        return [x for x in ("saves", "logs", "cache") if not (d / "data" / x).is_dir()]

    # prepare beside a data folder: its own folders made.
    d = folder("p-data")
    (d / "data").mkdir()
    got = run("prepare", d)
    check("prepare, a data folder", [
        got.get("rc") != "0" and "rc %r: %r" % (got.get("rc"), got.get("err")),
        got.get("layout") != "data" and "layout %r" % got.get("layout"),
        made(d) and "not made: " + ", ".join(made(d))], "used, its saves, logs and cache made")

    # A portable folder: renamed to data, with what is in it, and the game
    # inside it remembered where it is now. A game_dir outside it is left.
    for name, value, want in [
            ("relative", "portable%sgames%sJSRF" % (sep, sep), "data%sgames%sJSRF" % (sep, sep)),
            ("a whole path", None, "data%sgames%sJSRF" % (sep, sep)),
            ("outside it", str(work / "elsewhere"), str(work / "elsewhere"))]:
        d = folder("p-portable-" + name.replace(" ", "-"))
        p = d / "portable"
        (p / "games/JSRF/Media").mkdir(parents=True)
        (p / "games/JSRF/default.xbe").write_bytes(b"")
        (p / "saves/UDATA/4d530004").mkdir(parents=True)
        (p / "saves/UDATA/4d530004/save.dat").write_bytes(b"progress")
        v = value if value is not None else str(p / "games" / "JSRF")
        (p / "settings.cfg").write_text("resolution=720\ngame_dir=%s\nwidescreen=1" % v)  # no last newline
        (p / "developer.cfg").write_text("free_camera=1\n")
        before = files(p)
        got = run("prepare", d)
        after = files(d / "data") if (d / "data").is_dir() else {}
        lines = (d / "data/settings.cfg").read_text().splitlines() if (d / "data/settings.cfg").exists() else []
        kept = {k: after.get(k) for k in before if k != "settings.cfg"}
        check("prepare, a portable folder, its game " + name, [
            got.get("rc") != "0" and "rc %r: %r" % (got.get("rc"), got.get("err")),
            p.exists() and "the portable folder still there",
            got.get("layout") != "data" and "layout %r" % got.get("layout"),
            kept != {k: v for k, v in before.items() if k != "settings.cfg"} and "its files not all moved",
            sorted(lines) != sorted(["resolution=720", "widescreen=1", "game_dir=" + want]) and "settings %r" % lines,
            want.startswith("data") and got.get("game_state") != "there"
            and "the game %r, %r" % (got.get("game_state"), got.get("game")),
            made(d) and "not made: " + ", ".join(made(d))],
            "renamed to data, game_dir=%s" % want)

    # Both folders: refused, naming both, with nothing changed.
    d = folder("p-both")
    (d / "data").mkdir()
    (d / "data/settings.cfg").write_text("resolution=720\n")
    (d / "portable").mkdir()
    (d / "portable/settings.cfg").write_text("resolution=1080\n")
    before = tree(d)
    got = run("prepare", d)
    check("prepare, both folders", [
        got.get("rc") != "-1" and "rc %r" % got.get("rc"),
        "can't tell which one" not in got.get("err", "") and "said %r" % got.get("err"),
        (str(d / "data") not in got.get("err", "") or str(d / "portable") not in got.get("err", ""))
        and "both not named",
        tree(d) != before and "something changed"], "refused, both named, nothing changed")

    # A portable folder that can't be renamed (here, something else named
    # data is in the way): refused, with nothing changed.
    d = folder("p-in-the-way")
    (d / "portable").mkdir()
    (d / "portable/settings.cfg").write_text("resolution=720\n")
    (d / "data").write_text("not a folder\n")
    before = tree(d)
    got = run("prepare", d)
    check("prepare, a portable folder that can't be renamed", [
        got.get("rc") != "-1" and "rc %r" % got.get("rc"),
        "couldn't be renamed to data" not in got.get("err", "") and "said %r" % got.get("err"),
        "Nothing was changed" not in got.get("err", "") and "said %r" % got.get("err"),
        tree(d) != before and "something changed"], "refused, nothing changed")

    # The home folder of an earlier version, carried over into a new data
    # folder: its settings as settings.cfg, the files beside them but the logs,
    # and recordings/ (on Linux, a file there that is a link copied as the file
# it points at); not the logs, the cache, or a disc image unpacked there.
    # Nothing in it changed, and no second copy at the next start.
    d = folder("p-home")
    if windows:
        la = work / "la"
        cfg_dir = la / "JSRF-PC"
        more = [la]
    else:
        cfg_dir = home / ".config/jsrf-pc"
        more = []
        (home / ".local/share/jsrf-pc/save").mkdir(parents=True)
        (home / ".local/share/jsrf-pc/save/cache.bin").write_bytes(b"cache")
        (home / ".local/state/jsrf-pc/logs").mkdir(parents=True)
        (home / ".local/state/jsrf-pc/last-run.log").write_text("log\n")
    (cfg_dir / "recordings/run-1").mkdir(parents=True)
    (cfg_dir / "recordings/run-1/input.bin").write_bytes(b"\x01\x02")
    (cfg_dir / "settings").write_text("resolution=1080\ngame_dir=%s\n" % (work / "JSRF"))
    if windows:
        (cfg_dir / "developer.cfg").write_bytes(b"free_camera=1\n")
    else:   # kept elsewhere, by a link: what it points at is copied
        (work / "dotfiles").mkdir()
        (work / "dotfiles/developer.cfg").write_bytes(b"free_camera=1\n")
        (cfg_dir / "developer.cfg").symlink_to(work / "dotfiles/developer.cfg")
    (cfg_dir / "remap.cfg").write_bytes(b"a=b\n")   # bytes: on Windows, write_text writes CR LF
    (cfg_dir / "start-states.txt").write_bytes(b"0101 off\n")
    (cfg_dir / "last-run.log").write_text("log\n")
    (cfg_dir / "logs").mkdir()
    (cfg_dir / "logs/run-1.log").write_text("log\n")
    (cfg_dir / "save").mkdir()
    (cfg_dir / "save/cache.bin").write_bytes(b"cache")
    (cfg_dir / "JSRF (USA)/Media").mkdir(parents=True)
    homes = (home, cfg_dir)
    before = [tree(x) for x in homes]
    got = run("prepare", d, *more)
    after = files(d / "data") if (d / "data").is_dir() else {}
    want = {"settings.cfg": (cfg_dir / "settings").read_bytes(),
            "developer.cfg": b"free_camera=1\n", "remap.cfg": b"a=b\n", "start-states.txt": b"0101 off\n",
            "recordings%srun-1%sinput.bin" % (sep, sep): b"\x01\x02"}
    check("prepare, carried over from the home folder", [
        got.get("rc") != "0" and "rc %r: %r" % (got.get("rc"), got.get("err")),
        got.get("layout") != "data" and "layout %r" % got.get("layout"),
        after != want and "the data folder holds %s" % sorted(after),
        (d / "data/developer.cfg").is_symlink() and "developer.cfg copied as a link",
        (d / "data.new").exists() and "data.new left",
        [tree(x) for x in homes] != before and "the home folder changed",
        made(d) and "not made: " + ", ".join(made(d))],
        "its settings, developer.cfg, remap.cfg, start-states.txt and recordings, and nothing else")
    (cfg_dir / "settings").write_text("resolution=720\n")
    (cfg_dir / "remap.cfg").write_text("c=d\n")
    got = run("prepare", d, *more)
    check("prepare, the next start", [
        got.get("rc") != "0" and "rc %r: %r" % (got.get("rc"), got.get("err")),
        files(d / "data") != after and "copied again"], "nothing carried over again")

    # A first start with nothing to carry over: the data folder made, and
    # nothing written in the home folder. Then one left half made (data.new):
    # made again.
    empty = folder("empty-home")
    fresh_env = {} if windows else {"HOME": str(empty)}
    for name, junk in [("nothing to carry over", False), ("a data folder left half made", True)]:
        d = folder("p-fresh" + ("-junk" if junk else ""))
        if junk:
            (d / "data.new").mkdir()
            (d / "data.new/settings.cfg").write_text("resolution=1080\n")
        got = run("prepare", d, *([empty] if windows else []), extra=fresh_env)
        check("prepare, " + name, [
            got.get("rc") != "0" and "rc %r: %r" % (got.get("rc"), got.get("err")),
            (d / "data.new").exists() and "data.new left",
            files(d / "data") != {} if (d / "data").is_dir() else "no data folder",
            tree(empty) != [] and "the home folder holds %s" % tree(empty),
            made(d) and "not made: " + ", ".join(made(d))],
            "data made, with its saves, logs and cache, and nothing in the home folder")

    # The saves in the game folder: copied in the first time, and not again.
    d = folder("p-saves")
    (d / "data").mkdir()
    g = work / "game-with-saves"
    (g / "UDATA/4d530004").mkdir(parents=True)
    (g / "UDATA/4d530004/save.dat").write_bytes(b"progress")
    (g / "TDATA/4d530004").mkdir(parents=True)
    (g / "TDATA/4d530004/t.bin").write_bytes(b"t")
    got = run("saves", d, g)
    first = files(d / "data/saves")
    (g / "UDATA/4d530004/save.dat").write_bytes(b"newer")
    got2 = run("saves", d, g)
    check("the game's saves", [
        got.get("rc") != "0" and "rc %r: %r" % (got.get("rc"), got.get("err")),
        got2.get("rc") != "0" and "the next start: rc %r: %r" % (got2.get("rc"), got2.get("err")),
        first != {"UDATA%s4d530004%ssave.dat" % (sep, sep): b"progress", "TDATA%s4d530004%st.bin" % (sep, sep): b"t"}
        and "copied %s" % sorted(first),
        files(d / "data/saves") != first and "copied again",
        not (g / "UDATA/4d530004/save.dat").exists() and "the original moved"],
        "UDATA and TDATA copied once, the originals left")

    # Folders that can't be written, refused with nothing changed (not on
    # Windows, where a folder's read-only mark doesn't stop writing in it, nor
    # as root, whom nothing refuses).
    if windows:
        print("--   prepare, folders that can't be written: not tried on Windows")
    elif os.geteuid() == 0:
        print("--   prepare, folders that can't be written: not tried, as root")
    else:
        def locked(name, setup, lock, says):
            d = folder("p-ro-" + name.replace(" ", "-"))
            setup(d)
            before = tree(d)
            for x in lock(d):
                x.chmod(0o555)
            try:
                got = run("prepare", d, extra={"HOME": str(empty)})
            finally:
                for x in lock(d):
                    x.chmod(0o755)
            check("prepare, " + name, [
                got.get("rc") != "-1" and "rc %r" % got.get("rc"),
                says not in got.get("err", "") and "said %r" % got.get("err"),
                tree(d) != before and "something changed"], "refused, nothing changed")

        def with_portable(d):
            (d / "portable").mkdir()
            (d / "portable/settings.cfg").write_text("resolution=720\n")

        def with_data(d):
            (d / "data").mkdir()
            for x in ("saves", "logs", "cache"):
                (d / "data" / x).mkdir()

        locked("a portable folder that can't be written", with_portable, lambda d: [d / "portable"],
               "The portable folder can't be written to")
        locked("a launcher's folder that can't be written, with a portable folder", with_portable,
               lambda d: [d], "couldn't be renamed to data")
        locked("a launcher's folder that can't be written, with nothing beside it", lambda d: None,
               lambda d: [d], "can't make one in")
        locked("a data folder that can't be written", with_data, lambda d: [d / "data"],
               "The data folder can't be written to")

        # A carry-over that stops half way (a file beside the home folder's
        # settings that can't be read): refused, with no data folder and no
        # data.new left behind, and the home folder as it was.
        h = folder("p-ro-home")
        c = h / ".config/jsrf-pc"
        c.mkdir(parents=True)
        (c / "settings").write_text("resolution=1080\n")
        (c / "remap.cfg").write_bytes(b"a=b\n")
        d = folder("p-ro-copy")
        before = [tree(d), tree(h)]
        (c / "remap.cfg").chmod(0o000)
        try:
            got = run("prepare", d, extra={"HOME": str(h)})
        finally:
            (c / "remap.cfg").chmod(0o644)
        check("prepare, a carry-over that can't be copied", [
            got.get("rc") != "-1" and "rc %r" % got.get("rc"),
            "couldn't be copied from" not in got.get("err", "") and "said %r" % got.get("err"),
            (d / "data.new").exists() and "data.new left",
            (d / "data").exists() and "a data folder made",
            [tree(d), tree(h)] != before and "something changed"], "refused, nothing left behind")

    if tree(local) != local_before:
        bad.append("%s changed" % local)

if bad:
    print("user_files: FAILED\n  " + "\n  ".join(bad))
    sys.exit(1)
print("user_files: passed")
