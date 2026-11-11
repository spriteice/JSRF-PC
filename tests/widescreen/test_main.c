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
 *  11. F10 goes to 4:3 from every wide shape, and to Match screen from 4:3,
 *      so two presses come back to Match screen.
 *  12. A HUD mark reads back as its piece, side and flags, a menu part's
 *      across the width among them, and a value that isn't one (D3D's own
 *      no-operation values among them) doesn't.
 *  13. Between a HUD piece's marks, a draw moves to its side's edge: a piece
 *      at the 4:3 edge lands on the screen's edge at every wide shape,
 *      squeezed 2D and widened 3D alike, and keeps its distance from it
 *      squeezed; nothing moves at 4:3 or outside the marks. A piece in a
 *      viewport of its own, squeezed about that viewport's centre, lands
 *      the same: the rolling number's 51 to 210 at 38.25 to 157.5 at 16:9,
 *      as its clip moved with it.
 *  14. Its clip is the middle 4:3 moved with it, so the screen's edge takes
 *      the 4:3 edge's place, and a clip of its own moves the same way.
 *  15. Only a view as wide as the frame (or with no scale) is one player's:
 *      a split screen's half isn't.
 *  16. A squeezed overlay in a viewport narrower than the frame, a split
 *      screen's HUD, is squeezed about the frame's centre and lands inside
 *      its view's squeezed clip, at every wide shape: the left half's 0 to
 *      320 at 80 to 320 at 16:9, the right half's at 320 to 560. A viewport
 *      centred on the frame moves nothing, and nor does 4:3, a viewport
 *      outside the frame, a mirrored one or NaN.
 *  17. A menu part drawn across the width is stretched by 1/s about the
 *      centre, so the 4:3 screen the widened projection leaves it in fills
 *      the screen at every wide shape: its 0 to 640 at 0 to 640. It isn't
 *      moved, and its clip isn't moved; nothing is stretched at 4:3 or for
 *      an s that isn't between 0 and 1. Text across the whole width (a
 *      ticker) isn't moved either.
 *  18. A copy of a menu's backdrop shown on one half of the screen is
 *      clipped at the surface's middle, the left half up to it and the
 *      right half from it, so the two meet with no gap and no overlap at
 *      any width; a clip of its own is cut the same way, one wholly on the
 *      other half comes out empty, and neither half's clip is moved.
 *  19. The copies move out by whole steps of the grid, the nearest to the
 *      parts' move: 120 title pixels at 16:9, 80 at 16:10, 240 at 21:9, at
 *      every wide shape a whole number of steps within half a step of the
 *      parts' move, so the left copy's 4:3 edge lands within half a step
 *      of the screen's (and the right's the same); none at 4:3, too near
 *      it, or for an s that isn't between 0 and 1. A 3D copy moves s times
 *      that in the frame's pixels (90 at 16:9), a squeezed one that itself,
 *      the left copy left and the right right, and no other side moves.
 *  20. The side fill: on a filled frame a screen-space draw is squeezed when
 *      it is an overlay, or full-screen art (textured, sampling no surface);
 *      an untextured layer and a copy of the frame stay full width. Its
 *      columns: the 4:3 screen's whole columns are kept, 214 to 1493 of
 *      1707 at 2x and 16:9 (its edges at 213.375 and 1493.625), and the
 *      column a title pixel inside each edge, 216 and 1490, is copied out;
 *      16:10's edge at 2x, column 128 itself, is kept. At every wide shape
 *      and scale the kept columns hold the 4:3 screen's whole columns and
 *      no more, both sides are filled, the two sources sit as far inside
 *      their edges, and inside the kept columns. Nothing is filled at 4:3,
 *      for an s that isn't between 0 and 1, or on a raster too narrow.
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

static void f10(void)
{
    int i;
    CHECK(nv2a_wide_f10(NV2A_WIDE_SHAPE_4_3) == NV2A_WIDE_SHAPE_MATCH);
    for (i = NV2A_WIDE_SHAPE_16_10; i < NV2A_WIDE_SHAPES; i++) {
        CHECK(nv2a_wide_f10(i) == NV2A_WIDE_SHAPE_4_3);
        CHECK(nv2a_wide_f10(nv2a_wide_f10(i)) == NV2A_WIDE_SHAPE_MATCH);
    }
}

static void hud_marks(void)
{
    static const unsigned pieces[] = { 0, 7, 255 };
    static const int sides[] = { NV2A_WIDE_HUD_LEFT, NV2A_WIDE_HUD_NONE, NV2A_WIDE_HUD_RIGHT,
                                 NV2A_WIDE_HUD_ACROSS, NV2A_WIDE_HUD_WHOLE,
                                 NV2A_WIDE_HUD_LEFT_HALF, NV2A_WIDE_HUD_RIGHT_HALF,
                                 NV2A_WIDE_HUD_FILL, NV2A_WIDE_HUD_OVER, NV2A_WIDE_HUD_BARS,
                                 NV2A_WIDE_HUD_STRIP, NV2A_WIDE_HUD_LINES };
    static const unsigned flags[] = { 0u, NV2A_WIDE_HUD_OWN_VIEW, NV2A_WIDE_HUD_ON_GRID,
                                      NV2A_WIDE_HUD_OWN_VIEW | NV2A_WIDE_HUD_ON_GRID };
    /* past the last side, with each flag and both; sides with a bit the
     * flags don't use */
    static const uint32_t others[] = { 0u, 1u, 2u, 5u, 8u, 9u, 0x4855000Cu, 0x4855008Cu,
                                       0x4855004Cu, 0x485500CCu, 0x48550013u, 0x48550023u,
                                       0x48560001u, 0x00000102u, 0xFFFFFFFFu };
    unsigned i, j, k, piece, fl;
    int side;
    for (i = 0; i < sizeof pieces / sizeof pieces[0]; i++)
        for (j = 0; j < sizeof sides / sizeof sides[0]; j++)
            for (k = 0; k < sizeof flags / sizeof flags[0]; k++) {
                piece = 999; side = 9; fl = 99;
                CHECK(nv2a_wide_hud_read(nv2a_wide_hud_mark(pieces[i], sides[j], flags[k]),
                                         &piece, &side, &fl));
                CHECK(piece == pieces[i] && side == sides[j] && fl == flags[k]);
            }
    CHECK(nv2a_wide_hud_mark(0, NV2A_WIDE_HUD_RIGHT, 0) != nv2a_wide_hud_mark(0, NV2A_WIDE_HUD_LEFT, 0));
    CHECK(nv2a_wide_hud_mark(0, NV2A_WIDE_HUD_LEFT, 0)
          != nv2a_wide_hud_mark(0, NV2A_WIDE_HUD_LEFT, NV2A_WIDE_HUD_OWN_VIEW));
    /* build 3b-1's marks read as they did */
    CHECK(nv2a_wide_hud_mark(0, NV2A_WIDE_HUD_RIGHT, 0) == 0x48550002u);
    CHECK(nv2a_wide_hud_mark(0, NV2A_WIDE_HUD_NONE, 0) == 0x48550000u);
    CHECK(nv2a_wide_hud_mark(0x40, NV2A_WIDE_HUD_ACROSS, 0) == 0x48554003u);
    CHECK(nv2a_wide_hud_mark(0xC4, NV2A_WIDE_HUD_WHOLE, 0) == 0x4855C404u);
    CHECK(nv2a_wide_hud_mark(0x40, NV2A_WIDE_HUD_LEFT_HALF, 0) == 0x48554005u);
    CHECK(nv2a_wide_hud_mark(0x40, NV2A_WIDE_HUD_RIGHT_HALF, 0) == 0x48554006u);
    CHECK(nv2a_wide_hud_mark(0xC5, NV2A_WIDE_HUD_FILL, 0) == 0x4855C507u);
    CHECK(nv2a_wide_hud_mark(0xC6, NV2A_WIDE_HUD_OVER, 0) == 0x4855C608u);
    CHECK(nv2a_wide_hud_mark(0xC8, NV2A_WIDE_HUD_BARS, 0) == 0x4855C809u);
    CHECK(nv2a_wide_hud_mark(8, NV2A_WIDE_HUD_STRIP, 0) == 0x4855080Au);
    CHECK(nv2a_wide_hud_mark(0x44, NV2A_WIDE_HUD_RIGHT, NV2A_WIDE_HUD_ON_GRID) == 0x48554442u);
    CHECK(nv2a_wide_hud_mark(0xC9, NV2A_WIDE_HUD_LINES, 0) == 0x4855C90Bu);
    CHECK(nv2a_wide_hud_mark(0xC0, NV2A_WIDE_HUD_LEFT, NV2A_WIDE_HUD_ON_GRID) == 0x4855C041u);
    for (i = 0; i < sizeof others / sizeof others[0]; i++)
        CHECK(!nv2a_wide_hud_read(others[i], &piece, &side, &fl));
    CHECK(NV2A_WIDE_HUD_METHOD > 0x1EA4u && (NV2A_WIDE_HUD_METHOD & 3u) == 0
          && NV2A_WIDE_HUD_METHOD <= 0x1FFCu);
}

/* Where a draw at the title's x, in a viewport centred at c, lands on the
 * 640-wide screen once moved: the shader takes the move off x, and
 * squeezes a 2D overlay about the viewport's centre after; a 3D piece comes
 * out of the widened projection already at its squeezed place, and only
 * moves. */
static float hud_lands_in(float x, int side, float s, int squeezed, float c)
{
    float w = nv2a_wide_hud_shift(side, s, squeezed, c);
    if (squeezed) return c + s * (x - w - c);
    return c + s * (x - c) - w;
}
static float hud_lands(float x, int side, float s, int squeezed)
{
    return hud_lands_in(x, side, s, squeezed, 320.0f);
}

static int near_px(float a, float b) { return fabsf(a - b) < 1e-3f; }

static void hud_move(void)
{
    static const float shapes[] = { 1.6f, NV2A_WIDE_16_9, 64.0f / 27.0f, 32.0f / 9.0f };
    unsigned i;
    for (i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
        float s = nv2a_wide_squeeze_of(shapes[i]);
        /* the 4:3 edges to the screen's, 2D and 3D */
        CHECK(near_px(hud_lands(640.0f, NV2A_WIDE_HUD_RIGHT, s, 1), 640.0f));
        CHECK(near_px(hud_lands(0.0f, NV2A_WIDE_HUD_LEFT, s, 1), 0.0f));
        CHECK(near_px(hud_lands(640.0f, NV2A_WIDE_HUD_RIGHT, s, 0), 640.0f));
        CHECK(near_px(hud_lands(0.0f, NV2A_WIDE_HUD_LEFT, s, 0), 0.0f));
        /* a piece 52 pixels in from the right (the health bar's end) keeps
         * 52 squeezed pixels from it */
        CHECK(near_px(hud_lands(588.0f, NV2A_WIDE_HUD_RIGHT, s, 1), 640.0f - 52.0f * s));
        /* outside the marks, the squeeze alone */
        CHECK(near_px(hud_lands(588.0f, NV2A_WIDE_HUD_NONE, s, 1), 320.0f + s * 268.0f));
        /* a piece in a viewport of its own (the rolling number's, centred at
         * 130.5) lands as it would in the whole frame's: its x is the
         * frame's, whatever the centre the shader squeezes it about */
        CHECK(near_px(hud_lands_in(51.0f, NV2A_WIDE_HUD_LEFT, s, 1, 130.5f),
                      hud_lands(51.0f, NV2A_WIDE_HUD_LEFT, s, 1)));
        CHECK(near_px(hud_lands_in(210.0f, NV2A_WIDE_HUD_LEFT, s, 1, 130.5f), 210.0f * s));
        CHECK(near_px(hud_lands_in(600.0f, NV2A_WIDE_HUD_RIGHT, s, 1, 560.0f),
                      640.0f - 40.0f * s));
    }
    CHECK(near_px(nv2a_wide_hud_shift(NV2A_WIDE_HUD_RIGHT, 0.75f, 1, 320.0f), -320.0f / 3.0f));
    CHECK(near_px(nv2a_wide_hud_shift(NV2A_WIDE_HUD_LEFT, 0.75f, 1, 320.0f), 320.0f / 3.0f));
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_RIGHT, 0.75f, 0, 320.0f) == -80.0f);
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_LEFT, 0.75f, 0, 320.0f) == 80.0f);
    /* the rolling number at 16:9: 43.5 title pixels, and its first digit's
     * left edge at 38.25, where its clip goes (hud_clip) */
    CHECK(near_px(nv2a_wide_hud_shift(NV2A_WIDE_HUD_LEFT, 0.75f, 1, 130.5f), 43.5f));
    CHECK(near_px(hud_lands_in(51.0f, NV2A_WIDE_HUD_LEFT, 0.75f, 1, 130.5f), 38.25f));
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_RIGHT, 1.0f, 1, 320.0f) == 0.0f);    /* 4:3 */
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_LEFT, 1.0f, 0, 320.0f) == 0.0f);
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_NONE, 0.75f, 1, 320.0f) == 0.0f);    /* no piece */
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_NONE, 0.75f, 1, 130.5f) == 0.0f);
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_RIGHT, 0.0f, 1, 320.0f) == 0.0f);
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_RIGHT, NAN, 1, 320.0f) == 0.0f);
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_RIGHT, 0.75f, 1, NAN) == 0.0f);
}

static void hud_clip_is(uint32_t x0, uint32_t x1, float s, int side, uint32_t w0, uint32_t w1)
{
    uint32_t a = x0, b = x1;
    nv2a_wide_hud_clip_x(640, s, side, &a, &b);
    if (a != w0 || b != w1) {
        printf("widescreen: FAIL HUD clip %u..%u by %.4f to side %d: %u..%u, not %u..%u\n",
               x0, x1, (double)s, side, a, b, w0, w1);
        s_failures++;
    }
}

static void hud_clip(void)
{
    const float s16_10 = nv2a_wide_squeeze_of(1.6f), s21_9 = nv2a_wide_squeeze_of(64.0f / 27.0f);
    hud_clip_is(0, 640, 0.75f, NV2A_WIDE_HUD_RIGHT, 160, 640);   /* the middle 4:3, moved */
    hud_clip_is(0, 640, 0.75f, NV2A_WIDE_HUD_LEFT, 0, 480);
    hud_clip_is(0, 640, 0.75f, NV2A_WIDE_HUD_NONE, 80, 560);     /* nv2a_wide_clip_x's */
    hud_clip_is(0, 640, s21_9, NV2A_WIDE_HUD_RIGHT, 280, 640);
    hud_clip_is(0, 640, s21_9, NV2A_WIDE_HUD_LEFT, 0, 360);
    hud_clip_is(0, 640, s16_10, NV2A_WIDE_HUD_RIGHT, 106, 640);
    hud_clip_is(0, 640, s16_10, NV2A_WIDE_HUD_LEFT, 0, 534);
    hud_clip_is(393, 589, 0.75f, NV2A_WIDE_HUD_RIGHT, 454, 602); /* its own clip, moved */
    hud_clip_is(51, 211, 0.75f, NV2A_WIDE_HUD_LEFT, 38, 159);    /* the rolling number's */
    hud_clip_is(0, 640, 1.0f, NV2A_WIDE_HUD_RIGHT, 0, 640);      /* 4:3 */
    hud_clip_is(0, 640, NAN, NV2A_WIDE_HUD_LEFT, 0, 640);
}

static void hud_view(void)
{
    CHECK(nv2a_wide_hud_whole_view(320.0f));       /* one player */
    CHECK(nv2a_wide_hud_whole_view(-320.0f));
    CHECK(nv2a_wide_hud_whole_view(300.0f));       /* 600 wide */
    CHECK(!nv2a_wide_hud_whole_view(299.0f));
    CHECK(!nv2a_wide_hud_whole_view(160.0f));      /* a split screen's half */
    CHECK(nv2a_wide_hud_whole_view(0.0f));         /* no scale: the whole surface */
    CHECK(nv2a_wide_hud_whole_view(NAN));
}

/* Where a squeezed overlay at the title's x, in a viewport centred at c
 * and half wide, lands on the 640-wide screen: lined up, then squeezed
 * about the viewport's centre, as the shader does. */
static float overlay_lands(float x, float c, float half, float s)
{
    return c + s * (x - nv2a_wide_overlay_shift(c, half, s) - c);
}

/* Whether x is in the pixel that starts at p. */
static int in_px(float x, float p) { return x > p - 1e-3f && x < p + 1.0f + 1e-3f; }

static void split(void)
{
    static const float shapes[] = { 1.6f, NV2A_WIDE_16_9, 64.0f / 27.0f, 32.0f / 9.0f };
    unsigned i;
    for (i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
        float s = nv2a_wide_squeeze_of(shapes[i]);
        uint32_t a = 0, b = 320;
        /* each half's edges where its clip goes (nv2a_wide_clip_x), and a
         * point between where the whole frame's squeeze puts it */
        nv2a_wide_clip_x(640, s, &a, &b);
        CHECK(near_px(overlay_lands(0.0f, 160.0f, 160.0f, s), 320.0f - 320.0f * s));
        CHECK(in_px(overlay_lands(0.0f, 160.0f, 160.0f, s), (float)a));
        CHECK(in_px(overlay_lands(320.0f, 160.0f, 160.0f, s), (float)b - 1.0f));
        CHECK(near_px(overlay_lands(640.0f, 480.0f, 160.0f, s), 320.0f + 320.0f * s));
        CHECK(near_px(overlay_lands(320.0f, 480.0f, 160.0f, s), 320.0f));
        CHECK(near_px(overlay_lands(100.0f, 160.0f, 160.0f, s), 320.0f + s * (100.0f - 320.0f)));
        CHECK(near_px(overlay_lands(500.0f, 480.0f, 160.0f, s), 320.0f + s * 180.0f));
        /* a quarter's view, and the frame's own */
        CHECK(near_px(overlay_lands(50.0f, 160.0f, 160.0f, s),
                      overlay_lands(50.0f, 320.0f, 320.0f, s)));
        CHECK(nv2a_wide_overlay_shift(320.0f, 320.0f, s) == 0.0f);
        CHECK(nv2a_wide_overlay_shift(320.0f, 160.0f, s) == 0.0f);   /* a top or bottom half */
    }
    /* at 16:9: the left half's 0 to 320 at 80 to 320, 53.3 title pixels */
    CHECK(near_px(nv2a_wide_overlay_shift(160.0f, 160.0f, 0.75f), -160.0f / 3.0f));
    CHECK(near_px(nv2a_wide_overlay_shift(480.0f, 160.0f, 0.75f), 160.0f / 3.0f));
    CHECK(near_px(overlay_lands(0.0f, 160.0f, 160.0f, 0.75f), 80.0f));
    CHECK(near_px(overlay_lands(320.0f, 160.0f, 160.0f, 0.75f), 320.0f));
    CHECK(near_px(overlay_lands(640.0f, 480.0f, 160.0f, 0.75f), 560.0f));
    clip_is(0, 320, 0.75f, 80, 320);
    clip_is(320, 640, 0.75f, 320, 560);
    CHECK(nv2a_wide_overlay_shift(160.0f, 160.0f, 1.0f) == 0.0f);     /* 4:3 */
    CHECK(nv2a_wide_overlay_shift(160.0f, 160.0f, NAN) == 0.0f);
    CHECK(nv2a_wide_overlay_shift(160.0f, 160.0f, 0.0f) == 0.0f);
    CHECK(nv2a_wide_overlay_shift(NAN, 160.0f, 0.75f) == 0.0f);
    CHECK(nv2a_wide_overlay_shift(160.0f, NAN, 0.75f) == 0.0f);
    CHECK(nv2a_wide_overlay_shift(160.0f, -160.0f, 0.75f) == 0.0f);   /* mirrored */
    CHECK(nv2a_wide_overlay_shift(160.0f, 0.0f, 0.75f) == 0.0f);      /* no scale */
    CHECK(nv2a_wide_overlay_shift(1280.0f, 160.0f, 0.75f) == 0.0f);   /* another surface's */
    CHECK(nv2a_wide_overlay_shift(500.0f, 160.0f, 0.75f) == 0.0f);    /* past the frame */
    CHECK(nv2a_wide_overlay_shift(150.0f, 160.0f, 0.75f) == 0.0f);
    CHECK(nv2a_wide_overlay_shift(160.5f, 160.0f, 0.75f) != 0.0f);    /* a pixel's slack */
    CHECK(nv2a_wide_overlay_shift(159.5f, 160.0f, 0.75f) != 0.0f);
    CHECK(nv2a_wide_overlay_shift(160.0f, INFINITY, 0.75f) == 0.0f);
}

/* Where a 3D draw at the title's x lands once drawn across: the widened
 * projection leaves it at its squeezed place, and the stretch takes it out
 * from the centre again. */
static float across_lands(float x, float s)
{
    return 320.0f + nv2a_wide_across_stretch(s) * s * (x - 320.0f);
}

static void across(void)
{
    const float s16_10 = nv2a_wide_squeeze_of(16.0f / 10.0f);
    const float s21_9 = nv2a_wide_squeeze_of(64.0f / 27.0f);
    const float wide[] = { 0.75f, s16_10, s21_9 };
    unsigned i;
    uint32_t a = 0, b = 640;
    CHECK(near(nv2a_wide_across_stretch(0.75f), 4.0f / 3.0f));
    CHECK(near(nv2a_wide_across_stretch(s21_9), 64.0f / 36.0f));
    for (i = 0; i < sizeof wide / sizeof wide[0]; i++) {
        CHECK(near_px(across_lands(0.0f, wide[i]), 0.0f));
        CHECK(near_px(across_lands(640.0f, wide[i]), 640.0f));
        CHECK(near_px(across_lands(320.0f, wide[i]), 320.0f));
        CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_ACROSS, wide[i], 0, 320.0f) == 0.0f);
        CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_ACROSS, wide[i], 1, 320.0f) == 0.0f);
        CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_WHOLE, wide[i], 1, 320.0f) == 0.0f);
    }
    a = 0; b = 640;
    nv2a_wide_hud_clip_x(640, 0.75f, NV2A_WIDE_HUD_WHOLE, &a, &b);
    CHECK(a == 80 && b == 560);
    a = 0; b = 640;
    nv2a_wide_hud_clip_x(640, 0.75f, NV2A_WIDE_HUD_ACROSS, &a, &b);
    CHECK(a == 80 && b == 560);                                     /* nv2a_wide_clip_x's */
    CHECK(nv2a_wide_across_stretch(1.0f) == 1.0f);                  /* 4:3 */
    CHECK(nv2a_wide_across_stretch(0.0f) == 1.0f);
    CHECK(nv2a_wide_across_stretch(1.5f) == 1.0f);
    CHECK(nv2a_wide_across_stretch(NAN) == 1.0f);
}

static void half_is(uint32_t w, float s, int side, uint32_t x0, uint32_t x1, uint32_t w0, uint32_t w1)
{
    uint32_t a = x0, b = x1;
    nv2a_wide_half_clip_x(w, s, side, &a, &b);
    if (a != w0 || b != w1)
        printf("widescreen: half %d of %u at %.3f: %u..%u gave %u..%u, not %u..%u\n",
               side, w, (double)s, x0, x1, a, b, w0, w1);
    CHECK(a == w0 && b == w1);
}

static void halves(void)
{
    /* At 4:3 (and any s not between 0 and 1) the seam is the middle. */
    half_is(640, 1.0f, NV2A_WIDE_HUD_LEFT_HALF, 0, 640, 0, 320);
    half_is(640, 1.0f, NV2A_WIDE_HUD_RIGHT_HALF, 0, 640, 320, 640);
    half_is(641, 1.0f, NV2A_WIDE_HUD_LEFT_HALF, 0, 641, 0, 320);  /* odd: meet at 320 */
    half_is(641, 1.0f, NV2A_WIDE_HUD_RIGHT_HALF, 0, 641, 320, 641);
    half_is(640, 1.0f, NV2A_WIDE_HUD_LEFT_HALF, 100, 500, 100, 320);  /* a clip of its own */
    half_is(640, 1.0f, NV2A_WIDE_HUD_RIGHT_HALF, 100, 500, 320, 500);
    half_is(640, 1.0f, NV2A_WIDE_HUD_LEFT_HALF, 400, 600, 320, 320);  /* wholly right: empty */
    half_is(640, 1.0f, NV2A_WIDE_HUD_RIGHT_HALF, 10, 200, 320, 320);  /* wholly left: empty */
    half_is(640, 1.0f, NV2A_WIDE_HUD_NONE, 10, 200, 10, 200);     /* no half */
    half_is(640, 1.0f, NV2A_WIDE_HUD_ACROSS, 0, 640, 0, 640);
    CHECK(nv2a_wide_half_seam(640, 0.0f) == 320 && nv2a_wide_half_seam(640, NAN) == 320);
    /* Widened, half a cell left of the middle: 10.06 of the game's title
     * pixels, s as wide in the frame, 7.5 at 16:9 and 5.7 at 21:9 on a
     * 640-wide surface, to the nearest column; a wider surface by as much
     * more. */
    CHECK(nv2a_wide_half_seam(640, 0.75f) == 312);
    CHECK(nv2a_wide_half_seam(640, 0.5625f) == 314);
    CHECK(nv2a_wide_half_seam(1280, 0.75f) == 625);
    half_is(640, 0.75f, NV2A_WIDE_HUD_LEFT_HALF, 0, 640, 0, 312);
    half_is(640, 0.75f, NV2A_WIDE_HUD_RIGHT_HALF, 0, 640, 312, 640);
    half_is(640, 0.75f, NV2A_WIDE_HUD_RIGHT_HALF, 10, 200, 312, 312); /* wholly left: empty */
    half_is(640, 0.75f, NV2A_WIDE_HUD_NONE, 10, 200, 10, 200);
    {
        /* The seam in the middle of a cell, both ways from the grid's line
         * at the centre: a quarter of a step, give or take a column. */
        static const float ss[] = { 0.75f, (4.0f / 3.0f) / 1.6f, 0.5625f, 0.375f };
        unsigned i;
        for (i = 0; i < sizeof ss / sizeof ss[0]; i++) {
            float cell = 0.5f * NV2A_WIDE_HALF_STEP * ss[i];          /* in the frame */
            float off = (float)nv2a_wide_half_seam(640, ss[i]) - 320.0f;
            CHECK(fabsf(off + 0.5f * cell) <= 0.5f);
        }
    }
    {
        /* Each copy shows only the light grid at the seam, at each shape the
         * menu offers. The left copy shows the backdrop as far right as the
         * seam (in the game's pixels, about the centre) plus its move, and
         * the dark column down the backdrop's right-hand side starts DARK
         * steps right of the centre: a seam right of the middle showed half
         * a cell of it at 21:9, whose move is those six steps (spriteice, 8
         * November 2026). The right copy shows the backdrop from the seam
         * less its move, which must still be on the 4:3 screen, its light
         * grid running to the left edge. */
        enum { DARK = 6 };
        static const float ss[] = { (4.0f / 3.0f) / 1.6f, 0.75f, 0.5625f };
        unsigned i;
        for (i = 0; i < sizeof ss / sizeof ss[0]; i++) {
            const uint32_t w = 2560u;                                  /* a 4x raster's width */
            float seam = ((float)nv2a_wide_half_seam(w, ss[i]) - 0.5f * (float)w)
                       * (float)NV2A_SCALE_FRAME_W / (float)w / ss[i];  /* the game's pixels */
            float move = nv2a_wide_half_move(ss[i]);
            if (seam + move >= (float)DARK * NV2A_WIDE_HALF_STEP)
                printf("widescreen: at %.4f the left copy shows the dark column: seam %.2f, "
                       "move %.2f\n", (double)ss[i], (double)seam, (double)move);
            CHECK(seam + move < (float)DARK * NV2A_WIDE_HALF_STEP);
            CHECK(0.5f * (float)NV2A_SCALE_FRAME_W + seam - move > 0.0f);
        }
    }
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_LEFT_HALF, 0.75f, 0, 320.0f) == 0.0f);
    CHECK(nv2a_wide_hud_shift(NV2A_WIDE_HUD_RIGHT_HALF, 0.75f, 1, 320.0f) == 0.0f);
}

static void half_moves(void)
{
    static const float shapes[] = { 1.4f, 1.5f, 1.6f, 16.0f / 9.0f, 2.0f, 64.0f / 27.0f,
                                     32.0f / 9.0f };
    static const int others[] = { NV2A_WIDE_HUD_LEFT, NV2A_WIDE_HUD_NONE, NV2A_WIDE_HUD_RIGHT,
                                  NV2A_WIDE_HUD_ACROSS, NV2A_WIDE_HUD_WHOLE };
    const float s169 = 0.75f, s1610 = (4.0f / 3.0f) / 1.6f, s219 = 0.5625f;
    unsigned i;
    CHECK(NV2A_WIDE_HALF_STEP > 40.2f && NV2A_WIDE_HALF_STEP < 40.25f); /* 2 x 20.115 */
    CHECK(near(nv2a_wide_half_move(s169), 3.0f * NV2A_WIDE_HALF_STEP));  /* 16:9 */
    CHECK(near(nv2a_wide_half_move(s1610), 2.0f * NV2A_WIDE_HALF_STEP)); /* 16:10 */
    CHECK(near(nv2a_wide_half_move(s219), 6.0f * NV2A_WIDE_HALF_STEP));  /* 21:9 */
    for (i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
        float s = (4.0f / 3.0f) / shapes[i];
        float n = nv2a_wide_half_move(s);
        float steps = n / NV2A_WIDE_HALF_STEP;
        float parts = (1.0f / s - 1.0f) * 320.0f;
        float shift = nv2a_wide_half_shift(NV2A_WIDE_HUD_LEFT_HALF, s, 0);
        CHECK(fabsf(steps - floorf(steps + 0.5f)) < 1e-5f);        /* whole steps */
        CHECK(fabsf(n - parts) <= NV2A_WIDE_HALF_STEP / 2.0f);
        /* the left copy's 4:3 edge, 0, in the frame: squeezed about the
         * centre by the widened projection, then moved */
        CHECK(fabsf(320.0f + s * (0.0f - 320.0f) - shift) <= s * NV2A_WIDE_HALF_STEP / 2.0f + 1e-3f);
        CHECK(nv2a_wide_half_shift(NV2A_WIDE_HUD_RIGHT_HALF, s, 0) == -shift);
    }
    CHECK(nv2a_wide_half_move(1.0f) == 0.0f);                      /* 4:3 */
    CHECK(nv2a_wide_half_move((4.0f / 3.0f) / 1.4f) == 0.0f);      /* 16 pixels: none */
    CHECK(nv2a_wide_half_move(0.0f) == 0.0f);
    CHECK(nv2a_wide_half_move(1.5f) == 0.0f);
    CHECK(nv2a_wide_half_move(NAN) == 0.0f);
    CHECK(fabsf(nv2a_wide_half_shift(NV2A_WIDE_HUD_LEFT_HALF, s169, 0) - 90.5175f) < 1e-3f); /* 3D */
    CHECK(fabsf(nv2a_wide_half_shift(NV2A_WIDE_HUD_RIGHT_HALF, s169, 0) + 90.5175f) < 1e-3f);
    CHECK(near(nv2a_wide_half_shift(NV2A_WIDE_HUD_LEFT_HALF, s169, 1), 3.0f * NV2A_WIDE_HALF_STEP)); /* 2D */
    CHECK(near(nv2a_wide_half_shift(NV2A_WIDE_HUD_RIGHT_HALF, s169, 1), -3.0f * NV2A_WIDE_HALF_STEP));
    CHECK(fabsf(nv2a_wide_half_shift(NV2A_WIDE_HUD_LEFT_HALF, s1610, 0)
                - 2.0f * NV2A_WIDE_HALF_STEP * s1610) < 1e-4f);
    CHECK(fabsf(nv2a_wide_half_shift(NV2A_WIDE_HUD_LEFT_HALF, s219, 0) - 135.7763f) < 1e-3f);
    CHECK(nv2a_wide_half_shift(NV2A_WIDE_HUD_LEFT_HALF, 1.0f, 0) == 0.0f);          /* 4:3 */
    CHECK(nv2a_wide_half_shift(NV2A_WIDE_HUD_RIGHT_HALF, 1.0f, 1) == 0.0f);
    CHECK(nv2a_wide_half_shift(NV2A_WIDE_HUD_LEFT_HALF, NAN, 0) == 0.0f);
    for (i = 0; i < sizeof others / sizeof others[0]; i++) {
        CHECK(nv2a_wide_half_shift(others[i], s169, 0) == 0.0f);
        CHECK(nv2a_wide_half_shift(others[i], s169, 1) == 0.0f);
    }
}

/* A part's grid drawn as the backdrop's two copies, and the pieces at the
 * sides moved with them (spriteice, 8 November 2026: "Move them with the grid"). */
static void grid_slabs_and_pieces(void)
{
    static const float shapes[] = { 1.5f, 1.6f, 16.0f / 9.0f, 2.0f, 64.0f / 27.0f, 32.0f / 9.0f };
    const float s169 = 0.75f, s219 = 0.5625f, s14 = (4.0f / 3.0f) / 1.4f;
    unsigned i;
    /* the bands model's grid, 522 vertices; its bands and marks, 4 and 6 */
    CHECK(nv2a_wide_slab(522u, s169) && nv2a_wide_slab(522u, s219));
    CHECK(nv2a_wide_slab(NV2A_WIDE_SLAB_VERTS, s169));
    CHECK(!nv2a_wide_slab(NV2A_WIDE_SLAB_VERTS - 1u, s169));
    CHECK(!nv2a_wide_slab(6u, s169) && !nv2a_wide_slab(4u, s169) && !nv2a_wide_slab(0u, s169));
    CHECK(!nv2a_wide_slab(522u, 1.0f));                 /* 4:3 */
    CHECK(!nv2a_wide_slab(522u, s14));                  /* no whole step: the backdrop once */
    CHECK(!nv2a_wide_slab(522u, NAN) && !nv2a_wide_slab(522u, 0.0f));
    /* a piece on the grid moves as its side's copy does: 3D and squeezed */
    CHECK(fabsf(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_LEFT, s169, 0, 320.0f)
                - nv2a_wide_half_shift(NV2A_WIDE_HUD_LEFT_HALF, s169, 0)) < 1e-3f);   /* 90.5 */
    CHECK(fabsf(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_RIGHT, s169, 0, 320.0f)
                - nv2a_wide_half_shift(NV2A_WIDE_HUD_RIGHT_HALF, s169, 0)) < 1e-3f);
    CHECK(fabsf(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_LEFT, s169, 1, 320.0f)
                - 3.0f * NV2A_WIDE_HALF_STEP) < 1e-3f);                               /* 120.7 */
    CHECK(fabsf(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_RIGHT, s219, 1, 320.0f)
                + 6.0f * NV2A_WIDE_HALF_STEP) < 1e-3f);                               /* 241.4 */
    for (i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
        float s = (4.0f / 3.0f) / shapes[i];
        int sq;
        for (sq = 0; sq <= 1; sq++) {
            float hl = nv2a_wide_half_shift(NV2A_WIDE_HUD_LEFT_HALF, s, sq);
            float hr = nv2a_wide_half_shift(NV2A_WIDE_HUD_RIGHT_HALF, s, sq);
            CHECK(fabsf(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_LEFT, s, sq, 320.0f) - hl)
                  < 1e-3f);
            CHECK(fabsf(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_RIGHT, s, sq, 320.0f) - hr)
                  < 1e-3f);
            /* never more than half a step from where the margin takes it */
            CHECK(fabsf(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_LEFT, s, sq, 320.0f)
                        - nv2a_wide_hud_shift(NV2A_WIDE_HUD_LEFT, s, sq, 320.0f))
                  <= (sq ? 1.0f : s) * NV2A_WIDE_HALF_STEP / 2.0f + 1e-3f);
        }
        /* a viewport of its own: the same scale from the margin's move */
        CHECK(fabsf(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_LEFT, s, 1, 200.0f)
                    - nv2a_wide_hud_shift(NV2A_WIDE_HUD_LEFT, s, 1, 200.0f)
                      * nv2a_wide_half_move(s) / ((1.0f / s - 1.0f) * 320.0f)) < 1e-3f);
    }
    /* where the backdrop is drawn once, the margin's move; none at 4:3 or
     * across */
    CHECK(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_LEFT, s14, 0, 320.0f)
          == nv2a_wide_hud_shift(NV2A_WIDE_HUD_LEFT, s14, 0, 320.0f));
    CHECK(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_LEFT, s14, 0, 320.0f) != 0.0f);
    CHECK(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_LEFT, 1.0f, 0, 320.0f) == 0.0f);
    CHECK(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_ACROSS, s169, 0, 320.0f) == 0.0f);
    CHECK(nv2a_wide_hud_grid_shift(NV2A_WIDE_HUD_LEFT, NAN, 0, 320.0f) == 0.0f);
}

/* The pause map's yellow lines (capture-5: 108 vertices, x and z -213.3 to
 * 213.3, y -70.4 to 77.3 as they come in; the map's own models under 70). */
static void map_lines(void)
{
    const float span = 426.564f;
    CHECK(nv2a_wide_lines(span));
    CHECK(nv2a_wide_lines(NV2A_WIDE_LINES_SPAN));
    CHECK(!nv2a_wide_lines(67.6f) && !nv2a_wide_lines(17.7f) && !nv2a_wide_lines(0.0f));
    CHECK(!nv2a_wide_lines(NAN) && !nv2a_wide_lines(INFINITY) && !nv2a_wide_lines(-500.0f));
    /* the lengths go out 8 times as far; the thickness stays */
    CHECK(nv2a_wide_lines_coord(213.282f, span) == 213.282f * NV2A_WIDE_LINES_LONGER);
    CHECK(nv2a_wide_lines_coord(-213.282f, span) == -213.282f * NV2A_WIDE_LINES_LONGER);
    CHECK(nv2a_wide_lines_coord(77.304f, span) == 77.304f * NV2A_WIDE_LINES_LONGER);
    CHECK(nv2a_wide_lines_coord(-70.429f, span) == -70.429f * NV2A_WIDE_LINES_LONGER);
    CHECK(nv2a_wide_lines_coord(0.164f, span) == 0.164f);
    CHECK(nv2a_wide_lines_coord(-0.5f, span) == -0.5f);
    CHECK(nv2a_wide_lines_coord(0.0f, span) == 0.0f);
    /* not the lines: nothing changes */
    CHECK(nv2a_wide_lines_coord(8.8f, 17.7f) == 8.8f);
    CHECK(nv2a_wide_lines_coord(33.7f, 64.3f) == 33.7f);
    CHECK(nv2a_wide_lines_coord(213.282f, NAN) == 213.282f);
    /* the top of the pause menus' grey bar, model 3 at y -4.689 of the
     * menu's units (10 title pixels each) */
    CHECK(NV2A_WIDE_LINES_TOP == 47u);
    CHECK(NV2A_WIDE_LINES_LONGER > 4.0f);  /* 213 out reaches past 21:9's edges turned */
}

static void fill_is(uint32_t pw, float s, uint32_t x0, uint32_t x1, uint32_t l, uint32_t r)
{
    uint32_t a = 1, b = 2, c = 3, d = 4;
    int ok = nv2a_wide_fill_cols(pw, s, &a, &b, &c, &d);
    if (!ok || a != x0 || b != x1 || c != l || d != r)
        printf("widescreen: fill of %u at %.4f gave %d %u..%u from %u and %u, not %u..%u "
               "from %u and %u\n", pw, (double)s, ok, a, b, c, d, x0, x1, l, r);
    CHECK(ok && a == x0 && b == x1 && c == l && d == r);
}

static void fill(void)
{
    static const float art[4] = { -1.0f, 1.0f, -1.0f, 1.0f };   /* the whole screen */
    static const float tall[4] = { 0.2f, 0.6f, -1.0f, 1.0f };   /* as tall, narrow */
    static const float small[4] = { -0.5f, 0.5f, -0.5f, 0.5f }; /* an overlay */
    static const float shapes[] = { 1.4f, 1.5f, 1.6f, 16.0f / 9.0f, 2.0f, 64.0f / 27.0f,
                                     32.0f / 9.0f };
    uint32_t a = 7, b = 8, c = 9, d = 10, sc;
    unsigned i;
    CHECK(nv2a_wide_fill_fits(art, 1, 0));          /* art: fitted */
    CHECK(nv2a_wide_fill_fits(tall, 1, 0));
    CHECK(!nv2a_wide_fill_fits(art, 0, 0));         /* a fade, a bar: full width */
    CHECK(!nv2a_wide_fill_fits(art, 1, 1));         /* a copy of the frame */
    CHECK(!nv2a_wide_fill_fits(tall, 0, 0));
    CHECK(nv2a_wide_fill_fits(small, 0, 0));        /* an overlay is, as ever */
    CHECK(nv2a_wide_fill_fits(small, 1, 1));
    fill_is(1707, 0.75f, 214, 1493, 216, 1490);     /* 2x, 16:9 */
    fill_is(853, 0.75f, 107, 746, 108, 744);        /* 1x */
    fill_is(1536, (4.0f / 3.0f) / 1.6f, 128, 1408, 130, 1405);   /* 2x, 16:10 */
    fill_is(2276, 0.5625f, 498, 1778, 500, 1775);   /* 2x, 21:9 */
    for (i = 0; i < sizeof shapes / sizeof shapes[0]; i++)
        for (sc = 1; sc <= 8; sc++) {
            float s = nv2a_wide_squeeze_of(shapes[i]);
            uint32_t pw, ph, x0, x1, l, r;
            float shape;
            double e0, e1, in;
            nv2a_wide_raster(640, 480, sc, shapes[i], &pw, &ph, &shape);
            CHECK(nv2a_wide_fill_cols(pw, s, &x0, &x1, &l, &r));
            e0 = 0.5 * pw * (1.0 - s); e1 = 0.5 * pw * (1.0 + s);   /* the 4:3 edges */
            CHECK(x0 >= e0 - 1e-3 && x0 < e0 + 1.0);   /* the first whole column */
            CHECK(x1 <= e1 + 1e-3 && x1 > e1 - 1.0);   /* one past the last */
            CHECK(x0 > 0 && x1 < pw);                   /* both sides filled */
            in = floor(pw * (double)s / 640.0 + 0.5);
            CHECK(l - x0 == (uint32_t)(in < 1.0 ? 1.0 : in));
            CHECK(x1 - 1u - r == l - x0);
            CHECK(l < r && r < x1);
        }
    CHECK(!nv2a_wide_fill_cols(1280, 1.0f, &a, &b, &c, &d));      /* 4:3 */
    CHECK(!nv2a_wide_fill_cols(1707, 0.0f, &a, &b, &c, &d));
    CHECK(!nv2a_wide_fill_cols(1707, 1.5f, &a, &b, &c, &d));
    CHECK(!nv2a_wide_fill_cols(1707, NAN, &a, &b, &c, &d));
    CHECK(!nv2a_wide_fill_cols(3, 0.75f, &a, &b, &c, &d));        /* too narrow */
    CHECK(!nv2a_wide_fill_cols(0, 0.75f, &a, &b, &c, &d));
    CHECK(a == 7 && b == 8 && c == 9 && d == 10);                  /* untouched */
}

/* A strip hidden at the top of the picture: its columns from the screen's
 * left edge to width title pixels into the 4:3 picture, every one it
 * touches, and the picture's top six title rows, the row under them their
 * colour. */
static void fill_strips(void)
{
    static const float shapes[] = { 1.4f, 1.6f, 16.0f / 9.0f, 64.0f / 27.0f, 32.0f / 9.0f };
    uint32_t x1 = 1, r0 = 2, r1 = 3, sc;
    unsigned i;
    /* 1x, 16:9: the 4:3 screen from 107 (fill_is, above); rows 0 to 6 */
    CHECK(nv2a_wide_fill_strip(853, 480, 0.75f, 8, &x1, &r0, &r1));
    CHECK(x1 == 115 && r0 == 0 && r1 == 6);
    CHECK(nv2a_wide_fill_strip(853, 480, 0.75f, 0, &x1, &r0, &r1) && x1 == 107);  /* the side */
    /* 2x: from 214, two raster pixels a title pixel; rows 0 to 12 */
    CHECK(nv2a_wide_fill_strip(1707, 960, 0.75f, 8, &x1, &r0, &r1));
    CHECK(x1 == 230 && r0 == 0 && r1 == 12);
    CHECK(nv2a_wide_fill_strip(1707, 960, 0.75f, 121, &x1, &r0, &r1) && x1 == 456);
    /* 21:9 at 2.25 raster pixels a title pixel (a 2559x1080 window): every
     * one it touches */
    CHECK(nv2a_wide_fill_strip(2559, 1080, nv2a_wide_squeeze_of(2559.0f / 1080.0f), 121,
                               &x1, &r0, &r1));
    CHECK(x1 == 560 + 273 && r0 == 0 && r1 == 14);
    for (i = 0; i < sizeof shapes / sizeof shapes[0]; i++)
        for (sc = 1; sc <= 8; sc++) {
            float s = nv2a_wide_squeeze_of(shapes[i]), shape;
            uint32_t pw, ph, a, b, l, r;
            nv2a_wide_raster(640, 480, sc, shapes[i], &pw, &ph, &shape);
            CHECK(nv2a_wide_fill_cols(pw, s, &a, &b, &l, &r));
            CHECK(nv2a_wide_fill_strip(pw, ph, s, 8, &x1, &r0, &r1));
            CHECK(x1 == a + 8u * sc && r0 == 0 && r1 == 6u * sc && r1 < ph);
            CHECK(nv2a_wide_fill_strip(pw, ph, s, 0, &x1, &r0, &r1) && x1 == a);
            CHECK(nv2a_wide_fill_strip(pw, ph, s, 255, &x1, &r0, &r1) && x1 == a + 255u * sc);
        }
    x1 = 1; r0 = 2; r1 = 3;
    CHECK(!nv2a_wide_fill_strip(1280, 960, 1.0f, 8, &x1, &r0, &r1));       /* 4:3 */
    CHECK(!nv2a_wide_fill_strip(1707, 960, NAN, 8, &x1, &r0, &r1));
    CHECK(!nv2a_wide_fill_strip(3, 960, 0.75f, 8, &x1, &r0, &r1));
    CHECK(!nv2a_wide_fill_strip(1707, 0, 0.75f, 8, &x1, &r0, &r1));
    CHECK(!nv2a_wide_fill_strip(853, 480, 0.75f, 640, &x1, &r0, &r1));     /* the whole 4:3 */
    CHECK(!nv2a_wide_fill_strip(853, 1, 0.75f, 8, &x1, &r0, &r1));         /* no row under */
    CHECK(x1 == 1 && r0 == 2 && r1 == 3);                                   /* untouched */
}

/* A bar of a part whose bars go across: a plain rectangle much taller than
 * wide, and nothing else. */
static void bars(void)
{
    CHECK(nv2a_wide_bar(6, 0.112f, 59.0f));         /* the title's bars, as they come in */
    CHECK(nv2a_wide_bar(6, 4.028f, 59.0f));
    CHECK(nv2a_wide_bar(4, 1.0f, 8.0f));            /* just tall enough */
    CHECK(nv2a_wide_bar(6, 0.0f, 1.0f));            /* no width at all */
    CHECK(!nv2a_wide_bar(6, 1.0f, 7.9f));           /* not tall enough */
    CHECK(!nv2a_wide_bar(6, 1.324f, 1.244f));       /* a piece of the logo */
    CHECK(!nv2a_wide_bar(6, 13.874f, 1.244f));      /* wide, not tall */
    CHECK(!nv2a_wide_bar(7, 0.1f, 59.0f));          /* more than a rectangle */
    CHECK(!nv2a_wide_bar(243, 5.766f, 1.258f));     /* a letter */
    CHECK(!nv2a_wide_bar(2, 0.1f, 59.0f));          /* not a polygon */
    CHECK(!nv2a_wide_bar(6, 0.1f, 0.0f));           /* no height */
    CHECK(!nv2a_wide_bar(6, -1.0f, 59.0f));
    CHECK(!nv2a_wide_bar(6, NAN, 59.0f));
    CHECK(!nv2a_wide_bar(6, 0.1f, NAN));
    CHECK(!nv2a_wide_bar(6, 0.1f, INFINITY));
}

/* The side fill drawn draw by draw: the middle and each side's columns, the
 * source a side's copy shows, and the map that holds that source's centre
 * still and brings the whole side to within half a pixel of it. */
static void fill_copies(void)
{
    static const float shapes[] = { 1.4f, 1.6f, 16.0f / 9.0f, 64.0f / 27.0f, 32.0f / 9.0f };
    static const int others[] = { NV2A_WIDE_HUD_ACROSS, NV2A_WIDE_HUD_WHOLE,
                                  NV2A_WIDE_HUD_LEFT_HALF, NV2A_WIDE_HUD_RIGHT_HALF,
                                  NV2A_WIDE_HUD_FILL, NV2A_WIDE_HUD_OVER, NV2A_WIDE_HUD_BARS,
                                  NV2A_WIDE_HUD_STRIP, 10 };
    uint32_t x0 = 1, x1 = 2, src = 3, sc;
    float k = 4.0f, off = 5.0f;
    unsigned i;
    /* 2x, 16:9: the sources 216 and 1490 (fill_is, above). */
    CHECK(nv2a_wide_fill_copy(1707, 0.75f, NV2A_WIDE_HUD_NONE, &x0, &x1, &src, &k, &off));
    CHECK(x0 == 216 && x1 == 1491 && src == 1491 && k == 1.0f && off == 0.0f);
    CHECK(nv2a_wide_fill_copy(1707, 0.75f, NV2A_WIDE_HUD_LEFT, &x0, &x1, &src, &k, &off));
    CHECK(x0 == 0 && x1 == 216 && src == 216 && k == 3414.0f);
    CHECK(nv2a_wide_fill_copy(1707, 0.75f, NV2A_WIDE_HUD_RIGHT, &x0, &x1, &src, &k, &off));
    CHECK(x0 == 1491 && x1 == 1707 && src == 1490 && k == 3414.0f);
    for (i = 0; i < sizeof shapes / sizeof shapes[0]; i++)
        for (sc = 1; sc <= 8; sc++) {
            float s = nv2a_wide_squeeze_of(shapes[i]), shape;
            uint32_t pw, ph, a, b, l, r;
            int side;
            nv2a_wide_raster(640, 480, sc, shapes[i], &pw, &ph, &shape);
            CHECK(nv2a_wide_fill_cols(pw, s, &a, &b, &l, &r));
            CHECK(nv2a_wide_fill_copy(pw, s, NV2A_WIDE_HUD_NONE, &x0, &x1, &src, &k, &off));
            CHECK(x0 == l && x1 == r + 1u);             /* source to source */
            for (side = NV2A_WIDE_HUD_LEFT; side <= NV2A_WIDE_HUD_RIGHT; side += 2) {
                double c, e0, e1;
                CHECK(nv2a_wide_fill_copy(pw, s, side, &x0, &x1, &src, &k, &off));
                CHECK(side < 0 ? x0 == 0 && x1 == l && src == l
                               : x0 == r + 1u && x1 == pw && src == r);
                /* The source's centre, in NDC, stays where it is... */
                c = ((double)src + 0.5) * 2.0 / pw - 1.0;
                CHECK(fabs(k * c + off - c) < 1e-6 * k);
                /* ...and the side's far pixels show what is within half a
                 * pixel of it: x = (x' - off) / k. */
                e0 = ((double)x0 * 2.0 / pw - 1.0 - off) / k;
                e1 = ((double)x1 * 2.0 / pw - 1.0 - off) / k;
                CHECK(fabs(e0 - c) <= 1.0 / pw && fabs(e1 - c) <= 1.0 / pw);
            }
        }
    x0 = 1; x1 = 2; src = 3; k = 4.0f; off = 5.0f;
    for (i = 0; i < sizeof others / sizeof others[0]; i++)
        CHECK(!nv2a_wide_fill_copy(1707, 0.75f, others[i], &x0, &x1, &src, &k, &off));
    CHECK(!nv2a_wide_fill_copy(1280, 1.0f, NV2A_WIDE_HUD_LEFT, &x0, &x1, &src, &k, &off));
    CHECK(!nv2a_wide_fill_copy(1707, NAN, NV2A_WIDE_HUD_NONE, &x0, &x1, &src, &k, &off));
    CHECK(!nv2a_wide_fill_copy(3, 0.75f, NV2A_WIDE_HUD_RIGHT, &x0, &x1, &src, &k, &off));
    CHECK(x0 == 1 && x1 == 2 && src == 3 && k == 4.0f && off == 5.0f);    /* untouched */
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
    f10();
    hud_marks();
    hud_move();
    hud_clip();
    hud_view();
    split();
    across();
    halves();
    half_moves();
    grid_slabs_and_pieces();
    map_lines();
    fill();
    fill_copies();
    fill_strips();
    bars();
    if (s_failures) {
        printf("widescreen: %d failed\n", s_failures);
        return 1;
    }
    printf("widescreen: all cases pass\n");
    return 0;
}
