/*
 * Where the player's files are kept: the settings, the saves, the logs, the
 * engine's cache and the disc images unpacked. Both launchers' one way of
 * finding them (launcher/linux and launcher/windows), and the import tool's.
 *
 *   Portable: a folder named "portable" beside the launcher holds them all:
 *             settings.cfg, saves/ (UDATA and TDATA), logs/, cache/ and
 *             games/.
 *   Home:     otherwise. On Linux, ~/.config/jsrf-pc/settings, the cache in
 *             ~/.local/share/jsrf-pc/save, disc images unpacked into
 *             ~/.local/share/jsrf-pc, and the logs in ~/.local/state/jsrf-pc
 *             (each under its $XDG_*_HOME when that is set). On Windows, all
 *             of them in %LOCALAPPDATA%\JSRF-PC. The saves are the game
 *             folder's own UDATA and TDATA, where an Xbox keeps them.
 *
 * js_files_find() only looks: it makes, renames and writes nothing, so a
 * program of its own (the import tool) can call it while the launcher is
 * closed. js_files_prepare() is the launchers' alone.
 *
 * Paths are UTF-8, as launcher/setup's are.
 */
#ifndef JSRF_USER_FILES_H
#define JSRF_USER_FILES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JS_FILES_PATH 4096

/* The layout. */
#define JS_FILES_HOME     0   /* the home folder's places, or %LOCALAPPDATA%'s */
#define JS_FILES_PORTABLE 1   /* the portable folder beside the launcher */

/* The game the settings remember (game_dir). */
#define JS_GAME_UNSET 0       /* none */
#define JS_GAME_GONE  1       /* one, but no folder is there now */
#define JS_GAME_THERE 2       /* one, and its folder is there */

typedef struct js_files {
    int layout;                     /* JS_FILES_HOME or JS_FILES_PORTABLE */
    int found;                      /* 1 when it is there: the portable folder, or the home layout's settings file */
    char here[JS_FILES_PATH];       /* the folder looked beside: the launcher's */
    char root[JS_FILES_PATH];       /* the portable folder; "" in the home layout */
    char settings[JS_FILES_PATH];   /* the settings file (JSRF_SETTINGS_FILE) */
    char saves[JS_FILES_PATH];      /* the game's saves (JSRF_USER_DIR): portable/saves; "" in the home
                                     * layout, where they are in the game folder (game, below) */
    char cache[JS_FILES_PATH];      /* the engine's cache (JSRF_SAVE_DIR) */
    char games[JS_FILES_PATH];      /* where a disc image is unpacked, each into a folder of its name */
    char log[JS_FILES_PATH];        /* last-run.log */
    char logs[JS_FILES_PATH];       /* the logs before it */
    int game_state;                 /* JS_GAME_UNSET, JS_GAME_GONE or JS_GAME_THERE */
    char game[JS_FILES_PATH];       /* the game folder remembered, as a whole path; "" when unset */
} js_files;

/* What is beside `here` (the launcher's folder, or any other), and where
 * everything is: 1 when the layout is there (found), else 0, with the paths
 * the launcher would make. Reads, and never writes. */
int js_files_find(const char *here, js_files *f);

/* The launchers', before anything else is read or made: the folders made,
 * and a portable folder that can't be written refused rather than falling
 * back to the home folder, which would split the saves between two places.
 * 0, or -1 with what to tell the player in err. */
int js_files_prepare(const js_files *f, char *err, size_t n);

/* A game_dir value as a whole path: in the portable layout, one inside the
 * launcher's folder is kept relative to it, so the whole folder can move. */
void js_files_game(const js_files *f, const char *value, char *out, size_t n);

/* The game_dir value to keep for the game folder `game`: relative to the
 * launcher's folder when it is inside it, in the portable layout. */
void js_files_keep(const js_files *f, const char *game, char *out, size_t n);

/* ---- the layout from what was looked at: js_files_find's, and the tests' ---- */

typedef struct js_files_places {
    int windows;                    /* Windows's layout and its separator */
    const char *here;               /* the launcher's folder */
    int portable;                   /* a folder named portable is beside it */
    /* Linux: $XDG_CONFIG_HOME, $XDG_DATA_HOME, $XDG_STATE_HOME and $HOME,
     * NULL when one isn't set. */
    const char *config_home, *data_home, *state_home, *home;
    /* Windows: the local application data folder (its known folder, else
     * %LOCALAPPDATA%, else the launcher's folder). */
    const char *local_app_data;
} js_files_places;

void js_files_lay_out(const js_files_places *p, js_files *f);

#ifdef __cplusplus
}
#endif

#endif
