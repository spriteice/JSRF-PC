/* The game's menus laid out for widescreen: the game's half
 * (widescreen_menus.h). Plain C on guest memory, so a test links it
 * (tests/widescreen_menus). */
#include "widescreen_menus.h"
#include "nv2a_wide.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define L NV2A_WIDE_HUD_LEFT
#define R NV2A_WIDE_HUD_RIGHT
#define A NV2A_WIDE_HUD_ACROSS
#define E WIDE_MENU_EXTEND

/* The street pause menu (class 0x1D2A00), in the order its draw draws them.
 * Its fields as the read of the generated code found them, and each model
 * as spriteice's draw log of the menu showed it (widescreen-3b/menus/layout-1).
 * +0x64 is empty. */
static const WideMenuPart street_parts[] = {
    { 0x7C, E, "the backdrop and its grids (model 0x80)" },
    { 0x44, A, "the grey top bar (model 3)" },
    { 0x48, R, "the bottom label's grey bar (model 4)" },
    { 0x70, A, "the bands, their lines and marks (model 0x6B)" },
    { 0x50, R, "the CHANGE CHARACTERS button (model 6)" },
    { 0x58, R, "the SAVE button (model 8)" },
    { 0x5C, R, "the GG notebook button (model 9)" },
    { 0x4C, R, "the RETURN TO GAME button (model 5)" },
    { 0x6C, R, "the TOTAL panel (model 0x9D)" },
    { 0x68, L, "the counts box (model 0x9C)" },
    { 0x90, L, "ROTATE MAP's button (model 0x100)" },
    { 0x80, L, "CHANGE MAP's L (model 0x101)" },
    { 0x84, L, "CHANGE MAP's R (model 0x102)" },
};

/* The other pause menu (class 0x1D2AA8), a race's or a mission's: the same
 * models where the two share them. +0x58 is empty. */
static const WideMenuPart other_parts[] = {
    { 0x68, E, "the backdrop and its grids (model 0x80)" },
    { 0x44, A, "the grey top bar (model 3)" },
    { 0x48, R, "the bottom label's grey bar (model 4)" },
    { 0x5C, A, "the bands, their lines and marks (model 0x6B)" },
    { 0x4C, R, "the RETURN TO GAME button (model 5)" },
    { 0x50, R, "the START OVER button (model 7)" },
    { 0x6C, L, "the mode's emblem (bank 0xB model 0x10)" },
    { 0x78, L, "ROTATE MAP's button (model 0x100)" },
};

/* The GG notebook (class 0x1D2B68), from the street pause menu: the pause
 * menus' own models, from their pack (widescreen-3b/menus/search-4 and
 * search-5). Its page (+0x9C, +0xBC, +0xA4, +0xA0, the stamp +0xC0 and the
 * rows at +0xA8) stays where it is. */
static const WideMenuPart notebook_parts[] = {
    { 0xD0, E, "the backdrop and its grids (model 0x80)" },
    { 0xC4, A, "the bands, their lines and marks (model 0x6B)" },
    { 0x98, R, "the grey block with Press the B Button (model 4)" },
};

/* The character select that saving a graffiti opens (class 0x1D2BB0), drawn
 * as the notebook is, from the same models (widescreen-3b/menus/search-5,
 * message 6). Its panel of characters (+0x50, model 0xD5) and the
 * characters on it (+0x54 on) stay where they are, with their names. */
static const WideMenuPart characters_parts[] = {
    { 0xDC, E, "the backdrop and its grids (model 0x80)" },
    { 0xD0, A, "the bands, their lines and marks (model 0x6B)" },
    { 0x4C, R, "the grey block with Press the B Button (model 4)" },
};

/* The race preview (class 0x1D2788), before a race or a chase mission, in
 * the order its draw (slot +0xC) draws them (widescreen-3b/menus/search-6):
 * the pause menus' models. +0x50 is never loaded. The emblem is drawn only
 * for two kinds of mission. */
static const WideMenuPart preview_parts[] = {
    { 0x60, E, "the backdrop and its grids (model 0x80)" },
    { 0x48, A, "the grey top bar (model 3)" },
    { 0x4C, R, "the grey bar with Press the A Button (model 4)" },
    { 0x54, A, "the bands, their lines and marks (model 0x6B)" },
    { 0x64, L, "the mode's emblem (bank 0xB model 0x10)" },
};

#undef L
#undef R
#undef A
#undef E

const WideMenu widescreen_menus[WIDE_MENUS] = {
    [WIDE_MENU_STREET_PAUSE] = { 0x001129E0u, 0x001D2A00u, "the street pause menu",
                                 street_parts, sizeof street_parts / sizeof street_parts[0],
                                 0.0f },
    [WIDE_MENU_OTHER_PAUSE]  = { 0x001133A0u, 0x001D2AA8u, "the other pause menu",
                                 other_parts, sizeof other_parts / sizeof other_parts[0],
                                 0.0f },
    [WIDE_MENU_NOTEBOOK]     = { 0x00115030u, 0x001D2B68u, "the GG notebook",
                                 notebook_parts, sizeof notebook_parts / sizeof notebook_parts[0],
                                 WIDE_MENU_NOTEBOOK_BLOCK_Y },
    [WIDE_MENU_CHARACTERS]   = { 0x00115A40u, 0x001D2BB0u, "the save graffiti's character select",
                                 characters_parts,
                                 sizeof characters_parts / sizeof characters_parts[0],
                                 WIDE_MENU_NOTEBOOK_BLOCK_Y },
    [WIDE_MENU_RACE_PREVIEW] = { 0x00111320u, 0x001D2788u, "the race preview",
                                 preview_parts, sizeof preview_parts / sizeof preview_parts[0],
                                 0.0f },
};

/* The plain menus (the side fill), each the only draw of its class, at its
 * slot +0xC, as the read of the generated code found them
 * (widescreen-3b/menus/search-2; the graffiti menu and the loading screen,
 * search-3 to search-5). */
const WidePlainMenu widescreen_menus_plain[WIDE_PLAINS] = {
    [WIDE_PLAIN_SAVE]       = { 0x0011DD10u, 0x001D8CD0u, "the save and load menu", WIDE_FILL_ALWAYS, WIDE_MENU_STRIP, 1 },
    [WIDE_PLAIN_GARAGE]     = { 0x0011C070u, 0x001D8638u, "Roboy's garage menu", WIDE_FILL_ALWAYS, WIDE_MENU_STRIP, 1 },
    [WIDE_PLAIN_SYSTEM]     = { 0x0011FEB0u, 0x001D9090u, "the garage's system menu", WIDE_FILL_ALWAYS, WIDE_MENU_STRIP, 1 },
    [WIDE_PLAIN_OPTIONS]    = { 0x0011CF10u, 0x001D8898u, "the garage's options menu", WIDE_FILL_ALWAYS, WIDE_MENU_STRIP, 1 },
    [WIDE_PLAIN_PRACTICE]   = { 0x0011D740u, 0x001D8C90u, "the garage's practice menu", WIDE_FILL_ALWAYS, WIDE_MENU_STRIP, 1 },
    [WIDE_PLAIN_MUSIC]      = { 0x0011F7D0u, 0x001D8F88u, "the garage's music menu", WIDE_FILL_ALWAYS, WIDE_MENU_STRIP, 0 },
    [WIDE_PLAIN_TEST_RUN]   = { 0x0011C750u, 0x001D8798u, "the garage's test run menu", WIDE_FILL_ALWAYS, WIDE_MENU_STRIP, 1 },
    [WIDE_PLAIN_COURSE]     = { 0x0011A490u, 0x001D7C20u, "the test run's course screen", WIDE_FILL_ALWAYS, WIDE_MENU_STRIP, 1 },
    [WIDE_PLAIN_CHARACTERS] = { 0x0011AC50u, 0x001D7D00u, "the garage's character select", WIDE_FILL_ALWAYS, WIDE_MENU_STRIP, 1 },
    [WIDE_PLAIN_GRAFFITI]   = { 0x00127080u, 0x001DA300u, "the garage's graffiti menu", WIDE_FILL_GRAFFITI, WIDE_MENU_STRIP_GRAFFITI, 1 },
    [WIDE_PLAIN_LOADING]    = { 0x00066550u, 0x001CB0A8u, "the loading screen", WIDE_FILL_LOADING, 0, 1 },
    [WIDE_PLAIN_VS]         = { 0x00071100u, 0x001CCA80u, "VS mode's setup screen", WIDE_FILL_ALWAYS, 0, 0 },
};

/* The menu drawing now and its object, or -1. */
static int s_menu = -1;
static uint32_t s_self;
/* Whether the area name's sign being drawn is a menu's (the street pause
 * menu's, the notebook's or the race preview's), not one on its own or the
 * banner under the arrow to another area. */
static int s_sign_in_menu;
/* When the street pause menu and the notebook last began, for the area
 * name's sign, and whether they have. */
static double s_street_at, s_notebook_at;
static int s_street_seen, s_notebook_seen;
/* The other pause menu that began last, for its child, and when; when the
 * race preview last began, for its header, and whether it has. */
static uint32_t s_other;
static double s_other_at;
static double s_preview_at;
static int s_preview_seen;
/* When either pause menu last began, for the ticker, and whether one has,
 * and whether the ticker draws now. */
static double s_menu_at;
static int s_menu_seen;
static int s_ticker;

static uint32_t guest32(const uint8_t *mem, uint32_t va)
{
    return *(const volatile uint32_t *)(mem + va);
}

static const char *side_name(int side)
{
    return side == NV2A_WIDE_HUD_LEFT ? "the left edge" : side == NV2A_WIDE_HUD_RIGHT
         ? "the right edge" : side == WIDE_MENU_EXTEND
         ? "two halves, moved out by whole cells" : "across the width";
}

int widescreen_menus_begin(uint8_t *mem, int menu, uint32_t self, int on, double now)
{
    static unsigned char told[WIDE_MENUS], told_class[WIDE_MENUS];
    const WideMenu *m;
    if (menu < 0 || menu >= WIDE_MENUS || !self) return 0;
    m = &widescreen_menus[menu];
    if (guest32(mem, self) != m->vtable) {
        if (!told_class[menu]) {
            told_class[menu] = 1;
            fprintf(stderr, "[WIDE] %s (sub_%08X) drew an object of class %08X, not %08X: "
                    "left as it is\n", m->name, m->va, guest32(mem, self), m->vtable);
        }
        return 0;
    }
    if (menu == WIDE_MENU_STREET_PAUSE) { s_street_at = now; s_street_seen = 1; }
    if (menu == WIDE_MENU_NOTEBOOK) { s_notebook_at = now; s_notebook_seen = 1; }
    if (menu == WIDE_MENU_OTHER_PAUSE) { s_other = self; s_other_at = now; }
    if (menu == WIDE_MENU_RACE_PREVIEW) { s_preview_at = now; s_preview_seen = 1; }
    if (menu != WIDE_MENU_NOTEBOOK && menu != WIDE_MENU_CHARACTERS) {
        s_menu_at = now;
        s_menu_seen = 1;
    }
    if (!on) return 0;
    if (!told[menu]) {
        told[menu] = 1;
        fprintf(stderr, "[WIDE] %s (sub_%08X): laid out for the wide screen\n", m->name, m->va);
    }
    s_menu = menu;
    s_self = self;
    return 1;
}

void widescreen_menus_end(int began)
{
    if (began) s_menu = -1;
}

/* The title's state 6 draw: its top-left "please press start" model and
 * its logo, while it draws. */
static uint32_t s_title_press, s_title_logo;

int widescreen_menus_part(uint8_t *mem, uint32_t model, unsigned *piece)
{
    static unsigned char told[WIDE_MENUS][WIDE_MENU_PARTS_MAX];
    static int told_press, told_logo;
    const WideMenu *m;
    unsigned i;
    if (s_title_press && model == s_title_press) {
        *piece = WIDE_MENU_PIECE_TITLE;
        if (!told_press) {
            told_press = 1;
            fprintf(stderr, "[WIDE] menu part %u, the title's top-left \"please press start\" "
                    "(its pack's model 1): marked for the left edge\n", *piece);
        }
        return NV2A_WIDE_HUD_LEFT;
    }
    if (s_title_logo && model == s_title_logo) {
        *piece = WIDE_MENU_PIECE_LOGO;
        if (!told_logo) {
            told_logo = 1;
            fprintf(stderr, "[WIDE] menu part %u, the title's logo at the bottom right (its "
                    "pack's model 0): marked for the right edge\n", *piece);
        }
        return NV2A_WIDE_HUD_RIGHT;
    }
    if (s_menu < 0 || !model) return NV2A_WIDE_HUD_NONE;
    m = &widescreen_menus[s_menu];
    for (i = 0; i < m->n_parts && i < WIDE_MENU_PARTS_MAX; i++) {
        if (guest32(mem, s_self + m->parts[i].field) != model) continue;
        *piece = WIDE_MENU_PIECE(s_menu, i);
        if (!told[s_menu][i]) {
            told[s_menu][i] = 1;
            fprintf(stderr, "[WIDE] menu part %u in %s, %s (+0x%02X): marked for %s\n",
                    *piece, m->name, m->parts[i].name, m->parts[i].field,
                    side_name(m->parts[i].side));
        }
        return m->parts[i].side;
    }
    return NV2A_WIDE_HUD_NONE;
}

int widescreen_menus_text_side(float x, float y, unsigned *piece)
{
    static unsigned char told[2];
    int left;
    if (s_menu < 0 || x != x || y != y) return NV2A_WIDE_HUD_NONE;
    if (widescreen_menus[s_menu].block_y > 0.0f) {
        if (!(y >= widescreen_menus[s_menu].block_y)) return NV2A_WIDE_HUD_NONE;
        left = 0;
    } else {
        left = x < WIDE_MENU_TEXT_LEFT_OF;
    }
    *piece = left ? WIDE_MENU_PIECE_TEXT_L : WIDE_MENU_PIECE_TEXT_R;
    if (!told[left]) {
        told[left] = 1;
        fprintf(stderr, "[WIDE] menu part %u in %s, the text on the %s: drawn at once, at %s\n",
                *piece, widescreen_menus[s_menu].name, left ? "left" : "right",
                side_name(left ? NV2A_WIDE_HUD_LEFT : NV2A_WIDE_HUD_RIGHT));
    }
    return left ? NV2A_WIDE_HUD_LEFT : NV2A_WIDE_HUD_RIGHT;
}

/* A result screen's banner (widescreen_menus_sign): the sign at sign, its
 * flags flags, goes to the right edge; the first time, and for each of its
 * first WIDE_MENU_SIGNS_TOLD types, a line in the log says so. */
static int result_sign(const uint8_t *mem, uint32_t sign, uint32_t flags, unsigned *piece)
{
    static int told;
    static uint32_t types[WIDE_MENU_SIGNS_TOLD];
    static unsigned n_types;
    uint32_t type = guest32(mem, sign + WIDE_MENU_SIGN_TYPE);
    unsigned k;
    if (!told) {
        told = 1;
        fprintf(stderr, "[WIDE] menu part %u, a result screen's banner (a sign, flags %08X): "
                "moved to the right edge\n", WIDE_MENU_PIECE_RESULT, flags);
    }
    for (k = 0; k < n_types && types[k] != type; k++) {}
    if (k == n_types && n_types < WIDE_MENU_SIGNS_TOLD) {
        types[n_types++] = type;
        fprintf(stderr, "[WIDE] a result screen's banner, type %u (+0x%02X), flags %08X\n",
                type, WIDE_MENU_SIGN_TYPE, flags);
    }
    *piece = WIDE_MENU_PIECE_RESULT;
    return NV2A_WIDE_HUD_RIGHT;
}

/* Whether the sign at sign, with flags, is the banner with an area's name
 * under the arrow to another area: no flags, and its parent, a pointer
 * within top, of WIDE_MENU_BANNER_CLASS. */
static int area_banner(const uint8_t *mem, uint32_t top, uint32_t sign, uint32_t flags)
{
    uint32_t parent;
    if (flags != 0) return 0;
    parent = guest32(mem, sign + WIDE_MENU_SIGN_PARENT);
    return parent && parent % 4u == 0 && top >= 4u && parent <= top - 4u
        && guest32(mem, parent) == WIDE_MENU_BANNER_CLASS;
}

int widescreen_menus_sign(uint8_t *mem, uint32_t top, uint32_t sign, double now,
                          unsigned *piece)
{
    enum { IN_STREET, IN_NOTEBOOK, ON_ITS_OWN, IN_PREVIEW, UNDER_ARROW, PLACES };
    static const char *const where[PLACES] = {
        [IN_STREET] = "in the street pause menu", [IN_NOTEBOOK] = "in the GG notebook",
        [ON_ITS_OWN] = "on its own, with no menu drawing", [IN_PREVIEW] = "in the race preview",
        [UNDER_ARROW] = "under the arrow to another area",
    };
    static unsigned char told[PLACES];
    static uint32_t told_flags[WIDE_MENU_SIGNS_TOLD];
    static unsigned n_told;
    int street = s_street_seen && now - s_street_at <= WIDE_MENU_RECENT;
    int notebook = s_notebook_seen && now - s_notebook_at <= WIDE_MENU_RECENT;
    int preview = s_preview_seen && now - s_preview_at <= WIDE_MENU_RECENT;
    uint32_t flags;
    int which, banner;
    unsigned k;
    if (!sign) return NV2A_WIDE_HUD_NONE;
    flags = guest32(mem, sign + WIDE_MENU_SIGN_FLAGS);
    if (flags == WIDE_MENU_RESULT_SIGN_FLAGS || flags == WIDE_MENU_RESULT_SIGN_FLAGS_CHOICE)
        return result_sign(mem, sign, flags, piece);
    banner = area_banner(mem, top, sign, flags);
    if (!(flags & WIDE_MENU_AREA_SIGN_FLAG) && !banner) {
        for (k = 0; k < n_told; k++)
            if (told_flags[k] == flags) break;
        if (k == n_told && n_told < WIDE_MENU_SIGNS_TOLD) {
            told_flags[n_told++] = flags;
            fprintf(stderr, "[WIDE] a sign with flags %08X (class %08X): left where it is\n",
                    flags, guest32(mem, sign));
        }
        return NV2A_WIDE_HUD_NONE;
    }
    which = banner ? UNDER_ARROW : street ? IN_STREET : notebook ? IN_NOTEBOOK
          : preview ? IN_PREVIEW : ON_ITS_OWN;
    s_sign_in_menu = which != ON_ITS_OWN && which != UNDER_ARROW;
    if (!told[which]) {
        char parent[40] = "";
        told[which] = 1;
        if (banner)
            snprintf(parent, sizeof parent, ", its parent of class %08X", WIDE_MENU_BANNER_CLASS);
        fprintf(stderr, "[WIDE] menu part %u %s, the area name (a sign, flags %08X%s): "
                "marked for the left edge\n", WIDE_MENU_PIECE_SIGN, where[which], flags, parent);
    }
    *piece = WIDE_MENU_PIECE_SIGN;
    return NV2A_WIDE_HUD_LEFT;
}

int widescreen_menus_child(uint8_t *mem, uint32_t self, double now)
{
    static unsigned char told[2];
    int preview;
    if (!self) return 0;
    if (s_other && now - s_other_at <= WIDE_MENU_RECENT
        && guest32(mem, s_other) == widescreen_menus[WIDE_MENU_OTHER_PAUSE].vtable
        && guest32(mem, s_other + WIDE_MENU_FIRST_CHILD) == self)
        preview = 0;
    else if (s_preview_seen && now - s_preview_at <= WIDE_MENU_RECENT
             && guest32(mem, self) == WIDE_MENU_HEADER_CLASS)
        preview = 1;
    else
        return 0;
    if (!told[preview]) {
        told[preview] = 1;
        fprintf(stderr, "[WIDE] menu part %u in %s, the header (%s, class %08X): marked for the "
                "left edge\n", WIDE_MENU_PIECE_CHILD,
                preview ? "the race preview" : "the other pause menu",
                preview ? "the race's name" : "its child", guest32(mem, self));
    }
    return 1;
}

int widescreen_menus_ticker_begin(uint8_t *mem, uint32_t self, int on, double now)
{
    static unsigned char told[2];
    uint32_t cls;
    int preview;
    if (!on || !self || !s_menu_seen || !(now - s_menu_at <= WIDE_MENU_RECENT)) return 0;
    cls = guest32(mem, self);
    if (cls != WIDE_MENU_TICKER_CLASS && cls != WIDE_MENU_PREVIEW_TICKER_CLASS) return 0;
    preview = cls == WIDE_MENU_PREVIEW_TICKER_CLASS;
    if (!told[preview]) {
        told[preview] = 1;
        fprintf(stderr, "[WIDE] menu part %u, the %s ticker (class %08X): across the whole "
                "width\n", WIDE_MENU_PIECE_TICKER,
                preview ? "race preview's" : "pause menus'", cls);
    }
    s_ticker = 1;
    return 1;
}

void widescreen_menus_ticker_end(int began)
{
    if (began) s_ticker = 0;
}

int widescreen_menus_ticker_x(float x, float s, float *wide_x, unsigned *piece)
{
    if (!s_ticker || x != x || !(s > 0.0f) || !(s < 1.0f)) return NV2A_WIDE_HUD_NONE;
    *wide_x = 320.0f + (x - 320.0f) / s;
    *piece = WIDE_MENU_PIECE_TICKER;
    return NV2A_WIDE_HUD_WHOLE;
}

int widescreen_menus_map(int mode, int on)
{
    static unsigned char told[3];
    if (!on || mode < 0 || mode > 2) return 0;
    if (!told[mode]) {
        told[mode] = 1;
        if (mode == 2)
            fprintf(stderr, "[WIDE] menu part %u, the race preview's course map (sub_%08X): "
                    "shown in the light grid, not over the dark grid at the top\n",
                    WIDE_MENU_PIECE_COURSE, 0x00112470u);
        else
            fprintf(stderr, "[WIDE] menu part %u, the pause map (mode %d, sub_%08X): shown in "
                    "the light grid, not over the dark grid at the top, its yellow lines "
                    "carried to the screen's edges\n",
                    WIDE_MENU_PIECE_MAP, mode, mode ? 0x001137B0u : 0x00114A80u);
    }
    return 1;
}

/* Whether menu has a backdrop drawn in two copies (WIDE_MENU_EXTEND). */
static int menu_extends(unsigned menu)
{
    unsigned i;
    if (menu >= WIDE_MENUS) return 0;
    for (i = 0; i < widescreen_menus[menu].n_parts; i++)
        if (widescreen_menus[menu].parts[i].side == WIDE_MENU_EXTEND) return 1;
    return 0;
}

/* The mark's flags of the moving rectangle widescreen_menus_slider last
 * found (see s_sliders, below). */
static unsigned s_slider_flags;

unsigned widescreen_menus_mark_flags(unsigned piece, int side)
{
    if (side == NV2A_WIDE_HUD_OVER && piece == WIDE_MENU_PIECE_SLIDERS) return s_slider_flags;
    if (side != NV2A_WIDE_HUD_LEFT && side != NV2A_WIDE_HUD_RIGHT) return 0u;
    if (piece >= WIDE_MENU_PIECE0 && piece < WIDE_MENU_PIECE(WIDE_MENUS, 0))
        return menu_extends((piece - WIDE_MENU_PIECE0) / WIDE_MENU_PARTS_MAX)
             ? NV2A_WIDE_HUD_ON_GRID : 0u;
    if (piece == WIDE_MENU_PIECE_TEXT_L || piece == WIDE_MENU_PIECE_TEXT_R)
        return s_menu >= 0 && menu_extends((unsigned)s_menu) ? NV2A_WIDE_HUD_ON_GRID : 0u;
    if (piece == WIDE_MENU_PIECE_CHILD) return NV2A_WIDE_HUD_ON_GRID;
    if (piece == WIDE_MENU_PIECE_SIGN) return s_sign_in_menu ? NV2A_WIDE_HUD_ON_GRID : 0u;
    return 0u;
}

int widescreen_menus_mark(uint8_t *mem, uint32_t device, unsigned piece, int side,
                          widescreen_hud_space_fn make_space)
{
    if (!device || side == NV2A_WIDE_HUD_NONE || side == WIDE_MENU_EXTEND) return 0;
    return widescreen_hud_push(mem, device,
                               nv2a_wide_hud_mark(piece, side,
                                                  widescreen_menus_mark_flags(piece, side)),
                               make_space) != 0;
}

void widescreen_menus_unmark(uint8_t *mem, uint32_t device, unsigned piece, int marked,
                             widescreen_hud_space_fn make_space)
{
    if (!marked) return;
    widescreen_hud_push(mem, device, nv2a_wide_hud_mark(piece, NV2A_WIDE_HUD_NONE, 0),
                        make_space);
}

/* The plain menu drawing now, or -1, for its moving rectangles. */
static int s_fill = -1;

/* Whether the plain menu draws on this call (WidePlainMenu's when). */
static int fill_draws(const uint8_t *mem, const WidePlainMenu *m, uint32_t self, uint32_t pass)
{
    uint32_t state;
    switch (m->when) {
    case WIDE_FILL_GRAFFITI:
        state = guest32(mem, self + WIDE_GRAFFITI_STATE);
        return pass == WIDE_GRAFFITI_PASS && (state == 4u || state == 8u);
    case WIDE_FILL_LOADING:
        return guest32(mem, self + WIDE_LOADING_SHOWN) == 1u
            && !(mem[self + WIDE_LOADING_FLAGS] & 0x02u);
    default:
        return 1;
    }
}

int widescreen_menus_fill(uint8_t *mem, uint32_t device, int plain, uint32_t self, uint32_t pass,
                          int on, widescreen_hud_space_fn make_space)
{
    static unsigned char told[WIDE_PLAINS], told_class[WIDE_PLAINS];
    const WidePlainMenu *m;
    if (plain < 0 || plain >= WIDE_PLAINS || !self || !on || !device) return 0;
    m = &widescreen_menus_plain[plain];
    if (guest32(mem, self) != m->vtable) {
        if (!told_class[plain]) {
            told_class[plain] = 1;
            fprintf(stderr, "[WIDE] %s (sub_%08X) drew an object of class %08X, not %08X: "
                    "left as it is\n", m->name, m->va, guest32(mem, self), m->vtable);
        }
        return 0;
    }
    if (!fill_draws(mem, m, self, pass)) return 0;
    if (!widescreen_hud_push(mem, device,
                             nv2a_wide_hud_mark(WIDE_MENU_PIECE_FILL(plain), NV2A_WIDE_HUD_FILL, 0),
                             make_space))
        return 0;
    /* Its tan strip hidden, from this frame's flip. */
    if (m->strip
        && !widescreen_hud_push(mem, device, nv2a_wide_hud_mark(m->strip, NV2A_WIDE_HUD_STRIP, 0),
                                make_space))
        return 0;
    if (!told[plain]) {
        told[plain] = 1;
        fprintf(stderr, "[WIDE] %s (sub_%08X): its frames' sides filled, piece %u%s\n", m->name,
                m->va, WIDE_MENU_PIECE_FILL(plain),
                m->strip ? ", its tan strip hidden" : "");
    }
    s_fill = plain;
    return 1;
}

void widescreen_menus_fill_end(int began)
{
    if (began) s_fill = -1;
}

/* The moving rectangles of the model drawing now: its sliding nodes, and
 * the still nodes that carry a chain of them (keep_chains). As they're
 * found, each has the index of the nearest found node that carries it (-1
 * for none) in s_slider_up, whether its own keys slide it in s_slider_own,
 * and its place in the walk in s_slider_visit, for keep_chains. Those in a
 * chain, and the still ones added with a chain, are kept for the log too
 * (s_chain, s_carried). The still node that carries a chain is its anchor
 * (s_anchors), and it and what it carries are stretched about the anchor
 * (s_anchored). The last model and animation they were found for are kept,
 * as a menu draws the same ones every frame. The flags for the mark of the
 * node widescreen_menus_slider last found are in s_slider_flags (above), for
 * widescreen_menus_mark_flags: the node function's override marks the node
 * as soon as it's found. */
static uint32_t s_slider_model, s_slider_anim;
static uint32_t s_sliders[WIDE_MENU_SLIDERS_MAX];
static int s_slider_up[WIDE_MENU_SLIDERS_MAX];
static unsigned char s_slider_own[WIDE_MENU_SLIDERS_MAX];
static int s_slider_visit[WIDE_MENU_SLIDERS_MAX];
static unsigned s_n_sliders;
static uint32_t s_chain[WIDE_MENU_SLIDERS_MAX], s_carried[WIDE_MENU_SLIDERS_MAX];
static unsigned s_n_chain, s_n_carried;
static uint32_t s_anchors[WIDE_MENU_SLIDERS_MAX], s_anchored[WIDE_MENU_SLIDERS_MAX];
static unsigned s_n_anchors, s_n_anchored;
static int s_sliders_on;
/* The walk's first WIDE_MENU_VISITS nodes, in its order, for keep_chains:
 * each one's node, the place of the node that carries it (-1 for the
 * model's first and its siblings), its depth, whether it has a mesh, and
 * its index in s_sliders (-1 if it wasn't found). */
#define WIDE_MENU_VISITS 256u
static uint32_t s_visit_node[WIDE_MENU_VISITS];
static int s_visit_up[WIDE_MENU_VISITS], s_visit_found[WIDE_MENU_VISITS];
static unsigned char s_visit_depth[WIDE_MENU_VISITS], s_visit_mesh[WIDE_MENU_VISITS];
static unsigned s_n_visits;

static float guest_float(const uint8_t *mem, uint32_t va)
{
    float f;
    memcpy(&f, mem + va, sizeof f);
    return f;
}

/* How a track's translation keys move its node: not keyed (or not
 * readable), still, sliding (x changing, y and z the same in every frame),
 * or otherwise (y or z changing). */
enum { MOVE_NONE, MOVE_STILL, MOVE_SLIDE, MOVE_OTHER };

/* Whether a and b are the same key: exactly, or for the wider rule within a
 * thousandth (of the larger, past 1). */
static int same(float a, float b, int wide)
{
    float m = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
    return a == b || (wide && fabsf(a - b) <= 1e-3f * (m > 1.0f ? m : 1.0f));
}

/* The track at track (of frames frames): how its translation moves its node,
 * and its keys' x, y and z ranges in r (low and high each), when keyed. */
static int track_move(const uint8_t *mem, uint32_t top, uint32_t track, int frames, int wide,
                      float r[6])
{
    uint32_t keys;
    int f, c, moved = 0;
    if (!(mem[track] & 0x01u)) return MOVE_NONE;
    keys = guest32(mem, track + WIDE_TRACK_MOVE);
    if (keys < 0x10000u || keys > top || (uint32_t)frames * 12u > top - keys) return MOVE_NONE;
    for (c = 0; c < 3; c++) r[2 * c] = r[2 * c + 1] = guest_float(mem, keys + 4u * (uint32_t)c);
    for (f = 1; f < frames; f++)
        for (c = 0; c < 3; c++) {
            float v = guest_float(mem, keys + 12u * (uint32_t)f + 4u * (uint32_t)c);
            if (!same(v, guest_float(mem, keys + 4u * (uint32_t)c), wide)) moved |= 1 << c;
            if (v < r[2 * c]) r[2 * c] = v;
            if (v > r[2 * c + 1]) r[2 * c + 1] = v;
        }
    if (moved & 6) return MOVE_OTHER;
    return moved ? MOVE_SLIDE : MOVE_STILL;
}

/* Walks the model's tree, a node before its children and its children
 * before its next sibling, as the pose does: each node with its walk
 * index, its depth and its track. 1 when the tree and the animation read as
 * a model's and its, which the walk checks as it goes, else 0. */
typedef struct {
    uint32_t nodes, count, tracks;
    int frames;
} WideRig;

static int rig_read(const uint8_t *mem, uint32_t top, uint32_t model, uint32_t anim, WideRig *g)
{
    if (model < 0x10000u || anim < 0x10000u || model > top - 0x20u || anim > top - 8u) return 0;
    g->nodes = guest32(mem, model + WIDE_MODEL_NODES);
    g->count = guest32(mem, model + WIDE_MODEL_NODE_COUNT);
    g->frames = (int16_t)(mem[anim + WIDE_ANIM_FRAMES] | mem[anim + WIDE_ANIM_FRAMES + 1] << 8);
    g->tracks = guest32(mem, anim + WIDE_ANIM_TRACKS);
    return g->count && g->count <= 1024u && g->frames >= 2 && g->frames <= 4096
        && g->nodes >= 0x10000u && g->nodes <= top && g->count * WIDE_NODE_SIZE <= top - g->nodes
        && g->tracks >= 0x10000u && g->tracks <= top
        && g->count * WIDE_TRACK_SIZE <= top - g->tracks;
}

/* Whether node is one of the n in list. */
static int listed(const uint32_t *list, unsigned n, uint32_t node)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (list[i] == node) return 1;
    return 0;
}

/* Finds the model's sliding nodes into s_sliders, by the first rule (wide
 * 0) or the wider one (1), each with the nearest found node that carries it,
 * whether it slides by its own keys and its place in the walk (s_slider_up,
 * s_slider_own, s_slider_visit), and the walk's nodes (s_visit_*); with
 * tell, lists its nodes in the log instead, up to WIDE_MENU_NODES_TOLD, each
 * with what is done with it (as keep_chains left s_sliders, s_chain and
 * s_carried), and changes none of them. 1 if it reads as a model and its
 * animation as one, even with none. */
static int find_sliders(const uint8_t *mem, uint32_t top, uint32_t model, uint32_t anim, int wide,
                        int tell)
{
    struct { uint32_t node; uint8_t depth, carried; int up, parent; } stack[64];
    uint32_t visits = 0;
    unsigned depth = 0;
    WideRig g;
    if (!tell) s_n_sliders = s_n_visits = 0;
    if (!rig_read(mem, top, model, anim, &g)) return 0;
    stack[0].node = g.nodes; stack[0].depth = 0; stack[0].carried = 0; stack[0].up = -1;
    stack[0].parent = -1;
    depth = 1;
    while (depth) {
        uint32_t node = stack[--depth].node, next, track;
        unsigned d = stack[depth].depth;
        int carried = stack[depth].carried, up = stack[depth].up, below = up;
        int parent = stack[depth].parent, mesh, move, slides;
        float r[6] = { 0, 0, 0, 0, 0, 0 };
        if (node < g.nodes || (node - g.nodes) % WIDE_NODE_SIZE
            || (node - g.nodes) / WIDE_NODE_SIZE >= g.count || visits >= g.count)
            return 0;
        track = g.tracks + visits * WIDE_TRACK_SIZE;
        mesh = guest32(mem, node + WIDE_NODE_MESH) != 0;
        move = track_move(mem, top, track, g.frames, wide, r);
        /* the first rule: its translation alone keyed, and sliding; the
         * wider: sliding whatever else is keyed, or carried by an ancestor
         * that slides, and not moved in y or z itself */
        slides = wide ? move == MOVE_SLIDE : move == MOVE_SLIDE && (mem[track] & 0x15u) == 0x01u;
        if (tell) {
            if (visits < WIDE_MENU_NODES_TOLD) {
                int about = listed(s_anchored, s_n_anchored, node);
                const char *what = listed(s_anchors, s_n_anchors, node)
                                 ? ": carries a chain of sliding parts: stretched with it, "
                                   "about its own origin"
                                 : listed(s_chain, s_n_chain, node)
                                 ? about ? ": in a chain of sliding parts: stretched alike, "
                                           "about its carrier's origin"
                                         : ": in a chain of sliding parts: stretched alike"
                                 : listed(s_carried, s_n_carried, node)
                                 ? ": carried with a chain of sliding parts: stretched with it, "
                                   "about its carrier's origin"
                                 : listed(s_sliders, s_n_sliders, node)
                                 ? ": stretched across, with the band" : "";
                if (move == MOVE_NONE)
                    fprintf(stderr, "[WIDE]   node %u, depth %u: %s, track %02X, its translation "
                            "not keyed%s\n", visits, d, mesh ? "a mesh" : "no mesh", mem[track],
                            what);
                else
                    fprintf(stderr, "[WIDE]   node %u, depth %u: %s, track %02X, x %.2f..%.2f "
                            "y %.2f..%.2f z %.2f..%.2f%s\n", visits, d, mesh ? "a mesh" : "no mesh",
                            mem[track], r[0], r[1], r[2], r[3], r[4], r[5], what);
            }
        } else {
            int found = -1;
            if (mesh && (slides || (wide && carried && move != MOVE_OTHER))
                && s_n_sliders < WIDE_MENU_SLIDERS_MAX) {
                s_slider_up[s_n_sliders] = up;
                s_slider_own[s_n_sliders] = (unsigned char)(move == MOVE_SLIDE);
                s_slider_visit[s_n_sliders] = visits < WIDE_MENU_VISITS ? (int)visits : -1;
                below = found = (int)s_n_sliders;
                s_sliders[s_n_sliders++] = node;
            }
            if (visits < WIDE_MENU_VISITS) {
                s_visit_node[visits] = node;
                s_visit_up[visits] = parent;
                s_visit_found[visits] = found;
                s_visit_depth[visits] = (unsigned char)(d < 255u ? d : 255u);
                s_visit_mesh[visits] = (unsigned char)mesh;
                s_n_visits = visits + 1u;
            }
        }
        visits++;
        next = guest32(mem, node + WIDE_NODE_SIBLING);
        if (next) {
            if (depth >= sizeof stack / sizeof stack[0]) return 0;
            stack[depth].node = next; stack[depth].depth = (uint8_t)d;
            stack[depth].carried = (uint8_t)carried;
            stack[depth].up = up;
            stack[depth].parent = parent;
            depth++;
        }
        next = guest32(mem, node + WIDE_NODE_CHILD);
        if (next) {
            if (depth >= sizeof stack / sizeof stack[0] || d >= 255u) return 0;
            stack[depth].node = next; stack[depth].depth = (uint8_t)(d + 1u);
            stack[depth].carried = (uint8_t)(carried || (wide && move == MOVE_SLIDE));
            stack[depth].up = below;
            stack[depth].parent = (int)visits - 1;
            depth++;
        }
    }
    return 1;
}

/* Whether the walk's node at place w is carried by the one at place p. */
static int visit_under(int w, int p)
{
    for (w = s_visit_up[w]; w >= 0; w = s_visit_up[w])
        if (w == p) return 1;
    return 0;
}

/* The chains among the sliding nodes found, and the still node that
 * carries each one. The band's rectangles hang side by side, none carried
 * by another that slides. The garage's menus also slide their three arrows
 * along the bottom, along x alone and at the band's height in the keys, but
 * as a chain: each arrow is carried by the one before it and slides out
 * from it by its own keys. In the system and graffiti menus the first arrow
 * doesn't slide, so it wasn't found, and the two it carries were stretched
 * without it: one landed on the next (Adri on GitHub issue 2, 10 November
 * 2026; 9e02d60-arrows1's log, 11 November). Left as drawn instead, at
 * their 4:3 shape (test build 9e02d60-arrows2, 11 November), the practice
 * menu's ran on too far to the right: its last slides past the 4:3
 * screen's edge, which the stretched picture puts at the screen's. So all
 * of a chain is stretched alike, and with it the still node that carries
 * it, and what that one carries, when that node has a mesh, isn't the
 * model's first or one of its siblings, and carries nothing of the band.
 * Stretched about the screen's centre, as the band is, such a chain came
 * away from the stop symbol under the first arrow at the left, which isn't
 * stretched: the system and graffiti menus' arrows started a third of their
 * distance from the centre further left (test build 9e02d60-arrows3, 11
 * November). So that still node is the chain's anchor (s_anchors), and it
 * and what it carries are stretched about where its own origin lands
 * (s_anchored, NV2A_WIDE_HUD_FROM_ANCHOR). That origin is the model's, at
 * the 4:3 picture's left edge (9e02d60-arrows4's log): the arrows go out
 * from there rather than from the centre, so the first one's tail stays on
 * the stop symbol at 16:9 and is a little right of it at 21:9, as chosen on
 * 11 November. A chain carried
 * by the model's first node (Roboy's menu, practice, save and load) has no
 * anchor, and is stretched about the screen's centre, as the band is. A
 * found node that slides by its own keys and is carried by another found
 * node is a link: it, the one that carries it, and what either carries are
 * the chain (s_chain); the nodes added with it are s_carried. Each node
 * comes after the one that carries it, in the walk's order. */
static void keep_chains(void)
{
    unsigned char chain[WIDE_MENU_SLIDERS_MAX] = { 0 };
    unsigned i, n = s_n_sliders;
    s_n_chain = s_n_carried = s_n_anchors = s_n_anchored = 0;
    for (i = 0; i < n; i++)
        if (s_slider_own[i] && s_slider_up[i] >= 0) chain[i] = chain[s_slider_up[i]] = 1;
    for (i = 0; i < n; i++)
        if (s_slider_up[i] >= 0 && chain[s_slider_up[i]]) chain[i] = 1;
    for (i = 0; i < n; i++)
        if (chain[i]) s_chain[s_n_chain++] = s_sliders[i];
    for (i = 0; i < n; i++) {
        int v = s_slider_visit[i], p = v >= 0 ? s_visit_up[v] : -1, w, band = 0;
        /* a chain's first node, carried by a still node with a mesh, not
         * the model's first or one of its siblings */
        if (!chain[i] || s_slider_up[i] >= 0 || p < 0 || !s_visit_mesh[p] || !s_visit_depth[p])
            continue;
        for (w = p + 1; w < (int)s_n_visits && !band; w++)
            band = visit_under(w, p) && s_visit_found[w] >= 0 && !chain[s_visit_found[w]];
        if (band) continue;
        for (w = p; w < (int)s_n_visits && s_n_sliders < WIDE_MENU_SLIDERS_MAX; w++)
            if ((w == p || visit_under(w, p)) && s_visit_mesh[w] && s_visit_found[w] < 0
                && !listed(s_sliders, s_n_sliders, s_visit_node[w])) {
                s_carried[s_n_carried++] = s_visit_node[w];
                s_sliders[s_n_sliders++] = s_visit_node[w];
            }
        /* the anchor, if it was added, and it and what it carries, found or
         * added, stretched about it */
        if (!listed(s_sliders, s_n_sliders, s_visit_node[p])
            || listed(s_anchors, s_n_anchors, s_visit_node[p]))
            continue;
        s_anchors[s_n_anchors++] = s_visit_node[p];
        for (w = p; w < (int)s_n_visits && s_n_anchored < WIDE_MENU_SLIDERS_MAX; w++)
            if ((w == p || visit_under(w, p)) && listed(s_sliders, s_n_sliders, s_visit_node[w])
                && !listed(s_anchored, s_n_anchored, s_visit_node[w]))
                s_anchored[s_n_anchored++] = s_visit_node[w];
    }
}

int widescreen_menus_sliders_begin(const uint8_t *mem, uint32_t top, uint32_t model,
                                   uint32_t anim)
{
    static uint32_t told[WIDE_PLAINS][WIDE_MENU_MODELS_TOLD][2];
    static unsigned n_told[WIDE_PLAINS];
    static unsigned char told_still[WIDE_PLAINS];
    if (s_fill < 0 || s_sliders_on || !model || !anim) return 0;
    if (!widescreen_menus_plain[s_fill].slides) {
        if (!told_still[s_fill]) {
            told_still[s_fill] = 1;
            fprintf(stderr, "[WIDE] %s: its moving rectangles left as drawn, not stretched\n",
                    widescreen_menus_plain[s_fill].name);
        }
        return 0;
    }
    if (model != s_slider_model || anim != s_slider_anim) {
        int ok = find_sliders(mem, top, model, anim, 0, 0), wide = 0;
        unsigned k, few;
        if (ok && !s_n_sliders) {
            find_sliders(mem, top, model, anim, 1, 0);
            wide = 1;
        }
        s_slider_model = model;
        s_slider_anim = anim;
        if (!ok) s_n_sliders = s_n_visits = 0;
        keep_chains();
        few = s_n_sliders < WIDE_MENU_SLIDERS_MIN ? s_n_sliders : 0;
        if (few) s_n_sliders = s_n_chain = s_n_carried = s_n_anchors = s_n_anchored = 0;
        for (k = 0; k < n_told[s_fill]; k++)
            if (told[s_fill][k][0] == model && told[s_fill][k][1] == anim) break;
        if (k == n_told[s_fill] && k < WIDE_MENU_MODELS_TOLD) {
            char others[128] = "";
            told[s_fill][k][0] = model;
            told[s_fill][k][1] = anim;
            n_told[s_fill]++;
            if (s_n_chain)
                snprintf(others, sizeof others, "; %u of them in a chain of sliding parts%s, "
                         "stretched alike%s", s_n_chain, s_n_carried ? ", with what carries it"
                                                                     : "",
                         s_n_anchors ? " about its origin" : "");
            if (!ok)
                fprintf(stderr, "[WIDE] %s: its model %08X (animation %08X) doesn't read as a "
                        "model with an animation: no moving rectangles\n",
                        widescreen_menus_plain[s_fill].name, model, anim);
            else if (few)
                fprintf(stderr, "[WIDE] %s: %u moving %s in its model %08X (animation %08X)%s, "
                        "too few for a band of rectangles (a knob or a cursor): left as "
                        "drawn; its nodes:\n",
                        widescreen_menus_plain[s_fill].name, few, few == 1 ? "part" : "parts",
                        model, anim, wide ? " by the wider rule" : "");
            else if (s_n_sliders)
                fprintf(stderr, "[WIDE] %s: %u moving rectangles in its model %08X (animation "
                        "%08X)%s, piece %u, stretched across over the filled sides%s; its "
                        "nodes:\n",
                        widescreen_menus_plain[s_fill].name, s_n_sliders, model, anim,
                        wide ? " by the wider rule" : "", WIDE_MENU_PIECE_SLIDERS, others);
            else
                fprintf(stderr, "[WIDE] %s: no moving rectangles in its model %08X (animation "
                        "%08X), by either rule; its nodes:\n",
                        widescreen_menus_plain[s_fill].name, model, anim);
            if (ok) find_sliders(mem, top, model, anim, wide, 1);
        }
    }
    if (!s_n_sliders) return 0;
    s_sliders_on = 1;
    return 1;
}

void widescreen_menus_sliders_end(int began)
{
    if (began) s_sliders_on = 0;
    s_slider_flags = 0;
}

int widescreen_menus_slider(uint32_t model, uint32_t node, uint32_t via, unsigned *piece)
{
    static uint32_t told[8];
    static unsigned n_told;
    unsigned i, k;
    s_slider_flags = 0;
    if (!s_sliders_on || model != s_slider_model) return NV2A_WIDE_HUD_NONE;
    for (i = 0; i < s_n_sliders; i++)
        if (s_sliders[i] == node) {
            *piece = WIDE_MENU_PIECE_SLIDERS;
            s_slider_flags = (listed(s_anchors, s_n_anchors, node) ? NV2A_WIDE_HUD_ANCHOR : 0u)
                           | (listed(s_anchored, s_n_anchored, node)
                              ? NV2A_WIDE_HUD_FROM_ANCHOR : 0u);
            for (k = 0; k < n_told && told[k] != via; k++) {}
            if (k == n_told && n_told < sizeof told / sizeof told[0]) {
                told[n_told++] = via;
                fprintf(stderr, "[WIDE] a moving rectangle in %s drawn through sub_%08X\n",
                        s_fill >= 0 ? widescreen_menus_plain[s_fill].name : "a plain menu", via);
            }
            return NV2A_WIDE_HUD_OVER;
        }
    return NV2A_WIDE_HUD_NONE;
}

/* The title: when it last drew and in which state. */
static double s_title_at;
static uint32_t s_title_state;
static int s_title_seen;

void widescreen_menus_title(uint8_t *mem, uint32_t self, double now)
{
    static unsigned told;
    uint32_t state;
    if (!self || guest32(mem, self) != WIDE_TITLE_CLASS) return;
    state = guest32(mem, self + WIDE_TITLE_STATE);
    if ((!s_title_seen || state != s_title_state) && told < WIDE_TITLE_STATES_TOLD) {
        told++;
        fprintf(stderr, "[WIDE] t=%.1f the title's state %u\n", now, state);
    }
    s_title_state = state;
    s_title_at = now;
    s_title_seen = 1;
}

int widescreen_menus_title_bars(uint8_t *mem, uint32_t self, int on)
{
    static int told;
    uint32_t state;
    if (!on || !self || guest32(mem, self) != WIDE_TITLE_CLASS) return 0;
    state = guest32(mem, self + WIDE_TITLE_STATE);
    if (state == WIDE_TITLE_CITY) return 0;
    if (!told) {
        told = 1;
        fprintf(stderr, "[WIDE] menu part %u, the title's own draws in its state %u: its bars "
                "across (in every state but the city's)\n", WIDE_MENU_PIECE_BARS, state);
    }
    return 1;
}

int widescreen_menus_title_wide(double now)
{
    static int told;
    if (!s_title_seen || !(now - s_title_at <= WIDE_MENU_RECENT)
        || s_title_state < WIDE_TITLE_WIDE_FROM)
        return 0;
    if (!told) {
        told = 1;
        fprintf(stderr, "[WIDE] the title (class %08X) in its state %u: widened from its first "
                "\"please press start\" on\n", WIDE_TITLE_CLASS, s_title_state);
    }
    return 1;
}

int widescreen_menus_title_press_begin(uint8_t *mem, uint32_t self, int on)
{
    uint32_t pack;
    if (!on || !self || s_title_press || s_title_logo || guest32(mem, self) != WIDE_TITLE_CLASS)
        return 0;
    pack = guest32(mem, self + WIDE_TITLE_PACK);
    if (pack < 0x10000u) return 0;
    s_title_press = guest32(mem, pack + WIDE_TITLE_PRESS);
    s_title_logo = guest32(mem, pack + WIDE_TITLE_LOGO);
    if (s_title_logo == s_title_press) s_title_logo = 0;
    return s_title_press != 0 || s_title_logo != 0;
}

void widescreen_menus_title_press_end(int began)
{
    if (began) s_title_press = s_title_logo = 0;
}

/* The result screens' parts (widescreen-3b/menus/search-7, section 5). */
const WideResult widescreen_menus_results[WIDE_RESULTS] = {
    [WIDE_RESULT_CHOICE] = { 0x0010F670u, 0x001D24F0u,
                             "the result screen's return to title and restart icons and static bar",
                             WIDE_MENU_PIECE_CHOICE, 1u << 0 | 1u << 8, 0 },
    [WIDE_RESULT_TIME]   = { 0x0010EC10u, 0x001D23F0u, "the result screen's race time",
                             WIDE_MENU_PIECE_TIME, 1u << 0 | 1u << 3 | 1u << 5 | 1u << 8,
                             1u << 4 | 1u << 6 },
};

/* Which result part is drawing now, or -1. */
static int s_result = -1;

int widescreen_menus_result(const uint8_t *mem, int which, uint32_t self, uint32_t pass, int on)
{
    static unsigned char told[WIDE_RESULTS], told_class[WIDE_RESULTS], told_mode[WIDE_RESULTS];
    const WideResult *r;
    if (which < 0 || which >= WIDE_RESULTS || !self || !on) return 0;
    r = &widescreen_menus_results[which];
    if (pass >= 32u || !(r->passes >> pass & 1u)) return 0;
    if (guest32(mem, self) != r->vtable) {
        if (!told_class[which]) {
            told_class[which] = 1;
            fprintf(stderr, "[WIDE] %s (sub_%08X) drew an object of class %08X, not %08X: "
                    "left as it is\n", r->name, r->va, guest32(mem, self), r->vtable);
        }
        return 0;
    }
    if (r->modes) {
        uint32_t mode = guest32(mem, self + WIDE_RESULT_MODE);
        if (mode >= 32u || !(r->modes >> mode & 1u)) {
            if (!told_mode[which]) {
                told_mode[which] = 1;
                fprintf(stderr, "[WIDE] %s (sub_%08X) drew in mode %u (+0x%03X), not the result "
                        "screen's: left as it is\n", r->name, r->va, mode, WIDE_RESULT_MODE);
            }
            return 0;
        }
    }
    if (!told[which]) {
        told[which] = 1;
        fprintf(stderr, "[WIDE] menu part %u, %s (sub_%08X): moved to the right edge\n",
                r->piece, r->name, r->va);
    }
    s_result = which;
    return 1;
}

/* The x of one of the race time's places: table, mode, digit. */
static uint32_t time_place(unsigned table, uint32_t mode, unsigned digit)
{
    return WIDE_RESULT_PLACES + table * WIDE_RESULT_TABLE + mode * WIDE_RESULT_ROW
         + digit * WIDE_RESULT_PLACE;
}

float widescreen_menus_result_time_move(const uint8_t *mem, uint32_t self)
{
    static uint32_t told_modes;
    static int told_odd, told_split;
    uint32_t mode, flags;
    float x, y, end, move;
    const char *name = widescreen_menus_results[WIDE_RESULT_TIME].name;
    if (!self) return 0.0f;
    mode = guest32(mem, self + WIDE_RESULT_MODE);
    /* Only the result screen's modes, whoever asks: the race HUD's timer
     * reads other rows, and stays where it is. */
    if (mode >= WIDE_RESULT_MODES
        || !(widescreen_menus_results[WIDE_RESULT_TIME].modes >> mode & 1u)) return 0.0f;
    flags = guest32(mem, self + WIDE_RESULT_TIME_FLAGS);
    y = guest_float(mem, self + WIDE_RESULT_TIME_Y);
    if ((flags & WIDE_RESULT_TIME_SPLIT) || y != 0.0f) {
        if (!told_split) {
            told_split = 1;
            fprintf(stderr, "[WIDE] menu part %u, %s, mode %u: placed for a split screen "
                    "(flags %08X at +0x%02X, y %.1f at +0x%03X): not moved further\n",
                    WIDE_MENU_PIECE_TIME, name, mode, flags, WIDE_RESULT_TIME_FLAGS, (double)y,
                    WIDE_RESULT_TIME_Y);
        }
        return 0.0f;
    }
    x = guest_float(mem, time_place(WIDE_RESULT_HUNDREDTHS, mode, WIDE_RESULT_DIGITS - 1u));
    end = x + WIDE_RESULT_INK;
    move = WIDE_RESULT_BANNER_RIGHT - end;
    if (!isfinite(move) || move < 0.0f || move > WIDE_RESULT_MOVE_MAX) {
        if (!told_odd) {
            told_odd = 1;
            fprintf(stderr, "[WIDE] menu part %u, %s, mode %u: its last digit's place %.1f: "
                    "not moved further\n", WIDE_MENU_PIECE_TIME, name, mode, (double)x);
        }
        return 0.0f;
    }
    if (!(told_modes >> mode & 1u)) {
        told_modes |= 1u << mode;
        fprintf(stderr, "[WIDE] menu part %u, %s, mode %u: its right end at %.1f, moved %.1f "
                "further, under the banner's end (%.0f)\n", WIDE_MENU_PIECE_TIME, name, mode,
                (double)end, (double)move, (double)WIDE_RESULT_BANNER_RIGHT);
    }
    return move;
}

int widescreen_menus_result_time_begin(uint8_t *mem, uint32_t self, WideTimePlaces *saved)
{
    float move = widescreen_menus_result_time_move(mem, self);
    unsigned table, digit;
    if (move == 0.0f || !saved) return 0;
    saved->mode = guest32(mem, self + WIDE_RESULT_MODE);
    for (table = 0; table < WIDE_RESULT_TABLES; table++)
        for (digit = 0; digit < WIDE_RESULT_DIGITS; digit++) {
            uint32_t at = time_place(table, saved->mode, digit);
            float x;
            memcpy(&saved->x[table * WIDE_RESULT_DIGITS + digit], mem + at, sizeof(uint32_t));
            memcpy(&x, mem + at, sizeof x);
            x += move;
            memcpy(mem + at, &x, sizeof x);
        }
    return 1;
}

void widescreen_menus_result_time_end(uint8_t *mem, const WideTimePlaces *saved, int began)
{
    unsigned table, digit;
    if (!began || !saved) return;
    for (table = 0; table < WIDE_RESULT_TABLES; table++)
        for (digit = 0; digit < WIDE_RESULT_DIGITS; digit++)
            memcpy(mem + time_place(table, saved->mode, digit),
                   &saved->x[table * WIDE_RESULT_DIGITS + digit], sizeof(uint32_t));
}

void widescreen_menus_result_end(int which, int began)
{
    if (began && s_result == which) s_result = -1;
}

int widescreen_menus_result_text(void)
{
    static int told;
    if (s_result != WIDE_RESULT_CHOICE) return 0;
    if (!told) {
        told = 1;
        fprintf(stderr, "[WIDE] menu part %u, %s: its label drawn at once, at the right edge\n",
                WIDE_MENU_PIECE_CHOICE, widescreen_menus_results[WIDE_RESULT_CHOICE].name);
    }
    return 1;
}
