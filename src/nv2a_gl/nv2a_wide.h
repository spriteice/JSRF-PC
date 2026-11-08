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

/* A squeezed overlay in a viewport that isn't the whole frame -- a split
 * screen's HUD, in its player's viewport -- lined up with the rest of the
 * frame. Its x are the frame's pixels whatever its viewport, and its window
 * clip is squeezed about the frame's centre (nv2a_wide_clip_x), but the
 * shader squeezes it about its viewport's centre: one centred at 160 left
 * its draws 40 title pixels outward of their clip at 16:9, and cut there.
 * This is the wideShift (the title's pixels) that squeezes it about the
 * frame's centre instead, for a viewport centred at centre and half wide
 * (the title's pixels): (1 - s) times its centre's distance from the
 * frame's, over s. 0 for one centred on the frame, at s 1 (4:3), for a
 * centre that isn't a number, for a half width that isn't above 0 (a
 * mirrored view), and for a viewport that isn't inside the frame (one in
 * another surface's pixels). */
float nv2a_wide_overlay_shift(float centre, float half, float s);

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

/* A HUD piece's side; NONE is the end of its draws. ACROSS is a menu part
 * that goes across the whole width instead (port/widescreen_menus.h): a
 * background grid or bar, laid out for the 4:3 screen and stretched to the
 * wide one. Its 3D is stretched about the screen's centre
 * (nv2a_wide_across_stretch), its 2D left unsqueezed, which stretches it
 * the same way, and neither is clipped to the 4:3 screen. WHOLE is text
 * that runs the whole width, a menu's ticker: its 2D is squeezed, as any
 * text is, whatever its span, so its letters keep their shape, but it isn't
 * moved or clipped to the 4:3 screen, so where the game side puts it
 * (spread out from the centre by 1/s) it shows on the whole screen.
 * LEFT_HALF and RIGHT_HALF are a menu's backdrop, a grid, which the game
 * side draws twice: each copy is moved out by whole steps of its grid
 * (nv2a_wide_half_shift), the left one left and the right one right, and
 * shown only on its half of the screen (nv2a_wide_half_clip_x), so the grid
 * carries on to the screen's edges with its cells square: nothing is
 * stretched or squeezed. FILL isn't a piece's side but the frame's: a plain
 * menu draws in it, and its sides are filled (nv2a_wide_fill_cols). OVER is
 * a part of such a menu that moves over its sides, the moving rectangles of
 * the garage's menus and the loading screens: stretched as a part drawn
 * across is, it shows on the sides over the fill, which carries out only
 * what is under it (nv2a_wide_fill_copy), and it doesn't keep the frame
 * from being filled. BARS is a part whose bars go across: drawn as anything
 * else is (its 2D squeezed, its 3D widened with the scene) and not clipped
 * to the 4:3 screen, but a bar in its 3D (nv2a_wide_bar: a plain rectangle,
 * much taller than wide) is stretched about the screen's centre as a part
 * drawn across is. The game parks such bars just past the 4:3 edges, out of
 * sight on the console, and sweeps them in from there: stretched, they're
 * parked past the wide screen's edges and sweep in from those, and what
 * else the part moves in from off the screen comes in from the screen's
 * edges with the widened view. Its full-screen 2D is fitted as a filled
 * frame's is (nv2a_wide_fill_fits): art squeezed, keeping its shape, an
 * untextured layer (a fade, a flash) or a copy of the frame left full
 * width. It moves nothing and fills nothing. STRIP, like FILL, is the
 * frame's, not a piece's: a filled frame's menu has a thin strip of art
 * along the top of the picture, at the 4:3 screen's left edge, which the
 * fill would carry out across the left side (the garage menus' tan strip);
 * its piece is how far into the 4:3 picture the strip runs, in title
 * pixels, and the strip's rows, from the left edge of the screen to there,
 * take the colour of the row under it (nv2a_wide_fill_strip). LINES is a
 * menu's 3D map, the pause menus' and the race preview's: drawn as it is,
 * widened with the scene, but shown only in the light grid, not over the
 * dark grid along its top (NV2A_WIDE_LINES_TOP), and the pause map's yellow
 * lines (nv2a_wide_lines) are carried out past the wide screen's edges. It
 * moves nothing. */
enum { NV2A_WIDE_HUD_LEFT = -1, NV2A_WIDE_HUD_NONE = 0, NV2A_WIDE_HUD_RIGHT = 1,
       NV2A_WIDE_HUD_ACROSS = 2, NV2A_WIDE_HUD_WHOLE = 3, NV2A_WIDE_HUD_LEFT_HALF = 4,
       NV2A_WIDE_HUD_RIGHT_HALF = 5, NV2A_WIDE_HUD_FILL = 6, NV2A_WIDE_HUD_OVER = 7,
       NV2A_WIDE_HUD_BARS = 8, NV2A_WIDE_HUD_STRIP = 9, NV2A_WIDE_HUD_LINES = 10 };

/* Whether a draw between the marks of a part whose bars go across
 * (NV2A_WIDE_HUD_BARS), of n vertices whose positions come in w wide and h
 * tall, is a bar: a plain rectangle (no more than NV2A_WIDE_BAR_VERTS
 * vertices, two triangles) at least NV2A_WIDE_BAR_TALL times as tall as it
 * is wide. Not for a draw with no height, or one whose sizes aren't
 * numbers. */
#define NV2A_WIDE_BAR_VERTS 6u
#define NV2A_WIDE_BAR_TALL  8.0f
int nv2a_wide_bar(uint32_t n, float w, float h);

/* Whether a 3D draw of n vertices between the marks of a part drawn across
 * (NV2A_WIDE_HUD_ACROSS) at squeeze s is the part's grid, drawn as a
 * backdrop's copies are instead of stretched. The menus' bands model (0x6B)
 * draws the dark grid along the bottom and down the right-hand side as one
 * mesh of hundreds of vertices (522 in the pause menus, the race preview,
 * the GG notebook and character select), and its bands and marks as plain
 * rectangles. Stretched, the grid's cells came out 1/s as wide as the
 * backdrop's beside them, out of line with them. So a draw of at least
 * NV2A_WIDE_SLAB_VERTS vertices is drawn twice, unstretched, the left copy
 * moved left and the right one right by the backdrop's own move, each shown
 * on its half (nv2a_wide_half_shift, nv2a_wide_half_clip_x): every grid on
 * the screen then has the backdrop's lines. Only where the backdrop is
 * drawn twice too (nv2a_wide_half_move not 0); 0 for anything else. */
#define NV2A_WIDE_SLAB_VERTS 64u
int nv2a_wide_slab(uint32_t n, float s);

/* The pause menus' map (NV2A_WIDE_HUD_LINES) draws its yellow lines, one
 * through the player's place along each axis, as one model: three thin
 * boxes 426.6 long (213.3 each way) and 147.7 for the upright one, as their
 * positions come in, where the map's own models are under 70 across. At
 * 4:3 they run past the screen's edges; on a wide screen, turned, their
 * ends showed short of its edges (Aden, 8 November 2026, 07:15 UTC: "it
 * should also continue to the edge of the screen in the light blue
 * section"). A draw of the map whose positions span at least
 * NV2A_WIDE_LINES_SPAN on some axis is those lines: each position's
 * coordinate at least an eighth of that span from 0 (a box's length, not
 * its thickness) is made NV2A_WIDE_LINES_LONGER times as far out
 * (nv2a_wide_lines_coord), so each line runs on to the screen's edge
 * under the same camera, as thin as it was. They are shown only from row
 * NV2A_WIDE_LINES_TOP down (the title's pixels): the top of the grey bar,
 * under which the menus' light grid starts, not over the dark grid above
 * it (Aden, 07:03 UTC: "the yellow lines should only show in the light blue
 * grid, not the dark blue one"). So is every other draw of the map, and of
 * the race preview's course map, whose tops came up over that dark grid
 * once widened (08:32 UTC: "it should only be drawn in the light grid (as
 * the yellow lines)"). Below and at the right the menus' bands cover them,
 * as at 4:3. */
#define NV2A_WIDE_LINES_SPAN   200.0f
#define NV2A_WIDE_LINES_LONGER 8.0f
#define NV2A_WIDE_LINES_TOP    47u
int nv2a_wide_lines(float span);
float nv2a_wide_lines_coord(float c, float span);

/* A mark's flag: the piece draws in a viewport of its own, a strip that cuts
 * off what rolls past its edges. Its 2D fills that viewport, so measured
 * there it would be a backdrop and left unsqueezed: it is squeezed whatever
 * its span, and moved from that viewport's centre, not the frame's. */
#define NV2A_WIDE_HUD_OWN_VIEW 1u

/* A mark's flag: the piece is one of a menu's parts at a side, on a
 * backdrop drawn in two copies (NV2A_WIDE_HUD_LEFT_HALF and _RIGHT_HALF).
 * It moves with its side's copy of the grid, by whole steps of it
 * (nv2a_wide_hud_grid_shift), not by exactly the screen's margin, so it
 * sits on the same cell as on the console's 4:3 screen: the pause menus'
 * buttons across the dark column's edge, as there. Aden's choice, 8
 * November 2026 (07:44 UTC, card): "Move them with the grid". */
#define NV2A_WIDE_HUD_ON_GRID 2u

/* The mark's value for piece (0 to 255, which the log names), side and
 * flags (NV2A_WIDE_HUD_OWN_VIEW, NV2A_WIDE_HUD_ON_GRID, both or 0). */
uint32_t nv2a_wide_hud_mark(unsigned piece, int side, unsigned flags);

/* A value read on NV2A_WIDE_HUD_METHOD as a mark: 1, with its piece, side
 * and flags, or 0 for a value that isn't one. */
int nv2a_wide_hud_read(uint32_t value, unsigned *piece, int *side, unsigned *flags);

/* How far a draw between the marks of a piece on side moves to that side's
 * edge (0 for any side but LEFT and RIGHT: a part drawn across is
 * stretched instead, and a backdrop's copy moves by nv2a_wide_half_shift),
 * as the wideShift the shader takes off its x (the title's pixels),
 * for a draw whose viewport is centred at centre (the title's pixels: the
 * frame's 320 for one player's HUD). The squeeze brings x toward that
 * centre by s, so it left (1 - s) of the distance from there to the edge
 * between the piece and the screen's edge: a squeezed overlay (squeezed 1)
 * moves by that over s in its own pixels, and a 3D piece by that itself.
 * One player's HUD moves 106.7 and 80 title pixels at 16:9. 0 for no side,
 * at s 1 (4:3), and for a centre that isn't a number. */
float nv2a_wide_hud_shift(int side, float s, int squeezed, float centre);

/* That move for a piece marked NV2A_WIDE_HUD_ON_GRID: scaled from the
 * screen's margin, (1/s - 1) x 320 of the title's pixels, to the backdrop
 * copies' move, nv2a_wide_half_move (whole steps of the grid): 120.7 for
 * 106.7 at 16:9, 241.4 for 248.9 at 64:27. The margin's own move where the
 * backdrop isn't drawn twice (nv2a_wide_half_move 0). */
float nv2a_wide_hud_grid_shift(int side, float s, int squeezed, float centre);

/* How much a 3D draw between the marks of a part drawn across
 * (NV2A_WIDE_HUD_ACROSS) is stretched, about the screen's centre: 1/s, which
 * takes the 4:3 screen the widened projection leaves it in out to the
 * screen's edges, as the console's stretched 4:3 picture would (4/3 at
 * 16:9). 1, no stretch, at s 1 (4:3) and for an s that isn't between 0 and
 * 1. */
float nv2a_wide_across_stretch(float s);


/* A menu's backdrop (NV2A_WIDE_HUD_LEFT_HALF and _RIGHT_HALF) is a grid that
 * repeats every NV2A_WIDE_HALF_STEP of the title's pixels, as the game lays
 * its 4:3 screen out: two of the pause menus' cells, each 20.115 of them
 * (measured at 4:3, 16:9 and 21:9, 20.11 to 20.12), not 20: a step of 40
 * left the halves 0.07 of a cell out of step at 16:9 and 0.13 at 21:9, a
 * line at the seam thinned or gone. The grid has a line at the 4:3
 * screen's centre. */
#define NV2A_WIDE_HALF_STEP 40.23f

/* How far each copy of the backdrop moves out at squeeze s, in the title's
 * pixels as the game lays its 4:3 screen out: the whole number of
 * NV2A_WIDE_HALF_STEPs nearest to what the parts at the screen's edges move,
 * (1/s - 1) x 320, so the halves meet at the screen's middle with the grid
 * lined up: three steps (120.7) at 16:9, two (80.5) at 16:10, six (241.4)
 * at 21:9. 0, the backdrop drawn
 * once as the game draws it, at 4:3, for a shape too near 4:3 for a whole
 * step (whose screen edges the backdrop reaches as it is), and for an s
 * that isn't between 0 and 1. The game side draws the backdrop twice only
 * where this isn't 0. */
float nv2a_wide_half_move(float s);

/* That move, for a draw between the marks of a copy on side, as the
 * wideShift the shader takes off its x (the frame's title pixels): the
 * widened projection leaves the game's 4:3 screen s as wide, so a 3D copy
 * moves s times the move (90.5 at 16:9), and a squeezed overlay (squeezed 1)
 * the move itself, in its own pixels, which the squeeze makes s as wide.
 * Positive (to the left) for LEFT_HALF, negative for RIGHT_HALF, and 0 for
 * any other side and wherever nv2a_wide_half_move is 0. */
float nv2a_wide_half_shift(int side, float s, int squeezed);

/* That draw's window clip, columns *x0 to *x1 (the far one exclusive) on a
 * surface surf_w wide: squeezed about the centre as nv2a_wide_clip_x does,
 * then moved with the piece (only for LEFT and RIGHT), so the screen's own
 * edge takes the 4:3 edge's place. The near edge down to a whole pixel, the
 * far one up, and both inside 0 to surf_w. */
void nv2a_wide_hud_clip_x(uint32_t surf_w, float s, int side, uint32_t *x0, uint32_t *x1);

/* The column of a surface surf_w wide at which the backdrop's halves meet at
 * squeeze s: half a cell (a quarter of NV2A_WIDE_HALF_STEP, s as wide in
 * the frame) left of the middle, to the nearest column, so the seam runs
 * down the middle of a cell, not along the grid's line at the centre,
 * where the least step out would thin the line or drop it. Left, not
 * right: the left copy shows the backdrop as far right as the seam plus its
 * move, and the dark column down the backdrop's right-hand side starts six
 * steps right of the centre, the move at 21:9 (nv2a_wide_half_move), so
 * there a seam right of the middle showed half a cell of that column down
 * the middle of the screen (Aden, 8 November 2026, 08:32 UTC: "half of a
 * grid is drawn as the dark grid in 21:9"). The right copy shows the
 * backdrop from the seam less its move, which is light grid out to the 4:3
 * screen's left edge. The middle, surf_w / 2, for an s that isn't between 0
 * and 1. */
uint32_t nv2a_wide_half_seam(uint32_t surf_w, float s);

/* A half's clip (NV2A_WIDE_HUD_LEFT_HALF or _RIGHT_HALF): columns *x0 to
 * *x1 cut at the seam (nv2a_wide_half_seam), which the left half ends at
 * and the right half starts at, so the two meet with no gap and no overlap.
 * A clip wholly on the other half comes out empty (*x0 == *x1); any other
 * side leaves it as it is. */
void nv2a_wide_half_clip_x(uint32_t surf_w, float s, int side, uint32_t *x0, uint32_t *x1);

/* Whether a screen-space draw's viewport, of scale vp_scale_x (half its
 * width), spans the whole frame, as one player's HUD does: as wide as
 * NV2A_WIDE_FRAMED_MAX_W at least, or with no scale (the whole surface). A
 * split screen's HUD is drawn in its player's narrower viewport, and isn't
 * moved to the edge, only lined up with its view (nv2a_wide_overlay_shift);
 * a piece with a viewport of its own (NV2A_WIDE_HUD_OWN_VIEW) is moved. */
int nv2a_wide_hud_whole_view(float vp_scale_x);

/* The side fill. A plain menu -- one with no parts to move, whose rows end
 * in flat colour at the 4:3 screen's edges -- sits in the middle 4:3 of the
 * wide screen, and what the widened scene shows around it there is what the
 * console's screen edge hid. The game side marks each frame such a menu
 * draws in (NV2A_WIDE_HUD_FILL, written as the menu's draw starts, in order
 * with its draws: no end mark, the frame's flip ends it), and on that frame:
 *   - the menu's full-screen art is fitted into the 4:3 screen with the rest
 *     of the menu, keeping its shape, where it would otherwise be stretched
 *     across (nv2a_wide_fill_fits);
 *   - at the flip, the column of pixels just inside each 4:3 edge is copied
 *     out across that side, so each row's colour carries on to the screen's
 *     edge and nothing is stretched (nv2a_wide_fill_cols). It covers the
 *     4:3 screen's outermost title pixel too, which some art leaves dark
 *     (the options menu's right edge) and the console's screen edge hid.
 * The art is fitted from the start of the frame after a filled one too, as
 * another object may draw it before the menu does. A frame with a part
 * moved to an edge (the HUD's, or a laid-out menu's) isn't filled, so the
 * fill never paints over it. A frame with no mark is left as it is. */

/* Whether a screen-space draw of extent ext (NDC, as nv2a_wide_overlay
 * takes it) is squeezed on a filled frame: an overlay, as on any frame, or
 * full-screen art, which is textured and samples no surface the title drew
 * into (a copy of the frame, such as the one into the display buffer, which
 * must fill the screen). Untextured layers, fades and bars stay full width:
 * the fill carries their colour to the edges anyway. */
int nv2a_wide_fill_fits(const float ext[4], int textured, int samples_surface);

/* The fill on a filled frame drawn at a raster pw wide and widened at
 * squeeze s (the frame's shape's, nv2a_wide_squeeze_of): the 4:3 screen's
 * columns, *x0 to *x1 (the far one exclusive), which every partly covered
 * column at its edges is left out of; and the column copied across each
 * side, *src_l, one title pixel (pw x s / 640 of the raster's, at least one)
 * inside the left edge, across 0 to *src_l, and *src_r as far inside the
 * right edge, across *src_r + 1 to pw: the fill covers the columns between
 * each source and its edge too, and leaves *src_l to *src_r as they are.
 * Returns 0, with nothing to fill and the outputs untouched, at s 1 (4:3),
 * for an s that isn't between 0 and 1, and for a raster too narrow to have
 * sides. */
int nv2a_wide_fill_cols(uint32_t pw, float s, uint32_t *x0, uint32_t *x1,
                        uint32_t *src_l, uint32_t *src_r);

/* A strip's rows (NV2A_WIDE_HUD_STRIP) at the top of a filled frame drawn
 * at a raster pw x ph widened at squeeze s, running width title pixels into
 * the 4:3 picture: the strip is the picture's top NV2A_WIDE_STRIP_ROWS
 * title rows, and its raster rows, *r0 (0) to *r1 (the far one exclusive,
 * every row it touches), take row *r1's colour, from the screen's left edge
 * to *x1 (exclusive): the 4:3 screen's first column (nv2a_wide_fill_cols)
 * and width title pixels more (ph / 480 raster pixels each), every column
 * it touches. Returns 0, with the
 * outputs untouched, wherever nv2a_wide_fill_cols does, for a height of 0,
 * and where the strip would reach the 4:3 screen's right edge. */
#define NV2A_WIDE_STRIP_ROWS 6u
int nv2a_wide_fill_strip(uint32_t pw, uint32_t ph, float s, unsigned width, uint32_t *x1,
                         uint32_t *r0, uint32_t *r1);

/* The side fill drawn draw by draw. On a frame with a part over its sides
 * (NV2A_WIDE_HUD_OVER), copying columns at the flip would paint over that
 * part, and carry it out where it crosses a source column. So from the
 * menu's mark on, each of the menu's draws (but the parts over the sides)
 * is drawn in the middle, from source to source, and again on each side,
 * magnified about that side's source column until the one pixel there
 * covers the side: each side gets, row by row and in the draws' own order
 * and depth, the colour of what was drawn under the parts at its source,
 * and the parts over the sides, stretched, are kept where they're in front.
 *
 * For side NV2A_WIDE_HUD_NONE (the middle), LEFT or RIGHT, on a raster pw
 * wide at squeeze s: its columns, *x0 to *x1 (the far one exclusive) -- the
 * sources to each other, 0 to the left source, and past the right source to
 * pw, as nv2a_wide_fill_cols has them -- the source column the side's copy
 * shows, *src (none for the middle: the far edge, *x1), and the map the
 * copy's x takes in NDC, x' = *k x + *off: *k = 2 pw, which brings the
 * whole side to within half a pixel of the source column's centre, and 1
 * and 0 for the middle. Returns 0, with the outputs untouched, wherever
 * nv2a_wide_fill_cols does, and for any other side. */
int nv2a_wide_fill_copy(uint32_t pw, float s, int side, uint32_t *x0, uint32_t *x1,
                        uint32_t *src, float *k, float *off);

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
