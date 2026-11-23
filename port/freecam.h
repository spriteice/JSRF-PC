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
/* After one of the game's own shots (camera_scenes.h), the frames the view
 * takes from the game's to where the player had it, 0.4 of a second at the
 * game's 60 frames a second (spriteice, 22 November 2026: "smoothly reset
 * to the last freecam position", then "slightly faster"). */
#define FREECAM_BACK_FRAMES 24
/* Into the graffiti shot, the frames the view took from the free camera's
 * to the game's, 0.2 of a second (spriteice, 22 November 2026, the card
 * "Graffiti in and out", then "slightly faster"), as recordings made then
 * play it (PLAYBACK_BACK_STEADY). Since, the free camera cuts into it, as
 * the game's camera does ("the transition in for the graffiti cam in free
 * cam mode has to go ... the transition out stays"). */
#define FREECAM_INTO_FRAMES 12
/* Both as recordings made before they were quickened play them
 * (port/playback.h, PLAYBACK_BACK_EASE and PLAYBACK_BACK_GLIDE): half a
 * second out, and a quarter in. */
#define FREECAM_BACK_FRAMES_BEFORE 30
#define FREECAM_INTO_FRAMES_BEFORE 15

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

/* Each camera post-update (sub_000A28B0), whatever its mode, as it ends:
 * that camera was post-updated this frame, the follow camera's
 * (freecam_orbit) with it or not. One freecam steers that is post-updated
 * without the follow camera is the game's, and freecam starts again from the
 * game's view when it follows the player once more; one that is not
 * post-updated (the game is paused) keeps the view. Here too, with the word
 * from the camera's update this frame (freecam_shot), a shot starting keeps
 * the view to ease back to, and a post-update without the follow camera or
 * a shot after one lets it go: the camera's update runs in the task
 * manager's first walk, freecam_frame later in it (the sound manager's
 * update), and the post-update in its second walk, so only here are the
 * update's word and what the post-update ran from the same frame. */
void freecam_ran(uint32_t cam);

/* Each camera update of a camera a skater the pad moves (sub_000A5070), on
 * the game's thread: whether one of the game's own shots (camera_scenes.h)
 * plays on it, the level exit's not one, for its post-update this frame
 * (freecam_ran). The free camera, steering cam as a shot starts, keeps how
 * far its view is from the game's own, and once the follow camera runs cam
 * again, in whatever mode (on foot, on a rail), eases back there over
 * FREECAM_BACK_FRAMES, gliding there from the shot's view (freecam_view),
 * the stick or the mouse taking over its angle at once; another mode of the
 * game's in between, a scene, or the game putting the camera back behind
 * the player, starts it from the game's view, as before. A recording made
 * before plays as before (playback_freecam_back). */
void freecam_shot(uint32_t cam, int playing);

/* Each camera post-update (sub_000A28B0), as it ends, after freecam_ran:
 * the view the frame is built from, eye and what it looks at, for a glide
 * to start from (freecam_view). A glide its follow camera's hook did not
 * step this frame ends. */
void freecam_shown(uint32_t cam, const float eye[3], const float tgt[3]);

/* The follow camera's hook (sub_000A6100), its eye and what it looks at in
 * place, after walls: shot 0 as the free camera has placed them
 * (freecam_orbit), and shot 1 as the game has framed the graffiti shot
 * (camscene_shot). The view glides, each in its position, angle and
 * distance, smoothly, out of one of the game's shots to the free camera's
 * from the shot's last view, over FREECAM_BACK_FRAMES (freecam_shot), the
 * stick or the mouse steering where it glides to; into the graffiti shot it
 * cuts, as the game's camera does, but as recordings that glided in play
 * (from the free camera's last view, over FREECAM_INTO_FRAMES or
 * FREECAM_INTO_FRAMES_BEFORE). Returns whether it moved them. Only while
 * the free camera is on, and as a recording made since plays
 * (playback_freecam_back). */
int freecam_view(uint32_t cam, int shot, float eye[3], float tgt[3]);

/* Each camera post-update (sub_000A28B0), as it ends, before
 * freecam_shown: 1, with the view cam's glide was headed to this frame (the
 * eye and what it looks at as freecam_view was given them: the graffiti
 * shot as the game frames it, as recordings that glided in play, or the
 * free camera's own view), while it glides; else 0. The heading the controls follow is taken from that view,
 * not the one the glide shows, so that to the game the glide is a cut, as
 * its own camera's is: as its camera cuts to a shot and back, the game
 * keeps the skater going the way it went while the stick stays where it
 * was (CAM_HOLDS in recomp_manual.c), and lets that go once the camera
 * faces within 20 degrees of where it did, which a glide from where the view
 * was did on its first frame (spriteice, 22 November 2026: "can the skater
 * continue in the same direction the stick is pushing?"). Not as a
 * recording made before plays (playback_freecam_back). */
int freecam_ahead(uint32_t cam, float eye[3], float tgt[3]);

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

/* Whether freecam is on: the camera update leaves out the look at whoever
 * the player talks to while it is. (The graffiti camera is Input >
 * Behaviour > Camera's, camera_scenes.h, with freecam or without it.) */
int freecam_on(void);

/* The player's switch and tuning, as the settings file, F8, the pad's button
 * and the menu's rows last made them, whatever a recording plays: photo
 * mode's look goes by them (port/photo_mode.h). */
void freecam_settings(FREECAM_SETTINGS *out);

/* Auto centre's level by the name the menu gives it ("Off", "Mild",
 * "Medium", "High", "Aggressive"), for the log: "?" outside 0 to
 * FREECAM_CENTRE_LEVELS - 1. */
const char *freecam_centre_name(int level);

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
