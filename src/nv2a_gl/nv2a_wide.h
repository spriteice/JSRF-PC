/*
 * Widescreen's maths (nv2a_wide.c): how much the Xbox's 4:3 frame squeezes a
 * wider view, which draws are the title's 2D overlays and are squeezed with
 * it, and where their window clip and a framed 3D view go then. The state
 * (whether widescreen is on, the shape, the scene the game side widened)
 * stays in nv2a_gl.c, which asks this.
 *
 * Plain C, with nothing of GL's or SDL's, so a test links it
 * (tests/widescreen).
 */
#ifndef NV2A_WIDE_H
#define NV2A_WIDE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 16:9, the shape widescreen has by default, width over height. */
#define NV2A_WIDE_16_9 (16.0f / 9.0f)
/* The widest shape taken. */
#define NV2A_WIDE_MAX 2.4f

/* The inputs D3D's screen-space (XYZRHW) pass-through program reads: the
 * title's 2D -- the HUD, text, menus, and its copies of whole frames --
 * comes through it. */
#define NV2A_WIDE_SCREEN_SPACE_INPUTS 0x1F9Bu

/* A draw narrower than this, in NDC (whose whole screen is 2.0), both ways,
 * is an overlay; one as wide or as tall is a backdrop, a fade, a frame. */
#define NV2A_WIDE_OVERLAY_SPAN 1.8f

/* A window clip narrower than this, in the title's pixels, is a 3D view
 * inside a 2D frame; one as wide is the scene. */
#define NV2A_WIDE_FRAMED_MAX_W 600u

/* The shape a = width / height as widescreen takes it: between 16:9 and
 * NV2A_WIDE_MAX, and 16:9 for NaN. */
float nv2a_wide_clamp(float a);

/* s, how much the 4:3 frame squeezes a view of shape a: (4/3) / a, and
 * exactly 0.75 at 16:9, so that 16:9's picture is what it always was. */
float nv2a_wide_squeeze_of(float a);

/* Whether a surface w x h is the Xbox's 640x480 frame, the one widescreen
 * widens and whose 2D it squeezes. */
int nv2a_wide_frame_sized(uint32_t w, uint32_t h);

/* A screen-space draw's extent, ext = { x low, x high, y low, y high } in
 * the units its viewport divides by, turned into NDC in place through that
 * viewport (offset and scale, x then y), whatever units those are. A scale
 * of about 0 counts as half the Xbox's frame. */
void nv2a_wide_extent_ndc(float ext[4], const float vp_off[2], const float vp_scale[2]);

/* Whether a screen-space draw of extent ext (NDC, as above) is an overlay,
 * squeezed to keep its shape: narrower than NV2A_WIDE_OVERLAY_SPAN both
 * ways. */
int nv2a_wide_overlay(const float ext[4]);

/* A window clip's columns *x0 to *x1 (the far one exclusive) on a surface
 * surf_w wide, squeezed by s about the surface's centre with what was
 * squeezed or moved: the near edge down to a whole pixel, the far one up, and
 * neither below 0. */
void nv2a_wide_clip_x(uint32_t surf_w, float s, uint32_t *x0, uint32_t *x1);

/* A 3D view inside a 2D frame -- a window clip x0..x1 by y0..y1, both
 * inclusive, narrower than NV2A_WIDE_FRAMED_MAX_W -- moved toward the centre
 * with its squeezed frame: how far, in the title's pixels. 0 for a clip that
 * is empty or as wide as the scene. */
float nv2a_wide_shift(uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1, float s);

#ifdef __cplusplus
}
#endif

#endif
