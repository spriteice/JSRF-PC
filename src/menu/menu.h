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
 *           .presets = XBOX_PRESETS_SET, .preset = { 1, 1, 1 },
 *           .hint = "The window becomes fullscreen." },
 *     };
 *     xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
 *     #endif
 *
 * Plain C; menu.cpp draws the rows with Dear ImGui. Labels, hints,
 * (Restart), boxes, bind rows, File > About and File > Presets follow
 * docs/menu-format.md.
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
    XBOX_MENU_INPUT,
    XBOX_MENU_GAMEPLAY,
    XBOX_MENU_DEVELOPER,
    XBOX_MENU_COUNT
} XBOX_MENU_ID;

typedef enum {
    XBOX_ITEM_ACTION,       /* run() when chosen                              */
    XBOX_ITEM_TOGGLE,       /* ticked while state() is nonzero; run() flips it */
    XBOX_ITEM_CHOICE,       /* a submenu of choices[]; the ticked one is
                             * state(), pick(i) when another is chosen (or
                             * pick_with: below)                              */
    XBOX_ITEM_INFO,         /* text() in a box, with OK                       */
    XBOX_ITEM_SEPARATOR,
    XBOX_ITEM_NUMBER,       /* value() in a box, with - and + either side: a
                             * step down or up, from min to max; set(v)       */
    XBOX_ITEM_TOGGLES,      /* a submenu of choices[], each ticked while its
                             * bit in state() is set; pick(i) flips it        */
    XBOX_ITEM_GROUP,        /* a submenu of the rows of its menu whose group
                             * is its label (below, group)                    */
    XBOX_ITEM_BIND          /* a control, with text() beside it: the input that
                             * presses it. Chosen, a box waits for the next
                             * press it takes, and hands it to bind() (below,
                             * takes)                                         */
} XBOX_ITEM_KIND;

/* File > Presets' presets, in its order (docs/menu-format.md). */
typedef enum {
    XBOX_PRESET_XBOX,       /* Xbox mode: the game as the console runs it      */
    XBOX_PRESET_JSRF_PC,    /* JSRF-PC default: the engine's own defaults       */
    XBOX_PRESET_MAX,        /* Max quality: JSRF-PC default, with the graphics
                             * and display settings at their most             */
    XBOX_PRESETS
} XBOX_PRESET;

/* What the presets do with a row (XBOX_MENU_ITEM's presets). A preset sets
 * every setting but Developer's, and those its row says it leaves alone:
 * each other row of Display, Graphics, Audio, Input and Gameplay gives its
 * value in each preset, and the log says so once for one that says
 * nothing, which no preset then sets. */
typedef enum {
    XBOX_PRESETS_UNSAID,    /* nothing said: File's and Developer's rows, which
                             * no preset sets                                 */
    XBOX_PRESETS_SET,       /* each preset sets it, to its value in preset[]   */
    XBOX_PRESETS_LEFT       /* no preset sets it: a player's own, not a quality
                             * setting, with a way of its own back to its
                             * default (docs/menu-format.md, File > Presets)  */
} XBOX_PRESETS_USE;

/* What a bind row (XBOX_ITEM_BIND) takes, and the number bind() is handed
 * for each input. A pad's: a button, as SDL numbers them
 * (SDL_GameControllerButton), or a trigger pressed past half way, the left
 * SDL_CONTROLLER_BUTTON_MAX and the right one past it. The keyboard's and
 * the mouse's: a key, by its place (SDL_Scancode), or a mouse button,
 * SDL_NUM_SCANCODES and on, from SDL's first button. menu_sdl.cpp finds
 * them in SDL's events; the owner names them. */
typedef enum {
    XBOX_BIND_NONE,         /* not a bind row                                 */
    XBOX_BIND_PAD,          /* any pad's buttons and triggers                 */
    XBOX_BIND_KEYS          /* the keyboard's keys and the mouse's buttons    */
} XBOX_BIND_TAKES;

/* A preset's value for a row that has no fixed most, as Max quality's
 * anisotropic filtering: past a choice's last, a number's max, or a row of
 * ticks' every tick, it is the last, the max or every tick -- the most the
 * device has, which can stop short of the names a row could show. */
#define XBOX_PRESET_MOST 1e9

/* state, run, pick, text, value, set, keep and bind are called on the
 * window's thread while the game runs on: keep them to what the owner's
 * hotkey would do. */
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
     * Display's submenu (XBOX_GROUP_ENHANCEMENTS, below), below the rows
     * added as the game starts, under the window's. */
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
    /* File > Presets (docs/menu-format.md): whether the presets set the
     * row, and its value in each, in XBOX_PRESET's order -- a TOGGLE's 0 or
     * 1, a CHOICE's index, a NUMBER's value, a TOGGLES' ticks as state()
     * gives them (bit i for choice i, at most MENU_PRESET_TICKS of them),
     * each within the row's own (XBOX_PRESET_MOST). The owner gives them
     * beside the row, from the same constants its setting uses, so JSRF-PC
     * default's are the defaults. */
    XBOX_PRESETS_USE presets;
    double          preset[XBOX_PRESETS];
    /* A (Restart) row the presets set, a TOGGLE or a CHOICE, whose run or
     * pick restarts the game: keeps v (a toggle's 0 or 1, a choice's index)
     * for the next start, as they would, without restarting. File > Presets
     * restarts once, after every row is set. */
    void          (*keep)(int v);
    /* Set: drawn in the submenu of its menu with this label (an
     * XBOX_ITEM_GROUP row), whenever either was added, in the order the
     * submenu's rows were added; its last and under are not used. With no
     * such submenu, it is drawn below the menu's other rows (above any
     * `last` row), and the log says so once. A submenu nobody has put rows
     * in is not shown; one with the label of one before it in its menu adds
     * nothing; and one can't go in another. */
    const char     *group;
    /* Set: called in place of pick, with data, for rows that share one
     * function and tell it which row they are -- Developer > Missions'
     * levels, each a CHOICE of its missions (port/missions_menu.c). data is
     * the owner's, kept as it is: the menu never reads it. */
    void          (*pick_with)(const void *data, int index);
    const void     *data;
    /* XBOX_ITEM_BIND: what it takes, and bind(), handed the input pressed
     * (XBOX_BIND_TAKES): nonzero when the row has it now, 0 when it can't,
     * and the box waits on. text() names the input it has. The pad's Back
     * and F1 cancel the box, so they can't be bound and no layout can shut
     * the menu out. */
    XBOX_BIND_TAKES takes;
    int           (*bind)(int input);
} XBOX_MENU_ITEM;

/* Display's submenu, at its foot (nv2a_gl.c adds it): Enhancements, for
 * what goes beyond the console. */
#define XBOX_GROUP_ENHANCEMENTS "Enhancements"

/* Gameplay's submenus, in its order (nv2a_gl.c adds them, so they keep it
 * whichever module adds its rows first): Skips, for the parts of the game
 * a player can leave out, then Graffiti souls, for help with the graffiti
 * soul challenges. */
#define XBOX_GROUP_SKIPS        "Skips"
#define XBOX_GROUP_SOULS        "Graffiti souls"

/* Graphics' submenu, at its foot (nv2a_gl.c adds it): Upscaling, for the
 * rows that draw the game from sharper copies -- Menus and HUD, Textures
 * and Graffiti (2D, 3D and graffiti upscaling), then Sharpness, Edge
 * strength and Cleanup. */
#define XBOX_GROUP_UPSCALING    "Upscaling"

/* Input's submenus, in its order: Controller layout, then Keyboard layout,
 * which press each of the game's controls (src/input/input_remap.c adds
 * them, with their rows); Free camera (port/freecam.c adds it, with its
 * rows); then Behaviour, for how the game's camera and the keys act
 * (Default cam fix, port/camera_walls.c, then Keyboard moves,
 * port/keyboard_move.c: each adds it with its row, and the second adds
 * nothing). */
#define XBOX_GROUP_PAD_LAYOUT   "Controller layout"
#define XBOX_GROUP_KEY_LAYOUT   "Keyboard layout"
#define XBOX_GROUP_FREECAM      "Free camera"
#define XBOX_GROUP_BEHAVIOUR    "Behaviour"
#define XBOX_GROUP_BEHAVIOUR_HINT "Configure the keyboard movement in relation to the camera."

/* Developer's submenu right below Performance (port/developer.c adds it):
 * Missions, the game restarted at a story mission (port/missions_menu.c
 * puts its rows in it). */
#define XBOX_GROUP_MISSIONS     "Missions"

/* And right below Missions (port/developer.c adds it too): Playback, the
 * player's presses recorded and played back (port/playback.c puts its rows
 * in it). */
#define XBOX_GROUP_PLAYBACK     "Playback"

/* Two lines at 640 x 480, beside the pad's buttons. */
#define MENU_HINT_LONGEST 70

/* The most ticks a row of them (XBOX_ITEM_TOGGLES) can have for File >
 * Presets to set it: a bit of state()'s int each. */
#define MENU_PRESET_TICKS 31

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

/* File > Presets (menu_sdl.cpp, and tests/menu), on the window's thread:
 * the preset whose value every row the presets set has now, or -1 for none;
 * and preset p, each row put at its value through its own pick, run or set,
 * or keep for a (Restart) row, saying so in the log. menu_preset_apply is
 * nonzero when a (Restart) row was kept: the game starts again for it. */
int menu_preset_now(void);
int menu_preset_apply(int p);

/* A row by its labels (input playback's steps, and tests/menu), on the
 * window's thread, chosen as a click or the pad chooses it. A path is the
 * menu's title, then the labels down to the row (a submenu's, then its
 * row's: menu.h, group), joined by " > ", then what is chosen in it:
 *
 *   "Graphics > Anti-aliasing (SMAA)"       a toggle, flipped; with " > On"
 *                                           or " > Off", put so
 *   "Graphics > Anti-aliasing (MSAA) > 4x"  a choice, by its name
 *   "Graphics > Cleanup > <a tick's name>"  a tick, flipped; with " > On" or
 *                                           " > Off", put so
 *   "Display > HUD > HUD scale > 1.5"       a number, within its min and max
 *   an ACTION row's path                    run
 *
 * menu_path_run does it and menu_path_check only says whether it would:
 * MENU_PATH_OK, or why not (menu_path_why, for the log). Each takes the rows
 * added since the last frame first. A row that asks first (a restart, File >
 * Exit) is refused: what it sets is set before a run starts, not during it. */
typedef enum {
    MENU_PATH_OK,
    MENU_PATH_NO_MENU,      /* no menu has that title                        */
    MENU_PATH_NO_ROW,       /* nothing there has that label                  */
    MENU_PATH_NO_CHOICE,    /* the row has no such choice, or it isn't On or
                             * Off                                           */
    MENU_PATH_BAD_NUMBER,   /* not a number, or past the row's min or max    */
    MENU_PATH_ASKS,         /* the row asks first                            */
    MENU_PATH_NOT_RUN,      /* nothing to choose: a submenu with nothing after
                             * it, a box of text, a bind row (its box waits
                             * for a press), or a row that needs a choice or
                             * a number after it                             */
    MENU_PATH_TOO_LONG      /* more after what is chosen                     */
} MENU_PATH;
int menu_path_check(const char *path);
int menu_path_run(const char *path);
const char *menu_path_why(int result);

/* The hint the strip along the bottom showed in the last menu_build: the
 * row the pad is on, else the one the pointer is on; NULL on the bar, or in
 * a box. Read by tests/menu. */
const char *menu_hint(void);

/* A bind row's box (XBOX_ITEM_BIND), for the window's events (menu_sdl.cpp,
 * and tests/menu), on the window's thread, before the next menu_build:
 * what the row waiting takes, or 0 when none is; the input pressed, handed
 * to its bind(); and cancel, for Back or F1. Either ends the wait, and the
 * box shuts at the next menu_build, back on the row. From then until no
 * pad button or stick is held, Dear ImGui's pad navigation is held off,
 * and so is B as the menu's Back: the press that binds, or cancels, acts
 * in the menu no further. */
int  menu_bind_waiting(void);
void menu_bind_press(int input);
void menu_bind_cancel(void);

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
