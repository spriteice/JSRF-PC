/* The mark that a recording plays: a white disc with a play sign at the
 * middle of the HUD's left edge, straight above the graffiti soul
 * indicator's badge and as far from the edge (soul_indicator.h), while a
 * recording plays on route. As the play ends it turns green (it finished on
 * route), red (off route, or stopped by its steps) or blue (the player took
 * the game back), holds that colour for two seconds, and fades out while
 * growing, as the soul badge goes out (spriteice's design, 23 November
 * 2026, on the mock-up: "move it to the left edge", "It should fade blue
 * when the player takes over").
 *
 * It comes in, fading while shrinking to its size, the first time it is
 * drawn after the play starts, so a play that starts with a load shows it
 * coming in once the HUD is up. The draw is the hooks' (port/recomp_manual.c,
 * after the soul badge's, in the gauge's override, as a HUD piece of its own,
 * WIDE_HUD_MARK, so widescreen takes it to the left edge, Display > HUD scale
 * sizes it, and Disable HUD hides it): this is the state from frame to frame
 * and the quads, plain C, so tests/playback drives it. Its time is the
 * host's, in seconds: it changes nothing the game reads. */
#ifndef JSRF_PLAYBACK_MARK_H
#define JSRF_PLAYBACK_MARK_H

#include <stdint.h>

/* Where it goes, in the HUD's 640 x 480: its left edge as far from the
 * screen's as the soul badge's (SOUL_LEFT), its centre half way down, its
 * size the badge's. */
#define PB_MARK_LEFT      53.0f
#define PB_MARK_MIDDLE_Y  240.0f
#define PB_MARK_SIZE      40.0f

/* The way it moves, in seconds and sizes (1 is its size), as the soul
 * badge's. */
#define PB_MARK_IN_TIME   0.25      /* in: a fade, shrinking from PB_MARK_IN_FROM */
#define PB_MARK_IN_FROM   1.15
#define PB_MARK_TINT_TIME 0.15      /* ended: to its colour, */
#define PB_MARK_HOLD      2.0       /* held, */
#define PB_MARK_OUT_TIME  0.25      /* then out: a fade, growing to PB_MARK_OUT_TO */
#define PB_MARK_OUT_TO    1.15

/* Its colours, RGB. */
#define PB_MARK_WHITE     0x00FFFFFFu
#define PB_MARK_GREEN     0x002FD36Bu   /* finished on route */
#define PB_MARK_RED       0x00FF3B3Bu   /* off route, or stopped */
#define PB_MARK_BLUE      0x003B8BFFu   /* the player took the game back */
#define PB_MARK_SIGN      0x001B1E26u   /* the play sign on the disc */

/* The disc, in PB_MARK_BANDS bands, each a quad (two of its sides a chord
 * apart), and the play sign, a quad with two corners at its tip. */
#define PB_MARK_BANDS     12
#define PB_MARK_QUADS     (PB_MARK_BANDS + 1)

enum { PB_MARK_HIDDEN, PB_MARK_DUE, PB_MARK_IN, PB_MARK_ON, PB_MARK_ENDED, PB_MARK_OUT };
typedef struct {
    int phase;
    double at;          /* when the phase began */
    uint32_t colour;    /* ended: the colour it turns, RGB */
} PbMark;

/* A play starting: due to come in at its first draw. */
void playback_mark_play(PbMark *m);

/* The play over, at time now: how, one of playback.h's PLAYBACK_MARK_. Not
 * yet in, it goes straight to its colour. Nothing while hidden. */
void playback_mark_end(PbMark *m, int how, double now);

/* Each frame it may draw, at time now: moved on (in at its first draw,
 * out after its hold, hidden after its fade), and 1 with the quads' place,
 * fade and colour while it shows, else 0. */
typedef struct {
    float cx, cy, r;    /* the disc's centre and radius */
    float alpha;        /* the fade: 0 to 1 */
    uint32_t disc;      /* the disc's diffuse: its colour at the fade's alpha */
    uint32_t sign;      /* the play sign's */
} PbMarkShape;
int playback_mark_frame(PbMark *m, double now, PbMarkShape *s);

/* The quads as the HUD's quad batches take them (their add, mode 0, as
 * soul_indicator_vertices): PB_MARK_QUADS of four vertices, (x0, y0),
 * (x1, y0), (x0, y1), (x1, y1) of each, in guest memory mem at at, each
 * vertex SOUL_VERTEX bytes (soul_indicator.h): the disc's bands from the
 * top, then the play sign. With no texture, the diffuse is the colour. */
#define PB_MARK_BYTES (PB_MARK_QUADS * 4u * 0x18u)
void playback_mark_vertices(uint8_t *mem, uint32_t at, const PbMarkShape *s);

/* The hooks' side, in port/playback.c: the mark of this run's play, which
 * playback starts and ends, moved on to time now as playback_mark_frame,
 * under playback's lock. */
int playback_mark(double now, PbMarkShape *s);

#endif
