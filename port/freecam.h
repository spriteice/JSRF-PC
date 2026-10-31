/* The free camera: the right stick turns the follow camera around the player.
 *
 * The game still chooses what the camera looks at, how far away it is and
 * where a wall stops it; this only chooses the direction it looks from. The
 * hooks are in recomp_manual.c: sub_000A6100 (the follow camera), sub_000A5070
 * (the camera update, for graffiti and talking), sub_000A28B0 (the camera's
 * post-update), sub_001C3E14 (XInputGetState) and sub_00116E30 (once a game
 * frame). */
#ifndef JSRF_FREECAM_H
#define JSRF_FREECAM_H

#include <stdint.h>

/* At start, before the game runs: reads the settings, adds the in-game menu's
 * Controller rows and binds F8 and a pad's R3, which turn freecam on or off. */
void freecam_start(void);

/* Once a game frame: freecam steers the camera, and takes the stick, while it
 * is on and the follow camera ran since the last frame. */
void freecam_frame(void);

/* Each camera post-update (sub_000A28B0), whatever its mode: that camera was
 * post-updated this frame. One freecam steers that is post-updated without
 * the follow camera is the game's, and freecam starts again from the game's
 * view when it follows the player once more; one that is not post-updated
 * (the game is paused) keeps the view. */
void freecam_ran(uint32_t cam);

/* Each pad read: notes where the right stick is and, while freecam steers,
 * zeroes it for the game. NULLs: the pad on that port is gone. */
void freecam_pad(unsigned port, short *rx, short *ry);

/* The follow camera's eye for this frame. tgt is what it looks at and seye
 * the game's smoothed eye: the distance between them is kept. reset: the game
 * put that eye back behind the player this frame, and freecam starts again
 * from it. 1 and eye when freecam places it; 0 when it is off, and the game's
 * eye stands. mode is only for the log. */
int freecam_orbit(uint32_t cam, uint32_t mode, const float tgt[3], const float seye[3],
                  int reset, float eye[3]);

/* A line test against the world, as the follow camera's wall test makes it:
 * how much of the line from a to b is open, 0 to 1, up to the nearest hit.
 * ctx is the caller's. */
typedef double (*freecam_line_fn)(void *ctx, const double a[3], const double b[3]);

/* What set the share freecam_clear returns, for the log. */
enum {
    FREECAM_WALL_CLEAR,     /* nothing: all of it */
    FREECAM_WALL_EYE,       /* the eye's own line, as the game's */
    FREECAM_WALL_CORNER,    /* a corner of the view, cleared by coming in */
    FREECAM_WALL_NO_ROOM,   /* corners that neither clears: the eye's line alone */
    FREECAM_WALL_ACROSS,    /* a corner of the view, cleared by moving across */
    FREECAM_WALL_NEAREST    /* the eye's line meets a wall right by the target */
};

/* How far from the target tgt the eye freecam placed can be, as a share of
 * its distance, up to most (1 for all of it), and push, how far it moves
 * across there: its own line clear, and no surface across the view where
 * it starts, ahead of it (ahead, and half_w and half_h either side there;
 * 0s test the eye's line alone). When one cuts across it, the eye moves
 * across, away from it, or else comes in until lines from tgt to the
 * view's corners are clear, if either clears them. Never nearer tgt than
 * where the view starts in front of it. by says what set it. */
double freecam_clear(const float tgt[3], const float eye[3], double most, double ahead,
                     double half_w, double half_h, freecam_line_fn line, void *ctx,
                     double push[3], int *by);

/* After freecam_clear, each frame: open is the share it gave. Returns the
 * share to put the eye at: in at once when open is less than last frame's,
 * so the view never goes into a wall; then, once nothing has brought it in
 * for a moment, back out by degrees, so a wall going by, or one that comes
 * and goes, does not make the view jump or shake. Short of open, the view
 * there is not yet tested: freecam_clear up to it says, with how far across
 * the eye moves there, and when that is less, this again with what it gave
 * brings the eye in to it. */
double freecam_wall(double open);

/* After that, each frame: how far across to move the eye (out), for the
 * eye at share s, where freecam_clear moved it need. To need at once when
 * last frame's move, where the eye is now, no longer keeps the view clear,
 * so the view never goes into a wall; else last frame's for a moment, then
 * toward need by degrees, as freecam_wall goes back out, so the view does
 * not jump back across once a wall it moved away from ends. */
void freecam_across(const float tgt[3], const float eye[3], double s, const double need[3],
                    double ahead, double half_w, double half_h, freecam_line_fn line, void *ctx,
                    double out[3]);

/* Whether freecam is on: the camera update leaves out the graffiti camera
 * and the look at whoever the player talks to while it is. */
int freecam_on(void);

#endif
