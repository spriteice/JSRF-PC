/*
 * The internal resolution (nv2a_scale.c): how many times the title's own
 * surface size the renderer draws at, as RECOMP_GL_SCALE, F9's box and the
 * in-game menu's Display > Render scale have it. gl_scale() in nv2a_gl.c
 * reads it once, at start-up. And where the frame it makes goes in the
 * window.
 *
 * Plain C, with nothing of GL's or SDL's, so a test links it
 * (tests/render_scale).
 */
#ifndef NV2A_SCALE_H
#define NV2A_SCALE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The scales there are, 1x to this, on every system: F9's box and the menu
 * offer them all, the launchers take them all, and RECOMP_GL_SCALE is
 * taken at most at this. 6x makes 3840x2880 of the Xbox's 640x480: a 4K
 * screen's width, one pixel for one. */
#define NV2A_SCALE_MAX 6

/* The Xbox's frame, which the names count from. */
#define NV2A_SCALE_FRAME_W 640
#define NV2A_SCALE_FRAME_H 480

/* Each scale's name, 1x first, as F9's buttons and the menu's choices show
 * it: the scale, and the raster it makes of the Xbox's 640x480. */
extern const char *const nv2a_scale_names[NV2A_SCALE_MAX];

/* RECOMP_GL_SCALE's value as the renderer takes it: 1 when it is unset or
 * not a number, and otherwise clamped to 1..NV2A_SCALE_MAX. */
uint32_t nv2a_scale_parse(const char *v);

/* Where a frame of shape fw x fh (both above 0) goes in a ww x wh window: as
 * large as it fits whole, its shape kept, between black bars. *dw x *dh is
 * its size there; it is centred. */
void nv2a_scale_fit(int fw, int fh, int ww, int wh, int *dw, int *dh);

/* How many times a frame `from` pixels long is halved along one axis before
 * it is shown `to` pixels long: while it is more than twice that, as
 * nv2a_fit.c does. 0 when `to` is below 1. */
int nv2a_scale_halvings(int from, int to);

#ifdef __cplusplus
}
#endif

#endif
