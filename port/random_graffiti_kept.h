/* Random graffiti's kept tags: the design each of the player's tags was
 * sprayed with, kept until the tag is gone (port/random_graffiti.h), so an
 * area's next draw shows it again.
 *
 * A tag is kept under the key the game's own save keeps a spot's tags under
 * (sub_0003A340): the stage's number in the save, 1 to 15, and the spot's
 * number on its stage, under 80. So a spot is the same one in every run and
 * every area's load, whatever its id is this time. A table of all of them,
 * looked up by the key, nothing searched: the play's, the tags of the
 * progress object the game is playing.
 *
 * The game's save holds three slots of progress, and the kept tags go with
 * them, a table a slot (spriteice, 27 November 2026: "per the games own
 * saved slot, so there should be 3 per save"), in kept-graffiti.txt in the
 * save's own folder, so that every copy of the save takes its tags with it
 * (spriteice, the same day, on keeping the file there: "perfect"). The game
 * loading a slot makes its table the play's (rg_kept_load), and the game
 * saving to a slot makes the play's its table (rg_kept_save): the file is
 * read, the slot's lines put in place of its old ones, and written whole
 * through a file of its own beside it, renamed over it. Nothing else writes
 * it, so it always has what the save has. Without a file (no save yet, or a
 * headless build) the table is in memory only.
 *
 * The file is text, a first line naming it, then a line a kept tag:
 *
 *     jsrf-pc kept graffiti 2
 *     <slot> <stage> <spot> <parts> <size> <design>
 *
 * slot being the save's, 0 to 2, and a design g and one of the game's
 * designs (size * 28 + n), or o and the slot of one of the player's own (the
 * garage's first is slot 0). A line that can't be read is left out, and
 * counted. Paths are UTF-8, opened as such on Windows too. Plain C, nothing
 * of the game's: a test links it (tests/random_graffiti). */
#ifndef JSRF_RANDOM_GRAFFITI_KEPT_H
#define JSRF_RANDOM_GRAFFITI_KEPT_H

#include <stdint.h>

#define RG_KEPT_SLOTS   3u      /* the save's slots of progress */
#define RG_KEPT_STAGES  16u     /* the save's stages: the game's table of them gives 1 to 15 */
#define RG_KEPT_SPOTS   80u     /* a stage's spots in the save (sub_0003A340 takes numbers under 0x50) */
#define RG_KEPT_FILE    "kept-graffiti.txt"
#define RG_KEPT_NONE    0xFFu   /* a key with no tag kept */

typedef struct {
    uint8_t size;               /* 0 SS, 1 S, 2 M, 3 L, 4 XL; RG_KEPT_NONE: nothing kept */
    uint8_t parts;              /* the spot's parts, for the check against the save */
    int16_t design;             /* the game's design, or RG_OWN + the slot of one of the player's own */
} RG_KEPT;

/* The file the slots' tables are in from now on: NULL or "" for none. */
void rg_kept_file(const char *path);

/* A slot's table made the play's, from the file: how many tags it had, or
 * -1 when there is no file or it isn't this one (and the play has none).
 * *bad, when set, is how many of the file's lines couldn't be read. */
int rg_kept_load(uint32_t slot, int *bad);

/* The play's table made a slot's, in the file, the others' lines as they
 * were: 1 when it was written, -1 when there is no file to write or it
 * couldn't be. *bad as rg_kept_load's, for the lines left out. */
int rg_kept_save(uint32_t slot, int *bad);

/* The tag kept for a key, or NULL. */
const RG_KEPT *rg_kept_get(uint32_t stage, uint32_t spot);

/* A tag kept for a key, or ended: nothing for a key out of the table. */
void rg_kept_set(uint32_t stage, uint32_t spot, uint32_t size, uint32_t parts, int design);
void rg_kept_end(uint32_t stage, uint32_t spot);

/* Every tag of the play's ended. */
void rg_kept_clear(void);

/* How many tags are kept, on one stage, or on all for RG_KEPT_STAGES. */
uint32_t rg_kept_count(uint32_t stage);

#endif
