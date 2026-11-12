/* Run with: python tests/camera_walls/run.py
 *
 * Default cam fix without the game: port/camera_walls.c as it is, built with
 * the in-game menu, reading and keeping dcamfix= through the engine's own
 * settings code (taken from src/nv2a_gl/nv2a_gl.c by run.py). The menu's
 * xbox_MenuAdd is stood in for, to catch the row and its File > Presets
 * values. Each case is its own run, as the setting is read once; run.py
 * writes the settings file and checks the log and the file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "camera_walls.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static XBOX_MENU_ITEM s_row;
static int s_rows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    if (count > 0) s_row = items[0];
    s_rows += count;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    camwall_start();
    /* One row: a toggle on the Controller page, right below Free camera,
     * showing the setting, and taking effect at once. */
    CHECK(s_rows == 1);
    CHECK(s_row.menu == XBOX_MENU_CONTROLLER && s_row.kind == XBOX_ITEM_TOGGLE && !s_row.last);
    CHECK(!strcmp(s_row.label, "Default cam fix") && !s_row.confirm);
    CHECK(s_row.under && !strcmp(s_row.under, "Free camera"));
    /* A hint, a sentence short enough for the strip (menu.h). */
    CHECK(s_row.hint && strlen(s_row.hint) <= MENU_HINT_LONGEST && s_row.hint[strlen(s_row.hint) - 1] == '.');
    CHECK(s_row.state() == camwall_on());
    /* File > Presets: off in Xbox mode, and JSRF-PC default's and Max
     * quality's what a start with no dcamfix= gives. */
    CHECK(s_row.presets == XBOX_PRESETS_SET && !s_row.keep && s_row.preset[XBOX_PRESET_XBOX] == 0);
    CHECK(s_row.preset[XBOX_PRESET_MAX] == s_row.preset[XBOX_PRESET_JSRF_PC]);
    if (!strcmp(c, "default")) CHECK(s_row.preset[XBOX_PRESET_JSRF_PC] == camwall_on());
    if (!strcmp(c, "default") || !strcmp(c, "on")) CHECK(camwall_on());
    if (!strcmp(c, "off")) CHECK(!camwall_on());
    if (!strcmp(c, "flip")) {
        /* On, then the row: off at once (and kept, which run.py checks). */
        CHECK(camwall_on());
        s_row.run();
        CHECK(!camwall_on() && !s_row.state());
    }
    printf("camera_walls setting: %s\n", c);
    return 0;
}
