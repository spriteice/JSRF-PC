/* Run with: python tests/user_files/run.py
 *
 * Where the launchers keep the player's files (launcher/common/user_files.c),
 * linked as the launchers and the import tool link it.
 *
 *   test.exe layouts         the layout from what was looked at, for both
 *                            systems, and this system's game_dir both ways:
 *                            "ok" lines, a "FAIL" line for each miss
 *   test.exe find <here>     js_files_find's answer, a key=value line each
 *   test.exe prepare <here> [<local app data>]
 *                            js_files_find, then js_files_prepare: its answer,
 *                            what it says on one line, and the layout after.
 *                            On Windows, the home layout's folder is the one
 *                            given, when one is, as %LOCALAPPDATA%'s can't be
 *                            moved for a test.
 *   test.exe saves <here> <game>
 *                            as prepare, then js_files_saves_in for the game
 *                            folder given
 *
 * run.py makes the folders these are run on, and checks what they say, what
 * they change, and that find changes nothing.
 */
#include <stdio.h>
#include <string.h>

#include "user_files.h"

static int failed;

static void same(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want)) {
        printf("FAIL %s: \"%s\", not \"%s\"\n", what, got, want);
        failed = 1;
    } else {
        printf("ok   %s: %s\n", what, got);
    }
}

/* beside: 1 a portable folder, 2 a data folder, 3 both. */
static void lay(int windows, const char *here, int beside, const char *config,
                const char *data, const char *state, const char *home, const char *la, js_files *f)
{
    js_files_places p;
    memset(&p, 0, sizeof p);
    p.windows = windows;
    p.here = here;
    p.portable = beside & 1;
    p.data = (beside & 2) != 0;
    p.config_home = config;
    p.data_home = data;
    p.state_home = state;
    p.home = home;
    p.local_app_data = la;
    memset(f, 0x5a, sizeof *f);      /* nothing left over from before */
    js_files_lay_out(&p, f);
}

static void expect(const char *what, const js_files *f, int layout, const char *root, const char *settings,
                   const char *saves, const char *cache, const char *games, const char *log, const char *logs)
{
    char k[256];
    if (f->layout != layout) {
        printf("FAIL %s: layout %d, not %d\n", what, f->layout, layout);
        failed = 1;
    }
    snprintf(k, sizeof k, "%s, root", what);     same(k, f->root, root);
    snprintf(k, sizeof k, "%s, settings", what); same(k, f->settings, settings);
    snprintf(k, sizeof k, "%s, saves", what);    same(k, f->saves, saves);
    snprintf(k, sizeof k, "%s, cache", what);    same(k, f->cache, cache);
    snprintf(k, sizeof k, "%s, games", what);    same(k, f->games, games);
    snprintf(k, sizeof k, "%s, log", what);      same(k, f->log, log);
    snprintf(k, sizeof k, "%s, logs", what);     same(k, f->logs, logs);
}

static void layouts(void)
{
    static js_files f;
    char out[JS_FILES_PATH];

    /* Linux: the data folder, the portable folder, both, and the home
     * folder's places with and without XDG's (an empty one counts as none),
     * and with no HOME. */
    lay(0, "/pkg", 2, "/c", "/d", "/s", "/h", NULL, &f);
    expect("linux data", &f, JS_FILES_DATA, "/pkg/data", "/pkg/data/settings.cfg", "/pkg/data/saves",
           "/pkg/data/cache", "/pkg/data/games", "/pkg/data/logs/last-run.log", "/pkg/data/logs");
    if (f.both) { printf("FAIL linux data: both\n"); failed = 1; }
    lay(0, "/pkg", 1, "/c", "/d", "/s", "/h", NULL, &f);
    expect("linux portable", &f, JS_FILES_PORTABLE, "/pkg/portable", "/pkg/portable/settings.cfg",
           "/pkg/portable/saves", "/pkg/portable/cache", "/pkg/portable/games",
           "/pkg/portable/logs/last-run.log", "/pkg/portable/logs");
    if (f.both) { printf("FAIL linux portable: both\n"); failed = 1; }
    lay(0, "/pkg", 3, "/c", "/d", "/s", "/h", NULL, &f);
    expect("linux both", &f, JS_FILES_DATA, "/pkg/data", "/pkg/data/settings.cfg", "/pkg/data/saves",
           "/pkg/data/cache", "/pkg/data/games", "/pkg/data/logs/last-run.log", "/pkg/data/logs");
    if (!f.both) { printf("FAIL linux both: not both\n"); failed = 1; }
    else printf("ok   linux both: both\n");
    lay(0, "/pkg", 0, "/c", "/d", "/s", "/h", NULL, &f);
    if (f.both) { printf("FAIL linux home: both\n"); failed = 1; }
    lay(0, "/pkg", 0, NULL, NULL, NULL, "/h", NULL, &f);
    expect("linux home", &f, JS_FILES_HOME, "", "/h/.config/jsrf-pc/settings", "",
           "/h/.local/share/jsrf-pc/save", "/h/.local/share/jsrf-pc",
           "/h/.local/state/jsrf-pc/last-run.log", "/h/.local/state/jsrf-pc/logs");
    lay(0, "/pkg", 0, "/c", "/d", "/s", "/h", NULL, &f);
    expect("linux home, XDG's", &f, JS_FILES_HOME, "", "/c/jsrf-pc/settings", "", "/d/jsrf-pc/save",
           "/d/jsrf-pc", "/s/jsrf-pc/last-run.log", "/s/jsrf-pc/logs");
    lay(0, "/pkg", 0, "", "", "", "/h", NULL, &f);
    expect("linux home, XDG's empty", &f, JS_FILES_HOME, "", "/h/.config/jsrf-pc/settings", "",
           "/h/.local/share/jsrf-pc/save", "/h/.local/share/jsrf-pc",
           "/h/.local/state/jsrf-pc/last-run.log", "/h/.local/state/jsrf-pc/logs");
    lay(0, "/pkg", 0, NULL, NULL, NULL, NULL, NULL, &f);
    expect("linux home, no HOME", &f, JS_FILES_HOME, "", "/.config/jsrf-pc/settings", "",
           "/.local/share/jsrf-pc/save", "/.local/share/jsrf-pc", "/.local/state/jsrf-pc/last-run.log",
           "/.local/state/jsrf-pc/logs");

    /* Windows: the data folder and the portable folder (beside a launcher at
     * a drive's root too), and %LOCALAPPDATA%'s, or the launcher's folder's
     * when there is none. */
    lay(1, "C:\\Games\\JSRF-PC", 2, NULL, NULL, NULL, NULL, "C:\\Users\\a\\AppData\\Local", &f);
    expect("windows data", &f, JS_FILES_DATA, "C:\\Games\\JSRF-PC\\data",
           "C:\\Games\\JSRF-PC\\data\\settings.cfg", "C:\\Games\\JSRF-PC\\data\\saves",
           "C:\\Games\\JSRF-PC\\data\\cache", "C:\\Games\\JSRF-PC\\data\\games",
           "C:\\Games\\JSRF-PC\\data\\logs\\last-run.log", "C:\\Games\\JSRF-PC\\data\\logs");
    lay(1, "E:\\", 2, NULL, NULL, NULL, NULL, NULL, &f);
    same("windows data, at a drive's root, with its \\", f.root, "E:\\data");
    lay(1, "C:\\Games\\JSRF-PC", 3, NULL, NULL, NULL, NULL, NULL, &f);
    same("windows both", f.root, "C:\\Games\\JSRF-PC\\data");
    if (!f.both) { printf("FAIL windows both: not both\n"); failed = 1; }
    lay(1, "C:\\Games\\JSRF-PC", 1, NULL, NULL, NULL, NULL, "C:\\Users\\a\\AppData\\Local", &f);
    expect("windows portable", &f, JS_FILES_PORTABLE, "C:\\Games\\JSRF-PC\\portable",
           "C:\\Games\\JSRF-PC\\portable\\settings.cfg", "C:\\Games\\JSRF-PC\\portable\\saves",
           "C:\\Games\\JSRF-PC\\portable\\cache", "C:\\Games\\JSRF-PC\\portable\\games",
           "C:\\Games\\JSRF-PC\\portable\\logs\\last-run.log", "C:\\Games\\JSRF-PC\\portable\\logs");
    lay(1, "E:", 1, NULL, NULL, NULL, NULL, NULL, &f);
    same("windows portable, at a drive's root", f.root, "E:\\portable");
    lay(1, "E:\\", 1, NULL, NULL, NULL, NULL, NULL, &f);
    same("windows portable, at a drive's root, with its \\", f.root, "E:\\portable");
    lay(1, "C:\\Games\\JSRF-PC", 0, NULL, NULL, NULL, NULL, "C:\\Users\\a\\AppData\\Local", &f);
    expect("windows home", &f, JS_FILES_HOME, "", "C:\\Users\\a\\AppData\\Local\\JSRF-PC\\settings", "",
           "C:\\Users\\a\\AppData\\Local\\JSRF-PC\\save", "C:\\Users\\a\\AppData\\Local\\JSRF-PC",
           "C:\\Users\\a\\AppData\\Local\\JSRF-PC\\last-run.log", "C:\\Users\\a\\AppData\\Local\\JSRF-PC\\logs");
    lay(1, "C:\\Games\\JSRF-PC", 0, NULL, NULL, NULL, NULL, NULL, &f);
    same("windows home, no %LOCALAPPDATA%", f.settings, "C:\\Games\\JSRF-PC\\JSRF-PC\\settings");

    /* This system's game_dir: relative to the launcher's folder beside it
     * (the data and the portable layouts) only, and kept so when inside it. */
#ifdef _WIN32
    lay(1, "C:\\Games\\JSRF-PC", 2, NULL, NULL, NULL, NULL, NULL, &f);
    js_files_game(&f, "data\\games\\JSRF", out, sizeof out);
    same("game, data layout", out, "C:\\Games\\JSRF-PC\\data\\games\\JSRF");
    js_files_keep(&f, "C:\\Games\\JSRF-PC\\data\\games\\JSRF", out, sizeof out);
    same("keep, data layout", out, "data\\games\\JSRF");
    lay(1, "C:\\Games\\JSRF-PC", 1, NULL, NULL, NULL, NULL, NULL, &f);
    js_files_game(&f, "games\\JSRF", out, sizeof out);
    same("game, relative", out, "C:\\Games\\JSRF-PC\\games\\JSRF");
    js_files_game(&f, "D:\\JSRF", out, sizeof out);
    same("game, another drive", out, "D:\\JSRF");
    js_files_game(&f, "\\\\pc\\share\\JSRF", out, sizeof out);
    same("game, a share", out, "\\\\pc\\share\\JSRF");
    js_files_game(&f, "", out, sizeof out);
    same("game, none", out, "");
    js_files_keep(&f, "c:\\GAMES\\jsrf-pc\\games\\JSRF", out, sizeof out);
    same("keep, inside (its case aside)", out, "games\\JSRF");
    js_files_keep(&f, "C:\\Games\\JSRF-PC2\\JSRF", out, sizeof out);
    same("keep, beside it", out, "C:\\Games\\JSRF-PC2\\JSRF");
    lay(1, "C:\\Games\\JSRF-PC", 0, NULL, NULL, NULL, NULL, "C:\\L", &f);
    js_files_game(&f, "games\\JSRF", out, sizeof out);
    same("game, home layout", out, "games\\JSRF");
    js_files_keep(&f, "C:\\Games\\JSRF-PC\\games\\JSRF", out, sizeof out);
    same("keep, home layout", out, "C:\\Games\\JSRF-PC\\games\\JSRF");
#else
    lay(0, "/pkg", 2, NULL, NULL, NULL, "/h", NULL, &f);
    js_files_game(&f, "data/games/JSRF", out, sizeof out);
    same("game, data layout", out, "/pkg/data/games/JSRF");
    js_files_keep(&f, "/pkg/data/games/JSRF", out, sizeof out);
    same("keep, data layout", out, "data/games/JSRF");
    lay(0, "/pkg", 1, NULL, NULL, NULL, "/h", NULL, &f);
    js_files_game(&f, "games/JSRF", out, sizeof out);
    same("game, relative", out, "/pkg/games/JSRF");
    js_files_game(&f, "/games/JSRF", out, sizeof out);
    same("game, whole", out, "/games/JSRF");
    js_files_game(&f, "", out, sizeof out);
    same("game, none", out, "");
    js_files_keep(&f, "/pkg/games/JSRF", out, sizeof out);
    same("keep, inside", out, "games/JSRF");
    js_files_keep(&f, "/pkg2/JSRF", out, sizeof out);
    same("keep, beside it", out, "/pkg2/JSRF");
    js_files_keep(&f, "/PKG/games/JSRF", out, sizeof out);
    same("keep, another case", out, "/PKG/games/JSRF");
    lay(0, "/pkg", 0, NULL, NULL, NULL, "/h", NULL, &f);
    js_files_game(&f, "games/JSRF", out, sizeof out);
    same("game, home layout", out, "games/JSRF");
    js_files_keep(&f, "/pkg/games/JSRF", out, sizeof out);
    same("keep, home layout", out, "/pkg/games/JSRF");
#endif
}

static void show(const js_files *f)
{
    printf("layout=%s\nfound=%d\nboth=%d\nhere=%s\nroot=%s\nsettings=%s\nsaves=%s\ncache=%s\ngames=%s\nlog=%s\n"
           "logs=%s\ngame_state=%s\ngame=%s\n",
           f->layout == JS_FILES_DATA ? "data" : f->layout == JS_FILES_PORTABLE ? "portable" : "home", f->found,
           f->both, f->here, f->root, f->settings, f->saves, f->cache, f->games, f->log, f->logs,
           f->game_state == JS_GAME_THERE ? "there" : f->game_state == JS_GAME_GONE ? "gone" : "unset", f->game);
}

int main(int argc, char **argv)
{
    static js_files f;
    if (argc == 2 && !strcmp(argv[1], "layouts")) {
        layouts();
        return failed;
    }
    if (argc == 3 && !strcmp(argv[1], "find")) {
        printf("rc=%d\n", js_files_find(argv[2], &f));
        show(&f);
        return 0;
    }
    if ((argc == 3 || argc == 4) && !strcmp(argv[1], "prepare")) {
        static char err[JS_FILES_PATH * 3];
        char *c;
        js_files_find(argv[2], &f);
#ifdef _WIN32
        if (argc == 4 && f.layout == JS_FILES_HOME) {
            js_files_places p;
            memset(&p, 0, sizeof p);
            p.windows = 1;
            p.here = argv[2];
            p.local_app_data = argv[3];
            js_files_lay_out(&p, &f);
        }
#endif
        printf("rc=%d\n", js_files_prepare(&f, err, sizeof err));
        for (c = err; *c; c++) if (*c == '\n') *c = '|';   /* one line */
        printf("err=%s\n", err);
        show(&f);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "saves")) {
        static char err[JS_FILES_PATH * 3];
        char *c;
        int rc;
        js_files_find(argv[2], &f);
        rc = js_files_prepare(&f, err, sizeof err);
        if (!rc) rc = js_files_saves_in(&f, argv[3], err, sizeof err);
        printf("rc=%d\n", rc);
        for (c = err; *c; c++) if (*c == '\n') *c = '|';
        printf("err=%s\n", err);
        return 0;
    }
    fprintf(stderr, "usage: test.exe layouts | find <here> | prepare <here> [<local app data>] | saves <here> <game>\n");
    return 2;
}
