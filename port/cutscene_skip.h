/* Skip cutscenes: a scene ends at a second press of A, the way it ends by
 * itself.
 *
 * Every scene a mission plays (its events' command 227) is one object, the
 * scene (class 0x1C4BA8), made only by sub_00024240. Its update
 * (sub_00023790) plays it frame by frame, and at its last frame calls its
 * end, sub_0001E8C0: the scene is marked seen, the fade back to play
 * starts, its actors, sound and resources are let go, and the scene and its
 * holder are removed, so the mission carries on. The game's own skip, for a
 * scene the save has seen before, calls the same end at a press of A or
 * Start. With Skip cutscenes on, a first press of A while a scene plays shows
 * a prompt, "Press A again to skip", and a second press while it shows calls
 * that end, right after the scene's own update (recomp_manual.c). That is
 * then the one way out, for a scene seen before too: the game's own skip at
 * one press is left out while the option is on, and is the game's again
 * when it's off. Nothing in the story's flags or the mission is written:
 * the end is the game's.
 *
 * A rival's preview (the camera following a rival skater to their place, as
 * Poison Jam's in Shibuya Terminal or Beat's in Dogenzaka Hill) isn't a
 * scene: its mission plays it. The mission names the rival (its action 79),
 * gives it a recorded run (76), puts the bars on (206) and waits on its
 * arrival: its trigger 27 [slot] holds once the rival's run is over. The
 * rival replays the run's records, pad input, one after another, and stops
 * at the last one's end; the mission then plays the arrival its own way
 * (the rival removed, the bars off, the preview's seen bit, a scene where it
 * has one, then its end). With Skip cutscenes on, the same two presses, with
 * the same prompt, end a preview while its rival is still on its run: at
 * the second press the run jumps to its last record with one frame of it
 * left, so the rival ends it its own way at its next update and the mission
 * goes on from the arrival, as if the run had played out (recomp_manual.c,
 * after the events scan). That is the one way out of every preview, one seen
 * before too: the game's own skip of a preview seen before (its command 249,
 * at one press of A or Start) is left out while the option is on, as for
 * scenes, and is the game's again when it's off.
 *
 * The prompt is shown for CUTSCENE_SKIP_WINDOW seconds from the first press.
 * It fades in over CUTSCENE_SKIP_FADE while shrinking from
 * CUTSCENE_SKIP_GROW times its size to its size, and out the same way
 * reversed, as the graffiti soul indicator does. It goes with the scene when
 * the scene ends by itself.
 *
 * skipcutscenes= in the settings file, or Gameplay > Skips > Skip cutscenes:
 * off unless ticked. The idea, ending a scene through the game's own end,
 * is Miggas's and mstan's: no code of theirs is used. Plain C, so
 * tests/cutscene_skip links it. */
#ifndef JSRF_CUTSCENE_SKIP_H
#define JSRF_CUTSCENE_SKIP_H

#include <stdint.h>

#define CUTSCENE_SKIP_WINDOW 3.0    /* seconds the prompt waits for the second press */
#define CUTSCENE_SKIP_FADE   0.25   /* seconds of its fade in, and of its fade out */
#define CUTSCENE_SKIP_GROW   1.15f  /* its size at the start of its fade in and the end of its fade out */
/* With no frame of a playing scene for this long, the scene has ended by
 * itself, and the prompt goes with it. */
#define CUTSCENE_SKIP_GONE   0.1

/* Reads skipcutscenes= from the settings file, and adds the row to
 * Gameplay > Skips. Once, at start. */
void cutscene_skip_start(void);

/* Whether Skip cutscenes is on. */
int cutscene_skip_on(void);

/* Input playback's (port/playback.c): Skip cutscenes as a recording has it,
 * for its play, and the player's back after, the settings file left as it
 * is. */
void cutscene_skip_set(int on);

/* One frame of a playing scene, after its own update: scene is its address
 * (another one starts afresh), pressed whether A was pressed this frame, now
 * the time in seconds. Returns 1 when the scene is to end now: a second
 * press while the prompt shows. A first press, or one after the prompt's
 * time, shows the prompt. Off, it returns 0 and shows nothing. */
int cutscene_skip_frame(uint32_t scene, int pressed, double now);

/* The prompt at now, for its draw: 1 while it shows, with its opacity
 * (0 to 1) and its size (1 for its own); 0 when it doesn't. */
int cutscene_skip_prompt(double now, float *alpha, float *size);

/* A try of the prompt with no scene, for a test run (JSRF_SKIP_PROMPT_AT,
 * recomp_manual.c): it shows at now as at a first press, on or off, for
 * its whole while, and a press ends nothing. */
void cutscene_skip_try(double now);

/* The prompt's line, in its four pieces, each drawn with the game's font:
 * "Press ", the cap (the font's filled circle), the A on the cap, then
 * " again to skip". In the cap's place the game draws its own green A
 * button, the one its message boxes show, from the set the game loads it
 * in, which the prompt asks the game to load where it has let it go
 * (recomp_manual.c): the font's cap and A are for where the game has no
 * such set to give. */
enum {
    CUTSCENE_SKIP_PRESS,
    CUTSCENE_SKIP_CAP,
    CUTSCENE_SKIP_A,
    CUTSCENE_SKIP_REST,
    CUTSCENE_SKIP_PIECES
};
extern const char *const cutscene_skip_text[CUTSCENE_SKIP_PIECES];
extern const uint32_t cutscene_skip_colour[CUTSCENE_SKIP_PIECES];

/* Where each piece goes, on the HUD's 640 x 480, at size (1 its own):
 * width[] is each piece's width at size 1 as the font last drew it (an
 * estimate before its first draw). The line's right end sits at
 * CUTSCENE_SKIP_RIGHT and its middle at CUTSCENE_SKIP_MIDDLE, and the line
 * grows and shrinks about its own middle. The cap is CUTSCENE_SKIP_CAP_SIZE
 * times the text's size, as the in-game menu's caps are, and the A sits in
 * its middle. */
#define CUTSCENE_SKIP_RIGHT    587.0f  /* the speed gauge's right edge: 53 from the screen's */
#define CUTSCENE_SKIP_MIDDLE   414.0f  /* the speed gauge's middle, from its top at 401 to its bottom at 428 */
#define CUTSCENE_SKIP_SCALE    0.6f    /* the text's scale, as the game's menu labels' */
#define CUTSCENE_SKIP_CAP_SIZE 1.25f   /* the cap's size to the text's, as MENU_CAP_SIZE */
#define CUTSCENE_SKIP_HEIGHT   26.0f   /* the font's line at scale 1 (its new line's advance) */
typedef struct {
    float x, y;     /* the piece's top left, as the font call takes it */
    float scale;    /* its scale, for x and y alike */
} CutsceneSkipPlace;
void cutscene_skip_place(float size, const float width[CUTSCENE_SKIP_PIECES],
                         CutsceneSkipPlace place[CUTSCENE_SKIP_PIECES]);

/* The rivals a mission waits on, for a preview's skip (recomp_manual.c):
 * of its trigger list, n entries of CUTSCENE_SKIP_EVENT bytes at list in mem
 * (the guest's memory, addresses as offsets into it), the slots of its
 * entries with command CUTSCENE_SKIP_ARRIVED (their first word), each once,
 * in the list's order. Up to max go in slots[]; returns how many. At most
 * CUTSCENE_SKIP_EVENTS entries are looked at. */
#define CUTSCENE_SKIP_EVENT          0x54u /* conditions and count, flags and count, command, its words */
#define CUTSCENE_SKIP_EVENT_COMMAND  0x10u /* where an entry's command is, a word */
#define CUTSCENE_SKIP_EVENT_WORD     0x14u /* the command's first word */
#define CUTSCENE_SKIP_ARRIVED        27u   /* the trigger "the rival in slot [slot] has arrived" */
#define CUTSCENE_SKIP_EVENTS         1024u
int cutscene_skip_rivals(const uint8_t *mem, uint32_t list, uint32_t n, uint32_t *slots, int max);

/* A rival's run: its records, CUTSCENE_SKIP_RECORD bytes each, the first
 * 16-bit word of each its frames, with CUTSCENE_SKIP_RECORD_LAST set on the
 * run's last record. From the record at `record` on, in mem of size bytes,
 * the address of the run's last record; 0 when none is found within
 * CUTSCENE_SKIP_RECORDS records or mem, or record is 0. */
#define CUTSCENE_SKIP_RECORD         8u
#define CUTSCENE_SKIP_RECORD_LAST    0x8000u
#define CUTSCENE_SKIP_RECORDS        0x10000u
uint32_t cutscene_skip_run_last(const uint8_t *mem, uint32_t size, uint32_t record);

#endif
