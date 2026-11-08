/* Run with: python tests/widescreen_menus/run.py
 *
 * The menus laid out for widescreen, port/widescreen_menus.c, as the engine
 * builds it, against a guest memory that holds D3D's device (its put
 * pointer and limit) and its push buffer, a menu's object with its models
 * in its fields, and a sign and a header. space() plays D3D's make-space
 * call.
 *
 *   1. The menus: each is its draw's and its class's, named; each part is a
 *      field of its object once, on a side (left, right or across) or the
 *      backdrop's two halves, named; the street pause menu's backdrop is
 *      drawn in halves, its top bar and bands go across, its buttons right
 *      and ROTATE MAP left, and the other's the same; every piece number is
 *      one menu part's or one other piece's, past the HUD's.
 *   2. The area name's sign isn't moved before the street pause menu has
 *      drawn, and the header isn't before the other pause menu has.
 *   3. A menu whose object isn't of its class doesn't begin, and the log
 *      says so once (run.py counts); nor does one with no object, nor one
 *      that isn't a menu. In 4:3 a menu doesn't begin either, and none of
 *      its parts or its text is told apart.
 *   4. Begun, widened: each field's model is its part, on its side, with
 *      its piece number, and the log names each part once (run.py counts);
 *      a model in no field isn't, nor no model (the empty fields hold
 *      none). The first begin names the menu, and no other does. After the
 *      end, nothing is a part.
 *   5. While a menu draws, text that starts in the screen's left quarter
 *      goes to the left edge, and the rest to the right, the right-aligned
 *      bottom label at any length; NaN goes nowhere, and no text goes
 *      anywhere while no menu draws.
 *   6. The area name's sign goes left within a quarter of a second of the
 *      street pause menu, and not after; the HUD's own signs, without the
 *      map's flag, never do.
 *   7. The other pause menu's header goes left: its first child, within a
 *      quarter of a second of the menu, not another object, not after, and
 *      not once the menu's object is gone (its class changed).
 *   7a. The ticker runs the whole width: of its class, within a quarter of
 *      a second of a pause menu, widened, its text's x is spread out from
 *      the middle by 1/s, so the 4:3 screen's edges land at the wide
 *      screen's (0 and 640 at -106.7 and 746.7 at 16:9, the middle stays);
 *      not in 4:3, not another class, not after, not once it has ended, and
 *      not for NaN or an s that isn't between 0 and 1. While it draws, no
 *      menu text is told apart.
 *   8. A part's marks: the start, with its piece and side, where the put
 *      pointer is, and the end, with no side, after; no side, or no device,
 *      writes nothing, and no end follows a start that wasn't written.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "widescreen_menus.h"
#include "nv2a_wide.h"

#define DEVICE 0x0100u
#define BUFFER 0x1000u
#define LIMIT  0x2000u
#define ROOM   0x1800u     /* where make-space puts the pointer, below the limit */
#define MENU   0x4000u     /* a menu's object */
#define MODELS 0x5000u     /* its models, 0x10 apart */
#define SIGN   0x6000u     /* a sign */
#define HEADER 0x6100u     /* the other pause menu's header */

static uint32_t s_guest[0x8000 / 4];
static unsigned s_space_calls;
static int s_failures;

#define CHECK(c) do { if (!(c)) { printf("widescreen_menus: FAIL %s:%d: %s\n", \
                                         __FILE__, __LINE__, #c); s_failures++; } } while (0)

static uint8_t *mem(void) { return (uint8_t *)s_guest; }
static uint32_t word(uint32_t va) { return s_guest[va / 4]; }
static void set(uint32_t va, uint32_t v) { s_guest[va / 4] = v; }
static uint32_t space(void) { s_space_calls++; return ROOM; }
static uint32_t model(unsigned i) { return MODELS + 0x10u * i; }

static void fresh(void)
{
    memset(s_guest, 0, sizeof s_guest);
    set(DEVICE + WIDE_HUD_D3D_PUT, BUFFER);
    set(DEVICE + WIDE_HUD_D3D_LIMIT, LIMIT);
    s_space_calls = 0;
}

/* The menu's object, of its class, with part i's model at model(i) and
 * every other field empty. */
static void menu_object(int menu)
{
    const WideMenu *m = &widescreen_menus[menu];
    unsigned i;
    memset(mem() + MENU, 0, 0x200);
    set(MENU, m->vtable);
    for (i = 0; i < m->n_parts; i++) set(MENU + m->parts[i].field, model(i));
}

static const WideMenuPart *part_at(int menu, uint16_t field)
{
    const WideMenu *m = &widescreen_menus[menu];
    unsigned i;
    for (i = 0; i < m->n_parts; i++)
        if (m->parts[i].field == field) return &m->parts[i];
    return NULL;
}

static int side_of(int menu, uint16_t field)
{
    const WideMenuPart *p = part_at(menu, field);
    return p ? p->side : 99;
}

static void menus(void)
{
    static const struct { uint16_t field; int side; } street[] = {
        { 0x7C, WIDE_MENU_EXTEND },     { 0x44, NV2A_WIDE_HUD_ACROSS },
        { 0x70, NV2A_WIDE_HUD_ACROSS }, { 0x48, NV2A_WIDE_HUD_RIGHT },
        { 0x50, NV2A_WIDE_HUD_RIGHT },  { 0x58, NV2A_WIDE_HUD_RIGHT },
        { 0x5C, NV2A_WIDE_HUD_RIGHT },  { 0x4C, NV2A_WIDE_HUD_RIGHT },
        { 0x6C, NV2A_WIDE_HUD_RIGHT },  { 0x68, NV2A_WIDE_HUD_LEFT },
        { 0x90, NV2A_WIDE_HUD_LEFT },   { 0x80, NV2A_WIDE_HUD_LEFT },
        { 0x84, NV2A_WIDE_HUD_LEFT },
    }, other[] = {
        { 0x68, WIDE_MENU_EXTEND },     { 0x44, NV2A_WIDE_HUD_ACROSS },
        { 0x5C, NV2A_WIDE_HUD_ACROSS }, { 0x48, NV2A_WIDE_HUD_RIGHT },
        { 0x4C, NV2A_WIDE_HUD_RIGHT },  { 0x50, NV2A_WIDE_HUD_RIGHT },
        { 0x6C, NV2A_WIDE_HUD_LEFT },   { 0x78, NV2A_WIDE_HUD_LEFT },
    };
    unsigned m, i, j, n = 0, extend;
    unsigned seen[256] = { 0 };
    CHECK(widescreen_menus[WIDE_MENU_STREET_PAUSE].va == 0x001129E0u);
    CHECK(widescreen_menus[WIDE_MENU_STREET_PAUSE].vtable == 0x001D2A00u);
    CHECK(widescreen_menus[WIDE_MENU_OTHER_PAUSE].va == 0x001133A0u);
    CHECK(widescreen_menus[WIDE_MENU_OTHER_PAUSE].vtable == 0x001D2AA8u);
    CHECK(widescreen_menus[WIDE_MENU_STREET_PAUSE].n_parts == sizeof street / sizeof street[0]);
    CHECK(widescreen_menus[WIDE_MENU_OTHER_PAUSE].n_parts == sizeof other / sizeof other[0]);
    for (i = 0; i < sizeof street / sizeof street[0]; i++)
        CHECK(side_of(WIDE_MENU_STREET_PAUSE, street[i].field) == street[i].side);
    for (i = 0; i < sizeof other / sizeof other[0]; i++)
        CHECK(side_of(WIDE_MENU_OTHER_PAUSE, other[i].field) == other[i].side);
    CHECK(part_at(WIDE_MENU_STREET_PAUSE, 0x64) == NULL);   /* empty */
    CHECK(part_at(WIDE_MENU_OTHER_PAUSE, 0x58) == NULL);
    for (m = 0; m < WIDE_MENUS; m++) {
        const WideMenu *w = &widescreen_menus[m];
        CHECK(w->name != NULL && w->name[0] != '\0');
        CHECK(w->n_parts > 0 && w->n_parts <= 0x20u);
        extend = 0;
        for (j = 0; j < m; j++)
            CHECK(widescreen_menus[j].va != w->va && widescreen_menus[j].vtable != w->vtable);
        for (i = 0; i < w->n_parts; i++) {
            const WideMenuPart *p = &w->parts[i];
            CHECK(p->name != NULL && p->name[0] != '\0');
            CHECK(p->side == NV2A_WIDE_HUD_LEFT || p->side == NV2A_WIDE_HUD_RIGHT
                  || p->side == NV2A_WIDE_HUD_ACROSS || p->side == WIDE_MENU_EXTEND);
            extend += p->side == WIDE_MENU_EXTEND;
            CHECK(p->field >= 4 && (p->field & 3u) == 0 && p->field < 0x200);
            for (j = 0; j < i; j++) CHECK(w->parts[j].field != p->field);
            CHECK(WIDE_MENU_PIECE(m, i) < 256);
            seen[WIDE_MENU_PIECE(m, i)]++;
            n++;
        }
        CHECK(extend == 1);                             /* one backdrop */
    }
    seen[WIDE_MENU_PIECE_TEXT_L]++;
    seen[WIDE_MENU_PIECE_TEXT_R]++;
    seen[WIDE_MENU_PIECE_SIGN]++;
    seen[WIDE_MENU_PIECE_CHILD]++;
    seen[WIDE_MENU_PIECE_TICKER]++;
    for (i = 0; i < 256; i++) {
        CHECK(seen[i] <= 1);
        if (seen[i]) CHECK(i >= WIDE_HUD_PIECES);
    }
    CHECK(n == sizeof street / sizeof street[0] + sizeof other / sizeof other[0]);
}

static void before(void)
{
    fresh();
    set(SIGN + WIDE_MENU_SIGN_FLAGS, WIDE_MENU_AREA_SIGN_FLAG);
    CHECK(!widescreen_menus_sign(mem(), SIGN, 10.0));
    CHECK(!widescreen_menus_child(mem(), HEADER, 10.0));
}

static void not_begun(void)
{
    unsigned piece = 999;
    fresh();
    menu_object(WIDE_MENU_STREET_PAUSE);
    set(MENU, 0x001D2AA8u);                           /* the other menu's class */
    CHECK(!widescreen_menus_begin(mem(), WIDE_MENU_STREET_PAUSE, MENU, 1, 1.0));
    CHECK(!widescreen_menus_begin(mem(), WIDE_MENU_STREET_PAUSE, MENU, 1, 1.0));
    CHECK(widescreen_menus_part(mem(), model(0), &piece) == NV2A_WIDE_HUD_NONE);
    menu_object(WIDE_MENU_STREET_PAUSE);
    CHECK(!widescreen_menus_begin(mem(), WIDE_MENU_STREET_PAUSE, 0, 1, 1.0));
    CHECK(!widescreen_menus_begin(mem(), WIDE_MENUS, MENU, 1, 1.0));
    CHECK(!widescreen_menus_begin(mem(), -1, MENU, 1, 1.0));
    /* 4:3 */
    CHECK(!widescreen_menus_begin(mem(), WIDE_MENU_STREET_PAUSE, MENU, 0, 1.0));
    CHECK(widescreen_menus_part(mem(), model(0), &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_text_side(48.0f, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(piece == 999);
    widescreen_menus_end(0);
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER && s_space_calls == 0);
}

static void parts(void)
{
    int menu;
    for (menu = 0; menu < WIDE_MENUS; menu++) {
        const WideMenu *m = &widescreen_menus[menu];
        unsigned i, round, piece;
        int began;
        fresh();
        menu_object(menu);
        began = widescreen_menus_begin(mem(), menu, MENU, 1, 2.0);
        CHECK(began);
        for (round = 0; round < 2; round++)
            for (i = 0; i < m->n_parts; i++) {
                piece = 999;
                CHECK(widescreen_menus_part(mem(), model(i), &piece) == m->parts[i].side);
                CHECK(piece == WIDE_MENU_PIECE(menu, i));
            }
        piece = 999;
        CHECK(widescreen_menus_part(mem(), model(m->n_parts), &piece) == NV2A_WIDE_HUD_NONE);
        CHECK(widescreen_menus_part(mem(), 0, &piece) == NV2A_WIDE_HUD_NONE);
        CHECK(widescreen_menus_part(mem(), MENU, &piece) == NV2A_WIDE_HUD_NONE);
        CHECK(piece == 999);
        widescreen_menus_end(began);
        CHECK(widescreen_menus_part(mem(), model(0), &piece) == NV2A_WIDE_HUD_NONE);
        /* begun again, named no more */
        began = widescreen_menus_begin(mem(), menu, MENU, 1, 2.0);
        CHECK(began);
        widescreen_menus_end(began);
        CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER);   /* telling apart writes nothing */
    }
}

static void text(void)
{
    static const struct { float x; int side; unsigned piece; } want[] = {
        { 48.0f,  NV2A_WIDE_HUD_LEFT,  WIDE_MENU_PIECE_TEXT_L },   /* ROTATE MAP, CHANGE MAP */
        { 116.0f, NV2A_WIDE_HUD_LEFT,  WIDE_MENU_PIECE_TEXT_L },   /* the counts */
        { 159.9f, NV2A_WIDE_HUD_LEFT,  WIDE_MENU_PIECE_TEXT_L },
        { -20.0f, NV2A_WIDE_HUD_LEFT,  WIDE_MENU_PIECE_TEXT_L },
        { 160.0f, NV2A_WIDE_HUD_RIGHT, WIDE_MENU_PIECE_TEXT_R },
        { 540.0f, NV2A_WIDE_HUD_RIGHT, WIDE_MENU_PIECE_TEXT_R },   /* TOTAL's number */
        { 476.0f, NV2A_WIDE_HUD_RIGHT, WIDE_MENU_PIECE_TEXT_R },   /* the bottom label */
        { 276.0f, NV2A_WIDE_HUD_RIGHT, WIDE_MENU_PIECE_TEXT_R },   /* ... 300 wide */
        { 180.0f, NV2A_WIDE_HUD_RIGHT, WIDE_MENU_PIECE_TEXT_R },   /* ... 400 wide */
        { 700.0f, NV2A_WIDE_HUD_RIGHT, WIDE_MENU_PIECE_TEXT_R },
    };
    unsigned i, piece;
    int began;
    fresh();
    menu_object(WIDE_MENU_STREET_PAUSE);
    began = widescreen_menus_begin(mem(), WIDE_MENU_STREET_PAUSE, MENU, 1, 3.0);
    CHECK(began);
    for (i = 0; i < sizeof want / sizeof want[0]; i++) {
        piece = 999;
        CHECK(widescreen_menus_text_side(want[i].x, &piece) == want[i].side);
        CHECK(piece == want[i].piece);
    }
    piece = 999;
    CHECK(widescreen_menus_text_side(NAN, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(piece == 999);
    widescreen_menus_end(began);
    CHECK(widescreen_menus_text_side(48.0f, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_text_side(540.0f, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(piece == 999);
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER);
}

static void sign(void)
{
    int began;
    fresh();
    menu_object(WIDE_MENU_STREET_PAUSE);
    began = widescreen_menus_begin(mem(), WIDE_MENU_STREET_PAUSE, MENU, 1, 10.0);
    widescreen_menus_end(began);
    set(SIGN + WIDE_MENU_SIGN_FLAGS, WIDE_MENU_AREA_SIGN_FLAG | 0x58000000u);
    CHECK(widescreen_menus_sign(mem(), SIGN, 10.0));
    CHECK(widescreen_menus_sign(mem(), SIGN, 10.0 + 1.0 / 60.0));   /* the next frame */
    CHECK(widescreen_menus_sign(mem(), SIGN, 10.0 + WIDE_MENU_RECENT));
    CHECK(!widescreen_menus_sign(mem(), SIGN, 10.3));
    CHECK(!widescreen_menus_sign(mem(), SIGN, NAN));
    CHECK(!widescreen_menus_sign(mem(), 0, 10.0));
    set(SIGN + WIDE_MENU_SIGN_FLAGS, 0x58000000u);                  /* the HUD's own */
    CHECK(!widescreen_menus_sign(mem(), SIGN, 10.0));
    set(SIGN + WIDE_MENU_SIGN_FLAGS, 0x40000000u);
    CHECK(!widescreen_menus_sign(mem(), SIGN, 10.0));
    /* the other pause menu doesn't make it the street one's */
    set(SIGN + WIDE_MENU_SIGN_FLAGS, WIDE_MENU_AREA_SIGN_FLAG);
    menu_object(WIDE_MENU_OTHER_PAUSE);
    began = widescreen_menus_begin(mem(), WIDE_MENU_OTHER_PAUSE, MENU, 1, 20.0);
    widescreen_menus_end(began);
    CHECK(!widescreen_menus_sign(mem(), SIGN, 20.0));
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER);
}

static void child(void)
{
    int began;
    fresh();
    menu_object(WIDE_MENU_OTHER_PAUSE);
    set(MENU + WIDE_MENU_FIRST_CHILD, HEADER);
    set(HEADER, 0x001D0770u);
    began = widescreen_menus_begin(mem(), WIDE_MENU_OTHER_PAUSE, MENU, 1, 30.0);
    widescreen_menus_end(began);                    /* the header draws after the menu */
    CHECK(widescreen_menus_child(mem(), HEADER, 30.0));
    CHECK(widescreen_menus_child(mem(), HEADER, 30.0 + WIDE_MENU_RECENT));
    CHECK(!widescreen_menus_child(mem(), HEADER + 0x40, 30.0));    /* another header */
    CHECK(!widescreen_menus_child(mem(), 0, 30.0));
    CHECK(!widescreen_menus_child(mem(), HEADER, 30.3));
    set(MENU, 0x001D2A00u);                         /* gone, its memory reused */
    CHECK(!widescreen_menus_child(mem(), HEADER, 30.0));
    set(MENU, 0);
    CHECK(!widescreen_menus_child(mem(), HEADER, 30.0));
    /* a street pause menu with the same child isn't the other's */
    menu_object(WIDE_MENU_STREET_PAUSE);
    set(MENU + WIDE_MENU_FIRST_CHILD, HEADER);
    began = widescreen_menus_begin(mem(), WIDE_MENU_STREET_PAUSE, MENU, 1, 40.0);
    widescreen_menus_end(began);
    CHECK(!widescreen_menus_child(mem(), HEADER, 40.0));
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER);
}

#define TICKER 0x6200u

static void ticker(void)
{
    static const float wide[] = { 0.75f, 0.8333333f, 0.5625f };
    unsigned i, piece;
    float x;
    int began;
    fresh();
    set(TICKER, WIDE_MENU_TICKER_CLASS);
    menu_object(WIDE_MENU_OTHER_PAUSE);
    began = widescreen_menus_begin(mem(), WIDE_MENU_OTHER_PAUSE, MENU, 1, 50.0);
    widescreen_menus_end(began);                    /* the ticker draws after the menu */
    CHECK(!widescreen_menus_ticker_begin(mem(), TICKER, 0, 50.0));   /* 4:3 */
    CHECK(!widescreen_menus_ticker_begin(mem(), MENU, 1, 50.0));     /* another class */
    CHECK(!widescreen_menus_ticker_begin(mem(), 0, 1, 50.0));
    CHECK(!widescreen_menus_ticker_begin(mem(), TICKER, 1, 50.3));   /* too late */
    CHECK(widescreen_menus_ticker_x(640.0f, 0.75f, &x, &piece) == NV2A_WIDE_HUD_NONE);
    began = widescreen_menus_ticker_begin(mem(), TICKER, 1, 50.0 + 1.0 / 60.0);
    CHECK(began);
    for (i = 0; i < sizeof wide / sizeof wide[0]; i++) {
        float s = wide[i];
        piece = 999; x = -1.0f;
        CHECK(widescreen_menus_ticker_x(640.0f, s, &x, &piece) == NV2A_WIDE_HUD_WHOLE);
        CHECK(piece == WIDE_MENU_PIECE_TICKER);
        CHECK(fabsf(320.0f + s * (x - 320.0f) - 640.0f) < 1e-3f);   /* the wide screen's edge */
        CHECK(widescreen_menus_ticker_x(0.0f, s, &x, &piece) == NV2A_WIDE_HUD_WHOLE);
        CHECK(fabsf(320.0f + s * (x - 320.0f)) < 1e-3f);
        CHECK(widescreen_menus_ticker_x(320.0f, s, &x, &piece) == NV2A_WIDE_HUD_WHOLE);
        CHECK(x == 320.0f);
    }
    CHECK(widescreen_menus_ticker_x(0.0f, 0.75f, &x, &piece) == NV2A_WIDE_HUD_WHOLE);
    CHECK(fabsf(x - (320.0f - 320.0f / 0.75f)) < 1e-3f);           /* -106.7 */
    piece = 999; x = -1.0f;
    CHECK(widescreen_menus_ticker_x(NAN, 0.75f, &x, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_ticker_x(100.0f, 1.0f, &x, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_ticker_x(100.0f, 0.0f, &x, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_ticker_x(100.0f, NAN, &x, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(piece == 999 && x == -1.0f);
    CHECK(widescreen_menus_text_side(48.0f, &piece) == NV2A_WIDE_HUD_NONE);
    widescreen_menus_ticker_end(began);
    CHECK(widescreen_menus_ticker_x(640.0f, 0.75f, &x, &piece) == NV2A_WIDE_HUD_NONE);
    /* the street pause menu's too */
    menu_object(WIDE_MENU_STREET_PAUSE);
    began = widescreen_menus_begin(mem(), WIDE_MENU_STREET_PAUSE, MENU, 1, 60.0);
    widescreen_menus_end(began);
    began = widescreen_menus_ticker_begin(mem(), TICKER, 1, 60.0);
    CHECK(began);
    CHECK(widescreen_menus_ticker_x(640.0f, 0.75f, &x, &piece) == NV2A_WIDE_HUD_WHOLE);
    widescreen_menus_ticker_end(began);
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER);
}

static int mark_at(uint32_t va, unsigned piece, int side)
{
    unsigned p = 999, f = 99;
    int sd = 9;
    return word(va) == ((1u << 18) | NV2A_WIDE_HUD_METHOD)
        && nv2a_wide_hud_read(word(va + 4), &p, &sd, &f) && p == piece && sd == side && f == 0;
}

static void marks(void)
{
    static const int sides[] = { NV2A_WIDE_HUD_LEFT, NV2A_WIDE_HUD_RIGHT, NV2A_WIDE_HUD_ACROSS,
                                 NV2A_WIDE_HUD_WHOLE, NV2A_WIDE_HUD_LEFT_HALF,
                                 NV2A_WIDE_HUD_RIGHT_HALF };
    unsigned i, piece = WIDE_MENU_PIECE(WIDE_MENU_STREET_PAUSE, 0);
    int marked;
    for (i = 0; i < sizeof sides / sizeof sides[0]; i++) {
        fresh();
        marked = widescreen_menus_mark(mem(), DEVICE, piece + i, sides[i], space);
        CHECK(marked);
        CHECK(mark_at(BUFFER, piece + i, sides[i]));
        CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER + 8);
        set(BUFFER + 8, 0xD3D0D3D0u);      /* the part's own draws, as D3D writes them */
        set(DEVICE + WIDE_HUD_D3D_PUT, BUFFER + 12);
        widescreen_menus_unmark(mem(), DEVICE, piece + i, marked, space);
        CHECK(mark_at(BUFFER + 12, piece + i, NV2A_WIDE_HUD_NONE));
        CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER + 20);
        CHECK(s_space_calls == 0);
    }
    /* at the limit, make-space first */
    fresh();
    set(DEVICE + WIDE_HUD_D3D_PUT, LIMIT);
    CHECK(widescreen_menus_mark(mem(), DEVICE, WIDE_MENU_PIECE_TEXT_L, NV2A_WIDE_HUD_LEFT, space));
    CHECK(s_space_calls == 1 && mark_at(ROOM, WIDE_MENU_PIECE_TEXT_L, NV2A_WIDE_HUD_LEFT));
    /* nothing for no side, the backdrop's own (its halves have theirs), or
     * no device, and no end after no start */
    fresh();
    CHECK(!widescreen_menus_mark(mem(), DEVICE, piece, NV2A_WIDE_HUD_NONE, space));
    CHECK(!widescreen_menus_mark(mem(), DEVICE, piece, WIDE_MENU_EXTEND, space));
    CHECK(!widescreen_menus_mark(mem(), 0, piece, NV2A_WIDE_HUD_LEFT, space));
    widescreen_menus_unmark(mem(), DEVICE, piece, 0, space);
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER && word(BUFFER) == 0 && s_space_calls == 0);
}

int main(void)
{
    menus();
    before();
    not_begun();
    parts();
    text();
    sign();
    child();
    ticker();
    marks();
    if (s_failures) {
        printf("widescreen_menus: %d failed\n", s_failures);
        return 1;
    }
    printf("widescreen_menus: all cases pass\n");
    return 0;
}
