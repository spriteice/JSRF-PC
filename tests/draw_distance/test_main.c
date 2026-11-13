/* Run with: python tests/draw_distance/run.py
 *
 * Draw distance without the game: port/draw_distance.c as it is, built with
 * the in-game menu, reading and keeping drawdistance= through the engine's own
 * settings code (taken from src/nv2a_gl/nv2a_gl.c by run.py). The menu's
 * xbox_MenuAdd is stood in for, to catch the row. Each case is its own run, as
 * the setting is read once; run.py writes the settings file and checks the log
 * and the file.
 *
 * The gates themselves are the game's code, which runs only in the game. What
 * they do with the step is what draw_distance_with does to the constants they
 * read, so two stand-ins here read theirs as the game's do (recomp_manual.c,
 * "draw distance"): a crowd's person, drawn to 400 and faded from 300 by 0.01,
 * and a prop, near to 400, faded to 500 by 1 - (d - 400) * 0.01. Their
 * answers at each step are what the game's gates give with the same
 * constants.
 *
 * Far buildings works on the map's areas, which are the game's data: a
 * made-up map here has their shape (a street of five areas, the last a copy
 * of the first, with a copy of the first no list names and an area the shape
 * of the second elsewhere), and the sets the game would make from three eyes.
 * The map's shadow passes are given the widened set in the game's memory, as
 * the game's are, and must see the widened set with Far shadows on, or its
 * own set with it off.
 * The Misc group's log is given the draws of a place with more objects than
 * b2's log remembered, which wrote a line for every draw there, and a draw
 * just after Far buildings is turned on, before it has read the map.
 */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "draw_distance.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static XBOX_MENU_ITEM s_row, s_group_row, s_far_row, s_shadow_row;
static int s_rows, s_at[4];     /* each one's place in the call, from 1 */
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        const char *l = items[i].label ? items[i].label : "";
        XBOX_MENU_ITEM *to = !strcmp(l, "Draw distance") ? &s_row
                           : !strcmp(l, "Building distance") ? &s_group_row
                           : !strcmp(l, "Far buildings") ? &s_far_row
                           : !strcmp(l, "Far shadows") ? &s_shadow_row : NULL;
        if (!to) continue;
        *to = items[i];
        s_at[to == &s_row ? 0 : to == &s_group_row ? 1 : to == &s_far_row ? 2 : 3] = s_rows + i + 1;
    }
    s_rows += count;
}

/* The game's constants, as the gates read them (recomp_manual.c, DD_K...). */
static float k400 = 400.0f, k300 = 300.0f, k1_100 = 0.01f, k500 = 500.0f;

/* A crowd's person at depth d, as sub_000C29C0 answers: 0 not drawn, 2 full,
 * 3 fading. */
static float s_d;
static int s_state;
static void person(void)
{
    s_state = !(s_d < k400) ? 0 : s_d < k300 ? 2 : 3;
}
/* A prop at depth d, as sub_000B9B50 answers: 0 the near model, 1 fading, 2
 * too far; and its alpha when fading, as sub_000B9C70 gives it. */
static float s_alpha;
static void prop(void)
{
    s_state = s_d <= k400 ? 0 : s_d < k500 ? 1 : 2;
}
static void prop_fade(void)
{
    double a = 1.0 - ((double)s_d - k400) * k1_100;
    s_alpha = (float)(a > 1 ? 1 : a < 0 ? 0 : a);
}
static float *const k_person[] = { &k400, &k300 };
static float *const k_prop[] = { &k400, &k500 };
static float *const k_prop_fade[] = { &k400, &k1_100 };
static const unsigned char k_far_far[] = { 0, 0 }, k_far_per[] = { 0, 1 };

/* The person's alpha: its gate, then its draw's fade, taken at the step's
 * distances from the game's constants (sub_000C2FE0, written again). */
static float person_at(float d)
{
    s_d = d;
    draw_distance_with(k_person, k_far_far, 2, person);
    if (!s_state) return 0;
    return s_state == 3 ? draw_distance_fade(d, k400, k1_100) : 1.0f;
}
/* The prop's band, and s_alpha: 1 near, faded when fading, 0 too far. */
static int prop_at(float d)
{
    s_d = d;
    draw_distance_with(k_prop, k_far_far, 2, prop);
    s_alpha = s_state == 0 ? 1.0f : 0.0f;
    if (s_state == 1) draw_distance_with(k_prop_fade, k_far_per, 2, prop_fade);
    return s_state;
}

static int close_to(float a, float b) { return fabsf(a - b) < 1e-5f; }

/* What every step keeps: the constants back as they were after a call. */
static void constants_kept(void)
{
    CHECK(k400 == 400.0f && k300 == 300.0f && k1_100 == 0.01f && k500 == 500.0f);
}

static void check_step(int step)
{
    float k = (float)(step + 1);
    CHECK(draw_distance_step() == step);
    if (step == DRAW_DISTANCE_UNLIMITED) {
        /* No distance at all: the largest float, which no depth reaches, and
         * a fade's length the game's, as its start is never reached. */
        CHECK(draw_distance_far(400.0f) == FLT_MAX && draw_distance_far(15.0f) == FLT_MAX);
        CHECK(draw_distance_per(0.01f) == 0.01f);
        CHECK(person_at(4000.0f) == 1.0f && person_at(1e30f) == 1.0f);
        CHECK(prop_at(1e30f) == 0 && s_alpha == 1.0f);
        constants_kept();
        printf("unlimited: cut nowhere, faded nowhere\n");
        return;
    }
    CHECK(draw_distance_far(400.0f) == 400.0f * k && draw_distance_far(500.0f) == 500.0f * k);
    CHECK(draw_distance_far(600.0f) == 600.0f * k);
    CHECK(draw_distance_per(0.01f) == 0.01f / k && draw_distance_per(0.02f) == 0.02f / k);
    if (step == 0) {
        /* 1x is the game's, to the bit. */
        CHECK(draw_distance_far(400.0f) == 400.0f && draw_distance_per(0.01f) == 0.01f);
        CHECK(draw_distance_fade(350.0f, 400.0f, 0.01f) == (float)((400.0 - 350.0f) * 0.01f));
    }
    /* A person: full to 300 x step, fading to 400 x step, gone there. */
    CHECK(person_at(300.0f * k - 1) == 1.0f);
    CHECK(close_to(person_at(300.0f * k), 1.0f));
    CHECK(close_to(person_at(350.0f * k), 0.5f));
    CHECK(person_at(400.0f * k) == 0.0f && person_at(400.0f * k + 1) == 0.0f);
    /* A prop: near to 400 x step, fading to 500 x step, gone there. */
    CHECK(prop_at(400.0f * k) == 0 && s_alpha == 1.0f);
    CHECK(prop_at(450.0f * k) == 1 && close_to(s_alpha, 0.5f));
    CHECK(prop_at(500.0f * k) == 2 && prop_at(500.0f * k + 1) == 2);
    constants_kept();
    printf("step %d: person to %g, prop to %g\n", step, 400.0 * k, 500.0 * k);
}

/* ---- Far buildings ------------------------------------------------------ */

/* An area's volume, or a cell, as the game keeps it: a box from lo to hi, its
 * 6 planes' points first and then their normals. */
static void box(float *v, float x0, float y0, float z0, float x1, float y1, float z1)
{
    const float lo[3] = { x0, y0, z0 }, hi[3] = { x1, y1, z1 };
    int i, k;
    for (i = 0; i < 3; i++)
        for (k = 0; k < 3; k++) {
            float mid = (lo[k] + hi[k]) / 2;
            v[3 * (2 * i) + k] = k == i ? lo[k] : mid;
            v[3 * (2 * i + 1) + k] = k == i ? hi[k] : mid;
            v[18 + 3 * (2 * i) + k] = k == i ? 1.0f : 0.0f;       /* inwards */
            v[18 + 3 * (2 * i + 1) + k] = k == i ? -1.0f : 0.0f;
        }
}

/* The made-up map: areas 0 to 4 a street along x, 100 long each, area 4 a
 * copy of area 0 at the street's other end (both cut in two cells). Area 5 is
 * area 0's shape too, above it, and no list names it. Area 6 has area 1's
 * volume, moved, but a cell of its own. Areas 1, 2 and 3 are the same box
 * moved, and lists name them together, so they aren't copies to leave out. */
#define AREAS 7
static float s_vol[AREAS][36], s_cell[AREAS][2][36];
static const uint32_t k_list0[] = { 0, 1 }, k_list1[] = { 0, 1, 2 }, k_list2[] = { 1, 2, 3 },
                      k_list3[] = { 2, 3, 4, 6 }, k_list4[] = { 3, 4 }, k_list5[] = { 0 },
                      k_list6[] = { 3, 6 };
static DrawArea s_areas[AREAS];

static void map_make(void)
{
    static const uint32_t *const lists[AREAS] = { k_list0, k_list1, k_list2, k_list3, k_list4, k_list5, k_list6 };
    static const unsigned nlists[AREAS] = { 2, 3, 3, 4, 2, 1, 2 };
    int i;
    for (i = 0; i < 5; i++) {
        float x = 100.0f * i;
        int halves = i == 0 || i == 4;
        box(s_vol[i], x, 0, 0, x + 100, 50, 100);
        if (halves) {
            box(s_cell[i][0], x, 0, 0, x + 50, 50, 100);
            box(s_cell[i][1], x + 50, 0, 0, x + 100, 50, 100);
        } else {
            box(s_cell[i][0], x, 0, 0, x + 100, 50, 100);
        }
        s_areas[i].ncells = halves ? 2 : 1;
    }
    box(s_vol[5], 0, 500, 0, 100, 550, 100);
    box(s_cell[5][0], 0, 500, 0, 50, 550, 100);
    box(s_cell[5][1], 50, 500, 0, 100, 550, 100);
    s_areas[5].ncells = 2;
    box(s_vol[6], 100, 0, 500, 200, 50, 600);
    box(s_cell[6][0], 100, 0, 500, 150, 50, 600);
    s_areas[6].ncells = 1;
    for (i = 0; i < AREAS; i++) {
        s_areas[i].volume = s_vol[i];
        s_areas[i].cells = s_cell[i][0];
        s_areas[i].list = lists[i];
        s_areas[i].nlist = nlists[i];
        s_areas[i].fog_start = 100.0f * (float)(i + 1);
        s_areas[i].fog_end = 1000.0f * (float)(i + 1);
        s_areas[i].fog_density = 1.0f;
    }
}

/* The set the game makes from an eye in area `in`: that area's list. */
static void game_set(uint32_t set[DRAW_SET_WORDS], int in)
{
    unsigned e;
    memset(set, 0, DRAW_SET_WORDS * sizeof set[0]);
    for (e = 0; e < s_areas[in].nlist; e++) set[s_areas[in].list[e] >> 5] |= 1u << (s_areas[in].list[e] & 31);
}

/* The set as a mask of areas 0 to 31 (the map has 7). */
static uint32_t widened(int in, float x, int with_eye)
{
    uint32_t set[DRAW_SET_WORDS];
    const float eye[3] = { x, 25, 50 };
    int w;
    game_set(set, in);
    draw_buildings_widen(s_areas, AREAS, with_eye ? eye : NULL, set, 0.0, 0);
    for (w = 1; w < DRAW_SET_WORDS; w++) CHECK(!set[w]);
    return set[0];
}
#define AREA(i) (1u << (i))

static void check_copies(void)
{
    float d[3];
    /* Area 4 is area 0 moved 400 along x, each cell too; area 5 is area 0
     * moved 500 up. */
    CHECK(draw_area_copy(&s_areas[0], &s_areas[4], d) && d[0] == 400.0f && d[1] == 0 && d[2] == 0);
    CHECK(draw_area_copy(&s_areas[4], &s_areas[0], d) && d[0] == -400.0f);
    CHECK(draw_area_copy(&s_areas[0], &s_areas[5], d) && d[1] == 500.0f);
    /* The same box moved, as copies go, but lists name them together. */
    CHECK(draw_area_copy(&s_areas[1], &s_areas[3], d) && d[0] == 200.0f);
    /* Not copies: an area and itself, a cell of another shape, two cells
     * against one. */
    CHECK(!draw_area_copy(&s_areas[0], &s_areas[0], d));
    CHECK(!draw_area_copy(&s_areas[1], &s_areas[6], d));
    CHECK(!draw_area_copy(&s_areas[0], &s_areas[1], d));
    printf("copies: area 4 of area 0, (400, 0, 0) away\n");
}

/* The map's shadow passes (recomp_manual.c, sub_0006BA80): the game's memory
 * holds the widened set, and a pass reads it as the game's does. */
static uint32_t s_live[DRAW_SET_WORDS], s_seen[DRAW_SET_WORDS];
static int s_passes;
static void shadow_pass(void)
{
    memcpy(s_seen, s_live, sizeof s_seen);
    s_passes++;
}

/* Two calls of the shadow passes (the game's pass switch sends its passes 17
 * and 18 to sub_0006BA80), from area 0 with Far buildings on: with Far
 * shadows off, each sees only the game's own areas, 0 and 1, so no area Far
 * buildings added has its volume counted (known bug 52); on, each sees the
 * widened set, as b2's did. After each the widened set is back for what
 * draws next. */
static void check_shadow_passes(void)
{
    uint32_t game[DRAW_SET_WORDS];
    const uint32_t wide = AREA(0) | AREA(1) | AREA(2) | AREA(3) | AREA(6);
    int w, passes = s_passes;
    game_set(game, 0);
    memcpy(s_live, game, sizeof s_live);
    draw_buildings_widen(s_areas, AREAS, NULL, s_live, 0.0, 0);
    CHECK(s_live[0] == wide);
    for (w = 0; w < 2; w++) {
        memset(s_seen, 0xFF, sizeof s_seen);
        draw_buildings_shadows(s_live, game, shadow_pass);
        if (draw_far_shadows()) CHECK(s_seen[0] == wide);
        else CHECK(!memcmp(s_seen, game, sizeof s_seen) && s_seen[0] == (AREA(0) | AREA(1)));
        CHECK(s_live[0] == wide);
    }
    for (w = 1; w < DRAW_SET_WORDS; w++) CHECK(!s_live[w] && !s_seen[w]);
    CHECK(s_passes == passes + 2);
}

static void check_shadows(void)
{
    CHECK(!draw_far_shadows());
    check_shadow_passes();
    printf("far buildings: the shadows from the game's own areas\n");
}

static void check_far(void)
{
    CHECK(draw_far_buildings());
    map_make();
    check_copies();
    /* From area 0: the game's 0 and 1; added 2, 3 and 6, never 5, which no
     * list names; and not 4, a copy of 0, which the set has. */
    CHECK(widened(0, 50, 1) == (AREA(0) | AREA(1) | AREA(2) | AREA(3) | AREA(6)));
    /* From area 3: the game's 2, 3, 4 and 6; added 1, and not 0, a copy of 4. */
    CHECK(widened(3, 350, 1) == (AREA(1) | AREA(2) | AREA(3) | AREA(4) | AREA(6)));
    /* From area 2, nearer 4 than 0: of the two copies, the nearer, 4. */
    CHECK(widened(2, 260, 1) == (AREA(1) | AREA(2) | AREA(3) | AREA(4) | AREA(6)));
    /* From area 2, nearer 0: 0. */
    CHECK(widened(2, 240, 1) == (AREA(0) | AREA(1) | AREA(2) | AREA(3) | AREA(6)));
    /* No eye: in the areas' order. */
    CHECK(widened(2, 0, 0) == (AREA(0) | AREA(1) | AREA(2) | AREA(3) | AREA(6)));
    check_shadows();
    /* Another map, without area 6: worked out again, and logged again. */
    {
        uint32_t set[DRAW_SET_WORDS];
        game_set(set, 0);
        draw_buildings_widen(s_areas, AREAS - 1, NULL, set, 0.0, 0);
        CHECK(set[0] == (AREA(0) | AREA(1) | AREA(2) | AREA(3)));
    }
    printf("far buildings: every listed area, the nearer of two copies\n");
}

/* ---- the Misc group's log ------------------------------------------------ */

#define MISC_OBJECT(i) (0x01E5E1C0u + 0x5A0u * (uint32_t)(i))
#define MISC_MODEL(i)  (0x2C40u + 0x10u * (uint32_t)(i))

/* A map's set from area 0, so that Far buildings takes it: the first call on
 * a map starts the Misc group's log again. */
static void misc_map(int n)
{
    uint32_t set[DRAW_SET_WORDS];
    game_set(set, 0);
    draw_buildings_widen(s_areas, n, NULL, set, 0.0, 0);
}

/* The game's draws, as the place that flooded b2's log draws them: 100
 * objects (more than the 64 b2's log kept), each drawn twice a frame for 180
 * frames, in areas 0 to 6, each with a model of its own but objects 98 and 99,
 * which have object 97's, in its area. Every third object's places rise, to 1
 * at frame 60 and 2 at frame 120; object 0's fall back to 1 at frame 150.
 * Object 5 goes from area 5 to area 6 at frame 90. run.py checks the lines:
 * 99 models and areas named once, and 68 new highs. */
static void misc_place(void)
{
    int f, i, twice;
    for (f = 0; f < 180; f++)
        for (i = 0; i < 100; i++)
            for (twice = 0; twice < 2; twice++) {
                int m = i < 98 ? i : 97;
                uint32_t places = i % 3 ? 0 : f >= 120 ? 2 : f >= 60 ? 1 : 0;
                uint32_t area = (uint32_t)(m % 7);
                if (i == 0 && f >= 150) places = 1;
                if (i == 5 && f >= 90) area = 6;
                draw_misc_drawn(MISC_OBJECT(i), MISC_MODEL(m), area, places);
            }
}

static void check_misc(void)
{
    int i;
    CHECK(draw_far_buildings());
    map_make();
    misc_map(AREAS);
    misc_place();
    /* Another map: each named again. Then more models than a map's lines:
     * the log stops, says so once, and says nothing more on this map, not
     * even a new high. */
    misc_map(AREAS - 1);
    for (i = 0; i < 10; i++) draw_misc_drawn(MISC_OBJECT(i), MISC_MODEL(i), (uint32_t)(i % 7), 0);
    for (i = 0; i < 3000; i++) draw_misc_drawn(MISC_OBJECT(i), 0x100000u + (uint32_t)i, 1, 0);
    draw_misc_drawn(MISC_OBJECT(0), MISC_MODEL(0), 0, 5);
    /* The first map again: a new map to the log, which starts again. */
    misc_map(AREAS);
    draw_misc_drawn(MISC_OBJECT(0), MISC_MODEL(0), 0, 0);
    /* Off and on from the menu, the game on another map by then, and a draw
     * there before Far buildings has read it: the log waits for the map, so
     * object 1 is named once on it, not once before and again after. */
    s_far_row.run();
    draw_misc_drawn(MISC_OBJECT(1), MISC_MODEL(1), 1, 0);
    s_far_row.run();
    CHECK(draw_far_buildings());
    draw_misc_drawn(MISC_OBJECT(1), MISC_MODEL(1), 1, 0);
    misc_map(AREAS - 1);
    draw_misc_drawn(MISC_OBJECT(1), MISC_MODEL(1), 1, 0);
    printf("misc: each model once an area, again at a new high, at most %d lines a map\n", DRAW_MISC_LINES);
}

/* Far shadows on, as by default: the shadows from the widened set; then the
 * row, off at once (and kept, which run.py checks): the game's own. */
static void check_far_shadows(void)
{
    CHECK(draw_far_buildings() && draw_far_shadows() && s_shadow_row.state());
    map_make();
    check_shadow_passes();
    s_shadow_row.run();
    CHECK(!draw_far_shadows() && !s_shadow_row.state());
    check_shadow_passes();
    printf("far shadows: the shadows from the widened set, then the game's again\n");
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    draw_distance_start();
    /* Four rows, in one call, on the Display page: a choice under HUD,
     * showing the setting, then the Building distance submenu under HUD
     * too, below it, and in the submenu two toggles, Far buildings then Far
     * shadows. */
    CHECK(s_rows == 4 && s_at[0] == 1 && s_at[1] == 2 && s_at[2] == 3 && s_at[3] == 4);
    CHECK(s_group_row.menu == XBOX_MENU_DISPLAY && s_group_row.kind == XBOX_ITEM_GROUP && !s_group_row.last);
    CHECK(s_group_row.under && !strcmp(s_group_row.under, "HUD") && !s_group_row.group);
    CHECK(s_group_row.presets == XBOX_PRESETS_UNSAID);   /* a submenu is no setting (menu.h) */
    CHECK(s_far_row.menu == XBOX_MENU_DISPLAY && s_far_row.kind == XBOX_ITEM_TOGGLE && !s_far_row.last);
    CHECK(!s_far_row.confirm && s_far_row.run);
    CHECK(s_far_row.group && !strcmp(s_far_row.group, "Building distance") && !s_far_row.under);
    CHECK(s_far_row.state() == draw_far_buildings());
    CHECK(s_shadow_row.menu == XBOX_MENU_DISPLAY && s_shadow_row.kind == XBOX_ITEM_TOGGLE && !s_shadow_row.last);
    CHECK(!s_shadow_row.confirm && s_shadow_row.run);
    CHECK(s_shadow_row.group && !strcmp(s_shadow_row.group, "Building distance") && !s_shadow_row.under);
    CHECK(s_shadow_row.state() == draw_far_shadows());
    CHECK(s_row.menu == XBOX_MENU_DISPLAY && s_row.kind == XBOX_ITEM_CHOICE && !s_row.last);
    CHECK(!s_row.confirm && !s_row.group);
    CHECK(s_row.under && !strcmp(s_row.under, "HUD"));
    CHECK(s_row.nchoices == DRAW_DISTANCE_STEPS);
    /* Each with a hint, a sentence short enough for the strip (menu.h). */
    CHECK(s_row.hint && strlen(s_row.hint) <= MENU_HINT_LONGEST && s_row.hint[strlen(s_row.hint) - 1] == '.');
    CHECK(s_group_row.hint && strlen(s_group_row.hint) <= MENU_HINT_LONGEST &&
          s_group_row.hint[strlen(s_group_row.hint) - 1] == '.');
    CHECK(s_far_row.hint && strlen(s_far_row.hint) <= MENU_HINT_LONGEST &&
          s_far_row.hint[strlen(s_far_row.hint) - 1] == '.');
    /* Far shadows' gives the pop-in it saves and the glitches it can cause. */
    CHECK(s_shadow_row.hint && strlen(s_shadow_row.hint) <= MENU_HINT_LONGEST &&
          s_shadow_row.hint[strlen(s_shadow_row.hint) - 1] == '.' && strstr(s_shadow_row.hint, "pop-in") &&
          strstr(s_shadow_row.hint, "glitches"));
    CHECK(!strcmp(s_row.choices[0], "1x") && !strcmp(s_row.choices[3], "4x"));
    CHECK(!strcmp(s_row.choices[DRAW_DISTANCE_UNLIMITED], "Unlimited"));
    CHECK(s_row.state() == draw_distance_step());
    /* File > Presets: the game's own 1x and areas in Xbox mode, the most in
     * Max quality, and JSRF-PC default's what a start with neither key
     * gives. */
    CHECK(s_row.presets == XBOX_PRESETS_SET && !s_row.keep && s_far_row.presets == XBOX_PRESETS_SET && !s_far_row.keep);
    CHECK(s_row.preset[XBOX_PRESET_XBOX] == 0 && s_row.preset[XBOX_PRESET_MAX] == DRAW_DISTANCE_UNLIMITED);
    CHECK(s_far_row.preset[XBOX_PRESET_XBOX] == 0 && s_far_row.preset[XBOX_PRESET_MAX] == 1);
    CHECK(s_shadow_row.presets == XBOX_PRESETS_SET && !s_shadow_row.keep);
    CHECK(s_shadow_row.preset[XBOX_PRESET_XBOX] == 0 && s_shadow_row.preset[XBOX_PRESET_MAX] == 1);
    if (!strcmp(c, "default"))
        CHECK(s_row.preset[XBOX_PRESET_JSRF_PC] == draw_distance_step() &&
              s_far_row.preset[XBOX_PRESET_JSRF_PC] == draw_far_buildings() &&
              s_shadow_row.preset[XBOX_PRESET_JSRF_PC] == draw_far_shadows());
    if (!strcmp(c, "off")) check_step(0);
    /* None, or one the row doesn't have: 2x, the default. */
    if (!strcmp(c, "default") || !strncmp(c, "bad", 3)) check_step(1);
    if (!strcmp(c, "default")) CHECK(draw_far_buildings() && s_far_row.state());   /* on by default */
    if (!strcmp(c, "default")) CHECK(draw_far_shadows() && s_shadow_row.state());    /* on by default */
    if (!strcmp(c, "2x")) check_step(1);
    if (!strcmp(c, "3x")) check_step(2);
    if (!strcmp(c, "4x")) check_step(3);
    if (!strcmp(c, "unlimited")) check_step(DRAW_DISTANCE_UNLIMITED);
    if (!strcmp(c, "pick")) {
        /* 1x, then the row: 4x at once (and kept, which run.py checks); a
         * choice the row doesn't have changes nothing. */
        check_step(0);
        s_row.pick(3);
        CHECK(s_row.state() == 3);
        check_step(3);
        s_row.pick(DRAW_DISTANCE_STEPS);
        s_row.pick(-1);
        CHECK(s_row.state() == 3);
    }
    if (!strcmp(c, "log")) {
        /* 2x from the settings. Five seconds of 10 frames a second: each frame
         * 3 people, one past 1x's 400, and 2 props; then the line, and the
         * counts start again. */
        int f, i;
        CHECK(draw_distance_step() == 1);
        draw_distance_drawn(DRAW_CROWDS, 0, 0.0, 0);   /* starts the counts */
        for (f = 0; f < 50; f++) {
            double t = 0.05 + f * 0.1;
            for (i = 0; i < 3; i++) draw_distance_drawn(DRAW_CROWDS, i == 0, t, (unsigned)f);
            for (i = 0; i < 2; i++) draw_distance_drawn(DRAW_PROPS, 0, t, (unsigned)f);
        }
        fflush(stderr);
        draw_distance_drawn(DRAW_PICKUPS, 1, 5.1, 50);   /* the line, its counts first */
        draw_distance_drawn(DRAW_PICKUPS, 1, 6.0, 60);   /* none: under 5 s since */
        draw_distance_drawn(DRAW_GATES, 1, 99.0, 99);    /* not a gate: nothing */
    }
    if (!strcmp(c, "far_off")) {
        /* Off: the game's set as it is. */
        uint32_t set[DRAW_SET_WORDS];
        CHECK(!draw_far_buildings() && !s_far_row.state());
        map_make();
        game_set(set, 0);
        draw_buildings_widen(s_areas, AREAS, NULL, set, 0.0, 0);
        CHECK(set[0] == (AREA(0) | AREA(1)));
        /* And no Misc group lines. */
        misc_place();
    }
    if (!strcmp(c, "far_on")) check_far();
    if (!strcmp(c, "far_misc")) check_misc();
    if (!strcmp(c, "far_shadows")) check_far_shadows();
    if (!strcmp(c, "far_copies")) {
        /* JSRF_DRAW_COPIES: from area 0, 4 too. */
        CHECK(draw_far_buildings());
        map_make();
        CHECK(widened(0, 50, 1) == (AREA(0) | AREA(1) | AREA(2) | AREA(3) | AREA(4) | AREA(6)));
    }
    if (!strcmp(c, "far_flip")) {
        /* Off, then the row: on at once (and kept, which run.py checks). */
        CHECK(!draw_far_buildings());
        s_far_row.run();
        CHECK(draw_far_buildings() && s_far_row.state());
        map_make();
        CHECK(widened(0, 50, 1) == (AREA(0) | AREA(1) | AREA(2) | AREA(3) | AREA(6)));
    }
    if (!strncmp(c, "far_log", 7)) {
        /* Far buildings from the settings, at whatever step they give: five
         * seconds of 10 frames a second, a set from area 0 each, the gates
         * counting only at 2x; then the line. */
        int f;
        CHECK(draw_far_buildings());
        map_make();
        for (f = 0; f < 50; f++) {
            uint32_t set[DRAW_SET_WORDS], game[DRAW_SET_WORDS];
            game_set(set, 0);
            memcpy(game, set, sizeof game);
            draw_buildings_widen(s_areas, AREAS, NULL, set, 0.05 + f * 0.1, (unsigned)f);
            if (draw_distance_step()) draw_distance_drawn(DRAW_CROWDS, 0, 0.05 + f * 0.1, (unsigned)f);
            /* Two calls of the shadow passes a frame, from the game's areas,
             * or with Far shadows from all. */
            memcpy(s_live, set, sizeof s_live);
            draw_buildings_shadows(s_live, game, shadow_pass);
            draw_buildings_shadows(s_live, game, shadow_pass);
            CHECK(!memcmp(s_seen, draw_far_shadows() ? set : game, sizeof s_seen));
        }
        fflush(stderr);
        draw_distance_drawn(DRAW_GATES, 0, 5.1, 50);     /* not a gate: no line */
        {
            uint32_t set[DRAW_SET_WORDS];
            game_set(set, 0);
            draw_buildings_widen(s_areas, AREAS, NULL, set, 5.1, 50);   /* the line */
        }
    }
    printf("draw distance: %s\n", c);
    return 0;
}
