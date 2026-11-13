/*
 * HUD scale's maths: its steps, hudscale=, and where a HUD piece is drawn at
 * another size (nv2a_hud_scale.h).
 */
#include "nv2a_hud_scale.h"
#include "nv2a_scale.h"
#include "nv2a_wide.h"

#include <math.h>
#include <stddef.h>

/* How far outside 0.5 to 1.5 a number may be and still be in it: far less
 * than a step, far more than what a double's error leaves after the menu's
 * steps (0.5 + 3 x 0.1 is 0.8000000000000002). */
#define HUD_PARSE_SLACK 1e-6

int nv2a_hud_parse(double v)
{
    double t = v * 10.0;
    if (!(t >= NV2A_HUD_TENTHS_MIN - HUD_PARSE_SLACK * 10.0)
        || !(t <= NV2A_HUD_TENTHS_MAX + HUD_PARSE_SLACK * 10.0))
        return -1;                      /* NaN and the infinities too */
    t = floor(t + 0.5);
    return t < NV2A_HUD_TENTHS_MIN ? NV2A_HUD_TENTHS_MIN
         : t > NV2A_HUD_TENTHS_MAX ? NV2A_HUD_TENTHS_MAX : (int)t;
}

float nv2a_hud_size(int tenths)
{
    return tenths >= NV2A_HUD_TENTHS_MIN && tenths <= NV2A_HUD_TENTHS_MAX
         ? (float)tenths / 10.0f : 1.0f;
}

void nv2a_hud_anchor(int side, int low, float s, float *ax, float *ay)
{
    float mx, my;
    if (side != NV2A_WIDE_HUD_LEFT && side != NV2A_WIDE_HUD_RIGHT) {
        *ax = 0.0f;
        *ay = 0.0f;
        return;
    }
    if (!(s > 0.0f) || s > 1.0f) s = 1.0f;
    mx = s * 2.0f * NV2A_HUD_MARGIN_X / (float)NV2A_SCALE_FRAME_W;
    my = 2.0f * NV2A_HUD_MARGIN_Y / (float)NV2A_SCALE_FRAME_H;
    *ax = side < 0 ? -1.0f + mx : 1.0f - mx;
    *ay = low ? -1.0f + my : 1.0f - my;
}

/* How far past a whole pixel a clip's edge must land to be taken past it:
 * more than a float's error in the anchor's pixels at any render scale
 * (440 comes out as 439.99997), far less than a pixel. */
#define HUD_CLIP_SLACK (1.0f / 256.0f)

/* One edge of a clip, at c, sized k times about a (the title's pixels), and
 * put inside 0 to n: down to a whole pixel for a near edge, up for a far one. */
static uint32_t hud_clip_edge(uint32_t c, float a, float k, uint32_t n, int far_edge)
{
    float v = a + ((float)c - a) * k;
    v = far_edge ? ceilf(v - HUD_CLIP_SLACK) : floorf(v + HUD_CLIP_SLACK);
    return v > 0.0f ? (v < (float)n ? (uint32_t)v : n) : 0u;
}

void nv2a_hud_clip(uint32_t w, uint32_t h, float k, float ax, float ay,
                   uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1)
{
    float px, py;
    if (k == 1.0f || !(k > 0.0f)) return;
    px = (ax + 1.0f) * 0.5f * (float)w;     /* NDC to the title's pixels, y down */
    py = (1.0f - ay) * 0.5f * (float)h;
    *x0 = hud_clip_edge(*x0, px, k, w, 0);
    *x1 = hud_clip_edge(*x1, px, k, w, 1);
    *y0 = hud_clip_edge(*y0, py, k, h, 0);
    *y1 = hud_clip_edge(*y1, py, k, h, 1);
}
