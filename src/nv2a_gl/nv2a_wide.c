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

float nv2a_wide_overlay_shift(float centre, float half, float s)
{
    const float w = (float)NV2A_SCALE_FRAME_W;
    /* A pixel's slack at each side of the frame; every comparison false for
     * NaN. */
    if (!(s > 0.0f) || !(s < 1.0f) || !(half > WIDE_VP_SCALE_MIN)
        || !(centre - half >= -1.0f) || !(centre + half <= w + 1.0f))
        return 0.0f;
    return (1.0f - s) * (centre - 0.5f * w) / s;
}

/* A mark's side in its value's low four bits, with the own-viewport flag in
 * the byte's top bit, the on-the-grid flag in the bit under it, the anchor
 * flag under that and the from-the-anchor flag under that. A sized HUD
 * piece's mark (NV2A_WIDE_HUD_SIZED_TAG) has the bottom flag in the
 * on-the-grid flag's bit, and neither anchor flag. */
enum { WIDE_HUD_CODE_NONE, WIDE_HUD_CODE_LEFT, WIDE_HUD_CODE_RIGHT, WIDE_HUD_CODE_ACROSS,
       WIDE_HUD_CODE_WHOLE, WIDE_HUD_CODE_LEFT_HALF, WIDE_HUD_CODE_RIGHT_HALF,
       WIDE_HUD_CODE_FILL, WIDE_HUD_CODE_OVER, WIDE_HUD_CODE_BARS, WIDE_HUD_CODE_STRIP,
       WIDE_HUD_CODE_LINES, WIDE_HUD_CODE_MIDDLE };
#define WIDE_HUD_CODE_OWN_VIEW    0x80u
#define WIDE_HUD_CODE_ON_GRID     0x40u
#define WIDE_HUD_CODE_ANCHOR      0x20u
#define WIDE_HUD_CODE_FROM_ANCHOR 0x10u
#define WIDE_HUD_CODE_LOW         WIDE_HUD_CODE_ON_GRID
#define WIDE_HUD_CODE_SIDE        0x0Fu

uint32_t nv2a_wide_hud_mark(unsigned piece, int side, unsigned flags)
{
    uint32_t code = side < 0 ? WIDE_HUD_CODE_LEFT : side == NV2A_WIDE_HUD_RIGHT
                  ? WIDE_HUD_CODE_RIGHT : side == NV2A_WIDE_HUD_ACROSS ? WIDE_HUD_CODE_ACROSS
                  : side == NV2A_WIDE_HUD_WHOLE ? WIDE_HUD_CODE_WHOLE
                  : side == NV2A_WIDE_HUD_LEFT_HALF ? WIDE_HUD_CODE_LEFT_HALF
                  : side == NV2A_WIDE_HUD_RIGHT_HALF ? WIDE_HUD_CODE_RIGHT_HALF
                  : side == NV2A_WIDE_HUD_FILL ? WIDE_HUD_CODE_FILL
                  : side == NV2A_WIDE_HUD_OVER ? WIDE_HUD_CODE_OVER
                  : side == NV2A_WIDE_HUD_BARS ? WIDE_HUD_CODE_BARS
                  : side == NV2A_WIDE_HUD_STRIP ? WIDE_HUD_CODE_STRIP
                  : side == NV2A_WIDE_HUD_LINES ? WIDE_HUD_CODE_LINES
                  : side == NV2A_WIDE_HUD_MIDDLE ? WIDE_HUD_CODE_MIDDLE : WIDE_HUD_CODE_NONE;
    if (flags & NV2A_WIDE_HUD_OWN_VIEW) code |= WIDE_HUD_CODE_OWN_VIEW;
    if (flags & NV2A_WIDE_HUD_SIZED) {
        if (flags & NV2A_WIDE_HUD_LOW) code |= WIDE_HUD_CODE_LOW;
        return NV2A_WIDE_HUD_SIZED_TAG | ((uint32_t)(piece & 0xFFu) << 8) | code;
    }
    if (flags & NV2A_WIDE_HUD_ON_GRID) code |= WIDE_HUD_CODE_ON_GRID;
    if (flags & NV2A_WIDE_HUD_ANCHOR) code |= WIDE_HUD_CODE_ANCHOR;
    if (flags & NV2A_WIDE_HUD_FROM_ANCHOR) code |= WIDE_HUD_CODE_FROM_ANCHOR;
    return NV2A_WIDE_HUD_TAG | ((uint32_t)(piece & 0xFFu) << 8) | code;
}

int nv2a_wide_hud_read(uint32_t value, unsigned *piece, int *side, unsigned *flags)
{
    uint32_t code = value & WIDE_HUD_CODE_SIDE;
    int sized = (value & 0xFFFF0000u) == NV2A_WIDE_HUD_SIZED_TAG;
    if (((value & 0xFFFF0000u) != NV2A_WIDE_HUD_TAG && !sized) || code > WIDE_HUD_CODE_MIDDLE)
        return 0;
    if (sized && (value & (WIDE_HUD_CODE_ANCHOR | WIDE_HUD_CODE_FROM_ANCHOR))) return 0;
    *piece = (value >> 8) & 0xFFu;
    *side = code == WIDE_HUD_CODE_LEFT ? NV2A_WIDE_HUD_LEFT
          : code == WIDE_HUD_CODE_RIGHT ? NV2A_WIDE_HUD_RIGHT
          : code == WIDE_HUD_CODE_ACROSS ? NV2A_WIDE_HUD_ACROSS
          : code == WIDE_HUD_CODE_WHOLE ? NV2A_WIDE_HUD_WHOLE
          : code == WIDE_HUD_CODE_LEFT_HALF ? NV2A_WIDE_HUD_LEFT_HALF
          : code == WIDE_HUD_CODE_RIGHT_HALF ? NV2A_WIDE_HUD_RIGHT_HALF
          : code == WIDE_HUD_CODE_FILL ? NV2A_WIDE_HUD_FILL
          : code == WIDE_HUD_CODE_OVER ? NV2A_WIDE_HUD_OVER
          : code == WIDE_HUD_CODE_BARS ? NV2A_WIDE_HUD_BARS
          : code == WIDE_HUD_CODE_STRIP ? NV2A_WIDE_HUD_STRIP
          : code == WIDE_HUD_CODE_LINES ? NV2A_WIDE_HUD_LINES
          : code == WIDE_HUD_CODE_MIDDLE ? NV2A_WIDE_HUD_MIDDLE : NV2A_WIDE_HUD_NONE;
    *flags = ((value & WIDE_HUD_CODE_OWN_VIEW) ? NV2A_WIDE_HUD_OWN_VIEW : 0u);
    if (sized)
        *flags |= NV2A_WIDE_HUD_SIZED | ((value & WIDE_HUD_CODE_LOW) ? NV2A_WIDE_HUD_LOW : 0u);
    else
        *flags |= ((value & WIDE_HUD_CODE_ON_GRID) ? NV2A_WIDE_HUD_ON_GRID : 0u)
                | ((value & WIDE_HUD_CODE_ANCHOR) ? NV2A_WIDE_HUD_ANCHOR : 0u)
                | ((value & WIDE_HUD_CODE_FROM_ANCHOR) ? NV2A_WIDE_HUD_FROM_ANCHOR : 0u);
    return 1;
}

float nv2a_wide_hud_shift(int side, float s, int squeezed, float centre)
{
    float edge, move;
    if ((side != NV2A_WIDE_HUD_LEFT && side != NV2A_WIDE_HUD_RIGHT)
        || !(s > 0.0f) || !(s < 1.0f) || centre != centre)
        return 0.0f;
    edge = side < 0 ? 0.0f : (float)NV2A_SCALE_FRAME_W;
    move = (1.0f - s) * (centre - edge);    /* the shader takes it off x */
    if (squeezed) move /= s;
    return move;
}

float nv2a_wide_hud_grid_shift(int side, float s, int squeezed, float centre)
{
    float move = nv2a_wide_hud_shift(side, s, squeezed, centre);
    float half = nv2a_wide_half_move(s);
    float parts;
    if (move == 0.0f || half == 0.0f) return move;
    parts = (1.0f / s - 1.0f) * 0.5f * (float)NV2A_SCALE_FRAME_W;
    return move * half / parts;
}

float nv2a_wide_across_stretch(float s)
{
    if (!(s > 0.0f) || !(s < 1.0f)) return 1.0f;
    return 1.0f / s;
}

int nv2a_wide_origin_x(float px, float pw, float off_x, float shift, float w, float *x)
{
    float v;
    if (pw == 0.0f || pw != pw || !(w > 0.0f)) return 0;
    v = (px / pw + off_x - shift) * 2.0f / w - 1.0f;
    if (!isfinite(v)) return 0;
    *x = v;
    return 1;
}

float nv2a_wide_anchor_off(float anchor, float across)
{
    if (anchor != anchor || across != across) return 0.0f;
    return -anchor * across;
}

float nv2a_wide_half_move(float s)
{
    float parts;
    if (!(s > 0.0f) || !(s < 1.0f)) return 0.0f;
    parts = (1.0f / s - 1.0f) * 0.5f * (float)NV2A_SCALE_FRAME_W;
    return floorf(parts / NV2A_WIDE_HALF_STEP + 0.5f) * NV2A_WIDE_HALF_STEP;
}

float nv2a_wide_half_shift(int side, float s, int squeezed)
{
    float move;
    if (side != NV2A_WIDE_HUD_LEFT_HALF && side != NV2A_WIDE_HUD_RIGHT_HALF) return 0.0f;
    move = nv2a_wide_half_move(s);
    if (move == 0.0f) return 0.0f;
    if (!squeezed) move *= s;               /* the shader takes it off x */
    return side == NV2A_WIDE_HUD_LEFT_HALF ? move : -move;
}

uint32_t nv2a_wide_half_seam(uint32_t surf_w, float s)
{
    float cell;
    if (!(s > 0.0f) || !(s < 1.0f)) return surf_w / 2u;
    cell = 0.25f * NV2A_WIDE_HALF_STEP * s * (float)surf_w / (float)NV2A_SCALE_FRAME_W;
    return surf_w / 2u - (uint32_t)floorf(cell + 0.5f);
}

void nv2a_wide_half_clip_x(uint32_t surf_w, float s, int side, uint32_t *x0, uint32_t *x1)
{
    uint32_t mid = nv2a_wide_half_seam(surf_w, s);
    if (side == NV2A_WIDE_HUD_LEFT_HALF) {
        if (*x1 > mid) *x1 = mid;
        if (*x0 > *x1) *x0 = *x1;
    } else if (side == NV2A_WIDE_HUD_RIGHT_HALF) {
        if (*x0 < mid) *x0 = mid;
        if (*x1 < *x0) *x1 = *x0;
    }
}

void nv2a_wide_hud_clip_x(uint32_t surf_w, float s, int side, uint32_t *x0, uint32_t *x1)
{
    float c = 0.5f * (float)surf_w, m, a, b;
    if (!(s > 0.0f) || s > 1.0f) s = 1.0f;
    m = side == NV2A_WIDE_HUD_LEFT ? -(1.0f - s) * c
      : side == NV2A_WIDE_HUD_RIGHT ? (1.0f - s) * c : 0.0f;
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

int nv2a_wide_fill_fits(const float ext[4], int textured, int samples_surface)
{
    return nv2a_wide_overlay(ext) || (textured && !samples_surface);
}

int nv2a_wide_fill_cols(uint32_t pw, float s, uint32_t *x0, uint32_t *x1,
                        uint32_t *src_l, uint32_t *src_r)
{
    double c = 0.5 * (double)pw, a, b, in;
    uint32_t l, r, inset;
    if (!(s > 0.0f) || !(s < 1.0f) || pw < 4u) return 0;
    /* The first column wholly inside, and one past the last, with a
     * thousandth of a pixel's slack for s's rounding: 16:10's edge at 2x is
     * column 128 itself. */
    a = ceil(c - (double)s * c - 1e-3);
    b = floor(c + (double)s * c + 1e-3);
    if (!(a >= 1.0) || !(b <= (double)pw - 1.0) || !(b > a)) return 0;
    l = (uint32_t)a; r = (uint32_t)b;
    in = floor((double)pw * (double)s / (double)NV2A_SCALE_FRAME_W + 0.5);
    inset = in >= 1.0 ? (uint32_t)in : 1u;
    if (r - l <= 2u * inset) return 0;
    *x0 = l; *x1 = r;
    *src_l = l + inset;
    *src_r = r - 1u - inset;
    return 1;
}

int nv2a_wide_fill_strip(uint32_t pw, uint32_t ph, float s, unsigned width, uint32_t *x1,
                         uint32_t *r0, uint32_t *r1)
{
    uint32_t a, b, l, r;
    double x, y;
    if (!nv2a_wide_fill_cols(pw, s, &a, &b, &l, &r) || !ph) return 0;
    /* A title pixel is ph / 480 raster pixels, across as down: the 4:3
     * picture is as tall as the raster, and its pixels are square. */
    x = (double)a + ceil((double)width * (double)ph / (double)NV2A_SCALE_FRAME_H - 1e-3);
    y = ceil((double)NV2A_WIDE_STRIP_ROWS * (double)ph / (double)NV2A_SCALE_FRAME_H - 1e-3);
    if (!(x < (double)b) || !(y < (double)ph) || !(y >= 1.0)) return 0;
    *x1 = (uint32_t)x; *r0 = 0; *r1 = (uint32_t)y;
    return 1;
}

int nv2a_wide_bar(uint32_t n, float w, float h)
{
    return n >= 3u && n <= NV2A_WIDE_BAR_VERTS && w >= 0.0f && h > 0.0f && isfinite(h)
        && h >= NV2A_WIDE_BAR_TALL * w;
}

int nv2a_wide_slab(uint32_t n, float s)
{
    return n >= NV2A_WIDE_SLAB_VERTS && nv2a_wide_half_move(s) > 0.0f;
}

int nv2a_wide_lines(float span)
{
    return span >= NV2A_WIDE_LINES_SPAN && isfinite(span);
}

float nv2a_wide_lines_coord(float c, float span)
{
    if (!nv2a_wide_lines(span) || !(fabsf(c) >= span / 8.0f)) return c;
    return c * NV2A_WIDE_LINES_LONGER;
}

int nv2a_wide_fill_copy(uint32_t pw, float s, int side, uint32_t *x0, uint32_t *x1,
                        uint32_t *src, float *k, float *off)
{
    uint32_t l, r, sl, sr, a, b, c;
    double xc;
    if (!nv2a_wide_fill_cols(pw, s, &l, &r, &sl, &sr)) return 0;
    if (side == NV2A_WIDE_HUD_NONE) {
        *x0 = sl; *x1 = sr + 1u; *src = sr + 1u; *k = 1.0f; *off = 0.0f;
        return 1;
    }
    if (side == NV2A_WIDE_HUD_LEFT)       { a = 0u; b = sl; c = sl; }
    else if (side == NV2A_WIDE_HUD_RIGHT) { a = sr + 1u; b = pw; c = sr; }
    else return 0;
    *x0 = a; *x1 = b; *src = c;
    *k = 2.0f * (float)pw;
    /* The source column's centre in NDC, held still: x' - xc = k (x - xc). */
    xc = ((double)c + 0.5) * 2.0 / (double)pw - 1.0;
    *off = (float)((1.0 - (double)*k) * xc);
    return 1;
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
