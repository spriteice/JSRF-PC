/* Run with: python tests/widescreen/run.py
 *
 * Widescreen's maths, src/nv2a_gl/nv2a_wide.c, as the engine builds it:
 *
 *   1. A shape is taken as 4:3 at the least, NaN as 4:3, and as wide as it
 *      is otherwise.
 *   2. The 4:3 frame squeezes a view of shape a by (4/3) / a, by exactly
 *      0.75 at 16:9, and not at all at 4:3.
 *   2a. Display > Aspect ratio's shapes: their names in the menu's order,
 *      their aspect= keys, which read back (line end and all) and nothing
 *      else does, and their width over height, Match screen's the display's.
 *   3. Only the Xbox's 640x480 frame is widened.
 *   4. A screen-space draw's extent is put in NDC through its viewport,
 *      whether that is in the frame's pixels or another surface's, and with
 *      no viewport scale as half the frame.
 *   5. A draw narrower than 1.8 of NDC both ways is an overlay, and one as
 *      wide or as tall is not.
 *   6. A window clip is squeezed about the surface's centre, its near edge
 *      down to a whole pixel and its far one up.
 *   7. A 3D view in a frame narrower than 600 pixels moves toward the centre
 *      with its frame, and an empty clip or one as wide as the scene doesn't.
 *   8. The frame of a widened scene is drawn as wide as its shape at the
 *      scale's height, to the nearest pixel, at every scale and shape, and
 *      shown at that shape; at 4:3 and on any other surface, the scale's
 *      multiple of it and its own shape.
 *   9. A frame of the window's own shape fills it, whatever its raster was
 *      rounded to; others sit between bars, never past the window.
 *  10. Render scale's names follow the shape, and are nv2a_scale_names' at 4:3.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nv2a_wide.h"
#include "nv2a_scale.h"

static int s_failures;

#define CHECK(c) do { if (!(c)) { printf("widescreen: FAIL %s:%d: %s\n", \
                                         __FILE__, __LINE__, #c); s_failures++; } } while (0)

static int near(float a, float b) { return fabsf(a - b) < 1e-5f; }

static void shapes(void)
{
    CHECK(nv2a_wide_clamp(NV2A_WIDE_16_9) == NV2A_WIDE_16_9);
    CHECK(nv2a_wide_clamp(2.0f) == 2.0f);
    CHECK(nv2a_wide_clamp(1.6f) == 1.6f);                    /* 16:10 */
    CHECK(nv2a_wide_clamp(NV2A_WIDE_4_3) == NV2A_WIDE_4_3);
    CHECK(nv2a_wide_clamp(1.25f) == NV2A_WIDE_4_3);          /* 5:4, below 4:3 */
    CHECK(nv2a_wide_clamp(0.0f) == NV2A_WIDE_4_3);
    CHECK(nv2a_wide_clamp(-2.0f) == NV2A_WIDE_4_3);
    CHECK(nv2a_wide_clamp(NAN) == NV2A_WIDE_4_3);
    CHECK(nv2a_wide_clamp(2.4f) == 2.4f);
    CHECK(nv2a_wide_clamp(32.0f / 9.0f) == 32.0f / 9.0f);    /* no ceiling */
}

static void squeeze(void)
{
    CHECK(nv2a_wide_squeeze_of(NV2A_WIDE_16_9) == 0.75f);    /* exactly */
    CHECK(near(nv2a_wide_squeeze_of(1.6f), 4.0f / 4.8f));
    CHECK(near(nv2a_wide_squeeze_of(2.0f), 2.0f / 3.0f));
    CHECK(near(nv2a_wide_squeeze_of(64.0f / 27.0f), 0.5625f)); /* 21:9 */
    CHECK(near(nv2a_wide_squeeze_of(32.0f / 9.0f), 0.375f));
    CHECK(nv2a_wide_squeeze_of(NV2A_WIDE_4_3) == 1.0f);      /* no squeeze */
    CHECK(nv2a_wide_squeeze_of(1.25f) == 1.0f);
    CHECK(nv2a_wide_squeeze_of(NAN) == 1.0f);
}

static int same(const char *a, const char *b) { return a && b && !strcmp(a, b); }

static void menu_shapes(void)
{
    int i;
    CHECK(same(nv2a_wide_shape_names[NV2A_WIDE_SHAPE_4_3], "4:3"));
    CHECK(same(nv2a_wide_shape_names[NV2A_WIDE_SHAPE_16_10], "16:10"));
    CHECK(same(nv2a_wide_shape_names[NV2A_WIDE_SHAPE_16_9], "16:9"));
    CHECK(same(nv2a_wide_shape_names[NV2A_WIDE_SHAPE_21_9], "21:9"));
    CHECK(same(nv2a_wide_shape_names[NV2A_WIDE_SHAPE_MATCH], "Match screen"));

    CHECK(nv2a_wide_shape_key(NV2A_WIDE_SHAPE_4_3) == NULL);   /* that's widescreen=0 */
    CHECK(same(nv2a_wide_shape_key(NV2A_WIDE_SHAPE_MATCH), "match"));
    CHECK(nv2a_wide_shape_key(-1) == NULL);
    CHECK(nv2a_wide_shape_key(NV2A_WIDE_SHAPES) == NULL);
    for (i = NV2A_WIDE_SHAPE_16_10; i < NV2A_WIDE_SHAPES; i++)
        CHECK(nv2a_wide_shape_parse(nv2a_wide_shape_key(i)) == i);

    CHECK(nv2a_wide_shape_parse("21:9\n") == NV2A_WIDE_SHAPE_21_9);
    CHECK(nv2a_wide_shape_parse("16:10\r\n") == NV2A_WIDE_SHAPE_16_10);
    CHECK(nv2a_wide_shape_parse("4:3") == -1);
    CHECK(nv2a_wide_shape_parse("16:9x") == -1);
    CHECK(nv2a_wide_shape_parse("16:") == -1);
    CHECK(nv2a_wide_shape_parse("Match screen") == -1);
    CHECK(nv2a_wide_shape_parse("") == -1);
    CHECK(nv2a_wide_shape_parse(NULL) == -1);

    CHECK(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_4_3, 1920, 1080) == NV2A_WIDE_4_3);
    CHECK(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_16_10, 0, 0) == 1.6f);
    CHECK(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_16_9, 0, 0) == NV2A_WIDE_16_9);
    CHECK(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_21_9, 0, 0) == 64.0f / 27.0f);
    CHECK(near(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_MATCH, 1920, 1080), NV2A_WIDE_16_9));
    CHECK(near(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_MATCH, 1280, 800), 1.6f));    /* 16:10 */
    CHECK(near(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_MATCH, 3440, 1440), 3440.0f / 1440.0f));
    CHECK(near(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_MATCH, 5120, 1440), 32.0f / 9.0f));
    CHECK(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_MATCH, 1280, 1024) == NV2A_WIDE_4_3); /* 5:4 */
    CHECK(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_MATCH, 0, 0) == NV2A_WIDE_16_9);  /* not known */
    CHECK(nv2a_wide_shape_aspect(NV2A_WIDE_SHAPE_MATCH, 1920, 0) == NV2A_WIDE_16_9);
}

static void frame(void)
{
    CHECK(nv2a_wide_frame_sized(640, 480));
    CHECK(!nv2a_wide_frame_sized(512, 512));                 /* the boost's copy */
    CHECK(!nv2a_wide_frame_sized(128, 128));
    CHECK(!nv2a_wide_frame_sized(640, 448));
    CHECK(!nv2a_wide_frame_sized(1024, 1024));               /* graffiti */
    CHECK(!nv2a_wide_frame_sized(0, 0));
}

static void ndc_is(float x0, float x1, float y0, float y1, const float off[2],
                   const float scale[2], float w0, float w1, float w2, float w3)
{
    float e[4] = { x0, x1, y0, y1 };
    nv2a_wide_extent_ndc(e, off, scale);
    if (!near(e[0], w0) || !near(e[1], w1) || !near(e[2], w2) || !near(e[3], w3)) {
        printf("widescreen: FAIL extent %.1f..%.1f x %.1f..%.1f: %.4f..%.4f x %.4f..%.4f, "
               "not %.4f..%.4f x %.4f..%.4f\n", (double)x0, (double)x1, (double)y0, (double)y1,
               (double)e[0], (double)e[1], (double)e[2], (double)e[3],
               (double)w0, (double)w1, (double)w2, (double)w3);
        s_failures++;
    }
}

static void ndc(void)
{
    const float off[2] = { 320.0f, 240.0f }, scale[2] = { 320.0f, -240.0f };
    const float off4x[2] = { 1280.0f, 960.0f }, scale4x[2] = { 1280.0f, -960.0f };
    const float none[2] = { 0.0f, 0.0f };
    ndc_is(0.0f, 640.0f, 0.0f, 480.0f, off, scale, -1.0f, 1.0f, -1.0f, 1.0f);   /* the frame */
    ndc_is(100.0f, 200.0f, 20.0f, 60.0f, off, scale,
           -0.6875f, -0.375f, -0.9166667f, -0.75f);                            /* a HUD piece */
    ndc_is(0.0f, 2560.0f, 0.0f, 1920.0f, off4x, scale4x, -1.0f, 1.0f, -1.0f, 1.0f);
    ndc_is(320.0f, 640.0f, 240.0f, 480.0f, none, none, 1.0f, 2.0f, 1.0f, 2.0f); /* no scale */
}

static void overlay(void)
{
    const float hud[4]   = { -0.6875f, -0.375f, -0.92f, -0.75f };
    const float frame[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
    const float wide[4]  = { -0.9f, 0.9f, -0.2f, 0.2f };      /* 1.8 across */
    const float tall[4]  = { -0.2f, 0.2f, -0.9f, 0.9f };      /* 1.8 down */
    const float under[4] = { -0.89f, 0.89f, -0.89f, 0.89f };  /* 1.78 both ways */
    CHECK(nv2a_wide_overlay(hud));
    CHECK(!nv2a_wide_overlay(frame));
    CHECK(!nv2a_wide_overlay(wide));
    CHECK(!nv2a_wide_overlay(tall));
    CHECK(nv2a_wide_overlay(under));
}

static void clip_is(uint32_t x0, uint32_t x1, float s, uint32_t w0, uint32_t w1)
{
    uint32_t a = x0, b = x1;
    nv2a_wide_clip_x(640, s, &a, &b);
    if (a != w0 || b != w1) {
        printf("widescreen: FAIL clip %u..%u by %.3f: %u..%u, not %u..%u\n",
               x0, x1, (double)s, a, b, w0, w1);
        s_failures++;
    }
}

static void clip(void)
{
    clip_is(0, 640, 0.75f, 80, 560);            /* the whole frame: the middle 4:3 at 16:9 */
    clip_is(100, 200, 0.75f, 155, 230);         /* exact */
    clip_is(101, 201, 0.75f, 155, 231);         /* near edge down, far edge up */
    clip_is(0, 640, 1.0f, 0, 640);              /* no squeeze */
    clip_is(0, 640, 2.0f / 3.0f, 106, 534);     /* 2:1 */
}

static void shift(void)
{
    /* Character select's left window, 40..299 by 100..379, at 16:9. */
    CHECK(nv2a_wide_shift(40, 299, 100, 379, 0.75f) == -37.5f);
    CHECK(nv2a_wide_shift(340, 599, 100, 379, 0.75f) == 37.5f);
    CHECK(nv2a_wide_shift(0, 639, 0, 479, 0.75f) == 0.0f);    /* the scene */
    CHECK(nv2a_wide_shift(0, 599, 0, 479, 0.75f) == 0.0f);    /* 600 wide: the scene */
    CHECK(nv2a_wide_shift(0, 598, 0, 479, 0.75f) != 0.0f);    /* 599 wide: framed */
    CHECK(nv2a_wide_shift(300, 300, 0, 479, 0.75f) == 0.0f);  /* empty */
    CHECK(nv2a_wide_shift(40, 299, 100, 100, 0.75f) == 0.0f);
    CHECK(nv2a_wide_shift(40, 299, 100, 379, 1.0f) == 0.0f);  /* no squeeze, no move */
}

static void raster_is(uint32_t w, uint32_t h, uint32_t sc, float a,
                      uint32_t ww, uint32_t wh, float wshape)
{
    uint32_t pw = 0, ph = 0;
    float shape = -1.0f;
    nv2a_wide_raster(w, h, sc, a, &pw, &ph, &shape);
    if (pw != ww || ph != wh || shape != wshape) {
        printf("widescreen: FAIL raster %ux%u at %ux and %.4f: %ux%u at %.4f, not %ux%u at %.4f\n",
               w, h, sc, (double)a, pw, ph, (double)shape, ww, wh, (double)wshape);
        s_failures++;
    }
}

static void raster(void)
{
    const float r16_10 = 1.6f, r21_9 = 64.0f / 27.0f, r32_9 = 32.0f / 9.0f;
    uint32_t sc;
    raster_is(640, 480, 2, NV2A_WIDE_16_9, 1707, 960, NV2A_WIDE_16_9);   /* 1706.67 */
    raster_is(640, 480, 1, NV2A_WIDE_16_9, 853, 480, NV2A_WIDE_16_9);
    raster_is(640, 480, 3, NV2A_WIDE_16_9, 2560, 1440, NV2A_WIDE_16_9);  /* exact */
    raster_is(640, 480, 6, NV2A_WIDE_16_9, 5120, 2880, NV2A_WIDE_16_9);
    raster_is(640, 480, 2, r16_10, 1536, 960, r16_10);
    raster_is(640, 480, 2, r21_9, 2276, 960, r21_9);                     /* 2275.56 */
    raster_is(640, 480, 6, r21_9, 6827, 2880, r21_9);
    raster_is(640, 480, 6, r32_9, 10240, 2880, r32_9);                   /* no cap */
    raster_is(640, 480, 2, NV2A_WIDE_4_3, 1280, 960, 0.0f);              /* 4:3: as before */
    raster_is(640, 480, 2, 0.0f, 1280, 960, 0.0f);                       /* not widened */
    raster_is(640, 480, 2, 1.25f, 1280, 960, 0.0f);
    raster_is(512, 512, 2, NV2A_WIDE_16_9, 1024, 1024, 0.0f);            /* the boost's copy */
    raster_is(128, 128, 3, NV2A_WIDE_16_9, 384, 384, 0.0f);
    raster_is(1024, 1024, 1, r21_9, 1024, 1024, 0.0f);                   /* graffiti */
    raster_is(640, 448, 2, NV2A_WIDE_16_9, 1280, 896, 0.0f);
    raster_is(640, 480, 0, NV2A_WIDE_16_9, 853, 480, NV2A_WIDE_16_9);    /* no scale is 1x */
    /* Every scale and shape: the height is the scale's, and the width the
     * shape's of it to the nearest pixel. */
    for (sc = 1; sc <= NV2A_SCALE_MAX; sc++) {
        const float shapes[] = { r16_10, NV2A_WIDE_16_9, r21_9, r32_9, 2.0f };
        unsigned k;
        for (k = 0; k < sizeof shapes / sizeof shapes[0]; k++) {
            uint32_t pw, ph;
            float shape;
            double want = 480.0 * sc * shapes[k];
            nv2a_wide_raster(640, 480, sc, shapes[k], &pw, &ph, &shape);
            CHECK(ph == 480 * sc);
            CHECK(fabs((double)pw - want) <= 0.5);
            CHECK(shape == shapes[k]);
        }
    }
}

static void fit_is(float a, int ww, int wh, int w_dw, int w_dh)
{
    int dw = -1, dh = -1;
    nv2a_wide_fit(a, ww, wh, &dw, &dh);
    if (dw != w_dw || dh != w_dh) {
        printf("widescreen: FAIL fit %.4f into %dx%d: %dx%d, not %dx%d\n",
               (double)a, ww, wh, dw, dh, w_dw, w_dh);
        s_failures++;
    }
}

static void fit(void)
{
    fit_is(NV2A_WIDE_16_9, 1920, 1080, 1920, 1080);     /* fills it */
    fit_is(NV2A_WIDE_16_9, 3840, 2160, 3840, 2160);
    fit_is(NV2A_WIDE_16_9, 1280, 720, 1280, 720);
    fit_is(1.6f, 1280, 800, 1280, 800);                 /* 16:10 on a 16:10 screen */
    fit_is(64.0f / 27.0f, 2560, 1080, 2560, 1080);      /* 21:9 */
    fit_is(64.0f / 27.0f, 5120, 2160, 5120, 2160);
    fit_is(3440.0f / 1440.0f, 3440, 1440, 3440, 1440);  /* Match screen */
    fit_is(32.0f / 9.0f, 5120, 1440, 5120, 1440);
    fit_is(NV2A_WIDE_16_9, 1280, 800, 1280, 720);       /* bars above and below */
    fit_is(NV2A_WIDE_16_9, 1920, 1200, 1920, 1080);
    fit_is(64.0f / 27.0f, 1920, 1080, 1920, 810);
    fit_is(1.6f, 1920, 1080, 1728, 1080);               /* bars at the sides */
    fit_is(NV2A_WIDE_16_9, 2560, 1080, 1920, 1080);
    fit_is(NV2A_WIDE_4_3, 1920, 1080, 1440, 1080);
    fit_is(NV2A_WIDE_16_9, 1, 1, 1, 1);                 /* never 0, never past */
    fit_is(NV2A_WIDE_16_9, 1000, 1, 2, 1);               /* 1.78 to the nearest */
    fit_is(0.0f, 640, 480, 640, 480);                   /* no shape: the window */
}

static void scale_names(void)
{
    char b[32];
    int i;
    for (i = 0; i < NV2A_SCALE_MAX; i++) {
        nv2a_wide_scale_name(b, sizeof b, (uint32_t)(i + 1), NV2A_WIDE_4_3);
        CHECK(!strcmp(b, nv2a_scale_names[i]));
    }
    nv2a_wide_scale_name(b, sizeof b, 3, NV2A_WIDE_16_9);
    CHECK(!strcmp(b, "3x  2560x1440"));
    nv2a_wide_scale_name(b, sizeof b, 2, NV2A_WIDE_16_9);
    CHECK(!strcmp(b, "2x  1707x960"));
    nv2a_wide_scale_name(b, sizeof b, 2, 64.0f / 27.0f);
    CHECK(!strcmp(b, "2x  2276x960"));
    nv2a_wide_scale_name(b, sizeof b, 6, 1.6f);
    CHECK(!strcmp(b, "6x  4608x2880"));
}

int main(void)
{
    shapes();
    squeeze();
    menu_shapes();
    frame();
    ndc();
    overlay();
    clip();
    shift();
    raster();
    fit();
    scale_names();
    if (s_failures) {
        printf("widescreen: %d failed\n", s_failures);
        return 1;
    }
    printf("widescreen: all cases pass\n");
    return 0;
}
