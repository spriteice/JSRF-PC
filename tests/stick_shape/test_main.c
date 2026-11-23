/* Run with: python tests/stick_shape/run.py
 *
 * Stick shape without the game: src/input/stick_shape.c as it is, with the
 * settings file and the menu's xbox_MenuAdd stood in for. One case a run,
 * as the setting is read once. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "stick_shape.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define FULL 32767
#define DIAG 23170              /* a full push on a round stick, an eighth of a turn round */
#define HALF_DIAG 11585         /* half of it */

static const double k_pi = 3.14159265358979323846;

/* The settings file: stick_shape= as the case sets it. */
static int    s_has;
static double s_value, s_kept = -1;
int nv_window_pref_num(const char *key, double *v)
{
    if (strcmp(key, "stick_shape") || !s_has) return 0;
    *v = s_value;
    return 1;
}
void nv_window_pref_set_num(const char *key, double v)
{
    CHECK(!strcmp(key, "stick_shape"));
    s_kept = v;
}

/* The rows it adds: Input's Behaviour submenu, then its row in it. */
static XBOX_MENU_ITEM s_group, s_row;
static int s_rows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    if (count > 0) s_group = items[0];
    if (count > 1) s_row = items[1];
    s_rows += count;
}

/* One stick, squared, against what it should give. */
static void square(int x, int y, int wx, int wy)
{
    short sx = (short)x, sy = (short)y;
    stick_shape_square(&sx, &sy);
    if (sx != wx || sy != wy) {
        fprintf(stderr, "FAIL: stick (%d, %d) squared is (%d, %d), should be (%d, %d)\n",
                x, y, sx, sy, wx, wy);
        exit(1);
    }
}

static void check_square(void)
{
    int a;
    /* On an axis, nothing changes, either way along it, at any push, the
     * axis's ends too. */
    square(FULL, 0, FULL, 0);
    square(0, FULL, 0, FULL);
    square(-FULL, 0, -FULL, 0);
    square(0, -FULL, 0, -FULL);
    square(-32768, 0, -32768, 0);
    square(0, -32768, 0, -32768);
    square(12000, 0, 12000, 0);
    square(0, -500, 0, -500);
    /* A full push on each diagonal reaches the square's corner. */
    square(DIAG, DIAG, FULL, FULL);
    square(-DIAG, DIAG, -FULL, FULL);
    square(-DIAG, -DIAG, -FULL, -FULL);
    square(DIAG, -DIAG, FULL, -FULL);
    /* Half a push on a diagonal reaches half way to the corner: 41% further
     * on each axis than it was. */
    square(HALF_DIAG, HALF_DIAG, 16384, 16384);
    square(-HALF_DIAG, HALF_DIAG, -16384, 16384);
    /* Past the circle, as spriteice's pad reaches on its diagonals (0.77,
     * 0.89 of the way: stick-reach/FINDINGS.md), each axis held to its end. */
    square(25231, 29163, FULL, FULL);
    square(-25231, -29163, -32768, -32768);
    square(-32768, -32768, -32768, -32768);
    square(FULL, FULL, FULL, FULL);
    /* -32768 on one axis and a little on the other: still the axis's end,
     * and the little kept. */
    square(-32768, 100, -32768, 100);
    /* The centre stays the centre. */
    square(0, 0, 0, 0);
    /* All the way round, at a full push and half of one: the direction is
     * kept, and the larger axis is the push's length. */
    for (a = 0; a < 360; a++) {
        double t = a * k_pi / 180, r;
        for (r = FULL; r > FULL / 4; r /= 2) {
            short x = (short)lround(r * cos(t)), y = (short)lround(r * sin(t));
            double len = sqrt((double)x * x + (double)y * y), big;
            stick_shape_square(&x, &y);
            big = fabs((double)x) > fabs((double)y) ? fabs((double)x) : fabs((double)y);
            CHECK(fabs(big - (len > FULL ? FULL : len)) <= 1.0);
            CHECK(fabs(atan2(y, x) - atan2(sin(t), cos(t))) < 1e-3
                  || fabs(fabs(atan2(y, x) - atan2(sin(t), cos(t))) - 2 * k_pi) < 1e-3);
        }
    }
}

/* The pad's sticks through the setting: Round leaves all four as they came,
 * Square takes each stick on its own. */
static void check_pad(int shape)
{
    short axes[4] = { DIAG, DIAG, -HALF_DIAG, HALF_DIAG };
    stick_shape_pad(axes);
    if (shape == STICK_SHAPE_ROUND) {
        CHECK(axes[0] == DIAG && axes[1] == DIAG && axes[2] == -HALF_DIAG && axes[3] == HALF_DIAG);
    } else {
        CHECK(axes[0] == FULL && axes[1] == FULL && axes[2] == -16384 && axes[3] == 16384);
    }
    /* A stick on an axis, and one at the centre, the same either way. */
    {
        short still[4] = { 0, FULL, 0, 0 };
        stick_shape_pad(still);
        CHECK(still[0] == 0 && still[1] == FULL && still[2] == 0 && still[3] == 0);
    }
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "square")) { check_square(); printf("stick_shape %s: pass\n", c); return 0; }
    if (!strcmp(c, "round"))   { s_has = 1; s_value = 1; }
    if (!strcmp(c, "squared")) { s_has = 1; s_value = 0; }
    if (!strcmp(c, "unknown")) { s_has = 1; s_value = 7; }
    stick_shape_start();
    /* Input's Behaviour submenu, with its hint, then the row in it: a
     * choice, Square then Round, with a hint the strip can hold, that File >
     * Presets set to Square in each preset. */
    CHECK(s_rows == 2);
    CHECK(s_group.menu == XBOX_MENU_INPUT && s_group.kind == XBOX_ITEM_GROUP);
    CHECK(!strcmp(s_group.label, XBOX_GROUP_BEHAVIOUR) && !strcmp(s_group.label, "Behaviour"));
    CHECK(s_group.hint && !strcmp(s_group.hint, "Sets how the camera, the keys and the sticks act."));
    CHECK(strlen(s_group.hint) <= MENU_HINT_LONGEST);
    CHECK(s_row.menu == XBOX_MENU_INPUT && s_row.kind == XBOX_ITEM_CHOICE && !s_row.confirm);
    CHECK(s_row.group && !strcmp(s_row.group, XBOX_GROUP_BEHAVIOUR) && !s_row.under && !s_row.last);
    CHECK(!strcmp(s_row.label, "Stick shape") && s_row.nchoices == 2);
    CHECK(!strcmp(s_row.choices[0], "Square") && !strcmp(s_row.choices[1], "Round"));
    CHECK(STICK_SHAPE_SQUARE == 0 && STICK_SHAPE_ROUND == 1);   /* stick_shape= as the row's order */
    CHECK(s_row.hint && !strcmp(s_row.hint, "Square stretches each stick to the Xbox pad's square reach."));
    CHECK(strlen(s_row.hint) <= MENU_HINT_LONGEST && s_row.hint[strlen(s_row.hint) - 1] == '.');
    CHECK(s_row.presets == XBOX_PRESETS_SET && !s_row.keep);
    /* File > Presets: Square in Xbox, and left by the others, as all of
     * Input is. */
    CHECK(s_row.preset[XBOX_PRESET_XBOX] == STICK_SHAPE_SQUARE && s_row.preset[XBOX_PRESET_MODERN] == STICK_SHAPE_SQUARE
          && s_row.leave == XBOX_PRESETS_LEVELS);
    CHECK(s_row.state() == stick_shape_get());
    /* Square, the default, with no stick_shape=, with 0, or with a value it
     * doesn't know, as a start with no settings gives. */
    if (!strcmp(c, "default") || !strcmp(c, "squared") || !strcmp(c, "unknown")) {
        CHECK(stick_shape_get() == STICK_SHAPE_SQUARE && s_row.state() == STICK_SHAPE_SQUARE);
        CHECK(STICK_SHAPE_DEFAULT == s_row.state());
        check_pad(STICK_SHAPE_SQUARE);
        CHECK(s_kept == -1);            /* reading it keeps nothing */
    }
    if (!strcmp(c, "round")) {
        CHECK(stick_shape_get() == STICK_SHAPE_ROUND && s_row.state() == STICK_SHAPE_ROUND);
        check_pad(STICK_SHAPE_ROUND);
    }
    if (!strcmp(c, "default")) {
        /* The row: Round at once, and kept; then Square again. A choice
         * out of range changes nothing. */
        s_row.pick(STICK_SHAPE_ROUND);
        CHECK(s_row.state() == STICK_SHAPE_ROUND && stick_shape_get() == STICK_SHAPE_ROUND);
        CHECK(s_kept == STICK_SHAPE_ROUND);
        check_pad(STICK_SHAPE_ROUND);
        s_row.pick(STICK_SHAPE_SQUARE);
        CHECK(s_row.state() == STICK_SHAPE_SQUARE && s_kept == STICK_SHAPE_SQUARE);
        check_pad(STICK_SHAPE_SQUARE);
        s_kept = -1;
        s_row.pick(STICK_SHAPE_CHOICES);
        s_row.pick(-1);
        CHECK(s_row.state() == STICK_SHAPE_SQUARE && s_kept == -1);
    }
    printf("stick_shape %s: pass\n", c);
    return 0;
}
