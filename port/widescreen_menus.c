/* The game's menus laid out for widescreen: the game's half
 * (widescreen_menus.h). Plain C on guest memory, so a test links it
 * (tests/widescreen_menus). */
#include "widescreen_menus.h"
#include "nv2a_wide.h"

#include <math.h>
#include <stdio.h>

#define L NV2A_WIDE_HUD_LEFT
#define R NV2A_WIDE_HUD_RIGHT
#define A NV2A_WIDE_HUD_ACROSS
#define E WIDE_MENU_EXTEND

/* The street pause menu (class 0x1D2A00), in the order its draw draws them.
 * Its fields as the read of the generated code found them, and each model
 * as Aden's draw log of the menu showed it (widescreen-3b/menus/layout-1).
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

#undef L
#undef R
#undef A
#undef E

const WideMenu widescreen_menus[WIDE_MENUS] = {
    [WIDE_MENU_STREET_PAUSE] = { 0x001129E0u, 0x001D2A00u, "the street pause menu",
                                 street_parts, sizeof street_parts / sizeof street_parts[0] },
    [WIDE_MENU_OTHER_PAUSE]  = { 0x001133A0u, 0x001D2AA8u, "the other pause menu",
                                 other_parts, sizeof other_parts / sizeof other_parts[0] },
};

/* The menu drawing now and its object, or -1. */
static int s_menu = -1;
static uint32_t s_self;
/* When the street pause menu last began, for its area name's sign, and
 * whether it has. */
static double s_street_at;
static int s_street_seen;
/* The other pause menu that began last, for its child, and when. */
static uint32_t s_other;
static double s_other_at;
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
    if (menu == WIDE_MENU_OTHER_PAUSE) { s_other = self; s_other_at = now; }
    s_menu_at = now;
    s_menu_seen = 1;
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

int widescreen_menus_part(uint8_t *mem, uint32_t model, unsigned *piece)
{
    static unsigned char told[WIDE_MENUS][32];
    const WideMenu *m;
    unsigned i;
    if (s_menu < 0 || !model) return NV2A_WIDE_HUD_NONE;
    m = &widescreen_menus[s_menu];
    for (i = 0; i < m->n_parts && i < 32; i++) {
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

int widescreen_menus_text_side(float x, unsigned *piece)
{
    static unsigned char told[2];
    int left;
    if (s_menu < 0 || x != x) return NV2A_WIDE_HUD_NONE;
    left = x < WIDE_MENU_TEXT_LEFT_OF;
    *piece = left ? WIDE_MENU_PIECE_TEXT_L : WIDE_MENU_PIECE_TEXT_R;
    if (!told[left]) {
        told[left] = 1;
        fprintf(stderr, "[WIDE] menu part %u in %s, the text on the %s: drawn at once, at %s\n",
                *piece, widescreen_menus[s_menu].name, left ? "left" : "right",
                side_name(left ? NV2A_WIDE_HUD_LEFT : NV2A_WIDE_HUD_RIGHT));
    }
    return left ? NV2A_WIDE_HUD_LEFT : NV2A_WIDE_HUD_RIGHT;
}

int widescreen_menus_sign(uint8_t *mem, uint32_t sign, double now)
{
    static int told;
    if (!sign || !s_street_seen || !(now - s_street_at <= WIDE_MENU_RECENT)
        || !(guest32(mem, sign + WIDE_MENU_SIGN_FLAGS) & WIDE_MENU_AREA_SIGN_FLAG))
        return 0;
    if (!told) {
        told = 1;
        fprintf(stderr, "[WIDE] menu part %u in the street pause menu, the area name (a sign, "
                "flags %08X): marked for the left edge\n", WIDE_MENU_PIECE_SIGN,
                guest32(mem, sign + WIDE_MENU_SIGN_FLAGS));
    }
    return 1;
}

int widescreen_menus_child(uint8_t *mem, uint32_t self, double now)
{
    static int told;
    if (!self || !s_other || !(now - s_other_at <= WIDE_MENU_RECENT)
        || guest32(mem, s_other) != widescreen_menus[WIDE_MENU_OTHER_PAUSE].vtable
        || guest32(mem, s_other + WIDE_MENU_FIRST_CHILD) != self)
        return 0;
    if (!told) {
        told = 1;
        fprintf(stderr, "[WIDE] menu part %u in the other pause menu, the header (its child, "
                "class %08X): marked for the left edge\n", WIDE_MENU_PIECE_CHILD,
                guest32(mem, self));
    }
    return 1;
}

int widescreen_menus_ticker_begin(uint8_t *mem, uint32_t self, int on, double now)
{
    static int told;
    if (!on || !self || !s_menu_seen || !(now - s_menu_at <= WIDE_MENU_RECENT)
        || guest32(mem, self) != WIDE_MENU_TICKER_CLASS)
        return 0;
    if (!told) {
        told = 1;
        fprintf(stderr, "[WIDE] menu part %u, the pause menus' ticker (class %08X): "
                "across the whole width\n", WIDE_MENU_PIECE_TICKER, WIDE_MENU_TICKER_CLASS);
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

int widescreen_menus_mark(uint8_t *mem, uint32_t device, unsigned piece, int side,
                          widescreen_hud_space_fn make_space)
{
    if (!device || side == NV2A_WIDE_HUD_NONE || side == WIDE_MENU_EXTEND) return 0;
    return widescreen_hud_push(mem, device, nv2a_wide_hud_mark(piece, side, 0), make_space) != 0;
}

void widescreen_menus_unmark(uint8_t *mem, uint32_t device, unsigned piece, int marked,
                             widescreen_hud_space_fn make_space)
{
    if (!marked) return;
    widescreen_hud_push(mem, device, nv2a_wide_hud_mark(piece, NV2A_WIDE_HUD_NONE, 0),
                        make_space);
}
