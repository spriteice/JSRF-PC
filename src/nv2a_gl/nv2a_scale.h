/*
 * The internal resolution (nv2a_scale.c): how many times the title's own
 * surface size the renderer draws at, as RECOMP_GL_SCALE, F9's box and the
 * in-game menu's Display > Render scale have it. gl_scale() in nv2a_gl.c
 * reads it once, at start-up.
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

/* The scales F9's box and the menu offer: 1x to this. */
#define NV2A_SCALE_OFFERED 4

/* The most RECOMP_GL_SCALE is taken at; a larger value is taken as this. */
#define NV2A_SCALE_ENV_MAX 8

/* Each offered scale's name, 1x first, as F9's buttons and the menu's
 * choices show it: the scale, and the raster it makes of the Xbox's
 * 640x480. */
extern const char *const nv2a_scale_names[NV2A_SCALE_OFFERED];

/* RECOMP_GL_SCALE's value as the renderer takes it: 1 when it is unset or
 * not a number, and otherwise clamped to 1..NV2A_SCALE_ENV_MAX. */
uint32_t nv2a_scale_parse(const char *v);

#ifdef __cplusplus
}
#endif

#endif
