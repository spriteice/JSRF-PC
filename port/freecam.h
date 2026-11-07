/* The free camera: the right stick turns the follow camera around the player.
 *
 * The game still chooses what the camera looks at, how far away it is and
 * where a wall stops it; this only chooses the direction it looks from, and
 * camera_walls.h keeps its view out of walls there. The hooks are in
 * recomp_manual.c: sub_000A6100 (the follow camera), sub_000A5070
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

/* What freecam_orbit did. */
enum {
    FREECAM_OFF,        /* nothing: the game's eye stands */
    FREECAM_PLACED,     /* it placed the eye, going on from last frame */
    FREECAM_STARTED     /* it placed the eye, started again from the game's view */
};

/* The follow camera's eye for this frame. tgt is what it looks at and seye
 * the game's smoothed eye: the distance between them is kept. reset: the game
 * put that eye back behind the player this frame, and freecam starts again
 * from it. FREECAM_PLACED or FREECAM_STARTED, and eye, when freecam places
 * it: started again, what the eye kept after walls (camera_walls.h) starts
 * again too. FREECAM_OFF when it is off. mode is only for the log. */
int freecam_orbit(uint32_t cam, uint32_t mode, const float tgt[3], const float seye[3],
                  int reset, float eye[3]);

/* Whether freecam is on: the camera update leaves out the graffiti camera
 * and the look at whoever the player talks to while it is. */
int freecam_on(void);

#endif
