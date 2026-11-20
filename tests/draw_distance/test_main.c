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
 * made-up map here has their shape, a street that winds up one turn of a
 * spiral, its top right over its bottom and many crossings from it by the
 * lists, as Dogenzaka Hill's (known bug 57), with a side street and an area
 * no list names; and the sets the game would make from three eyes, widened
 * at each step. The map's shadow passes are given the widened set in the
 * game's memory, as the game's are, and must see the game's set widened to
 * Far shadows' step or Far buildings', the nearer.
 * The Misc group's log is given the draws of a place with more objects than
 * b2's log remembered, which wrote a line for every draw there, and a draw
 * just after Far buildings is turned on, before it has read the map.
 * Developer > Far buildings test's rows are pressed as the menu presses them,
 * between the game's sets, and the eye's area is followed through the sets
 * of the made-up map and of two tiny ones.
 * Dogenzaka Hill's own rule (known bug 66) is given a made-up map with the
 * shape of the hill's corner: 30 areas, as Stg12's, where the street, area
 * 0, and the corner, area 16, list area 14, only 14 lists area 13, up the
 * hill, and only 13 lists area 11, further up (known bug 69).
 * Chuo Street's rule for Far shadows (known bug 52) is given a made-up map of
 * 59 areas, as Stg11's, where the three areas it hides are named only by an
 * area two crossings from the eye.
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
static XBOX_MENU_ITEM s_test_row, s_leave_row, s_backdrop_row;     /* Developer > Far buildings test */
static int s_rows, s_at[7];     /* each one's place in the calls, from 1 */
static int s_calls, s_call_rows[4];     /* the calls, and each one's rows */
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    XBOX_MENU_ITEM *const caught[7] = { &s_row, &s_group_row, &s_far_row, &s_shadow_row,
                                        &s_test_row, &s_leave_row, &s_backdrop_row };
    int i, k;
    for (i = 0; i < count; i++) {
        const char *l = items[i].label ? items[i].label : "";
        XBOX_MENU_ITEM *to = !strcmp(l, "Draw distance") ? &s_row
                           : !strcmp(l, "Building distance") ? &s_group_row
                           : !strcmp(l, "Far buildings") ? &s_far_row
                           : !strcmp(l, "Far shadows") ? &s_shadow_row
                           : !strcmp(l, "Far buildings test") ? &s_test_row
                           : !strcmp(l, "Leave out area") ? &s_leave_row
                           : !strcmp(l, "Game's backdrop depth") ? &s_backdrop_row : NULL;
        if (!to) continue;
        *to = items[i];
        for (k = 0; k < 7; k++)
            if (caught[k] == to) s_at[k] = s_rows + i + 1;
    }
    if (s_calls < 4) s_call_rows[s_calls] = count;
    s_calls++;
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

/* An area's volume, as the game keeps it: a box from lo to hi, its 6 planes'
 * points first and then their normals. */
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

/* The made-up map: a street of areas 0 to 8 that climbs as it goes round
 * the 8 squares of a ring, 100 a square and 100 up an area, so that its
 * top, area 8, lies right over its bottom, area 0, as Dogenzaka Hill's does.
 * Each area's list names itself and the street's areas either side of it, so
 * by the lists the top is 7 crossings from the bottom, and no list near the
 * one names the other. Area 9 is a side street off area 3, which only area 3
 * names. Area 10 is an area no list names (a placeholder, as on the game's
 * maps), its own list naming area 0. */
#define AREAS  11
#define BOTTOM 0
#define TOP    8
#define SIDE   9
#define UNLISTED 10
static const int k_ring[8][2] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 2, 1 }, { 2, 2 }, { 1, 2 }, { 0, 2 }, { 0, 1 } };
static float s_vol[AREAS][36];
static const uint32_t k_list0[] = { 0, 1 }, k_list1[] = { 0, 1, 2 }, k_list2[] = { 1, 2, 3 },
                      k_list3[] = { 2, 3, 4, 9 }, k_list4[] = { 3, 4, 5 }, k_list5[] = { 4, 5, 6 },
                      k_list6[] = { 5, 6, 7 }, k_list7[] = { 6, 7, 8 }, k_list8[] = { 7, 8 },
                      k_list9[] = { 3, 9 }, k_list10[] = { 0 };
static DrawArea s_areas[AREAS];

static void map_make(void)
{
    static const uint32_t *const lists[AREAS] = { k_list0, k_list1, k_list2, k_list3, k_list4, k_list5,
                                                  k_list6, k_list7, k_list8, k_list9, k_list10 };
    static const unsigned nlists[AREAS] = { 2, 3, 3, 4, 3, 3, 3, 3, 2, 2, 1 };
    int i;
    for (i = 0; i <= TOP; i++) {
        float x = 100.0f * (float)k_ring[i % 8][0], z = 100.0f * (float)k_ring[i % 8][1];
        box(s_vol[i], x, 100.0f * (float)i, z, x + 100, 100.0f * (float)i + 50, z + 100);
    }
    box(s_vol[SIDE], 300, 300, 100, 400, 350, 200);
    box(s_vol[UNLISTED], 1000, 0, 1000, 1010, 10, 1010);     /* a 10-unit cube */
    for (i = 0; i < AREAS; i++) {
        s_areas[i].volume = s_vol[i];
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

#define AREA(i) (1u << (i))
#define RUN(a, b) (((1u << ((b) + 1)) - 1) & ~((1u << (a)) - 1))     /* areas a to b */

/* What each step draws from the eye in an area, as a mask of areas 0 to 31
 * (the map has 11): the game's set, then a crossing more a step, and at All
 * every area a list names. From the bottom the top comes in only at All,
 * from the top the bottom only at All, and the area no list names never. */
static const uint32_t k_from_bottom[DRAW_FAR_STEPS] = {
    RUN(0, 1), RUN(0, 2), RUN(0, 3), RUN(0, 4) | AREA(SIDE), RUN(0, 9) };
static const uint32_t k_from_top[DRAW_FAR_STEPS] = {
    RUN(7, 8), RUN(6, 8), RUN(5, 8), RUN(4, 8), RUN(0, 9) };
static const uint32_t k_from_middle[DRAW_FAR_STEPS] = {     /* area 4 */
    RUN(3, 5), RUN(2, 6) | AREA(SIDE), RUN(1, 7) | AREA(SIDE), RUN(0, 9), RUN(0, 9) };

/* The set from an eye in area `in`, at Far buildings' step, as a mask. */
static uint32_t widened(int in)
{
    uint32_t set[DRAW_SET_WORDS];
    int w;
    game_set(set, in);
    draw_buildings_widen(s_areas, AREAS, set, 0.0, 0);
    for (w = 1; w < DRAW_SET_WORDS; w++) CHECK(!set[w]);
    return set[0];
}

/* At each step: what it adds from the bottom, the top and the middle. The
 * top is never added from the bottom short of All, nor the bottom from the
 * top: the lists say neither is seen from the other (known bug 57, which
 * drew every listed area at every step). The side street comes in with
 * area 3, and the area no list names never. */
static void check_reach(int step)
{
    CHECK(draw_far_buildings() == step && s_far_row.state() == step);
    map_make();
    CHECK(widened(BOTTOM) == k_from_bottom[step]);
    CHECK(widened(TOP) == k_from_top[step]);
    CHECK(widened(4) == k_from_middle[step]);
    CHECK(!(widened(BOTTOM) & AREA(TOP)) == (step < DRAW_FAR_WHOLE));
    CHECK(!(widened(TOP) & AREA(BOTTOM)) == (step < DRAW_FAR_WHOLE));
    CHECK(!(widened(BOTTOM) & AREA(UNLISTED)) && !(widened(TOP) & AREA(UNLISTED)));
    /* Another map, without the area no list names: worked out again, and
     * logged again (run.py counts the lines). */
    {
        uint32_t set[DRAW_SET_WORDS];
        game_set(set, BOTTOM);
        draw_buildings_widen(s_areas, AREAS - 1, set, 0.0, 0);
        CHECK(set[0] == k_from_bottom[step]);
    }
    printf("far buildings %s: from the bottom %X, the top %X\n", s_far_row.choices[step],
           (unsigned)k_from_bottom[step], (unsigned)k_from_top[step]);
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
 * and 18 to sub_0006BA80), from the bottom with Far buildings on: each sees
 * the game's set widened to Far shadows' step or Far buildings', the nearer,
 * so with Far shadows off only the game's own areas, and no area Far
 * buildings added has its volume counted (known bug 52). After each the
 * widened set is back for what draws next. */
static void check_shadow_passes(void)
{
    uint32_t game[DRAW_SET_WORDS];
    int far = draw_far_buildings(), shadows = draw_far_shadows();
    int w, passes = s_passes;
    uint32_t wide = k_from_bottom[far], seen = k_from_bottom[shadows < far ? shadows : far];
    game_set(game, BOTTOM);
    memcpy(s_live, game, sizeof s_live);
    draw_buildings_widen(s_areas, AREAS, s_live, 0.0, 0);
    CHECK(s_live[0] == wide);
    for (w = 0; w < 2; w++) {
        memset(s_seen, 0xFF, sizeof s_seen);
        draw_buildings_shadows(s_live, game, shadow_pass);
        CHECK(s_seen[0] == seen);
        CHECK(s_live[0] == wide);
    }
    for (w = 1; w < DRAW_SET_WORDS; w++) CHECK(!s_live[w] && !s_seen[w]);
    CHECK(s_passes == passes + 2);
}

/* Every step of Far buildings with every step of Far shadows, from the rows
 * (each kept, which run.py checks of the last): the shadows never reach
 * further than the buildings. */
static void check_shadows(void)
{
    int far, shadows;
    map_make();
    for (far = 1; far < DRAW_FAR_STEPS; far++)
        for (shadows = 0; shadows < DRAW_FAR_STEPS; shadows++) {
            s_far_row.pick(far);
            s_shadow_row.pick(shadows);
            CHECK(draw_far_buildings() == far && draw_far_shadows() == shadows);
            check_shadow_passes();
        }
    /* A choice the row doesn't have changes nothing. */
    s_shadow_row.pick(DRAW_FAR_STEPS);
    s_shadow_row.pick(-1);
    CHECK(s_shadow_row.state() == DRAW_FAR_STEPS - 1);
    s_shadow_row.pick(0);
    printf("far shadows: the game's set widened as far as Far shadows, never past Far buildings\n");
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
    draw_buildings_widen(s_areas, n, set, 0.0, 0);
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
    s_far_row.pick(0);
    draw_misc_drawn(MISC_OBJECT(1), MISC_MODEL(1), 1, 0);
    s_far_row.pick(DRAW_FAR_DEFAULT);
    CHECK(draw_far_buildings());
    draw_misc_drawn(MISC_OBJECT(1), MISC_MODEL(1), 1, 0);
    misc_map(AREAS - 1);
    draw_misc_drawn(MISC_OBJECT(1), MISC_MODEL(1), 1, 0);
    printf("misc: each model once an area, again at a new high, at most %d lines a map\n", DRAW_MISC_LINES);
}

/* ---- Developer > Far buildings test -------------------------------------- */

/* A press of Leave out area's - or +, as the menu gives it: the value a step
 * below or above, within its bounds. */
static void leave_press(int by)
{
    double v = s_leave_row.value() + by;
    if (v < s_leave_row.min) v = s_leave_row.min;
    if (v > s_leave_row.max) v = s_leave_row.max;
    s_leave_row.set(v);
}

/* The shadows' set from the bottom, with Far shadows at shadows and the
 * widened set in the game's memory (check_shadow_passes). */
static uint32_t shadows_seen(void)
{
    uint32_t game[DRAW_SET_WORDS];
    game_set(game, BOTTOM);
    memcpy(s_live, game, sizeof s_live);
    draw_buildings_widen(s_areas, AREAS, s_live, 0.0, 0);
    draw_buildings_shadows(s_live, game, shadow_pass);
    return s_seen[0];
}

/* Two tiny maps for the eye's area: two areas whose lists make the same
 * set, as two areas side by side can; and a set no area makes. */
static const uint32_t k_twin_list[] = { 0, 1 };
static DrawArea s_twins[2];
static uint32_t widened_twins(void)
{
    uint32_t set[DRAW_SET_WORDS] = { AREA(0) | AREA(1) };
    s_twins[0].list = s_twins[1].list = k_twin_list;
    s_twins[0].nlist = s_twins[1].nlist = 2;
    draw_buildings_widen(s_twins, 2, set, 0.0, 0);
    return set[0];
}

/* Far buildings 3x from the settings, Far shadows 1x. From the bottom, Far
 * buildings adds areas 2, 3, 4 and the side street, 9. Each + leaves out the
 * next of them, from the game's next set (the press is stepped on the
 * game's thread), and Far shadows leaves it out too; after the last, none
 * again; - steps back, and from none stays there. An area the game's own set
 * has is never taken out. A new map starts at none again, and says so. The
 * backdrop's row flips draw_far_test_backdrop. run.py checks the lines, and
 * that neither row is kept. */
static void check_far_test(void)
{
    static const int k_steps[] = { 2, 3, 4, SIDE, -1 };
    uint32_t all = k_from_bottom[3];
    int i;
    CHECK(draw_far_buildings() == 3 && draw_far_shadows() == 1);
    map_make();
    CHECK(widened(BOTTOM) == all && s_leave_row.value() == -1);
    for (i = 0; i < 5; i++) {
        leave_press(1);
        CHECK(s_leave_row.value() == (i ? k_steps[i - 1] : -1));      /* not yet: the game's next set */
        CHECK(widened(BOTTOM) == (k_steps[i] < 0 ? all : all & ~AREA(k_steps[i])));
        CHECK(s_leave_row.value() == k_steps[i]);
    }
    leave_press(-1);                    /* from none, stays none */
    CHECK(widened(BOTTOM) == all && s_leave_row.value() == -1);
    leave_press(1);
    leave_press(1);                     /* two before the game's next set count as one */
    CHECK(widened(BOTTOM) == (all & ~AREA(2)));
    leave_press(1);
    CHECK(widened(BOTTOM) == (all & ~AREA(3)));
    leave_press(-1);
    CHECK(widened(BOTTOM) == (all & ~AREA(2)) && s_leave_row.value() == 2);
    /* Far shadows at 1x from the bottom: areas 0 to 2, without 2. */
    CHECK(shadows_seen() == RUN(0, 1));
    /* From area 1 the game draws area 2 itself: never taken out. */
    CHECK(widened(1) & AREA(2));
    CHECK(s_leave_row.value() == 2);
    /* A new map: none left out. */
    misc_map(AREAS - 1);
    CHECK(s_leave_row.value() == -1);
    CHECK(!draw_far_test_backdrop() && !s_backdrop_row.state());
    s_backdrop_row.run();
    CHECK(draw_far_test_backdrop() && s_backdrop_row.state());
    s_backdrop_row.run();
    CHECK(!draw_far_test_backdrop() && !s_backdrop_row.state());
    printf("far buildings test: each added area left out in turn, none after the last\n");
}

/* The eye's area through the made-up map's sets, then the tiny maps', at 1x.
 * run.py checks the lines: a change of area has one, with what Far buildings
 * adds there, up to DRAW_EYE_LINES between two [DRAW] lines, and the [DRAW]
 * line says the eye's area, its areas since the line before, and what Far
 * buildings adds from it. */
static void check_eye(void)
{
    uint32_t set[DRAW_SET_WORDS];
    int i;
    CHECK(draw_far_buildings() == 1);
    map_make();
    game_set(set, BOTTOM);
    draw_buildings_widen(s_areas, AREAS, set, 0.0, 0);        /* starts the counts */
    for (i = 0; i < 25; i++) {          /* 50 changes: 40 lines, the rest not */
        game_set(set, TOP);
        draw_buildings_widen(s_areas, AREAS, set, 0.1, 1);
        game_set(set, BOTTOM);
        draw_buildings_widen(s_areas, AREAS, set, 0.1, 1);
    }
    game_set(set, 4);
    draw_buildings_widen(s_areas, AREAS, set, 5.1, 50);        /* the [DRAW] line */
    game_set(set, 4);
    draw_buildings_widen(s_areas, AREAS, set, 10.2, 100);      /* and the next: area 4 since */
    /* A set no area's list makes: the area unknown. */
    memset(set, 0, sizeof set);
    set[0] = RUN(0, 3);
    draw_buildings_widen(s_areas, AREAS, set, 10.3, 101);
    /* Two areas whose lists make the same set: the first, and the log says so. */
    CHECK(widened_twins() == (AREA(0) | AREA(1)));
    printf("eye: its area through the sets\n");
}

/* ---- Dogenzaka Hill's rule ---------------------------------------------- */

/* A made-up map with the shape of Dogenzaka Hill's corner (known bug 66):
 * 30 areas, as Stg12 has, each listing itself, but the street, area 0,
 * which lists the corner, 16, and area 14 up from them; the corner, which
 * lists 0, 14 and a side area, 19; and area 14, which lists 13, further up
 * the hill, as well as 0 and 16; and 13, which lists 14 and 11, further up
 * still. So the game draws 13 from 13 and 14 only, and 1x adds it from 0
 * and 16 through 14's list; and it draws 11 from 11 and 13 only, and 2x adds
 * it from 0 and 16 through 13's (known bug 69). `last` is area 29's list, so
 * a second map, told from the first, can be made. */
#define HILL_AREAS  30
#define HILL_STAGE  0x00010002u     /* Stg12, as the mission asks for it */
#define STREET      0
#define PAST_UPHILL 11
#define UPHILL      13
#define CORNER      16
#define SIDE_AREA   19
static uint32_t s_hill_self[HILL_AREAS];
static const uint32_t k_street[] = { 0, 14, 16 }, k_up14[] = { 14, 13, 16, 0 },
                      k_corner[] = { 16, 0, 14, 19 }, k_uphill[] = { 13, 14, 11 };
static DrawArea s_hill[HILL_AREAS];

static void hill_make(uint32_t last)
{
    int i;
    memset(s_hill, 0, sizeof s_hill);
    for (i = 0; i < HILL_AREAS; i++) {
        s_hill_self[i] = (uint32_t)i;
        s_hill[i].list = &s_hill_self[i];
        s_hill[i].nlist = 1;
    }
    s_hill_self[HILL_AREAS - 1] = last;
    s_hill[STREET].list = k_street;     s_hill[STREET].nlist = 3;
    s_hill[14].list = k_up14;           s_hill[14].nlist = 4;
    s_hill[CORNER].list = k_corner;     s_hill[CORNER].nlist = 4;
    s_hill[UPHILL].list = k_uphill;     s_hill[UPHILL].nlist = 3;
}

/* The set from an eye in area `in` on the hill, widened, as a mask; n areas
 * of it. */
static uint32_t hill_widened(int in, int n)
{
    uint32_t set[DRAW_SET_WORDS] = { 0 };
    unsigned e;
    int w;
    set[0] = AREA(in);
    for (e = 0; e < s_hill[in].nlist; e++) set[0] |= AREA(s_hill[in].list[e]);
    draw_buildings_widen(s_hill, n, set, 0.0, 0);
    for (w = 1; w < DRAW_SET_WORDS; w++) CHECK(!set[w]);
    return set[0];
}

/* Far buildings 1x and Far shadows 1x from the settings. On the hill's map
 * after its stage, area 13 is never added from the street or the corner, at
 * 1x, 3x or All, nor to their shadows, and is still drawn from 13 and 14,
 * where the game draws it itself; the side area is still added. Nor is area
 * 11, past 13, reached only through 13's list, at 2x, 3x or All, from the
 * street or the corner (known bug 69); from 14, where the game draws 13, 1x
 * adds it. On a map after another stage, or on one of 29 areas after the
 * hill's stage, 13 and 11 are added from the street as before. run.py checks
 * the lines. */
static void check_hill(void)
{
    uint32_t game[DRAW_SET_WORDS] = { AREA(STREET) | AREA(14) | AREA(CORNER) };
    int step;
    CHECK(draw_far_buildings() == 1 && draw_far_shadows() == 1);
    hill_make(HILL_AREAS - 1);
    draw_buildings_stage(HILL_STAGE);
    CHECK(hill_widened(STREET, HILL_AREAS) == (game[0] | AREA(SIDE_AREA)));
    CHECK(hill_widened(CORNER, HILL_AREAS) == (game[0] | AREA(SIDE_AREA)));
    CHECK(hill_widened(14, HILL_AREAS) & AREA(UPHILL));
    CHECK(hill_widened(14, HILL_AREAS) & AREA(PAST_UPHILL));
    CHECK(hill_widened(UPHILL, HILL_AREAS) & AREA(UPHILL));
    /* Further: not at 2x, 3x or All either, nor what lies past 13; at All,
     * every other area, as each lists itself. */
    for (step = 2; step <= DRAW_FAR_WHOLE; step++) {
        uint32_t all = ~(AREA(UPHILL) | AREA(PAST_UPHILL)) & RUN(0, HILL_AREAS - 1);
        s_far_row.pick(step);
        CHECK(hill_widened(STREET, HILL_AREAS) == (step < DRAW_FAR_WHOLE ? game[0] | AREA(SIDE_AREA) : all));
        CHECK(hill_widened(CORNER, HILL_AREAS) == (step < DRAW_FAR_WHOLE ? game[0] | AREA(SIDE_AREA) : all));
        CHECK(hill_widened(14, HILL_AREAS) & AREA(PAST_UPHILL));
    }
    /* The shadows, at 1x under All: the game's set and the side area. */
    memcpy(s_live, game, sizeof s_live);
    draw_buildings_widen(s_hill, HILL_AREAS, s_live, 0.0, 0);
    draw_buildings_shadows(s_live, game, shadow_pass);
    CHECK(s_seen[0] == (game[0] | AREA(SIDE_AREA)));
    s_far_row.pick(1);
    /* Developer > Far buildings test steps through what is added: the side
     * area, never 13. */
    CHECK(hill_widened(STREET, HILL_AREAS) == (game[0] | AREA(SIDE_AREA)));
    leave_press(1);
    CHECK(hill_widened(STREET, HILL_AREAS) == game[0] && s_leave_row.value() == SIDE_AREA);
    leave_press(1);
    CHECK(hill_widened(STREET, HILL_AREAS) == (game[0] | AREA(SIDE_AREA)) && s_leave_row.value() == -1);
    /* Another map after another stage: 13 added from the street, and 11
     * past it at 2x. */
    hill_make(HILL_AREAS - 2);
    draw_buildings_stage(HILL_STAGE + 1);
    CHECK(hill_widened(STREET, HILL_AREAS) & AREA(UPHILL));
    s_far_row.pick(2);
    CHECK(hill_widened(STREET, HILL_AREAS) & AREA(PAST_UPHILL));
    s_far_row.pick(1);
    /* The hill's stage, but a map of 29 areas: not the one, so no rule. */
    draw_buildings_stage(HILL_STAGE);
    CHECK(hill_widened(STREET, HILL_AREAS - 1) & AREA(UPHILL));
    /* The hill again: the rule again. */
    hill_make(HILL_AREAS - 1);
    CHECK(hill_widened(STREET, HILL_AREAS) == (game[0] | AREA(SIDE_AREA)));
    printf("dogenzaka hill: area 13 and what lies past it never added from the street or the corner\n");
}

/* ---- Chuo Street's rule ------------------------------------------------- */

/* A made-up map with the shape of Chuo Street's halfpipe (known bug 52): 59
 * areas, as Stg11 has, each listing itself, but the halfpipe, area 1, which
 * lists 2 along it; area 2, which lists 1 and the end, 6; and the end, which
 * lists 2 and the three scenery areas past the end wall, 47, 48 and 50. So
 * from the halfpipe 1x adds the end and 2x the three. */
#define CHUO_AREAS  59
#define CHUO_STAGE  0x00010001u     /* Stg11, as the mission asks for it */
#define HALFPIPE    1
#define ALONG       2
#define END_WALL    6
#define SCENERY     (AREA(47 - 32) | AREA(48 - 32) | AREA(50 - 32))     /* in the set's second word */
static uint32_t s_chuo_self[CHUO_AREAS];
static const uint32_t k_halfpipe[] = { 1, 2 }, k_along[] = { 2, 1, 6 }, k_end[] = { 6, 2, 47, 48, 50 };
static DrawArea s_chuo[CHUO_AREAS];

static void chuo_make(void)
{
    int i;
    memset(s_chuo, 0, sizeof s_chuo);
    for (i = 0; i < CHUO_AREAS; i++) {
        s_chuo_self[i] = (uint32_t)i;
        s_chuo[i].list = &s_chuo_self[i];
        s_chuo[i].nlist = 1;
    }
    s_chuo[HALFPIPE].list = k_halfpipe;     s_chuo[HALFPIPE].nlist = 2;
    s_chuo[ALONG].list = k_along;           s_chuo[ALONG].nlist = 3;
    s_chuo[END_WALL].list = k_end;          s_chuo[END_WALL].nlist = 5;
}

/* From an eye in area `in`, n areas of the map: Far buildings' set, and what
 * the shadow passes see, each a set's second word, where 47, 48 and 50 are;
 * and whether the shadows' first word is the buildings'. */
static void chuo_sets(int in, int n, uint32_t *buildings, uint32_t *shadows, int *first_same)
{
    uint32_t game[DRAW_SET_WORDS] = { 0 };
    unsigned e;
    game[in >> 5] |= 1u << (in & 31);
    for (e = 0; e < s_chuo[in].nlist; e++) game[s_chuo[in].list[e] >> 5] |= 1u << (s_chuo[in].list[e] & 31);
    memcpy(s_live, game, sizeof s_live);
    draw_buildings_widen(s_chuo, n, s_live, 0.0, 0);
    memset(s_seen, 0xFF, sizeof s_seen);
    draw_buildings_shadows(s_live, game, shadow_pass);
    *buildings = s_live[1];
    *shadows = s_seen[1];
    *first_same = s_seen[0] == s_live[0];
}

/* Far buildings 2x and Far shadows 2x from the settings. On Chuo Street's
 * map after its stage, the shadows never take 47, 48 and 50 from the
 * halfpipe, at 2x or at All, though Far buildings adds them; the rest of the
 * shadows' set is Far buildings'. From the end, whose own list names them,
 * the game draws them and their shadows stay. On a map after another stage,
 * or on one of 58 areas after Chuo Street's stage, the shadows take them as
 * before. run.py checks the lines. */
static void check_chuo(void)
{
    uint32_t buildings, shadows;
    int same;
    CHECK(draw_far_buildings() == 2 && draw_far_shadows() == 2);
    chuo_make();
    draw_buildings_stage(CHUO_STAGE);
    chuo_sets(HALFPIPE, CHUO_AREAS, &buildings, &shadows, &same);
    CHECK(buildings == SCENERY && !shadows && same);
    s_far_row.pick(DRAW_FAR_WHOLE);
    s_shadow_row.pick(DRAW_FAR_WHOLE);
    chuo_sets(HALFPIPE, CHUO_AREAS, &buildings, &shadows, &same);
    CHECK((buildings & SCENERY) == SCENERY && !(shadows & SCENERY) && shadows == (buildings & ~SCENERY) && same);
    /* Far shadows below Far buildings: the shadows' own step, still without
     * them. */
    s_shadow_row.pick(1);
    chuo_sets(HALFPIPE, CHUO_AREAS, &buildings, &shadows, &same);
    CHECK((buildings & SCENERY) == SCENERY && !shadows);
    s_shadow_row.pick(DRAW_FAR_WHOLE);
    /* From the end the game draws them itself: their shadows stay. */
    chuo_sets(END_WALL, CHUO_AREAS, &buildings, &shadows, &same);
    CHECK((shadows & SCENERY) == SCENERY && same);
    /* Another map after another stage, and one of 58 areas after Chuo
     * Street's: the shadows take them. */
    s_far_row.pick(2);
    s_shadow_row.pick(2);
    draw_buildings_stage(CHUO_STAGE + 1);
    s_chuo_self[CHUO_AREAS - 1] = CHUO_AREAS - 2;      /* another map, told from the first */
    chuo_sets(HALFPIPE, CHUO_AREAS, &buildings, &shadows, &same);
    CHECK(buildings == SCENERY && shadows == SCENERY);
    draw_buildings_stage(CHUO_STAGE);
    chuo_sets(HALFPIPE, CHUO_AREAS - 1, &buildings, &shadows, &same);
    CHECK(buildings == SCENERY && shadows == SCENERY);
    printf("chuo street: the shadows of areas 47, 48 and 50 never added\n");
}

/* The whole map's set, for the free camera when its eye and the game's
 * camera's are both in no area (recomp_manual.c, area_set_from_game_eye;
 * known bug 61): every area some list names, as All has it, whatever Far
 * buildings' step, Off included; never the area no list names; added to
 * what the set holds; nothing for no map. */
static void check_whole_map(void)
{
    uint32_t set[DRAW_SET_WORDS];
    int w;
    CHECK(draw_far_buildings() == 0);
    map_make();
    memset(set, 0, sizeof set);
    CHECK(draw_set_whole_map(s_areas, AREAS, set) == 10);
    CHECK(set[0] == RUN(0, 9) && !(set[0] & AREA(UNLISTED)));
    for (w = 1; w < DRAW_SET_WORDS; w++) CHECK(!set[w]);
    /* Over a set that holds some already, and over the area no list names:
     * added to, never taken from. */
    memset(set, 0, sizeof set);
    set[0] = AREA(UNLISTED);
    CHECK(draw_set_whole_map(s_areas, AREAS, set) == 11 && set[0] == (RUN(0, 9) | AREA(UNLISTED)));
    /* The map without the area no list names: the same 10. */
    memset(set, 0, sizeof set);
    CHECK(draw_set_whole_map(s_areas, AREAS - 1, set) == 10 && set[0] == RUN(0, 9));
    /* No map, or none of it read: the set as it was. */
    memset(set, 0, sizeof set);
    CHECK(draw_set_whole_map(NULL, AREAS, set) == 0 && !set[0]);
    CHECK(draw_set_whole_map(s_areas, 0, set) == 0 && !set[0]);
    /* A list naming an area past the map's end is left out, as Far
     * buildings leaves it. */
    {
        static const uint32_t far_list[] = { 0, 40 };
        DrawArea one = s_areas[0];
        one.list = far_list;
        one.nlist = 2;
        memset(set, 0, sizeof set);
        CHECK(draw_set_whole_map(&one, 1, set) == 1 && set[0] == AREA(0) && !set[1]);
    }
    printf("whole map: %X\n", (unsigned)RUN(0, 9));
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    draw_distance_start();
    /* Four rows, in one call, on the Display page: a choice under HUD,
     * showing the setting, then the Building distance submenu under HUD
     * too, below it, and in the submenu two choices, Far buildings then Far
     * shadows, each Off, 1x, 2x, 3x or All. Then three in a second call, on
     * the Developer page: the Far buildings test submenu, and in it Leave
     * out area, a number from -1 (none) up, and Game's backdrop depth, a
     * tick, both off; neither set by the presets, as Developer's rows aren't
     * (docs/menu-format.md). */
    CHECK(s_calls == 2 && s_call_rows[0] == 4 && s_call_rows[1] == 3);
    CHECK(s_rows == 7 && s_at[0] == 1 && s_at[1] == 2 && s_at[2] == 3 && s_at[3] == 4);
    CHECK(s_at[4] == 5 && s_at[5] == 6 && s_at[6] == 7);
    CHECK(s_test_row.menu == XBOX_MENU_DEVELOPER && s_test_row.kind == XBOX_ITEM_GROUP);
    CHECK(!s_test_row.group && !s_test_row.under && !s_test_row.last);
    CHECK(s_leave_row.menu == XBOX_MENU_DEVELOPER && s_leave_row.kind == XBOX_ITEM_NUMBER);
    CHECK(s_leave_row.group && !strcmp(s_leave_row.group, "Far buildings test"));
    CHECK(s_leave_row.min == -1 && s_leave_row.max == DRAW_AREAS_MAX - 1 && s_leave_row.step == 1);
    CHECK(s_leave_row.value && s_leave_row.set && s_leave_row.value() == -1);
    CHECK(s_backdrop_row.menu == XBOX_MENU_DEVELOPER && s_backdrop_row.kind == XBOX_ITEM_TOGGLE);
    CHECK(s_backdrop_row.group && !strcmp(s_backdrop_row.group, "Far buildings test"));
    CHECK(s_backdrop_row.state && s_backdrop_row.run && !s_backdrop_row.state() && !draw_far_test_backdrop());
    CHECK(s_test_row.presets == XBOX_PRESETS_UNSAID && s_leave_row.presets == XBOX_PRESETS_UNSAID &&
          s_backdrop_row.presets == XBOX_PRESETS_UNSAID);
    CHECK(!s_leave_row.confirm && !s_backdrop_row.confirm);
    {
        const XBOX_MENU_ITEM *const tests[3] = { &s_test_row, &s_leave_row, &s_backdrop_row };
        int i;
        for (i = 0; i < 3; i++)
            CHECK(tests[i]->hint && strlen(tests[i]->hint) <= MENU_HINT_LONGEST &&
                  tests[i]->hint[strlen(tests[i]->hint) - 1] == '.');
    }
    CHECK(s_group_row.menu == XBOX_MENU_DISPLAY && s_group_row.kind == XBOX_ITEM_GROUP && !s_group_row.last);
    CHECK(s_group_row.under && !strcmp(s_group_row.under, "HUD") && !s_group_row.group);
    CHECK(s_group_row.presets == XBOX_PRESETS_UNSAID);   /* a submenu is no setting (menu.h) */
    CHECK(s_far_row.menu == XBOX_MENU_DISPLAY && s_far_row.kind == XBOX_ITEM_CHOICE && !s_far_row.last);
    CHECK(!s_far_row.confirm && s_far_row.pick && s_far_row.nchoices == DRAW_FAR_STEPS);
    CHECK(s_far_row.group && !strcmp(s_far_row.group, "Building distance") && !s_far_row.under);
    CHECK(s_far_row.state() == draw_far_buildings());
    CHECK(s_shadow_row.menu == XBOX_MENU_DISPLAY && s_shadow_row.kind == XBOX_ITEM_CHOICE && !s_shadow_row.last);
    CHECK(!s_shadow_row.confirm && s_shadow_row.pick && s_shadow_row.nchoices == DRAW_FAR_STEPS);
    {
        static const char *const names[DRAW_FAR_STEPS] = { "Off", "1x", "2x", "3x", "All" };
        int i;
        for (i = 0; i < DRAW_FAR_STEPS; i++)
            CHECK(!strcmp(s_far_row.choices[i], names[i]) && !strcmp(s_shadow_row.choices[i], names[i]));
    }
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
    /* The submenu's gives the pop-in they save, and Far shadows' the
     * glitches it can cause. */
    CHECK(strstr(s_group_row.hint, "pop-in"));
    CHECK(s_shadow_row.hint && strlen(s_shadow_row.hint) <= MENU_HINT_LONGEST &&
          s_shadow_row.hint[strlen(s_shadow_row.hint) - 1] == '.' && strstr(s_shadow_row.hint, "glitches"));
    CHECK(!strcmp(s_row.choices[0], "1x") && !strcmp(s_row.choices[3], "4x"));
    CHECK(!strcmp(s_row.choices[DRAW_DISTANCE_UNLIMITED], "Unlimited"));
    CHECK(s_row.state() == draw_distance_step());
    /* File > Presets: the game's own 1x and areas in Xbox mode (Off), the
     * most in Max quality (Unlimited, but 3x for Far buildings and Far
     * shadows, as All draws the sky buildings of known bug 57), and JSRF-PC
     * default's what a start with none of the keys gives. */
    CHECK(s_row.presets == XBOX_PRESETS_SET && !s_row.keep && s_far_row.presets == XBOX_PRESETS_SET && !s_far_row.keep);
    CHECK(s_row.preset[XBOX_PRESET_XBOX] == 0 && s_row.preset[XBOX_PRESET_MAX] == DRAW_DISTANCE_UNLIMITED);
    CHECK(s_far_row.preset[XBOX_PRESET_XBOX] == 0 && s_far_row.preset[XBOX_PRESET_MAX] == 3);
    CHECK(s_shadow_row.presets == XBOX_PRESETS_SET && !s_shadow_row.keep);
    CHECK(s_shadow_row.preset[XBOX_PRESET_XBOX] == 0 && s_shadow_row.preset[XBOX_PRESET_MAX] == 3);
    if (!strcmp(c, "default"))
        CHECK(s_row.preset[XBOX_PRESET_JSRF_PC] == draw_distance_step() &&
              s_far_row.preset[XBOX_PRESET_JSRF_PC] == draw_far_buildings() &&
              s_shadow_row.preset[XBOX_PRESET_JSRF_PC] == draw_far_shadows());
    if (!strcmp(c, "off")) check_step(0);
    /* None, or one the row doesn't have: 2x, the default. */
    if (!strcmp(c, "default") || !strncmp(c, "bad", 3)) check_step(1);
    /* Far buildings and Far shadows 3x by default, and from a key the row
     * doesn't have. */
    if (!strcmp(c, "default") || !strncmp(c, "far_bad", 7)) {
        CHECK(draw_far_buildings() == 3 && s_far_row.state() == 3 && DRAW_FAR_DEFAULT == 3);
        CHECK(draw_far_shadows() == 3 && s_shadow_row.state() == 3 && DRAW_SHADOWS_DEFAULT == 3);
    }
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
        /* Off: the game's set as it is, and no Misc group lines. */
        check_reach(0);
        misc_place();
    }
    if (!strcmp(c, "reach_1x")) check_reach(1);
    if (!strcmp(c, "reach_2x")) check_reach(2);
    if (!strcmp(c, "reach_3x")) check_reach(3);
    if (!strcmp(c, "reach_all")) check_reach(DRAW_FAR_WHOLE);
    if (!strcmp(c, "far_misc")) check_misc();
    if (!strcmp(c, "far_test")) check_far_test();
    if (!strcmp(c, "far_eye")) check_eye();
    if (!strcmp(c, "far_hill")) check_hill();
    if (!strcmp(c, "far_chuo")) check_chuo();
    if (!strcmp(c, "shadows")) check_shadows();
    if (!strcmp(c, "whole_map")) check_whole_map();
    if (!strcmp(c, "far_pick")) {
        /* Off from its own key, whatever the old toggle says; then the row:
         * 2x at once (and kept, which run.py checks); a choice the row
         * doesn't have changes nothing. */
        CHECK(!draw_far_buildings() && !s_far_row.state());
        s_far_row.pick(2);
        s_far_row.pick(DRAW_FAR_STEPS);
        s_far_row.pick(-1);
        check_reach(2);
    }
    if (!strcmp(c, "shadows_pick")) {
        /* The row: 1x at once (and kept alone, which run.py checks). */
        s_shadow_row.pick(1);
        CHECK(draw_far_shadows() == 1 && s_shadow_row.state() == 1);
    }
    if (!strncmp(c, "far_log", 7)) {
        /* Far buildings and Far shadows from the settings, at whatever steps
         * they give: five seconds of 10 frames a second, a set from the
         * bottom each, the gates counting only at 2x; then the line. */
        int f;
        CHECK(draw_far_buildings());
        map_make();
        for (f = 0; f < 50; f++) {
            uint32_t set[DRAW_SET_WORDS], game[DRAW_SET_WORDS];
            int far = draw_far_buildings(), shadows = draw_far_shadows();
            game_set(set, BOTTOM);
            memcpy(game, set, sizeof game);
            draw_buildings_widen(s_areas, AREAS, set, 0.05 + f * 0.1, (unsigned)f);
            if (draw_distance_step()) draw_distance_drawn(DRAW_CROWDS, 0, 0.05 + f * 0.1, (unsigned)f);
            /* Two calls of the shadow passes a frame, from the game's set
             * widened to Far shadows' step. */
            memcpy(s_live, set, sizeof s_live);
            draw_buildings_shadows(s_live, game, shadow_pass);
            draw_buildings_shadows(s_live, game, shadow_pass);
            CHECK(s_seen[0] == k_from_bottom[shadows < far ? shadows : far]);
        }
        fflush(stderr);
        draw_distance_drawn(DRAW_GATES, 0, 5.1, 50);     /* not a gate: no line */
        {
            uint32_t set[DRAW_SET_WORDS];
            game_set(set, BOTTOM);
            draw_buildings_widen(s_areas, AREAS, set, 5.1, 50);   /* the line */
        }
    }
    printf("draw distance: %s\n", c);
    return 0;
}
