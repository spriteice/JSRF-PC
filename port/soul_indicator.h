/* The graffiti soul indicator: the soul's badge while a soul challenge is on
 * track.
 *
 * Gameplay > Graffiti souls > Visual indicator, kept as soulindicator= in
 * the settings file (0, off, the default, or 1): while one of an area's
 * special graffiti soul challenges is under way and on track, the graffiti
 * soul's badge (the cyan badge with the soul's figure in it, as the streets
 * show the soul, drawn white: spriteice, 2026-11-17) shows at the bottom
 * left, level with the gauge at the bottom right and as far from its edge.
 * It comes in with a fade while shrinking to its size, pulses on each new
 * one of Shibuya Terminal's platforms, and on failing turns red, holds a
 * second, and fades out while growing; the soul's award fades it out the
 * same way, without the red (spriteice's design, 12:00 UTC on 2026-11-15).
 * The game's rules don't change: this reads the game's memory and adds the
 * badge's quad to the HUD.
 *
 * Which challenge, and what state it is in, is read from the mission's own
 * script and nothing kept per area: the award that sets a notebook flag
 * (RT[C0] to RT[10F]) from one of two detectors.
 *   - A course (op 0x11): steps the skater does in boxes, which fail in fail
 *     boxes. The course's record for the player says how far it has got.
 *     Its progress shows the badge once the game can fail the run: past
 *     its first step, or partway through a first step that has fail boxes.
 *     Most courses open with a step that has none (Chuo street's second of
 *     grinding on its first rail), which the game never fails and whose
 *     time adds up over tries, so the badge waits for the step after it
 *     (spriteice, 11:42 UTC on 2026-11-17). Progress going back to 0
 *     without the course done is a failure, which is what the game's fail
 *     does (sub_00015130). Reading the drop, not the failed flag, doesn't
 *     depend on the order of the frame's updates: the game clears failed
 *     again on its next detector pass (op 0x12).
 *   - Sheets (op 0x20): the mission flags in the award's conditions that a
 *     touch (op 1) sets, Shibuya Terminal's 13 platforms. The first set
 *     shows the badge, each new one pulses it, and all of them cleared (the
 *     ground's touch clears them) is a failure.
 * The challenge is under way while the award's other conditions hold: for
 * a course, they hold once the game's start detector (op 0x6A) has set its
 * mission flag; for both, they hold until the award sets its notebook flag,
 * which is the soul awarded. An award with no progress to show before it
 * (Kibogaoka Hill's one high sheet, an op 1 touch) shows nothing.
 *
 * The hooks are in recomp_manual.c: the mission flow's step hands each flow
 * to soul_indicator_flow, which reads the mission once at its setup (state
 * 14); the gauge's draw, after the gauge's first draw in each pass of the
 * game's loop (soul_indicator_once), asks soul_indicator_frame for the quad,
 * and draws its vertices (soul_indicator_vertices) through the game's own
 * calls, as a HUD piece of its own (WIDE_HUD_SOUL), so widescreen takes it
 * to the left edge, Display > HUD scale sizes it, and Disable HUD hides
 * it. The reading, the state from frame to frame, the
 * quad and the [SOUL] log are here, plain C on guest memory, so a test
 * links them (tests/soul_indicator). */
#ifndef JSRF_SOUL_INDICATOR_H
#define JSRF_SOUL_INDICATOR_H

#include <stdint.h>

/* The mission flow (sub_00051FC0's this), one a mission. */
#define SOUL_FLOW_MISSION  0x58u    /* the mission: chapter << 16 | its file's number (915, mssn0915) */
#define SOUL_FLOW_STATE    0x5Cu    /* its state, an index into its step table */
#define SOUL_FLOW_COURSES  0x94u    /* -> the mission's courses, a pointer each */
#define SOUL_FLOW_SCRIPT   0x1040u  /* -> the mission's script */
#define SOUL_FLOW_SIZE     0x1044u  /* the flow's fields read here end before this */
#define SOUL_STATE_SETUP   14u      /* the mission's setup, its script in memory */
#define SOUL_STATE_NEXT    93u      /* making the next mission's flow */
#define SOUL_STATE_HANDOVER 94u     /* handing over to it */

/* The script, laid out as the mission file's header: a list (a pointer and
 * a count) for each record type, type t at 0x34 + 8 * t. */
#define SOUL_SCRIPT_COURSES  0xB4u   /* type 16: the courses (count at +4) */
#define SOUL_SCRIPT_TRIGGERS 0x17Cu  /* type 41: the detectors, every one each tick (sub_00056990) */
#define SOUL_SCRIPT_SIZE     0x184u  /* the script's fields read here end before this */

/* A trigger: 0x54 bytes. */
#define SOUL_TRIGGER_SIZE  0x54u
#define SOUL_TRIGGER_IF    0x00u    /* -> its conditions, a flag word each; how many at +4 */
#define SOUL_TRIGGER_SETS  0x08u    /* -> the flags it sets, a flag word each; how many at +0xC */
#define SOUL_TRIGGER_OP    0x10u    /* its detector */
#define SOUL_TRIGGER_ARGS  0x14u    /* the detector's arguments, a dword each */
#define SOUL_TRIGGERS_MOST 1024u    /* more than any mission has */
#define SOUL_OP_TOUCH      0x01u    /* the player touched an area */
#define SOUL_OP_COURSE     0x11u    /* a course done: player, course */
#define SOUL_OP_SHEETS     0x20u    /* the sheets' award */

/* A flag word: its bank, its index and the value it sets or tests. */
#define SOUL_FLAG_BANK(w)  ((w) & 7u)
#define SOUL_FLAG_INDEX(w) (((w) >> 3) & 0xFFFFu)
#define SOUL_FLAG_VALUE(w) (((w) >> 19) & 1u)
/* The flag manager and its banks, as JSRF_MSCRIPT_DUMP reads them (the
 * port's [FLAG] log): mission flags (MF), which aren't saved, then CF, GF,
 * and the saved RT, which holds the GG notebook's. */
#define SOUL_FLAGS         0x001EFFB0u
#define SOUL_BANK_MF       0u
#define SOUL_BANK_RT       3u
#define SOUL_BANKS         4u
#define SOUL_BANK_FLAGS    0x200u   /* flags in a bank */
#define SOUL_AWARD_FIRST   0xC0u    /* the notebook's flags: RT[C0] to RT[10F] */
#define SOUL_AWARD_LAST    0x10Fu

/* A course object (sub_00014ED0): its steps, and its record for each
 * player. */
#define SOUL_COURSE_STEPS   0x00u   /* -> the script's entry for it: -> its steps, how many */
#define SOUL_COURSE_PLAYERS 0x04u   /* -> the player's record, a pointer each */
#define SOUL_PLAYERS        4u
#define SOUL_RECORD_DONE    0x00u
#define SOUL_RECORD_FAILED  0x04u
#define SOUL_RECORD_STEP    0x08u
#define SOUL_RECORD_TIMER   0x0Cu
#define SOUL_RECORD_AMOUNT  0x10u
#define SOUL_RECORD_SIZE    0x14u
/* A step, 0x20 bytes: its kind, amount and time, then its boxes and its
 * fail boxes (a pointer, then how many). */
#define SOUL_STEP_FAILS     0x1Cu   /* how many fail boxes it has */
#define SOUL_STEP_SIZE      0x20u

/* Where the badge goes, in the HUD's 640 x 480: as far from the left edge
 * as the gauge (x 436 to 587) is from the right, its centre level with the
 * gauge's (y 401 to 428), and its size. Constants, to adjust from the test
 * build. */
#define SOUL_LEFT          53.0f
#define SOUL_MIDDLE_Y      414.5f
#define SOUL_SIZE          40.0f

/* The way it moves, in seconds and sizes (1 is its size). */
#define SOUL_IN_TIME       0.25     /* in: a fade, shrinking from SOUL_IN_FROM */
#define SOUL_IN_FROM       1.15
#define SOUL_PULSE_TIME    0.3      /* a new sheet: up to SOUL_PULSE_TO and back */
#define SOUL_PULSE_TO      1.2
#define SOUL_RED_TIME      0.15     /* failed: to red, */
#define SOUL_RED_HOLD      1.0      /* held, */
#define SOUL_OUT_TIME      0.25     /* then out: a fade, growing to SOUL_OUT_TO */
#define SOUL_OUT_TO        1.15
#define SOUL_RED_MOST      0.5f     /* how far the badge goes to red, the white still showing under it */
#define SOUL_WHITE_DOT     0x0080FF80u /* the white pass's diffuse: green alone, as a dot product's side */
#define SOUL_RED           0x00FF0000u

/* At start, before the game runs: reads the setting and adds its row to the
 * in-game menu's Gameplay > Graffiti souls. */
void soul_indicator_start(void);

/* Whether the badge shows. */
int soul_indicator_on(void);

/* The challenge a mission holds. */
enum { SOUL_NONE, SOUL_COURSE, SOUL_SHEETS };
#define SOUL_CONDITIONS 32  /* the most conditions an award read here has */
#define SOUL_LIST_MOST  64  /* the most flags a trigger's list has */

typedef struct {
    int kind;                           /* SOUL_NONE, SOUL_COURSE or SOUL_SHEETS */
    uint32_t flow, mission, script;
    unsigned trigger;                   /* the award's place among the triggers */
    unsigned others;                    /* how many more awards it could have been */
    uint32_t award;                     /* the notebook flag it sets */
    unsigned nif;                       /* its conditions, but for the sheets' */
    uint32_t cond[SOUL_CONDITIONS];
    unsigned nsheets;                   /* the sheets' flags */
    uint32_t sheet[SOUL_CONDITIONS];
    uint32_t player, course;            /* the course's */
} SoulChallenge;

/* Reads the challenge of the mission whose flow is at flow, in the game's
 * memory mem, whose guest addresses run below top: 1 and c filled when it
 * has one to show, 0 and c->kind SOUL_NONE when it hasn't. */
int soul_indicator_read(const uint8_t *mem, uint32_t top, uint32_t flow, SoulChallenge *c);

/* What the challenge is doing this frame. */
typedef struct {
    int under_way;      /* the award's conditions hold, but for the sheets' */
    int awarded;        /* its notebook flag is set */
    int progress;       /* a course: 1 once the game can fail it (above); sheets: how many are set */
    int done;           /* a course done, the award to come; all the sheets set */
    int failed;         /* a course's failed flag */
    int moving;         /* a course: 1 when it has any progress, its first step's included */
    int step;           /* a course: its step */
} SoulSample;

/* Reads it: 1, or 0 when it can't be read (and s says nothing). */
int soul_indicator_sample(const uint8_t *mem, uint32_t top, const SoulChallenge *c,
                          SoulSample *s);

/* The badge's state from frame to frame. */
enum { SOUL_HIDDEN, SOUL_IN, SOUL_ON, SOUL_FAILING, SOUL_OUT };
typedef struct {
    int phase;
    double at;          /* when the phase began */
    double pulse_at;    /* when the last pulse began, or < 0 for none */
    int red;            /* going out red, after failing */
    int last;           /* the progress last frame */
} SoulShow;

/* What a frame changed, for the log. */
enum { SOUL_SAME, SOUL_SHOWN, SOUL_PULSED, SOUL_FAILED, SOUL_DONE, SOUL_ENDED };

/* The badge as it starts a mission: hidden. */
void soul_indicator_reset(SoulShow *show);

/* Moves the badge on to time now (seconds) with this frame's sample of a
 * challenge of kind kind: returns what it changed. */
int soul_indicator_step(SoulShow *show, int kind, const SoulSample *s, double now);

/* The quad to draw at time now, in the HUD's 640 x 480. */
typedef struct {
    float x0, y0, x1, y1;
    float alpha;        /* the fade: 0 to 1 */
    float red;          /* how far to red: 0 to 1 */
    uint32_t colour;    /* the badge's diffuse: SOUL_WHITE_DOT, at the fade's alpha */
    uint32_t red_colour;/* the red pass's diffuse: red, its alpha the fade times the red */
} SoulQuad;

/* 1 and q filled when the badge shows, 0 when it doesn't. */
int soul_indicator_quad(const SoulShow *show, double now, SoulQuad *q);

/* The quad as the HUD's quad batches take it (their add, mode 0): four
 * vertices of SOUL_VERTEX bytes, (x0, y0), (x1, y0), (x0, y1), (x1, y1),
 * each with its place, a diffuse, the specular the game's HUD quads carry,
 * and its corner of the texture. */
#define SOUL_VERTEX          0x18u
#define SOUL_VERTEX_X        0x00u
#define SOUL_VERTEX_Y        0x04u
#define SOUL_VERTEX_DIFFUSE  0x08u
#define SOUL_VERTEX_SPECULAR 0x0Cu
#define SOUL_VERTEX_U        0x10u
#define SOUL_VERTEX_V        0x14u
#define SOUL_QUAD_BYTES      (4u * SOUL_VERTEX)
#define SOUL_SPECULAR        0x00FFFFFFu

/* Writes q's four vertices at at, in guest memory mem, with the diffuse
 * colour: q->colour for the badge, q->red_colour for its red pass. */
void soul_indicator_vertices(uint8_t *mem, uint32_t at, const SoulQuad *q, uint32_t colour);

/* The hooks' side. Each step of the mission flow at flow: the mission's
 * challenge read at its setup's first frame, and dropped when the flow
 * hands over to the next. */
void soul_indicator_flow(const uint8_t *mem, uint32_t top, uint32_t flow, double now);

/* Whether the badge draws with this draw of the gauge: with the gauge's
 * first draw in each pass of the game's main loop (loop, the passes so far),
 * and not again in that pass, so its fade is never drawn twice over. pass is
 * the gauge's own, the HUD walk's pass it drew on. The log says once which
 * pass the badge first drew with, and once if the gauge drew again in one
 * loop pass, on which. */
int soul_indicator_once(unsigned loop, uint32_t pass);

/* Each frame the gauge is drawn: the quad to draw after it, as
 * soul_indicator_quad, from the mission's challenge, or 0. The [SOUL] log
 * says each change. */
int soul_indicator_frame(const uint8_t *mem, uint32_t top, double now, SoulQuad *q);

#endif
