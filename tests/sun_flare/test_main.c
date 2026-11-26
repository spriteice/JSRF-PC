/* Run with: python tests/sun_flare/run.py
 *
 * port/sun_flare.c without the game. run.py runs it once for each case, the
 * case's name the argument:
 *
 * ends: for a piece in each of the eight ways the sun can be, the ends of
 *   the line the fix tests are the piece's own, and the game's line, from
 *   its box's smallest corner to its largest, is the piece only when the sun
 *   is ahead on all three axes. Then a wall at a slant to the axes, which
 *   the piece crosses and the game's mirrored line runs alongside.
 * steady: the fix's result steadied: an edge, blocked one test and clear
 *   the next, and a wall with gaps, blocked four tests in five, are never
 *   given to the game; a solid wall, blocked nineteen tests in twenty, is,
 *   and kept; a clean change to blocked is given on its SUN_FLARE_HIDEth
 *   test and not before, and the way back on its SUN_FLARE_SHOWth; and a
 *   new flare starts from its own first result.
 * note: the log's lines for a run of made-up tests, as the hook makes them.
 * switches: the two switches, as run.py sets them in the environment.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun_flare.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

static int same(const float a[3], const float b[3])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

/* A piece from p to q as the game keeps it: its box, the smallest and the
 * largest corner (sub_00044C90). */
static void box(const float p[3], const float q[3], float lo[3], float hi[3])
{
    int k;
    for (k = 0; k < 3; k++) {
        lo[k] = p[k] < q[k] ? p[k] : q[k];
        hi[k] = p[k] < q[k] ? q[k] : p[k];
    }
}

/* Which side of the plane n.x = c the point is on. */
static double side(const float n[3], float c, const float x[3])
{
    return n[0] * x[0] + n[1] * x[1] + n[2] * x[2] - c;
}

static void ends(void)
{
    int way, k, at_game = 0;
    for (way = 0; way < 8; way++) {
        /* The sun's way: up, along x and z, each one way or the other. */
        const float d[3] = { (way & 1) ? -70.f : 70.f, (way & 2) ? -37.f : 37.f, (way & 4) ? -70.f : 70.f };
        const float p[3] = { 1200.f, 85.5f, -340.25f };
        float q[3], lo[3], hi[3], a[3], b[3];
        for (k = 0; k < 3; k++) q[k] = p[k] + d[k];
        box(p, q, lo, hi);
        sun_flare_ends(lo, hi, d, a, b);
        CHECK(same(a, p) && same(b, q), "way %d: the fix's line is (%g %g %g)-(%g %g %g), not the piece",
              way, a[0], a[1], a[2], b[0], b[1], b[2]);
        if (same(lo, p) && same(hi, q)) at_game |= 1 << way;
    }
    CHECK(at_game == 1, "the game's line is the piece for the ways %02X, not the sun ahead on all three alone",
          at_game);
    {
        /* The sun ahead in x, behind in z: the piece runs from (0, 0, 0) to
         * (70, 37, -70), and the game's line from (0, 0, -70) to (70, 37,
         * 0). A wall at 45 degrees to x and z, its face toward the eye, which
         * the piece crosses at its middle's far side. */
        const float p[3] = { 0, 0, 0 }, q[3] = { 70.f, 37.f, -70.f }, n[3] = { 1, 0, -1 };
        const float wall = 90.f;
        float lo[3], hi[3], a[3], b[3];
        box(p, q, lo, hi);
        sun_flare_ends(lo, hi, q, a, b);
        CHECK(side(n, wall, a) < 0 && side(n, wall, b) > 0, "the fix's line doesn't cross the wall");
        CHECK(side(n, wall, lo) < 0 && side(n, wall, hi) < 0, "the game's line crosses the wall");
    }
    printf("ends: the fix's line is the piece in all 8 ways; the game's in 1\n");
}

static void note(void)
{
    SunFlareTest t;
    int lines = 0;
    memset(&t, 0, sizeof t);
    t.flare = 0x00ABC000u;
    t.state = 1;
    t.eye[0] = 100; t.eye[1] = 20; t.eye[2] = -300;
    t.dir[0] = 600; t.dir[1] = 530; t.dir[2] = -800;
    t.pieces = 12;
    t.tested = 3;
    t.game = -1;
    t.real = 4;
    t.fix = 0;
    t.t = 10.0;
    lines += sun_flare_note(&t);        /* the first: a line */
    t.t = 10.1;
    lines += sun_flare_note(&t);        /* the same: none until 2 s on */
    t.t = 11.9;
    lines += sun_flare_note(&t);
    t.t = 12.0;
    lines += sun_flare_note(&t);        /* 2 s on: a line */
    CHECK(lines == 2, "%d lines for the same test over 2 s, not 2", lines);
    t.real = -1;
    t.t = 12.1;
    CHECK(!sun_flare_note(&t), "a change 0.1 s after the last line was written at once");
    t.t = 12.3;
    CHECK(sun_flare_note(&t), "a change held back wasn't written 0.3 s after the last line");
    t.game = 0;
    t.real = 0;
    t.fix = 1;
    t.shown = 1;
    t.state = 2;
    t.t = 13.0;
    CHECK(sun_flare_note(&t), "a change 0.7 s after the last line wasn't written");
    t.flare = 0x00ABD000u;
    t.t = 13.3;
    CHECK(sun_flare_note(&t), "another flare's test wasn't written");
}

static void steady(void)
{
    const uint32_t f = 0x00ABC000u;
    SunFlareHold hold = { 0, 0, 0 };
    int i, given = 0, at = -1;
    CHECK(!sun_flare_steady(&hold, f, 0), "a new flare's first result, clear, isn't given");
    /* An edge: blocked one test, clear the next, for 10 seconds: never
     * given. */
    for (i = 0; i < 600; i++) given += sun_flare_steady(&hold, f, i & 1);
    CHECK(!given, "an edge, blocked every other test, was given %d times", given);
    /* A wall with gaps: blocked four tests in five, for 10 seconds: never
     * given either, as it isn't solid. */
    for (i = 0; i < 600; i++) given += sun_flare_steady(&hold, f, i % 5 != 4);
    CHECK(!given, "a wall blocked four tests in five was given %d times", given);
    /* A solid wall: blocked nineteen tests in twenty. Given within the
     * window, and kept. */
    for (i = 0; i < 300; i++)
        if (sun_flare_steady(&hold, f, i % 20 != 19) && at < 0) at = i;
    CHECK(at >= 0 && at < SUN_FLARE_WINDOW, "a wall blocked nineteen tests in twenty given at test %d", at);
    CHECK(hold.shown == 1, "a wall blocked nineteen tests in twenty not kept");
    /* The way back, from a window all blocked: clear given on its
     * SUN_FLARE_SHOWth test, not before. */
    for (i = 0; i < SUN_FLARE_WINDOW; i++) sun_flare_steady(&hold, f, 1);
    for (i = 1; i < SUN_FLARE_SHOW; i++)
        CHECK(sun_flare_steady(&hold, f, 0) == 1, "clear given after %d tests", i);
    CHECK(sun_flare_steady(&hold, f, 0) == 0, "clear for %d tests wasn't given", SUN_FLARE_SHOW);
    /* A clean change, from a window all clear: blocked given on its
     * SUN_FLARE_HIDEth test, not before. */
    for (i = SUN_FLARE_SHOW; i < SUN_FLARE_WINDOW; i++) sun_flare_steady(&hold, f, 0);
    CHECK(hold.shown == 0, "a window all clear isn't given as clear");
    for (i = 1; i < SUN_FLARE_HIDE; i++)
        CHECK(!sun_flare_steady(&hold, f, 1), "blocked given after %d tests", i);
    CHECK(sun_flare_steady(&hold, f, 1) == 1, "blocked for %d tests wasn't given", SUN_FLARE_HIDE);
    /* Another flare (a new stage's) starts from its own first result. */
    CHECK(sun_flare_steady(&hold, 0x00ABD000u, 1) == 1, "a new flare's first result, blocked, isn't given");
    CHECK(sun_flare_steady(&hold, 0x00ABD000u, 0) == 1, "a new flare's one clear test given");
    printf("steady: an edge and a wall with gaps aren't given, a solid wall is; hidden on the %dth blocked "
           "test of %d, shown again on the %dth clear\n", SUN_FLARE_HIDE, SUN_FLARE_WINDOW, SUN_FLARE_SHOW);
}

static void switches(void)
{
    printf("fix %d, log %d\n", sun_flare_fix_on(), sun_flare_log_on());
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "";
    if (!strcmp(which, "ends")) ends();
    else if (!strcmp(which, "steady")) steady();
    else if (!strcmp(which, "note")) note();
    else if (!strcmp(which, "switches")) switches();
    else { fprintf(stderr, "no case %s\n", which); return 2; }
    return fails ? 1 : 0;
}
