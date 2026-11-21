/* Run with: python tests/camera_walls/run.py
 *
 * Default cam fix and No skater fade without the game:
 * port/camera_walls.c as it is, built with the in-game menu, reading and
 * keeping dcamfix= and noskaterfade= through the engine's own settings code
 * (taken from src/nv2a_gl/nv2a_gl.c by run.py). The menu's xbox_MenuAdd is
 * stood in for, to catch the rows and their File > Presets values. Each
 * case is its own run, as the settings are read once; run.py writes the
 * settings file and checks the log and the file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "camera_walls.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

/* The rows it adds: Input's Behaviour submenu, then its two rows in it. */
#define ROWS_MOST 8
static XBOX_MENU_ITEM s_added[ROWS_MOST];
static int s_rows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    while (count-- > 0) {
        if (s_rows < ROWS_MOST) s_added[s_rows] = *items;
        s_rows++;
        items++;
    }
}

/* A row's hint: a sentence short enough for the strip (menu.h). */
static int hint_ok(const XBOX_MENU_ITEM *r)
{
    return r->hint && strlen(r->hint) <= MENU_HINT_LONGEST && r->hint[strlen(r->hint) - 1] == '.';
}

/* The game's whole-draw distance, CAM_SKATER_WHOLE's 15.0, as the follow
 * camera's hook gives it to camwall_least. */
#define WHOLE 15.0

/* No skater fade's hold: camwall_least gives WHOLE, or 0, for the free
 * camera's eye and for the game's own, as the two settings say (spriteice,
 * 19 November 2026: the free camera's with the row on; the game's with it
 * on and Default cam fix on; neither with it off, as it starts). */
static int holds(int free_held, int game_held)
{
    return camwall_least(1, WHOLE) == (free_held ? WHOLE : 0)
        && camwall_least(0, WHOLE) == (game_held ? WHOLE : 0);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    XBOX_MENU_ITEM group, row, whole;
    camwall_start();
    /* Input's Behaviour submenu, with its hint, then the row in it: a
     * toggle showing the setting, and taking effect at once. Then No skater
     * fade's row right under it, the same way. */
    CHECK(s_rows == 3);
    group = s_added[0];
    row = s_added[1];
    whole = s_added[2];
    CHECK(group.menu == XBOX_MENU_INPUT && group.kind == XBOX_ITEM_GROUP);
    CHECK(!strcmp(group.label, XBOX_GROUP_BEHAVIOUR) && !strcmp(group.label, "Behaviour"));
    CHECK(group.hint && !strcmp(group.hint, "Sets how the camera, the keys and the sticks act."));
    CHECK(row.menu == XBOX_MENU_INPUT && row.kind == XBOX_ITEM_TOGGLE && !row.last);
    CHECK(!strcmp(row.label, "Default cam fix") && !row.confirm);
    CHECK(row.group && !strcmp(row.group, XBOX_GROUP_BEHAVIOUR) && !row.under);
    CHECK(hint_ok(&row));
    CHECK(row.state() == camwall_on());
    /* File > Presets: off in Xbox mode, and JSRF-PC default's and Max
     * quality's what a start with no dcamfix= gives. */
    CHECK(row.presets == XBOX_PRESETS_SET && !row.keep && row.preset[XBOX_PRESET_XBOX] == 0);
    CHECK(row.preset[XBOX_PRESET_MAX] == row.preset[XBOX_PRESET_JSRF_PC]);
    if (!strcmp(c, "default")) CHECK(row.preset[XBOX_PRESET_JSRF_PC] == camwall_on());
    if (!strcmp(c, "default") || !strcmp(c, "on")) CHECK(camwall_on());
    if (!strcmp(c, "off")) CHECK(!camwall_on());
    /* No skater fade: Input > Behaviour, the row after Default cam fix's,
     * a toggle showing the setting (spriteice, 19 November 2026: "under
     * default cam fix"). */
    CHECK(whole.menu == XBOX_MENU_INPUT && whole.kind == XBOX_ITEM_TOGGLE && !whole.last);
    CHECK(!strcmp(whole.label, "No skater fade") && !whole.confirm);
    CHECK(whole.group && !strcmp(whole.group, XBOX_GROUP_BEHAVIOUR) && !whole.under);
    CHECK(hint_ok(&whole));
    /* File > Presets: off in each of the three (spriteice, 19 November
     * 2026: "No skater fade should be off by default", "In all presets"),
     * JSRF-PC default's and Max quality's what a start with no
     * noskaterfade= gives. */
    CHECK(whole.presets == XBOX_PRESETS_SET && !whole.keep && whole.preset[XBOX_PRESET_XBOX] == 0);
    CHECK(whole.preset[XBOX_PRESET_JSRF_PC] == 0 && whole.preset[XBOX_PRESET_MAX] == 0);
    CHECK(whole.preset[XBOX_PRESET_JSRF_PC] == CAMWALL_WHOLE_DEFAULT && CAMWALL_WHOLE_DEFAULT == 0);
    /* The hold each setting gives: with no settings, No skater fade off, so
     * neither eye; on, both eyes, or with Default cam fix off, the free
     * camera's alone. */
    if (!strcmp(c, "default") || !strcmp(c, "on") || !strcmp(c, "whole_off"))
        CHECK(!whole.state() && camwall_on() && holds(0, 0));
    if (!strcmp(c, "off")) CHECK(!whole.state() && !camwall_on() && holds(0, 0));
    if (!strcmp(c, "whole_on")) CHECK(whole.state() && camwall_on() && holds(1, 1));
    if (!strcmp(c, "flip")) {
        /* No skater fade on, then Default cam fix's row: off at once (and
         * kept, which run.py checks), the game's eye no longer held, No
         * skater fade left as it was. */
        CHECK(camwall_on() && whole.state() && holds(1, 1));
        row.run();
        CHECK(!camwall_on() && !row.state() && whole.state() && holds(1, 0));
    }
    if (!strcmp(c, "whole_flip")) {
        /* With Default cam fix off: off, as it starts, then the row: on at
         * once, Default cam fix left as it was; then off, and on again
         * (kept, which run.py checks). */
        CHECK(!whole.state() && !camwall_on() && holds(0, 0));
        whole.run();
        CHECK(whole.state() && !camwall_on() && holds(1, 0));
        whole.run();
        CHECK(!whole.state() && !camwall_on() && holds(0, 0));
        whole.run();
        CHECK(whole.state() && !camwall_on() && holds(1, 0));
    }
    if (!strcmp(c, "whole_game")) {
        /* With Default cam fix on: the row on holds the game's eye as well
         * as the free camera's, and off again lets both come in. */
        CHECK(!whole.state() && camwall_on() && holds(0, 0));
        whole.run();
        CHECK(whole.state() && camwall_on() && holds(1, 1));
        whole.run();
        CHECK(!whole.state() && camwall_on() && holds(0, 0));
    }
    printf("camera_walls setting: %s\n", c);
    return 0;
}
