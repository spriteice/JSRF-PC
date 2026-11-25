/* Run with: python tests/camera_scenes/run.py
 *
 * The game's own camera shots without the game: port/camera_scenes.c as it
 * is, built with the in-game menu, with the settings file, the menu's
 * xbox_MenuAdd and camera_walls.h's two settings stood in for. One case a run, named by the first argument,
 * as the ticks are read once; the second, when there is one, is the
 * settings file's three keys, each 0, 1, or - for none (graffiti_cam=,
 * half_pipe_cam= and loop_cam=, in that order), or x for a value no key
 * takes. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "camera_scenes.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

/* The settings file: each key as the case's second argument sets it, and
 * what the row keeps. */
static const char *const k_keys[CAMSCENES] = { "graffiti_cam", "half_pipe_cam", "loop_cam" };
static char s_has[CAMSCENES];
static double s_value[CAMSCENES], s_kept[CAMSCENES] = { -1, -1, -1 };
static int key_index(const char *key)
{
    int i;
    for (i = 0; i < CAMSCENES; i++)
        if (!strcmp(key, k_keys[i])) return i;
    return -1;
}
int nv_window_pref_num(const char *key, double *v)
{
    int i = key_index(key);
    CHECK(i >= 0);
    if (!s_has[i]) return 0;
    *v = s_value[i];
    return 1;
}
void nv_window_pref_set_num(const char *key, double v)
{
    int i = key_index(key);
    CHECK(i >= 0);
    s_kept[i] = v;
}

/* camera_walls.h's Default cam fix and No skater fade, whose ticks the row
 * holds first: their state, and each flip. */
static int s_fix = 1, s_fade, s_fix_flips, s_fade_flips;
int  camwall_on(void)         { return s_fix; }
int  camwall_whole(void)      { return s_fade; }
void camwall_flip(void)       { s_fix = !s_fix; s_fix_flips++; }
void camwall_whole_flip(void) { s_fade = !s_fade; s_fade_flips++; }

/* The rows it adds: Input's Behaviour submenu, then its row in it. */
#define ROWS_MOST 4
static XBOX_MENU_ITEM s_added[ROWS_MOST];
static int s_rows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    while (count-- > 0) {
        if (s_rows < ROWS_MOST) s_added[s_rows] = *items;
        s_rows++;
        items++;
    }
}

/* The row's state: Default cam fix's and No skater fade's ticks, then the
 * shots'. */
static int row_state(unsigned ticks)
{
    return s_fix << CAMROW_FIX | s_fade << CAMROW_FADE | (int)(ticks << CAMROW_SHOTS);
}

#define GRAFFITI  CAMSCENE_BIT(CAMSCENE_GRAFFITI)
#define HALF_PIPE CAMSCENE_BIT(CAMSCENE_HALF_PIPE)
#define FIXED     CAMSCENE_BIT(CAMSCENE_FIXED)

/* The modes a skater asks for that are no shot of the ticks': follow, the
 * follow modes 4 and 5 (5 every grind's), the look (9). */
static const uint32_t k_follow[] = { 3, 4, 5, 9 };
#define FOLLOWS (sizeof k_follow / sizeof k_follow[0])

static void check_row(void)
{
    const XBOX_MENU_ITEM *group = &s_added[0], *row = &s_added[1];
    static const char *const names[] = {
        "Default cam fix", "No skater fade", "Graffiti cam", "Half pipe cam", "Loop cam"
    };
    int i;
    /* Input's Behaviour submenu, with its hint, then the row in it: a row
     * of five ticks, camera_walls.h's two first, in their order, taking
     * effect at once (spriteice, 22 November 2026). */
    CHECK(s_rows == 2);
    CHECK(group->menu == XBOX_MENU_INPUT && group->kind == XBOX_ITEM_GROUP);
    CHECK(!strcmp(group->label, XBOX_GROUP_BEHAVIOUR));
    CHECK(group->hint && !strcmp(group->hint, XBOX_GROUP_BEHAVIOUR_HINT));
    CHECK(row->menu == XBOX_MENU_INPUT && row->kind == XBOX_ITEM_TOGGLES && !row->last);
    CHECK(!strcmp(row->label, "Camera") && !row->confirm && !row->keep);
    CHECK(row->group && !strcmp(row->group, XBOX_GROUP_BEHAVIOUR) && !row->under);
    CHECK(row->nchoices == CAMROW_TICKS && CAMROW_TICKS == 5 && CAMSCENES == 3);
    CHECK(CAMROW_FIX == 0 && CAMROW_FADE == 1 && CAMROW_SHOTS == 2);
    for (i = 0; i < CAMROW_TICKS; i++) CHECK(!strcmp(row->choices[i], names[i]));
    CHECK(row->state && row->pick && !row->run);
    CHECK(row->state() == row_state(camscene_ticks()));
    /* Sentences short enough for the strip: the row's, and each tick's,
     * Default cam fix and No skater fade keeping the lines their rows had
     * (the card "A line for each tick"). */
    CHECK(row->hint && strlen(row->hint) <= MENU_HINT_LONGEST
          && row->hint[strlen(row->hint) - 1] == '.');
    CHECK(row->choice_hints);
    for (i = 0; i < CAMROW_TICKS; i++)
        CHECK(row->choice_hints[i] && strlen(row->choice_hints[i]) <= MENU_HINT_LONGEST
              && row->choice_hints[i][strlen(row->choice_hints[i]) - 1] == '.'
              && strcmp(row->choice_hints[i], row->hint));
    CHECK(!strcmp(row->choice_hints[CAMROW_FIX],
                  "Fixes the default camera clipping through walls, mostly in widescreen."));
    CHECK(!strcmp(row->choice_hints[CAMROW_FADE],
                  "Keeps the camera back at a wall instead of fading your skater."));
    /* File > Presets: Xbox sets it, Default cam fix and No skater
     * fade off and every shot on; Modern too, Default cam fix and Half pipe
     * cam on (spriteice, 25 November 2026: "modern ships with half pipe cam
     * on");
     * Performance, Balanced and Quality leave it as it is
     * ("our Default and Max presets to not touch any of the users changes
     * in the camera menu at all", and all of Input, 23 November 2026). A start with no keys
     * has every shot on (spriteice, 22 November 2026: "all on by
     * default"). */
    CHECK(row->presets == XBOX_PRESETS_SET);
    CHECK(row->preset[XBOX_PRESET_XBOX] == CAMROW_XBOX && CAMROW_XBOX == 28);
    CHECK(row->preset[XBOX_PRESET_MODERN] == CAMROW_MODERN && CAMROW_MODERN == 9);   /* Default cam fix, Half pipe cam */
    CHECK(row->leave == XBOX_PRESETS_LEVELS && !(row->leave & XBOX_PRESET_BIT(XBOX_PRESET_XBOX))
          && !(row->leave & XBOX_PRESET_BIT(XBOX_PRESET_MODERN)));
    CHECK(CAMSCENE_DEFAULT == 7);
    CHECK(MENU_PRESET_TICKS >= CAMROW_TICKS);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    const char *keys = argc > 2 ? argv[2] : "---";
    unsigned want = 0;
    int i;
    CHECK(strlen(keys) == CAMSCENES);
    for (i = 0; i < CAMSCENES; i++) {
        s_has[i] = keys[i] != '-';
        s_value[i] = keys[i] == 'x' ? 2 : keys[i] - '0';
        if (keys[i] == '1' || ((keys[i] == '-' || keys[i] == 'x') && (CAMSCENE_DEFAULT & (1u << i))))
            want |= 1u << i;
    }
    if (!strcmp(c, "modes")) {
        /* Of the follow camera's post-update's modes, only graffiti's is
         * one of the game's own shots: the game aims it at the spot being
         * sprayed, and it is left as the game frames it. */
        for (i = 0; i < (int)FOLLOWS; i++) CHECK(!camscene_shot(k_follow[i]));
        CHECK(CAMSCENE_MODE_GRAFFITI == 13);
        CHECK(camscene_shot(CAMSCENE_MODE_GRAFFITI));
        printf("ok %s\n", c);
        return 0;
    }
    if (!strcmp(c, "rise")) {
        /* Half pipe cam unticked, through the half pipe's jump: the camera
         * target as much higher as the skater is than as it started, the
         * game's own height under it taken in, and let go smoothly over
         * CAMSCENE_RISE_OUT frames as the jump ends; nothing outside one. */
        CAMSCENE_RISE r;
        double sk, tg, lift, last;
        memset(&r, 0, sizeof r);
        CHECK(camscene_rise(&r, 0, 50, 20) == 0);
        CHECK(camscene_rise(&r, 1, 50, 20) == 0);           /* the jump starts */
        CHECK(camscene_rise(&r, 1, 80, 20) == 30);          /* up the skater goes, the camera stays */
        CHECK(camscene_rise(&r, 1, 150, 60) == 60);         /* the game's own rise taken in */
        CHECK(150 - 50 == 60 + 60 - 20);                    /* the target as far above as the skater */
        CHECK(camscene_rise(&r, 1, 120, 20) == 70);
        lift = camscene_rise(&r, 1, 90, 30);                /* down again, nearly at the lip */
        CHECK(lift == 30);
        /* It lands: what was added let go, the most at first, none at
         * the end, half way half of it. */
        last = lift;
        for (int i = 1; i < CAMSCENE_RISE_OUT; i++) {
            double t = (double)i / CAMSCENE_RISE_OUT;
            lift = camscene_rise(&r, 0, 50, 20);
            CHECK(lift > 0 && lift < last);
            CHECK(fabs(lift - 30 * (1 - t * t * (3 - 2 * t))) < 1e-9);
            last = lift;
        }
        CHECK(camscene_rise(&r, 0, 50, 20) == 0);
        CHECK(camscene_rise(&r, 0, 50, 20) == 0);
        /* Another jump as one is let go of starts from what is added then:
         * no jump in the camera. */
        CHECK(camscene_rise(&r, 1, 50, 20) == 0);
        CHECK(camscene_rise(&r, 1, 90, 20) == 40);
        lift = camscene_rise(&r, 0, 60, 20);
        lift = camscene_rise(&r, 0, 60, 20);
        sk = 60; tg = 25;
        CHECK(camscene_rise(&r, 1, sk, tg) == lift);
        CHECK(camscene_rise(&r, 1, sk + 10, tg) == lift + 10);
        printf("ok %s\n", c);
        return 0;
    }
    camscene_start();
    /* Each tick as its key says, or on with none, or with one no key takes. */
    CHECK(camscene_ticks() == want);
    check_row();
    if (!strcmp(c, "asked")) {
        /* The shots asked for: the graffiti camera; mode 6, which only the
         * half pipe's jump asks for; and a fixed shot of the stage's camera
         * areas, whatever the skater does (a grind, mode 5, as on a loop,
         * or none, as when it jumps past one), but not while the level
         * exit's camera point is set: that shot is the level exit's. */
        CHECK(camscene_asked(3, 0, 0, 0) == 0);
        for (i = 0; i < (int)FOLLOWS; i++) {
            CHECK(camscene_asked(k_follow[i], 0, 0, 0) == 0);
            CHECK(camscene_asked(k_follow[i], 0, 1, 0) == 0);
            CHECK(camscene_asked(k_follow[i], 1, 0, 0) == FIXED);
            CHECK(camscene_asked(k_follow[i], 1, 1, 0) == 0);
            CHECK(camscene_asked(k_follow[i], 0, 0, 1) == GRAFFITI);
            CHECK(camscene_asked(k_follow[i], 1, 1, 1) == GRAFFITI);
        }
        CHECK(camscene_asked(CAMSCENE_MODE_HALF_PIPE, 0, 0, 0) == HALF_PIPE);
        CHECK(camscene_asked(CAMSCENE_MODE_HALF_PIPE, 0, 1, 0) == HALF_PIPE);
        CHECK(camscene_asked(CAMSCENE_MODE_HALF_PIPE, 1, 0, 1) == (GRAFFITI | HALF_PIPE | FIXED));
        CHECK(camscene_asked(CAMSCENE_MODE_HALF_PIPE, 1, 1, 0) == HALF_PIPE);
        /* Any value set counts, as the camera update tests each for 0. */
        CHECK(camscene_asked(5, 0xFFFFFFFFu, 0, 2) == (GRAFFITI | FIXED));
        CHECK(camscene_asked(5, 7, 0x10, 0) == 0);
        CHECK(CAMSCENE_MODE_HALF_PIPE == 6 && CAMSCENE_MODE_FOLLOW == 3);
    } else if (!strcmp(c, "hidden")) {
        /* With the ticks as the keys set them: a shot whose tick is off is
         * hidden, one whose tick is on plays; and the level exit's shot is
         * never hidden, nor anything not asked for. */
        unsigned shots, ticks = camscene_ticks();
        for (shots = 0; shots <= CAMSCENE_ALL; shots++)
            CHECK(camscene_hidden(shots, ticks) == (shots & ~want));
        CHECK(camscene_hidden(0xFFFFFFFFu, ticks) == (CAMSCENE_ALL & ~want));
        CHECK(camscene_hidden(camscene_asked(3, 1, 1, 0), ticks) == 0);
        CHECK(camscene_hidden(camscene_asked(5, 1, 1, 0), ticks) == 0);
        /* A recording's ticks in place of the player's: theirs alone. */
        for (shots = 0; shots <= CAMSCENE_ALL; shots++)
            CHECK(camscene_hidden(CAMSCENE_ALL, shots) == (CAMSCENE_ALL & ~shots));
    } else if (!strcmp(c, "before")) {
        /* A recording made before the ticks: the half pipe's and the fixed
         * shots played, and the graffiti shot only with the free camera
         * off, whatever the player's ticks. */
        CHECK(camscene_ticks_before(0) == CAMSCENE_ALL);
        CHECK(camscene_ticks_before(1) == (HALF_PIPE | FIXED));
        CHECK(camscene_hidden(camscene_asked(3, 0, 0, 1), camscene_ticks_before(1)) == GRAFFITI);
        CHECK(camscene_hidden(camscene_asked(6, 1, 0, 0), camscene_ticks_before(1)) == 0);
        CHECK(camscene_hidden(camscene_asked(3, 0, 0, 1), camscene_ticks_before(0)) == 0);
    } else if (!strcmp(c, "pick")) {
        /* Default cam fix's and No skater fade's ticks flip camera_walls.h's
         * settings, once each, and no shot's. Each shot's tick flips at
         * once, alone, and is kept as its own key; a choice out of range
         * changes nothing. */
        const XBOX_MENU_ITEM *row = &s_added[1];
        unsigned now = want;
        row->pick(CAMROW_FIX);
        CHECK(s_fix == 0 && s_fix_flips == 1 && s_fade_flips == 0 && camscene_ticks() == now);
        CHECK(row->state() == row_state(now));
        row->pick(CAMROW_FADE);
        CHECK(s_fade == 1 && s_fade_flips == 1 && s_fix_flips == 1 && camscene_ticks() == now);
        CHECK(row->state() == row_state(now));
        for (i = 0; i < CAMSCENES; i++) {
            row->pick(CAMROW_SHOTS + i);
            now ^= 1u << i;
            CHECK(camscene_ticks() == now && row->state() == row_state(now));
            CHECK(s_kept[i] == ((now >> i) & 1u));
            CHECK(camscene_hidden(CAMSCENE_ALL, camscene_ticks()) == (CAMSCENE_ALL & ~now));
        }
        row->pick(CAMROW_SHOTS + 1);
        now ^= HALF_PIPE;
        CHECK(camscene_ticks() == now && s_kept[1] == ((now >> 1) & 1u));
        row->pick(-1);
        row->pick(CAMROW_TICKS);
        CHECK(camscene_ticks() == now && s_fix_flips == 1 && s_fade_flips == 1);
    } else if (strcmp(c, "keys")) {
        fprintf(stderr, "no case %s\n", c);
        return 2;
    }
    printf("ok %s\n", c);
    return 0;
}
