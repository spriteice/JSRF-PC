/* Run with: python tests/render_scale/run.py
 *
 * The internal resolution, src/nv2a_gl/nv2a_scale.c, as the engine builds it,
 * with the raster each choice makes (nv2a_wide_raster, src/nv2a_gl/nv2a_wide.c):
 *
 *   1. There are eight choices, in Display > Resolution's order: Default,
 *      720p, 900p, 1080p, 1440p, 1600p, 2160p, Match screen (spriteice,
 *      01:38 UTC on 2026-11-21, and the card of 01:42 UTC). Each has its
 *      name and its key in the settings file, resolution=, and gives its
 *      lines: Default the Xbox's own 480.
 *   2. Match screen gives the display's height, held between 480 and 2160
 *      lines, and 1080 while the height isn't known.
 *   3. resolution='s value is a choice by its key, its line end left out or
 *      not; anything else, a name or a number that isn't a key among them,
 *      is none. Each key reads back as its choice.
 *   4. scale= from before resolution= (1x to 6x) is the choice of those
 *      that gives at least as many lines, up to 2160p: 1 Default, 2 1080p,
 *      3 1440p, 4 to 6 2160p. Anything else is none.
 *   5. The default is Match screen (spriteice, 01:29 UTC on 2026-11-21).
 *   6. A surface's side at a choice's lines is lines / 480 times it, to the
 *      nearest pixel, and at least 1.
 *   7. Each choice's frame at 4:3, 16:10, 16:9 and 21:9 is the grid sent to
 *      spriteice (live-resolution/GRID.md, 01:42 UTC on 2026-11-21): the
 *      choice's height whatever the shape, and the width the shape's of it.
 *      A 512x512 and a 128x128 surface, as a run makes besides the frame,
 *      are lines / 480 times their size, and never widened.
 *   8. A frame is fitted into the window whole, its shape kept: a 4:3 frame
 *      in a wide window and in a tall one, the 16:9 and 21:9 shapes a
 *      widened scene is shown at, and a frame already the window's shape.
 *   9. A frame is halved along an axis while it is more than twice the size
 *      it is shown at there, and not at all at 2 to 1 or less: 2160p's 4:3
 *      frame on a 1080-line screen not at all, 2880 lines once.
 */
#include <stdio.h>
#include <string.h>
#include "nv2a_scale.h"
#include "nv2a_wide.h"

static int s_failures;

#define CHECK(c) do { if (!(c)) { printf("render_scale: FAIL %s:%d: %s\n", \
                                         __FILE__, __LINE__, #c); s_failures++; } } while (0)

#define N_CHOICES 8

static void choices(void)
{
    static const char *const names[N_CHOICES] = {
        "Default", "720p", "900p", "1080p", "1440p", "1600p", "2160p", "Match screen" };
    static const char *const keys[N_CHOICES] = {
        "default", "720", "900", "1080", "1440", "1600", "2160", "match" };
    static const uint32_t lines[N_CHOICES - 1] = { 480, 720, 900, 1080, 1440, 1600, 2160 };
    int i;
    CHECK(NV2A_SCALE_CHOICES == N_CHOICES);
    CHECK(sizeof nv2a_scale_names / sizeof nv2a_scale_names[0] == N_CHOICES);
    CHECK(NV2A_SCALE_XBOX == 0 && NV2A_SCALE_MATCH == N_CHOICES - 1);
    CHECK(NV2A_SCALE_FRAME_W == 640 && NV2A_SCALE_FRAME_H == 480);
    for (i = 0; i < N_CHOICES; i++) {
        CHECK(nv2a_scale_names[i] && !strcmp(nv2a_scale_names[i], names[i]));
        CHECK(nv2a_scale_key(i) && !strcmp(nv2a_scale_key(i), keys[i]));
        if (i < N_CHOICES - 1) {
            CHECK(nv2a_scale_lines(i, 0) == lines[i]);
            CHECK(nv2a_scale_lines(i, 1440) == lines[i]);   /* the display's is Match screen's */
        }
    }
    CHECK(nv2a_scale_lines(NV2A_SCALE_XBOX, 0) == NV2A_SCALE_FRAME_H);
    CHECK(nv2a_scale_lines(NV2A_SCALE_1600P, 0) == 1600);
    CHECK(nv2a_scale_lines(-1, 1080) == 0);
    CHECK(nv2a_scale_lines(N_CHOICES, 1080) == 0);
    CHECK(nv2a_scale_key(-1) == NULL);
    CHECK(nv2a_scale_key(N_CHOICES) == NULL);
}

static void match(void)
{
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, 1080) == 1080);    /* the Ally X */
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, 800) == 800);      /* a 1280x800 screen */
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, 1440) == 1440);
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, 1600) == 1600);
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, 2160) == 2160);
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, 2880) == 2160);    /* held at the most */
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, 4320) == 2160);
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, 400) == 480);      /* and at the fewest */
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, 1) == 480);
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, 0) == 1080);       /* not known */
    CHECK(nv2a_scale_lines(NV2A_SCALE_MATCH, -5) == 1080);
    CHECK(NV2A_SCALE_LINES_MIN == 480 && NV2A_SCALE_LINES_MAX == 2160);
    CHECK(NV2A_SCALE_LINES_UNKNOWN == 1080);
}

static void parse(void)
{
    int i;
    for (i = 0; i < N_CHOICES; i++) {
        char line[32];
        CHECK(nv2a_scale_parse(nv2a_scale_key(i)) == i);
        snprintf(line, sizeof line, "%s\n", nv2a_scale_key(i));
        CHECK(nv2a_scale_parse(line) == i);
        snprintf(line, sizeof line, "%s\r\n", nv2a_scale_key(i));
        CHECK(nv2a_scale_parse(line) == i);
    }
    CHECK(nv2a_scale_parse(NULL) == -1);
    CHECK(nv2a_scale_parse("") == -1);
    CHECK(nv2a_scale_parse("\n") == -1);
    CHECK(nv2a_scale_parse("1080p") == -1);         /* the name, not the key */
    CHECK(nv2a_scale_parse("Default") == -1);
    CHECK(nv2a_scale_parse("Match screen") == -1);
    CHECK(nv2a_scale_parse(" 1080") == -1);
    CHECK(nv2a_scale_parse("1080 ") == -1);
    CHECK(nv2a_scale_parse("108") == -1);
    CHECK(nv2a_scale_parse("10800") == -1);
    CHECK(nv2a_scale_parse("1200") == -1);          /* no such choice */
    CHECK(nv2a_scale_parse("3") == -1);             /* scale='s, not resolution='s */
    CHECK(nv2a_scale_parse("matchscreen") == -1);
}

static void old_scale(void)
{
    CHECK(nv2a_scale_of_old("1") == NV2A_SCALE_XBOX);
    CHECK(nv2a_scale_of_old("2") == NV2A_SCALE_1080P);   /* 960 lines */
    CHECK(nv2a_scale_of_old("3") == NV2A_SCALE_1440P);   /* 1440, today's default */
    CHECK(nv2a_scale_of_old("4") == NV2A_SCALE_2160P);   /* 1920 */
    CHECK(nv2a_scale_of_old("5") == NV2A_SCALE_2160P);
    CHECK(nv2a_scale_of_old("6") == NV2A_SCALE_2160P);
    CHECK(nv2a_scale_of_old("3\n") == NV2A_SCALE_1440P);
    CHECK(nv2a_scale_of_old("3\r\n") == NV2A_SCALE_1440P);
    CHECK(nv2a_scale_of_old(NULL) == -1);
    CHECK(nv2a_scale_of_old("") == -1);
    CHECK(nv2a_scale_of_old("0") == -1);
    CHECK(nv2a_scale_of_old("7") == -1);
    CHECK(nv2a_scale_of_old("-3") == -1);
    CHECK(nv2a_scale_of_old("3x") == -1);
    CHECK(nv2a_scale_of_old("sharp") == -1);
    CHECK(nv2a_scale_of_old("1080") == -1);
}

static void defaults(void)
{
    CHECK(NV2A_SCALE_DEFAULT == NV2A_SCALE_MATCH);
}

static void px(void)
{
    CHECK(nv2a_scale_px(640, 480) == 640);
    CHECK(nv2a_scale_px(640, 720) == 960);
    CHECK(nv2a_scale_px(640, 900) == 1200);
    CHECK(nv2a_scale_px(640, 1600) == 2133);        /* 2133.33 */
    CHECK(nv2a_scale_px(480, 1600) == 1600);
    CHECK(nv2a_scale_px(300, 900) == 563);          /* 562.5, up */
    CHECK(nv2a_scale_px(1, 480) == 1);
    CHECK(nv2a_scale_px(1, 720) == 2);              /* 1.5, up */
    CHECK(nv2a_scale_px(0, 1080) == 1);             /* at least 1 */
    CHECK(nv2a_scale_px(4096, 2160) == 18432);
}

/* Each choice's frame (640x480) at a shape, as width x height. */
static void frame_is(int choice, float a, uint32_t want_w, uint32_t want_h)
{
    uint32_t pw = 0, ph = 0;
    float shape;
    nv2a_wide_raster(NV2A_SCALE_FRAME_W, NV2A_SCALE_FRAME_H, nv2a_scale_lines(choice, 0), a,
                     &pw, &ph, &shape);
    if (pw != want_w || ph != want_h) {
        printf("render_scale: FAIL %s at %.4f: %ux%u, not %ux%u\n", nv2a_scale_names[choice],
               (double)a, pw, ph, want_w, want_h);
        s_failures++;
    }
}

static void surface_is(uint32_t w, uint32_t h, int choice, uint32_t want_w, uint32_t want_h)
{
    uint32_t pw = 0, ph = 0;
    float shape = -1.0f;
    nv2a_wide_raster(w, h, nv2a_scale_lines(choice, 0), NV2A_WIDE_16_9, &pw, &ph, &shape);
    if (pw != want_w || ph != want_h || shape != 0.0f) {
        printf("render_scale: FAIL %ux%u at %s: %ux%u at %.4f, not %ux%u\n", w, h,
               nv2a_scale_names[choice], pw, ph, (double)shape, want_w, want_h);
        s_failures++;
    }
}

static void grid(void)
{
    /* The shapes Display > Aspect ratio has: 4:3 (widescreen off), 16:10,
     * 16:9 and 21:9 (64:27). */
    static const float shapes[4] = { NV2A_WIDE_4_3, 16.0f / 10.0f, NV2A_WIDE_16_9, 64.0f / 27.0f };
    static const uint32_t widths[N_CHOICES - 1][4] = {
        {  640,  768,  853, 1138 },     /* Default */
        {  960, 1152, 1280, 1707 },     /* 720p */
        { 1200, 1440, 1600, 2133 },     /* 900p */
        { 1440, 1728, 1920, 2560 },     /* 1080p */
        { 1920, 2304, 2560, 3413 },     /* 1440p */
        { 2133, 2560, 2844, 3793 },     /* 1600p */
        { 2880, 3456, 3840, 5120 },     /* 2160p */
    };
    int c, s;
    for (c = 0; c < N_CHOICES - 1; c++)
        for (s = 0; s < 4; s++)
            frame_is(c, shapes[s], widths[c][s], nv2a_scale_lines(c, 0));
    /* The boost's copy and the small one beside it. */
    surface_is(512, 512, NV2A_SCALE_XBOX, 512, 512);
    surface_is(512, 512, NV2A_SCALE_720P, 768, 768);
    surface_is(512, 512, NV2A_SCALE_900P, 960, 960);
    surface_is(512, 512, NV2A_SCALE_1080P, 1152, 1152);
    surface_is(512, 512, NV2A_SCALE_1440P, 1536, 1536);
    surface_is(512, 512, NV2A_SCALE_1600P, 1707, 1707);     /* 1706.67 */
    surface_is(512, 512, NV2A_SCALE_2160P, 2304, 2304);
    surface_is(128, 128, NV2A_SCALE_XBOX, 128, 128);
    surface_is(128, 128, NV2A_SCALE_900P, 240, 240);
    surface_is(128, 128, NV2A_SCALE_1600P, 427, 427);       /* 426.67 */
    surface_is(128, 128, NV2A_SCALE_2160P, 576, 576);
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
    fit_is(1920, 1440, 2560, 1440, 1920, 1440);     /* 1440p on a 1440-line screen */
    fit_is(2880, 2160, 1920, 1080, 1440, 1080);     /* 2160p on a 1080-line one */
    fit_is(640, 480, 800, 1200, 800, 600);          /* a tall window */
    fit_is(1280, 960, 1280, 960, 1280, 960);        /* the window the game opens */
    fit_is(1600, 900, 1920, 1080, 1920, 1080);      /* a widened scene at 16:9 */
    fit_is(1600, 900, 1280, 960, 1280, 720);
    fit_is(2100, 900, 1920, 1080, 1920, 822);       /* at 21:9 */
    fit_is(3793, 1600, 3840, 1600, 3793, 1600);     /* 21:9 on a 3840x1600 screen */
}

static void halvings(void)
{
    CHECK(nv2a_scale_halvings(1440, 1080) == 0);    /* 1080p's 4:3 on 1080 lines */
    CHECK(nv2a_scale_halvings(2160, 1080) == 0);    /* 2160p: 2.00 */
    CHECK(nv2a_scale_halvings(2400, 1080) == 1);    /* 2.22 */
    CHECK(nv2a_scale_halvings(2880, 1080) == 1);    /* 2.67 */
    CHECK(nv2a_scale_halvings(2880, 1440) == 0);    /* 2.00 */
    CHECK(nv2a_scale_halvings(2880, 2160) == 0);    /* on 4K */
    CHECK(nv2a_scale_halvings(3840, 1440) == 1);
    CHECK(nv2a_scale_halvings(1920, 720) == 1);     /* in a small window */
    CHECK(nv2a_scale_halvings(2880, 360) == 2);     /* in a smaller one */
    CHECK(nv2a_scale_halvings(2880, 1) == 10);
    CHECK(nv2a_scale_halvings(480, 1080) == 0);     /* drawn smaller than shown */
    CHECK(nv2a_scale_halvings(2880, 0) == 0);
}

int main(void)
{
    choices();
    match();
    parse();
    old_scale();
    defaults();
    px();
    grid();
    fit();
    halvings();
    if (s_failures) {
        printf("render_scale: %d failed\n", s_failures);
        return 1;
    }
    printf("render_scale: all cases pass (%d choices)\n", NV2A_SCALE_CHOICES);
    return 0;
}
