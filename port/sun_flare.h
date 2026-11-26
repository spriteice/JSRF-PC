/* The sun's glare hidden behind walls (known bug 86): an experiment, and its
 * log (sun_flare.c).
 *
 * The game hides the sun's glare, and its lens ghosts, with a line test on
 * the CPU: from the camera's eye toward the sun, against the level's
 * collision, cut into pieces of about 100 units. It tests each piece from
 * the smallest corner of the piece's box to its largest, which is the piece
 * itself only when the sun is ahead on all three axes; otherwise it tests the
 * piece mirrored about its middle, which can run past a slanted wall or a
 * slope the piece crosses, and the glare shows through it.
 *
 * Developer > Experimental's Sun flare fix (JSRF_SUN_FIX=1, off by default)
 * tests each piece itself, from the corner where the line enters its box to
 * the one where it leaves, with the game's own line test. A line is one
 * point of the sun, so it flips between blocked and clear for a frame or two
 * as it passes an edge, and each flip starts the game's fade: the fix counts
 * its last SUN_FLARE_WINDOW results, and hides the glare only once
 * SUN_FLARE_HIDE of them are blocked, and shows it again once SUN_FLARE_SHOW
 * of them are clear, so only a solid surface the sun stays behind hides it.
 * And it hides the sprite the game draws in the glare's place while the sun is
 * blocked (spriteice at 8:57pm on 2026-11-25: "the outline is still there").
 * Developer > Logging's JSRF_SUN_LOG logs both lines' results side by side.
 * The hooks are in recomp_manual.c: sub_00044E80, the flare's test, and
 * sub_00044F00, its two sprites' colours. */
#ifndef JSRF_SUN_FLARE_H
#define JSRF_SUN_FLARE_H

#include <stdint.h>

/* The most pieces the game cuts the line into (sub_00044C90). */
#define SUN_FLARE_PIECES 30

/* The switches, read once: the fix (JSRF_SUN_FIX) and the log (JSRF_SUN_LOG).
 * Each is on when its variable is set to anything but 0. */
int sun_flare_fix_on(void);
int sun_flare_log_on(void);

/* How many of the real line's last results the fix counts, a second at 60
 * frames a second; how many of them must be blocked before the game is given
 * blocked, nearly all; and how many must be clear before it is given clear
 * again, a quarter. So the glare hides only behind a surface the line stays
 * blocked by, and not behind an edge, a pole, a seam or a gap it passes, and
 * comes back soon after (spriteice at 8:37am on 2026-11-26: "can we please
 * make it far less sensitive to changes so it only disappears when its
 * behind the most solid surfaces"; at 9:41pm on 2026-11-25: "it doesn't need
 * to be the most accurate sun ever, just not clearly showing on solid walls
 * like the screenshot without constantly flicking in and out"). */
#define SUN_FLARE_WINDOW 60
#define SUN_FLARE_HIDE   57
#define SUN_FLARE_SHOW   15

/* The real line's result, steadied, for one flare. */
typedef struct {
    uint32_t flare;     /* the flare it is for; 0 for none yet */
    int      shown;     /* the result the game is given: 1 blocked */
    uint64_t recent;    /* its last SUN_FLARE_WINDOW results, the latest in
                         * bit 0: 1 blocked */
} SunFlareHold;

/* The result to give the game for this test of the flare: clear until
 * SUN_FLARE_HIDE of the last SUN_FLARE_WINDOW results are blocked, then
 * blocked until SUN_FLARE_SHOW of them are clear. A new flare starts from its
 * first result, as if it had had it all along. */
int sun_flare_steady(SunFlareHold *hold, uint32_t flare, int blocked);

/* A piece's ends, from its box (lo, the smallest corner, and hi, the largest)
 * and the direction toward the sun: a where the line enters the box, b where
 * it leaves. */
void sun_flare_ends(const float lo[3], const float hi[3], const float dir[3],
                    float a[3], float b[3]);

/* What one test found, for the log. game and real are the first piece the
 * game's line and the real line meet, or -1 for none. */
typedef struct {
    double   t;         /* the game's clock, in seconds */
    uint32_t flare;     /* the flare: each is logged on its own */
    int      state;     /* the flare's: 1 in sight, 2 blocked, 4 and 3 between, 0 off */
    float    eye[3];    /* where the line starts */
    float    dir[3];    /* from the eye to the sun's point */
    int      pieces;    /* the line's pieces */
    int      tested;    /* those with surfaces in their box, which are tested */
    int      game;      /* the first piece the game's line meets, or -1 */
    int      real;      /* the first piece the real line meets, or -1 */
    int      fix;       /* the fix on: the real line's result, steadied, is used */
    int      shown;     /* what the game is given: 1 blocked */
} SunFlareTest;

/* Logs a test as a [SUNFLARE] line when its results or its flare change, or
 * every 2 seconds while they don't, and no more than 4 times a second.
 * Returns 1 when it wrote a line. */
int sun_flare_note(const SunFlareTest *test);

#endif
