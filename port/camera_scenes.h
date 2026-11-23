/* The game's own camera shots (camera_scenes.c).
 *
 * Most of the time the game's camera follows the player, and the free
 * camera (freecam.h) and the view kept out of walls (camera_walls.h) act on
 * that follow camera. Some shots the game frames itself, in a camera mode of
 * their own (the camera's +0x208), and those are the game's to place: this
 * names them. Three are a tick each, in Input > Behaviour > Camera, after
 * Default cam fix and No skater fade (camscene_start): ticked, a shot plays
 * as the game frames it, whatever the free camera, Default cam fix and No
 * skater fade; unticked, the camera update never switches to it, and the
 * camera follows the player as anywhere else, the free camera and Default
 * cam fix with it. The level exit's shot is no tick's: it plays as the game
 * has it, always. The hooks are in recomp_manual.c: sub_000A5070, the
 * camera update, and sub_000A6100, the follow camera's post-update. */
#ifndef JSRF_CAMERA_SCENES_H
#define JSRF_CAMERA_SCENES_H

#include <stdint.h>

/* The graffiti camera's mode: the spray code (sub_0008D280) asks for it,
 * and it frames the spot being sprayed, then goes back behind the player. */
#define CAMSCENE_MODE_GRAFFITI 13u

/* The modes the skater asks its camera for (its +0xE58) that the ticks
 * read: follow, which the others go back to, and the half pipe's, which the
 * half pipe's jump asks for (sub_000996A0, setting the skater's state to 6
 * as well), and only it. */
#define CAMSCENE_MODE_FOLLOW    3u
#define CAMSCENE_MODE_HALF_PIPE 6u

/* Whether camera mode mode, one the follow camera's post-update serves (3,
 * 4, 5, 9 and 13), is one of the game's own shots: graffiti's. The others
 * follow the player, or look from them at whoever they talk to, from where
 * the player stands in the open. The graffiti shot is aimed at the spot on
 * a wall, a bus or a column, so that surface is right by what it looks at:
 * kept out of walls, the eye would come in to a first-person view the
 * console never shows. It is left as the game frames it. */
int camscene_shot(uint32_t mode);

/* The shots Input > Behaviour > Camera ticks, in its order (spriteice, 22
 * November 2026), a bit each in camscene_ticks, camscene_asked and
 * camscene_hidden: */
enum {
    CAMSCENE_GRAFFITI,    /* Graffiti cam: the spot being sprayed (mode 13) */
    CAMSCENE_HALF_PIPE,   /* Half pipe cam: a jump in a half pipe (mode 6) */
    CAMSCENE_FIXED,       /* Loop cam: a fixed shot the stage places (mode 10) */
    CAMSCENES
};
#define CAMSCENE_BIT(shot) (1u << (shot))
#define CAMSCENE_ALL       ((1u << CAMSCENES) - 1u)

/* Every tick on with none in the settings: each shot as the game plays it
 * (spriteice, 22 November 2026, on the proposal's card: "Go ahead, all on
 * by default"). */
#define CAMSCENE_DEFAULT CAMSCENE_ALL

/* Input > Behaviour > Camera's ticks, in its order: camera_walls.h's
 * Default cam fix and No skater fade first, "at the top of it in their
 * current order" (spriteice, 22 November 2026), then the shots', in
 * CAMSCENE_'s order, from CAMROW_SHOTS. */
enum { CAMROW_FIX, CAMROW_FADE, CAMROW_SHOTS, CAMROW_TICKS = CAMROW_SHOTS + CAMSCENES };

/* File > Presets: Xbox sets the row, the console's own camera, no fix and
 * no floor at a wall, and every shot the game's; Modern sets Default cam fix
 * alone, none of the game's shots; the others leave it as the player has it
 * (spriteice, 22 November 2026: "our Default and Max presets to not touch
 * any of the users changes in the camera menu at all"; and all of Input, 23
 * November 2026). */
#define CAMROW_XBOX   (CAMSCENE_ALL << CAMROW_SHOTS)
#define CAMROW_MODERN (1 << CAMROW_FIX)

/* The ticks, read once, at start, before the game runs, which also adds
 * their row, Input > Behaviour > Camera, Behaviour's first, after
 * camwall_start: each tick changes its setting at once and keeps it in the
 * settings file, on the window's thread, the first two through
 * camera_walls.h. graffiti_cam=, half_pipe_cam= and loop_cam= in the
 * settings file, 1 (the default) or 0. */
void camscene_start(void);

/* The ticks, a bit each (CAMSCENE_BIT). */
unsigned camscene_ticks(void);

/* The game's shots a skater asks its camera for, a bit each, from what the
 * camera update (sub_000A5070) picks the camera's mode by, the skater's:
 * asked, the mode it asks for (+0xE58); fixed, a fixed shot asked for
 * (+0xEC8, which makes it mode 10); exit, the level exit's camera point set
 * (+0xF04); graffiti, the graffiti camera asked for (+0xE38 and +0xE3C,
 * which make it mode 13). Mode 10 has two makers, told apart by exit. The
 * player manager (sub_000A76E0), once its skater comes into a level's exit,
 * takes it off the pad and sets exit, with the exit's camera point; the
 * skater's update then asks for an eye at that point (0x8085B). Only while
 * exit is 0, the skater's update asks for the shot of the stage's camera
 * area it is in, a loop's or any other (0x80F72), and lets it go when it
 * leaves. That one is CAMSCENE_FIXED; the level exit's is none of these, so
 * no tick ever touches it. */
unsigned camscene_asked(uint32_t asked, uint32_t fixed, uint32_t exit, uint32_t graffiti);

/* Of shots, camscene_asked's, those whose tick is off in ticks
 * (camscene_ticks, or a recording's on playback): the camera update reads
 * each as not asked for, and the camera follows the player as anywhere
 * else. */
unsigned camscene_hidden(unsigned shots, unsigned ticks);

/* The ticks as the builds before them played the shots, for a recording
 * made with one (port/playback.h): the half pipe's and the stage's fixed
 * shots always, and the graffiti shot only while the free camera was off
 * (freecam), as the free camera turned it off then. */
unsigned camscene_ticks_before(int freecam);

/* Half pipe cam unticked: the camera follows the player through the half
 * pipe's jump, and rises and falls with them (spriteice, 22 November 2026:
 * "make it follow the skater in the corresponding cameras natural view,
 * without triggering the half pipe cam"). The follow camera (sub_000A3360)
 * keeps its height from where the skater last stood (its +0xDFC, which the
 * skater's update sets to its height, sub_0008EAA0 and sub_0008FE10, and
 * not in the air), and adds to it a lift along the skater's own up, with
 * its tilt (+0xCBC, +0xCC4): up the vert, tipped on its side, that lift
 * goes across, and the camera stays at the lip as the skater flies up out
 * of view. So through the jump
 * the camera's target is kept as high above where it was as the skater is
 * above where it was, the eye with it, so the game's own angle and distance
 * stay, and the free camera's orbit rises the same way; once the jump ends
 * (the skater lands), what was added is let go smoothly over
 * CAMSCENE_RISE_OUT frames. */
#define CAMSCENE_RISE_OUT 15

typedef struct {
    int on;             /* in a jump, or letting go of one */
    int out;            /* frames into letting go; 0: in the jump */
    double skater0;     /* the skater's height as the jump started */
    double target0;     /* the camera target's, with what was added then */
    double lift;        /* what is added this frame */
    double from;        /* what was added as the jump ended */
} CAMSCENE_RISE;

/* Each follow camera post-update of the player's camera (sub_000A6100):
 * jump, the half pipe's jump asked for and its tick off (camscene_hidden);
 * skater, the skater's height; target, the camera target's as the game put
 * it this frame. Returns how far to raise the target, the eye and the free
 * camera's orbit this frame: 0 outside a jump. */
double camscene_rise(CAMSCENE_RISE *r, int jump, double skater, double target);

#endif
