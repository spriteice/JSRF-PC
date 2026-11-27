/*
 * The story walk: Developer > Missions' start states, worked out from the
 * game's own mission scripts -- see mission_walk.c.
 *
 * Given the game's mission files (D:\Media\Mission\mssnCCNN.bin), it walks
 * the story from New Game, chapter by chapter, to the start of each story
 * mission, and gives start-states.txt's text: one line a mission, with the
 * flags, skaters and counts the story has on arriving there, which
 * port/missions.c reads. A mission no exit leads to gets its chapter's
 * opening, marked story=0.
 *
 * Nothing about the story is written here: every flag, skater and count
 * comes from the files. The files, and the text made from them, are the
 * player's own and stay on their PC.
 */
#ifndef JSRF_MISSION_WALK_H
#define JSRF_MISSION_WALK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JSRF_WALK_FILE      "start-states.txt"
#define JSRF_WALK_FOLDER    "Media/Mission"         /* in the game's folder */
#define JSRF_WALK_FILE_MAX  (1u << 20)              /* the largest mission file read */
#define JSRF_WALK_MISSIONS  10000                   /* CCNN: 0000 to 9999 */

/* One mission file: its number (CCNN) and its bytes, which the caller
 * keeps for the length of the call. */
typedef struct jsrf_mission_file {
    int            id;
    const uint8_t *data;
    size_t         size;
} jsrf_mission_file;

typedef struct jsrf_walk {
    char *text;          /* start-states.txt, malloc'ed; NULL when it failed */
    size_t len;
    int   story;         /* the story's missions (chapters 1 to 9) */
    int   reached;       /* of them, the ones the walk reaches from New Game */
    int   marked;        /* the rest: their chapter's opening, story=0 */
    char  why[400];      /* when it failed: why */
} jsrf_walk;

/* CCNN from a mission file's name, "mssnCCNN.bin" in any case; -1 for any
 * other name. */
int jsrf_mission_id(const char *name);

/* The walk over n mission files, in any order. 0 with the text in out, or
 * -1 with why: a file that isn't laid out as a mission, two files for one
 * mission, no 0101 (New Game's), or a chapter whose clear isn't found. */
int jsrf_walk_states(const jsrf_mission_file *files, int n, jsrf_walk *out);

/* The mission files in the game's folder (its Media/Mission, any case),
 * read and walked. 0, or -1 with why. */
int jsrf_walk_game(const char *game_dir, jsrf_walk *out);

void jsrf_walk_free(jsrf_walk *w);

/* ---- start-states.txt in the settings file's folder ---------------------------------
 *
 * dir: the folder the settings file is in, where the engine reads
 * start-states.txt (port/missions.c). backups: where a dated folder is made
 * for the one there before (<backups>/<date and time>/start-states.txt), and
 * backup gets that folder ("" when there was none to back up). Nothing is
 * changed when the backup can't be made. */

/* text written as start-states.txt: first beside it, then the one there now
 * backed up, then the new one put in its place. 0, or -1 with why. */
int jsrf_states_write(const char *dir, const char *backups, const char *text, size_t len,
                      char *backup, size_t bn, char *why, size_t wn);

/* start-states.txt removed, after the backup. 0 (also when there was none),
 * or -1 with why. */
int jsrf_states_remove(const char *dir, const char *backups, char *backup, size_t bn, char *why, size_t wn);

/* 1 when dir has a start-states.txt that is text, byte for byte. */
int jsrf_states_same(const char *dir, const char *text, size_t len);

#ifdef __cplusplus
}
#endif

#endif
