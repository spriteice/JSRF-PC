/* The free camera: the right stick, or the mouse, turns the follow camera
 * around the player.
 *
 * The game still chooses what the camera looks at, how far away it is and
 * where a wall stops it; this only chooses the direction it looks from, and
 * camera_walls.h keeps its view out of walls there, with the eye no nearer
 * the player than the game draws them whole while No skater fade is on.
 * While its eye is in none of the map's areas, the areas drawn are the game's
 * own eye's (sub_0006A970), and your skater is drawn while in none itself
 * (sub_0006AC10). The hooks are in
 * recomp_manual.c: sub_000A6100 (the follow camera), sub_000A5070
 * (the camera update, for graffiti and talking), sub_000A28B0 (the camera's
 * post-update), sub_001C3E14 (XInputGetState) and sub_00116E30 (once a game
 * frame). Photo mode (photo_mode.h) turns its own camera by the player's
 * look settings here (freecam_settings, freecam_deadzone, freecam_turn), and
 * asks the window for the mouse beside the free camera. */
#ifndef JSRF_FREECAM_H
#define JSRF_FREECAM_H

#include <stdint.h>

/* The steps of sensitivity the settings file takes (and the in-game menu
 * offers), the stick's and the mouse's, and auto centre's levels: off, mild,
 * medium, high, aggressive. */
#define FREECAM_SENS_MIN      1.0
#define FREECAM_SENS_MAX      10.0
#define FREECAM_CENTRE_LEVELS 5

/* Freecam's switch and tuning, as the settings file names them (freecam.c):
 * the player's, or a recording's while one plays (port/playback.c). */
typedef struct {
    int on;                     /* freecam */
    int invert_x, invert_y;     /* 0 or 1 */
    int centre;                 /* autocentre: 0 (off) to FREECAM_CENTRE_LEVELS - 1 */
    double sens_x, sens_y;      /* sensitivity_x and _y: FREECAM_SENS_MIN to _MAX */
    double deadzone;            /* 0 to 1 */
    int mouse;                  /* mouse: Mouse look, 0 or 1 */
    double mouse_sens;          /* mouse_sensitivity: FREECAM_SENS_MIN to _MAX */
} FREECAM_SETTINGS;

/* At start, before the game runs: reads the settings, adds the in-game menu's
 * Free camera submenu, at the top of Input, with its rows, binds F8 and the
 * pad's button for it (Input > Controller layout's Toggle free camera, R3 by
 * default), which turn freecam on or off, and asks the window for the
 * mouse's moves while freecam and its Mouse look are on. */
void freecam_start(void);

/* Once a game frame: freecam takes up what the player changed since the last
 * frame (F8, the pad's button and the menu's rows ask on the window's
 * thread), recorded, or a recording's in its place while one plays
 * (port/playback.c); then it steers the camera, and takes the stick, while
 * it is on and the follow camera ran since the last frame. It takes the
 * mouse's move since the last frame too, recorded, or the recording's in its
 * place, for the follow camera to turn by this frame; a move in a frame it
 * doesn't steer is dropped, so the view doesn't jump when it steers again. */
void freecam_frame(void);

/* Each camera post-update (sub_000A28B0), whatever its mode: that camera was
 * post-updated this frame. One freecam steers that is post-updated without
 * the follow camera is the game's, and freecam starts again from the game's
 * view when it follows the player once more; one that is not post-updated
 * (the game is paused) keeps the view. */
void freecam_ran(uint32_t cam);

/* Each pad read, after input playback has recorded the pad or put the
 * recording's in its place (port/pad.c): notes where the right stick is and,
 * while freecam steers, zeroes it for the game. NULLs: the pad on that port is
 * gone. */
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

/* The player's switch and tuning, as the settings file, F8, the pad's button
 * and the menu's rows last made them, whatever a recording plays: photo
 * mode's look goes by them (port/photo_mode.h). */
void freecam_settings(FREECAM_SETTINGS *out);

/* A stick, -1..1 on each axis, past c's radial deadzone, rescaled so that
 * its edge is 0 and full tilt is 1. */
void freecam_deadzone(const FREECAM_SETTINGS *c, double *x, double *y);

/* The turn this frame by c's tuning, in radians: right, and the view's tilt
 * up, from a stick past its deadzone (x, y: per game frame) and the mouse's
 * move (mx, my: its counts, right and up), each by its sensitivity, then
 * inverted as c asks. */
void freecam_turn(const FREECAM_SETTINGS *c, double x, double y, double mx, double my,
                  double *right, double *up);

#endif
