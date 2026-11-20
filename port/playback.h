/* Input playback: the pad as the game read it, recorded by the game's own
 * frame and played back (Developer > Playback; docs/playing.md).
 *
 * A frame is a pass of the game's main loop (sub_00013A80): the game steps
 * once a pass, whatever the time (the shared folder's
 * input-playback/PHASE-0.md), so a press keyed to its pass, not to the
 * clock, comes at the same point of the game's play however long a load or
 * a hitch took. Phase 0 also found that the game's own state still drifts
 * with no input at all (its random numbers, and the title's scene, follow
 * the host's time somewhere), so playback doesn't count on the presses
 * alone:
 *
 * - Anchors: each mission file and stage the game asks for, and each
 *   cutscene's start and end. The game's random generator is kept with each,
 *   and put back on playback. Playback holds the pad as the recording had it
 *   until the game reaches the next anchor, then goes on from that anchor's
 *   frame; one the game reaches early skips to it. One the game doesn't
 *   reach within PLAYBACK_ANCHOR_WAIT, or one it reaches in another order,
 *   stops the run as off route.
 * - Checks: once a second of frames, the player's skater: where it is and
 *   which way it faces, and the camera's mode. Playback measures how far the
 *   skater is from that spot; past PLAYBACK_DRIFT_LIMIT for
 *   PLAYBACK_DRIFT_CHECKS checks in a row, the run stops as off route.
 * - Idle moments: a second or more with nothing pressed and the skater
 *   still. Playback puts the skater back on the recorded spot there, facing
 *   the recorded way, when the camera is in the recorded mode and the skater
 *   is within the drift limit of it.
 *
 * Every pad's state is recorded on all four ports, in the game's
 * XInputGetState (port/pad.c), after the keyboard has had the left stick and
 * before the free camera takes the right one: what the game read, but with
 * the right stick as the pad gave it while the free camera steers. On
 * playback the recording's state takes its place there, and the free camera
 * takes its stick from it as it did. The
 * free camera's switch and tuning are recorded too, as the game's frame takes
 * them up (port/freecam.c): at the start, and whenever F8, the pad or the
 * menu changes them. On playback the recording's are in place of the player's
 * from its first, and the player's are back once it stops; the player's own
 * are never changed by it. So is the mouse's move the free camera turns by,
 * each frame it moves: on playback the recording's is in place of the
 * player's. Anything pressed on a real pad or the keyboard,
 * the right stick too, ends playback and gives the game back, once they have
 * been let go of since it started; so does turning the free camera on or off,
 * or changing it in the menu, and a move of the mouse that would turn it,
 * once the mouse has been still since playback started.
 *
 * The recording is a folder, `recordings/<date>-<time>/` beside the settings
 * file (`portable/recordings/` in portable mode), holding recording.txt: one
 * line each, so a run can be written by hand.
 *
 *   jsrf-pc-recording 1                     the format, first
 *   version <version> <linux|windows>       the build it was made on
 *   pad <frame>[.<read>] <port> <buttons> <analog> <lx> <ly> <rx> <ry>
 *   pad <frame>[.<read>] <port> none        no pad on that port
 *   freecam <frame> <on> <invert_x> <invert_y> <sensitivity_x> <sensitivity_y> <deadzone> <autocentre> <mouse> <mouse_sensitivity>
 *   mouse <frame> <x> <y>
 *   anchor <frame> <kind> <value> <random>
 *   check <frame> <x> <y> <z> <turn> <camera>
 *   idle <frame> <x> <y> <z> <turn> <camera>
 *   end <frame>
 *
 * A pad line is a change: the state from that read of that frame on (read is
 * the port's read in the frame, from 0; left out, 0). buttons is wButtons in
 * four hex digits; analog the eight analog buttons (A B X Y Black White LT
 * RT) in two hex digits each; the sticks in decimal, up positive. A freecam
 * line is the free camera's switch and tuning from that frame on, as the
 * settings file has them (port/freecam.c), each in its range there; before
 * the first, the free camera is as the player has it. One without mouse and
 * mouse_sensitivity (a recording made before the mouse turned the free
 * camera) has Mouse look off, at 5. A mouse line is the mouse's move the free
 * camera turned by at that frame, in the mouse's counts, right and up
 * positive; on playback, the moves up to the recording's frame not yet taken
 * are taken at once, so a frame skipped to turns the view as far, and one
 * waited at turns it once. A kind is mission.bin,
 * mission.dat, stage, scene or scene-end, its value and the random generator
 * in eight hex digits. turn is the skater's turn, 0x10000 a whole turn, in
 * eight hex digits; camera the camera's mode. Lines starting with # are
 * comments. */
#ifndef JSRF_PLAYBACK_H
#define JSRF_PLAYBACK_H

#include <stdint.h>
#include "freecam.h"
#include "xinput_xbox.h"

#define PLAYBACK_PORTS        4
#define PLAYBACK_FILE         "recording.txt"
#define PLAYBACK_FOLDER       "recordings"   /* beside the settings file */
#define PLAYBACK_CHECK_EVERY  60u            /* frames between checks: a second */
#define PLAYBACK_IDLE_FRAMES  60u            /* nothing pressed and still this long */
#define PLAYBACK_STILL        1.0f           /* units the skater may move and be still */
#define PLAYBACK_DRIFT_LIMIT  50.0f          /* units off the recorded spot */
#define PLAYBACK_DRIFT_CHECKS 3              /* checks in a row past it: off route */
#define PLAYBACK_ANCHOR_WAIT  3600u          /* frames an anchor is waited for: a minute */
#define PLAYBACK_ANCHOR_AHEAD 4              /* recorded anchors an early one may skip */

/* What starts a stretch of play. */
typedef enum {
    PLAYBACK_MISSION_BIN,     /* a mission's .bin asked for (sub_00030490): its id */
    PLAYBACK_MISSION_DAT,     /* its .dat (sub_00030690): its id */
    PLAYBACK_STAGE,           /* a stage the mission's flow asks for: 0x0001000N for Stg1N */
    PLAYBACK_SCENE,           /* a cutscene starts playing (sub_00023790): 0 */
    PLAYBACK_SCENE_END,       /* and ends: 0 */
    PLAYBACK_ANCHOR_KINDS
} PLAYBACK_ANCHOR;

/* Where a recording or a playback stands. */
typedef enum {
    PLAYBACK_OFF,             /* neither */
    PLAYBACK_RECORDING,
    PLAYBACK_PLAYING,
    PLAYBACK_WAITING,         /* playing, and holding the pad for the next anchor */
    PLAYBACK_FINISHED,        /* played to the end on route */
    PLAYBACK_OFF_ROUTE,       /* stopped: the game went another way */
    PLAYBACK_HANDED_BACK,     /* stopped: a real pad or the keyboard pressed something */
    PLAYBACK_RECORDED         /* a recording, ended */
} PLAYBACK_STATE;

/* What a run came to, for the log's summary and tests/playback. */
typedef struct {
    PLAYBACK_STATE state;
    uint32_t frames;          /* frames played, or recorded */
    int anchors, anchors_met; /* anchors in the recording, and met */
    int anchors_skipped;      /* recorded anchors an early one skipped */
    int checks, idles_put_back;
    float drift_most;         /* the largest drift at a check */
    uint32_t drift_most_at;   /* at which recorded frame */
    char why[160];            /* off route: why */
} PLAYBACK_RESULT;

/* As the engine starts (port/main.c), before the game: a recording or a
 * playback asked for by the environment, which is then taken out of it, so
 * File > Restart starts afresh: JSRF_RECORD=1 (Developer > Playback >
 * Record), JSRF_PLAY=<folder> (the launcher's --play, which closes the game
 * at the end) or JSRF_PLAY_MENU=<folder> (Developer > Playback > Play, which
 * gives it back). Then Developer > Playback's rows. */
void playback_start(void);

/* A recording into folder, made if need be, or a playback of the one in it
 * (exit_at_end: close the game at the end, or when off route). 0 when it
 * can't be: the log says why. */
int playback_record(const char *folder);
int playback_play(const char *folder, int exit_at_end);

/* The next pass of the game's main loop, at its start (sub_00013A80). */
void playback_pass(void);

/* Each of the game's pad reads (port/pad.c's pad_get_state), with the host's
 * state, the keyboard's moves in it, before the free camera takes its stick,
 * and whether the port has a pad: recorded, or put in its place by the
 * recording's. Returns whether the port has a pad, as the game is to see it. */
int playback_pad(uint32_t port, XBOX_INPUT_STATE *st, int connected);

/* Once a game frame (port/freecam.c's freecam_frame), with the free camera's
 * switch and tuning as the player has them: recorded when they change, or,
 * while a recording plays, the recording's put in their place. The player
 * changing them while it plays hands the game back. Returns whether the
 * recording's are in place. */
int playback_freecam(FREECAM_SETTINGS *fc);

/* Once a game frame (freecam_frame), after playback_freecam, with the
 * mouse's move the free camera turns by this frame, in its counts, right and
 * up (0 in a frame it doesn't steer, or with Mouse look off): recorded when it
 * moved, or, while a recording plays, the recording's put in its place. The
 * player's mouse moving while it plays hands the game back. Returns whether
 * the recording's is in place. */
int playback_mouse(int *dx, int *dy);

/* An anchor, as the game reaches it, with its random generator: kept, or
 * put back. */
void playback_anchor(PLAYBACK_ANCHOR kind, uint32_t value, uint32_t *random);

/* Each update of the camera that follows the main player's skater: its
 * position (x, y, z) and turn, which playback may put back at an idle
 * moment, and the camera's mode. */
void playback_view(float position[3], uint32_t *turn, uint32_t camera);

/* Ends a recording, from the menu's thread: at the next pass, or now when
 * the game has stopped passing. Nothing while not recording. */
void playback_stop(void);

/* The run so far. */
PLAYBACK_STATE playback_state(void);
const PLAYBACK_RESULT *playback_result(void);

#endif
