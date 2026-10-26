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
 *           .label = "Widescreen (16:9)", .state = is_wide, .run = toggle_wide },
 *     };
 *     xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
 *     #endif
 *
 * Plain C; menu.cpp draws the rows with Dear ImGui. Labels, (Restart), boxes
 * and File > About follow docs/menu-format.md.
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
} XBOX_MENU_ITEM;

/* Adds rows to the end of their menus, copied: the table can be on the
 * caller's stack. Any thread, at any time, before the window opens or after;
 * they are drawn from the next frame. Rows added in one call stay together,
 * so a module adds its rows in one call. */
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count);

/* The window's side (menu_sdl.cpp, and tests/menu): one frame of the menu,
 * between ImGui::NewFrame() and ImGui::Render(), on the window's thread. It
 * stays until the window shuts it (F1, Back). just_opened: the first
 * frame after opening -- the bar alone, the pad on the first menu's title,
 * so a pad has somewhere to start. */
void menu_build(int just_opened);

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
