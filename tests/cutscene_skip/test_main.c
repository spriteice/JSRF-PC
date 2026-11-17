/* Run with: python tests/cutscene_skip/run.py
 *
 * Skip cutscenes without the game: port/cutscene_skip.c as it is, built with
 * the in-game menu, reading and keeping skipcutscenes= through the engine's
 * own settings code (taken from src/nv2a_gl/nv2a_gl.c by run.py). The menu's
 * xbox_MenuAdd is stood in for, to catch the row. The presses, the prompt's
 * fades and its places are driven with made-up times and scenes, and a try
 * of the prompt with no scene (JSRF_SKIP_PROMPT_AT's), and a skater
 * preview's own ends found on made-up events lists. Each case is
 * its own run, as the setting is read once; run.py writes the settings file
 * and checks the log and the file.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "cutscene_skip.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-4)

#define SCENE   0x00A00000u
#define SCENE_2 0x00A10000u
#define FRAME   (1.0 / 60.0)

static XBOX_MENU_ITEM s_row;
static int s_rows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    if (count > 0) s_row = items[0];
    s_rows += count;
}

/* Frames of the scene with nothing pressed, from *t to until: none ends it. */
static void play(uint32_t scene, double *t, double until)
{
    for (; *t < until; *t += FRAME)
        CHECK(!cutscene_skip_frame(scene, 0, *t));
}

static void row(void)
{
    /* One row: a toggle in Display > Skips, showing the setting. */
    CHECK(s_rows == 1);
    CHECK(s_row.menu == XBOX_MENU_DISPLAY && s_row.kind == XBOX_ITEM_TOGGLE && !s_row.last && !s_row.under);
    CHECK(s_row.group && !strcmp(s_row.group, XBOX_GROUP_SKIPS));
    CHECK(!strcmp(s_row.label, "Skip cutscenes") && !s_row.confirm);
    /* A hint, a sentence short enough for the strip, naming no key (docs/menu-format.md). */
    CHECK(s_row.hint && strlen(s_row.hint) <= MENU_HINT_LONGEST && s_row.hint[strlen(s_row.hint) - 1] == '.');
    CHECK(!strcmp(s_row.hint, "Skips a cutscene when the jump button is pressed twice."));
    CHECK(s_row.state() == cutscene_skip_on());
    /* File > Presets: off in Xbox mode, and JSRF-PC default's and Max
     * quality's are what a start with no skipcutscenes= gives. */
    CHECK(s_row.presets == XBOX_PRESETS_SET && !s_row.keep && s_row.preset[XBOX_PRESET_XBOX] == 0);
    CHECK(s_row.preset[XBOX_PRESET_MAX] == s_row.preset[XBOX_PRESET_JSRF_PC]);
}

/* Off: presses show nothing and end nothing. */
static void off(void)
{
    float a, s;
    double t = 10.0;
    CHECK(!cutscene_skip_on());
    CHECK(!cutscene_skip_frame(SCENE, 1, t));
    CHECK(!cutscene_skip_prompt(t, &a, &s));
    CHECK(!cutscene_skip_frame(SCENE, 1, t + FRAME));
    CHECK(!cutscene_skip_prompt(t + FRAME, &a, &s));
}

/* A try (JSRF_SKIP_PROMPT_AT), on or off: the prompt shows with no scene,
 * fading in, all there, and out, for its whole while, then not. */
static void try_prompt(void)
{
    float a, s;
    cutscene_skip_try(20.0);
    CHECK(cutscene_skip_prompt(20.0, &a, &s) && NEAR(a, 0.0f) && NEAR(s, CUTSCENE_SKIP_GROW));
    CHECK(cutscene_skip_prompt(21.0, &a, &s) && NEAR(a, 1.0f) && NEAR(s, 1.0f));
    CHECK(cutscene_skip_prompt(20.0 + CUTSCENE_SKIP_WINDOW + CUTSCENE_SKIP_FADE * 0.5, &a, &s) && NEAR(a, 0.5f));
    CHECK(!cutscene_skip_prompt(20.0 + CUTSCENE_SKIP_WINDOW + CUTSCENE_SKIP_FADE + FRAME, &a, &s));
}

static void presses(void)
{
    float a, s;
    double t = 100.0, first;
    CHECK(cutscene_skip_on());
    /* Nothing shows until A is pressed. */
    play(SCENE, &t, 101.0);
    CHECK(!cutscene_skip_prompt(t, &a, &s));
    /* The first press: the prompt, which fades in while shrinking. */
    first = t;
    CHECK(!cutscene_skip_frame(SCENE, 1, t));
    CHECK(cutscene_skip_prompt(t, &a, &s) && NEAR(a, 0.0f) && NEAR(s, CUTSCENE_SKIP_GROW));
    t += FRAME;
    play(SCENE, &t, first + CUTSCENE_SKIP_FADE / 2);
    CHECK(cutscene_skip_prompt(t, &a, &s));
    CHECK(NEAR(a, (t - first) / CUTSCENE_SKIP_FADE)
          && NEAR(s, CUTSCENE_SKIP_GROW - (CUTSCENE_SKIP_GROW - 1.0f) * a));
    play(SCENE, &t, first + 1.0);
    CHECK(cutscene_skip_prompt(t, &a, &s) && NEAR(a, 1.0f) && NEAR(s, 1.0f));
    /* The second press within the time: the scene ends, and the prompt
     * goes with it. */
    CHECK(cutscene_skip_frame(SCENE, 1, t));
    CHECK(!cutscene_skip_prompt(t, &a, &s));
    /* That press isn't another's first, in the same frame (an end's flags
     * can start another preview then); a press after it is. */
    CHECK(!cutscene_skip_frame(SCENE_2, 1, t));
    CHECK(!cutscene_skip_prompt(t, &a, &s));
    CHECK(!cutscene_skip_frame(SCENE_2, 1, t + CUTSCENE_SKIP_GONE + FRAME));
    CHECK(cutscene_skip_prompt(t + CUTSCENE_SKIP_GONE + FRAME, &a, &s));
    CHECK(!cutscene_skip_prompt(t + 2.0 * CUTSCENE_SKIP_GONE + 3.0 * FRAME, &a, &s));

    /* Another scene: its first press only shows the prompt. */
    t += 5.0;
    CHECK(!cutscene_skip_frame(SCENE_2, 1, t));
    first = t;
    t += FRAME;
    /* After the time, the prompt fades out while growing, and a press
     * then shows it again rather than ending the scene. */
    play(SCENE_2, &t, first + CUTSCENE_SKIP_WINDOW - FRAME);
    CHECK(cutscene_skip_prompt(t, &a, &s) && NEAR(a, 1.0f) && NEAR(s, 1.0f));
    play(SCENE_2, &t, first + CUTSCENE_SKIP_WINDOW + CUTSCENE_SKIP_FADE / 2);
    CHECK(cutscene_skip_prompt(t, &a, &s));
    CHECK(NEAR(a, 1.0 - (t - first - CUTSCENE_SKIP_WINDOW) / CUTSCENE_SKIP_FADE)
          && NEAR(s, 1.0f + (CUTSCENE_SKIP_GROW - 1.0f) * (1.0f - a)));
    play(SCENE_2, &t, first + CUTSCENE_SKIP_WINDOW + CUTSCENE_SKIP_FADE + FRAME);
    CHECK(!cutscene_skip_prompt(t, &a, &s));
    CHECK(!cutscene_skip_frame(SCENE_2, 1, t));
    CHECK(cutscene_skip_prompt(t, &a, &s) && NEAR(a, 0.0f));

    /* A press in one scene and the next press in another: a first press. */
    t += FRAME;
    CHECK(!cutscene_skip_frame(SCENE, 1, t));
    CHECK(cutscene_skip_prompt(t, &a, &s));

    /* The scene ends by itself while the prompt shows: no more frames, and
     * the prompt goes. */
    CHECK(!cutscene_skip_prompt(t + CUTSCENE_SKIP_GONE + FRAME, &a, &s));
    CHECK(!cutscene_skip_prompt(t + CUTSCENE_SKIP_GONE + 2 * FRAME, &a, &s));
}

/* The places: the line's right end at CUTSCENE_SKIP_RIGHT at its own size,
 * its middle still at any size, the A in the cap's middle, the cap bigger
 * than the text. */
static void places(void)
{
    const float width[CUTSCENE_SKIP_PIECES] = { 50.0f, 20.0f, 10.0f, 120.0f };
    const float line = 50.0f + 20.0f + 120.0f;
    CutsceneSkipPlace p[CUTSCENE_SKIP_PIECES];
    float size;
    CHECK(cutscene_skip_text[CUTSCENE_SKIP_PIECES - 1] != NULL);
    CHECK(!strcmp(cutscene_skip_text[CUTSCENE_SKIP_PRESS], "Press ")
          && !strcmp(cutscene_skip_text[CUTSCENE_SKIP_A], "A")
          && !strcmp(cutscene_skip_text[CUTSCENE_SKIP_REST], " again to skip"));
    /* The cap: the font's filled circle, in the menu's dark green. */
    CHECK(!strcmp(cutscene_skip_text[CUTSCENE_SKIP_CAP], "\x81\x9C"));
    CHECK(cutscene_skip_colour[CUTSCENE_SKIP_CAP] == 0xFF397C2Du);
    CHECK(cutscene_skip_colour[CUTSCENE_SKIP_A] == 0xFFFFFFFFu);
    for (size = 1.0f; size <= CUTSCENE_SKIP_GROW + 1e-4f; size += 0.05f) {
        const float middle = CUTSCENE_SKIP_RIGHT - line / 2;
        cutscene_skip_place(size, width, p);
        CHECK(NEAR(p[CUTSCENE_SKIP_PRESS].x + line * size / 2, middle));
        CHECK(NEAR(p[CUTSCENE_SKIP_CAP].x, p[CUTSCENE_SKIP_PRESS].x + 50.0f * size));
        CHECK(NEAR(p[CUTSCENE_SKIP_REST].x, p[CUTSCENE_SKIP_CAP].x + 20.0f * size));
        CHECK(NEAR(p[CUTSCENE_SKIP_A].x + 10.0f * size / 2, p[CUTSCENE_SKIP_CAP].x + 20.0f * size / 2));
        CHECK(NEAR(p[CUTSCENE_SKIP_PRESS].scale, CUTSCENE_SKIP_SCALE * size));
        CHECK(NEAR(p[CUTSCENE_SKIP_CAP].scale, CUTSCENE_SKIP_SCALE * CUTSCENE_SKIP_CAP_SIZE * size));
        CHECK(p[CUTSCENE_SKIP_CAP].y < p[CUTSCENE_SKIP_PRESS].y);
        CHECK(NEAR(p[CUTSCENE_SKIP_A].y, p[CUTSCENE_SKIP_PRESS].y));
        CHECK(NEAR(p[CUTSCENE_SKIP_PRESS].y + CUTSCENE_SKIP_HEIGHT * CUTSCENE_SKIP_SCALE * size / 2,
                   CUTSCENE_SKIP_MIDDLE));
        if (NEAR(size, 1.0f))
            CHECK(NEAR(p[CUTSCENE_SKIP_REST].x + 120.0f, CUTSCENE_SKIP_RIGHT));
    }
}

/* A skater preview's own ends, on made-up events lists shaped as the
 * missions' are: 0121's (one end per chapter, the 249 in chapter C31's),
 * the Stg20 ones' (three ends in turn, a 249 for the first and the last)
 * and 0463's (one end on the mission's own flag alone). */
static uint8_t s_mem[0x2000];
static int s_asked, s_asked_mission;
static struct { uint32_t bank, id, value; } s_flags[8];
static void put(uint32_t at, uint32_t v) { memcpy(s_mem + at, &v, sizeof v); }
static uint32_t flag(uint32_t bank, uint32_t id, uint32_t value) { return bank | id << 3 | value << 19; }
static int holds(void *ctx, uint32_t at)
{
    uint32_t c;
    int i;
    CHECK(ctx == s_mem);
    memcpy(&c, s_mem + at, sizeof c);
    s_asked++;
    if ((c & 7u) == CUTSCENE_SKIP_BANK_MISSION) s_asked_mission++;
    for (i = 0; i < 8; i++)
        if (s_flags[i].bank == (c & 7u) && s_flags[i].id == ((c >> 3) & 0xFFFFu))
            return s_flags[i].value == ((c >> 19) & 1u);
    return ((c >> 19) & 1u) == 0;   /* a flag never set is 0 */
}
static void set_flags(uint32_t c1, uint32_t v1, uint32_t c2, uint32_t v2)
{
    memset(s_flags, 0, sizeof s_flags);
    s_flags[0].bank = 1; s_flags[0].id = c1; s_flags[0].value = v1;
    s_flags[1].bank = 1; s_flags[1].id = c2; s_flags[1].value = v2;
}
/* Entry i of the list at 0x100: command, words, and its conditions (up to
 * 2, kept at 0x1000 + i * 16). */
static uint32_t event(uint32_t i, uint32_t command, uint32_t word0, uint32_t word1, int many,
                      uint32_t c1, uint32_t c2)
{
    const uint32_t e = 0x100u + i * CUTSCENE_SKIP_EVENT, conditions = 0x1000u + i * 16u;
    memset(s_mem + e, 0, CUTSCENE_SKIP_EVENT);
    put(conditions, c1);
    put(conditions + 4u, c2);
    put(e, conditions);
    put(e + 4u, (uint32_t)many);
    put(e + 0x10u, command);
    put(e + 0x14u, word0);
    put(e + 0x18u, word1);
    return e;
}
static void preview_ends(void)
{
    uint32_t ends[8], e0, e1, e2;
    int n;
    /* 0121: ends by chapter, exit 1 for C31's (the preview's), 0 for the
     * others; its 249 [134 1] if C31=1. */
    e0 = event(0, 230, 1, 0, 2, flag(0, 203, 1), flag(1, 31, 1));
    event(1, 230, 0, 0, 2, flag(0, 203, 1), flag(1, 123, 1));
    event(2, 230, 0, 0, 2, flag(0, 203, 1), flag(1, 124, 1));
    event(3, 249, 134, 1, 1, flag(1, 31, 1), 0);
    set_flags(31, 1, 123, 0);
    s_asked = s_asked_mission = 0;
    n = cutscene_skip_preview_ends(s_mem, 0x100, 4, 1, holds, s_mem, ends, 8);
    CHECK(n == 1 && ends[0] == e0);
    CHECK(s_asked == 1 && s_asked_mission == 0);
    n = cutscene_skip_preview_ends(s_mem, 0x100, 4, 0, holds, s_mem, ends, 8);
    CHECK(n == 0);
    CHECK(s_asked_mission == 0);
    /* Another chapter's: its own end, not C31's. */
    set_flags(31, 0, 123, 1);
    n = cutscene_skip_preview_ends(s_mem, 0x100, 4, 0, holds, s_mem, ends, 8);
    CHECK(n == 1 && ends[0] == 0x100u + CUTSCENE_SKIP_EVENT);
    /* Stg20: 400, then 420, then 410, then 430; the 249s are for 400 (exit
     * 0) and 410 (exit 1), and the end at 420 takes exit 1 too. */
    e0 = event(0, 230, 0, 0, 2, flag(0, 200, 1), flag(1, 400, 1));
    e1 = event(1, 230, 1, 0, 2, flag(0, 200, 1), flag(1, 420, 1));
    e2 = event(2, 230, 1, 0, 2, flag(0, 200, 1), flag(1, 410, 1));
    event(3, 249, 136, 0, 1, flag(1, 400, 1), 0);
    event(4, 249, 137, 1, 1, flag(1, 410, 1), 0);
    set_flags(400, 0, 410, 1);
    n = cutscene_skip_preview_ends(s_mem, 0x100, 5, 1, holds, s_mem, ends, 8);
    CHECK(n == 1 && ends[0] == e2);
    set_flags(400, 1, 410, 0);
    n = cutscene_skip_preview_ends(s_mem, 0x100, 5, 0, holds, s_mem, ends, 8);
    CHECK(n == 1 && ends[0] == e0);
    CHECK(s_asked_mission == 0);
    (void)e1;
    /* 0463: one end on the mission's own flag alone: no question asked. */
    e0 = event(0, 230, 0, 0, 1, flag(0, 203, 1), 0);
    event(1, 249, 140, 0, 0, 0, 0);
    s_asked = 0;
    n = cutscene_skip_preview_ends(s_mem, 0x100, 2, 0, holds, s_mem, ends, 8);
    CHECK(n == 1 && ends[0] == e0 && s_asked == 0);
    /* Too many conditions: passed over; ends[] kept to max; no list. */
    put(e0 + 4u, CUTSCENE_SKIP_CONDITIONS + 1u);
    CHECK(cutscene_skip_preview_ends(s_mem, 0x100, 2, 0, holds, s_mem, ends, 8) == 0);
    event(0, 230, 0, 0, 0, 0, 0);
    event(1, 230, 0, 0, 0, 0, 0);
    CHECK(cutscene_skip_preview_ends(s_mem, 0x100, 2, 0, holds, s_mem, ends, 1) == 1);
    CHECK(cutscene_skip_preview_ends(s_mem, 0, 2, 0, holds, s_mem, ends, 8) == 0);
}

/* Which lists are a preview's, and which of those the game has no skip
 * for: 0121's rounds (C123 to C126), whose end is then found by any exit;
 * its C31 visit, and 0463's 249 with no conditions, have the game's. */
static void preview_lists(void)
{
    uint32_t ends[8], e1;
    int n;
    event(0, 230, 1, 0, 2, flag(0, 203, 1), flag(1, 31, 1));
    e1 = event(1, 230, 0, 0, 2, flag(0, 203, 1), flag(1, 123, 1));
    event(2, 230, 0, 0, 2, flag(0, 203, 1), flag(1, 124, 1));
    event(3, 249, 134, 1, 1, flag(1, 31, 1), 0);
    set_flags(31, 0, 123, 1);
    CHECK(cutscene_skip_preview_list(s_mem, 0x100, 4, holds, s_mem) == CUTSCENE_SKIP_LIST_PREVIEW);
    n = cutscene_skip_preview_ends(s_mem, 0x100, 4, CUTSCENE_SKIP_ANY_EXIT, holds, s_mem, ends, 8);
    CHECK(n == 1 && ends[0] == e1);
    /* The C31 visit: the 249 holds. */
    set_flags(31, 1, 123, 0);
    CHECK(cutscene_skip_preview_list(s_mem, 0x100, 4, holds, s_mem) == CUTSCENE_SKIP_LIST_SEEN);
    n = cutscene_skip_preview_ends(s_mem, 0x100, 4, CUTSCENE_SKIP_ANY_EXIT, holds, s_mem, ends, 8);
    CHECK(n == 1 && ends[0] == 0x100u);
    /* Two ends by different exits, both on: both found, in order. */
    set_flags(31, 1, 123, 1);
    n = cutscene_skip_preview_ends(s_mem, 0x100, 4, CUTSCENE_SKIP_ANY_EXIT, holds, s_mem, ends, 8);
    CHECK(n == 2 && ends[0] == 0x100u && ends[1] == e1);
    /* No 249, or any other command: not a preview's list. */
    CHECK(cutscene_skip_preview_list(s_mem, 0x100, 3, holds, s_mem) == CUTSCENE_SKIP_LIST_OTHER);
    event(2, 227, 21, 0, 0, 0, 0);
    CHECK(cutscene_skip_preview_list(s_mem, 0x100, 4, holds, s_mem) == CUTSCENE_SKIP_LIST_OTHER);
    CHECK(cutscene_skip_preview_list(s_mem, 0, 4, holds, s_mem) == CUTSCENE_SKIP_LIST_OTHER);
    CHECK(cutscene_skip_preview_list(s_mem, 0x100, 0, holds, s_mem) == CUTSCENE_SKIP_LIST_OTHER);
    CHECK(cutscene_skip_preview_list(s_mem, 0x100, CUTSCENE_SKIP_EVENTS + 1u, holds, s_mem)
          == CUTSCENE_SKIP_LIST_OTHER);
    /* 0463: a 249 with no conditions always holds. */
    event(0, 230, 0, 0, 1, flag(0, 203, 1), 0);
    event(1, 249, 140, 0, 0, 0, 0);
    CHECK(cutscene_skip_preview_list(s_mem, 0x100, 2, holds, s_mem) == CUTSCENE_SKIP_LIST_SEEN);
    /* A 249 with too many conditions never holds. */
    put(0x100u + CUTSCENE_SKIP_EVENT + 4u, CUTSCENE_SKIP_CONDITIONS + 1u);
    CHECK(cutscene_skip_preview_list(s_mem, 0x100, 2, holds, s_mem) == CUTSCENE_SKIP_LIST_PREVIEW);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    cutscene_skip_start();
    row();
    places();
    preview_ends();
    preview_lists();
    if (!strcmp(c, "off")) {
        CHECK(s_row.preset[XBOX_PRESET_JSRF_PC] == cutscene_skip_on());
        off();
        try_prompt();
    }
    if (!strcmp(c, "on")) presses();
    if (!strcmp(c, "flip")) {
        /* Off, then the row: on at once (and kept, which run.py checks). */
        off();
        s_row.run();
        CHECK(cutscene_skip_on() && s_row.state());
        presses();
    }
    printf("cutscene_skip: %s\n", c);
    return 0;
}
