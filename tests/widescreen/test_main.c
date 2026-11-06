/* Run with: python tests/widescreen/run.py
 *
 * Widescreen's maths, src/nv2a_gl/nv2a_wide.c, as the engine builds it:
 *
 *   1. A shape is taken between 16:9 and 2.4:1, and NaN as 16:9.
 *   2. The 4:3 frame squeezes a view of shape a by (4/3) / a, and by exactly
 *      0.75 at 16:9.
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
 */
#include <math.h>
#include <stdio.h>
#include "nv2a_wide.h"

static int s_failures;

#define CHECK(c) do { if (!(c)) { printf("widescreen: FAIL %s:%d: %s\n", \
                                         __FILE__, __LINE__, #c); s_failures++; } } while (0)

static int near(float a, float b) { return fabsf(a - b) < 1e-5f; }

static void shapes(void)
{
    CHECK(nv2a_wide_clamp(NV2A_WIDE_16_9) == NV2A_WIDE_16_9);
    CHECK(nv2a_wide_clamp(2.0f) == 2.0f);
    CHECK(nv2a_wide_clamp(1.6f) == NV2A_WIDE_16_9);         /* below 16:9 */
    CHECK(nv2a_wide_clamp(4.0f / 3.0f) == NV2A_WIDE_16_9);
    CHECK(nv2a_wide_clamp(0.0f) == NV2A_WIDE_16_9);
    CHECK(nv2a_wide_clamp(-2.0f) == NV2A_WIDE_16_9);
    CHECK(nv2a_wide_clamp(NAN) == NV2A_WIDE_16_9);
    CHECK(nv2a_wide_clamp(2.4f) == 2.4f);
    CHECK(nv2a_wide_clamp(32.0f / 9.0f) == 2.4f);            /* above 2.4:1 */
    CHECK(nv2a_wide_clamp(INFINITY) == 2.4f);
}

static void squeeze(void)
{
    CHECK(nv2a_wide_squeeze_of(NV2A_WIDE_16_9) == 0.75f);    /* exactly */
    CHECK(near(nv2a_wide_squeeze_of(2.0f), 2.0f / 3.0f));
    CHECK(near(nv2a_wide_squeeze_of(2.4f), 4.0f / 7.2f));
    CHECK(near(nv2a_wide_squeeze_of(4.0f / 3.0f), 1.0f));
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

int main(void)
{
    shapes();
    squeeze();
    frame();
    ndc();
    overlay();
    clip();
    shift();
    if (s_failures) {
        printf("widescreen: %d failed\n", s_failures);
        return 1;
    }
    printf("widescreen: all cases pass\n");
    return 0;
}
