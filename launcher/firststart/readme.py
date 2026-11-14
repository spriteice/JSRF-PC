#!/usr/bin/env python3
"""The README.txt of a package without the engine (b2 row 4b).

    python3 launcher/firststart/readme.py <linux|windows> <README.txt> > <out>

Takes the system's README.txt (launcher/linux/README.txt or
launcher/windows/README.txt), which the packages with the engine carry as it
is, and changes what differs when the player's PC builds the engine: the
opening's "the engine only", a section on the first start before Controls, and
the legal notes. Each place is found by the words it starts with, and a README
that no longer has one stops this, so the two can't drift apart unseen.
"""
import sys
import textwrap

WIDTH = 78

FIRST_START = {
    "windows": """\
The first start
---------------
This package has no engine yet: the first time you run "JSRF-PC.exe", it
builds the engine from your own copy of the game, once. It asks first:

  "No matching binary detected. Build now with local tools?" when a tools
  folder is beside "JSRF-PC.exe". It comes from the release's tools archive,
  JSRF-PC-tools-windows-x86_64.zip: extract it here, where it makes one
  folder, tools. Nothing is downloaded.

  "No matching binary detected. Download tools now and build?" when there is
  none. The tools come from their own sites: Python from GitHub, and capstone
  and Zig from PyPI, about 100 MB, each checked by its SHA-256.

Yes goes on: it asks where the game is (as in Playing above), translates the
game's default.xbe into C, and compiles it, with one progress bar. It takes a
few minutes (2 to 3 on the PCs tested). No closes the window, and the game
doesn't start. Only the tools in the tools folder are used, whatever else is
on the PC, and nothing is installed: it all stays in this folder.

Then it offers to delete the tools and the code made from your game, a few
hundred MB. The engine (engine\\jsrf_recomp.exe) stays. Keep them to build a
new release faster. The engine is checked against the one this release was
tested with: if yours differs, as another copy of the game can make it, it
says so and asks whether to play anyway.

A new release extracted over this folder builds its own engine at its first
start. The build's log is build\\build.log.
""",
    "linux": """\
The first start
---------------
This package has no engine yet: the first time you run ./jsrf-pc, it builds
the engine from your own copy of the game, once. It asks first:

  "No matching binary detected. Build now with local tools?" when a tools
  folder is beside jsrf-pc. It comes from the release's tools archive,
  JSRF-PC-tools-linux-x86_64.tar.gz: extract it here, where it makes one
  folder, tools. Nothing is downloaded.

  "No matching binary detected. Download tools now and build?" when there is
  none. The tools come from their own sites: Python from GitHub, and capstone
  and Zig from PyPI, about 100 MB, each checked by its SHA-256.

Yes goes on: it asks where the game is (as in Playing above), translates the
game's default.xbe into C, and compiles it, with one progress bar. It takes a
few minutes (2 to 3 on the PCs tested). No closes the window, and the game
doesn't start. Only the tools in the tools folder are used, whatever else is
on the PC, and nothing is installed: it all stays in this folder.

Then it offers to delete the tools and the code made from your game, a few
hundred MB. The engine (bin/jsrf_recomp) stays. Keep them to build a new
release faster. The engine is checked against the one this release was tested
with: if yours differs, as another copy of the game can make it, it says so
and asks whether to play anyway.

A new release extracted over this folder builds its own engine at its first
start. The build's log is build/build.log.
""",
}

# Paragraphs replaced whole: (the words they start with, the new text).
OPENING = ("You need your own copy of the game",
           "You need your own copy of the game -- the standalone US disc (not the "
           "Sega GT 2002 + JSRF combo disc, whose JSRF is a different build). This "
           "package holds none of the game: no game files, no textures, no music, no "
           "video, and none of its code. The first time it starts, it builds the "
           "engine from your copy (see The first start below).")
LEGAL = ("Jet Set Radio Future is (c) SEGA.",
         "Jet Set Radio Future is (c) SEGA. This is an unofficial fan project, not "
         "affiliated with or endorsed by SEGA. This package holds the engine's "
         "runtime and what it takes to build the rest, and nothing of the game: no "
         "game data and none of its code. The engine the first start builds from "
         "your copy, the runtime with the game's code machine-translated to native "
         "code, is made on your PC and stays there.")
# Added to the licences' paragraph, before it points at the folder.
TOOLS = ("The tools the first start uses keep their own licences, in the tools "
         "folder: Python under the PSF licence, capstone under the BSD licence, and "
         "Zig under the MIT licence, with those of LLVM and the C libraries it "
         "carries.")


def flat(par):
    return " ".join(par.split())


def wrap(text):
    return "\n".join(textwrap.wrap(text, WIDTH, break_on_hyphens=False))


def main(argv):
    if len(argv) != 3 or argv[1] not in FIRST_START:
        sys.exit("usage: readme.py <linux|windows> <README.txt>")
    with open(argv[2], encoding="utf-8") as f:
        pars = f.read().replace("\r\n", "\n").split("\n\n")
    found = {"opening": 0, "legal": 0, "licences": 0, "controls": 0}
    out = []
    for par in pars:
        # A section's first paragraph carries its title and underline: the
        # blank lines before it and those two lines are kept as they are.
        body = par.lstrip("\n")
        head = par[:len(par) - len(body)]
        lines = body.split("\n")
        if len(lines) > 2 and lines[1] and set(lines[1]) == {"-"}:
            if lines[0] == "Controls":
                out.append("\n" + FIRST_START[argv[1]].rstrip("\n"))
                found["controls"] += 1
            head += "\n".join(lines[:2]) + "\n"
            body = "\n".join(lines[2:])
        text = flat(body)
        if text.startswith(OPENING[0]):
            body = wrap(OPENING[1])
            found["opening"] += 1
        elif text.startswith(LEGAL[0]):
            body = wrap(LEGAL[1])
            found["legal"] += 1
        elif text.startswith("Licences:"):
            i = text.find(" See the licences")
            if i < 0:
                sys.exit("readme.py: the licences' paragraph doesn't point at the folder")
            body = wrap(text[:i] + " " + TOOLS + text[i:])
            found["licences"] += 1
        out.append(head + body)
    missing = [k for k, n in found.items() if n != 1]
    if missing:
        sys.exit("readme.py: %s: not found once in %s" % (", ".join(missing), argv[2]))
    sys.stdout.write("\n\n".join(out).rstrip("\n") + "\n")


if __name__ == "__main__":
    main(sys.argv)
