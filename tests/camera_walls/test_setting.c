/* Run with: python tests/camera_walls/run.py
 *
 * Default cam fix and No skater fade without the game:
 * port/camera_walls.c as it is, built with the in-game menu, reading and
 * keeping dcamfix= and noskaterfade= through the engine's own settings code
 * (taken from src/nv2a_gl/nv2a_gl.c by run.py). Their ticks are the first
 * two of Input > Behaviour > Camera, port/camera_scenes.c's row
 * (tests/camera_scenes), which turns them over with camwall_flip and
 * camwall_whole_flip; the menu's xbox_MenuAdd is stood in for, to see that
 * this file adds no row of its own. Each case is its own run, as the
 * settings are read once; run.py writes the settings file and checks the
 * log and the file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "camera_walls.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

/* The rows it adds: none, as the Camera row holds its ticks. */
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
    camwall_start();
    /* No row of its own: Camera's first two ticks are these. */
    CHECK(s_rows == 0);
    /* Default cam fix: off with no dcamfix=, since b2.2 (spriteice, 23
     * November 2026: "with default cam switch off"). */
    if (!strcmp(c, "on")) CHECK(camwall_on());
    if (!strcmp(c, "default") || !strcmp(c, "off")) CHECK(!camwall_on());
    /* No skater fade: off with no noskaterfade= (spriteice, 19 November
     * 2026: "No skater fade should be off by default"). */
    CHECK(CAMWALL_WHOLE_DEFAULT == 0);
    /* The hold each setting gives: with no settings, No skater fade off, so
     * neither eye; on, both eyes, or with Default cam fix off, the free
     * camera's alone. */
    if (!strcmp(c, "on")) CHECK(!camwall_whole() && camwall_on() && holds(0, 0));
    if (!strcmp(c, "default") || !strcmp(c, "off") || !strcmp(c, "whole_off"))
        CHECK(!camwall_whole() && !camwall_on() && holds(0, 0));
    if (!strcmp(c, "whole_on")) CHECK(camwall_whole() && camwall_on() && holds(1, 1));
    if (!strcmp(c, "flip")) {
        /* No skater fade on, then Default cam fix's tick: off at once (and
         * kept, which run.py checks), the game's eye no longer held, No
         * skater fade left as it was. */
        CHECK(camwall_on() && camwall_whole() && holds(1, 1));
        camwall_flip();
        CHECK(!camwall_on() && camwall_whole() && holds(1, 0));
    }
    if (!strcmp(c, "whole_flip")) {
        /* With Default cam fix off: off, as it starts, then the tick: on at
         * once, Default cam fix left as it was; then off, and on again
         * (kept, which run.py checks). */
        CHECK(!camwall_whole() && !camwall_on() && holds(0, 0));
        camwall_whole_flip();
        CHECK(camwall_whole() && !camwall_on() && holds(1, 0));
        camwall_whole_flip();
        CHECK(!camwall_whole() && !camwall_on() && holds(0, 0));
        camwall_whole_flip();
        CHECK(camwall_whole() && !camwall_on() && holds(1, 0));
    }
    if (!strcmp(c, "whole_game")) {
        /* With Default cam fix on: the tick on holds the game's eye as well
         * as the free camera's, and off again lets both come in. */
        CHECK(!camwall_whole() && camwall_on() && holds(0, 0));
        camwall_whole_flip();
        CHECK(camwall_whole() && camwall_on() && holds(1, 1));
        camwall_whole_flip();
        CHECK(!camwall_whole() && camwall_on() && holds(0, 0));
    }
    printf("camera_walls setting: %s\n", c);
    return 0;
}
