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
 * once the mouse has been still since playback started. Input > Behaviour >
 * Camera's ticks for the game's shots (port/camera_scenes.c) are recorded
 * too, as the camera update reads them: at the first, and whenever the menu
 * changes them, with whether the free camera eases back after a shot. On
 * playback the recording's are in place of the player's from its first, and
 * changing them in the menu ends playback; a recording with none, made
 * before the ticks, plays the shots as the builds before them did.
 *
 * The recording is a folder, `recordings/<date>-<time>/` beside the settings
 * file (`portable/recordings/` in portable mode), holding recording.txt: one
 * line each, so a run can be written by hand.
 *
 * A run's folder, as --play takes it, can hold two more beside it:
 *
 *   steps.txt   a line each, in the recording's order, # for a comment:
 *     setting <key>=<value>     into the play's copy of the settings file
 *     developer <NAME>=<value>  into its developer.cfg (port/developer.c)
 *     idle <n>: menu <path>     a row by its labels (menu.h, menu_path_run)
 *     idle <n>: mark <words>    a line in the log
 *     idle <n>: screenshot      the picture shown, as shot-<line>.bmp
 *     frame <n>: ...            menu, mark or screenshot, at a recorded frame
 *   profile/    settings.cfg, developer.cfg, start-states.txt, saves/ and
 *               cache/, as a portable folder has them
 *
 * An idle moment's steps run once playback has reached it (the skater put
 * back, or not), and hold the recording there, the pad still, until they
 * are done; a frame's run once the recording reaches it, holding nothing.
 * Each line is checked as the recording is loaded, and each menu row at
 * the play's first flip (playback_flip): one that can't be stops the play.
 * With --play, a profile/ is copied for each play into plays/<date>-<time>/
 * in the run's folder before anything reads them (playback_first), its
 * setting and developer lines written into the copy, and the settings file
 * (JSRF_SETTINGS_FILE), the engine's save folder (JSRF_SAVE_DIR) and the
 * game's saves (JSRF_USER_DIR) pointed at it, so the player's are never
 * opened; the screenshots go there too. Menu rows and setting lines change
 * settings, so they need that copy: Play (Restart) leaves a profile/ out,
 * and runs marks and screenshots alone, into a plays/ folder of their own.
 *
 *   jsrf-pc-recording 1                     the format, first
 *   version <version> <linux|windows>       the build it was made on
 *   start mission <CCNN> <skater> <photo> <progress>
 *   start load <photo>                      where the run started, before any frame
 *   pad <frame>[.<read>] <port> <buttons> <analog> <lx> <ly> <rx> <ry>
 *   pad <frame>[.<read>] <port> none        no pad on that port
 *   freecam <frame> <on> <invert_x> <invert_y> <sensitivity_x> <sensitivity_y> <deadzone> <autocentre> <mouse> <mouse_sensitivity>
 *   mouse <frame> <x> <y>
 *   camera <frame> <graffiti> <half_pipe> <loop> <back>
 *   anchor <frame> <kind> <value> <random>
 *   check <frame> <x> <y> <z> <turn> <camera>
 *   idle <frame> <x> <y> <z> <turn> <camera>
 *   end <frame>
 *
 * A start line says the run was started from Developer > Missions with
 * Record ticked: at mission CCNN as skater (the game's number, -1 for none,
 * New Game's), with photo mode's tick (0 or 1) and the progress the mission
 * started with (missions_started's hash, in eight hex digits, 00000000 for
 * New Game's); or at the title's Load Game. It is written with the first
 * line after the version, once Missions has started the run or refused it
 * (missions_paths, before the game's first pass). A play of it starts the game
 * there again, as the pick did (playback_first), and stops at its first
 * frame, off route, if this run didn't start there, as or with the same.
 * Without one, the run started as the game does, and so must a play of it.
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
 * waited at turns it once. A camera line is Input > Behaviour > Camera's
 * shots' ticks from that frame on, each 0 or 1: Graffiti cam, Half pipe cam
 * and Loop cam; before the first, the ticks are as the player has them.
 * back says what the free camera did after a game's shot (port/freecam.c):
 * 4, it glided from the shot's view to its own in 24 frames, the controls
 * following the view it glided to, and cut into the graffiti shot as the
 * game's camera does; 3, as 4, but it glided into the graffiti shot from
 * its own view in 12 frames (made before that); 2, it glided in 30 and 15,
 * the controls following the view it showed (made before that); 1, it
 * eased back to its view's angle from the game's view (made before it
 * glided); one without it (made before it eased) has the free camera start
 * from the game's view then, as does a recording with no camera line,
 * which has the half pipe's and the stage's fixed shots, and the graffiti
 * shot only while the free camera is off (camscene_ticks_before). A kind is mission.bin,
 * mission.dat, stage, scene or scene-end, its value and the random generator
 * in eight hex digits. turn is the skater's turn, 0x10000 a whole turn, in
 * eight hex digits; camera the camera's mode. Lines starting with # are
 * comments. */
#ifndef JSRF_PLAYBACK_H
#define JSRF_PLAYBACK_H

#include <stdint.h>
#include "freecam.h"
#include "missions.h"
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
#define PLAYBACK_STEPS        "steps.txt"    /* a run's steps, beside its recording */
#define PLAYBACK_PROFILE      "profile"      /* the settings and saves a run starts from */
#define PLAYBACK_PROFILE_SETTINGS "settings.cfg" /* its settings file, as a portable folder has it */
#define PLAYBACK_PLAYS        "plays"        /* a folder for each play: its copy, its screenshots */
#define PLAYBACK_STEP_SETTLE  5u             /* flips after a menu row before the next step */
#define PLAYBACK_STEP_WAIT    600u           /* frames steps may hold the recording: ten seconds */

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

/* The environment variable Record (Restart), and a pick in Developer >
 * Missions with Record ticked, carry over the restart: 1 records. */
#define PLAYBACK_RECORD_VAR "JSRF_RECORD"

/* First of all as the engine starts (port/main.c), before Missions and
 * Photo mode read their variables: a recording about to be played (JSRF_PLAY
 * or JSRF_PLAY_MENU, left for playback_start) that has a start line puts
 * its start in their place, as Developer > Missions' pick does
 * (JSRF_START_MISSION, JSRF_START_SKATER, JSRF_START_LOAD, JSRF_START_PHOTO). */
void playback_first(void);

/* As the engine starts (port/main.c), before the game: a recording or a
 * playback asked for by the environment, which is then taken out of it, so
 * File > Restart starts afresh: JSRF_RECORD=1 (Developer > Playback >
 * Record, or a mission picked with Developer > Missions > Record ticked),
 * JSRF_PLAY=<folder> (the launcher's --play, which closes the game at the
 * end) or JSRF_PLAY_MENU=<folder> (Developer > Playback > Play, which gives
 * it back). Then Developer > Playback's rows. */
void playback_start(void);

/* Developer > Missions > Record's tick (port/missions_menu.c adds the row),
 * and flipping it: while it is ticked, a mission or Load game picked there
 * restarts with PLAYBACK_RECORD_VAR. Ticked when a run recording from a
 * mission writes its start line, so it stays ticked from pick to pick. */
int playback_missions_ticked(void);
void playback_missions_flip(void);

/* A recording into folder, made if need be, with a start line when this run
 * was started at a mission or Load Game (missions_started), or a playback of
 * the one in it (exit_at_end: close the game at the end, or when off route).
 * 0 when it can't be: the log says why. */
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

/* For a recording with no camera line: playback_camscenes puts this in place
 * of the ticks, for camscene_ticks_before. */
#define PLAYBACK_CAMSCENES_BEFORE 0x80000000u

/* Each camera update (sub_000A5070) on a camera the pad steers, with
 * Input > Behaviour > Camera's ticks as the player has them, a bit each
 * (camera_scenes.h): recorded when they change, or, while a recording plays,
 * the recording's put in their place from its first camera line, or
 * PLAYBACK_CAMSCENES_BEFORE for a recording with none. The player changing
 * them while it plays hands the game back. Returns whether the recording's
 * are in place. */
int playback_camscenes(unsigned *ticks);

/* What the free camera does after a game's shot (port/freecam.c), as the
 * camera line's back number has it (the format, above). */
enum {
    PLAYBACK_BACK_NONE,     /* starts from the game's view */
    PLAYBACK_BACK_EASE,     /* eases its angle back from the game's view */
    PLAYBACK_BACK_GLIDE,    /* glides from the shot's view, and into the graffiti shot */
    PLAYBACK_BACK_STEADY,   /* glides quicker, the controls following the view it glides to */
    PLAYBACK_BACK_OUT       /* as STEADY, but cuts into the graffiti shot, as the game's camera does */
};
/* PLAYBACK_BACK_OUT, unless a recording plays: then its camera lines'
 * back, or PLAYBACK_BACK_NONE for one whose camera lines say nothing of it,
 * or that has none. */
int playback_freecam_back(void);

/* An anchor, as the game reaches it, with its random generator: kept, or
 * put back. */
void playback_anchor(PLAYBACK_ANCHOR kind, uint32_t value, uint32_t *random);

/* Each update of the camera that follows the main player's skater: its
 * position (x, y, z) and turn, which playback may put back at an idle
 * moment, and the camera's mode. */
void playback_view(float position[3], uint32_t *turn, uint32_t camera);

/* At each flip, on the window's thread (src/nv2a_gl/nv2a_gl.c's present),
 * while a run with steps plays: at its first, every menu row its steps name
 * looked for; then each step whose moment has come, one a flip, in the
 * file's order: a menu row chosen by its labels as a click chooses it
 * (menu_path_run), then PLAYBACK_STEP_SETTLE flips for it to show; a mark
 * in the log; or the picture this flip shows written by shot as a BMP
 * (1 when written), shot-<line>.bmp in the play's folder. */
void playback_flip(int (*shot)(const char *path));

/* Ends a recording, from the menu's thread: at the next pass, or now when
 * the game has stopped passing. Nothing while not recording, but a line
 * that says so, for whoever chose Stop recording. */
void playback_stop(void);

/* The game restarting (port/main.c's restart, File > Restart and every
 * (Restart) row), which doesn't run this run's exit: a recording ends, its
 * reason "the game restarted", and a play stops, with its summary. Nothing,
 * and nothing said, while neither runs. */
void playback_restart(void);

/* The run so far. */
PLAYBACK_STATE playback_state(void);
const PLAYBACK_RESULT *playback_result(void);

#endif
