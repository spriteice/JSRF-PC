/* Run with: python tests/render_scale/run.py
 *
 * The internal resolution, src/nv2a_gl/nv2a_scale.c, as the engine builds it:
 *
 *   1. There are six scales, and each one's name is the scale and the raster
 *      it makes of the Xbox's 640x480, 1x first.
 *   2. RECOMP_GL_SCALE's value is 1 when unset or not a number, a number as
 *      strtol reads it otherwise, and clamped to 1..NV2A_SCALE_MAX: 1x to 6x.
 *   3. A frame is fitted into the window whole, its shape kept: a 4:3 frame
 *      in a wide window and in a tall one, the 16:9 and 21:9 shapes a
 *      widened scene is shown at, and a frame already the window's shape.
 *   4. A frame is halved along an axis while it is more than twice the size
 *      it is shown at there, and not at all at 2 to 1 or less: 5x and 6x on
 *      a 1080-line screen once, 6x on a 1440-line one not at all.
 */
#include <stdio.h>
#include <string.h>
#include "nv2a_scale.h"

static int s_failures;

#define CHECK(c) do { if (!(c)) { printf("render_scale: FAIL %s:%d: %s\n", \
                                         __FILE__, __LINE__, #c); s_failures++; } } while (0)

static void names(void)
{
    int i;
    CHECK(NV2A_SCALE_MAX == 6);
    CHECK(sizeof nv2a_scale_names / sizeof nv2a_scale_names[0] == NV2A_SCALE_MAX);
    CHECK(NV2A_SCALE_FRAME_W == 640 && NV2A_SCALE_FRAME_H == 480);
    for (i = 0; i < NV2A_SCALE_MAX; i++) {
        char want[32];
        snprintf(want, sizeof want, "%dx  %dx%d", i + 1, 640 * (i + 1), 480 * (i + 1));
        CHECK(nv2a_scale_names[i] && !strcmp(nv2a_scale_names[i], want));
    }
}

static void parse(void)
{
    CHECK(nv2a_scale_parse(NULL) == 1);
    CHECK(nv2a_scale_parse("") == 1);
    CHECK(nv2a_scale_parse("sharp") == 1);
    CHECK(nv2a_scale_parse("0") == 1);
    CHECK(nv2a_scale_parse("-3") == 1);
    CHECK(nv2a_scale_parse("1") == 1);
    CHECK(nv2a_scale_parse("3") == 3);
    CHECK(nv2a_scale_parse(" 2") == 2);
    CHECK(nv2a_scale_parse("0x4") == 4);
    CHECK(nv2a_scale_parse("4x") == 4);
    CHECK(nv2a_scale_parse("5") == 5);
    CHECK(nv2a_scale_parse("6") == 6);
    CHECK(nv2a_scale_parse("7") == 6);
    CHECK(nv2a_scale_parse("8") == 6);
    CHECK(nv2a_scale_parse("99999999999999999999") == 6);
}

static void fit_is(int fw, int fh, int ww, int wh, int want_w, int want_h)
{
    int dw = -1, dh = -1;
    nv2a_scale_fit(fw, fh, ww, wh, &dw, &dh);
    if (dw != want_w || dh != want_h) {
        printf("render_scale: FAIL %dx%d into %dx%d: %dx%d, not %dx%d\n",
               fw, fh, ww, wh, dw, dh, want_w, want_h);
        s_failures++;
    }
}

static void fit(void)
{
    fit_is(640, 480, 1920, 1080, 1440, 1080);       /* the Ally X's screen */
    fit_is(2560, 1920, 2560, 1440, 1920, 1440);     /* 4x on a 1440-line screen */
    fit_is(3840, 2880, 1920, 1080, 1440, 1080);     /* 6x on a 1080-line one */
    fit_is(640, 480, 800, 1200, 800, 600);          /* a tall window */
    fit_is(1280, 960, 1280, 960, 1280, 960);        /* the window the game opens */
    fit_is(1600, 900, 1920, 1080, 1920, 1080);      /* a widened scene at 16:9 */
    fit_is(1600, 900, 1280, 960, 1280, 720);
    fit_is(2100, 900, 1920, 1080, 1920, 822);       /* at 21:9 */
}

static void halvings(void)
{
    CHECK(nv2a_scale_halvings(1920, 1080) == 0);    /* 4x on 1080 lines: 1.78 */
    CHECK(nv2a_scale_halvings(2400, 1080) == 1);    /* 5x: 2.22 */
    CHECK(nv2a_scale_halvings(2880, 1080) == 1);    /* 6x: 2.67 */
    CHECK(nv2a_scale_halvings(2880, 1440) == 0);    /* 6x on 1440 lines: 2.00 */
    CHECK(nv2a_scale_halvings(2880, 2160) == 0);    /* 6x on 4K */
    CHECK(nv2a_scale_halvings(3840, 1440) == 1);    /* 6x's width on 1080 lines */
    CHECK(nv2a_scale_halvings(1920, 720) == 1);     /* 4x in a small window */
    CHECK(nv2a_scale_halvings(2880, 360) == 2);     /* 6x in a smaller one */
    CHECK(nv2a_scale_halvings(2880, 1) == 10);
    CHECK(nv2a_scale_halvings(480, 1080) == 0);     /* drawn smaller than shown */
    CHECK(nv2a_scale_halvings(2880, 0) == 0);
}

int main(void)
{
    names();
    parse();
    fit();
    halvings();
    if (s_failures) {
        printf("render_scale: %d failed\n", s_failures);
        return 1;
    }
    printf("render_scale: all cases pass (1x to %dx)\n", NV2A_SCALE_MAX);
    return 0;
}
