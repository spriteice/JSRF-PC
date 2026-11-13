/* Run with: python tests/hud_scale/run.py
 *
 * HUD scale's maths, src/nv2a_gl/nv2a_hud_scale.c, as the engine builds it:
 *
 *   1. Display > HUD > HUD scale's steps: 0.5x to 1.5x, a tenth a
 *      step, kept as tenths, 5 to 15, with 10 (1x) as a start with no
 *      setting takes it; the row's numbers say the same.
 *   2. hudscale='s number, or the menu row's, read as tenths: each step as
 *      the menu reaches it, up from 0.5 and down from 1.5 by its - and +
 *      (0.5 + 3 x 0.1 is 0.8000000000000002), and as the file keeps it
 *      ("%g": "0.8", "1", "1.5") reads back as itself; a number between two
 *      steps to the nearer; anything outside 0.5 to 1.5 by more than a
 *      float's error doesn't read (an earlier build's 0 and 0.25 among
 *      them), nor NaN or an infinity.
 *   3. Each step's size: 0.5 to 1.5; 1 for anything outside 5 to 15.
 *   4. A piece is sized about the inner corner of the HUD's margin on its
 *      side, at the top or the bottom: 51 title pixels in from the left or
 *      right edge, 40 down from the top or up from the bottom. Moved to a
 *      wide screen's edge, the margin across is squeezed with it (0.75 at
 *      16:9), so it is as far from the screen's edge on the screen as at
 *      4:3. A piece in the middle, or with any other side, is sized about
 *      the screen's centre; an s that isn't between 0 and 1 is taken as 1.
 *   5. Sized as the shader sizes it (nv2a_vsh.c's epilogue, which run.py
 *      checks is there on both paths): the anchor stays where it is, and a
 *      point k times as far from it, 2D and 3D (any w) alike; 0, the
 *      uniform's default, changes nothing.
 *   6. The window clip is sized about the same point: the rolling number's
 *      strip, 51 to 210 by 40 to 61, is 51 to 131 by 40 to 51 at 0.5x, near
 *      edges down to a whole pixel and far ones up, at every render scale,
 *      and an edge that lands on one, give or take a float's error, on it;
 *      a clip sized past the surface stops at its edges; a size of 1, 0 or
 *      NaN leaves it.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nv2a_hud_scale.h"
#include "nv2a_wide.h"

static int s_failures;

#define CHECK(c) do { if (!(c)) { printf("hud_scale: FAIL %s:%d: %s\n", \
                                         __FILE__, __LINE__, #c); s_failures++; } } while (0)

/* Two places in the title's pixels, within a float's error at 640. */
static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }

static void steps(void)
{
    CHECK(NV2A_HUD_TENTHS_MIN == 5 && NV2A_HUD_TENTHS_MAX == 15 && NV2A_HUD_TENTHS_DEFAULT == 10);
    CHECK(NV2A_HUD_SCALE_MIN == 0.5 && NV2A_HUD_SCALE_MAX == 1.5 && NV2A_HUD_SCALE_DEFAULT == 1.0);
    CHECK(NV2A_HUD_SCALE_STEP == 0.1);
    CHECK(nv2a_hud_parse(NV2A_HUD_SCALE_MIN) == NV2A_HUD_TENTHS_MIN);
    CHECK(nv2a_hud_parse(NV2A_HUD_SCALE_MAX) == NV2A_HUD_TENTHS_MAX);
    CHECK(nv2a_hud_parse(NV2A_HUD_SCALE_DEFAULT) == NV2A_HUD_TENTHS_DEFAULT);
}

/* One press of the row's - (dir -1) or + (1), as src/menu/menu.cpp's
 * step_from takes it, slack and all. */
static double menu_step(double v, int dir)
{
    const double min = NV2A_HUD_SCALE_MIN, max = NV2A_HUD_SCALE_MAX, step = NV2A_HUD_SCALE_STEP;
    double n = (v - min) / step;
    n = dir > 0 ? floor(n + 1e-6) + 1 : ceil(n - 1e-6) - 1;
    v = min + n * step;
    return v < min ? min : v > max ? max : v;
}

static void keys(void)
{
    static const double others[] = { 0.0, -0.0, 0.25, 0.4, 0.44, 1.56, 1.6, 2.0, -1.0, 100.0 };
    char text[32];
    double v;
    int t, i;
    /* Up from 0.5, and down from 1.5, as the row's + and - go. */
    for (v = NV2A_HUD_SCALE_MIN, t = NV2A_HUD_TENTHS_MIN; t <= NV2A_HUD_TENTHS_MAX;
         v = menu_step(v, 1), t++) {
        CHECK(nv2a_hud_parse(v) == t);
        snprintf(text, sizeof text, "%g", t / 10.0);  /* as hudscale= keeps it */
        CHECK(nv2a_hud_parse(strtod(text, NULL)) == t);
    }
    CHECK(menu_step(NV2A_HUD_SCALE_MAX, 1) == NV2A_HUD_SCALE_MAX);
    for (v = NV2A_HUD_SCALE_MAX, t = NV2A_HUD_TENTHS_MAX; t >= NV2A_HUD_TENTHS_MIN;
         v = menu_step(v, -1), t--)
        CHECK(nv2a_hud_parse(v) == t);
    CHECK(menu_step(NV2A_HUD_SCALE_MIN, -1) == NV2A_HUD_SCALE_MIN);
    snprintf(text, sizeof text, "%g", 10 / 10.0);
    CHECK(!strcmp(text, "1"));                      /* "default value of 1" */
    /* Between two steps, the nearer; just outside the ends, the end. */
    CHECK(nv2a_hud_parse(0.74) == 7 && nv2a_hud_parse(0.76) == 8 && nv2a_hud_parse(1.25) == 13);
    CHECK(nv2a_hud_parse((double)0.8f) == 8);                    /* a float's 0.8 */
    CHECK(nv2a_hud_parse(0.4999999) == 5 && nv2a_hud_parse(1.5000001) == 15);
    for (i = 0; i < (int)(sizeof others / sizeof others[0]); i++) CHECK(nv2a_hud_parse(others[i]) == -1);
    CHECK(nv2a_hud_parse(NAN) == -1 && nv2a_hud_parse(INFINITY) == -1 && nv2a_hud_parse(-INFINITY) == -1);
}

static void sizes(void)
{
    int t;
    for (t = NV2A_HUD_TENTHS_MIN; t <= NV2A_HUD_TENTHS_MAX; t++)
        CHECK(nv2a_hud_size(t) == (float)t / 10.0f);
    CHECK(nv2a_hud_size(5) == 0.5f && nv2a_hud_size(10) == 1.0f && nv2a_hud_size(15) == 1.5f);
    CHECK(nv2a_hud_size(4) == 1.0f && nv2a_hud_size(16) == 1.0f && nv2a_hud_size(0) == 1.0f);
    CHECK(nv2a_hud_size(-1) == 1.0f);
}

/* An NDC x or y as the title's pixels across or down, on the 640x480 screen. */
static float px_x(float ax) { return (ax + 1.0f) * 0.5f * 640.0f; }
static float px_y(float ay) { return (1.0f - ay) * 0.5f * 480.0f; }

static void anchors(void)
{
    static const float bad[] = { 0.0f, -0.5f, 1.5f, NAN };
    float ax = 9.0f, ay = 9.0f, bx, by;
    unsigned i;
    /* 4:3: 51 in from either side, 40 from the top or the bottom */
    nv2a_hud_anchor(NV2A_WIDE_HUD_LEFT, 0, 1.0f, &ax, &ay);
    CHECK(near(px_x(ax), 51.0f) && near(px_y(ay), 40.0f));
    nv2a_hud_anchor(NV2A_WIDE_HUD_RIGHT, 0, 1.0f, &ax, &ay);
    CHECK(near(px_x(ax), 589.0f) && near(px_y(ay), 40.0f));
    nv2a_hud_anchor(NV2A_WIDE_HUD_RIGHT, 1, 1.0f, &ax, &ay);
    CHECK(near(px_x(ax), 589.0f) && near(px_y(ay), 440.0f));
    nv2a_hud_anchor(NV2A_WIDE_HUD_LEFT, 1, 1.0f, &ax, &ay);
    CHECK(near(px_x(ax), 51.0f) && near(px_y(ay), 440.0f));
    /* 16:9, at the wide screen's edge: the margin squeezed with the piece,
     * 38.25 of the frame's 640, which is 51 of the screen's 853.3 */
    nv2a_hud_anchor(NV2A_WIDE_HUD_LEFT, 0, 0.75f, &ax, &ay);
    CHECK(near(px_x(ax), 38.25f) && near(px_y(ay), 40.0f));
    CHECK(near(px_x(ax) / 0.75f, 51.0f));
    nv2a_hud_anchor(NV2A_WIDE_HUD_RIGHT, 1, 0.75f, &ax, &ay);
    CHECK(near(px_x(ax), 640.0f - 38.25f) && near(px_y(ay), 440.0f));
    /* the middle, and any other side: the centre, low or not */
    nv2a_hud_anchor(NV2A_WIDE_HUD_MIDDLE, 0, 0.75f, &ax, &ay);
    CHECK(ax == 0.0f && ay == 0.0f);
    ax = ay = 9.0f;
    nv2a_hud_anchor(NV2A_WIDE_HUD_MIDDLE, 1, 1.0f, &ax, &ay);
    CHECK(ax == 0.0f && ay == 0.0f);
    ax = ay = 9.0f;
    nv2a_hud_anchor(NV2A_WIDE_HUD_NONE, 0, 1.0f, &ax, &ay);
    CHECK(ax == 0.0f && ay == 0.0f);
    ax = ay = 9.0f;
    nv2a_hud_anchor(NV2A_WIDE_HUD_ACROSS, 1, 1.0f, &ax, &ay);
    CHECK(ax == 0.0f && ay == 0.0f);
    /* an s that isn't between 0 and 1: 1 */
    nv2a_hud_anchor(NV2A_WIDE_HUD_RIGHT, 0, 1.0f, &bx, &by);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        nv2a_hud_anchor(NV2A_WIDE_HUD_RIGHT, 0, bad[i], &ax, &ay);
        CHECK(ax == bx && ay == by);
    }
}

/* The shader's step (nv2a_vsh.c): gl_Position.xy * (1 + h.x) - h.yz * (h.x * w),
 * here for x, and the point it lands on, divided by w. */
static float shade(float clip, float w, float k_less_1, float anchor)
{
    return (clip * (1.0f + k_less_1) - anchor * (k_less_1 * w)) / w;
}

static void shader(void)
{
    static const float ks[] = { 1.5f, 1.25f, 0.75f, 0.5f, 0.25f };
    static const float ws[] = { 1.0f, 0.37f, 12.5f, 300.0f };
    static const float xs[] = { -1.0f, -0.840625f, -0.2f, 0.0f, 0.6f, 1.0f };
    float ax, ay;
    unsigned i, j, n;
    nv2a_hud_anchor(NV2A_WIDE_HUD_LEFT, 0, 1.0f, &ax, &ay);
    for (i = 0; i < sizeof ks / sizeof ks[0]; i++)
        for (j = 0; j < sizeof ws / sizeof ws[0]; j++) {
            float w = ws[j], h = ks[i] - 1.0f;
            CHECK(fabsf(shade(ax * w, w, h, ax) - ax) < 1e-4f);         /* the anchor stays */
            CHECK(fabsf(shade(ay * w, w, h, ay) - ay) < 1e-4f);
            for (n = 0; n < sizeof xs / sizeof xs[0]; n++)
                CHECK(fabsf(shade(xs[n] * w, w, h, ax) - (ax + (xs[n] - ax) * ks[i])) < 1e-4f);
            /* 0, as every other draw has it: nothing moves */
            for (n = 0; n < sizeof xs / sizeof xs[0]; n++)
                CHECK(shade(xs[n] * w, w, 0.0f, ax) == xs[n] * w / w);
        }
}

static void clips(void)
{
    static const float bad[] = { 1.0f, 0.0f, -1.0f, NAN };
    uint32_t x0, y0, x1, y1, sc;
    float ax, ay;
    unsigned i;
    nv2a_hud_anchor(NV2A_WIDE_HUD_LEFT, 0, 1.0f, &ax, &ay);
    /* the rolling number's strip, at every render scale */
    for (sc = 1; sc <= 6; sc++) {
        x0 = 51 * sc; y0 = 40 * sc; x1 = 210 * sc; y1 = 61 * sc;
        nv2a_hud_clip(640 * sc, 480 * sc, 0.5f, ax, ay, &x0, &y0, &x1, &y1);
        CHECK(x0 == 51 * sc && y0 == 40 * sc);
        CHECK(x1 == (uint32_t)ceilf(51.0f * (float)sc + 159.0f * (float)sc * 0.5f));
        CHECK(y1 == (uint32_t)ceilf(40.0f * (float)sc + 21.0f * (float)sc * 0.5f));
    }
    x0 = 51; y0 = 40; x1 = 210; y1 = 61;
    nv2a_hud_clip(640, 480, 0.5f, ax, ay, &x0, &y0, &x1, &y1);
    CHECK(x0 == 51 && y0 == 40 && x1 == 131 && y1 == 51);
    x0 = 60; y0 = 45; x1 = 210; y1 = 61;                  /* inside the margin's corner */
    nv2a_hud_clip(640, 480, 1.5f, ax, ay, &x0, &y0, &x1, &y1);
    CHECK(x0 == 64 && y0 == 47 && x1 == 290 && y1 == 72);  /* 64.5 down, 47.5 down, 289.5 up, 71.5 up */
    /* the whole surface, 1.5x about the right's bottom corner: its edges */
    nv2a_hud_anchor(NV2A_WIDE_HUD_RIGHT, 1, 1.0f, &ax, &ay);
    x0 = 0; y0 = 0; x1 = 640; y1 = 480;
    nv2a_hud_clip(640, 480, 1.5f, ax, ay, &x0, &y0, &x1, &y1);
    CHECK(x0 == 0 && y0 == 0 && x1 == 640 && y1 == 480);
    /* and 0.25x about it: toward the corner */
    x0 = 0; y0 = 0; x1 = 640; y1 = 480;
    nv2a_hud_clip(640, 480, 0.25f, ax, ay, &x0, &y0, &x1, &y1);
    CHECK(x0 == 441 && y0 == 330 && x1 == 602 && y1 == 450);  /* 441.75, 330, 601.75, 450 */
    /* left as it is */
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        x0 = 51; y0 = 40; x1 = 210; y1 = 61;
        nv2a_hud_clip(640, 480, bad[i], ax, ay, &x0, &y0, &x1, &y1);
        CHECK(x0 == 51 && y0 == 40 && x1 == 210 && y1 == 61);
    }
}

int main(void)
{
    steps();
    keys();
    sizes();
    anchors();
    shader();
    clips();
    if (s_failures) {
        printf("hud_scale: %d failed\n", s_failures);
        return 1;
    }
    printf("hud_scale: all cases pass\n");
    return 0;
}
