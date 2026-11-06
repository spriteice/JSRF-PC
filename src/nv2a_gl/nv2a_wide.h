/*
 * Widescreen's maths (nv2a_wide.c): the shapes Display > Aspect ratio
 * offers and how they are saved, how much the Xbox's 4:3 frame squeezes a
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

/* Shapes, width over height: the Xbox's own, and 16:9, widescreen's
 * shape when nothing else is chosen. */
#define NV2A_WIDE_4_3  (4.0f / 3.0f)
#define NV2A_WIDE_16_9 (16.0f / 9.0f)

/* The shapes Display > Aspect ratio offers, in its order. 4:3 is widescreen
 * off; each of the others is a shape widescreen can be on at. 21:9 is 64:27,
 * the shape of 2560x1080 and 5120x2160 screens, as HDMI has it. Match screen
 * is the shape of the display the window is on. */
enum {
    NV2A_WIDE_SHAPE_4_3,
    NV2A_WIDE_SHAPE_16_10,
    NV2A_WIDE_SHAPE_16_9,
    NV2A_WIDE_SHAPE_21_9,
    NV2A_WIDE_SHAPE_MATCH,
    NV2A_WIDE_SHAPES
};

/* Each shape's name, as the menu's choices show it. */
extern const char *const nv2a_wide_shape_names[NV2A_WIDE_SHAPES];

/* A wide shape as the settings file keeps it, aspect= ("16:10", "16:9",
 * "21:9", "match"), and NULL for 4:3 and anything not a shape. */
const char *nv2a_wide_shape_key(int shape);

/* aspect='s value (its line end left out or not) as a wide shape, and -1
 * for anything else, 4:3 among them: widescreen on or off is widescreen=. */
int nv2a_wide_shape_parse(const char *v);

/* A shape's width over height. Match screen's is the display's, w x h,
 * taken as nv2a_wide_clamp does, and 16:9 when the display's size isn't
 * known (either is below 1). */
float nv2a_wide_shape_aspect(int shape, int disp_w, int disp_h);

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

/* The shape a = width / height as widescreen takes it: 4:3 at the least,
 * and 4:3 for NaN. As wide as it is otherwise, on every system: a 32:9
 * screen's Match screen is 32:9. */
float nv2a_wide_clamp(float a);

/* s, how much the 4:3 frame squeezes a view of shape a: (4/3) / a, exactly
 * 0.75 at 16:9, so that 16:9's picture is what it always was, and 1 (no
 * squeeze) at 4:3 and below. */
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
