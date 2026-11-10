/*
 * menu.h -- the in-game menu's contents (F1, or Back on a pad).
 *
 * The menu is a bar of menus, each a list of rows, and any part of the game
 * can add rows to it. Whoever owns a setting adds them: a row carries the
 * owner's own functions, so the setting keeps living where it always did
 * (nv2a_gl.c for the window's) and the menu holds no copy of it. Nor does
 * the menu touch the settings file: the owner saves what it changes, as its
 * hotkey does, through the engine's one settings file (nv2a_gl.c's, which
 * portable mode moves beside the game).
 *
 * The menu is built where the window can show it (NV2A_GL_MENU, in
 * src/nv2a_gl/CMakeLists.txt); linking xbox_menu then defines
 * XBOXRECOMP_MENU, and code that adds rows goes under it. A page, written
 * where its settings live:
 *
 *     #if defined(XBOXRECOMP_MENU)
 *     #include "menu.h"
 *     static const XBOX_MENU_ITEM rows[] = {
 *         { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_TOGGLE,
 *           .label = "Fullscreen", .state = is_fullscreen, .run = toggle_fullscreen,
 *           .hint = "The window becomes fullscreen." },
 *     };
 *     xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
 *     #endif
 *
 * Plain C; menu.cpp draws the rows with Dear ImGui. Labels, hints,
 * (Restart), boxes and File > About follow docs/menu-format.md.
 */
#ifndef XBOX_MENU_H
#define XBOX_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/* The menus, left to right. A new one is a line here and its title in
 * menu.cpp; a menu nobody has added rows to is not shown. */
typedef enum {
    XBOX_MENU_FILE,
    XBOX_MENU_DISPLAY,
    XBOX_MENU_GRAPHICS,
    XBOX_MENU_AUDIO,
    XBOX_MENU_CONTROLLER,
    XBOX_MENU_DEVELOPER,
    XBOX_MENU_COUNT
} XBOX_MENU_ID;

typedef enum {
    XBOX_ITEM_ACTION,       /* run() when chosen                              */
    XBOX_ITEM_TOGGLE,       /* ticked while state() is nonzero; run() flips it */
    XBOX_ITEM_CHOICE,       /* a submenu of choices[]; the ticked one is
                             * state(), pick(i) when another is chosen        */
    XBOX_ITEM_INFO,         /* text() in a box, with OK                       */
    XBOX_ITEM_SEPARATOR,
    XBOX_ITEM_NUMBER,       /* value() in a box, with - and + either side: a
                             * step down or up, from min to max; set(v)       */
    XBOX_ITEM_TOGGLES       /* a submenu of choices[], each ticked while its
                             * bit in state() is set; pick(i) flips it        */
} XBOX_ITEM_KIND;

/* state, run, pick, text, value and set are called on the window's thread
 * while the game runs on: keep them to what the owner's hotkey would do. */
typedef struct XBOX_MENU_ITEM {
    XBOX_MENU_ID    menu;
    XBOX_ITEM_KIND  kind;
    const char     *label;
    int           (*state)(void);
    void          (*run)(void);
    void          (*pick)(int index);
    const char *const *choices;
    int             nchoices;
    const char   *(*text)(void);
    /* Asked before run() or pick() when set -- for what cannot be taken
     * back, such as a restart. Shown under the item (or the choice). */
    const char     *confirm;
    /* XBOX_ITEM_NUMBER: format shows value() (printf, one double). - and +
     * go to the step below or above -- min, min + step, ... -- within min
     * and max, and set() is called with it. */
    double        (*value)(void);
    void          (*set)(double v);
    double          min, max, step;
    const char     *format;
    /* Nonzero: drawn below the menu's other rows, whenever it was added --
     * Display's Skip intro, added as the game starts, below the window's
     * rows, added once the window is up. */
    int             last;
    /* Set: drawn right below the row of its menu with this label, whenever
     * either was added, as a row added at start goes under one of the
     * window's. Rows under the same row keep the order they were added in.
     * The row named is one that isn't under another itself; with no such
     * row, this one is drawn below the menu's other rows (above any `last`
     * row), and the log says so once. */
    const char     *under;
    /* What the row does, in one sentence of at most MENU_HINT_LONGEST
     * characters, shown along the bottom while the pad or the pointer is on
     * the row or its choices (docs/menu-format.md, Hints). Every row but a
     * separator has one: the log says so once if one hasn't. */
    const char     *hint;
} XBOX_MENU_ITEM;

/* Two lines at 640 x 480, beside the pad's buttons. */
#define MENU_HINT_LONGEST 70

/* Adds rows to the end of their menus (above any `last` row), or under the
 * row each names, copied: the table can be on the caller's stack. Any
 * thread, at any time, before the window opens or after; they are drawn
 * from the next frame. Rows added in one call stay together, so a module
 * adds its rows in one call. */
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count);

/* The window's side (menu_sdl.cpp, and tests/menu): one frame of the menu,
 * with the game dimmed behind it and a strip along the bottom, between
 * ImGui::NewFrame() and ImGui::Render(), on the window's thread. It stays
 * until the window shuts it (F1, Back). just_opened: the first frame after
 * opening -- the bar alone, the pad on the first menu's title, so a pad has
 * somewhere to start. pad: the player last used a pad, not the keyboard, so
 * the strip shows the pad's buttons, not the keys. */
void menu_build(int just_opened, int pad);

/* The hint the strip along the bottom showed in the last menu_build: the
 * row the pad is on, else the one the pointer is on; NULL on the bar, or in
 * a box. Read by tests/menu. */
const char *menu_hint(void);

/* The window's side too, before ImGui::NewFrame(): how far the menu has
 * faded in, from 0 (gone) to 1, `seconds` after it was `fade`, as it opens
 * (open nonzero) or shuts. It comes or goes in MENU_FADE_SECONDS, and is
 * never 0 while open. The window draws the menu at that alpha
 * (ImGuiStyle::Alpha) until it is 0 again; while it shuts, the menu takes no
 * input: what the backend read for the frame is dropped. */
#define MENU_FADE_SECONDS 0.10f
float menu_fade(float fade, int open, float seconds);

/* The window's side too: a box in the middle of the window, over whatever is
 * behind it, saying how far something the game is waiting for has got --
 * `what`, and a bar of done out of total (nv2a_gl.c's shader cache, built
 * before the first frame). Between ImGui::NewFrame() and ImGui::Render(), with
 * the menu or without it. It takes no input. */
void menu_build_busy(const char *what, int done, int total);

#ifdef __cplusplus
}
#endif

#endif /* XBOX_MENU_H */
