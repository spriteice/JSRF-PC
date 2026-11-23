/* Skip cutscenes (cutscene_skip.h): skipcutscenes= in the settings file, or
 * Gameplay > Skips > Skip cutscenes.
 *
 * Read once, at start (cutscene_skip_start). The menu's row changes it at
 * once and keeps it in the settings file; it runs on the window's thread,
 * and the scene's update and the prompt's draw read it on the game's. The
 * two presses and the prompt's state are the game thread's alone. */
#include <stdio.h>
#include <string.h>
#include "cutscene_skip.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file the engine already keeps, and a
 * number kept there. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
#else
/* Without the SDL window (a headless build) there is no settings file: no
 * keys. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
#endif

/* Off with no skipcutscenes= in the settings: scenes play to their end, as
 * on the Xbox. */
#define CUTSCENE_SKIP_DEFAULT 0

/* The in-game menu's dark green, behind the A on its cap (menu_style.cpp),
 * and the font's white. Colours as the font call takes them: alpha, red,
 * green, blue, from the top byte down. */
#define PROMPT_GREEN 0xFF397C2Du
#define PROMPT_WHITE 0xFFFFFFFFu

static volatile int s_on;

static uint32_t s_scene;        /* the scene the presses were in */
static double   s_shown = -1.0; /* when the prompt was asked for; < 0 none */
static double   s_seen;         /* the last frame of a playing scene */
static double   s_ended = -1.0; /* when a second press last ended one; < 0 never */

const char *const cutscene_skip_text[CUTSCENE_SKIP_PIECES] = {
    [CUTSCENE_SKIP_PRESS] = "Press ",
    [CUTSCENE_SKIP_CAP]   = "\x81\x9C",     /* Shift-JIS's filled circle, in the font's own set */
    [CUTSCENE_SKIP_A]     = "A",
    [CUTSCENE_SKIP_REST]  = " again to skip",
};
const uint32_t cutscene_skip_colour[CUTSCENE_SKIP_PIECES] = {
    [CUTSCENE_SKIP_PRESS] = PROMPT_WHITE,
    [CUTSCENE_SKIP_CAP]   = PROMPT_GREEN,
    [CUTSCENE_SKIP_A]     = PROMPT_WHITE,
    [CUTSCENE_SKIP_REST]  = PROMPT_WHITE,
};

#if defined(XBOXRECOMP_MENU)
static int  menu_on(void) { return s_on; }
static void menu_flip(void)
{
    s_on = !s_on;
    fprintf(stderr, "[SKIP] skip cutscenes %s (the menu)\n", s_on ? "on" : "off");
    nv_window_pref_set_num("skipcutscenes", s_on);
}
#endif

void cutscene_skip_start(void)
{
    double v;
    s_on = nv_window_pref_num("skipcutscenes", &v) > 0 ? v != 0 : CUTSCENE_SKIP_DEFAULT;
    fprintf(stderr, "[SKIP] skip cutscenes %s (settings)\n", s_on ? "on" : "off");
#if defined(XBOXRECOMP_MENU)
    {
        /* In Gameplay's Skips, which the window adds once it is up, below
         * Skip intro (added before it). */
        const XBOX_MENU_ITEM row = {
            .menu = XBOX_MENU_GAMEPLAY, .kind = XBOX_ITEM_TOGGLE, .label = "Skip cutscenes",
            .state = menu_on, .run = menu_flip, .group = XBOX_GROUP_SKIPS,
            /* File > Presets: scenes play to their end in Xbox, as on the
             * Xbox, and can be skipped in Modern; the others change only the
             * visuals, so leave it (spriteice, 23 November 2026). */
            .presets = XBOX_PRESETS_SET, .preset = { 0, 1, 0, 0, 0 }, .leave = XBOX_PRESETS_LEVELS,
            .hint = "Skips a cutscene when the jump button is pressed twice." };
        xbox_MenuAdd(&row, 1);
    }
#endif
}

int cutscene_skip_on(void)
{
    return s_on;
}

void cutscene_skip_set(int on)
{
    s_on = on != 0;
}

int cutscene_skip_frame(uint32_t scene, int pressed, double now)
{
    if (!s_on) {
        s_shown = -1.0;
        return 0;
    }
    if (scene != s_scene) {
        s_scene = scene;
        s_shown = -1.0;
    }
    s_seen = now;
    if (!pressed)
        return 0;
    /* The press that has just ended one isn't the next one's first: an
     * end's flags can start another preview in the same frame (a Stg20
     * preview's skip sets the flag of its mission's next round). */
    if (s_ended >= 0.0 && now >= s_ended && now - s_ended < CUTSCENE_SKIP_GONE)
        return 0;
    if (s_shown >= 0.0 && now - s_shown < CUTSCENE_SKIP_WINDOW) {
        s_shown = -1.0;
        s_scene = 0;
        s_ended = now;
        fprintf(stderr, "[SKIP] t=%.2f A pressed again: the scene at %08X ends\n", now, scene);
        return 1;
    }
    s_shown = now;
    fprintf(stderr, "[SKIP] t=%.2f A pressed in the scene at %08X: the prompt shows\n", now, scene);
    return 0;
}

void cutscene_skip_try(double now)
{
    s_scene = 0;
    s_shown = now;
    s_seen = now + CUTSCENE_SKIP_WINDOW + CUTSCENE_SKIP_FADE;  /* no scene to go with */
    fprintf(stderr, "[SKIP] t=%.2f the prompt shown for a try (JSRF_SKIP_PROMPT_AT)\n", now);
}

int cutscene_skip_prompt(double now, float *alpha, float *size)
{
    double t, u;
    if (s_shown < 0.0)
        return 0;
    if (now - s_seen > CUTSCENE_SKIP_GONE || now < s_shown) {
        s_shown = -1.0;     /* the scene ended by itself */
        return 0;
    }
    t = now - s_shown;
    if (t < CUTSCENE_SKIP_FADE) {
        u = t / CUTSCENE_SKIP_FADE;
        *alpha = (float)u;
        *size = CUTSCENE_SKIP_GROW - (CUTSCENE_SKIP_GROW - 1.0f) * (float)u;
    } else if (t < CUTSCENE_SKIP_WINDOW) {
        *alpha = 1.0f;
        *size = 1.0f;
    } else if (t < CUTSCENE_SKIP_WINDOW + CUTSCENE_SKIP_FADE) {
        u = (t - CUTSCENE_SKIP_WINDOW) / CUTSCENE_SKIP_FADE;
        *alpha = 1.0f - (float)u;
        *size = 1.0f + (CUTSCENE_SKIP_GROW - 1.0f) * (float)u;
    } else {
        s_shown = -1.0;
        return 0;
    }
    return 1;
}

void cutscene_skip_place(float size, const float width[CUTSCENE_SKIP_PIECES],
                         CutsceneSkipPlace place[CUTSCENE_SKIP_PIECES])
{
    const float line = width[CUTSCENE_SKIP_PRESS] + width[CUTSCENE_SKIP_CAP]
                     + width[CUTSCENE_SKIP_REST];
    const float text = CUTSCENE_SKIP_HEIGHT * CUTSCENE_SKIP_SCALE * size;
    const float cap = text * CUTSCENE_SKIP_CAP_SIZE;
    const float left = CUTSCENE_SKIP_RIGHT - line * 0.5f - line * size * 0.5f;
    const float text_y = CUTSCENE_SKIP_MIDDLE - text * 0.5f;
    CutsceneSkipPlace *p;

    p = &place[CUTSCENE_SKIP_PRESS];
    p->x = left;
    p->y = text_y;
    p->scale = CUTSCENE_SKIP_SCALE * size;

    p = &place[CUTSCENE_SKIP_CAP];
    p->x = left + width[CUTSCENE_SKIP_PRESS] * size;
    p->y = CUTSCENE_SKIP_MIDDLE - cap * 0.5f;
    p->scale = CUTSCENE_SKIP_SCALE * CUTSCENE_SKIP_CAP_SIZE * size;

    p = &place[CUTSCENE_SKIP_A];
    p->x = place[CUTSCENE_SKIP_CAP].x
         + (width[CUTSCENE_SKIP_CAP] - width[CUTSCENE_SKIP_A]) * size * 0.5f;
    p->y = text_y;
    p->scale = CUTSCENE_SKIP_SCALE * size;

    p = &place[CUTSCENE_SKIP_REST];
    p->x = place[CUTSCENE_SKIP_CAP].x + width[CUTSCENE_SKIP_CAP] * size;
    p->y = text_y;
    p->scale = CUTSCENE_SKIP_SCALE * size;
}

static uint32_t word_at(const uint8_t *mem, uint32_t at)
{
    uint32_t v;
    memcpy(&v, mem + at, sizeof v);
    return v;
}
int cutscene_skip_preview_ends(const uint8_t *mem, uint32_t list, uint32_t n, uint32_t exit,
                               CutsceneSkipHolds holds, void *ctx, uint32_t *ends, int max)
{
    uint32_t i, k;
    int count = 0;
    for (i = 0; list && i < n && i < CUTSCENE_SKIP_EVENTS && count < max; i++) {
        const uint32_t e = list + i * CUTSCENE_SKIP_EVENT;
        const uint32_t conditions = word_at(mem, e), many = word_at(mem, e + 4u);
        int hold = many <= CUTSCENE_SKIP_CONDITIONS;
        if (word_at(mem, e + 0x10u) != CUTSCENE_SKIP_END) continue;
        if (exit != CUTSCENE_SKIP_ANY_EXIT && word_at(mem, e + 0x14u) != exit) continue;
        for (k = 0; hold && k < many; k++) {
            const uint32_t at = conditions + 4u * k;
            if ((word_at(mem, at) & 7u) != CUTSCENE_SKIP_BANK_MISSION) hold = holds(ctx, at);
        }
        if (hold) ends[count++] = e;
    }
    return count;
}

int cutscene_skip_preview_list(const uint8_t *mem, uint32_t list, uint32_t n,
                               CutsceneSkipHolds holds, void *ctx)
{
    uint32_t i, k;
    int skips = 0;
    if (!list || !n || n > CUTSCENE_SKIP_EVENTS)
        return CUTSCENE_SKIP_LIST_OTHER;
    for (i = 0; i < n; i++) {
        const uint32_t command = word_at(mem, list + i * CUTSCENE_SKIP_EVENT + 0x10u);
        if (command == CUTSCENE_SKIP_SEEN)
            skips++;
        else if (command != CUTSCENE_SKIP_END)
            return CUTSCENE_SKIP_LIST_OTHER;
    }
    if (!skips)
        return CUTSCENE_SKIP_LIST_OTHER;
    for (i = 0; i < n; i++) {
        const uint32_t e = list + i * CUTSCENE_SKIP_EVENT;
        const uint32_t conditions = word_at(mem, e), many = word_at(mem, e + 4u);
        int hold = many <= CUTSCENE_SKIP_CONDITIONS;
        if (word_at(mem, e + 0x10u) != CUTSCENE_SKIP_SEEN) continue;
        for (k = 0; hold && k < many; k++)
            hold = holds(ctx, conditions + 4u * k);
        if (hold)
            return CUTSCENE_SKIP_LIST_SEEN;
    }
    return CUTSCENE_SKIP_LIST_PREVIEW;
}
