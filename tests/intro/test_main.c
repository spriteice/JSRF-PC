/* Run with: python tests/intro/run.py
 *
 * The intro's setting without the game: port/intro.c as it is, built with the
 * in-game menu, reading and keeping skipintro= through the engine's own
 * settings code (taken from src/nv2a_gl/nv2a_gl.c by run.py). The menu's
 * xbox_MenuAdd is stood in for, to catch the row. Each case is its own run, as
 * the setting is read once; run.py writes the settings file and checks the log
 * and the file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "intro.h"

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
    intro_start();
    /* One row: a toggle at the bottom of the Display page, showing the
     * setting. */
    CHECK(s_rows == 1);
    CHECK(s_row.menu == XBOX_MENU_DISPLAY && s_row.kind == XBOX_ITEM_TOGGLE && s_row.last);
    CHECK(!strcmp(s_row.label, "Skip intro") && !s_row.confirm);
    CHECK(s_row.state() == intro_skip());
    if (!strcmp(c, "off")) CHECK(!intro_skip());
    if (!strcmp(c, "on")) CHECK(intro_skip());
    if (!strcmp(c, "flip")) {
        /* Off, then the row: on at once (and kept, which run.py checks). */
        CHECK(!intro_skip());
        s_row.run();
        CHECK(intro_skip() && s_row.state());
    }
    printf("intro: %s\n", c);
    return 0;
}
