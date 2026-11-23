"""Compile Developer > Missions (port/missions.c, port/missions_menu.c), and
drive it without the game.

Cut by text: the settings file code from src/nv2a_gl/nv2a_gl.c, unchanged,
as tests/developer does (nv2a_gl.c doesn't link on its own): developer.cfg
and start-states.txt are read beside the case's settings file.

Takes port/missions.c as it is and runs it on made-up guest memory, one case
to a run, since the switches are read once (test_main.c); the path layer's
two calls it makes are made up there too. Each run walks the game flow from
the intro through the title, New Game, the graffiti's resource and the
entrances' reset to the scene that starts the mission, as the hook on
sub_0007BDD0 sees it.

A start (JSRF_START_MISSION, JSRF_START_STATE, JSRF_START_SKATER): all three
are taken out of the environment, as JSRF_START_LOAD is, the mission's file is asked for by the
game's own name, the saves go to the save folder's mission-saves, the
title's variables are written before its own calls and New Game is chosen
after, the progress is written as the flow reaches 0x11 (the chapter, the
skaters, the three flag banks, the souls filled to their count with the
lowest not held, each appeared, got and held, and the Noise Tanks) and the
chapter, the mission, the entrance and the skater as it reaches 0x1C, after
the entrances' reset; a skater the progress has locked is unlocked, as the
game's own unlock does; what the line leaves out stays as New Game has it, an
entrance of -1 is written as the game writes it, the line may start with its
own mission, story=0 is taken, and a change to the progress in between is
named. Without JSRF_START_STATE, the mission's line in start-states.txt
beside the settings file is taken (any line ending), and refused as the
variable's would be; JSRF_START_STATE wins over it. Nothing is written in a
later state before the title. Refused, with a line saying why and nothing
written: a mission that isn't four digits, a chapter outside the story's 1 to
9, a skater not one of the game's 24, a state line with a field it
doesn't know, a flag, skater, entrance, count or story out of range, a field
given twice, a list it can't read, another mission's line, and a mission
whose file isn't there. A state line or a skater without a mission is left
alone, and with neither nothing is said. What each start says it started
at, for input playback (missions_started): nothing when refused or not
asked for, Load Game, or the mission with the skater given; and its
progress hashed the same for the same fields however the line came (the
variable, start-states.txt, with its own number or another line ending), 0
for New Game's (no line, or a line that gives nothing), and its own for
each other line.

A start at Load Game (JSRF_START_LOAD=1): the title's part as for a mission,
but Load Game chosen, the saves apart, no mission's file asked for, and nothing
written; a mission, a state line and a skater given with it are taken out and
left alone. Any other value is refused, and the rest goes on as without it.

The menu (test_menu.c): made-up mission files through a made-up kernel file
layer and made-up names at the game's tables' addresses. Only chapters 1 to 9
are listed, each once, by the game's own file names in any case; a file that
can't be read is counted and left out. The levels are in story order (by
their lowest mission), the garage and stg40 (Gouji's Tower) named by us,
the named sets by the game's table, the rest in Other sets at the foot with
their sets; each mission's
caption has its kind, who a race or a battle is against (four at most, each
once, a fight's not), its set in Other sets, and whether start-states.txt
marks it off the story's way. Without the game's names, levels go by their
sets and characters by number. Character is all 24 characters, in the
game's order from its table (by number when the table isn't one), Galam
shown as Garam, kept in developer.cfg; then Load game between two lines;
then Photo mode, a tick, and Record, a tick, and a line below them. A pick
asks first, sets the start's variables (JSRF_START_PHOTO=1 too while Photo
mode is ticked, and JSRF_RECORD=1 while Record is, for a mission or Load
game) and restarts, or takes them out again when the restart comes back. Every file it opens is closed, read
only.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
settings = gl[gl.index("static void sdl_cfg_path(char *buf, size_t n)"):
              gl.index("/* nv2a_widescreen() asks this when RECOMP_WIDESCREEN is not set. */")]
preamble = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)
#endif
""" + settings

STARTING = "[MISSION] starting at 0110: chapter 1, mission 10{skater}; "
NO_STATE = (STARTING.format(skater="") + "no JSRF_START_STATE or line in start-states.txt,"
            " so the progress is New Game's\n")
SAVES = "[MISSION] saves in save/mission-saves (E:\\UDATA and E:\\TDATA), apart from the player's own\n"
TITLE = "[MISSION] t=12.50 the title (game flow state 0xA) left out: New Game chosen, on to state 0xE\n"
TITLE_LOAD = TITLE.replace("New Game chosen", "Load Game chosen")
AT_LOAD = "[MISSION] starting at Load Game, with the mission runs' saves{left}\n"
PROGRESS = ("[MISSION] t=12.50 before the graffiti's resource (state 0x11): chapter 1, skaters {skaters},"
            " Chapter flags {c}, Global flags {g}, bank 3 flags {r}, souls held {souls},"
            " Noise Tanks {tanks}\n")
NEW_GAME = dict(skaters="1,2,6 (New Game's)", c="0 (New Game's)", g="0 (New Game's)",
                r="0 (New Game's)", souls="20 (0 added)", tanks="0 (0 added)")
STARTED = ("[MISSION] t=12.50 0110 started at game flow state 0x1C: chapter 1, mission 10,"
           " entrance {entrance}, skater {skater}; the progress {progress}\n")
AS_WRITTEN = "as written at state 0x11"
FULL_STATE = "e=7 C=0,2 G=1 R=384,450,511 S=1,2,6 souls=25 tanks=200"
FULL = dict(skaters="1,2,6", c="2", g="1", r="3", souls="25 (5 added)", tanks="200 (200 added)")
LEFT = "0 (as the game left it)"
YOYO = "6 (as the game left it)"
FIELDS = "e, C, G, R, S, souls, tanks and story"


def started(entrance=LEFT, skater=YOYO, progress=AS_WRITTEN):
    return STARTED.format(entrance=entrance, skater=skater, progress=progress)


def refused(why, source="JSRF_START_STATE"):
    return f"[MISSION] {source} refused for 0110: {why}; starting normally\n"


def full_run(source, line, skater=""):
    return (STARTING.format(skater=skater) + f"{source}: {line}\n" + SAVES + TITLE
            + PROGRESS.format(**FULL) + started(entrance="7"))


# case: (test_main.c's case, the environment, start-states.txt or None, the
# whole log it must print)
cases = {
    "full": ("full", {"JSRF_START_MISSION": "0110", "JSRF_START_STATE": FULL_STATE}, None,
             full_run("JSRF_START_STATE", FULL_STATE)),
    "own_line": ("own_line", {"JSRF_START_MISSION": "0110", "JSRF_START_STATE": "0110 " + FULL_STATE}, None,
                 full_run("JSRF_START_STATE", "0110 " + FULL_STATE)),
    "changed": ("changed", {"JSRF_START_MISSION": "0110", "JSRF_START_STATE": FULL_STATE}, None,
                STARTING.format(skater="") + "JSRF_START_STATE: " + FULL_STATE + "\n" + SAVES + TITLE
                + PROGRESS.format(**FULL)
                + started(entrance="7", progress="changed since state 0x11: the Global flags")),
    "no_state": ("no_state", {"JSRF_START_MISSION": "0110"}, None,
                 NO_STATE + SAVES + TITLE + PROGRESS.format(**NEW_GAME) + started()),
    "order": ("order", {"JSRF_START_MISSION": "0110"}, None,
              NO_STATE + SAVES + TITLE + PROGRESS.format(**NEW_GAME) + started()),
    "exit_names_none": ("exit_names_none", {"JSRF_START_MISSION": "0110", "JSRF_START_STATE": "e=-1"}, None,
                        STARTING.format(skater="") + "JSRF_START_STATE: e=-1\n" + SAVES + TITLE
                        + PROGRESS.format(**NEW_GAME) + started(entrance="-1")),
    "no_entrance": ("no_entrance", {"JSRF_START_MISSION": "0110", "JSRF_START_STATE": "S=0,1,2,3 souls=15"},
                    None,
                    STARTING.format(skater="") + "JSRF_START_STATE: S=0,1,2,3 souls=15\n" + SAVES + TITLE
                    + PROGRESS.format(**dict(NEW_GAME, skaters="0,1,2,3")) + started()),
    "story_off": ("no_state", {"JSRF_START_MISSION": "0110", "JSRF_START_STATE": "story=0"}, None,
                  STARTING.format(skater="") + "JSRF_START_STATE: story=0\n" + SAVES + TITLE
                  + PROGRESS.format(**NEW_GAME) + started()),
    "no_file": ("no_file", {"JSRF_START_MISSION": "0110"}, None,
                NO_STATE + "[MISSION] 0110 refused: the game's files have no"
                " D:\\Media\\Mission\\mssn0110.bin; starting normally\n"),
    "no_game_manager": ("no_game_manager", {"JSRF_START_MISSION": "0110"}, None,
                        NO_STATE + SAVES + "[MISSION] t=12.50 0110: the game manager isn't there at"
                        " the title; starting normally, with the saves still apart\n"),
    "nothing": ("nothing", {}, None, ""),
    "state_alone": ("nothing", {"JSRF_START_STATE": FULL_STATE}, None,
                    "[MISSION] JSRF_START_STATE without JSRF_START_MISSION: left alone\n"),
    "skater_alone": ("nothing", {"JSRF_START_SKATER": "2"}, None,
                     "[MISSION] JSRF_START_SKATER without JSRF_START_MISSION: left alone\n"),
    # Developer > Missions > Character's skater, written with the mission.
    "skater": ("skater", {"JSRF_START_MISSION": "0110", "JSRF_START_SKATER": "2"}, None,
               STARTING.format(skater=", skater 2") + "no JSRF_START_STATE or line in start-states.txt,"
               " so the progress is New Game's\n" + SAVES + TITLE + PROGRESS.format(**NEW_GAME)
               + started(skater="2")),
    # A skater New Game hasn't unlocked, unlocked as the game does; the last
    # of the game's 24; and one a line's skaters have locked.
    "skater_locked": ("skater_locked", {"JSRF_START_MISSION": "0110", "JSRF_START_SKATER": "3"}, None,
                      STARTING.format(skater=", skater 3") + "no JSRF_START_STATE or line in start-states.txt,"
                      " so the progress is New Game's\n" + SAVES + TITLE
                      + PROGRESS.format(**dict(NEW_GAME, skaters="1,2,3,6 (New Game's; 3 unlocked, the skater"
                                                " given)"))
                      + started(skater="3")),
    "skater_last": ("skater_last", {"JSRF_START_MISSION": "0110", "JSRF_START_SKATER": "23"}, None,
                    STARTING.format(skater=", skater 23") + "no JSRF_START_STATE or line in start-states.txt,"
                    " so the progress is New Game's\n" + SAVES + TITLE
                    + PROGRESS.format(**dict(NEW_GAME, skaters="1,2,6,23 (New Game's; 23 unlocked, the skater"
                                              " given)"))
                    + started(skater="23")),
    "skater_line": ("skater_line", {"JSRF_START_MISSION": "0110", "JSRF_START_STATE": "S=0,1,2,3",
                                    "JSRF_START_SKATER": "6"}, None,
                    STARTING.format(skater=", skater 6") + "JSRF_START_STATE: S=0,1,2,3\n" + SAVES + TITLE
                    + PROGRESS.format(**dict(NEW_GAME, skaters="0,1,2,3,6 (6 unlocked, the skater given)"))
                    + started(skater="6")),
    # Load Game: nothing but the title's part, with the saves apart; what a
    # mission start would take is taken out and left alone.
    "load": ("load", {"JSRF_START_LOAD": "1"}, None, AT_LOAD.format(left="") + SAVES + TITLE_LOAD),
    "load_over_mission": ("load", {"JSRF_START_LOAD": "1", "JSRF_START_MISSION": "0110",
                                   "JSRF_START_SKATER": "3", "JSRF_START_STATE": FULL_STATE}, None,
                          AT_LOAD.format(left=" (JSRF_START_MISSION left alone)") + SAVES + TITLE_LOAD),
    "load_no_game_manager": ("no_game_manager", {"JSRF_START_LOAD": "1"}, None,
                             AT_LOAD.format(left="") + SAVES + "[MISSION] t=12.50 Load Game: the game manager"
                             " isn't there at the title; starting normally, with the saves still apart\n"),
    "load_refused": ("nothing", {"JSRF_START_LOAD": "yes"}, None,
                     "[MISSION] JSRF_START_LOAD=yes refused: not 1; left alone\n"),
    "load_refused_mission": ("no_state", {"JSRF_START_LOAD": "0", "JSRF_START_MISSION": "0110"}, None,
                             "[MISSION] JSRF_START_LOAD=0 refused: not 1; left alone\n"
                             + NO_STATE + SAVES + TITLE + PROGRESS.format(**NEW_GAME) + started()),
    # start-states.txt beside the settings file: the mission's line, with
    # either line ending, after others; JSRF_START_STATE wins over it.
    "lines": ("full", {"JSRF_START_MISSION": "0110"},
              "0100 e=0\n0110 " + FULL_STATE + "\n0112 e=0\n",
              full_run("start-states.txt", "0110 " + FULL_STATE)),
    "lines_crlf": ("full", {"JSRF_START_MISSION": "0110", "JSRF_START_SKATER": "1"},
                   "0100 e=0\r\n0110 " + FULL_STATE + "\r\n",
                   full_run("start-states.txt", "0110 " + FULL_STATE, ", skater 1")
                   .replace("skater 6 (as the game left it)", "skater 1")),
    "lines_last": ("full", {"JSRF_START_MISSION": "0110"}, "01100 e=3\n0110 " + FULL_STATE,
                   full_run("start-states.txt", "0110 " + FULL_STATE)),
    "lines_env_wins": ("full", {"JSRF_START_MISSION": "0110", "JSRF_START_STATE": FULL_STATE},
                       "0110 e=3\n", full_run("JSRF_START_STATE", FULL_STATE)),
    "lines_none": ("no_state", {"JSRF_START_MISSION": "0110"}, "0100 e=0\n0111 e=1\n",
                   NO_STATE + SAVES + TITLE + PROGRESS.format(**NEW_GAME) + started()),
    "lines_bare": ("no_state", {"JSRF_START_MISSION": "0110"}, "0110\n",
                   STARTING.format(skater="") + "start-states.txt: 0110\n" + SAVES + TITLE
                   + PROGRESS.format(**NEW_GAME) + started()),
    "lines_empty": ("no_state", {"JSRF_START_MISSION": "0110"}, "",
                    NO_STATE + SAVES + TITLE + PROGRESS.format(**NEW_GAME) + started()),
    "lines_refused": ("refused", {"JSRF_START_MISSION": "0110"}, "0110 x=1\n",
                      refused(f"\"x\" is not one of {FIELDS}", "start-states.txt")),
    "lines_too_long": ("refused", {"JSRF_START_MISSION": "0110"}, "0110 C=" + ",".join(["1"] * 1100) + "\n",
                       refused("longer than 2047 characters", "start-states.txt")),
    "lines_too_big": ("no_state", {"JSRF_START_MISSION": "0110"}, "#" * (1 << 20) + "\n0110 e=7\n",
                      "[MISSION] {folder}start-states.txt left out: unreadable, or over 1048576 bytes\n"
                      + NO_STATE + SAVES + TITLE + PROGRESS.format(**NEW_GAME) + started()),
}
for name, mission, why in (("letters", "abcd", "not four digits, CCNN"),
                           ("three_digits", "011", "not four digits, CCNN"),
                           ("five_digits", "01100", "not four digits, CCNN"),
                           ("sign", "+110", "not four digits, CCNN")):
    cases["mission_" + name] = ("refused", {"JSRF_START_MISSION": mission}, None,
                                f"[MISSION] JSRF_START_MISSION={mission} refused: {why}; starting normally\n")
for name, mission, chapter in (("chapter_0", "0010", 0), ("chapter_10", "1010", 10)):
    cases["mission_" + name] = ("refused", {"JSRF_START_MISSION": mission}, None,
                                f"[MISSION] JSRF_START_MISSION={mission} refused: chapter {chapter}, where"
                                " only the story's, 1 to 9, start this way; starting normally\n")
for name, skater in (("word", "x"), ("empty", ""), ("sign", "+2"), ("minus", "-1"), ("past", "24"),
                     ("far", "64"), ("space", "2 ")):
    cases["skater_" + name] = ("refused", {"JSRF_START_MISSION": "0110", "JSRF_START_SKATER": skater,
                                           "JSRF_START_STATE": FULL_STATE}, None,
                               f"[MISSION] JSRF_START_SKATER={skater} refused: not 0 to 23, the game's"
                               " characters; starting normally\n")
for name, line, why in (
        ("field", "x=1", f"\"x\" is not one of {FIELDS}"),
        ("long_key", "CG=1", f"\"CG\" is not one of {FIELDS}"),
        ("no_value", "e=0 C", "\"C\" has no value"),
        ("flag", "C=512", "C=512 is not what C= takes"),
        ("skater", "S=1,32", "S=1,32 is not what S= takes"),
        ("empty", "G=", "G= is not what G= takes"),
        ("list", "R=1,,2", "R=1,,2 is not what R= takes"),
        ("trailing", "R=1,", "R=1, is not what R= takes"),
        ("entrance", "e=-2", "e=-2 is not what e= takes"),
        ("entrance_word", "e=7x", "e=7x is not what e= takes"),
        ("souls", "souls=257", "souls=257 is not what souls= takes"),
        ("tanks", "tanks=1025", "tanks=1025 is not what tanks= takes"),
        ("story", "story=2", "story=2 is not what story= takes"),
        ("story_twice", "story=0 story=1", "story= is given twice"),
        ("twice", "C=1 G=2 C=3", "C= is given twice"),
        ("other_line", "0112 e=0", "it is 0112's line, not 0110's"),
        ("too_long", "C=" + ",".join(["1"] * 1100), "longer than 2047 characters")):
    cases["state_" + name] = ("refused", {"JSRF_START_MISSION": "0110", "JSRF_START_STATE": line}, None,
                              refused(why))

# The menu: test_menu.c's case, start-states.txt, developer.cfg, the rows it
# must print in order, and the whole log.
SUMMARY = ("[MISSION] Developer > Missions: 12 story missions from D:\\Media\\Mission, in 7 levels and Other"
           " sets; 1 off the story's way (start-states.txt); 2 unreadable; Character {who}\n")
CONFIRM = "; asks \"Restart the game at this mission, losing unsaved progress?\""
LINES = "0100 e=0\n0197 G=1 story=0\n0110 e=7 story=1\n"
# test_menu.c's made-up order of the characters, and its names: Galam, shown
# as Garam, is 8.
ORDER = [(5 * i + 6) % 24 for i in range(24)]
NAMES = ["Garam" if i == 8 else f"Char{i}" for i in range(24)]
CHARACTERS = " | ".join(NAMES[i] for i in ORDER)
BY_NUMBER = " | ".join(NAMES)
NO_ORDER = ("[MISSION] Developer > Missions: the game's order of its characters isn't at 0x0021C450;"
            " Character by number\n")


def rows(skaters=CHARACTERS, ticked=0, named=True):
    def name(n, stg):
        return f"Name {n} ({stg})" if named else stg
    return [f"row: CHOICE Character in Missions: {skaters} [ticked {ticked}]",
            "row: SEPARATOR - in Missions",
            "row: ACTION Load game in Missions; asks \"Restart at Load Game with the mission saves, losing"
            " unsaved progress?\"",
            "row: SEPARATOR - in Missions",
            "row: TOGGLE Photo mode in Missions [ticked 0]",
            "row: TOGGLE Record in Missions [ticked 0]",
            "row: SEPARATOR - in Missions",
            "row: CHOICE Garage (stg00) in Missions: 0100 (scene) | 0197 (scene, not on the story's way)"
            " [ticked -1]" + CONFIRM,
            f"row: CHOICE {name(2, 'stg12')} in Missions: 0110 (free play) | 0112 (race, Char0) | 0113 (fight)"
            " [ticked -1]" + CONFIRM,
            f"row: CHOICE {name(0, 'stg10')} in Missions: 0120 (free play) | 0371 (tag battle, Char10, Char12,"
            " Garam)"
            " [ticked -1]" + CONFIRM,
            f"row: CHOICE {name(1, 'stg11')} in Missions: 0230 (free play) [ticked -1]" + CONFIRM,
            f"row: CHOICE {name(9, 'stg30')} in Missions: 0252 (flag game, Char1, Char2, Char3, Char4)"
            " [ticked -1]" + CONFIRM,
            f"row: CHOICE {name(5, 'stg21')} in Missions: 0361 (kind 9) [ticked -1]" + CONFIRM,
            "row: CHOICE Gouji's Tower (stg40) in Missions: 0890 (fight) [ticked -1]" + CONFIRM,
            "row: CHOICE Other sets in Missions: 0296 (scene, stg09) [ticked -1]" + CONFIRM]


def no_names_rows():
    r = rows(" | ".join("Char6" if i == 6 else f"#{i}" for i in range(24)), ticked=6, named=False)
    return [line.replace("Char0", "#0").replace("Char10, Char12, Garam", "#10, #12, #8")
            .replace("Char1, Char2, Char3, Char4", "#1, #2, #3, #4") for line in r]


menu_cases = {
    "menu": ("menu", LINES, None, rows(), SUMMARY.format(who="Char6")),
    "menu_kept": ("menu", LINES, "mission_skater=1\n", rows(ticked=ORDER.index(1)), SUMMARY.format(who="Char1")),
    "menu_kept_garam": ("menu", LINES, "mission_skater=8\n", rows(ticked=ORDER.index(8)),
                        SUMMARY.format(who="Garam")),
    "menu_kept_bad": ("menu", LINES, "mission_skater=24\n", rows(),
                      "[MISSION] developer.cfg's mission_skater=24 isn't one of the game's characters (0 to 23);"
                      " Yoyo instead\n" + SUMMARY.format(who="Char6")),
    "bad_order": ("bad_order", LINES, None, rows(BY_NUMBER, ticked=6), NO_ORDER + SUMMARY.format(who="Char6")),
    "menu_no_lines": ("menu", None, None, [line.replace(", not on the story's way", "") for line in rows()],
                      SUMMARY.format(who="Char6").replace("; 1 off", "; 0 off")),
    "no_names": ("no_names", LINES, None, no_names_rows(),
                 "[MISSION] Developer > Missions: the game's level names aren't at 0x0021B988; levels by their"
                 " sets alone\n[MISSION] Developer > Missions: 1 of the game's 24 character names at 0x001EFEF0;"
                 " the rest by number\n" + NO_ORDER + SUMMARY.format(who="Char6")),
    "no_folder": ("no_folder", LINES, None, [],
                  "[MISSION] Developer > Missions: no story mission files in D:\\Media\\Mission; not shown\n"),
    "pick": ("pick", LINES, None,
             rows() + [f"restart: JSRF_START_MISSION=0112 JSRF_START_SKATER={ORDER[1]} JSRF_START_LOAD=-"
                        " JSRF_START_PHOTO=- JSRF_RECORD=-"],
             SUMMARY.format(who="Char6")
             + f"[MISSION] Developer > Missions > Character: Char{ORDER[1]} (skater {ORDER[1]}), from the next"
             " mission picked\n"
             f"[MISSION] 0112 picked in Developer > Missions, skater {ORDER[1]}: restarting\n"),
    "pick_fails": ("pick_fails", LINES, None,
                   rows() + [f"restart: JSRF_START_MISSION=0112 JSRF_START_SKATER={ORDER[1]} JSRF_START_LOAD=-"
                              " JSRF_START_PHOTO=- JSRF_RECORD=-"],
                   SUMMARY.format(who="Char6")
                   + f"[MISSION] Developer > Missions > Character: Char{ORDER[1]} (skater {ORDER[1]}), from the"
                   " next mission picked\n"
                   f"[MISSION] 0112 picked in Developer > Missions, skater {ORDER[1]}: restarting\n"
                   "[MISSION] 0112: the game couldn't restart; nothing started\n"),
    "menu_load": ("load", LINES, None,
                  rows() + ["restart: JSRF_START_MISSION=- JSRF_START_SKATER=- JSRF_START_LOAD=1"
                            " JSRF_START_PHOTO=- JSRF_RECORD=-"],
                  SUMMARY.format(who="Char6") + "[MISSION] Load game picked in Developer > Missions: restarting\n"),
    "menu_load_fails": ("load_fails", LINES, None,
                   rows() + ["restart: JSRF_START_MISSION=- JSRF_START_SKATER=- JSRF_START_LOAD=1"
                             " JSRF_START_PHOTO=- JSRF_RECORD=-"],
                   SUMMARY.format(who="Char6") + "[MISSION] Load game picked in Developer > Missions: restarting\n"
                   "[MISSION] Load game: the game couldn't restart; nothing started\n"),
    # Photo mode ticked: carried over a pick's restart, and taken out again
    # when the restart comes back.
    "photo": ("photo", LINES, None,
              rows() + ["restart: JSRF_START_MISSION=0110 JSRF_START_SKATER=6 JSRF_START_LOAD=- JSRF_START_PHOTO=1"
                        " JSRF_RECORD=-"],
              SUMMARY.format(who="Char6")
              + "[MISSION] 0110 picked in Developer > Missions, skater 6, in photo mode: restarting\n"),
    "photo_fails": ("photo_fails", LINES, None,
                    rows() + ["restart: JSRF_START_MISSION=0110 JSRF_START_SKATER=6 JSRF_START_LOAD=-"
                              " JSRF_START_PHOTO=1 JSRF_RECORD=-"],
                    SUMMARY.format(who="Char6")
                    + "[MISSION] 0110 picked in Developer > Missions, skater 6, in photo mode: restarting\n"
                    "[MISSION] 0110: the game couldn't restart; nothing started\n"),
    "photo_load": ("photo_load", LINES, None,
                   rows() + ["restart: JSRF_START_MISSION=- JSRF_START_SKATER=- JSRF_START_LOAD=1"
                             " JSRF_START_PHOTO=1 JSRF_RECORD=-"],
                   SUMMARY.format(who="Char6")
                   + "[MISSION] Load game picked in Developer > Missions, in photo mode: restarting\n"),
    # Record ticked: JSRF_RECORD=1 carried over a pick's restart, and taken
    # out again when the restart comes back.
    "record": ("record", LINES, None,
               rows() + ["restart: JSRF_START_MISSION=0110 JSRF_START_SKATER=6 JSRF_START_LOAD=- JSRF_START_PHOTO=-"
                         " JSRF_RECORD=1"],
               SUMMARY.format(who="Char6")
               + "[MISSION] 0110 picked in Developer > Missions, skater 6, recording: restarting\n"),
    "record_fails": ("record_fails", LINES, None,
                     rows() + ["restart: JSRF_START_MISSION=0110 JSRF_START_SKATER=6 JSRF_START_LOAD=-"
                               " JSRF_START_PHOTO=- JSRF_RECORD=1"],
                     SUMMARY.format(who="Char6")
                     + "[MISSION] 0110 picked in Developer > Missions, skater 6, recording: restarting\n"
                     "[MISSION] 0110: the game couldn't restart; nothing started\n"),
    "record_load": ("record_load", LINES, None,
                    rows() + ["restart: JSRF_START_MISSION=- JSRF_START_SKATER=- JSRF_START_LOAD=1"
                              " JSRF_START_PHOTO=- JSRF_RECORD=1"],
                    SUMMARY.format(who="Char6")
                    + "[MISSION] Load game picked in Developer > Missions, recording: restarting\n"),
}

# What each start notes for a recording (missions_started), by the case: the
# skater given, and its progress's hash. The same progress hashes the same
# however its line came or is written; none, or a line that gives nothing,
# is New Game's, 0; every other line its own.
FULL_CASES = ["full", "own_line", "changed", "lines", "lines_crlf", "lines_last", "lines_env_wins"]
NEW_GAME_CASES = ["no_state", "order", "skater", "skater_locked", "skater_last", "lines_none", "lines_bare",
                  "lines_empty", "lines_too_big", "no_game_manager", "load_refused_mission"]
OTHER_CASES = ["exit_names_none", "no_entrance", "story_off", "skater_line"]
SKATERS = {"skater": 2, "skater_locked": 3, "skater_last": 23, "skater_line": 6, "lines_crlf": 1}
started = {}

failed = 0
with tempfile.TemporaryDirectory(prefix="missions-") as directory:
    work = pathlib.Path(directory)
    exe = str(work / "test.exe")
    menu_exe = str(work / "menu.exe")
    # -Wno-unused-function: the settings code has helpers only the rest of
    # nv2a_gl.c calls.
    flags = cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-DNV2A_GL_USE_SDL=1",
                  "-Iport", "-Isrc", "-Isrc/kernel"]
    # Each program's own file, with the settings code in front, named apart,
    # and port/missions.c built once and linked into both, as the engine
    # builds it: built with line counts (tests/run_all.py --coverage), two
    # sources read from standard input, or a second build of
    # port/missions.c, would share one counts file with another stamp, and
    # the second program's run would say so in the log this compares.
    main_c, menu_c = work / "missions_test_main.c", work / "missions_test_menu.c"
    main_c.write_text(preamble + (root / "tests/missions/test_main.c").read_text(encoding="utf-8"),
                      encoding="utf-8")
    menu_c.write_text(preamble + (root / "tests/missions/test_menu.c").read_text(encoding="utf-8"),
                      encoding="utf-8")
    missions_o = str(work / "missions.o")
    subprocess.run([*flags, "-c", "port/missions.c", "-o", missions_o], cwd=root, check=True)
    subprocess.run([*flags, str(main_c), missions_o, "-o", exe], cwd=root, check=True)
    subprocess.run([*flags, "-DXBOXRECOMP_MENU=1", "-Isrc/menu", "-Isrc/input", str(menu_c), missions_o,
                    "port/missions_menu.c", "-o", menu_exe], cwd=root, check=True)

    def run(name, exe, arg, extra, lines, dev):
        folder = work / name
        folder.mkdir()
        if lines is not None:
            (folder / "start-states.txt").write_bytes(lines.encode())
        if dev is not None:
            (folder / "developer.cfg").write_text(dev)
        env = {k: v for k, v in os.environ.items() if not k.startswith(("JSRF_", "RECOMP_"))}
        env.update(extra, JSRF_SETTINGS_FILE=str(folder / "settings.cfg"))
        result = subprocess.run([exe, arg], capture_output=True, text=True, timeout=60, env=env)
        return folder, result

    for case, (arg, extra, lines, log) in cases.items():
        folder, result = run(case, exe, arg, extra, lines, None)
        log = log.replace("{folder}", str(folder) + os.sep)
        problems = [] if result.returncode == 0 else [f"exit {result.returncode}"]
        problems += [line for line in result.stdout.splitlines() if "FAILED" in line]
        for line in result.stdout.splitlines():
            if line.startswith("started: "):
                skater, progress = line[len("started: skater "):].split(", progress ")
                started[case] = (int(skater), progress)
        if result.stderr != log:
            problems.append(f"log was {result.stderr!r}, wanted {log!r}")
        print(f"{case}: {'ok' if not problems else 'FAILED'}")
        for problem in problems:
            print(f"  {problem}")
        failed += bool(problems)

    for case, (arg, lines, dev, want, log) in menu_cases.items():
        folder, result = run(case, menu_exe, arg, {}, lines, dev)
        problems = [] if result.returncode == 0 else [f"exit {result.returncode}"]
        problems += [line for line in result.stdout.splitlines() if "FAILED" in line]
        got = [line for line in result.stdout.splitlines() if line.startswith(("row: ", "restart: "))]
        if got != want:
            problems.append(f"rows were {got!r}, wanted {want!r}")
        if result.stderr != log:
            problems.append(f"log was {result.stderr!r}, wanted {log!r}")
        if arg.startswith("pick"):
            kept = (folder / "developer.cfg").read_text() if (folder / "developer.cfg").exists() else ""
            if kept != f"mission_skater={ORDER[1]}\n":
                problems.append(f"developer.cfg was {kept!r}, wanted Char{ORDER[1]} kept")
        print(f"{case}: {'ok' if not problems else 'FAILED'}")
        for problem in problems:
            print(f"  {problem}")
        failed += bool(problems)

    # What each start would note for a recording.
    problems = []
    full = {started[c][1] for c in FULL_CASES if c in started}
    if len(full) != 1 or len([c for c in FULL_CASES if c in started]) != len(FULL_CASES) or "00000000" in full:
        problems.append(f"the full line's progress: {[(c, started.get(c)) for c in FULL_CASES]}")
    for c in NEW_GAME_CASES:
        if started.get(c, (0, ""))[1] != "00000000":
            problems.append(f"{c}: progress {started.get(c)}, wanted New Game's, 00000000")
    others = [started.get(c, (0, "00000000"))[1] for c in OTHER_CASES]
    if len(set(others) | full) != len(others) + 1 or "00000000" in others:
        problems.append(f"each other line's progress its own: {list(zip(OTHER_CASES, others))}")
    for c, (skater, _) in started.items():
        if skater != SKATERS.get(c, -1):
            problems.append(f"{c}: skater {skater}, wanted {SKATERS.get(c, -1)}")
    print(f"started: {'ok' if not problems else 'FAILED'}")
    for problem in problems:
        print(f"  {problem}")
    failed += bool(problems)
if failed:
    raise SystemExit(f"missions: {failed} case(s) failed")
print(f"missions: all {len(cases) + len(menu_cases)} cases pass, and what each start notes")
