/*
 * Widescreen's maths: the squeeze, the title's 2D overlays, their clip, and
 * a framed 3D view (nv2a_wide.h).
 */
#include "nv2a_wide.h"
#include "nv2a_scale.h"

#include <math.h>

/* A viewport scale smaller than this is taken as none. */
#define WIDE_VP_SCALE_MIN 1e-6f

float nv2a_wide_clamp(float a)
{
    if (!(a >= NV2A_WIDE_16_9)) a = NV2A_WIDE_16_9;     /* NaN too */
    if (a > NV2A_WIDE_MAX) a = NV2A_WIDE_MAX;
    return a;
}

float nv2a_wide_squeeze_of(float a)
{
    return (a == NV2A_WIDE_16_9) ? 0.75f : 4.0f / (3.0f * a);
}

int nv2a_wide_frame_sized(uint32_t w, uint32_t h)
{
    return w == NV2A_SCALE_FRAME_W && h == NV2A_SCALE_FRAME_H;
}

void nv2a_wide_extent_ndc(float ext[4], const float vp_off[2], const float vp_scale[2])
{
    float sx = fabsf(vp_scale[0]) > WIDE_VP_SCALE_MIN ? fabsf(vp_scale[0])
                                                       : 0.5f * (float)NV2A_SCALE_FRAME_W;
    float sy = fabsf(vp_scale[1]) > WIDE_VP_SCALE_MIN ? fabsf(vp_scale[1])
                                                       : 0.5f * (float)NV2A_SCALE_FRAME_H;
    ext[0] = (ext[0] - vp_off[0]) / sx; ext[1] = (ext[1] - vp_off[0]) / sx;
    ext[2] = (ext[2] - vp_off[1]) / sy; ext[3] = (ext[3] - vp_off[1]) / sy;
}

int nv2a_wide_overlay(const float ext[4])
{
    return ext[1] - ext[0] < NV2A_WIDE_OVERLAY_SPAN && ext[3] - ext[2] < NV2A_WIDE_OVERLAY_SPAN;
}

void nv2a_wide_clip_x(uint32_t surf_w, float s, uint32_t *x0, uint32_t *x1)
{
    float c = 0.5f * (float)surf_w;
    float a = c + s * ((float)*x0 - c), b = c + s * ((float)*x1 - c);
    *x0 = a > 0.0f ? (uint32_t)floorf(a) : 0u;
    *x1 = b > 0.0f ? (uint32_t)ceilf(b) : 0u;
}

float nv2a_wide_shift(uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1, float s)
{
    if (x1 > x0 && y1 > y0 && x1 - x0 + 1 < NV2A_WIDE_FRAMED_MAX_W)
        return (1.0f - s) * (0.5f * (float)(x0 + x1 + 1) - 0.5f * (float)NV2A_SCALE_FRAME_W);
    return 0.0f;
}
