/* Run with: python tests/developer/run.py
 *
 * port/developer.c without the game or the menu, reading the engine's own
 * settings code (taken from src/nv2a_gl/nv2a_gl.c by run.py). run.py writes
 * the settings file and developer.cfg beside it, and checks the log this
 * writes: what developer_start put in the environment, that nothing reached
 * the log while Log to file was off, and that developer_restart takes out
 * what it put in and gives the log back.
 *
 * Built with the in-game menu too, where xbox_MenuAdd is stood in for: each
 * page of switches is a line in the log, its label then its rows in order,
 * and a line after it with its ticks, a bit for each row.
 */
#include <stdio.h>
#include <stdlib.h>
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

void developer_start(void);
void developer_restart(void);

#if defined(XBOXRECOMP_MENU)
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    int i, k;
    for (i = 0; i < count; i++) {
        if (items[i].kind != XBOX_ITEM_TOGGLES) continue;
        fprintf(stderr, "[MENU] %s:", items[i].label);
        for (k = 0; k < items[i].nchoices; k++)
            fprintf(stderr, "%s%s", k ? " | " : " ", items[i].choices[k]);
        fprintf(stderr, "\n[TICKS] %s: %d\n", items[i].label, items[i].state());
    }
}
#endif

/* Two of Logging's switches, and three of Engine switches': one that is set
 * to 1, one that is set to 0, and RECOMP_GL_CURTAIN, set to a word, skip. A
 * line each, "-" when it is not set. */
static void show(const char *when)
{
    static const char *const name[] = { "RECOMP_GL_PROFILE", "RECOMP_LOCK_WATCH",
                                        "RECOMP_GL_TEX_ONESAMPLER", "RECOMP_GL_TITLE_MIPS",
                                        "RECOMP_GL_CURTAIN" };
    size_t i;
    for (i = 0; i < sizeof name / sizeof name[0]; i++) {
        const char *v = getenv(name[i]);
        fprintf(stderr, "%s: %s=%s\n", when, name[i], v ? v : "-");
    }
}

int main(void)
{
    developer_start();
    show("started");
    developer_restart();
    show("restarting");
    return 0;
}
