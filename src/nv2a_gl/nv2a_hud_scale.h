/*
 * HUD scale's maths (nv2a_hud_scale.c): Display > HUD > HUD scale's steps
 * and how they are kept, and where a HUD piece is drawn at another size.
 *
 * The HUD's pieces are marked in the command stream, each between a start
 * and an end mark (nv2a_wide.h, NV2A_WIDE_HUD_METHOD; the game side's are in
 * port/widescreen_hud.c). A piece the HUD scale sizes has
 * NV2A_WIDE_HUD_SIZED in its marks, and NV2A_WIDE_HUD_LOW when it sits at
 * the bottom of the screen. Every draw between its marks, 2D and 3D alike,
 * is then drawn at the HUD scale about the piece's anchor
 * (nv2a_hud_anchor), the inner corner of the HUD's margin on its side, so
 * each piece stays as far from the screen's edges as the game puts it and
 * grows or shrinks from there. Disable HUD, beside it, draws none of them.
 * The state (the scale, whether the HUD is off, the piece being drawn)
 * stays in nv2a_gl.c, which asks this.
 *
 * Plain C, with nothing of GL's or SDL's, so a test links it
 * (tests/hud_scale).
 */
#ifndef NV2A_HUD_SCALE_H
#define NV2A_HUD_SCALE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Display > HUD > HUD scale: 0.5x to 1.5x the HUD's own size, a
 * tenth a step, kept as a whole number of tenths (spriteice, 12 November
 * 2026, 09:37 UTC: "The scale will be 0.5 to 1.5, and each click of the
 * plus or minus will move the scale by 0.1", and "default value of 1"). */
#define NV2A_HUD_TENTHS_MIN     5
#define NV2A_HUD_TENTHS_MAX     15
#define NV2A_HUD_TENTHS_DEFAULT 10      /* the game's own size */

/* The row's numbers, as the menu steps them (src/menu/menu.h,
 * XBOX_ITEM_NUMBER). */
#define NV2A_HUD_SCALE_MIN     0.5
#define NV2A_HUD_SCALE_MAX     1.5
#define NV2A_HUD_SCALE_STEP    0.1
#define NV2A_HUD_SCALE_DEFAULT 1.0

/* A scale, as hudscale= or the menu's row gives it, in tenths: rounded to
 * the nearest tenth, NV2A_HUD_TENTHS_MIN to _MAX; -1 for a number outside
 * 0.5 to 1.5 by more than a float's error, or one that isn't a number. */
int nv2a_hud_parse(double v);

/* The size tenths draws the HUD's pieces at, against the game's own: 0.5
 * to 1.5; 1 for anything outside NV2A_HUD_TENTHS_MIN to _MAX. */
float nv2a_hud_size(int tenths);

/* The HUD's margin, in the title's pixels: how far in from the 4:3 screen's
 * edges the game puts its HUD, which a piece keeps at every size. Across,
 * the score, the trick name and the rolling number start 51 in from the
 * left, and the health bar, the gauge and the digits end 52 to 55 in from
 * the right (the two two-digit numbers, 36); down, the rolling number's
 * label and the health bar start 40 from the top, and the gauge ends 42
 * from the bottom (widescreen-3b/pc/search-2/PC-REPORT.md in the shared
 * folder, and the screenshots in widescreen-hud/). */
#define NV2A_HUD_MARGIN_X 51.0f
#define NV2A_HUD_MARGIN_Y 40.0f

/* Where a piece on side (NV2A_WIDE_HUD_LEFT or _RIGHT), at the bottom of
 * the screen when low, is sized about: the inner corner of the HUD's
 * margin there, *ax and *ay in the frame's NDC (-1 to 1, y up, as the
 * shader's gl_Position has it). s is how much the frame squeezes the HUD
 * across, as at the screen's edges in widescreen (nv2a_wide_squeeze_of), 1
 * at 4:3: the margin across is s as wide in the frame. The screen's centre,
 * (0, 0), for any other side; an s that isn't between 0 and 1 is taken as
 * 1. */
void nv2a_hud_anchor(int side, int low, float s, float *ax, float *ay);

/* The draw's window clip, columns *x0 to *x1 and rows *y0 to *y1 (the far
 * ones exclusive) on a surface w x h of the title's pixels, sized k times
 * about the anchor (ax, ay) as the draw is, so that a strip that cuts off
 * what moves past its edges (the rolling number's) still does: the near
 * edges down to a whole pixel, the far ones up, and all inside the surface.
 * Left as it is for a k of 1, or one that isn't above 0. */
void nv2a_hud_clip(uint32_t w, uint32_t h, float k, float ax, float ay,
                   uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1);

#ifdef __cplusplus
}
#endif

#endif
