/* Run with: python tests/soul_indicator/run.py
 *
 * The graffiti soul indicator without the game: port/soul_indicator.c as it
 * is, built with the in-game menu, reading and keeping soulindicator=
 * through the engine's own settings code (taken from src/nv2a_gl/nv2a_gl.c
 * by run.py). The menu's xbox_MenuAdd is stood in for, to catch the row.
 * Each settings case is its own run, as the setting is read once; run.py
 * writes the settings file and checks the log and the file.
 *
 * The game's side is a guest memory of the test's own, laid out as
 * soul_indicator.h says the game's is: a mission flow, its script and its
 * triggers, the flag manager's banks, and a course's object and record.
 *   1. The reader: Shibuya Terminal's platforms (13 touches, each setting a
 *      mission flag, one touch clearing them all, and the award over the
 *      13), with an op 0x20 record that awards nothing beside it; a course
 *      (op 0x11); an award from a touch alone, and sheets with no touch, in
 *      neither of which is there progress to show; a mission with no
 *      triggers; and what the game never holds, each a check once.
 *   2. The state through runs of frames: in at the first progress the game
 *      can fail (for a course, past a first step with no fail boxes), a
 *      pulse for each new sheet (not for a course's steps), failing when the
 *      sheets are all cleared, or a course's progress drops to nothing with
 *      or without its failed flag seen, not when it is done, then red, held
 *      and out; the award, out without the red; the challenge over some
 *      other way; and in again after a failure.
 *   3. The quad's place, size, alpha and colours at each point of each of
 *      those.
 *   4. The hooks' side: the mission read once at its setup's first frame,
 *      dropped at its hand-over, a new mission read afresh, a course's moves
 *      logged, and nothing drawn with the setting off; run.py checks the
 *      [SOUL] lines.
 *   5. The draw's side: the quad's four vertices as the HUD's quad batches
 *      take them, and the badge drawn with the gauge's first draw in each
 *      pass of the game's loop, not again in that pass.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "soul_indicator.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-4)

static XBOX_MENU_ITEM s_row;
static int s_rows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    if (count > 0) s_row = items[0];
    s_rows += count;
}

/* ---- the guest memory ------------------------------------------------------ */
#define TOP       0x00400000u
#define FLOW      0x00300000u
#define FLOW_2    0x00302000u   /* the next mission's flow */
#define SCRIPT    0x00310000u
#define TRIGGERS  0x00320000u
#define LISTS     0x00330000u   /* the triggers' flag lists, one after another */
#define COURSES   0x00340000u   /* the flow's courses: a pointer each */
#define COURSE    0x00340100u   /* a course object */
#define RECORD    0x00340200u   /* its record for player 0 */
#define ENTRIES   0x00340300u   /* the script's courses: -> its steps, how many */
#define STEPS     0x00340400u   /* course 0's steps */
static uint8_t *mem;
static uint32_t s_lists, s_triggers;

static void put32(uint32_t va, uint32_t v) { memcpy(mem + va, &v, 4); }
static uint32_t get32(uint32_t va) { uint32_t v; memcpy(&v, mem + va, 4); return v; }

/* A flag word, as the game's script holds them. */
static uint32_t flag(uint32_t bank, uint32_t index, uint32_t value)
{
    return bank | index << 3 | value << 19;
}
#define MF(i, v) flag(0, i, v)
#define GF(i, v) flag(2, i, v)
#define RT(i, v) flag(3, i, v)

/* The flag manager's banks, at the game's offsets. */
static void set_flag(uint32_t word)
{
    static const uint32_t bank[4] = { 0x6A14u, 0x18u, 0x58u, 0x98u };
    uint32_t i = SOUL_FLAG_INDEX(word), at = SOUL_FLAGS + bank[SOUL_FLAG_BANK(word)] + (i >> 5) * 4;
    uint32_t w = get32(at);
    put32(at, SOUL_FLAG_VALUE(word) ? w | 1u << (i & 31) : w & ~(1u << (i & 31)));
}

static uint32_t list(const uint32_t *words, unsigned n)
{
    uint32_t va = s_lists;
    unsigned k;
    for (k = 0; k < n; k++) put32(va + 4 * k, words[k]);
    s_lists += 4 * n + 4;
    return va;
}

/* A mission with no triggers yet, its flow at flow in state 14. */
static void mission(uint32_t flow, uint32_t id)
{
    memset(mem + SCRIPT, 0, 0x200);
    memset(mem + TRIGGERS, 0, 0x10000);
    memset(mem + SOUL_FLAGS, 0, 0x6A14 + 0x40);
    s_lists = LISTS;
    s_triggers = 0;
    put32(flow + SOUL_FLOW_MISSION, id);
    put32(flow + SOUL_FLOW_STATE, SOUL_STATE_SETUP);
    put32(flow + SOUL_FLOW_SCRIPT, SCRIPT);
    put32(flow + SOUL_FLOW_COURSES, 0);
    put32(SCRIPT + SOUL_SCRIPT_TRIGGERS, TRIGGERS);
}

static void trigger(uint32_t op, const uint32_t *cond, unsigned ncond, const uint32_t *sets,
                    unsigned nsets, uint32_t arg0, uint32_t arg1)
{
    uint32_t t = TRIGGERS + s_triggers * SOUL_TRIGGER_SIZE;
    put32(t + SOUL_TRIGGER_IF, ncond ? list(cond, ncond) : 0);
    put32(t + SOUL_TRIGGER_IF + 4, ncond);
    put32(t + SOUL_TRIGGER_SETS, nsets ? list(sets, nsets) : 0);
    put32(t + SOUL_TRIGGER_SETS + 4, nsets);
    put32(t + SOUL_TRIGGER_OP, op);
    put32(t + SOUL_TRIGGER_ARGS, arg0);
    put32(t + SOUL_TRIGGER_ARGS + 4, arg1);
    put32(SCRIPT + SOUL_SCRIPT_TRIGGERS + 4, ++s_triggers);
}

/* Shibuya Terminal's platforms, as mssn0910 has them. */
static const uint32_t k_platform[13] = {
    0x33, 0x34, 0x35, 0x36, 0x37, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x47, 0x48, 0x49
};
static void platforms(uint32_t flow)
{
    uint32_t cond[16], clear[13], sets[2];
    unsigned k;
    mission(flow, 0x0009038Eu);     /* 910 */
    cond[0] = GF(0x14, 0);
    cond[1] = RT(0x24, 1);
    cond[2] = RT(0xC4, 0);
    for (k = 0; k < 13; k++) {
        uint32_t set = MF(k_platform[k], 1);
        trigger(SOUL_OP_TOUCH, cond, 3, &set, 1, 0, 6 + k);
        clear[k] = MF(k_platform[k], 0);
        cond[3 + k] = MF(k_platform[k], 1);
    }
    trigger(SOUL_OP_TOUCH, cond, 3, clear, 13, 0, 19);
    sets[0] = RT(0xC4, 1);
    sets[1] = MF(0x63, 1);
    trigger(SOUL_OP_SHEETS, cond, 16, sets, 2, 0, 0);
    /* Another op 0x20 record, which awards nothing in the notebook. */
    sets[0] = MF(0x70, 1);
    trigger(SOUL_OP_SHEETS, cond, 3, sets, 1, 0, 0);
    set_flag(RT(0x24, 1));      /* unlocked by the tape */
}

/* A course, as mssn0925 has one: started by op 0x6A's MF[5C], awarded by op
 * 0x11 for course 0, player 0; its three steps a grind with fail boxes, a
 * state in a box with fail boxes, and the end. With first_fails 0, its
 * first step has none, as mssn0915's (Chuo street's second on its first
 * rail). */
static void course(uint32_t flow, uint32_t first_fails)
{
    static const uint32_t kind[3] = { 1, 8, 9 };
    uint32_t cond[3] = { MF(0x5C, 1), RT(0xC5, 0), GF(0x14, 0) }, award = RT(0xC5, 1);
    unsigned k;
    mission(flow, first_fails ? 0x0009039Du : 0x00090393u);     /* 925, 915 */
    trigger(0x12, cond, 1, NULL, 0, 0, 0);      /* clears failed */
    trigger(SOUL_OP_COURSE, cond, 3, &award, 1, 0, 0);
    put32(SCRIPT + SOUL_SCRIPT_COURSES, ENTRIES);
    put32(SCRIPT + SOUL_SCRIPT_COURSES + 4, 1);
    memset(mem + COURSES, 0, 0x500);
    put32(ENTRIES, STEPS);
    put32(ENTRIES + 4, 3);
    for (k = 0; k < 3; k++) {
        put32(STEPS + k * SOUL_STEP_SIZE, kind[k]);
        put32(STEPS + k * SOUL_STEP_SIZE + SOUL_STEP_FAILS, k == 0 ? first_fails : k == 1 ? 3 : 0);
    }
    put32(COURSE + SOUL_COURSE_STEPS, ENTRIES);
    put32(COURSE + SOUL_COURSE_PLAYERS, RECORD);
}
#define FAIL_BOXES 46u          /* mssn0925's first step's */
#define HALF       0x3F000000u  /* 0.5f: a record's time or amount */

/* The course's record: step, timer, amount; done and failed. */
static void record(uint32_t step, uint32_t timer, uint32_t amount, uint32_t done, uint32_t failed)
{
    put32(RECORD + SOUL_RECORD_STEP, step);
    put32(RECORD + SOUL_RECORD_TIMER, timer);
    put32(RECORD + SOUL_RECORD_AMOUNT, amount);
    put32(RECORD + SOUL_RECORD_DONE, done);
    put32(RECORD + SOUL_RECORD_FAILED, failed);
}

/* ---- 1. the reader ------------------------------------------------------------ */
static void check_read(void)
{
    SoulChallenge c;
    uint32_t sets = RT(0xC6, 1), cond[2] = { MF(0x50, 1), GF(0x14, 0) };
    unsigned k;
    platforms(FLOW);
    CHECK(soul_indicator_read(mem, TOP, FLOW, &c));
    CHECK(c.kind == SOUL_SHEETS && c.trigger == 14 && !c.others && c.flow == FLOW);
    CHECK(c.mission == 0x0009038Eu && c.script == SCRIPT && c.award == RT(0xC4, 1));
    CHECK(c.nsheets == 13 && c.nif == 3);
    for (k = 0; k < 13; k++) CHECK(c.sheet[k] == MF(k_platform[k], 1));
    CHECK(c.cond[0] == GF(0x14, 0) && c.cond[1] == RT(0x24, 1) && c.cond[2] == RT(0xC4, 0));

    course(FLOW, FAIL_BOXES);
    CHECK(soul_indicator_read(mem, TOP, FLOW, &c));
    CHECK(c.kind == SOUL_COURSE && c.trigger == 1 && c.award == RT(0xC5, 1));
    CHECK(c.nif == 3 && c.nsheets == 0 && c.player == 0 && c.course == 0);

    /* An award from one touch (Kibogaoka Hill's high sheet): none. */
    mission(FLOW, 0x000903ACu);     /* 940 */
    trigger(SOUL_OP_TOUCH, cond + 1, 1, &sets, 1, 0, 5);
    CHECK(!soul_indicator_read(mem, TOP, FLOW, &c) && c.kind == SOUL_NONE);
    /* Sheets no touch sets: no progress to show, so none. */
    trigger(SOUL_OP_SHEETS, cond, 2, &sets, 1, 0, 0);
    CHECK(!soul_indicator_read(mem, TOP, FLOW, &c) && c.kind == SOUL_NONE);
    /* No triggers. */
    mission(FLOW, 0x000903A7u);     /* 935 */
    CHECK(!soul_indicator_read(mem, TOP, FLOW, &c) && c.kind == SOUL_NONE);

    /* Two awards: the first, and the other counted. */
    course(FLOW, FAIL_BOXES);
    trigger(SOUL_OP_COURSE, cond, 1, &sets, 1, 0, 0);
    CHECK(soul_indicator_read(mem, TOP, FLOW, &c) && c.trigger == 1 && c.others == 1);

    /* What the game never holds: each a [CHECK] line, once (run.py). */
    CHECK(!soul_indicator_read(mem, TOP, TOP - 8, &c));
    course(FLOW, FAIL_BOXES);
    put32(FLOW + SOUL_FLOW_SCRIPT, 0);
    CHECK(!soul_indicator_read(mem, TOP, FLOW, &c));
    course(FLOW, FAIL_BOXES);
    put32(SCRIPT + SOUL_SCRIPT_TRIGGERS + 4, 5000);
    CHECK(!soul_indicator_read(mem, TOP, FLOW, &c));
    course(FLOW, FAIL_BOXES);
    put32(TRIGGERS + SOUL_TRIGGER_SIZE + SOUL_TRIGGER_IF + 4, 200);
    CHECK(!soul_indicator_read(mem, TOP, FLOW, &c));
    printf("soul_indicator: the reader\n");
}

/* ---- 2 and 3. the state, and the quad ------------------------------------------ */
static int step(SoulShow *show, const SoulChallenge *c, double now)
{
    SoulSample s;
    CHECK(soul_indicator_sample(mem, TOP, c, &s));
    return soul_indicator_step(show, c->kind, &s, now);
}

/* The quad at now: its edges, alpha and colours. */
static void quad_is(const SoulShow *show, double now, float x0, float y0, float x1, float y1,
                    uint32_t colour, uint32_t red)
{
    SoulQuad q;
    CHECK(soul_indicator_quad(show, now, &q));
    if (!(NEAR(q.x0, x0) && NEAR(q.y0, y0) && NEAR(q.x1, x1) && NEAR(q.y1, y1)
          && q.colour == colour && q.red_colour == red)) {
        fprintf(stderr, "quad at %.3f: %.3f %.3f %.3f %.3f %08X %08X\n", now, q.x0, q.y0, q.x1,
                q.y1, q.colour, q.red_colour);
        CHECK(0);
    }
}

/* Its size, k times the badge's, about its centre (73, 414.5). */
static void sized(const SoulShow *show, double now, float k, uint32_t colour, uint32_t red)
{
    float half = 20.0f * k;
    quad_is(show, now, 73.0f - half, 414.5f - half, 73.0f + half, 414.5f + half, colour, red);
}

static void check_sheets(void)
{
    SoulChallenge c;
    SoulShow show;
    SoulQuad q;
    unsigned k;
    platforms(FLOW);
    CHECK(soul_indicator_read(mem, TOP, FLOW, &c));
    soul_indicator_reset(&show);
    /* No platform yet: nothing. */
    CHECK(step(&show, &c, 10.0) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    CHECK(!soul_indicator_quad(&show, 10.0, &q));
    /* The first: in, fading in from nothing, shrinking from 115%. */
    set_flag(MF(0x33, 1));
    CHECK(step(&show, &c, 11.0) == SOUL_SHOWN && show.phase == SOUL_IN);
    CHECK(!soul_indicator_quad(&show, 11.0, &q));
    sized(&show, 11.125, 1.075f, 0x8080FF80u, 0x00FF0000u);
    /* On track: its size, 53 from the left edge, level with the gauge. */
    CHECK(step(&show, &c, 11.3) == SOUL_SAME && show.phase == SOUL_ON);
    quad_is(&show, 11.3, 53.0f, 394.5f, 93.0f, 434.5f, 0xFF80FF80u, 0x00FF0000u);
    /* A new one: a pulse, up to 120% at its middle and back. */
    set_flag(MF(0x34, 1));
    CHECK(step(&show, &c, 12.0) == SOUL_PULSED);
    sized(&show, 12.075, 1.1f, 0xFF80FF80u, 0x00FF0000u);
    sized(&show, 12.15, 1.2f, 0xFF80FF80u, 0x00FF0000u);
    sized(&show, 12.225, 1.1f, 0xFF80FF80u, 0x00FF0000u);
    CHECK(step(&show, &c, 12.4) == SOUL_SAME && show.pulse_at < 0.0);
    sized(&show, 12.4, 1.0f, 0xFF80FF80u, 0x00FF0000u);
    /* The same count again: nothing new. */
    CHECK(step(&show, &c, 12.5) == SOUL_SAME);
    /* The ground clears them: failed. Red over 0.15 s, to SOUL_RED_MOST. */
    for (k = 0; k < 13; k++) set_flag(MF(k_platform[k], 0));
    CHECK(step(&show, &c, 13.0) == SOUL_FAILED && show.phase == SOUL_FAILING);
    sized(&show, 13.075, 1.0f, 0xFF80FF80u, 0x40FF0000u);
    sized(&show, 13.5, 1.0f, 0xFF80FF80u, 0x80FF0000u);
    /* Held a second, then out over 0.25 s, growing to 115%, still red. */
    CHECK(step(&show, &c, 14.1) == SOUL_SAME && show.phase == SOUL_FAILING);
    CHECK(step(&show, &c, 14.2) == SOUL_SAME && show.phase == SOUL_OUT && show.red);
    sized(&show, 14.275, 1.075f, 0x8080FF80u, 0x40FF0000u);
    CHECK(step(&show, &c, 14.5) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    CHECK(!soul_indicator_quad(&show, 14.5, &q));
    /* Again, and all thirteen: then the award, out without the red. */
    set_flag(MF(0x40, 1));
    CHECK(step(&show, &c, 15.0) == SOUL_SHOWN);
    for (k = 0; k < 13; k++) set_flag(MF(k_platform[k], 1));
    CHECK(step(&show, &c, 16.0) == SOUL_PULSED);
    set_flag(RT(0xC4, 1));
    set_flag(MF(0x63, 1));
    CHECK(step(&show, &c, 17.0) == SOUL_DONE && show.phase == SOUL_OUT && !show.red);
    sized(&show, 17.125, 1.075f, 0x8080FF80u, 0x00FF0000u);
    CHECK(step(&show, &c, 17.3) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    /* Awarded, it stays out whatever the flags do. */
    for (k = 0; k < 13; k++) set_flag(MF(k_platform[k], 0));
    set_flag(MF(0x33, 1));
    CHECK(step(&show, &c, 18.0) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    printf("soul_indicator: the platforms\n");
}

static void check_course(void)
{
    SoulChallenge c;
    SoulShow show;
    SoulQuad q;
    course(FLOW, FAIL_BOXES);
    CHECK(soul_indicator_read(mem, TOP, FLOW, &c));
    soul_indicator_reset(&show);
    /* Before the setup makes the course: nothing, and no check. */
    CHECK(step(&show, &c, 20.0) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    put32(FLOW + SOUL_FLOW_COURSES, COURSES);
    CHECK(step(&show, &c, 20.1) == SOUL_SAME);
    put32(COURSES, COURSE);
    /* Progress before the start detector's MF[5C]: not under way. */
    record(1, 0, 0, 0, 0);
    CHECK(step(&show, &c, 20.2) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    set_flag(MF(0x5C, 1));
    CHECK(step(&show, &c, 21.0) == SOUL_SHOWN);
    /* The next step: no pulse for a course. */
    record(2, 30, 0, 0, 0);
    CHECK(step(&show, &c, 21.5) == SOUL_SAME && show.phase == SOUL_ON);
    CHECK(show.pulse_at < 0.0);
    /* A fail box: failed set, the rest to 0. */
    record(0, 0, 0, 0, 1);
    CHECK(step(&show, &c, 22.0) == SOUL_FAILED);
    CHECK(step(&show, &c, 23.2) == SOUL_SAME && show.phase == SOUL_OUT);
    CHECK(step(&show, &c, 23.5) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    /* Failed already cleared by the detector's pass (op 0x12) when the
     * frame is read: the drop alone. And failed still set while the
     * progress shows: no progress. */
    record(0, 0, 0, 0, 0);
    CHECK(step(&show, &c, 23.6) == SOUL_SAME);
    record(0, 12, 0, 0, 0);
    CHECK(step(&show, &c, 24.0) == SOUL_SHOWN);
    record(0, 0, 0, 0, 0);
    CHECK(step(&show, &c, 24.5) == SOUL_FAILED);
    record(1, 0, 0, 0, 1);
    CHECK(step(&show, &c, 24.6) == SOUL_SAME && show.phase == SOUL_FAILING);
    /* Again while red: in again. */
    record(0, 0, 5, 0, 0);
    CHECK(step(&show, &c, 24.7) == SOUL_SHOWN && show.phase == SOUL_IN && !show.red);
    /* Done, the award to come: its progress back to 0 is no failure. */
    record(0, 0, 0, 1, 0);
    CHECK(step(&show, &c, 25.0) == SOUL_SAME && show.phase == SOUL_ON);
    set_flag(RT(0xC5, 1));
    record(0, 0, 0, 0, 0);
    CHECK(step(&show, &c, 25.1) == SOUL_DONE && !show.red);
    CHECK(soul_indicator_quad(&show, 25.2, &q) && q.red == 0.0f);
    /* Over another way (the mission's flags gone): out, without the red. */
    set_flag(RT(0xC5, 0));
    soul_indicator_reset(&show);
    record(3, 0, 0, 0, 0);
    CHECK(step(&show, &c, 26.0) == SOUL_SHOWN);
    set_flag(GF(0x14, 1));
    CHECK(step(&show, &c, 26.5) == SOUL_ENDED && show.phase == SOUL_OUT && !show.red);
    set_flag(GF(0x14, 0));
    /* What the game never holds: a course past the script's count, and a
     * record outside memory. Each a [CHECK] line, once (run.py); nothing. */
    c.course = 3;
    CHECK(step(&show, &c, 27.0) == SOUL_SAME);
    c.course = 0;
    put32(COURSE + SOUL_COURSE_PLAYERS, TOP - 4);
    record(0, 0, 0, 0, 0);
    CHECK(step(&show, &c, 27.5) == SOUL_SAME);

    /* Chuo street's (mssn0915): its first step, a second on the first rail,
     * has no fail boxes, so the game can't fail it and its time adds up
     * over tries. Nothing shows while it is under way, nor when the skater
     * falls off and the time is kept; the next step shows the badge, and
     * the game's fail there turns it red (spriteice, 11:42 UTC). */
    course(FLOW, 0);
    CHECK(soul_indicator_read(mem, TOP, FLOW, &c) && c.kind == SOUL_COURSE);
    put32(FLOW + SOUL_FLOW_COURSES, COURSES);
    put32(COURSES, COURSE);
    set_flag(MF(0x5C, 1));
    soul_indicator_reset(&show);
    record(0, HALF, 0, 0, 0);
    CHECK(step(&show, &c, 30.0) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    CHECK(step(&show, &c, 31.0) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    record(1, 0, 0, 0, 0);
    CHECK(step(&show, &c, 32.0) == SOUL_SHOWN);
    record(0, 0, 0, 0, 1);
    CHECK(step(&show, &c, 33.0) == SOUL_FAILED);
    CHECK(step(&show, &c, 34.5) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    /* Its amount, as its time. */
    record(0, 0, HALF, 0, 0);
    CHECK(step(&show, &c, 35.0) == SOUL_SAME && show.phase == SOUL_HIDDEN);
    /* Its steps not where the game keeps them: a [CHECK] line, once
     * (run.py), and any progress shows, as the badge did before it read
     * them. */
    put32(COURSE + SOUL_COURSE_STEPS, 0);
    CHECK(step(&show, &c, 36.0) == SOUL_SHOWN);
    printf("soul_indicator: the course\n");
}

/* ---- 4. the hooks' side ------------------------------------------------------- */
static void check_hooks(void)
{
    SoulQuad q;
    unsigned k;
    /* The platforms' mission at its setup: read once, however many frames
     * the setup takes (run.py counts the line). */
    platforms(FLOW);
    soul_indicator_flow(mem, TOP, FLOW, 1.0);
    soul_indicator_flow(mem, TOP, FLOW, 1.1);
    put32(FLOW + SOUL_FLOW_STATE, 15);
    soul_indicator_flow(mem, TOP, FLOW, 1.2);
    CHECK(!soul_indicator_frame(mem, TOP, 2.0, &q));
    set_flag(MF(0x33, 1));
    CHECK(!soul_indicator_frame(mem, TOP, 3.0, &q));       /* in, from nothing */
    CHECK(soul_indicator_frame(mem, TOP, 3.125, &q) && q.colour == 0x8080FF80u);
    set_flag(MF(0x34, 1));
    CHECK(soul_indicator_frame(mem, TOP, 4.0, &q));
    for (k = 0; k < 13; k++) set_flag(MF(k_platform[k], 0));
    CHECK(soul_indicator_frame(mem, TOP, 5.0, &q));
    CHECK(soul_indicator_frame(mem, TOP, 5.5, &q) && q.red_colour == 0x80FF0000u);
    /* Handing over to the next mission: dropped. */
    put32(FLOW + SOUL_FLOW_STATE, SOUL_STATE_NEXT);
    soul_indicator_flow(mem, TOP, FLOW, 6.0);
    set_flag(MF(0x35, 1));
    CHECK(!soul_indicator_frame(mem, TOP, 6.5, &q));
    /* The next mission, a course, read afresh at its own setup; the old
     * flow's last steps don't drop it. */
    course(FLOW_2, FAIL_BOXES);
    soul_indicator_flow(mem, TOP, FLOW_2, 7.0);
    soul_indicator_flow(mem, TOP, FLOW, 7.0);
    put32(FLOW_2 + SOUL_FLOW_STATE, 15);
    soul_indicator_flow(mem, TOP, FLOW_2, 7.1);
    put32(FLOW_2 + SOUL_FLOW_COURSES, COURSES);
    put32(COURSES, COURSE);
    set_flag(MF(0x5C, 1));
    record(1, 0, 0, 0, 0);
    CHECK(!soul_indicator_frame(mem, TOP, 8.0, &q));
    CHECK(soul_indicator_frame(mem, TOP, 8.5, &q) && q.colour == 0xFF80FF80u);
    /* Its moves, each said once (run.py): failed by the game, under way
     * again in its first step, on to the next, and done. */
    record(0, 0, 0, 0, 1);
    CHECK(soul_indicator_frame(mem, TOP, 8.6, &q));
    record(0, HALF, 0, 0, 0);
    soul_indicator_frame(mem, TOP, 8.7, &q);
    record(1, 0, 0, 0, 0);
    soul_indicator_frame(mem, TOP, 8.8, &q);
    record(1, 0, 0, 1, 0);
    soul_indicator_frame(mem, TOP, 8.9, &q);
    set_flag(RT(0xC5, 1));
    CHECK(soul_indicator_frame(mem, TOP, 9.0, &q) && q.red == 0.0f);
    /* A mission with nothing to show. */
    mission(FLOW, 0x000903A7u);     /* 935 */
    soul_indicator_flow(mem, TOP, FLOW, 10.0);
    CHECK(!soul_indicator_frame(mem, TOP, 10.5, &q));
    printf("soul_indicator: the hooks\n");
}

/* The quad's vertices, written where the draw's scratch would be, with
 * nothing written past them. */
#define VERTICES  0x00350000u
static float getf(uint32_t va) { float v; memcpy(&v, mem + va, 4); return v; }
static void check_vertices(void)
{
    const SoulQuad q = { .x0 = 53.0f, .y0 = 394.5f, .x1 = 93.0f, .y1 = 434.5f, .alpha = 0.5f,
                         .colour = 0x80FFFFFFu, .red_colour = 0x33FF0000u };
    unsigned k;
    put32(VERTICES + SOUL_QUAD_BYTES, 0xA5A5A5A5u);
    soul_indicator_vertices(mem, VERTICES, &q, q.red_colour);
    for (k = 0; k < 4; k++) {
        uint32_t v = VERTICES + k * SOUL_VERTEX;
        /* (x0, y0), (x1, y0), (x0, y1), (x1, y1), the texture's corners with them. */
        CHECK(getf(v + SOUL_VERTEX_X) == (k & 1u ? 93.0f : 53.0f));
        CHECK(getf(v + SOUL_VERTEX_Y) == (k & 2u ? 434.5f : 394.5f));
        CHECK(get32(v + SOUL_VERTEX_DIFFUSE) == 0x33FF0000u);
        CHECK(get32(v + SOUL_VERTEX_SPECULAR) == SOUL_SPECULAR);
        CHECK(getf(v + SOUL_VERTEX_U) == (k & 1u ? 1.0f : 0.0f));
        CHECK(getf(v + SOUL_VERTEX_V) == (k & 2u ? 1.0f : 0.0f));
    }
    CHECK(get32(VERTICES + SOUL_QUAD_BYTES) == 0xA5A5A5A5u);
    soul_indicator_vertices(mem, VERTICES, &q, q.colour);
    CHECK(get32(VERTICES + 3 * SOUL_VERTEX + SOUL_VERTEX_DIFFUSE) == 0x80FFFFFFu);
    printf("soul_indicator: the vertices\n");
}

/* Once a pass of the game's loop: with the gauge's first draw in it, on
 * whichever of the walk's passes that is. run.py checks the two lines. */
static void check_once(void)
{
    CHECK(soul_indicator_once(1, 0));
    CHECK(!soul_indicator_once(1, 3));
    CHECK(!soul_indicator_once(1, 5));
    CHECK(soul_indicator_once(2, 0));
    CHECK(soul_indicator_once(3, 3));
    CHECK(!soul_indicator_once(3, 0));
    CHECK(!soul_indicator_once(3, 8));
    printf("soul_indicator: once a pass\n");
}

/* With the setting off, nothing, whatever the challenge does. */
static void check_off(void)
{
    SoulQuad q;
    /* Still once a pass, and nothing said. */
    CHECK(soul_indicator_once(1, 0));
    CHECK(!soul_indicator_once(1, 3));
    platforms(FLOW);
    soul_indicator_flow(mem, TOP, FLOW, 1.0);
    put32(FLOW + SOUL_FLOW_STATE, 15);
    set_flag(MF(0x33, 1));
    CHECK(!soul_indicator_frame(mem, TOP, 2.0, &q));
    CHECK(!soul_indicator_frame(mem, TOP, 3.0, &q));
    printf("soul_indicator: off draws nothing\n");
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    soul_indicator_start();
    /* One row: a toggle in Gameplay > Graffiti souls, showing the setting. */
    CHECK(s_rows == 1);
    CHECK(s_row.menu == XBOX_MENU_GAMEPLAY && s_row.kind == XBOX_ITEM_TOGGLE && !s_row.last && !s_row.under);
    CHECK(s_row.group && !strcmp(s_row.group, XBOX_GROUP_SOULS));
    CHECK(!strcmp(s_row.label, "Visual indicator") && !s_row.confirm);
    /* A hint, a sentence short enough for the strip (menu.h). */
    CHECK(s_row.hint && strlen(s_row.hint) <= MENU_HINT_LONGEST && s_row.hint[strlen(s_row.hint) - 1] == '.');
    CHECK(s_row.state() == soul_indicator_on());
    /* File > Presets: off in each, and JSRF-PC default's and Max quality's
     * are what a start with no soulindicator= gives. */
    CHECK(s_row.presets == XBOX_PRESETS_SET && !s_row.keep && s_row.preset[XBOX_PRESET_XBOX] == 0);
    CHECK(s_row.preset[XBOX_PRESET_JSRF_PC] == 0 && s_row.preset[XBOX_PRESET_MAX] == 0);
    if (!strcmp(c, "default")) CHECK(!soul_indicator_on() && s_row.preset[XBOX_PRESET_JSRF_PC] == soul_indicator_on());
    if (!strcmp(c, "off")) CHECK(!soul_indicator_on());
    if (!strcmp(c, "on")) CHECK(soul_indicator_on());
    if (!strcmp(c, "flip")) {
        /* Off, then the row: on at once (and kept, which run.py checks). */
        CHECK(!soul_indicator_on());
        s_row.run();
        CHECK(soul_indicator_on() && s_row.state());
    }
    if (!strcmp(c, "game") || !strcmp(c, "default")) {
        mem = calloc(1, TOP);
        CHECK(mem != NULL);
        if (!strcmp(c, "game")) {
            CHECK(soul_indicator_on());
            check_read();
            check_sheets();
            check_course();
            check_hooks();
            check_vertices();
            check_once();
        } else {
            check_off();
        }
        free(mem);
    }
    printf("soul_indicator: %s\n", c);
    return 0;
}
