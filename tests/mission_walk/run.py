"""Compile the story walk that makes Developer > Missions' start-states.txt
(launcher/tools/mission_walk.c, b3 row 38), with launcher/setup/setup_core.c's
file helpers, and check it (test_main.c) on made-up mission files
(made_up.py), never the game's:

- Each story in known/ is made again from its seed (story-*.txt), or by
  hand to show one rule of the walk (case-*.txt), and the walk must reach
  the same missions with the same lines, and give the same file, or fail at
  the same chapter's clear or missing opening. The known answers are what
  mission-select/tools/walk3.py and starts.py (in the project's shared
  folder; the Python walk the Missions test builds' lines were made with)
  give on the same files, so the C walk is held to the Python one, down
  to how many states each search looked at, so that it searches the same
  way and not only to the same end. They
  cover the story's way through nine chapters, puzzles whose ways look
  alike to the search, a chapter's opening that leaves to the game flow,
  lower sensors out of order, and a big puzzle of thousands of states; the
  cases, an entry the walk can't read ending what follows it, an event
  letting the actions run first, a game over, a scene's skip, the garage on
  Load Game, and a "you can't leave yet" action that is no guard of itself. Each story was kept because a bug planted in the
  walk changed its answer.
- The same files handed over in another order, or walked twice, give the
  same file; the Media and Mission folders are found in any case.
- What isn't a mission file is refused with why: a file cut short, a list
  that runs past the file's end, two files for one mission, no 0101, no
  Media or Mission folder, no mission files, a number past 9999.
- start-states.txt is written, replaced and removed, each time after a
  dated backup of the one there, and left as it was when no backup can be
  made.

Requires GCC on PATH, including MinGW on Windows. CC, when set, is the
compiler instead of gcc.
"""
import os
import pathlib
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile

here = pathlib.Path(__file__).resolve().parent
root = here.parents[1]
sys.path.insert(0, str(here))
sys.dont_write_bytecode = True      # nothing left in the checkout
import made_up  # noqa: E402

cc = shlex.split(os.environ.get("CC", "gcc"))
ours = ["tests/mission_walk/test_main.c", "launcher/tools/mission_walk.c", "launcher/setup/setup_core.c",
        "port/xiso.c", "port/xbe_check.c"]
libs = ["-lwinhttp", "-lshell32", "-lole32"] if os.name == "nt" else ["-ldl"]
failed = []


def made(folder, seed, chapters, calm, hard, big):
    subprocess.run([sys.executable, str(here / "made_up.py"), str(folder), str(seed), str(chapters),
                    str(calm), str(hard), str(big)], check=True)


def run(exe, *args):
    r = subprocess.run([exe, *[str(a) for a in args]], capture_output=True, text=True, timeout=300)
    return r.returncode, r.stdout, r.stderr


def check(what, ok, detail=""):
    if not ok:
        failed.append(what)
        print("FAIL %s%s" % (what, ("\n" + detail) if detail else ""))


def write_mission(folder, name, m):
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, name), "wb") as f:
        f.write(made_up.layout(m))


def new_game_only():
    """0101 alone: a mission whose one sensor takes the chapter's clear."""
    m = made_up.M(101)
    k = m.add_exit(0xFF, 0, 0)
    m.trig.append(made_up.entry([], [made_up.word(0, 1, 1)], 1))
    m.ev.append(made_up.entry([made_up.word(0, 1, 1)], [], 230, [k]))
    return m


with tempfile.TemporaryDirectory(prefix="mission_walk-") as directory:
    out = pathlib.Path(directory)
    exe = str(out / "mission_walk.exe")
    miniz = out / "miniz.o"
    m = subprocess.run(cc + ["-O1", "-w", "-c", str(root / "third_party/miniz/miniz.c"), "-o", str(miniz)],
                       capture_output=True, text=True)   # its #pragma message, every time
    if m.returncode:
        sys.exit("mission_walk: miniz.c didn't compile\n" + m.stdout + m.stderr)
    objs = []
    for i, src in enumerate(ours):
        obj = str(out / f"{i}.o")
        subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-DJSRF_WALK_TRACE", "-Ilauncher/tools",
                             "-Ilauncher/setup", "-Iport", "-c", src, "-o", obj], cwd=root, check=True)
        objs.append(obj)
    subprocess.run(cc + [*objs, str(miniz), *libs, "-lm", "-o", exe], cwd=root, check=True)

    code, text, err = run(exe, "ids")
    check("jsrf_mission_id", code == 0, text + err)
    code, text, err = run(exe, "range")
    check("numbers past 9999 refused", code == 0, text + err)
    code, text, err = run(exe, "put", out / "put")
    check("start-states.txt written, replaced and removed after backups", code == 0 and "put: done" in text, text + err)

    # The known stories: the Python walk's answers.
    def order_of(p):
        kind, name = p.stem.split("-", 1)
        return (kind != "case", int(name) if name.isdigit() else 0, name)

    for path in sorted(here.glob("known/*.txt"), key=order_of):
        lines = path.read_text().splitlines()
        head = lines[0].split()
        folder = out / path.stem
        if head[0] == "case":
            subprocess.run([sys.executable, str(here / "made_up.py"), str(folder), "case", head[1]], check=True)
            name = "case %s" % head[1]
        else:
            made(folder, *head[1:])
            name = "story %s" % head[1]
        code, text, err = run(exe, "story", folder)
        check(name + ": exit 0", code == 0, text + err)
        for kind, what in (("reached ", "the missions reached"), ("searched ", "the states each search looked at")):
            wanted = [x for x in lines if x.startswith(kind)]
            got = [x for x in err.splitlines() if x.startswith(kind)]
            check(name + ": " + what, got == wanted,
                  "\n".join("  want %s\n  got  %s" % (a, b) for a, b in zip(wanted, got) if a != b)
                  or "  %d lines wanted, %d got" % (len(wanted), len(got)))
        at = next(i for i in range(1, len(lines)) if not lines[i].startswith(("reached ", "searched ")))
        reached = [x for x in lines if x.startswith("reached ")]
        end = lines[at]
        if end == "file":
            want = "\n".join(lines[at + 1:]) + "\n"
            check(name + ": the file", text == want,
                  "\n".join("  want %s\n  got  %s" % (a, b)
                            for a, b in zip(want.splitlines(), text.splitlines()) if a != b)[:2000])
        elif end.startswith("no clear "):
            check(name + ": no way to chapter %s's clear" % end.split()[2],
                  text.startswith("fail: The walk found no way to chapter %s's clear" % end.split()[2]), text)
        elif end.startswith("no opening "):
            check(name + ": no opening %s" % end.split()[2],
                  text.startswith("fail: ") and "its chapter's opening, %s," % end.split()[2] in text, text)
        else:
            check(name + ": its known answer is read", False, end)
        print("%s: %d missions reached, %s" % (name, len(reached), end))

    # Order, and the folders' names in any case.
    story7 = out / "story-7"
    code, text, err = run(exe, "order", story7)
    check("the same file in any order", code == 0, text + err)
    want = run(exe, "story", story7)[1]
    if os.name != "nt":
        other = out / "cases" / "upper"
        shutil.copytree(story7 / "Media" / "Mission", other / "MEDIA" / "mission")
        check("Media and Mission in any case", run(exe, "story", other)[1] == want)

    # What isn't a mission file.
    def refused(name, folder, words):
        code, text, err = run(exe, "story", folder)
        check(name, code == 0 and text.startswith("fail: ") and words in text, text + err)

    cases = out / "cases"
    refused("no Media folder", cases / "empty", "has no Media folder")
    os.makedirs(cases / "empty", exist_ok=True)
    refused("no Media folder (an empty folder)", cases / "empty", "has no Media folder")
    os.makedirs(cases / "nomission" / "Media", exist_ok=True)
    refused("no Mission folder", cases / "nomission", "has no Mission folder")
    os.makedirs(cases / "nofiles" / "Media" / "Mission", exist_ok=True)
    (cases / "nofiles" / "Media" / "Mission" / "mssn0101.bak").write_bytes(b"x")
    refused("no mission files", cases / "nofiles", "has no mission files in it")

    good = made_up.layout(new_game_only())
    one = cases / "one" / "Media" / "Mission"
    write_mission(one, "mssn0101.bin", new_game_only())
    refused("0101 alone: no chapter 2 to clear to", cases / "one", "no way to chapter 1's clear")

    cut = cases / "cut" / "Media" / "Mission"
    os.makedirs(cut)
    (cut / "mssn0101.bin").write_bytes(good[:0x100])
    refused("a file cut short", cases / "cut", "mssn0101.bin isn't laid out as the game's mission files are")

    past = bytearray(good)
    struct.pack_into("<I", past, 0x180, 1000)          # 1000 sensors in a small file
    os.makedirs(cases / "past" / "Media" / "Mission")
    (cases / "past" / "Media" / "Mission" / "mssn0101.bin").write_bytes(bytes(past))
    refused("a list past the file's end", cases / "past", "isn't laid out")

    words = bytearray(good)
    off = struct.unpack_from("<I", words, 0x17C)[0]     # the sensor's flags, pointed past the end
    struct.pack_into("<I", words, off + 8, len(words) - 2)
    os.makedirs(cases / "words" / "Media" / "Mission")
    (cases / "words" / "Media" / "Mission" / "mssn0101.bin").write_bytes(bytes(words))
    refused("conditions past the file's end", cases / "words", "isn't laid out")

    if os.name != "nt":
        two = cases / "two" / "Media" / "Mission"
        write_mission(two, "mssn0101.bin", new_game_only())
        write_mission(two, "MSSN0101.BIN", new_game_only())
        refused("two files for one mission", cases / "two", "two files for mission 0101")

    nonew = cases / "nonew" / "Media" / "Mission"
    write_mission(nonew, "mssn0102.bin", new_game_only())
    refused("no 0101", cases / "nonew", "no 0101, where New Game starts")

if failed:
    sys.exit("mission_walk: %d failed: %s" % (len(failed), ", ".join(failed)))
print("mission_walk: all cases pass")
