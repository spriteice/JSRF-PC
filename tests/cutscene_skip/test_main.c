/* Run with: python tests/cutscene_skip/run.py
 *
 * Skip cutscenes without the game: port/cutscene_skip.c as it is, built with
 * the in-game menu, reading and keeping skipcutscenes= through the engine's
 * own settings code (taken from src/nv2a_gl/nv2a_gl.c by run.py). The menu's
 * xbox_MenuAdd is stood in for, to catch the row. The presses, the prompt's
 * fades and its places are driven with made-up times and scenes, and a try
 * of the prompt with no scene (JSRF_SKIP_PROMPT_AT's), and a rival's
 * preview: the rivals a mission waits on, found on made-up trigger lists,
 * and its run's last record, on made-up runs. Each case is
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
    /* One row: a toggle in Gameplay > Skips, showing the setting. */
    CHECK(s_rows == 1);
    CHECK(s_row.menu == XBOX_MENU_GAMEPLAY && s_row.kind == XBOX_ITEM_TOGGLE && !s_row.last && !s_row.under);
    CHECK(s_row.group && !strcmp(s_row.group, XBOX_GROUP_SKIPS));
    CHECK(!strcmp(s_row.label, "Skip cutscenes") && !s_row.confirm);
    /* A hint, a sentence short enough for the strip, naming no key (docs/menu-format.md). */
    CHECK(s_row.hint && strlen(s_row.hint) <= MENU_HINT_LONGEST && s_row.hint[strlen(s_row.hint) - 1] == '.');
    CHECK(!strcmp(s_row.hint, "Skips a cutscene when the jump button is pressed twice."));
    CHECK(s_row.state() == cutscene_skip_on());
    /* File > Presets: off in Xbox and on in Modern; the others change only
     * visuals, so leave it. */
    CHECK(s_row.presets == XBOX_PRESETS_SET && !s_row.keep && s_row.leave == XBOX_PRESETS_LEVELS);
    CHECK(s_row.preset[XBOX_PRESET_XBOX] == 0 && s_row.preset[XBOX_PRESET_MODERN] == 1);
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

/* A rival's preview: the rivals a mission waits on, found in made-up
 * trigger lists shaped as the missions' are, and a run's last record found
 * in made-up runs. */
static uint8_t s_mem[0x2000];
static void put(uint32_t at, uint32_t v) { memcpy(s_mem + at, &v, sizeof v); }
static void put16(uint32_t at, uint16_t v) { memcpy(s_mem + at, &v, sizeof v); }
/* Entry i of the list at 0x100: its command and first word. */
static void trigger(uint32_t i, uint32_t command, uint32_t word0)
{
    const uint32_t e = 0x100u + i * CUTSCENE_SKIP_EVENT;
    memset(s_mem + e, 0, CUTSCENE_SKIP_EVENT);
    put(e + CUTSCENE_SKIP_EVENT_COMMAND, command);
    put(e + CUTSCENE_SKIP_EVENT_WORD, word0);
}
static void rivals(void)
{
    uint32_t slots[4];
    /* 0111's: player areas (1), the rival on its way (26) and arrived (27),
     * then the list's other sensors and its -1 separators. */
    trigger(0, 26, 1);
    trigger(1, CUTSCENE_SKIP_ARRIVED, 1);
    trigger(2, 1, 0);
    trigger(3, 0xFFFFFFFFu, 0);
    trigger(4, 16, 0);
    memset(slots, 0xEE, sizeof slots);
    CHECK(cutscene_skip_rivals(s_mem, 0x100, 5, slots, 4) == 1 && slots[0] == 1u);
    /* A slot waited on twice is one rival; two slots, in the list's order. */
    trigger(2, CUTSCENE_SKIP_ARRIVED, 1);
    trigger(3, CUTSCENE_SKIP_ARRIVED, 2);
    CHECK(cutscene_skip_rivals(s_mem, 0x100, 5, slots, 4) == 2 && slots[0] == 1u && slots[1] == 2u);
    CHECK(cutscene_skip_rivals(s_mem, 0x100, 5, slots, 1) == 1 && slots[0] == 1u);
    /* No 27: a mission with no preview (a race's rival is driven another
     * way); no list; too many entries looked at no further. */
    trigger(1, 1, 1);
    trigger(2, 1, 2);
    trigger(3, 1, 3);
    CHECK(cutscene_skip_rivals(s_mem, 0x100, 5, slots, 4) == 0);
    CHECK(cutscene_skip_rivals(s_mem, 0, 5, slots, 4) == 0);
    trigger(1, CUTSCENE_SKIP_ARRIVED, 1);
    CHECK(cutscene_skip_rivals(s_mem, 0x100, 1, slots, 4) == 0);
    CHECK(cutscene_skip_rivals(s_mem, 0x100, 2, slots, 4) == 1);
}
static void run_last(void)
{
    const uint32_t run = 0x1000u, size = (uint32_t)sizeof s_mem;
    uint32_t k;
    memset(s_mem, 0, sizeof s_mem);
    /* A run of five records, frames 30, 1, 200, 7 and 12, the last marked. */
    put16(run + 0u * CUTSCENE_SKIP_RECORD, 30);
    put16(run + 1u * CUTSCENE_SKIP_RECORD, 1);
    put16(run + 2u * CUTSCENE_SKIP_RECORD, 200);
    put16(run + 3u * CUTSCENE_SKIP_RECORD, 7);
    put16(run + 4u * CUTSCENE_SKIP_RECORD, 12 | CUTSCENE_SKIP_RECORD_LAST);
    for (k = 0; k < 5; k++)
        CHECK(cutscene_skip_run_last(s_mem, size, run + k * CUTSCENE_SKIP_RECORD)
              == run + 4u * CUTSCENE_SKIP_RECORD);
    /* The pad words after the frames don't count, only bit 15 of the
     * first: a pad word with bit 15 set isn't an end. */
    put16(run + 2u * CUTSCENE_SKIP_RECORD + 2u, 0x8000u);
    put(run + 2u * CUTSCENE_SKIP_RECORD + 4u, 0x80008000u);
    CHECK(cutscene_skip_run_last(s_mem, size, run) == run + 4u * CUTSCENE_SKIP_RECORD);
    /* None to the memory's end, none at 0, and a record past the end. */
    put16(run + 4u * CUTSCENE_SKIP_RECORD, 12);
    CHECK(cutscene_skip_run_last(s_mem, size, run) == 0);
    CHECK(cutscene_skip_run_last(s_mem, size, 0) == 0);
    CHECK(cutscene_skip_run_last(s_mem, size, size - 4u) == 0);
    CHECK(cutscene_skip_run_last(s_mem, 4u, 0x10u) == 0);
    /* The last record is the memory's last bytes. */
    put16(size - CUTSCENE_SKIP_RECORD, CUTSCENE_SKIP_RECORD_LAST);
    CHECK(cutscene_skip_run_last(s_mem, size, run) == size - CUTSCENE_SKIP_RECORD);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    cutscene_skip_start();
    row();
    places();
    rivals();
    run_last();
    if (!strcmp(c, "off")) {
        CHECK(!cutscene_skip_on());
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
