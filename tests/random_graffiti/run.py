"""Compile random graffiti (port/random_graffiti.c) and drive it without the
game.

Takes port/random_graffiti.c as it is and runs it on made-up guest memory,
one case to a run (test_main.c), since the switch is read once. Off, or set
to 0, every call gets the game's own answer, nothing is drawn, an area's
load asks the holder nothing, no sheet is made a texture, a spot's draw is
the game's own and a soul brings nothing. On, in a build of the game's own
sheet only the builder's own calls for the player's row of the story's set
are answered: each size with one of its unlocked designs, a size with none
unlocked with the game's own; a build of the game's own draws one design a
size, said in one line of the log, and the next draws its own, every
unlocked design coming up over many builds. An area's load, with a made-up
holder: after VS's build it is the game's own; with room for one set and no
textures loaded it is too; with them loaded, the sheet and its textures are
let go, the step is held while the holder is busy or doesn't take the ask,
the build asked for is the story's from the design files, and a build the
game starts meanwhile ends the wait. With room for four sets, 13 designs a
size are drawn, different as far as the save has them, and a build is asked
for for each set, the game's own sheet's last; each other set's two sheets
are to be made textures under ids of their own, and the builder is answered
for every row of them, a size with none unlocked with the garage's design.
Spots, each a made-up record and mesh: those of a kind take the designs in
turn, the first 13 all different, and keep them; a spot's draw is the game's
own for the game's sheet's design, where its design's sheet wasn't made or
has been let go, and where it has nothing of the player's (and then it is
dealt no design); it is one draw from the design's sheet, the player's
layers in the design's row; and two where another tagger's layer is there
too, each with the other's layers hidden, the game's own sheet's last only
where another tagger's tag goes on over the player's, the first with a
strict depth test and the second with the game's; and a new draw deals
the spots again. A soul got in play, with a holder that can be stepped: the
designs are drawn again with the soul's among them, each build is asked for
at a pass and run to its end at once, the game's own sheet's last, every
texture made again in the place it had, and a spot keeps its place among
the designs; a build not done in its steps is left to the holder; and an
area's load, or the game's letting go of the textures, ends a soul's builds.
Fewer sets are drawn with less texture memory never used, fewer of the
texture manager's places free, no node of its free, or fewer designs
unlocked; and the manager's lists are walked as its own lookup walks them.
The hooks that hand it the builder's calls, the flow's step, the builder's
writes, a spot's draw, a soul and the holder's update need the game: the
test build's steps.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import re
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))

SS, L, XL = r"(2|9|27)", r"(8[4-9]|9\d|10\d|11[01])", r"(112|139)"
T = r"\[GRAFFITI\] t=12\.5 "


def drawn(picks, sets=1):
    """The log's line for a draw of this many designs a size, from the test's unlocked designs."""
    if picks == 1:
        return (rf"{T}random graffiti's set drawn: SS design {SS} \(of 3 unlocked\),"
                rf" S design 33 \(of 1 unlocked\), M the garage's \(none unlocked\),"
                rf" L design {L} \(of 28 unlocked\), XL design {XL} \(of 2 unlocked\)\n")
    return (rf"{T}random graffiti's {picks} designs a size drawn, for {sets} sets of sheets:"
            rf" SS designs( {SS}){{{min(picks, 3)}}} \(of 3 unlocked\),"
            rf" S designs 33 \(of 1 unlocked\), M the garage's \(none unlocked\),"
            rf" L designs( {L}){{{picks}}} \(of 28 unlocked\),"
            rf" XL designs( {XL}){{{min(picks, 2)}}} \(of 2 unlocked\)\n")


def room(sets, picks, unused, mine, places, most=28):
    """... its words for what is to be built, and the room there is (in MB)."""
    return (rf"{sets} set{'s' if sets > 1 else ''}, {picks} design{'s' if picks > 1 else ''} a size"
            rf" \({most} unlocked in the fullest size,"
            rf" {unused * 1024} KB of texture memory never used and {mine * 1024} KB the other sets' before,"
            rf" {places} texture places free\)\n")


def area(sets, picks, unused, mine=0, places=500, stage="no stage loaded", sheet="is let go", most=28):
    """... for an area's load that is to build this many sets."""
    return (rf"{T}an area loads, {stage}: the sheet {sheet}, to be built again for "
            + room(sets, picks, unused, mine, places, most))


def others(made, of, first=0):
    """... for the other sets' sheets, made textures in the places from `first` on."""
    places = "".join(f" {first + n}" for n in range(made))
    return (rf"{T}the other sets' sheets: {made} of {of} made textures, in places{places};"
            rf" the game's own sheet's build is asked for\n")


def soul(design, then):
    """... for a soul got in play with room for four sets, and its builds."""
    return (rf"{T}a graffiti soul's design {design} is got: the sheets are to be built again in play\n"
            rf"{T}a graffiti soul: the sheets are built again in play, " + room(4, 13, 28, 6, 492)
            + drawn(13, 4) + then)


def built(steps):
    return (rf"{T}built again in play: 6 of 6 of the other sets' sheets and 2 of 2 of the game's own"
            rf" made textures again, in {steps} steps of the holder's\n")


DEALT = (rf"({T}spot 00000[0-9A-F]{{3}} is dealt design (0 of 13: set 0's sheet, row 0, texture 00000102"
         r"|\d+ of 13: set [123]'s sheet, row [0-3], texture 5247000[246])\n){6}")
TWICE = (rf"({T}spot 00000[0-9A-F]{{3}} is drawn twice: another tagger's layers from the game's own sheet"
         r" first, the player's from texture 5247000[246]; the first with depth test 201, the second with"
         r" the game's \(depth write 0, test 0\)\n){2}"
         rf"{T}spot 00000[0-9A-F]{{3}} is drawn twice: another tagger's layers from the game's own sheet"
         r" last, the player's from texture 5247000[246]; the first with depth test 201, the second with"
         r" the game's \(depth write 0, test 0\)\n")
NOT_MADE = (rf"{T}set 1's sheet 1 was not made a texture \(the device made no texture\):"
            r" its spots draw the game's own\n")
BEFORE = (rf"{T}the deal before this: \d+ spots dealt a design,"
          r" set 0 \d+, set 1 \d+, set 2 \d+, set 3 \d+\n")
NEXT_LOAD = rf"{T}a graffiti soul's design \d+ is got: the next load's draw has it\n"
SLOW = rf"{T}a build in play is not done in 4096 steps of the holder's: left to its own pace\n"


def few(picks, sets):
    """... for a draw with only SS designs unlocked, this many."""
    return (rf"{T}random graffiti's {picks} designs a size drawn, for {sets} sets of sheets:"
            rf" SS designs( [1-6]){{{picks}}} \(of {picks} unlocked\),"
            r" S the garage's \(none unlocked\), M the garage's \(none unlocked\),"
            r" L the garage's \(none unlocked\), XL the garage's \(none unlocked\)\n")


STILL = "a stage still loaded"
# case: the switch's value (None: not set), and the whole log it must print
cases = {
    "off": (None, ""),
    "zero": ("0", ""),
    "on": ("1", drawn(1) + drawn(1)),
    "area": ("1", area(1, 1, 9, stage=STILL) + drawn(1) + area(1, 1, 9) + drawn(1) + area(1, 1, 9)),
    "spots": ("1", drawn(1) + area(4, 13, 28, places=300, sheet="was just built") + drawn(13, 4) + NOT_MADE
              + others(5, 6, 200) + DEALT + TWICE
              + area(4, 13, 28, mine=5, places=300, stage=STILL) + drawn(13, 4) + others(6, 6, 200)
              + BEFORE + DEALT),
    "soul": ("1", NEXT_LOAD + area(4, 13, 28) + drawn(13, 4) + others(6, 6) + DEALT
             + soul(91, built(4)) + soul(104, built(4)) + soul(95, built(4))
             + soul(91, SLOW + built(4099))
             + soul(104, "") + area(4, 13, 28, mine=6) + drawn(13, 4) + others(6, 6)
             + rf"{T}a graffiti soul's design 95 is got: the sheets are to be built again in play\n"
             + NEXT_LOAD),
    "room": ("1", area(3, 9, 12) + drawn(9, 3) + others(4, 4)
             + area(4, 13, 28, mine=4) + drawn(13, 4) + others(6, 6)
             + area(4, 13, 8, mine=6) + drawn(13, 4) + others(6, 6)
             + area(3, 9, 7, mine=6) + drawn(9, 3) + others(4, 4)
             + area(2, 5, 28, mine=6, places=68) + drawn(5, 2) + others(2, 2, 432)
             + area(1, 1, 28, mine=6, places=0) + drawn(1)
             + area(2, 2, 28, mine=6, most=2) + few(2, 2) + others(2, 2)
             + area(2, 5, 28, mine=6, most=5) + few(5, 2) + others(2, 2)
             + area(3, 6, 28, mine=6, most=6) + few(6, 3) + others(4, 4)),
    "starts": ("1", f"({drawn(1)}){{2000}}"),
}

failed = 0
with tempfile.TemporaryDirectory(prefix="random_graffiti-") as directory:
    exe = str(pathlib.Path(directory) / "test.exe")
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Iport",
                    "port/random_graffiti.c", "tests/random_graffiti/test_main.c", "-o", exe],
                   cwd=root, check=True)
    for case, (switch, log) in cases.items():
        env = {k: v for k, v in os.environ.items() if k != "JSRF_RANDOM_GRAFFITI"}
        if switch is not None:
            env["JSRF_RANDOM_GRAFFITI"] = switch
        run = subprocess.run([exe, case], env=env, capture_output=True, text=True, timeout=60)
        problems = [] if run.returncode == 0 else [f"exit {run.returncode}"]
        problems += [line for line in run.stdout.splitlines() if "FAILED" in line]
        if "check:" not in run.stdout:
            problems.append("no checks ran")
        if not re.fullmatch(log, run.stderr):
            problems.append(f"log was {run.stderr[:400]!r}, wanted {log[:400]!r}")
        print(f"{case}: {'ok' if not problems else 'FAILED'}")
        for problem in problems:
            print(f"  {problem}")
        failed += bool(problems)
if failed:
    raise SystemExit(f"random_graffiti: {failed} case(s) failed")
print("random_graffiti: all cases pass")
