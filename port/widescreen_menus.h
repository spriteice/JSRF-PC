/* The game's menus laid out for widescreen: the game's half.
 *
 * The game lays its menus out for a 4:3 screen. In widescreen the widened
 * projection leaves a menu's 3D parts in the middle 4:3 at their shape, and
 * the renderer squeezes its text there with them, so a menu sits in the
 * middle of the wide screen with its backdrop's edges showing. Each menu
 * that has a wide layout gets one here (docs/project-rules.md, Where code
 * goes 6: one named override per menu draw method): its backdrop's grids
 * carry on to the screen's edges, its bars go across the whole width, and
 * its other parts, which keep their shape, go to the left or the right
 * edge, the way the HUD's pieces do (widescreen_hud.h).
 *
 * A menu's parts are 3D models held in fixed fields of the menu's object,
 * each drawn once a frame through the models' draw, sub_00047690, with
 * that field's model as this. So the override of the menu's draw says which
 * menu is drawing (widescreen_menus_begin), and the models' draw asks, for
 * each model it is given, whether it is one of that menu's fields and on
 * which side (widescreen_menus_part): the parts are told apart by their
 * field in the menu's object, not by where the game calls from. The marks
 * that tell the renderer are the HUD's (NV2A_WIDE_HUD_METHOD), with the
 * part's side, or NV2A_WIDE_HUD_ACROSS for a bar or band, which is
 * stretched across.
 *
 * The backdrop (WIDE_MENU_EXTEND) holds both of the menu's grids, the dark
 * one around the edges and the light window behind the map, which repeat
 * every NV2A_WIDE_HALF_STEP. Stretched, its cells would come out wider than
 * tall, so it is drawn twice instead, unstretched, between marks for each
 * half of the screen (NV2A_WIDE_HUD_LEFT_HALF, _RIGHT_HALF), and the
 * renderer moves the left copy left by a whole number of steps and shows it
 * on the screen's left half, and the right copy right by as many, on the
 * right half (nv2a_wide_half_move, _shift). The halves meet at the screen's
 * middle, behind the map, where the grid lines up, as the move is whole
 * steps; the window's edge and the dark column move out with the parts near
 * them. The game's draw is the same both times: nothing of the game's own
 * is called to move it.
 *
 * A menu's text is queued, as the HUD's is, for the font's one draw late in
 * the frame. While a menu draws, the font call's override draws it at once
 * instead (widescreen_menus_text_side), between marks of its own: text that
 * starts in the screen's left quarter goes to the left edge with the parts
 * there, and the rest to the right. Each text call comes after the draw of
 * the part it sits on, so it is still drawn over that part.
 *
 * Three parts aren't the menu's fields:
 *   - the street pause menu's area name (the "Garage" strip at the top
 *     left) is a HUD sign that the map makes (class 0x1D1B68, drawn by
 *     sub_000FC370), told apart from the HUD's own signs by its flags
 *     (WIDE_MENU_AREA_SIGN_FLAG) and drawn only while that menu is up;
 *   - the other pause menu's header is that menu's child object (class
 *     0x1D0770, drawn by sub_000F7F00), drawn after the menu's draw returns:
 *     the menu's first child;
 *   - the ticker, a line of text that scrolls in from the right along the
 *     top bar in a mission (class 0x1D3770, drawn by sub_00115F40), the
 *     pause object's child after the menu, drawn after the menu.
 * The first two go to the left edge (widescreen_menus_sign, _child). The
 * ticker runs the whole width (widescreen_menus_ticker_x): its x is spread
 * out from the screen's middle by 1/s, so where the 4:3 screen's edges
 * were its line comes in at the wide screen's, and its mark
 * (NV2A_WIDE_HUD_WHOLE) has the renderer squeeze it but not clip it to the
 * 4:3 screen.
 *
 * At 4:3, and with no menu drawing, nothing is marked. */
#ifndef JSRF_WIDESCREEN_MENUS_H
#define JSRF_WIDESCREEN_MENUS_H

#include <stdint.h>
#include "widescreen_hud.h"

/* The menus with a wide layout, each by the game function that draws it. */
enum {
    WIDE_MENU_STREET_PAUSE,   /* the street's pause menu, with the map */
    WIDE_MENU_OTHER_PAUSE,    /* the pause menu of a race or a mission, with START OVER */
    WIDE_MENUS
};

/* A part's side when it is the backdrop, drawn in two halves moved out by
 * whole steps of its grid; not a renderer's side. */
#define WIDE_MENU_EXTEND 16

/* A part: the field of the menu's object that holds its model, its side
 * (NV2A_WIDE_HUD_LEFT, _RIGHT or _ACROSS, or WIDE_MENU_EXTEND) and its name,
 * for the log. */
typedef struct {
    uint16_t field;
    int8_t side;
    const char *name;
} WideMenuPart;

typedef struct {
    uint32_t va;               /* the function that draws it, its class's slot +0x24 */
    uint32_t vtable;           /* its class */
    const char *name;          /* for the log */
    const WideMenuPart *parts;
    unsigned n_parts;
} WideMenu;

extern const WideMenu widescreen_menus[WIDE_MENUS];

/* The marks' piece numbers, past the HUD's: a menu's part n is
 * WIDE_MENU_PIECE(menu, n); its text on each side, and the two parts that
 * aren't fields, have their own. */
#define WIDE_MENU_PIECE0        0x40u
#define WIDE_MENU_PIECE(m, n)   (WIDE_MENU_PIECE0 + 0x20u * (unsigned)(m) + (unsigned)(n))
#define WIDE_MENU_PIECE_TEXT_L  0xC0u
#define WIDE_MENU_PIECE_TEXT_R  0xC1u
#define WIDE_MENU_PIECE_SIGN    0xC2u
#define WIDE_MENU_PIECE_CHILD   0xC3u
#define WIDE_MENU_PIECE_TICKER  0xC4u

/* The ticker's class. */
#define WIDE_MENU_TICKER_CLASS 0x001D3770u

/* The area name's sign: the flag the map sets in its sign's flags (+0x5C),
 * which no HUD sign has (theirs are 0x58000000 and 0x40000000). */
#define WIDE_MENU_SIGN_FLAGS     0x5Cu
#define WIDE_MENU_AREA_SIGN_FLAG 0x80000000u

/* Where an object keeps its first child: the frame's walk goes down it. */
#define WIDE_MENU_FIRST_CHILD 0x28u

/* How long after a pause menu last drew its part that isn't a field may
 * still be moved, in seconds: the area name's sign is drawn before the
 * street pause menu in a frame, so it goes by the last frame's menu, and
 * the other pause menu's header and the ticker right after the menu. */
#define WIDE_MENU_RECENT 0.25

/* Where a menu's text goes: text that starts left of this x (the title's
 * pixels) to the left edge, and the rest to the right. The left-hand text
 * starts at 48 or 116, and the right-hand text at 540, or ends at 576 or
 * 580, right-aligned: so a long label in another language starts left of
 * the middle, but nowhere near this far. */
#define WIDE_MENU_TEXT_LEFT_OF 160.0f

/* The menu's draw: menu (WIDE_MENU_...) starts drawing, its object at self,
 * on (the scene is widened) and D3D has a device; the first time, a line in
 * the log names it. now is the time in seconds, for the area name's sign.
 * Returns whether it began, for widescreen_menus_end. A menu whose object
 * isn't of its class doesn't begin, and the log says so once. */
int widescreen_menus_begin(uint8_t *mem, int menu, uint32_t self, int on, double now);

/* After it: the menu no longer draws. Its object stays known for its child
 * (widescreen_menus_child). */
void widescreen_menus_end(int began);

/* For the models' draw: if model is one of the drawing menu's parts, its
 * side (and its piece number for the marks in *piece), else
 * NV2A_WIDE_HUD_NONE. The first time for each part, a line in the log names
 * it. */
int widescreen_menus_part(uint8_t *mem, uint32_t model, unsigned *piece);

/* For the font call's override: the side text at x goes to while a menu
 * draws (NV2A_WIDE_HUD_LEFT left of WIDE_MENU_TEXT_LEFT_OF, _RIGHT from it
 * on, and its piece in *piece), or NV2A_WIDE_HUD_NONE when no menu draws or
 * x isn't a number. */
int widescreen_menus_text_side(float x, unsigned *piece);

/* For the HUD signs' draw: whether the sign at sign is the street pause
 * menu's area name, to go to the left edge: its flags have
 * WIDE_MENU_AREA_SIGN_FLAG, and that menu drew within WIDE_MENU_RECENT of
 * now. The first time, a line in the log says so. */
int widescreen_menus_sign(uint8_t *mem, uint32_t sign, double now);

/* For the header class's draw: whether the object at self is the first
 * child of the other pause menu that drew last, within WIDE_MENU_RECENT of
 * now, to go to the left edge with it. The first time, a line in the log
 * says so. */
int widescreen_menus_child(uint8_t *mem, uint32_t self, double now);

/* The ticker's draw: the object at self starts drawing, on as for
 * widescreen_menus_begin. It begins, for widescreen_menus_ticker_x, when it
 * is of the ticker's class and a pause menu drew within WIDE_MENU_RECENT of
 * now; the first time, a line in the log says so. Returns whether it began,
 * for widescreen_menus_ticker_end. */
int widescreen_menus_ticker_begin(uint8_t *mem, uint32_t self, int on, double now);
void widescreen_menus_ticker_end(int began);

/* For the font call's override: while the ticker draws, its text at x goes
 * across the whole width, at *wide_x (x spread out from the middle by 1/s,
 * the widened projection's squeeze), with its piece in *piece: returns
 * NV2A_WIDE_HUD_WHOLE. Otherwise, or for an x or an s that isn't a
 * number, or an s that isn't between 0 and 1, NV2A_WIDE_HUD_NONE. */
int widescreen_menus_ticker_x(float x, float s, float *wide_x, unsigned *piece);

/* A part's marks: the start, with its side, before its draws, and the end
 * after them, written as widescreen_hud_push writes the HUD's. Return
 * whether the start was written, and nothing is written at the end when it
 * wasn't. WIDE_MENU_EXTEND isn't a mark's side: the backdrop's two halves
 * are marked NV2A_WIDE_HUD_LEFT_HALF and _RIGHT_HALF. */
int widescreen_menus_mark(uint8_t *mem, uint32_t device, unsigned piece, int side,
                          widescreen_hud_space_fn make_space);
void widescreen_menus_unmark(uint8_t *mem, uint32_t device, unsigned piece, int marked,
                             widescreen_hud_space_fn make_space);

#endif
