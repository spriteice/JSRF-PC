/* The free camera: the right stick turns the follow camera around the player.
 *
 * The game still chooses what the camera looks at, how far away it is and
 * where a wall stops it; this only chooses the direction it looks from. The
 * hooks are in recomp_manual.c: sub_000A6100 (the follow camera), sub_000A5070
 * (the camera update, for graffiti), sub_001C3E14 (XInputGetState) and
 * sub_00116E30 (once a game frame). */
#ifndef JSRF_FREECAM_H
#define JSRF_FREECAM_H

#include <stdint.h>

/* Once a game frame: freecam steers the camera, and takes the stick, while it
 * is on and the follow camera ran since the last frame. */
void freecam_frame(void);

/* Each pad read: notes where the right stick is and, while freecam steers,
 * zeroes it for the game. NULLs: the pad on that port is gone. */
void freecam_pad(unsigned port, short *rx, short *ry);

/* The follow camera's eye for this frame. tgt is what it looks at, seye the
 * game's smoothed eye (the distance between them is kept) and raise what the
 * game adds to the eye's height. reset: the game put that eye back behind
 * the player this frame, and freecam starts again from it. 1 and eye when
 * freecam places it; 0 when it is off, and the game's eye stands. mode is
 * only for the log. */
int freecam_orbit(uint32_t cam, uint32_t mode, const float tgt[3], const float seye[3],
                  float raise, int reset, float eye[3]);

/* Whether freecam is on: the camera update leaves out the graffiti camera
 * while it is. */
int freecam_on(void);

/* F8: freecam on or off. Returns the new state, which the caller keeps in the
 * settings file as freecam=. */
int jsrf_freecam_toggle(void);

#endif
