/*
 * Widescreen's maths (nv2a_wide.c): the shapes Display > Aspect ratio
 * offers and how they are saved, how much the Xbox's 4:3 frame squeezes a
 * wider view, which draws are the title's 2D overlays and are squeezed with
 * it, and where their window clip, a framed 3D view and the HUD (moved to
 * the screen's edges) go then; and the raster a widened frame is drawn at,
 * so that neither axis is stretched when it is shown, and where it goes in
 * the window. The state (whether widescreen is on, the shape, the scene
 * the game side widened, the HUD piece being drawn) stays in nv2a_gl.c,
 * which asks this.
 *
 * Plain C, with nothing of GL's or SDL's, so a test links it
 * (tests/widescreen).
 */
#ifndef NV2A_WIDE_H
#define NV2A_WIDE_H

#include <stddef.h>
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

/* The shape F10 goes to from shape (4:3 when widescreen is off): 4:3 from
 * any other, and Match screen from 4:3, so the one key goes between the
 * Xbox's shape and the screen's own. */
int nv2a_wide_f10(int shape);

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

/* The HUD at the screen's edges. The title places each HUD piece for its
 * 4:3 screen, so squeezed it sits in the middle 4:3 with the rest of the 2D.
 * The game side (port/widescreen_hud.c) marks the draws of each HUD piece,
 * in the command stream and so in order with them, with the side of the
 * screen the piece is on, and the renderer moves every draw between the
 * marks, 2D and 3D, to that side's edge, with its clip. The mark is this
 * method, past the highest the title's D3D writes (0x1EA4) and outside
 * every range it works one out in. Its value is NV2A_WIDE_HUD_TAG with the
 * piece and the side, so that anything else that arrives on the method is
 * told apart and left alone. */
#define NV2A_WIDE_HUD_METHOD 0x1FF0u
#define NV2A_WIDE_HUD_TAG    0x48550000u   /* "HU" */

/* A HUD piece's side; NONE is the end of its draws. */
enum { NV2A_WIDE_HUD_LEFT = -1, NV2A_WIDE_HUD_NONE = 0, NV2A_WIDE_HUD_RIGHT = 1 };

/* The mark's value for piece (0 to 255, which the log names) and side. */
uint32_t nv2a_wide_hud_mark(unsigned piece, int side);

/* A value read on NV2A_WIDE_HUD_METHOD as a mark: 1, with its piece and
 * side, or 0 for a value that isn't one. */
int nv2a_wide_hud_read(uint32_t value, unsigned *piece, int *side);

/* How far a draw between the marks of a piece on side moves to that side's
 * edge, as the wideShift the shader takes off its x (the title's pixels): a
 * squeezed overlay (squeezed 1) by (1 - s) / s of the half width in its own
 * pixels, and a 3D piece by (1 - s) of it, so either moves (1 - s) of the
 * half width on screen, where the squeeze left that much between the 4:3
 * edge and the screen's. 0 for no side and at s 1 (4:3). */
float nv2a_wide_hud_shift(int side, float s, int squeezed);

/* That draw's window clip, columns *x0 to *x1 (the far one exclusive) on a
 * surface surf_w wide: squeezed about the centre as nv2a_wide_clip_x does,
 * then moved with the piece, so the screen's own edge takes the 4:3 edge's
 * place. The near edge down to a whole pixel, the far one up, and both
 * inside 0 to surf_w. */
void nv2a_wide_hud_clip_x(uint32_t surf_w, float s, int side, uint32_t *x0, uint32_t *x1);

/* Whether a screen-space draw's viewport, of scale vp_scale_x (half its
 * width), spans the whole frame, as one player's HUD does: as wide as
 * NV2A_WIDE_FRAMED_MAX_W at least, or with no scale (the whole surface). A
 * split screen's HUD is drawn in its player's narrower viewport, and isn't
 * moved. */
int nv2a_wide_hud_whole_view(float vp_scale_x);

/* The raster a surface w x h is drawn at, at render scale sc, in the
 * device's pixels (*pw x *ph), and the shape it is shown at (*shape, width
 * over height, or 0 for pw:ph). The Xbox's frame in a scene widened to
 * shape a (wider than 4:3) is as many pixels across as a makes of sc times
 * its height, to the nearest -- 1707x960 at 2x and 16:9 -- so that a pixel
 * of it is as wide as it is tall once shown, and is shown at a itself.
 * Anything else, and the frame when a is 4:3 or less (or 0), is sc times
 * its size and its own shape. The title sees none of it: everything it
 * can observe stays w x h. */
void nv2a_wide_raster(uint32_t w, uint32_t h, uint32_t sc, float a,
                      uint32_t *pw, uint32_t *ph, float *shape);

/* Where a frame of shape a (width over height, above 0) goes in a ww x wh
 * window: as large as it fits whole, between black bars, the side the bars
 * are on to the nearest pixel and never past the window -- so a frame of
 * the window's own shape fills it, whatever its raster's width was rounded
 * to. *dw x *dh is its size there; it is centred. */
void nv2a_wide_fit(float a, int ww, int wh, int *dw, int *dh);

/* Render scale sc's name for the frame it makes at shape a: "3x  2560x1440"
 * at 16:9, and as nv2a_scale_names has it ("3x  1920x1440") at 4:3. */
void nv2a_wide_scale_name(char *buf, size_t n, uint32_t sc, float a);

#ifdef __cplusplus
}
#endif

#endif
