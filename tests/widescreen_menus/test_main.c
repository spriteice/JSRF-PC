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
 *   2. The area name's sign goes left before any menu has drawn, and the
 *      header isn't moved before the other pause menu has drawn.
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
 *   6. The area name's sign goes left with the street pause menu, after it,
 *      and with any other menu; the HUD's own signs, without the map's
 *      flag, never do, and the log names each of their flags once. A
 *      result screen's banner, a sign with either of the result flow's
 *      flags, goes right with its own piece, whatever menu drew last, and
 *      no flags on its mark; the log names it once and each of its types
 *      once. Another flag with them makes a sign that stays, or with the
 *      area's flag an area's name. The banner under the arrow to another
 *      area, a sign with no flags whose parent is of the arrow's object's
 *      class, goes left with the area name's piece, not with a grid, even
 *      with a menu just drawn, named once; with flags, another parent, none,
 *      one that isn't a word's place or is past the memory's end, it stays
 *      and its parent isn't read.
 *   7. The other pause menu's header goes left: its first child, within a
 *      quarter of a second of the menu, not another object, not after, and
 *      not once the menu's object is gone (its class changed). So does the
 *      race preview's, the race's name: any header within a quarter of a
 *      second of the preview, not another class, not before, not after.
 *   7a. The ticker runs the whole width: of its class, within a quarter of
 *      a second of a pause menu, widened, its text's x is spread out from
 *      the middle by 1/s, so the 4:3 screen's edges land at the wide
 *      screen's (0 and 640 at -106.7 and 746.7 at 16:9, the middle stays);
 *      not in 4:3, not another class, not after, not once it has ended, and
 *      not for NaN or an s that isn't between 0 and 1. While it draws, no
 *      menu text is told apart. The race preview's ticker, of its own
 *      class, the same after the preview; a header isn't a ticker.
 *   8. A part's marks: the start, with its piece and side, where the put
 *      pointer is, and the end, with no side, after; no side, or no device,
 *      writes nothing, and no end follows a start that wasn't written.
 *   9. The plain menus (the side fill): each is its draw's and its class's,
 *      named, no draw or class twice and none a laid-out menu's; their
 *      pieces one each, past every other. Widened, of its class, with a
 *      device: the frame's mark, with its piece and the fill, where the put
 *      pointer is (after make-space at the limit), and no end; again on the
 *      next frame; the log names each menu once (run.py counts). Nothing in
 *      4:3, with no device or no object, for one that isn't a plain menu,
 *      or for an object of another class, which the log says once. The
 *      graffiti menu only on its pass 9 in its states 4 and 8, the loading
 *      screen only while +0x98 is 1 and +0xA4's bit 1 is clear. VS mode's
 *      setup screen is one, on every call, with no strip and no moving
 *      rectangles.
 *  10. The GG notebook: its backdrop in halves, its bands across, its grey
 *      block right, and its text from y 400 down right with the block, the
 *      page's text where it is; the area name's sign goes left with it
 *      too. The save graffiti's character select
 *      the same, from its own fields, its panel where it is, and its
 *      characters' names with it.
 *  11. The moving rectangles: while a plain menu draws an animated model,
 *      its nodes with a mesh whose translation alone is keyed, y and z the
 *      same every frame and x changing, are found, in the walk's order (a
 *      node, its children, then its next sibling), and each is marked over
 *      the filled sides; a node keyed otherwise, with no mesh, or of another
 *      model isn't; nothing outside a plain menu's draw, after the end, for
 *      no animation, or for a model or an animation that doesn't read as
 *      one (a pointer out of memory, a tree that loops). A model with none
 *      by that rule is looked at by the wider one: a node with a mesh that
 *      slides whatever else is keyed, or that an ancestor's slide carries
 *      and that doesn't climb itself, is found, and no other; one with none
 *      by either has its nodes listed in the log, and so has each model a
 *      menu's first finds are made for, each node with what is done with
 *      it. Whichever node function draws a rectangle, it is marked the
 *      same, and the log names each function once. A model with a single
 *      one (a knob, a cursor) has none, and the log says so once. The
 *      music menu's are left as drawn: none is found, and the log says so
 *      once.
 *  11a. The arrows: of the nodes found, those in a chain (one sliding by
 *      its own keys, carried by another found, the one that carries it,
 *      and what either carries), such as the garage's arrows along the
 *      bottom, are marked alike with the band, by either rule, and so is
 *      the still node with a mesh that carries a chain, and what it carries,
 *      unless it's the model's first or carries one of the band too; a
 *      still piece carried by one of the band goes with it. The log says
 *      how many are in a chain and marks each node.
 *  12. The title: widened from its state 3 on, within a quarter of a second
 *      of its draw, not in its states 0 to 2, nor for another class; in its
 *      state 6 draw, its pack's model 1 goes to the left edge, its model 0,
 *      the logo, to the right edge, and its other models nowhere; nothing
 *      in 4:3 or after the end. Widened, its own bars go across in every
 *      state but 6, not in 4:3, nor for another class.
 *      Its states are logged as they change.
 *  13. The result screens (their banner is a sign, 6): the choice and the
 *      race time, each its draw's and its class's, named, with its own
 *      piece, past the other menus' and the banner's and before the plain
 *      menus'. Widened, of its class, on its passes (0 and 8; the race
 *      time's 0, 3, 5 and 8): moved (each named once), and marked whole for
 *      the right edge with no flags; not on its other passes, nor in 4:3,
 *      nor for another class, which the log says once. The race time only
 *      in the result screen's modes, 4 and 6: not in the race HUD's, 2 and
 *      3, nor any other, which the log says once. While the choice draws,
 *      text is its label, drawn at once; not while the race time draws, nor
 *      after the choice's end, nor before. The race time's places, its
 *      four tables at the game's addresses: in modes 4 and 6, moved 151
 *      and 164 further, so the hundredths' last digit's ink ends at the
 *      banner's end, each of its mode's x moved for the draw, its y and
 *      every other mode's row as they were, then each word put back (one
 *      that isn't a whole number too); not in the race HUD's modes, nor
 *      placed for a split screen (a quarter's flag or a y of its own), nor
 *      already past the banner's end, nor from a place that isn't one, nor
 *      when not begun, each said once.
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
#define ARROW  0x6080u     /* the arrow's object, the banner's parent */
#define HEADER 0x6100u     /* the other pause menu's header */

#define RIG    0x10000u    /* a model and its animation, where a pointer can be */
#define TOP    0x20000u    /* the guest memory's end, for the pointers' checks */
#define GUEST  0x240000u   /* the array's: past the race time's places (WIDE_RESULT_HUNDREDTHS) */

static uint32_t s_guest[GUEST / 4];
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

/* Whether the sign at va, drawn at now, goes to the left edge with the area
 * name's piece. */
static int sign_left(uint32_t va, double now)
{
    unsigned piece = 999;
    return widescreen_menus_sign(mem(), GUEST, va, now, &piece) == NV2A_WIDE_HUD_LEFT
        && piece == WIDE_MENU_PIECE_SIGN;
}

/* Whether it goes to the right edge with a result screen's banner's piece. */
static int sign_right(uint32_t va, double now)
{
    unsigned piece = 999;
    return widescreen_menus_sign(mem(), GUEST, va, now, &piece) == NV2A_WIDE_HUD_RIGHT
        && piece == WIDE_MENU_PIECE_RESULT;
}

/* Whether it stays where it is, its piece untouched. */
static int sign_stays(uint32_t va, double now)
{
    unsigned piece = 999;
    return widescreen_menus_sign(mem(), GUEST, va, now, &piece) == NV2A_WIDE_HUD_NONE
        && piece == 999;
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
    }, notebook[] = {
        { 0xD0, WIDE_MENU_EXTEND },     { 0xC4, NV2A_WIDE_HUD_ACROSS },
        { 0x98, NV2A_WIDE_HUD_RIGHT },
    }, characters[] = {
        { 0xDC, WIDE_MENU_EXTEND },     { 0xD0, NV2A_WIDE_HUD_ACROSS },
        { 0x4C, NV2A_WIDE_HUD_RIGHT },
    }, preview[] = {
        { 0x60, WIDE_MENU_EXTEND },     { 0x48, NV2A_WIDE_HUD_ACROSS },
        { 0x54, NV2A_WIDE_HUD_ACROSS }, { 0x4C, NV2A_WIDE_HUD_RIGHT },
        { 0x64, NV2A_WIDE_HUD_LEFT },
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
    CHECK(widescreen_menus[WIDE_MENU_NOTEBOOK].va == 0x00115030u);
    CHECK(widescreen_menus[WIDE_MENU_NOTEBOOK].vtable == 0x001D2B68u);
    CHECK(widescreen_menus[WIDE_MENU_NOTEBOOK].n_parts == sizeof notebook / sizeof notebook[0]);
    for (i = 0; i < sizeof notebook / sizeof notebook[0]; i++)
        CHECK(side_of(WIDE_MENU_NOTEBOOK, notebook[i].field) == notebook[i].side);
    CHECK(widescreen_menus[WIDE_MENU_CHARACTERS].va == 0x00115A40u);
    CHECK(widescreen_menus[WIDE_MENU_CHARACTERS].vtable == 0x001D2BB0u);
    CHECK(widescreen_menus[WIDE_MENU_CHARACTERS].n_parts
          == sizeof characters / sizeof characters[0]);
    for (i = 0; i < sizeof characters / sizeof characters[0]; i++)
        CHECK(side_of(WIDE_MENU_CHARACTERS, characters[i].field) == characters[i].side);
    CHECK(widescreen_menus[WIDE_MENU_CHARACTERS].block_y == 400.0f);
    CHECK(widescreen_menus[WIDE_MENU_RACE_PREVIEW].va == 0x00111320u);
    CHECK(widescreen_menus[WIDE_MENU_RACE_PREVIEW].vtable == 0x001D2788u);
    CHECK(widescreen_menus[WIDE_MENU_RACE_PREVIEW].n_parts == sizeof preview / sizeof preview[0]);
    for (i = 0; i < sizeof preview / sizeof preview[0]; i++)
        CHECK(side_of(WIDE_MENU_RACE_PREVIEW, preview[i].field) == preview[i].side);
    CHECK(widescreen_menus[WIDE_MENU_RACE_PREVIEW].block_y == 0.0f);
    CHECK(part_at(WIDE_MENU_RACE_PREVIEW, 0x50) == NULL);   /* never loaded */
    CHECK(part_at(WIDE_MENU_CHARACTERS, 0x50) == NULL);     /* the panel stays */
    CHECK(part_at(WIDE_MENU_CHARACTERS, 0x54) == NULL);     /* and its characters */
    CHECK(widescreen_menus[WIDE_MENU_STREET_PAUSE].block_y == 0.0f);
    CHECK(widescreen_menus[WIDE_MENU_OTHER_PAUSE].block_y == 0.0f);
    CHECK(widescreen_menus[WIDE_MENU_NOTEBOOK].block_y == 400.0f);
    CHECK(part_at(WIDE_MENU_STREET_PAUSE, 0x64) == NULL);   /* empty */
    CHECK(part_at(WIDE_MENU_OTHER_PAUSE, 0x58) == NULL);
    CHECK(part_at(WIDE_MENU_NOTEBOOK, 0xA4) == NULL);       /* the page stays */
    CHECK(part_at(WIDE_MENU_NOTEBOOK, 0xC0) == NULL);
    for (m = 0; m < WIDE_MENUS; m++) {
        const WideMenu *w = &widescreen_menus[m];
        CHECK(w->name != NULL && w->name[0] != '\0');
        CHECK(w->n_parts > 0 && w->n_parts <= WIDE_MENU_PARTS_MAX);
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
    seen[WIDE_MENU_PIECE_SLIDERS]++;
    seen[WIDE_MENU_PIECE_TITLE]++;
    seen[WIDE_MENU_PIECE_LOGO]++;
    seen[WIDE_MENU_PIECE_BARS]++;
    for (i = 0; i < 256; i++) {
        CHECK(seen[i] <= 1);
        if (seen[i]) CHECK(i >= WIDE_HUD_PIECES);
    }
    CHECK(n == sizeof street / sizeof street[0] + sizeof other / sizeof other[0]
               + sizeof notebook / sizeof notebook[0] + sizeof characters / sizeof characters[0]
               + sizeof preview / sizeof preview[0]);
    CHECK(WIDE_MENU_PIECE(WIDE_MENUS, 0) <= WIDE_MENU_PIECE_TEXT_L);  /* below the others */
}

static void before(void)
{
    fresh();
    set(SIGN + WIDE_MENU_SIGN_FLAGS, WIDE_MENU_AREA_SIGN_FLAG);
    CHECK(sign_left(SIGN, 10.0));             /* on its own */
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
    CHECK(widescreen_menus_text_side(48.0f, 100.0f, &piece) == NV2A_WIDE_HUD_NONE);
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
        CHECK(widescreen_menus_text_side(want[i].x, 100.0f + 30.0f * (float)i, &piece)
              == want[i].side);
        CHECK(piece == want[i].piece);
        CHECK(widescreen_menus_mark_flags(piece, want[i].side) == NV2A_WIDE_HUD_ON_GRID);
    }
    piece = 999;
    CHECK(widescreen_menus_text_side(NAN, 100.0f, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_text_side(48.0f, NAN, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(piece == 999);
    widescreen_menus_end(began);
    CHECK(widescreen_menus_text_side(48.0f, 100.0f, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_text_side(540.0f, 100.0f, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(piece == 999);
    /* the notebook's: from y 400 down, right with its block; above, with
     * the page */
    menu_object(WIDE_MENU_NOTEBOOK);
    began = widescreen_menus_begin(mem(), WIDE_MENU_NOTEBOOK, MENU, 1, 4.0);
    CHECK(began);
    piece = 999;
    CHECK(widescreen_menus_text_side(324.8f, 410.0f, &piece) == NV2A_WIDE_HUD_RIGHT);
    CHECK(piece == WIDE_MENU_PIECE_TEXT_R);
    CHECK(widescreen_menus_text_side(48.0f, 400.0f, &piece) == NV2A_WIDE_HUD_RIGHT);
    piece = 999;
    CHECK(widescreen_menus_text_side(90.0f, 170.0f, &piece) == NV2A_WIDE_HUD_NONE);  /* a row */
    CHECK(widescreen_menus_text_side(540.0f, 399.9f, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_text_side(324.8f, NAN, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(piece == 999);
    widescreen_menus_end(began);
    CHECK(widescreen_menus_text_side(324.8f, 410.0f, &piece) == NV2A_WIDE_HUD_NONE);
    /* the character select's: the same, its names above with the panel */
    menu_object(WIDE_MENU_CHARACTERS);
    began = widescreen_menus_begin(mem(), WIDE_MENU_CHARACTERS, MENU, 1, 4.5);
    CHECK(began);
    piece = 999;
    CHECK(widescreen_menus_text_side(330.0f, 410.0f, &piece) == NV2A_WIDE_HUD_RIGHT);
    CHECK(piece == WIDE_MENU_PIECE_TEXT_R);
    piece = 999;
    CHECK(widescreen_menus_text_side(112.0f, 128.0f, &piece) == NV2A_WIDE_HUD_NONE);  /* a name */
    CHECK(widescreen_menus_text_side(400.0f, 378.0f, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(piece == 999);
    widescreen_menus_end(began);
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
    CHECK(sign_left(SIGN, 10.0));
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SIGN, NV2A_WIDE_HUD_LEFT)
          == NV2A_WIDE_HUD_ON_GRID);                               /* the menu's: on its grid */
    CHECK(sign_left(SIGN, 10.0 + 1.0 / 60.0));   /* the next frame */
    CHECK(sign_left(SIGN, 10.0 + WIDE_MENU_RECENT));
    CHECK(sign_left(SIGN, 10.3));               /* after it: the banner */
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SIGN, NV2A_WIDE_HUD_LEFT) == 0u);
    CHECK(sign_left(SIGN, NAN));
    CHECK(sign_stays(0, 10.0));
    set(SIGN + WIDE_MENU_SIGN_FLAGS, 0x58000000u);                  /* the HUD's own */
    CHECK(sign_stays(SIGN, 10.0));
    CHECK(sign_stays(SIGN, 10.0));              /* named once */
    set(SIGN + WIDE_MENU_SIGN_FLAGS, 0x40000000u);
    CHECK(sign_stays(SIGN, 10.0));
    /* with the other pause menu up, too */
    set(SIGN + WIDE_MENU_SIGN_FLAGS, WIDE_MENU_AREA_SIGN_FLAG);
    menu_object(WIDE_MENU_OTHER_PAUSE);
    began = widescreen_menus_begin(mem(), WIDE_MENU_OTHER_PAUSE, MENU, 1, 20.0);
    widescreen_menus_end(began);
    CHECK(sign_left(SIGN, 20.0));
    /* the notebook's area name is the same sign, drawn after it */
    menu_object(WIDE_MENU_NOTEBOOK);
    began = widescreen_menus_begin(mem(), WIDE_MENU_NOTEBOOK, MENU, 1, 25.0);
    widescreen_menus_end(began);
    CHECK(sign_left(SIGN, 25.0));
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SIGN, NV2A_WIDE_HUD_LEFT)
          == NV2A_WIDE_HUD_ON_GRID);
    CHECK(sign_left(SIGN, 25.3));
    /* the race preview's own area name, drawn after it, on its grid */
    menu_object(WIDE_MENU_RACE_PREVIEW);
    began = widescreen_menus_begin(mem(), WIDE_MENU_RACE_PREVIEW, MENU, 1, 27.0);
    widescreen_menus_end(began);
    CHECK(sign_left(SIGN, 27.0));
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SIGN, NV2A_WIDE_HUD_LEFT)
          == NV2A_WIDE_HUD_ON_GRID);
    CHECK(sign_left(SIGN, 27.3));                /* the banner */
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SIGN, NV2A_WIDE_HUD_LEFT) == 0u);
    set(SIGN + WIDE_MENU_SIGN_FLAGS, 0x58000000u);
    CHECK(sign_stays(SIGN, 25.0));
    /* a result screen's banner: the result flow's flags, either, go right
     * with their own piece, whatever menu drew last, named once, and each
     * type once */
    set(SIGN + WIDE_MENU_SIGN_FLAGS, WIDE_MENU_RESULT_SIGN_FLAGS);
    set(SIGN + WIDE_MENU_SIGN_TYPE, 7);
    CHECK(sign_right(SIGN, 27.0));                                 /* the preview's just drawn */
    CHECK(sign_right(SIGN, 40.0));
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_RESULT, NV2A_WIDE_HUD_RIGHT) == 0u);
    set(SIGN + WIDE_MENU_SIGN_TYPE, 0);
    CHECK(sign_right(SIGN, 40.0));
    set(SIGN + WIDE_MENU_SIGN_FLAGS, WIDE_MENU_RESULT_SIGN_FLAGS_CHOICE);
    CHECK(sign_right(SIGN, 40.0));                                 /* type 0 named once */
    CHECK(sign_right(SIGN, NAN));
    CHECK(sign_stays(0, 40.0));
    /* another flag with them makes another sign: one that stays, or with
     * the area's flag, an area's name */
    set(SIGN + WIDE_MENU_SIGN_FLAGS, WIDE_MENU_RESULT_SIGN_FLAGS | 0x01000000u);
    CHECK(sign_stays(SIGN, 40.0));
    set(SIGN + WIDE_MENU_SIGN_FLAGS, WIDE_MENU_RESULT_SIGN_FLAGS | WIDE_MENU_AREA_SIGN_FLAG);
    CHECK(sign_left(SIGN, 40.0));
    /* the banner under the arrow to another area: no flags, its parent of
     * the arrow's object's class: left, not on a grid, even with a menu
     * just drawn, named once */
    CHECK(WIDE_MENU_SIGN_PARENT == 0x24u && WIDE_MENU_BANNER_CLASS == 0x001D1DD0u);
    set(ARROW, WIDE_MENU_BANNER_CLASS);
    set(SIGN + WIDE_MENU_SIGN_PARENT, ARROW);
    set(SIGN + WIDE_MENU_SIGN_FLAGS, 0);
    menu_object(WIDE_MENU_STREET_PAUSE);
    began = widescreen_menus_begin(mem(), WIDE_MENU_STREET_PAUSE, MENU, 1, 50.0);
    widescreen_menus_end(began);
    CHECK(sign_left(SIGN, 50.0));
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SIGN, NV2A_WIDE_HUD_LEFT) == 0u);
    CHECK(sign_left(SIGN, 60.0));
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SIGN, NV2A_WIDE_HUD_LEFT) == 0u);
    /* with the HUD's flags, or another parent: it stays, its flags named once */
    set(SIGN + WIDE_MENU_SIGN_FLAGS, 0x58000000u);
    CHECK(sign_stays(SIGN, 60.0));
    set(SIGN + WIDE_MENU_SIGN_FLAGS, 0);
    set(ARROW, 0x001D2020u);                                      /* the HUD manager's class */
    CHECK(sign_stays(SIGN, 60.0));
    set(ARROW, WIDE_MENU_BANNER_CLASS);
    /* no parent, or one that isn't a word's place, or past the memory's
     * end (where the banner's class is, so a read would show): stays */
    set(SIGN + WIDE_MENU_SIGN_PARENT, 0);
    CHECK(sign_stays(SIGN, 60.0));
    set(SIGN + WIDE_MENU_SIGN_PARENT, ARROW + 2u);
    CHECK(sign_stays(SIGN, 60.0));
    set(GUEST - 4u, WIDE_MENU_BANNER_CLASS);
    set(SIGN + WIDE_MENU_SIGN_PARENT, GUEST - 4u);
    CHECK(sign_left(SIGN, 60.0));                                 /* the last word: read */
    {
        unsigned piece = 999;
        CHECK(widescreen_menus_sign(mem(), GUEST - 4u, SIGN, 60.0, &piece)
              == NV2A_WIDE_HUD_NONE && piece == 999);             /* past the end */
        CHECK(widescreen_menus_sign(mem(), 2u, SIGN, 60.0, &piece)
              == NV2A_WIDE_HUD_NONE && piece == 999);             /* no memory */
    }
    set(GUEST - 4u, 0);
    set(SIGN + WIDE_MENU_SIGN_PARENT, ARROW);
    CHECK(sign_left(SIGN, 60.0));
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
    /* the race preview's header, the race's name: a header drawn after it,
     * of the header's class, its child or not (twice: the log says it
     * once); not another class, not before it has drawn, and not after */
    CHECK(!widescreen_menus_child(mem(), HEADER + 0x40, 44.0));
    menu_object(WIDE_MENU_RACE_PREVIEW);
    began = widescreen_menus_begin(mem(), WIDE_MENU_RACE_PREVIEW, MENU, 1, 45.0);
    widescreen_menus_end(began);
    set(HEADER + 0x40, WIDE_MENU_HEADER_CLASS);
    CHECK(widescreen_menus_child(mem(), HEADER + 0x40, 45.0));
    CHECK(widescreen_menus_child(mem(), HEADER + 0x40, 45.0 + WIDE_MENU_RECENT));
    CHECK(!widescreen_menus_child(mem(), HEADER + 0x40, 45.3));
    set(HEADER + 0x40, WIDE_MENU_TICKER_CLASS);
    CHECK(!widescreen_menus_child(mem(), HEADER + 0x40, 45.0));
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
    CHECK(widescreen_menus_text_side(48.0f, 100.0f, &piece) == NV2A_WIDE_HUD_NONE);
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
    /* and the race preview's, of its own class, after the preview */
    set(TICKER, WIDE_MENU_PREVIEW_TICKER_CLASS);
    CHECK(!widescreen_menus_ticker_begin(mem(), TICKER, 1, 70.0));   /* no menu lately */
    menu_object(WIDE_MENU_RACE_PREVIEW);
    began = widescreen_menus_begin(mem(), WIDE_MENU_RACE_PREVIEW, MENU, 1, 70.0);
    widescreen_menus_end(began);
    began = widescreen_menus_ticker_begin(mem(), TICKER, 1, 70.0);
    CHECK(began);
    CHECK(widescreen_menus_ticker_x(0.0f, 0.75f, &x, &piece) == NV2A_WIDE_HUD_WHOLE);
    CHECK(fabsf(x - (320.0f - 320.0f / 0.75f)) < 1e-3f);
    widescreen_menus_ticker_end(began);
    set(TICKER, WIDE_MENU_HEADER_CLASS);                             /* not a ticker */
    CHECK(!widescreen_menus_ticker_begin(mem(), TICKER, 1, 70.0));
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER);
}

static int mark_flags_at(uint32_t va, unsigned piece, int side, unsigned flags)
{
    unsigned p = 999, f = 99;
    int sd = 9;
    return word(va) == ((1u << 18) | NV2A_WIDE_HUD_METHOD)
        && nv2a_wide_hud_read(word(va + 4), &p, &sd, &f) && p == piece && sd == side
        && f == flags;
}

static int mark_at(uint32_t va, unsigned piece, int side)
{
    return mark_flags_at(va, piece, side, 0u);
}

static void map(void)
{
    int marked;
    fresh();
    CHECK(!widescreen_menus_map(0, 0));                  /* not widened: nothing */
    CHECK(widescreen_menus_map(0, 1));
    CHECK(widescreen_menus_map(0, 1));                   /* named once */
    CHECK(widescreen_menus_map(1, 1));
    CHECK(!widescreen_menus_map(2, 0));
    CHECK(widescreen_menus_map(2, 1));                   /* the race preview's course map */
    CHECK(!widescreen_menus_map(3, 1) && !widescreen_menus_map(-1, 1));
    marked = widescreen_menus_mark(mem(), DEVICE, WIDE_MENU_PIECE_MAP, NV2A_WIDE_HUD_LINES, space);
    CHECK(marked && mark_at(BUFFER, WIDE_MENU_PIECE_MAP, NV2A_WIDE_HUD_LINES));
    widescreen_menus_unmark(mem(), DEVICE, WIDE_MENU_PIECE_MAP, marked, space);
    CHECK(mark_at(BUFFER + 8, WIDE_MENU_PIECE_MAP, NV2A_WIDE_HUD_NONE));
    CHECK(WIDE_MENU_PIECE_MAP > WIDE_MENU_PIECE_BARS && WIDE_MENU_PIECE_MAP < WIDE_MENU_PIECE_FILL0);
    fresh();
    marked = widescreen_menus_mark(mem(), DEVICE, WIDE_MENU_PIECE_COURSE, NV2A_WIDE_HUD_LINES,
                                   space);
    CHECK(marked && mark_at(BUFFER, WIDE_MENU_PIECE_COURSE, NV2A_WIDE_HUD_LINES));
    widescreen_menus_unmark(mem(), DEVICE, WIDE_MENU_PIECE_COURSE, marked, space);
    CHECK(mark_at(BUFFER + 8, WIDE_MENU_PIECE_COURSE, NV2A_WIDE_HUD_NONE));
    CHECK(WIDE_MENU_PIECE_COURSE > WIDE_MENU_PIECE_MAP
          && WIDE_MENU_PIECE_COURSE < WIDE_MENU_PIECE_FILL0);
}

static void marks(void)
{
    static const int sides[] = { NV2A_WIDE_HUD_LEFT, NV2A_WIDE_HUD_RIGHT, NV2A_WIDE_HUD_ACROSS,
                                 NV2A_WIDE_HUD_WHOLE, NV2A_WIDE_HUD_LEFT_HALF,
                                 NV2A_WIDE_HUD_RIGHT_HALF, NV2A_WIDE_HUD_OVER };
    unsigned i, piece = WIDE_MENU_PIECE(WIDE_MENU_STREET_PAUSE, 0);
    int marked;
    for (i = 0; i < sizeof sides / sizeof sides[0]; i++) {
        fresh();
        marked = widescreen_menus_mark(mem(), DEVICE, piece + i, sides[i], space);
        CHECK(marked);
        /* a side's part moves with the grid (the street pause menu's
         * backdrop is drawn in two copies); nothing else does */
        CHECK(mark_flags_at(BUFFER, piece + i, sides[i],
                            sides[i] == NV2A_WIDE_HUD_LEFT || sides[i] == NV2A_WIDE_HUD_RIGHT
                            ? NV2A_WIDE_HUD_ON_GRID : 0u));
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
    /* every laid-out menu's parts at a side move with the grid, and the
     * header; the text only while such a menu draws; other pieces don't */
    for (i = 0; i < WIDE_MENUS; i++) {
        CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE(i, 1), NV2A_WIDE_HUD_RIGHT)
              == NV2A_WIDE_HUD_ON_GRID);
        CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE(i, 1), NV2A_WIDE_HUD_ACROSS) == 0u);
    }
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_CHILD, NV2A_WIDE_HUD_LEFT)
          == NV2A_WIDE_HUD_ON_GRID);
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_TEXT_L, NV2A_WIDE_HUD_LEFT) == 0u);
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_TICKER, NV2A_WIDE_HUD_WHOLE) == 0u);
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_TITLE, NV2A_WIDE_HUD_LEFT) == 0u);
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_LOGO, NV2A_WIDE_HUD_RIGHT) == 0u);
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE(WIDE_MENUS, 0), NV2A_WIDE_HUD_LEFT) == 0u);
    CHECK(widescreen_menus_mark_flags(0x10u, NV2A_WIDE_HUD_LEFT) == 0u);   /* the HUD's */
    /* nothing for no side, the backdrop's own (its halves have theirs), or
     * no device, and no end after no start */
    fresh();
    CHECK(!widescreen_menus_mark(mem(), DEVICE, piece, NV2A_WIDE_HUD_NONE, space));
    CHECK(!widescreen_menus_mark(mem(), DEVICE, piece, WIDE_MENU_EXTEND, space));
    CHECK(!widescreen_menus_mark(mem(), 0, piece, NV2A_WIDE_HUD_LEFT, space));
    widescreen_menus_unmark(mem(), DEVICE, piece, 0, space);
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER && word(BUFFER) == 0 && s_space_calls == 0);
}

static void plain(void)
{
    static const struct { uint32_t va, vtable; } want[WIDE_PLAINS] = {
        [WIDE_PLAIN_SAVE]       = { 0x0011DD10u, 0x001D8CD0u },
        [WIDE_PLAIN_GARAGE]     = { 0x0011C070u, 0x001D8638u },
        [WIDE_PLAIN_SYSTEM]     = { 0x0011FEB0u, 0x001D9090u },
        [WIDE_PLAIN_OPTIONS]    = { 0x0011CF10u, 0x001D8898u },
        [WIDE_PLAIN_PRACTICE]   = { 0x0011D740u, 0x001D8C90u },
        [WIDE_PLAIN_MUSIC]      = { 0x0011F7D0u, 0x001D8F88u },
        [WIDE_PLAIN_TEST_RUN]   = { 0x0011C750u, 0x001D8798u },
        [WIDE_PLAIN_COURSE]     = { 0x0011A490u, 0x001D7C20u },
        [WIDE_PLAIN_CHARACTERS] = { 0x0011AC50u, 0x001D7D00u },
        [WIDE_PLAIN_GRAFFITI]   = { 0x00127080u, 0x001DA300u },
        [WIDE_PLAIN_LOADING]    = { 0x00066550u, 0x001CB0A8u },
        [WIDE_PLAIN_VS]         = { 0x00071100u, 0x001CCA80u },
    };
    const uint32_t garage = widescreen_menus_plain[WIDE_PLAIN_GARAGE].vtable;
    unsigned i, j;
    CHECK(WIDE_PLAINS == 12);
    CHECK(WIDE_MENU_PIECE(WIDE_MENUS - 1, 0x1F) < WIDE_MENU_PIECE_TEXT_L);
    CHECK(WIDE_MENU_PIECE_FILL(0) > WIDE_MENU_PIECE_TICKER);
    CHECK(WIDE_MENU_PIECE_FILL(WIDE_PLAINS - 1) <= 0xFFu);
    for (i = 0; i < WIDE_PLAINS; i++) {
        const WidePlainMenu *m = &widescreen_menus_plain[i];
        CHECK(m->va == want[i].va && m->vtable == want[i].vtable);
        CHECK(m->name != NULL && m->name[0] != '\0');
        for (j = 0; j < i; j++)
            CHECK(widescreen_menus_plain[j].va != m->va
                  && widescreen_menus_plain[j].vtable != m->vtable);
        for (j = 0; j < WIDE_MENUS; j++)
            CHECK(widescreen_menus[j].va != m->va && widescreen_menus[j].vtable != m->vtable);
        CHECK(m->vtable != WIDE_MENU_TICKER_CLASS);
    }
    CHECK(widescreen_menus_plain[WIDE_PLAIN_GRAFFITI].when == WIDE_FILL_GRAFFITI);
    CHECK(widescreen_menus_plain[WIDE_PLAIN_LOADING].when == WIDE_FILL_LOADING);
    for (i = 0; i < WIDE_PLAIN_GRAFFITI; i++)
        CHECK(widescreen_menus_plain[i].when == WIDE_FILL_ALWAYS);
    CHECK(widescreen_menus_plain[WIDE_PLAIN_VS].when == WIDE_FILL_ALWAYS);
    /* the garage's menus have the tan strip, at the top of the picture: the
     * yellow ones two title pixels into the 4:3 picture (eight cover it),
     * the graffiti menu to its panel's edge, at 120 (121 covers it); the
     * loading screen and VS mode's setup screen have none. Six rows cover
     * its four or five. The music menu's rectangles are left as drawn: the
     * practice menu's, left as drawn, made its sides flicker. VS mode's
     * setup screen has none. */
    CHECK(WIDE_MENU_STRIP == 8u && WIDE_MENU_STRIP_GRAFFITI == 121u);
    CHECK(NV2A_WIDE_STRIP_ROWS == 6u);
    for (i = 0; i < WIDE_PLAINS; i++) {
        CHECK(widescreen_menus_plain[i].strip
              == (i < WIDE_PLAIN_GRAFFITI ? WIDE_MENU_STRIP
                  : i == WIDE_PLAIN_GRAFFITI ? WIDE_MENU_STRIP_GRAFFITI : 0u));
        CHECK(widescreen_menus_plain[i].slides
              == (i != WIDE_PLAIN_MUSIC && i != WIDE_PLAIN_VS));
    }
    /* each, widened: the frame's mark, its strip's after it, and both again
     * on the next frame (the graffiti menu on its pass 9 in its state 4,
     * the loading screen shown) */
    for (i = 0; i < WIDE_PLAINS; i++) {
        uint32_t pass = i == WIDE_PLAIN_GRAFFITI ? 9u : 0u;
        uint32_t step = widescreen_menus_plain[i].strip ? 16u : 8u;
        int began, k;
        fresh();
        memset(mem() + MENU, 0, 0x200);
        set(MENU, widescreen_menus_plain[i].vtable);
        set(MENU + WIDE_GRAFFITI_STATE, 4);
        set(MENU + WIDE_LOADING_SHOWN, 1);
        for (k = 0; k < 2; k++) {
            uint32_t at = BUFFER + step * (uint32_t)k;
            began = widescreen_menus_fill(mem(), DEVICE, (int)i, MENU, pass, 1, space);
            CHECK(began);
            widescreen_menus_fill_end(began);
            CHECK(mark_at(at, WIDE_MENU_PIECE_FILL(i), NV2A_WIDE_HUD_FILL));
            if (step == 16u)
                CHECK(mark_at(at + 8, widescreen_menus_plain[i].strip, NV2A_WIDE_HUD_STRIP));
            CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == at + step);
        }
        CHECK(word(BUFFER + 2u * step) == 0 && s_space_calls == 0);
    }
    /* the graffiti menu: pass 9 only, in states 4 and 8 only */
    fresh();
    memset(mem() + MENU, 0, 0x200);
    set(MENU, widescreen_menus_plain[WIDE_PLAIN_GRAFFITI].vtable);
    {
        static const struct { uint32_t pass, state; int fills; } g[] = {
            { 9, 4, 1 }, { 9, 8, 1 }, { 8, 4, 0 }, { 0, 8, 0 }, { 9, 0, 0 }, { 9, 3, 0 },
            { 9, 5, 0 }, { 9, 7, 0 }, { 9, 9, 0 }, { 9, 0x104, 0 },
        };
        unsigned k, writes = 0;
        for (k = 0; k < sizeof g / sizeof g[0]; k++) {
            int began;
            set(MENU + WIDE_GRAFFITI_STATE, g[k].state);
            began = widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_GRAFFITI, MENU, g[k].pass, 1,
                                          space);
            CHECK(began == g[k].fills);
            widescreen_menus_fill_end(began);
            writes += 2u * (unsigned)began;                   /* the fill and its strip */
            CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER + 8u * writes);
        }
    }
    /* the loading screen: while +0x98 is 1 and +0xA4's bit 1 is clear */
    fresh();
    memset(mem() + MENU, 0, 0x200);
    set(MENU, widescreen_menus_plain[WIDE_PLAIN_LOADING].vtable);
    {
        static const struct { uint32_t shown; uint8_t flags; int fills; } l[] = {
            { 1, 0, 1 }, { 1, 0xFD, 1 }, { 1, 0x02, 0 }, { 1, 0xFF, 0 }, { 0, 0, 0 },
            { 2, 0, 0 }, { 0x101, 0, 0 },
        };
        unsigned k, writes = 0;
        for (k = 0; k < sizeof l / sizeof l[0]; k++) {
            int began;
            set(MENU + WIDE_LOADING_SHOWN, l[k].shown);
            set(MENU + WIDE_LOADING_FLAGS, 0xAB00u | l[k].flags);   /* the byte alone */
            began = widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_LOADING, MENU, 0, 1, space);
            CHECK(began == l[k].fills);
            widescreen_menus_fill_end(began);
            writes += (unsigned)began;
            CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER + 8u * writes);
        }
    }
    /* at the limit, make-space first */
    fresh();
    set(MENU, widescreen_menus_plain[WIDE_PLAIN_SAVE].vtable);
    set(DEVICE + WIDE_HUD_D3D_PUT, LIMIT);
    widescreen_menus_fill_end(widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_SAVE, MENU, 0, 1,
                                                    space));
    CHECK(s_space_calls == 1 && mark_at(ROOM, WIDE_MENU_PIECE_FILL(WIDE_PLAIN_SAVE),
                                        NV2A_WIDE_HUD_FILL));
    CHECK(mark_at(ROOM + 8, WIDE_MENU_STRIP, NV2A_WIDE_HUD_STRIP));
    /* nothing in 4:3, with no device or object, for no plain menu, or for an
     * object of another class (twice: the log says it once) */
    fresh();
    set(MENU, garage);
    CHECK(!widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_GARAGE, MENU, 0, 0, space));
    CHECK(!widescreen_menus_fill(mem(), 0, WIDE_PLAIN_GARAGE, MENU, 0, 1, space));
    CHECK(!widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_GARAGE, 0, 0, 1, space));
    CHECK(!widescreen_menus_fill(mem(), DEVICE, -1, MENU, 0, 1, space));
    CHECK(!widescreen_menus_fill(mem(), DEVICE, WIDE_PLAINS, MENU, 0, 1, space));
    set(MENU, widescreen_menus_plain[WIDE_PLAIN_SYSTEM].vtable);
    CHECK(!widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_GARAGE, MENU, 0, 1, space));
    CHECK(!widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_GARAGE, MENU, 0, 1, space));
    set(MENU, widescreen_menus[WIDE_MENU_STREET_PAUSE].vtable);
    CHECK(!widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_SYSTEM, MENU, 0, 1, space));
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER && word(BUFFER) == 0 && s_space_calls == 0);
}

/* A model at RIG with six nodes, their array out of the walk's order, and
 * its animation of four frames: the walk's n0 (its root), n1 its first
 * child, then n2 and n3 its siblings, n4 n3's child and n5 n4's sibling. */
#define ANIM   (RIG + 0x1000u)
#define TRACKS (RIG + 0x1100u)
#define KEYS   (RIG + 0x2000u)
static uint32_t node_va(unsigned walk)
{
    static const unsigned slot[6] = { 0, 3, 1, 4, 2, 5 };
    return RIG + 0x100u + WIDE_NODE_SIZE * slot[walk];
}
static void setf(uint32_t va, float f) { memcpy(mem() + va, &f, sizeof f); }
static void rig(void)
{
    /* each node's mesh, track flags, and how its keys move: 0 still, 1
     * along x, 2 along x and y, 3 along x and z */
    static const struct { uint32_t mesh; uint8_t flags; int moves; } n[6] = {
        { 0x7000u, 0x01, 0 },   /* n0: still */
        { 0x7010u, 0x01, 1 },   /* n1: slides */
        { 0,       0x01, 1 },   /* n2: slides, but no mesh */
        { 0x7030u, 0x11, 1 },   /* n3: slides, but turns too */
        { 0x7040u, 0x01, 1 },   /* n4: slides */
        { 0x7050u, 0x01, 2 },   /* n5: and climbs */
    };
    unsigned k, f;
    memset(mem() + RIG, 0, TOP - RIG);
    set(RIG + WIDE_MODEL_NODES, node_va(0));
    set(RIG + WIDE_MODEL_NODE_COUNT, 6);
    set(node_va(0) + WIDE_NODE_CHILD, node_va(1));
    set(node_va(1) + WIDE_NODE_SIBLING, node_va(2));
    set(node_va(2) + WIDE_NODE_SIBLING, node_va(3));
    set(node_va(3) + WIDE_NODE_CHILD, node_va(4));
    set(node_va(4) + WIDE_NODE_SIBLING, node_va(5));
    set(ANIM, 4u << 16);                           /* 16 bits at +2 */
    set(ANIM + WIDE_ANIM_TRACKS, TRACKS);
    for (k = 0; k < 6; k++) {
        uint32_t track = TRACKS + WIDE_TRACK_SIZE * k, keys = KEYS + 0x100u * k;
        set(node_va(k) + WIDE_NODE_MESH, n[k].mesh);
        set(track, 0xC0DE0000u | n[k].flags);        /* the byte alone */
        set(track + WIDE_TRACK_MOVE, keys);
        for (f = 0; f < 4; f++) {
            setf(keys + 12u * f, n[k].moves ? -40.0f + 30.0f * (float)f : 5.0f);
            setf(keys + 12u * f + 4, n[k].moves == 2 ? (float)f : 22.0f);
            setf(keys + 12u * f + 8, n[k].moves == 3 ? (float)f : 1.5f);
        }
    }
}

static void sliders(void)
{
    unsigned k, piece;
    int fill, began;
    fresh();
    rig();
    memset(mem() + MENU, 0, 0x200);
    set(MENU, widescreen_menus_plain[WIDE_PLAIN_SYSTEM].vtable);
    /* no plain menu draws */
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG, ANIM));
    CHECK(widescreen_menus_slider(RIG, node_va(1), 0x0005BD80u, &piece) == NV2A_WIDE_HUD_NONE);
    fill = widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_SYSTEM, MENU, 0, 1, space);
    CHECK(fill);
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG, 0));     /* no animation */
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, 0, ANIM));
    began = widescreen_menus_sliders_begin(mem(), TOP, RIG, ANIM);
    CHECK(began);
    for (k = 0; k < 6; k++) {
        piece = 999;
        CHECK(widescreen_menus_slider(RIG, node_va(k), 0x0005BD80u, &piece)
              == (k == 1 || k == 4 ? NV2A_WIDE_HUD_OVER : NV2A_WIDE_HUD_NONE));
        CHECK(piece == (k == 1 || k == 4 ? WIDE_MENU_PIECE_SLIDERS : 999u));
    }
    CHECK(widescreen_menus_slider(RIG + 0x40u, node_va(1), 0x0005BD80u, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG, ANIM));  /* one at a time */
    widescreen_menus_sliders_end(began);
    CHECK(widescreen_menus_slider(RIG, node_va(1), 0x0005BD80u, &piece) == NV2A_WIDE_HUD_NONE);
    began = widescreen_menus_sliders_begin(mem(), TOP, RIG, ANIM);   /* the next frame */
    CHECK(began);
    CHECK(widescreen_menus_slider(RIG, node_va(4), 0x0005BD80u, &piece) == NV2A_WIDE_HUD_OVER);
    widescreen_menus_sliders_end(began);
    /* along z too isn't sliding: n4 isn't found, and n1 alone is a knob,
     * too few for a band, left as drawn (twice: the log says it once) */
    CHECK(WIDE_MENU_SLIDERS_MIN == 2u);
    setf(KEYS + 0x400u + 12u * 2 + 8, 9.0f);
    set(RIG + 0x40u + WIDE_MODEL_NODES, node_va(0));
    set(RIG + 0x40u + WIDE_MODEL_NODE_COUNT, 6);
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG + 0x40u, ANIM));
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG + 0x40u, ANIM));
    CHECK(widescreen_menus_slider(RIG + 0x40u, node_va(1), 0x0005BD80u, &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_slider(RIG + 0x40u, node_va(4), 0x0005BD80u, &piece) == NV2A_WIDE_HUD_NONE);
    /* what doesn't read as a model: out of memory, a loop, too many nodes,
     * one frame */
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, TOP - 0x10u, ANIM));
    set(RIG + 0x80u + WIDE_MODEL_NODES, TOP - 0x10u);
    set(RIG + 0x80u + WIDE_MODEL_NODE_COUNT, 6);
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG + 0x80u, ANIM));
    set(RIG + 0xC0u + WIDE_MODEL_NODES, node_va(0));
    set(RIG + 0xC0u + WIDE_MODEL_NODE_COUNT, 6);
    set(node_va(5) + WIDE_NODE_SIBLING, node_va(0));
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG + 0xC0u, ANIM));
    CHECK(widescreen_menus_slider(RIG + 0xC0u, node_va(1), 0x0005BD80u, &piece) == NV2A_WIDE_HUD_NONE);
    set(node_va(5) + WIDE_NODE_SIBLING, node_va(0) + 4u);           /* not a node */
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG, ANIM + 0x10u));
    set(node_va(5) + WIDE_NODE_SIBLING, 0);
    set(RIG + 0xC0u + WIDE_MODEL_NODE_COUNT, 5);                    /* the walk finds 6 */
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG + 0xC0u, ANIM + 0x20u));
    set(ANIM + 0x30u, 1u << 16);
    set(ANIM + 0x30u + WIDE_ANIM_TRACKS, TRACKS);
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG, ANIM + 0x30u));
    widescreen_menus_fill_end(fill);
    /* after the menu, nothing */
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG, ANIM));
    /* the music menu's are left as drawn, though they slide: none found
     * (twice: the log says it once) */
    fresh();
    rig();
    memset(mem() + MENU, 0, 0x200);
    set(MENU, widescreen_menus_plain[WIDE_PLAIN_MUSIC].vtable);
    fill = widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_MUSIC, MENU, 0, 1, space);
    CHECK(fill);
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG, ANIM));
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG, ANIM));
    CHECK(widescreen_menus_slider(RIG, node_va(1), 0x0005BD80u, &piece) == NV2A_WIDE_HUD_NONE);
    widescreen_menus_fill_end(fill);
    /* the marks themselves are written by widescreen_menus_mark, side OVER
     * (marks) */
}

/* The wider rule, for a model with no node the first finds: n0 still; n1
 * slides with its scale keyed too; n2 slides, but has no mesh; n3 has no
 * mesh and slides and turns, carrying its children: n4, not keyed, and n5,
 * which climbs. */
static void sliders_wider(void)
{
    static const struct { uint32_t mesh; uint8_t flags; int moves; } n[6] = {
        { 0x7000u, 0x01, 0 }, { 0x7010u, 0x05, 1 }, { 0, 0x01, 1 },
        { 0,       0x11, 1 }, { 0x7040u, 0x00, 0 }, { 0x7050u, 0x01, 2 },
    };
    unsigned k, piece;
    int fill, began;
    fresh();
    rig();
    for (k = 0; k < 6; k++) {
        set(node_va(k) + WIDE_NODE_MESH, n[k].mesh);
        set(TRACKS + WIDE_TRACK_SIZE * k, 0xC0DE0000u | n[k].flags);
    }
    memset(mem() + MENU, 0, 0x200);
    set(MENU, widescreen_menus_plain[WIDE_PLAIN_OPTIONS].vtable);
    fill = widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_OPTIONS, MENU, 0, 1, space);
    CHECK(fill);
    began = widescreen_menus_sliders_begin(mem(), TOP, RIG, ANIM);
    CHECK(began);
    for (k = 0; k < 6; k++) {
        piece = 999;
        CHECK(widescreen_menus_slider(RIG, node_va(k), 0x0005C990u, &piece)
              == (k == 1 || k == 4 ? NV2A_WIDE_HUD_OVER : NV2A_WIDE_HUD_NONE));
        CHECK(piece == (k == 1 || k == 4 ? WIDE_MENU_PIECE_SLIDERS : 999u));
    }
    /* named once a function: a second time, and the first rule's again */
    CHECK(widescreen_menus_slider(RIG, node_va(4), 0x0005C990u, &piece) == NV2A_WIDE_HUD_OVER);
    CHECK(widescreen_menus_slider(RIG, node_va(1), 0x0005BD80u, &piece) == NV2A_WIDE_HUD_OVER);
    widescreen_menus_sliders_end(began);
    /* none by either: n1 still too, n3 not keyed; the log lists the nodes */
    set(TRACKS + WIDE_TRACK_SIZE * 1, 0xC0DE0000u);
    set(TRACKS + WIDE_TRACK_SIZE * 3, 0xC0DE0000u);
    set(RIG + 0x40u + WIDE_MODEL_NODES, node_va(0));
    set(RIG + 0x40u + WIDE_MODEL_NODE_COUNT, 6);
    CHECK(!widescreen_menus_sliders_begin(mem(), TOP, RIG + 0x40u, ANIM));
    CHECK(widescreen_menus_slider(RIG + 0x40u, node_va(4), 0x0005C990u, &piece)
          == NV2A_WIDE_HUD_NONE);
    widescreen_menus_fill_end(fill);
}

/* The arrows, for a model whose animation slides other parts along x alone
 * besides its rectangles, as the garage's menus slide their three arrows
 * along the bottom: as a chain, each carried by the one before and sliding
 * out from it, all at the keys' one height (9e02d60-arrows1's log). Nine
 * nodes, in the walk's order in their array at CHAIN. First as the system
 * and graffiti menus' model, by the first rule (drawn by the test run menu,
 * whose models no other case lists): n0 still; n1, n2 and n3 slide side by
 * side, the band; n4, the first arrow, still, carries n5, the second,
 * sliding out from it, which carries n6, the third, sliding out from n5.
 * All are stretched alike but n0: before, n4 wasn't, so the arrows came
 * apart (Adri on GitHub issue 2, 10 November 2026: in the system and
 * graffiti menus, one arrow overlapped the next). n4 is the chain's anchor
 * (NV2A_WIDE_HUD_ANCHOR), and it, n5 and n6 are stretched about its origin
 * (NV2A_WIDE_HUD_FROM_ANCHOR), so the arrows go out from where the first
 * one's origin lands, and still start at the stop symbol under it, which
 * isn't stretched; the band, about the screen's centre. Then by the
 * wider rule, every node turned too: n3 carries n4, a still piece of it,
 * which goes with the band; n5, still, carries n6, a rectangle of the band,
 * and n7, sliding, which carries n8, sliding out from it. The chain n7 and
 * n8 is stretched with the band, about the screen's centre, but not n5,
 * which carries a rectangle too, and so isn't the chain's anchor. */
#define CHAIN (RIG + 0x800u)
static uint32_t chain_va(unsigned walk) { return CHAIN + WIDE_NODE_SIZE * walk; }
static void sliders_band(void)
{
    /* each node's track flags and how its keys slide it: 0 still, 1 as
     * the band, from far off one side to past the other, 2 out from the
     * one that carries it, as the game's arrows do */
    static const struct { uint8_t flags; int slides; } sys[9] = {
        { 0x01, 0 }, { 0x01, 1 }, { 0x01, 1 }, { 0x01, 1 },
        { 0x01, 0 }, { 0x01, 2 }, { 0x01, 2 }, { 0x00, 0 }, { 0x00, 0 },
    }, wide[9] = {
        { 0x11, 0 }, { 0x11, 1 }, { 0x11, 1 }, { 0x11, 1 },
        { 0x10, 0 }, { 0x11, 0 }, { 0x11, 1 }, { 0x11, 2 }, { 0x11, 2 },
    };
    const uint32_t system = RIG + 0x20u, practice = RIG + 0x60u;
    unsigned k, f, piece;
    int fill, began;
    fresh();
    rig();
    for (k = 0; k < 9; k++) {
        uint32_t track = TRACKS + WIDE_TRACK_SIZE * k, keys = KEYS + 0x100u * k;
        set(chain_va(k) + WIDE_NODE_MESH, 0x7100u + 0x10u * k);
        set(track, 0xC0DE0000u | sys[k].flags);
        set(track + WIDE_TRACK_MOVE, keys);
        for (f = 0; f < 4; f++) {
            setf(keys + 12u * f, sys[k].slides == 1 ? -40.0f + 30.0f * (float)f
                               : sys[k].slides == 2 ? 4.0f * (float)f : 0.0f);
            setf(keys + 12u * f + 4, 0.0f);
            setf(keys + 12u * f + 8, 0.0f);
        }
    }
    set(chain_va(0) + WIDE_NODE_CHILD, chain_va(1));
    set(chain_va(1) + WIDE_NODE_SIBLING, chain_va(2));
    set(chain_va(2) + WIDE_NODE_SIBLING, chain_va(3));
    set(chain_va(3) + WIDE_NODE_SIBLING, chain_va(4));
    set(chain_va(4) + WIDE_NODE_CHILD, chain_va(5));
    set(chain_va(5) + WIDE_NODE_CHILD, chain_va(6));
    set(system + WIDE_MODEL_NODES, chain_va(0));
    set(system + WIDE_MODEL_NODE_COUNT, 7);
    memset(mem() + MENU, 0, 0x200);
    set(MENU, widescreen_menus_plain[WIDE_PLAIN_TEST_RUN].vtable);
    fill = widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_TEST_RUN, MENU, 0, 1, space);
    CHECK(fill);
    began = widescreen_menus_sliders_begin(mem(), TOP, system, ANIM);
    CHECK(began);
    for (k = 0; k < 7; k++) {
        piece = 999;
        CHECK(widescreen_menus_slider(system, chain_va(k), 0x0005BD80u, &piece)
              == (k >= 1 ? NV2A_WIDE_HUD_OVER : NV2A_WIDE_HUD_NONE));
        CHECK(piece == (k >= 1 ? WIDE_MENU_PIECE_SLIDERS : 999u));
        CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SLIDERS, NV2A_WIDE_HUD_OVER)
              == (k == 4 ? NV2A_WIDE_HUD_ANCHOR | NV2A_WIDE_HUD_FROM_ANCHOR
                  : k >= 5 ? NV2A_WIDE_HUD_FROM_ANCHOR : 0u));
        /* only the moving rectangles' mark over the sides has them */
        CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SLIDERS, NV2A_WIDE_HUD_ACROSS) == 0u);
    }
    /* the mark the node function's override pushes for n5 */
    CHECK(widescreen_menus_slider(system, chain_va(5), 0x0005BD80u, &piece)
          == NV2A_WIDE_HUD_OVER);
    {
        uint32_t put = word(DEVICE + WIDE_HUD_D3D_PUT);
        CHECK(widescreen_menus_mark(mem(), DEVICE, piece, NV2A_WIDE_HUD_OVER, space));
        CHECK(word(put + 4) == nv2a_wide_hud_mark(WIDE_MENU_PIECE_SLIDERS, NV2A_WIDE_HUD_OVER,
                                                  NV2A_WIDE_HUD_FROM_ANCHOR));
        widescreen_menus_unmark(mem(), DEVICE, piece, 1, space);
    }
    widescreen_menus_sliders_end(began);
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SLIDERS, NV2A_WIDE_HUD_OVER) == 0u);
    widescreen_menus_fill_end(fill);
    /* by the wider rule: n3 carries n4; n5, beside n3, carries n6 and n7,
     * and n7 carries n8 */
    for (k = 0; k < 9; k++) {
        set(TRACKS + WIDE_TRACK_SIZE * k, 0xC0DE0000u | wide[k].flags);
        for (f = 0; f < 4; f++)
            setf(KEYS + 0x100u * k + 12u * f, wide[k].slides == 1 ? -40.0f + 30.0f * (float)f
                                            : wide[k].slides == 2 ? 4.0f * (float)f : 0.0f);
    }
    set(chain_va(3) + WIDE_NODE_SIBLING, chain_va(5));
    set(chain_va(3) + WIDE_NODE_CHILD, chain_va(4));
    set(chain_va(4) + WIDE_NODE_CHILD, 0);
    set(chain_va(5) + WIDE_NODE_CHILD, chain_va(6));
    set(chain_va(6) + WIDE_NODE_CHILD, 0);
    set(chain_va(6) + WIDE_NODE_SIBLING, chain_va(7));
    set(chain_va(7) + WIDE_NODE_CHILD, chain_va(8));
    set(practice + WIDE_MODEL_NODES, chain_va(0));
    set(practice + WIDE_MODEL_NODE_COUNT, 9);
    memset(mem() + MENU, 0, 0x200);
    set(MENU, widescreen_menus_plain[WIDE_PLAIN_PRACTICE].vtable);
    fill = widescreen_menus_fill(mem(), DEVICE, WIDE_PLAIN_PRACTICE, MENU, 0, 1, space);
    CHECK(fill);
    began = widescreen_menus_sliders_begin(mem(), TOP, practice, ANIM);
    CHECK(began);
    for (k = 0; k < 9; k++) {
        CHECK(widescreen_menus_slider(practice, chain_va(k), 0x0005BD80u, &piece)
              == (k >= 1 && k != 5 ? NV2A_WIDE_HUD_OVER : NV2A_WIDE_HUD_NONE));
        CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_SLIDERS, NV2A_WIDE_HUD_OVER) == 0u);
    }
    widescreen_menus_sliders_end(began);
    widescreen_menus_fill_end(fill);
}

#define PACK (RIG + 0x3000u)   /* where a pointer can be */
static void title(void)
{
    static const struct { uint32_t state; int wide; } st[] = {
        { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 1 }, { 5, 1 }, { 6, 1 }, { 4, 1 }, { 11, 1 },
    };
    unsigned k, piece;
    int began;
    fresh();
    memset(mem() + MENU, 0, 0x200);
    set(MENU, WIDE_TITLE_CLASS);
    set(MENU + WIDE_TITLE_PACK, PACK);
    set(PACK + WIDE_TITLE_LOGO, model(0));
    set(PACK + WIDE_TITLE_PRESS, model(1));
    set(PACK + 8, model(2));                                /* the centred one */
    CHECK(!widescreen_menus_title_wide(70.0));              /* never drawn */
    for (k = 0; k < sizeof st / sizeof st[0]; k++) {
        set(MENU + WIDE_TITLE_STATE, st[k].state);
        widescreen_menus_title(mem(), MENU, 70.0 + k);
        widescreen_menus_title(mem(), MENU, 70.0 + k);     /* the same state: no line */
        CHECK(widescreen_menus_title_wide(70.0 + k) == st[k].wide);
        CHECK(widescreen_menus_title_wide(70.0 + k + WIDE_MENU_RECENT) == st[k].wide);
        CHECK(!widescreen_menus_title_wide(70.0 + k + 0.3));
        /* its own bars across, widened, but in 6 */
        CHECK(widescreen_menus_title_bars(mem(), MENU, 1) == (st[k].state != WIDE_TITLE_CITY));
        CHECK(!widescreen_menus_title_bars(mem(), MENU, 0));
    }
    CHECK(!widescreen_menus_title_bars(mem(), 0, 1));
    set(MENU + WIDE_TITLE_STATE, 6);
    set(MENU, 0x001D2A00u);                                 /* another class */
    widescreen_menus_title(mem(), MENU, 90.0);
    CHECK(!widescreen_menus_title_wide(90.0));
    widescreen_menus_title(mem(), 0, 90.0);
    CHECK(!widescreen_menus_title_wide(90.0));
    CHECK(!widescreen_menus_title_press_begin(mem(), MENU, 1));
    set(MENU + WIDE_TITLE_STATE, 3);
    CHECK(!widescreen_menus_title_bars(mem(), MENU, 1));
    set(MENU, WIDE_TITLE_CLASS);
    /* its state 6 draw: model 1 to the left edge, model 0 to the right,
     * model 2 where it is */
    CHECK(!widescreen_menus_title_press_begin(mem(), MENU, 0));    /* 4:3 */
    CHECK(widescreen_menus_part(mem(), model(1), &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_part(mem(), model(0), &piece) == NV2A_WIDE_HUD_NONE);
    began = widescreen_menus_title_press_begin(mem(), MENU, 1);
    CHECK(began);
    CHECK(!widescreen_menus_title_press_begin(mem(), MENU, 1));    /* one at a time */
    piece = 999;
    CHECK(widescreen_menus_part(mem(), model(2), &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(piece == 999);
    CHECK(widescreen_menus_part(mem(), model(0), &piece) == NV2A_WIDE_HUD_RIGHT);
    CHECK(piece == WIDE_MENU_PIECE_LOGO);
    CHECK(widescreen_menus_part(mem(), model(1), &piece) == NV2A_WIDE_HUD_LEFT);
    CHECK(piece == WIDE_MENU_PIECE_TITLE);
    CHECK(widescreen_menus_part(mem(), model(1), &piece) == NV2A_WIDE_HUD_LEFT);
    CHECK(widescreen_menus_part(mem(), model(0), &piece) == NV2A_WIDE_HUD_RIGHT);
    widescreen_menus_title_press_end(began);
    CHECK(widescreen_menus_part(mem(), model(1), &piece) == NV2A_WIDE_HUD_NONE);
    CHECK(widescreen_menus_part(mem(), model(0), &piece) == NV2A_WIDE_HUD_NONE);
    set(MENU + WIDE_TITLE_PACK, 0);                         /* no pack */
    CHECK(!widescreen_menus_title_press_begin(mem(), MENU, 1));
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER);
}

static void time_move(void);

static void results(void)
{
    int began, marked;
    CHECK(widescreen_menus_results[WIDE_RESULT_CHOICE].va == 0x0010F670u
          && widescreen_menus_results[WIDE_RESULT_CHOICE].vtable == 0x001D24F0u
          && widescreen_menus_results[WIDE_RESULT_CHOICE].piece == WIDE_MENU_PIECE_CHOICE);
    CHECK(widescreen_menus_results[WIDE_RESULT_TIME].va == 0x0010EC10u
          && widescreen_menus_results[WIDE_RESULT_TIME].vtable == 0x001D23F0u
          && widescreen_menus_results[WIDE_RESULT_TIME].piece == WIDE_MENU_PIECE_TIME);
    CHECK(WIDE_RESULTS == 2);
    CHECK(WIDE_MENU_PIECE_RESULT > WIDE_MENU_PIECE_COURSE
          && WIDE_MENU_PIECE_CHOICE > WIDE_MENU_PIECE_RESULT
          && WIDE_MENU_PIECE_TIME > WIDE_MENU_PIECE_CHOICE
          && WIDE_MENU_PIECE_TIME < WIDE_MENU_PIECE_FILL0);
    CHECK(widescreen_menus_mark_flags(WIDE_MENU_PIECE_RESULT, NV2A_WIDE_HUD_RIGHT) == 0u
          && widescreen_menus_mark_flags(WIDE_MENU_PIECE_CHOICE, NV2A_WIDE_HUD_RIGHT) == 0u
          && widescreen_menus_mark_flags(WIDE_MENU_PIECE_TIME, NV2A_WIDE_HUD_RIGHT) == 0u);
    fresh();
    memset(mem() + MENU, 0, 0x200);
    CHECK(!widescreen_menus_result_text());              /* nothing drawing */
    set(MENU, 0x001D24F0u);
    CHECK(!widescreen_menus_result(mem(), WIDE_RESULT_CHOICE, MENU, 0, 0));   /* 4:3 */
    CHECK(!widescreen_menus_result(mem(), WIDE_RESULT_CHOICE, MENU, 7, 1));   /* pass 7 */
    CHECK(!widescreen_menus_result(mem(), WIDE_RESULT_CHOICE, 0, 0, 1));
    CHECK(!widescreen_menus_result(mem(), WIDE_RESULTS, MENU, 0, 1));
    CHECK(!widescreen_menus_result(mem(), -1, MENU, 0, 1));
    set(MENU, 0x001D2538u);                              /* another class */
    CHECK(!widescreen_menus_result(mem(), WIDE_RESULT_CHOICE, MENU, 0, 1));
    CHECK(!widescreen_menus_result(mem(), WIDE_RESULT_CHOICE, MENU, 0, 1));   /* said once */
    set(MENU, 0x001D24F0u);
    began = widescreen_menus_result(mem(), WIDE_RESULT_CHOICE, MENU, 0, 1);
    CHECK(began);
    CHECK(widescreen_menus_result_text());               /* its label, at once */
    marked = widescreen_menus_mark(mem(), DEVICE, WIDE_MENU_PIECE_CHOICE, NV2A_WIDE_HUD_RIGHT,
                                   space);
    CHECK(marked && mark_at(BUFFER, WIDE_MENU_PIECE_CHOICE, NV2A_WIDE_HUD_RIGHT));
    widescreen_menus_unmark(mem(), DEVICE, WIDE_MENU_PIECE_CHOICE, marked, space);
    CHECK(mark_at(BUFFER + 8, WIDE_MENU_PIECE_CHOICE, NV2A_WIDE_HUD_NONE));
    widescreen_menus_result_end(WIDE_RESULT_CHOICE, began);
    CHECK(!widescreen_menus_result_text());
    began = widescreen_menus_result(mem(), WIDE_RESULT_CHOICE, MENU, 8, 1);   /* pass 8 */
    CHECK(began);
    CHECK(widescreen_menus_result_text());
    CHECK(widescreen_menus_result_text());
    widescreen_menus_result_end(WIDE_RESULT_CHOICE, began);
    CHECK(!widescreen_menus_result_text());
    CHECK(!widescreen_menus_result(mem(), WIDE_RESULT_TIME, MENU, 0, 1));     /* another class */
    /* the race time: the race HUD's timer (modes 2 and 3) stays where it is,
     * on every pass; the result screen's (modes 4 and 6) moves, on its four
     * passes only */
    set(MENU, 0x001D23F0u);
    set(MENU + WIDE_RESULT_MODE, 2);
    {
        static const uint32_t passes[] = { 0, 3, 5, 8 }, others[] = { 1, 2, 4, 6, 7, 9 };
        static const uint32_t race[] = { 2, 3, 0, 1, 5, 7, 64 }, result[] = { 4, 6 };
        unsigned p, k;
        for (k = 0; k < sizeof race / sizeof race[0]; k++) {
            set(MENU + WIDE_RESULT_MODE, race[k]);
            for (p = 0; p < 10; p++)
                CHECK(!widescreen_menus_result(mem(), WIDE_RESULT_TIME, MENU, p, 1));
        }
        for (k = 0; k < sizeof result / sizeof result[0]; k++) {
            set(MENU + WIDE_RESULT_MODE, result[k]);
            CHECK(!widescreen_menus_result(mem(), WIDE_RESULT_TIME, MENU, 0, 0));   /* 4:3 */
            for (p = 0; p < sizeof others / sizeof others[0]; p++)
                CHECK(!widescreen_menus_result(mem(), WIDE_RESULT_TIME, MENU, others[p], 1));
            for (p = 0; p < sizeof passes / sizeof passes[0]; p++) {
                began = widescreen_menus_result(mem(), WIDE_RESULT_TIME, MENU, passes[p], 1);
                CHECK(began);
                CHECK(!widescreen_menus_result_text());  /* the race time has no label */
                widescreen_menus_result_end(WIDE_RESULT_TIME, began);
            }
        }
    }
    fresh();                                             /* clears the object too */
    set(MENU, 0x001D23F0u);
    set(MENU + WIDE_RESULT_MODE, 4);
    began = widescreen_menus_result(mem(), WIDE_RESULT_TIME, MENU, 3, 1);
    marked = widescreen_menus_mark(mem(), DEVICE, WIDE_MENU_PIECE_TIME, NV2A_WIDE_HUD_RIGHT,
                                   space);
    CHECK(began && marked && mark_at(BUFFER, WIDE_MENU_PIECE_TIME, NV2A_WIDE_HUD_RIGHT));
    widescreen_menus_unmark(mem(), DEVICE, WIDE_MENU_PIECE_TIME, marked, space);
    CHECK(mark_at(BUFFER + 8, WIDE_MENU_PIECE_TIME, NV2A_WIDE_HUD_NONE));
    widescreen_menus_result_end(WIDE_RESULT_TIME, began);
    time_move();
}

/* The race time's further move: its places as the game has them (run-9,
 * part 2, and search-9, C: four tables of floats, a row of two (x, y) for
 * each of seven modes; the separators' rows other than modes 4 and 6 are
 * made up here), the hundredths' last digit's ink lined up with the
 * banner's band's end, its mode's rows moved for the draw and put back. */
static int near(float a, float b) { return fabsf(a - b) < 0.01f; }
static float getf(uint32_t va) { float f; memcpy(&f, mem() + va, sizeof f); return f; }
static uint32_t place_at(unsigned table, unsigned mode, unsigned digit)
{
    return WIDE_RESULT_PLACES + table * WIDE_RESULT_TABLE + mode * WIDE_RESULT_ROW
         + digit * WIDE_RESULT_PLACE;
}
static void time_places(void)
{
    static const float x[WIDE_RESULT_TABLES][WIDE_RESULT_MODES][WIDE_RESULT_DIGITS] = {
        { { 246, 271 }, { 188, 204 }, { 246, 271 }, { 246, 271 }, { 278, 294 }, { 188, 204 },
          { 246, 271 } },
        { { 303, 328 }, { 236, 252 }, { 303, 328 }, { 303, 328 }, { 326, 342 }, { 236, 252 },
          { 303, 328 } },
        { { 363, 379 }, { 284, 300 }, { 361, 377 }, { 361, 377 }, { 374, 390 }, { 284, 300 },
          { 361, 377 } },
        { { 291, 349 }, { 220, 268 }, { 291, 349 }, { 291, 349 }, { 310, 358 }, { 220, 268 },
          { 291, 349 } } };
    static const float y[WIDE_RESULT_MODES] = { 42, 203, 42, 42, 203, 203, 203 };
    unsigned table, mode, d;
    for (table = 0; table < WIDE_RESULT_TABLES; table++)
        for (mode = 0; mode < WIDE_RESULT_MODES; mode++)
            for (d = 0; d < WIDE_RESULT_DIGITS; d++) {
                setf(place_at(table, mode, d), x[table][mode][d]);
                setf(place_at(table, mode, d) + 4,
                     table == WIDE_RESULT_TABLES - 1 && mode == 6 ? 207.0f : y[mode]);
            }
}
static void time_move(void)
{
    static uint8_t before[WIDE_RESULT_TABLES * WIDE_RESULT_TABLE];
    WideTimePlaces saved;
    unsigned table, mode, d;
    int began;
    CHECK(WIDE_RESULT_PLACES + WIDE_RESULT_HUNDREDTHS * WIDE_RESULT_TABLE == 0x00221510u);
    CHECK(WIDE_RESULT_PLACES + (WIDE_RESULT_TABLES - 1u) * WIDE_RESULT_TABLE == 0x00221580u);
    CHECK(WIDE_RESULT_PLACES + WIDE_RESULT_TABLES * WIDE_RESULT_TABLE <= GUEST);
    fresh();
    time_places();
    memcpy(before, mem() + WIDE_RESULT_PLACES, sizeof before);
    set(MENU, 0x001D23F0u);
    CHECK(widescreen_menus_result_time_move(mem(), 0) == 0.0f);
    set(MENU + WIDE_RESULT_MODE, 6);                     /* YOU WIN: 248 to 389 */
    CHECK(near(widescreen_menus_result_time_move(mem(), MENU), 164.0f));
    CHECK(near(widescreen_menus_result_time_move(mem(), MENU), 164.0f));   /* said once */
    set(MENU + WIDE_RESULT_MODE, 4);                     /* mode 4: to 402 */
    CHECK(near(widescreen_menus_result_time_move(mem(), MENU), 151.0f));
    /* placed for a split screen: a quarter's (x, y), or 40 down */
    set(MENU + WIDE_RESULT_TIME_FLAGS, WIDE_RESULT_TIME_SPLIT);
    CHECK(widescreen_menus_result_time_move(mem(), MENU) == 0.0f);
    set(MENU + WIDE_RESULT_TIME_FLAGS, 0);
    setf(MENU + WIDE_RESULT_TIME_Y, 40.0f);
    CHECK(widescreen_menus_result_time_move(mem(), MENU) == 0.0f);   /* said once */
    setf(MENU + WIDE_RESULT_TIME_Y, 0.0f);
    CHECK(!widescreen_menus_result_time_begin(mem(), MENU + 0x100u, &saved));   /* mode 0 */
    set(MENU + WIDE_RESULT_MODE, 2);                     /* the race HUD's timer */
    CHECK(widescreen_menus_result_time_move(mem(), MENU) == 0.0f);
    set(MENU + WIDE_RESULT_MODE, 3);
    CHECK(widescreen_menus_result_time_move(mem(), MENU) == 0.0f);
    set(MENU + WIDE_RESULT_MODE, WIDE_RESULT_MODES);     /* past the tables */
    CHECK(widescreen_menus_result_time_move(mem(), MENU) == 0.0f);
    set(MENU + WIDE_RESULT_MODE, 6);
    setf(place_at(WIDE_RESULT_HUNDREDTHS, 6, 1), 560.0f);   /* already past the banner's end */
    CHECK(widescreen_menus_result_time_move(mem(), MENU) == 0.0f);
    setf(place_at(WIDE_RESULT_HUNDREDTHS, 6, 1), NAN);      /* not a place */
    CHECK(widescreen_menus_result_time_move(mem(), MENU) == 0.0f);
    setf(place_at(WIDE_RESULT_HUNDREDTHS, 6, 1), -400.0f);  /* too far to be one */
    CHECK(widescreen_menus_result_time_move(mem(), MENU) == 0.0f);
    CHECK(!widescreen_menus_result_time_begin(mem(), MENU, &saved));
    CHECK(memcmp(mem() + WIDE_RESULT_PLACES, before, sizeof before) != 0);
    time_places();
    CHECK(memcmp(mem() + WIDE_RESULT_PLACES, before, sizeof before) == 0);
    /* the draw: mode 6's rows, all four tables', moved 164 right, y as they
     * were, every other mode's as it was; then each word put back */
    began = widescreen_menus_result_time_begin(mem(), MENU, &saved);
    CHECK(began);
    for (table = 0; table < WIDE_RESULT_TABLES; table++)
        for (mode = 0; mode < WIDE_RESULT_MODES; mode++)
            for (d = 0; d < WIDE_RESULT_DIGITS; d++) {
                uint32_t at = place_at(table, mode, d);
                float was, y_was;
                memcpy(&was, before + (at - WIDE_RESULT_PLACES), sizeof was);
                memcpy(&y_was, before + (at + 4 - WIDE_RESULT_PLACES), sizeof y_was);
                CHECK(near(getf(at), mode == 6 ? was + 164.0f : was));
                CHECK(getf(at + 4) == y_was);
            }
    CHECK(near(getf(place_at(WIDE_RESULT_HUNDREDTHS, 6, 1)) + WIDE_RESULT_INK,
               WIDE_RESULT_BANNER_RIGHT));
    widescreen_menus_result_time_end(mem(), &saved, began);
    CHECK(memcmp(mem() + WIDE_RESULT_PLACES, before, sizeof before) == 0);
    /* a place that isn't a whole number comes back word for word */
    setf(place_at(0, 4, 0), 278.3f);
    memcpy(before, mem() + WIDE_RESULT_PLACES, sizeof before);
    set(MENU + WIDE_RESULT_MODE, 4);
    began = widescreen_menus_result_time_begin(mem(), MENU, &saved);
    CHECK(began && near(getf(place_at(0, 4, 0)), 278.3f + 151.0f));
    widescreen_menus_result_time_end(mem(), &saved, began);
    CHECK(memcmp(mem() + WIDE_RESULT_PLACES, before, sizeof before) == 0);
    widescreen_menus_result_time_end(mem(), &saved, 0);   /* not begun: nothing */
    CHECK(memcmp(mem() + WIDE_RESULT_PLACES, before, sizeof before) == 0);
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
    map();
    plain();
    sliders();
    sliders_wider();
    sliders_band();
    title();
    results();
    if (s_failures) {
        printf("widescreen_menus: %d failed\n", s_failures);
        return 1;
    }
    printf("widescreen_menus: all cases pass\n");
    return 0;
}
