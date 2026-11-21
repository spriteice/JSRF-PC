/*
 * The internal resolution (nv2a_scale.c): how many lines the renderer draws
 * the Xbox's 480-line frame at, as Display > Resolution and F9's box choose
 * it -- the Xbox's own 480 (Default), a height from 720p to 2160p, or the
 * display's (Match screen) -- and so how large it draws every surface the
 * title draws into: that many lines over 480 times its size. Taken while
 * the game runs, at the next flip (nv2a_gl.c, res_take), and kept as
 * resolution= in the settings file. And where the frame it makes goes in
 * the window.
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

/* The choices, in Display > Resolution's order (spriteice, 01:11 and 01:38
 * UTC on 2026-11-21): the Xbox's own 640x480, named Default; six heights;
 * and the height of the display the window is on. Each is the frame's
 * height, whatever its shape: the width follows Aspect ratio. */
enum {
    NV2A_SCALE_XBOX,            /* "Default": 480 lines, the Xbox's own */
    NV2A_SCALE_720P,
    NV2A_SCALE_900P,
    NV2A_SCALE_1080P,
    NV2A_SCALE_1440P,
    NV2A_SCALE_1600P,
    NV2A_SCALE_2160P,
    NV2A_SCALE_MATCH,           /* "Match screen": the display's height */
    NV2A_SCALE_CHOICES
};

/* The choice a player gets who hasn't chosen one, which File > Presets'
 * JSRF-PC default and Max quality set too (spriteice, 01:29 UTC on
 * 2026-11-21): Match screen. Here and nowhere else. */
#define NV2A_SCALE_DEFAULT NV2A_SCALE_MATCH

/* The Xbox's frame, which the lines count from: a surface w x h is drawn
 * at nv2a_scale_px(w) x nv2a_scale_px(h). */
#define NV2A_SCALE_FRAME_W 640
#define NV2A_SCALE_FRAME_H 480

/* The fewest and the most lines a choice gives: Match screen's display
 * height is held between them. 2160, a 4K screen's height, is the most
 * any choice offers. */
#define NV2A_SCALE_LINES_MIN NV2A_SCALE_FRAME_H
#define NV2A_SCALE_LINES_MAX 2160

/* Match screen's lines while the display's height isn't known: a 1080-line
 * screen's, the commonest. */
#define NV2A_SCALE_LINES_UNKNOWN 1080

/* Each choice's name, as the menu's row and F9's buttons show it. */
extern const char *const nv2a_scale_names[NV2A_SCALE_CHOICES];

/* A choice as the settings file keeps it, resolution= ("default", "720",
 * "900", "1080", "1440", "1600", "2160", "match"), and NULL for anything
 * not a choice. */
const char *nv2a_scale_key(int choice);

/* resolution='s value (its line end left out or not) as a choice, and -1
 * for anything else. */
int nv2a_scale_parse(const char *v);

/* The most scale= from before resolution= held: 1x to this many times the
 * Xbox's 640x480. The launchers' --scale takes the same. */
#define NV2A_SCALE_OLD_MAX 6

/* The settings file's scale= from before resolution= (1x to 6x the Xbox's
 * 640x480, as the launchers, F9 and the menu kept it), or the launchers'
 * --scale, as the choice of those that gives at least as many lines, up to
 * 2160p: 1 is Default, 2 is 1080p, 3 is 1440p, and 4 to 6 are 2160p. -1
 * when v is NULL or holds no whole number from 1 to NV2A_SCALE_OLD_MAX. */
int nv2a_scale_of_old(const char *v);

/* The lines a choice gives. Match screen's is the display's height, disp_h,
 * held between NV2A_SCALE_LINES_MIN and NV2A_SCALE_LINES_MAX, and
 * NV2A_SCALE_LINES_UNKNOWN when disp_h is below 1. 0 for anything not a
 * choice. */
uint32_t nv2a_scale_lines(int choice, int disp_h);

/* A surface's side, v of the title's pixels, in the raster's at `lines`:
 * lines / 480 times it, to the nearest, and at least 1. */
uint32_t nv2a_scale_px(uint32_t v, uint32_t lines);

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
