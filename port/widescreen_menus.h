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
 * right half (nv2a_wide_half_move, _shift). The halves meet behind the map,
 * down the middle of a cell just left of the screen's middle
 * (nv2a_wide_half_seam), where the grid lines up, as the move is whole
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
 *     (WIDE_MENU_AREA_SIGN_FLAG); the banner with an area's name that
 *     shows under an arrow as the player nears its way in is a sign too,
 *     of another maker (WIDE_MENU_BANNER_CLASS), and goes to the left edge
 *     as well;
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
 * The plain menus. The game's other menus move nothing: Roboy's garage
 * menus, save and load, and the like, each a column of rows in the middle
 * of the 4:3 screen whose bands end at its edges. They get the side fill
 * instead (nv2a_wide.h): each one's draw method, its class's slot +0xC (its
 * +0x24 sends the walk back to +0xC), has an override that marks the frame
 * before the menu draws (widescreen_menus_fill, NV2A_WIDE_HUD_FILL), and on
 * that frame the renderer fits the frame's full-screen art into the 4:3
 * screen in its own shape, and carries the column just inside each 4:3
 * edge out across that side, row by row, when the frame is shown: each
 * band's colour runs on to the wide screen's edge, and whatever the game
 * keeps past the 4:3 edges, which the console's screen hid, stays hidden.
 * The mark is the frame's, not a part's: it has no end, and holds until
 * the frame is shown. A frame with a part moved to an edge (the HUD's, or a
 * laid-out menu's) isn't filled. The garage's graffiti menu and the loading
 * screens are filled too, but only on the frames they draw in: the graffiti
 * menu on its pass 9 in its states 4 and 8 (graffiti select's 3D scene,
 * shown wide, draws in the others), the loading screen while it's shown.
 * So is VS mode's setup screen, the first after "VS mode" on the title
 * (spriteice, 8 November 2026, 08:39 UTC: "the first menu after clicking "vs
 * mode" (3rd option on title screen) is still completely broken"), by its
 * object's draw (sub_00071100, class 0x1CCA80): its layout, the teal with
 * its frame's and panels' outlines, is one full-screen picture cut from
 * the screen's sheet of parts, which the renderer's rule for full-screen 2D
 * stretched across the wide screen while the panels' shades, labels and
 * icons, smaller, kept their shape in the middle 4:3, out of line with it
 * (widescreen-3b/menus, test build menus11's step 13). Filled, the picture
 * is fitted into the 4:3 screen with them, and its edges, flat teal, are
 * carried out to the screen's. Its one 3D model is left as drawn: squeezed
 * as one (menus10), it no longer sat right.
 *
 * The moving rectangles. Most of these menus have a band of rectangles
 * that slide across the top of the screen, past the 4:3 edges, which the
 * fill would carry out as they pass, flipping each side's colour. They're
 * nodes of the menu's own background model, moved by its animation, drawn
 * in the one call that draws the band under them. So while a filled menu
 * draws an animated model, the nodes whose animation moves them along x
 * alone are found from the animation's keys (widescreen_menus_sliders_begin).
 * The same animation slides the three arrows along the bottom of the
 * garage's menus along x alone too, as a chain, each carried by the one
 * before it and sliding out from it; where the first doesn't slide, it
 * wasn't found, and the two it carries were stretched without it, so one
 * landed on the next (Adri on GitHub issue 2, 10 November 2026). So a
 * chain is stretched alike, and with it the still node that carries it
 * (the first arrow), unless that one carries some of the band too. Left as
 * drawn instead, an arrow that slides past the 4:3 screen's edge would run
 * on into a side (test build 9e02d60-arrows2, on the practice menu). That
 * still node is the chain's anchor: it and what it carries are stretched
 * about its own origin, at the 4:3 picture's left edge, rather than the
 * screen's centre, so the arrows still start at the stop symbol under the
 * first, which isn't stretched (on the system and graffiti menus; a chain
 * the model's first node carries is stretched about the centre). Each
 * draw found is marked NV2A_WIDE_HUD_OVER, node by node, from the
 * override of the node function the fixed-function pass calls for it
 * (widescreen_menus_slider): the renderer stretches them across the whole
 * width, as the console's stretched picture would, and fills the sides from
 * the band under them, draw by draw (nv2a_wide_fill_copy).
 *
 * The GG notebook is laid out as the street pause menu is, from the same
 * models: its backdrop in two halves, its bands across, the grey block
 * with "Press the B Button" at the right edge with its text, and its page
 * (the tab, the record with its stamp, the arrows) where it is, with its
 * text. Its area name is the street pause menu's, the map's sign, drawn
 * after the notebook while it's up, and goes to the left edge as there.
 * The character select that saving a graffiti opens (class 0x1D2BB0) is
 * laid out the same way, from the same models: its backdrop in two
 * halves, its bands (animated here) across, the grey block with "Press the
 * B Button" at the right edge with its text, and its panel of characters
 * (model 0xD5) where it is, with the characters and their names.
 *
 * The race preview (class 0x1D2788), shown before every race and chase
 * mission starts, is drawn as the pause menus are, from the same models
 * (widescreen-3b/menus/search-6): its backdrop in two halves, its top bar
 * and bands across, the grey bar with "Press the A Button!" at the right
 * edge with its text, and the mode's emblem, when it has one, at the left.
 * Its other parts are its children, drawn after it: the race's name, a
 * header (class 0x1D0770), goes to the left edge (widescreen_menus_child);
 * the area's name is the map's kind of sign, which goes there already; its
 * ticker (class 0x1D07F8, drawn as the pause menus' is) runs the whole
 * width; and its course map is an overlay root of its own, widened as the
 * pause map is.
 *
 * The title (class 0x1CCEF8) is widened from its first frame, its opening
 * too (with JSRF_WIDE_TITLE=0, from the first "please press start" on, its
 * state 3, from the state its draw reads: widescreen_menus_title). In state
 * 6, over the city, the top-left "please press start" (its pack's model 1)
 * goes to the left edge, and the logo at the bottom right (its pack's model
 * 0) to the right edge. The centred one (model 2, states 3 and 10) stays
 * where it is. In every other state, widened, the bars the title itself
 * draws go across (widescreen_menus_title_bars, NV2A_WIDE_HUD_BARS): its
 * opening parks its tall bars just past the 4:3 edges, which the console's
 * screen hid, and sweeps them in from there, so they're stretched across
 * the width about its centre, as a menu's bars are, and come in from the
 * screen's edges. The rest of it, its logo too, is drawn as it is, all of
 * it 3D and not clipped, so what flies in from off the screen comes in
 * from the screen's edges with the widened view, and the city, drawn by
 * another object, fills the sides.
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
    WIDE_MENU_NOTEBOOK,       /* the GG notebook, from the street pause menu */
    WIDE_MENU_CHARACTERS,     /* the character select that saving a graffiti opens */
    WIDE_MENU_RACE_PREVIEW,   /* the preview before a race or a chase mission, with the course */
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
    uint32_t va;               /* the function that draws it, its class's slot +0x24 (+0xC for the race preview) */
    uint32_t vtable;           /* its class */
    const char *name;          /* for the log */
    const WideMenuPart *parts;
    unsigned n_parts;
    /* Where its text goes: 0, by x (WIDE_MENU_TEXT_LEFT_OF), as the pause
     * menus' does; or the y (the title's pixels) from which text goes to
     * the right edge with the menu's bottom block, the rest staying where
     * it is, as the notebook's does. */
    float block_y;
} WideMenu;

extern const WideMenu widescreen_menus[WIDE_MENUS];

/* The marks' piece numbers, past the HUD's: a menu's part n is
 * WIDE_MENU_PIECE(menu, n), WIDE_MENU_PARTS_MAX a menu (up to eight menus);
 * its text on each side, and the parts that aren't fields, have their own. */
#define WIDE_MENU_PIECE0        0x40u
#define WIDE_MENU_PARTS_MAX     0x10u
#define WIDE_MENU_PIECE(m, n)   (WIDE_MENU_PIECE0 + WIDE_MENU_PARTS_MAX * (unsigned)(m) + (unsigned)(n))
#define WIDE_MENU_PIECE_TEXT_L  0xC0u
#define WIDE_MENU_PIECE_TEXT_R  0xC1u
#define WIDE_MENU_PIECE_SIGN    0xC2u
#define WIDE_MENU_PIECE_CHILD   0xC3u
#define WIDE_MENU_PIECE_TICKER  0xC4u
#define WIDE_MENU_PIECE_SLIDERS 0xC5u   /* the moving rectangles */
#define WIDE_MENU_PIECE_TITLE   0xC6u   /* the title's top-left "please press start" */
#define WIDE_MENU_PIECE_LOGO    0xC7u   /* the title's logo at the bottom right */
#define WIDE_MENU_PIECE_BARS    0xC8u   /* the title's own draws, its bars across */
#define WIDE_MENU_PIECE_MAP     0xC9u   /* the pause menus' 3D map, its yellow lines */
#define WIDE_MENU_PIECE_COURSE  0xCAu   /* the race preview's 3D course map */
#define WIDE_MENU_PIECE_RESULT  0xCBu   /* a result screen's banner */
#define WIDE_MENU_PIECE_CHOICE  0xCCu   /* a result screen's icons and static bar */
#define WIDE_MENU_PIECE_TIME    0xCDu   /* a result screen's race time */

/* The tickers' classes: the pause menus' and the race preview's, both drawn
 * by sub_00115F40. */
#define WIDE_MENU_TICKER_CLASS         0x001D3770u
#define WIDE_MENU_PREVIEW_TICKER_CLASS 0x001D07F8u

/* The headers' class (drawn by sub_000F7F00): the other pause menu's, and
 * the race preview's band with the race's name. */
#define WIDE_MENU_HEADER_CLASS 0x001D0770u

/* The area name's sign: the flag the map sets in its sign's flags (+0x5C),
 * which no HUD sign has (theirs are 0x58000000 and 0x40000000). */
#define WIDE_MENU_SIGN_FLAGS     0x5Cu
#define WIDE_MENU_AREA_SIGN_FLAG 0x80000000u
/* The banner with an area's name that shows under an arrow as the player
 * nears the way into another area (spriteice, 8 November 2026, 14:48 UTC: "The
 * level banner before entering a level needs to go to the left side of the
 * screen, the same distance away from the edge as it is in 4:3") is a sign
 * with no flags. Only sub_001022C0 makes those, each the child of the HUD
 * manager's object of class 0x1D1DD0 (drawn by sub_00102490, 3D at the
 * HUD's origin, the arrow by our reading), whose pointer the sign keeps at
 * +0x24 (pc/search-2, section 5). spriteice's menus12 log has it as "a sign with
 * flags 00000000 (class 001D1B68): left where it is". The arrow stays in
 * the middle. */
#define WIDE_MENU_SIGN_PARENT    0x24u
#define WIDE_MENU_BANNER_CLASS   0x001D1DD0u
/* A result screen's banner, YOU WIN, CLEARED, GAME OVER or YOU LOSE, is a
 * HUD sign too, of the type (+0x54) for its word, that the result flow
 * makes with flags of its own, which no other maker of a sign
 * (sub_000FBFD0) gives (widescreen-3b/pc/search-2, section 5): 0x14000000
 * from sub_00072C00, a race's or a mission's end, and 0x12000000 from
 * sub_00074680, which makes the return to title and restart choice too. */
#define WIDE_MENU_SIGN_TYPE                0x54u
#define WIDE_MENU_RESULT_SIGN_FLAGS        0x14000000u
#define WIDE_MENU_RESULT_SIGN_FLAGS_CHOICE 0x12000000u

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

/* The notebook's text that goes to the right edge with its grey block:
 * "Press the B Button to go back", at y 410, in the block's rows (406 to
 * 436). The page's text is above it. The character select's is drawn
 * there too, right-aligned at 576, and its characters' names above it. */
#define WIDE_MENU_NOTEBOOK_BLOCK_Y 400.0f

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

/* For the font call's override: the side text at x, y goes to while a menu
 * draws (NV2A_WIDE_HUD_LEFT left of WIDE_MENU_TEXT_LEFT_OF, _RIGHT from it
 * on, and its piece in *piece; for a menu with a block_y, _RIGHT from that
 * y down, and NV2A_WIDE_HUD_NONE above it), or NV2A_WIDE_HUD_NONE when no
 * menu draws or x or y isn't a number. */
int widescreen_menus_text_side(float x, float y, unsigned *piece);

/* For the HUD signs' draw: the side the sign at sign goes to, with its
 * piece in *piece, or NV2A_WIDE_HUD_NONE where it stays. top is the end of
 * the guest's memory, for the sign's parent's pointer.
 *   - An area's name goes to the left edge (WIDE_MENU_PIECE_SIGN): its
 *     flags have WIDE_MENU_AREA_SIGN_FLAG. The map makes it, for the street
 *     pause menu and the GG notebook, and the race preview makes its own.
 *     The first time in each (that menu, the notebook or the preview drew
 *     within WIDE_MENU_RECENT of now, or none of them), a line in the log
 *     says so. A menu's moves with its grid (widescreen_menus_mark_flags).
 *   - The banner with an area's name under the arrow to another area goes
 *     to the left edge too, with the same piece, not with a grid: its flags
 *     are 0 and its parent (+WIDE_MENU_SIGN_PARENT, within top) is of class
 *     WIDE_MENU_BANNER_CLASS. The first time, a line in the log says so.
 *   - A result screen's banner goes to the right edge
 *     (WIDE_MENU_PIECE_RESULT): its flags are one of the result flow's
 *     (WIDE_MENU_RESULT_SIGN_FLAGS, _CHOICE). The first time, and for each
 *     of its first WIDE_MENU_SIGNS_TOLD types (+WIDE_MENU_SIGN_TYPE), a line
 *     in the log says so, so a log names which type is which word.
 * The first time for each of the first WIDE_MENU_SIGNS_TOLD flags of a sign
 * that stays, a line in the log says so too. */
#define WIDE_MENU_SIGNS_TOLD 8u
int widescreen_menus_sign(uint8_t *mem, uint32_t top, uint32_t sign, double now,
                          unsigned *piece);

/* For the header class's draw: whether the object at self is the first
 * child of the other pause menu that drew last, within WIDE_MENU_RECENT of
 * now, or a header (of WIDE_MENU_HEADER_CLASS) drawn within
 * WIDE_MENU_RECENT of the race preview, its race's name, to go to the left
 * edge with it. The first time for each, a line in the log says so. */
int widescreen_menus_child(uint8_t *mem, uint32_t self, double now);

/* The ticker's draw: the object at self starts drawing, on as for
 * widescreen_menus_begin. It begins, for widescreen_menus_ticker_x, when it
 * is of a ticker's class (the pause menus' or the race preview's) and a
 * pause menu or the race preview drew within WIDE_MENU_RECENT of now; the
 * first time for each class, a line in the log says so. Returns whether it
 * began, for widescreen_menus_ticker_end. */
int widescreen_menus_ticker_begin(uint8_t *mem, uint32_t self, int on, double now);
void widescreen_menus_ticker_end(int began);

/* For the font call's override: while the ticker draws, its text at x goes
 * across the whole width, at *wide_x (x spread out from the middle by 1/s,
 * the widened projection's squeeze), with its piece in *piece: returns
 * NV2A_WIDE_HUD_WHOLE. Otherwise, or for an x or an s that isn't a
 * number, or an s that isn't between 0 and 1, NV2A_WIDE_HUD_NONE. */
int widescreen_menus_ticker_x(float x, float s, float *wide_x, unsigned *piece);

/* The flags of piece's mark at side: NV2A_WIDE_HUD_ON_GRID for a piece at
 * the left or right of a menu whose backdrop is drawn in two copies
 * (WIDE_MENU_EXTEND), so that it moves with its side's copy of the grid and
 * keeps its cell (spriteice's choice, 8 November 2026, "Move them with the
 * grid"): the menus' parts, their text and the header; the area name's sign
 * only in the street pause menu and the notebook, not as the banner on its
 * own (from the last widescreen_menus_sign). For the moving rectangles over
 * the sides (WIDE_MENU_PIECE_SLIDERS, NV2A_WIDE_HUD_OVER): the flags of the
 * node widescreen_menus_slider last found, NV2A_WIDE_HUD_ANCHOR for a still
 * node that carries a chain and NV2A_WIDE_HUD_FROM_ANCHOR for it and what it
 * carries (widescreen_menus_sliders_begin). 0 for anything else. */
unsigned widescreen_menus_mark_flags(unsigned piece, int side);

/* A menu's 3D map's draw starts: the pause menus' (sub_00114A80 for mode 0,
 * sub_001137B0 for mode 1; widescreen-3b/menus/search-1, section 5), or the
 * race preview's course map (sub_00112470, mode 2; search-6), on as for
 * widescreen_menus_begin: returns on, for marks around the whole draw
 * (WIDE_MENU_PIECE_MAP, or WIDE_MENU_PIECE_COURSE for the course map, with
 * NV2A_WIDE_HUD_LINES), in which the renderer shows the map only in the
 * light grid, not over the dark grid at the top, and carries the pause
 * map's yellow lines out to the wide screen's edges (nv2a_wide_lines). The
 * first time for each, a line in the log says so; 0 for any other mode. */
int widescreen_menus_map(int mode, int on);

/* A part's marks: the start, with its side and its flags
 * (widescreen_menus_mark_flags), before its draws, and the end after them,
 * written as widescreen_hud_push writes the HUD's. Return whether the start
 * was written, and nothing is written at the end when it wasn't.
 * WIDE_MENU_EXTEND isn't a mark's side: the backdrop's two halves are
 * marked NV2A_WIDE_HUD_LEFT_HALF and _RIGHT_HALF. */
int widescreen_menus_mark(uint8_t *mem, uint32_t device, unsigned piece, int side,
                          widescreen_hud_space_fn make_space);
void widescreen_menus_unmark(uint8_t *mem, uint32_t device, unsigned piece, int marked,
                             widescreen_hud_space_fn make_space);

/* The plain menus, each by the game function that draws it. The graffiti
 * menu and the loading screen draw on some of their calls only
 * (WidePlainMenu's when). */
enum {
    WIDE_PLAIN_SAVE,          /* save and load */
    WIDE_PLAIN_GARAGE,        /* Roboy's garage menu */
    WIDE_PLAIN_SYSTEM,        /* the garage's system menu */
    WIDE_PLAIN_OPTIONS,       /* the garage's options menu */
    WIDE_PLAIN_PRACTICE,      /* the garage's practice menu */
    WIDE_PLAIN_MUSIC,         /* the garage's music menu */
    WIDE_PLAIN_TEST_RUN,      /* the garage's test run menu */
    WIDE_PLAIN_COURSE,        /* the test run's course screen */
    WIDE_PLAIN_CHARACTERS,    /* the garage's character select */
    WIDE_PLAIN_GRAFFITI,      /* the garage's graffiti menu */
    WIDE_PLAIN_LOADING,       /* the loading screen, with the blue rectangles */
    WIDE_PLAIN_VS,            /* VS mode's setup screen */
    WIDE_PLAINS
};

/* When a plain menu's draw draws: at every call, or as the graffiti menu's
 * and the loading screen's do. */
enum { WIDE_FILL_ALWAYS, WIDE_FILL_GRAFFITI, WIDE_FILL_LOADING };

typedef struct {
    uint32_t va;               /* the function that draws it, its class's slot +0xC (+0x24 for the graffiti menu) */
    uint32_t vtable;           /* its class */
    const char *name;          /* for the log */
    uint8_t when;              /* WIDE_FILL_... */
    uint8_t strip;             /* how far its tan strip runs into the 4:3 picture, or 0 (WIDE_MENU_STRIP) */
    uint8_t slides;            /* whether its moving rectangles go across (widescreen_menus_sliders_begin) */
} WidePlainMenu;

/* The garage's menus' tan strip: a thin strip of their art along the top of
 * the picture, its top four or five title rows, from the 4:3 screen's left
 * edge to two title pixels in. The side fill would carry it out across the
 * whole left side, so on their frames it is hidden (NV2A_WIDE_HUD_STRIP,
 * nv2a_wide_fill_strip): the picture's top six title rows, from the
 * screen's left edge to WIDE_MENU_STRIP title pixels into the 4:3 picture,
 * take the colour of the row under them, the band's. The graffiti menu's
 * strip runs on as a gap over its dark panel, two title rows tall, through
 * which the rectangles behind the panel show, to the panel's right edge,
 * 120 title pixels in: its strip is hidden as far as
 * WIDE_MENU_STRIP_GRAFFITI, and the panel goes up to the top. At 4:3
 * nothing is hidden: the console draws them. */
#define WIDE_MENU_STRIP          8u
#define WIDE_MENU_STRIP_GRAFFITI 121u

/* The graffiti menu draws only on its pass 9 (its draw's one argument), and
 * then only in its states 4 and 8 (+0x1C0, 32 bits). */
#define WIDE_GRAFFITI_PASS  9u
#define WIDE_GRAFFITI_STATE 0x1C0u
/* The loading screen draws only while +0x98 (32 bits) is 1 and the byte at
 * +0xA4 has 0x02 clear. */
#define WIDE_LOADING_SHOWN  0x98u
#define WIDE_LOADING_FLAGS  0xA4u

extern const WidePlainMenu widescreen_menus_plain[WIDE_PLAINS];

/* The plain menus' marks' piece numbers, one each, for the renderer's log. */
#define WIDE_MENU_PIECE_FILL0     0xD0u
#define WIDE_MENU_PIECE_FILL(p)   (WIDE_MENU_PIECE_FILL0 + (unsigned)(p))

/* The plain menu's draw: plain (WIDE_PLAIN_...) is about to draw, its object
 * at self, with pass its draw's argument (for the graffiti menu; 0 for the
 * others), on (the scene is widened) and D3D has a device: if it draws on
 * this call, its frame's mark is written, as widescreen_hud_push writes the
 * HUD's. The first time, a line in the log names it. A menu whose object
 * isn't of its class isn't marked, and the log says so once. Returns
 * whether the mark was written: the menu draws, for its moving rectangles
 * (widescreen_menus_sliders_begin), until widescreen_menus_fill_end. */
int widescreen_menus_fill(uint8_t *mem, uint32_t device, int plain, uint32_t self, uint32_t pass,
                          int on, widescreen_hud_space_fn make_space);
void widescreen_menus_fill_end(int began);

/* The moving rectangles: an animated model's nodes that its animation moves
 * along x alone. The model's nodes are an array (the 32-bit pointer at
 * model +0x08, as many as the 32 bits at +0x1C, WIDE_NODE_SIZE apart), a
 * tree through each node's first child (+0x64) and next sibling (+0x68),
 * which the pose walks depth first, a node before its children and its
 * children before its next sibling; the animation has its frame count (16
 * bits, signed, at +2) and a pointer to its tracks (+4), one
 * WIDE_TRACK_SIZE record a node in the walk's order: flags (a byte: 0x1 the
 * translation keyed, 0x10 the rotation, 0x4 the scale) and a pointer to the
 * translation's keys (+4), x, y and z floats a frame. A node slides when it
 * has a mesh (+0, not 0), only its translation is keyed, and its keys' y
 * and z are the same in every frame while x changes. A model with no such
 * node is looked at again by a wider rule (the save and load menu's and
 * the options menu's rectangles weren't found by the first): a node with a
 * mesh slides when its translation is keyed with y and z the same in every
 * frame and x changing, whatever else is keyed, or when one of its
 * ancestors slides so and its own keys, if any, don't move it in y or z. */
#define WIDE_MODEL_NODES      0x08u
#define WIDE_MODEL_NODE_COUNT 0x1Cu
#define WIDE_NODE_SIZE        0x6Cu
#define WIDE_NODE_MESH        0x00u
#define WIDE_NODE_CHILD       0x64u
#define WIDE_NODE_SIBLING     0x68u
#define WIDE_ANIM_FRAMES      0x02u
#define WIDE_ANIM_TRACKS      0x04u
#define WIDE_TRACK_SIZE       0x10u
#define WIDE_TRACK_MOVE       0x04u
#define WIDE_MENU_SLIDERS_MAX 32u
/* A band of moving rectangles has nine to twelve; a model with fewer moving
 * nodes than this, one, is a knob or a cursor (the options menu's volume
 * knobs, one node each), left where the game draws it: stretched across, a
 * knob landed past its scale's ends. */
#define WIDE_MENU_SLIDERS_MIN 2u
/* How many of a plain menu's animated models the log names, and how many of
 * each one's nodes it lists, so the log says which parts are stretched, and
 * why a model's rectangles aren't found. */
#define WIDE_MENU_MODELS_TOLD 4u
#define WIDE_MENU_NODES_TOLD  48u

/* The models' draw, for model drawn with animation anim (its first
 * argument; 0 for none), on guest memory below top: while a plain menu
 * draws (widescreen_menus_fill), the model's sliding nodes are found, with
 * the still node that carries a chain of them, for widescreen_menus_slider,
 * until widescreen_menus_sliders_end. Returns whether any were. A node found
 * that slides by its own keys and is carried by another found node is a
 * link, and it, the one that carries it, and what either carries are a
 * chain; the node that carries a chain's first is found too, with what it
 * carries, when it has a mesh, isn't the model's first node or one of its
 * siblings, and carries no node found but the chain's: that node is the
 * chain's anchor, and it and what it carries are marked to be stretched
 * about its origin (widescreen_menus_mark_flags). A model whose nodes or
 * animation don't read as above (a pointer out of memory, a tree that isn't
 * one) has none. For each of a plain menu's first WIDE_MENU_MODELS_TOLD
 * models, a line in the log says how many, by which rule, and how many are
 * in a chain, and lists the model's nodes (up to WIDE_MENU_NODES_TOLD):
 * their depth, mesh, track flags, how their keys move, and which are
 * stretched with the band, in a chain, or carrying one. A model with fewer
 * than WIDE_MENU_SLIDERS_MIN found has none, and its line says so. A plain
 * menu whose rectangles don't go across
 * (WidePlainMenu.slides 0: the music menu, whose dark yellow covers the
 * sides, where stretched they only moved faster) has none, and the log
 * says so once. The practice menu's rectangles go across: left as drawn,
 * they cross the column its sides are filled from, and the sides flicker
 * (08:36 UTC: "the flickering is still also present in roboboys "ill
 * teach ya the basics" menu"). */
int widescreen_menus_sliders_begin(const uint8_t *mem, uint32_t top, uint32_t model,
                                   uint32_t anim);
void widescreen_menus_sliders_end(int began);

/* The node functions (the pose's, one for each way of drawing a node), for
 * node of model, from the function at via: NV2A_WIDE_HUD_OVER for a sliding
 * node of the model the sliders were found for (its piece in *piece, its
 * mark's flags for widescreen_menus_mark_flags), else NV2A_WIDE_HUD_NONE.
 * The first time a node function draws one, a line in the log names it. */
int widescreen_menus_slider(uint32_t model, uint32_t node, uint32_t via, unsigned *piece);

/* The title (class 0x1CCEF8): its draw's state (+0x44, 32 bits) and its
 * pack of models (a pointer at +0x7C; model 0 at the pack's +0, model 1 at
 * +4). */
#define WIDE_TITLE_CLASS     0x001CCEF8u
#define WIDE_TITLE_STATE     0x44u
#define WIDE_TITLE_PACK      0x7Cu
#define WIDE_TITLE_LOGO      0x00u    /* the logo at the bottom right, in the pack */
#define WIDE_TITLE_PRESS     0x04u    /* the top-left "please press start", in the pack */
#define WIDE_TITLE_WIDE_FROM 3u       /* the first "please press start" */
#define WIDE_TITLE_CITY      6u       /* the city, with the two above */
/* How many of the title's changes of state the log names, with the time. */
#define WIDE_TITLE_STATES_TOLD 40u

/* The title's draw: the object at self draws now, in its state. Not of the
 * title's class, nothing. The first WIDE_TITLE_STATES_TOLD times its state
 * changes, a line in the log says to which, and when (now, in seconds). */
void widescreen_menus_title(uint8_t *mem, uint32_t self, double now);

/* The title's draw, widened (on): whether the bars it draws now go across,
 * between marks (NV2A_WIDE_HUD_BARS, piece WIDE_MENU_PIECE_BARS): the object
 * at self is the title, in any state but the city's. The first time, a line
 * in the log says so. */
int widescreen_menus_title_bars(uint8_t *mem, uint32_t self, int on);

/* With JSRF_WIDE_TITLE=0, where the title isn't widened from its first frame
 * (as in test build menus5): whether the title's scene is widened, as it
 * drew within WIDE_MENU_RECENT of now, in a state from WIDE_TITLE_WIDE_FROM
 * on. The first time, a line in the log says so. */
int widescreen_menus_title_wide(double now);

/* The title's state 6 draw: the object at self draws its two models, on
 * (the scene is widened); the second, the top-left "please press start",
 * goes to the left edge, and the first, the logo, to the right edge
 * (widescreen_menus_part), until widescreen_menus_title_press_end. Returns
 * whether it began. */
int widescreen_menus_title_press_begin(uint8_t *mem, uint32_t self, int on);
void widescreen_menus_title_press_end(int began);

/* The result screens: YOU WIN, CLEARED, GAME OVER and YOU LOSE, each over
 * the character's dance, each part moved whole to the right edge, as a HUD
 * piece is (spriteice, 8 November 2026, 09:05 UTC: "Can we also move the banner
 * in the "you win" and "cleared" screen to the right accordingly, it blocks
 * the characters dance. The game over and you lose screens will also follow
 * this, including the return to title and restart icons, and the small
 * static banner."). The banner is a HUD sign, moved by the signs' draw
 * (widescreen_menus_sign; spriteice's menus9 log has the result flow's signs on
 * those screens, and no draw of class 0x1D2538, the part menus9 took for
 * the banner). The other parts are each its object's draw, its class's
 * slot +0xC, this in ecx, one argument, the pass (ret 4), drawing on some
 * passes only (widescreen-3b/menus/search-7, section 5; run-9, part 2):
 *   - the choice, sub_0010F670 (class 0x1D24F0), on passes 0 and 8: the
 *     return to title and restart icons and the grey static bar under them,
 *     and the bar's label, queued, right-aligned at x 580, which is drawn at
 *     once between the marks (widescreen_menus_result_text) so it moves with
 *     them;
 *   - the race time under the banner, sub_0010EC10 (class 0x1D23F0), on
 *     passes 0, 3, 5 and 8: its digits, 2D quads, placed by its mode
 *     (+0x160, 32 bits). The same class draws the race HUD's timer at the
 *     top of the screen, which stays as it is (spriteice, 09:06 UTC: "Make sure
 *     the race time ONLY changes in this context, I do not want it moved
 *     anywhere else"): only the result flow's (sub_00073500) modes, 4 and 6,
 *     move; the race HUD's (sub_000FF390) are 2 and 3, and the log says
 *     once that the timer is left as it is.
 * Nothing else moves with them: another object's draw, nor the fade. */
enum { WIDE_RESULT_CHOICE, WIDE_RESULT_TIME, WIDE_RESULTS };

typedef struct {
    uint32_t va;               /* its draw, its class's slot +0xC */
    uint32_t vtable;           /* its class */
    const char *name;          /* for the log */
    unsigned piece;            /* its marks' piece (WIDE_MENU_PIECE_CHOICE, ...) */
    uint32_t passes;           /* the passes it draws on, a bit each */
    uint32_t modes;            /* 0, or the modes (+WIDE_RESULT_MODE) that move, a bit each */
} WideResult;

extern const WideResult widescreen_menus_results[WIDE_RESULTS];

#define WIDE_RESULT_MODE   0x160u /* the race time's mode (32 bits) */

/* The race time on a result screen goes further right than the rest, so
 * that its right end comes under the banner's (spriteice, 8 November 2026, 11:20
 * UTC: "The timer has not moved enough, it should be at the edge of the
 * screen (the same distance from the edge that the banner is in 4:3)"):
 * moved with the others, it kept its 4:3 place, 164 title pixels short of
 * the banner's end in mode 6 and 151 in mode 4 (spriteice's menus9 shots, at
 * 21:9). Its digits and separators are placed by its mode's rows of four
 * tables of floats, one after another (widescreen-3b/menus/menus10/search-9,
 * C): the two digits of each field, minutes, seconds and hundredths, then
 * the two separators, a row of two (x, y) a mode. It has no x of its own:
 * +0x14C is a y, 40 in a split screen, and with +0x44's
 * WIDE_RESULT_TIME_SPLIT an (x, y) from the split screen's quarters is
 * added. So for its draw its mode's rows are moved right by what lines the
 * hundredths' last digit's ink (WIDE_RESULT_INK past its place; the
 * hundredths are always 16 by 19) up with the banner's band's end
 * (WIDE_RESULT_BANNER_RIGHT, 4:3 title pixels, the same for the four
 * banners), and put back after, word for word. Only the result flow makes
 * modes 4 and 6; the race HUD's timer, in modes 2 and 3, reads other rows.
 * A time placed for a split screen isn't moved further: the log says so
 * once. The banner's end and the ink are measured from the menus9 shots
 * (the band 158 to 553, the time 248 to 389 in mode 6). */
#define WIDE_RESULT_PLACES         0x00221430u /* the race time's places: four tables of floats */
#define WIDE_RESULT_TABLES         4u          /* minutes, seconds, hundredths, the separators */
#define WIDE_RESULT_HUNDREDTHS     2u          /* the hundredths' table */
#define WIDE_RESULT_MODES          7u          /* a row each */
#define WIDE_RESULT_DIGITS         2u          /* two (x, y) a row */
#define WIDE_RESULT_PLACE          8u          /* one (x, y) */
#define WIDE_RESULT_ROW            (WIDE_RESULT_DIGITS * WIDE_RESULT_PLACE)
#define WIDE_RESULT_TABLE          (WIDE_RESULT_MODES * WIDE_RESULT_ROW)
#define WIDE_RESULT_TIME_Y         0x14Cu      /* its y offset, a float: 0, or 40 in a split screen */
#define WIDE_RESULT_TIME_FLAGS     0x44u
#define WIDE_RESULT_TIME_SPLIT     0x80000000u /* a split screen quarter's (x, y) added */
#define WIDE_RESULT_INK            12.0f       /* a hundredths digit's ink past its place */
#define WIDE_RESULT_BANNER_RIGHT   553.0f      /* the banner's band's right end, 4:3 */
#define WIDE_RESULT_MOVE_MAX       320.0f      /* more than half the 4:3 width: not a place */

/* The race time at self, which widescreen_menus_result has begun on: how
 * many title pixels to move its mode's places right for this draw, so the
 * hundredths' last digit's ink ends under the banner's; 0 when its mode
 * isn't a result screen's (4 or 6), it already does, it is placed for a
 * split screen, or its place can't be read (the log says so once). The first time for each mode, a line in the log gives its
 * end and the move. */
float widescreen_menus_result_time_move(const uint8_t *mem, uint32_t self);

/* Its places as they were, to put back (widescreen_menus_result_time_end). */
typedef struct {
    uint32_t mode;
    uint32_t x[WIDE_RESULT_TABLES * WIDE_RESULT_DIGITS];
} WideTimePlaces;

/* Before the race time's draw: its mode's places moved right by
 * widescreen_menus_result_time_move's, each x kept in saved. Returns
 * whether they were moved, for widescreen_menus_result_time_end, after the
 * draw, which puts each back as it was. */
int widescreen_menus_result_time_begin(uint8_t *mem, uint32_t self, WideTimePlaces *saved);
void widescreen_menus_result_time_end(uint8_t *mem, const WideTimePlaces *saved, int began);

/* A result screen's part is about to draw: which (WIDE_RESULT_...), its
 * object at self, with pass its draw's argument, on (the scene is
 * widened): whether it draws on this call (pass one of its passes), its
 * object is of its class and, for the race time, in one of the result
 * screen's modes, for marks around the whole draw (its piece,
 * NV2A_WIDE_HUD_RIGHT), until widescreen_menus_result_end. The first time
 * for each, a line in the log says so; an object of another class isn't
 * moved, nor the race HUD's timer, and the log says so once for each. */
int widescreen_menus_result(const uint8_t *mem, int which, uint32_t self, uint32_t pass, int on);
void widescreen_menus_result_end(int which, int began);

/* Text queued now (the font call, sub_0003D5D0): whether it is the result
 * screen choice's label, drawn while the choice draws, to draw at once,
 * between the choice's marks, so it moves with the icons and the bar. */
int widescreen_menus_result_text(void);

#endif
