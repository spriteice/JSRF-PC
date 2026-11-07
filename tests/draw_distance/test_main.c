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
 */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "draw_distance.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static XBOX_MENU_ITEM s_row;
static int s_rows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    if (count > 0) s_row = items[0];
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

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    draw_distance_start();
    /* One row: a choice under Render scale on the Display page, showing the
     * setting. */
    CHECK(s_rows == 1);
    CHECK(s_row.menu == XBOX_MENU_DISPLAY && s_row.kind == XBOX_ITEM_CHOICE && !s_row.last);
    CHECK(!strcmp(s_row.label, "Draw distance") && !s_row.confirm);
    CHECK(s_row.under && !strcmp(s_row.under, "Render scale (Restart)"));
    CHECK(s_row.nchoices == DRAW_DISTANCE_STEPS);
    CHECK(!strcmp(s_row.choices[0], "1x") && !strcmp(s_row.choices[3], "4x"));
    CHECK(!strcmp(s_row.choices[DRAW_DISTANCE_UNLIMITED], "Unlimited"));
    CHECK(s_row.state() == draw_distance_step());
    if (!strcmp(c, "off") || !strncmp(c, "bad", 3)) check_step(0);
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
    printf("draw distance: %s\n", c);
    return 0;
}
