/* Run with: python tests/audio/run.py
 *
 * The Audio menu's setting without the game: port/audio.c as it is, built
 * with the in-game menu, reading and keeping audioeffects= through the
 * engine's own settings code (taken from src/nv2a_gl/nv2a_gl.c by run.py).
 * The menu's xbox_MenuAdd, the effects' apu_gpfx_choose and the window's
 * nv_window_restart are stood in for, to catch the row, the choice and the
 * restart, and the row's keep for File > Presets, which doesn't restart.
 * Each case is its own run, as the setting is read once; run.py writes the
 * settings file and checks the log and the file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "audio.h"
#include "apu_gpfx.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static XBOX_MENU_ITEM s_row;
static int s_rows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    if (count > 0) s_row = items[0];
    s_rows += count;
}

static int s_chosen = -1, s_choices;
void apu_gpfx_choose(int on)
{
    s_chosen = on;
    s_choices++;
}

static int s_restarts;
void nv_window_restart(void)
{
    s_restarts++;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    int on;
    audio_start();
    /* One row: a toggle on the Audio page, showing the setting, that asks
     * before it restarts the game. */
    CHECK(s_rows == 1);
    CHECK(s_row.menu == XBOX_MENU_AUDIO && s_row.kind == XBOX_ITEM_TOGGLE && !s_row.last);
    CHECK(!strcmp(s_row.label, "Audio effects (Restart)"));
    CHECK(s_row.confirm && !strcmp(s_row.confirm,
                                   "Apply selection and restart, losing unsaved progress?"));
    /* A hint, a sentence short enough for the strip (menu.h). */
    CHECK(s_row.hint && strlen(s_row.hint) <= MENU_HINT_LONGEST && s_row.hint[strlen(s_row.hint) - 1] == '.');
    CHECK(s_row.state() == audio_effects());
    /* File > Presets: on in each, and kept for the restart the preset gives,
     * not restarted for by the row. JSRF-PC default's is what a start with
     * no audioeffects= gives. */
    CHECK(s_row.presets == XBOX_PRESETS_SET && s_row.keep);
    CHECK(s_row.preset[XBOX_PRESET_XBOX] == 1 && s_row.preset[XBOX_PRESET_JSRF_PC] == 1 &&
          s_row.preset[XBOX_PRESET_MAX] == 1);
    CHECK(strcmp(c, "default") || s_row.preset[XBOX_PRESET_JSRF_PC] == audio_effects());
    /* The setting, passed to the effects once, which leave the environment
     * to win over it. */
    CHECK(s_choices == 1 && s_chosen == audio_effects());
    on = !strcmp(c, "default") || !strcmp(c, "on") || !strcmp(c, "env_off")
         || !strcmp(c, "flip_off") || !strcmp(c, "keep_off");
    CHECK(audio_effects() == on);
    if (!strncmp(c, "flip_", 5)) {
        /* The row: the other way, kept (which run.py checks), and the game
         * restarted for it; the effects keep what this start chose. */
        s_row.run();
        CHECK(audio_effects() == !on && s_row.state() == !on);
        CHECK(s_restarts == 1 && s_choices == 1);
    }
    if (!strcmp(c, "keep_off")) {
        /* A preset: kept, which run.py checks, with no restart of its own. */
        s_row.keep(0);
        CHECK(audio_effects() == 0 && s_row.state() == 0 && s_choices == 1);
    }
    CHECK(!strncmp(c, "flip_", 5) || s_restarts == 0);
    printf("audio: %s\n", c);
    return 0;
}
