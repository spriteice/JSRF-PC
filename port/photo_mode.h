/* Photo mode: Developer > Missions > Photo mode, the world held with no HUD
 * and a camera that flies anywhere.
 *
 * While its row is ticked, as soon as the player has control -- none of the
 * game's own holds on (a scene, a fade, a talk, the pause menu) and the
 * game's camera following the player's skater for PHOTO_SETTLE_FRAMES game
 * frames -- photo mode holds the world, hides the HUD and flies the camera,
 * from the view the game's camera had. Unticked, it lets the world go, the
 * HUD comes back and the game's camera takes over again. It never starts
 * while input playback records or plays, so a recording stays the game's own
 * frames.
 *
 * The hold is the game's own: the game manager's fourth hold (its +0x4C),
 * the one the game takes for its fades and its missions' held moments, set
 * and cleared through the game's own call (sub_00012730), which keeps the
 * camera drawing. While photo mode has it, the frame's draw (sub_000131F0)
 * runs as in play, every object drawn by its usual draw, as the hold's own
 * draws leave some out. The camera's draw (sub_000A2960) builds its view
 * from its eye and look point, and makes the set of the map's areas to draw
 * from that eye (sub_0006A970); photo mode places both before it, so Far
 * buildings, Far shadows and Draw distance go from where it is. In no area
 * the set is made from the last place its eye was in an area. The view is
 * built from a look point PHOTO_LOOK_AHEAD ahead of the eye, far enough
 * that the game's view (sub_001BAF70) always takes its usual way.
 *
 * Where the map joins two of its parts, as Kibogaoka Hill's tunnel does,
 * the game has a portal: a quad and an offset (sub_0006F7B0 makes them from
 * the stage's data as it loads). Each game frame it tests each skater's
 * step against them (sub_0006F8C0), and a skater that crosses one, against
 * its face, is moved by its offset with its camera (sub_00080340), so the
 * view comes out in the other part. Photo mode tests its eye's step the same
 * way (photo_mode_frame), from the portals its hook reads from the game's
 * own list (PHOTO_GAME), so the camera goes through as the skater does.
 *
 * The HUD goes as Display > HUD > Disable HUD hides it (nv2a_hud_hold),
 * leaving that row as it is. The hooks are in recomp_manual.c; the state,
 * the flight and the [PHOTO] log are photo_mode.c's.
 *
 * The controls, read from every pad as the game would read it (the keyboard
 * merged over the first, as its layout has it), and the mouse:
 *   left stick          moves along the view: forward, back and sideways
 *   right stick, mouse  turns and tilts the view, by Input > Free camera's
 *                       Invert X and Y, Sensitivity X and Y, Deadzone,
 *                       Mouse look and Mouse sensitivity
 *   R and L triggers    straight up and down
 *   R1 held, L1 held    PHOTO_FAST times as fast, PHOTO_SLOW as fast: the
 *                       game's White and Black (the keyboard's T and R)
 *   Y                   back to where photo mode started
 * The other face buttons do nothing, so a thumb can fly and turn at once.
 * While photo mode holds the world, the game gets no presses.
 *
 * The tick is the player's for as long as the game runs, not kept in the
 * settings: Developer > Missions' picks carry it over the restart they make
 * in JSRF_START_PHOTO=1, read once as the engine starts and taken out of
 * the environment, so File > Restart, or a fresh start, has it off. */
#ifndef JSRF_PHOTO_MODE_H
#define JSRF_PHOTO_MODE_H

#include <stdint.h>
#include "xinput_xbox.h"

/* The environment variable a pick carries the tick in. */
#define PHOTO_START_VAR "JSRF_START_PHOTO"

/* Game frames the game's camera follows the player's skater, with none of
 * the game's holds on, before photo mode holds the world: half a second. */
#define PHOTO_SETTLE_FRAMES 30

/* The flight: units of the world a game frame at full tilt (about a
 * skater's run, 120 a second at the game's 60 frames a second), and R1's and
 * L1's shares of it. */
#define PHOTO_SPEED 2.0
#define PHOTO_FAST  4.0
#define PHOTO_SLOW  0.25

/* The game manager's four holds (its +0x40, +0x44, +0x48 and +0x4C), a bit
 * each; PHOTO_HOLD_OURS is the one photo mode takes. */
#define PHOTO_HOLD_PAUSE  0x1u      /* the pause menus' */
#define PHOTO_HOLD_2      0x2u
#define PHOTO_HOLD_3      0x4u
#define PHOTO_HOLD_OURS   0x8u      /* the fades', which keeps the camera drawing */

/* The look point the view is built from, this far ahead of the eye. The
 * game's view (sub_001BAF70) turns the eye to it by its heading, then tilts
 * it; with the look point less than 0.3 across from the eye on the level,
 * it tilts first and then turns, which turns the picture round about its
 * middle as the camera turns. Its own camera looks at the skater, about 38
 * ahead, so only straight up or down would be that near. At this distance
 * the look point is more than 0.3 across at every tilt photo mode allows
 * (64 x cos 89 degrees is 1.1), so the picture never turns over. */
#define PHOTO_LOOK_AHEAD 64.0
#define PHOTO_VIEW_LEVEL 0.3        /* the game's: 1 / 3.3333 (0x1E0EF0) */

/* A portal of the map's, as the game made it (sub_0006F7B0): its quad as two
 * triangles, each three corners A, B and C, crossed only against the face
 * whose normal is (B - A) x (C - B); and the offset crossing it moves a
 * skater by. */
typedef struct {
    float tri[2][3][3];
    float by[3];
} PHOTO_PORTAL;
#define PHOTO_PORTALS_MOST 64

/* What the game is doing, as photo mode's hook reads it once a game frame,
 * at the start of the game's main loop pass (sub_00013A80). */
typedef struct {
    uint32_t holds;     /* PHOTO_HOLD_ bits on */
    int playback;       /* input playback records or plays */
    const PHOTO_PORTAL *portals;    /* the map's portals, at most PHOTO_PORTALS_MOST */
    int nportals;
} PHOTO_GAME;

/* What photo_mode_frame asks of its hook. */
enum {
    PHOTO_NOTHING,
    PHOTO_ASK_HOLD,     /* the game's own call: PHOTO_HOLD_OURS on */
    PHOTO_ASK_LET_GO    /* and off; then the camera's view put back (photo_mode_game_view) */
};

/* At start, before the game runs: the tick, from PHOTO_START_VAR, taken out
 * of the environment; and the mouse's look asked of the window while photo
 * mode holds the world. */
void photo_mode_start(void);

/* The row's tick, and flipping it (on the window's thread); the game takes
 * it up at its next frame. */
int photo_mode_ticked(void);
void photo_mode_flip(void);

/* Once a game frame, at the start of the game's main loop pass, before the
 * game takes up its holds' requests: what to ask of the game. While holding,
 * the flight moves by the pads and the mouse, and through the map's
 * portals: the eye's step from where it was crossing one against its face,
 * the eye is moved by its offset, once a frame at most, as the game moves a
 * skater. */
int photo_mode_frame(const PHOTO_GAME *g);

/* Each time the follow camera works out the view of a skater the pad moves
 * (sub_000A6100), in a mode of its own but the graffiti shot: the player
 * has control, through camera cam. */
void photo_mode_followed(uint32_t cam);

/* Whether photo mode holds the world: from asking the hold until asking to
 * let it go. */
int photo_mode_holding(void);

/* The camera's draw (sub_000A2960), before the game's: when photo mode holds
 * the world and cam is its camera, the view's eye and look point are placed
 * (1), started from the game's own, as given, the first time; else 0 and
 * both are left. */
int photo_mode_place(uint32_t cam, float eye[3], float look[3]);

/* At PHOTO_ASK_LET_GO: the camera photo mode placed, and the eye and look
 * point the game's camera had when photo mode took over, to put back. 0 when
 * it placed none. */
int photo_mode_game_view(uint32_t *cam, float eye[3], float look[3]);

/* The set of the map's areas made from photo mode's eye (sub_0006A970):
 * empty, it is made again from the last point the eye was in an area,
 * returned in from (1); else that point is now the eye (0). */
int photo_mode_area(int empty, const float eye[3], float from[3]);

/* Each of the game's pad reads (port/pad.c), the state as the game is to see
 * it: emptied while photo mode holds the world. */
void photo_mode_pad(XBOX_INPUT_STATE *st);

#endif
