/*
 * Widescreen's maths: the shapes, the squeeze, the title's 2D overlays,
 * their clip, a framed 3D view, the HUD's marks and its move to the
 * screen's edges, and a widened frame's raster and place in the window
 * (nv2a_wide.h).
 */
#include "nv2a_wide.h"
#include "nv2a_scale.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* A viewport scale smaller than this is taken as none. */
#define WIDE_VP_SCALE_MIN 1e-6f

const char *const nv2a_wide_shape_names[NV2A_WIDE_SHAPES] = {
    "4:3", "16:10", "16:9", "21:9", "Match screen" };

/* Each wide shape's aspect= key, by shape; 4:3 has none. */
static const char *const k_shape_keys[NV2A_WIDE_SHAPES] = {
    NULL, "16:10", "16:9", "21:9", "match" };

const char *nv2a_wide_shape_key(int shape)
{
    return shape >= 0 && shape < NV2A_WIDE_SHAPES ? k_shape_keys[shape] : NULL;
}

int nv2a_wide_shape_parse(const char *v)
{
    int i;
    size_t n;
    if (!v) return -1;
    n = strcspn(v, "\r\n");
    for (i = 0; i < NV2A_WIDE_SHAPES; i++)
        if (k_shape_keys[i] && strlen(k_shape_keys[i]) == n && !strncmp(v, k_shape_keys[i], n))
            return i;
    return -1;
}

float nv2a_wide_shape_aspect(int shape, int disp_w, int disp_h)
{
    switch (shape) {
    case NV2A_WIDE_SHAPE_16_10: return 16.0f / 10.0f;
    case NV2A_WIDE_SHAPE_16_9:  return NV2A_WIDE_16_9;
    case NV2A_WIDE_SHAPE_21_9:  return 64.0f / 27.0f;
    case NV2A_WIDE_SHAPE_MATCH:
        if (disp_w < 1 || disp_h < 1) return NV2A_WIDE_16_9;
        return nv2a_wide_clamp((float)disp_w / (float)disp_h);
    default:                    return NV2A_WIDE_4_3;
    }
}

int nv2a_wide_f10(int shape)
{
    return shape == NV2A_WIDE_SHAPE_4_3 ? NV2A_WIDE_SHAPE_MATCH : NV2A_WIDE_SHAPE_4_3;
}

float nv2a_wide_clamp(float a)
{
    return a >= NV2A_WIDE_4_3 ? a : NV2A_WIDE_4_3;      /* NaN too */
}

float nv2a_wide_squeeze_of(float a)
{
    if (!(a > NV2A_WIDE_4_3)) return 1.0f;
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

/* A mark's side in its value's low byte. */
enum { WIDE_HUD_CODE_NONE, WIDE_HUD_CODE_LEFT, WIDE_HUD_CODE_RIGHT };

uint32_t nv2a_wide_hud_mark(unsigned piece, int side)
{
    uint32_t code = side < 0 ? WIDE_HUD_CODE_LEFT : side > 0 ? WIDE_HUD_CODE_RIGHT
                                                             : WIDE_HUD_CODE_NONE;
    return NV2A_WIDE_HUD_TAG | ((uint32_t)(piece & 0xFFu) << 8) | code;
}

int nv2a_wide_hud_read(uint32_t value, unsigned *piece, int *side)
{
    uint32_t code = value & 0xFFu;
    if ((value & 0xFFFF0000u) != NV2A_WIDE_HUD_TAG || code > WIDE_HUD_CODE_RIGHT) return 0;
    *piece = (value >> 8) & 0xFFu;
    *side = code == WIDE_HUD_CODE_LEFT ? NV2A_WIDE_HUD_LEFT
          : code == WIDE_HUD_CODE_RIGHT ? NV2A_WIDE_HUD_RIGHT : NV2A_WIDE_HUD_NONE;
    return 1;
}

float nv2a_wide_hud_shift(int side, float s, int squeezed)
{
    float move;
    if (side == NV2A_WIDE_HUD_NONE || !(s > 0.0f) || !(s < 1.0f)) return 0.0f;
    move = (1.0f - s) * 0.5f * (float)NV2A_SCALE_FRAME_W;
    if (squeezed) move /= s;
    return side < 0 ? move : -move;     /* the shader takes it off x */
}

void nv2a_wide_hud_clip_x(uint32_t surf_w, float s, int side, uint32_t *x0, uint32_t *x1)
{
    float c = 0.5f * (float)surf_w, m, a, b;
    if (!(s > 0.0f) || s > 1.0f) s = 1.0f;
    m = side < 0 ? -(1.0f - s) * c : side > 0 ? (1.0f - s) * c : 0.0f;
    a = floorf(c + s * ((float)*x0 - c) + m);
    b = ceilf(c + s * ((float)*x1 - c) + m);
    *x0 = a > 0.0f ? (a < (float)surf_w ? (uint32_t)a : surf_w) : 0u;
    *x1 = b > 0.0f ? (b < (float)surf_w ? (uint32_t)b : surf_w) : 0u;
}

int nv2a_wide_hud_whole_view(float vp_scale_x)
{
    float w = fabsf(vp_scale_x);
    return !(w > WIDE_VP_SCALE_MIN) || 2.0f * w >= (float)NV2A_WIDE_FRAMED_MAX_W;
}

void nv2a_wide_raster(uint32_t w, uint32_t h, uint32_t sc, float a,
                      uint32_t *pw, uint32_t *ph, float *shape)
{
    if (sc < 1) sc = 1;
    *pw = w * sc; *ph = h * sc; *shape = 0.0f;
    if (!nv2a_wide_frame_sized(w, h) || !(a > NV2A_WIDE_4_3)) return;
    *pw = (uint32_t)floor((double)*ph * (double)a + 0.5);
    *shape = a;
}

void nv2a_wide_fit(float a, int ww, int wh, int *dw, int *dh)
{
    double d;
    *dw = ww; *dh = wh;
    if (!(a > 0.0f) || ww < 1 || wh < 1) return;
    if ((double)ww > (double)wh * (double)a) {
        d = floor((double)wh * (double)a + 0.5);
        if (d < (double)ww) *dw = d < 1.0 ? 1 : (int)d;
    } else {
        d = floor((double)ww / (double)a + 0.5);
        if (d < (double)wh) *dh = d < 1.0 ? 1 : (int)d;
    }
}

void nv2a_wide_scale_name(char *buf, size_t n, uint32_t sc, float a)
{
    uint32_t pw, ph;
    float shape;
    nv2a_wide_raster(NV2A_SCALE_FRAME_W, NV2A_SCALE_FRAME_H, sc, a, &pw, &ph, &shape);
    snprintf(buf, n, "%ux  %ux%u", (unsigned)(sc < 1 ? 1 : sc), (unsigned)pw, (unsigned)ph);
}
