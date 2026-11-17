/* Developer > Missions: the game started at a story mission (missions.c).
 *
 * JSRF_START_MISSION=CCNN starts the game at mission NN of chapter CC, where
 * the title would be: the game's own New Game is chosen for the player, and
 * before the game flow goes on to the mission, the story up to it is written
 * into the running game -- the chapter, the mission, player 1's entrance, the
 * Chapter, Global and bank 3 flags, the unlocked skaters, and the graffiti
 * souls and Noise Tanks a gate on the way counts. JSRF_START_STATE gives that
 * story, as one line ("e=7 C=0,2 G=1 R=384,450 S=1,2,6 souls=25 tanks=200";
 * what it leaves out stays as New Game has it; story=0 marks a mission no exit
 * in the game's scripts leads to, started with its chapter's opening flags).
 * Without it, the mission's line in start-states.txt beside the settings file
 * is taken, when a test build has put one there; without either, the
 * mission starts with New Game's progress. JSRF_START_SKATER gives the
 * character player 1 starts as: any of the game's MISSIONS_CHARACTERS, by
 * the game's own number (MISSIONS_SKATER_YOYO is Yoyo), unlocked in the
 * progress written, as the game's own unlock does, so the mission's cast
 * keeps them; without it, New Game's Yoyo. Nothing is skipped but the
 * title, and the intro, whatever Display > Skips > Skip intro says.
 *
 * JSRF_START_LOAD=1 starts the game at the title's Load Game instead: the
 * game's own Load Game screen, with the saves of the runs started at a
 * mission (MISSIONS_SAVES), and nothing written.
 *
 * All four are read once, as the engine starts, and taken out of the
 * environment, so File > Restart afterwards is a normal start. A value the
 * engine can't use is refused with a [MISSION] line, and the game starts
 * normally. Developer > Missions (missions_menu.c) sets them and restarts.
 *
 * A run started at a mission keeps its saves apart: E:\UDATA and E:\TDATA go
 * to the folder "mission-saves" in the engine's save folder, so the player's
 * own save is never opened.
 *
 * The game flow's hook is in recomp_manual.c: sub_0007BDD0, its update. */
#ifndef JSRF_MISSIONS_H
#define JSRF_MISSIONS_H

#include <stdint.h>

/* The save folder of a run started at a mission, in the engine's save folder. */
#define MISSIONS_SAVES "mission-saves"

/* Each mission's start line, "CCNN" then JSRF_START_STATE's fields, one a
 * line, in a file of this name beside the settings file: for the test builds
 * only, never in the repository or a package (it is made from the game's
 * files). */
#define MISSIONS_LINES "start-states.txt"

/* The game's characters, numbered 0 to MISSIONS_CHARACTERS - 1: game
 * variable 0xD's skater, and the bits of the progress's unlocked ones. Its
 * own unlock (sub_00039CB0) and its table of them (0x21C450) take every
 * number from 0 to 23. */
#define MISSIONS_CHARACTERS  24
#define MISSIONS_SKATER_YOYO 6      /* New Game's skater */

/* At start, before the game runs: reads JSRF_START_MISSION,
 * JSRF_START_STATE (or start-states.txt's line), JSRF_START_SKATER and
 * JSRF_START_LOAD, and takes them out of the environment. */
void missions_start(void);

/* Whether start-states.txt's line for mission CCNN marks it off the story's
 * way (story=0). 0 with no file, no line, or a line that can't be read. */
int missions_off_story(int mission);

/* Once the path layer is up (xbox_path_init), with the engine's save folder:
 * refuses the start if the mission's file isn't in the game's files, and
 * otherwise puts the game's saves in the save folder's MISSIONS_SAVES, as it
 * does for a start at Load Game. */
void missions_paths(const char *save_dir);

/* Whether the game is being started at a mission, or at Load Game: from
 * missions_start, until the mission is started (or Load Game chosen) or the
 * start refused. */
int missions_starting(void);

/* State 0xA's own calls that the start keeps, in the game's order: made by
 * the hook, which can call into the game. */
typedef void (*missions_title_fn)(void);

/* The game flow's update, before the game's own, with the flow object at
 * `flow` in guest memory `mem` (`size` bytes) and the game's time `t` in
 * seconds for the log. In place of the title it chooses New Game (or Load
 * Game, for JSRF_START_LOAD, and then nothing more), calling `title` for
 * the title's own calls; then it writes the progress before the
 * flow asks for the graffiti's resource (state 0x11), and the chapter, the
 * mission, the entrance and the skater before it makes the mission's scene
 * (0x1C). */
void missions_flow(uint8_t *mem, uint32_t size, uint32_t flow, missions_title_fn title, double t);

/* Developer > Missions' rows (missions_menu.c), once the game's program is
 * in guest memory `mem` (`size` bytes) and the path layer is up: the story's
 * missions, read from the game's own mission files, by level, with the
 * game's own names for the levels and the characters, and Load game. A
 * pick sets JSRF_START_MISSION and JSRF_START_SKATER, or JSRF_START_LOAD,
 * and restarts. Nothing without the in-game menu. */
void missions_menu_start(const uint8_t *mem, uint32_t size);

#endif
