/* The intro (intro.h): skipintro=1 in the settings file, or Display > Skip
 * intro.
 *
 * Read once, at start (intro_start). The menu's row changes it at once and
 * keeps it in the settings file; it runs on the window's thread, and the
 * cards' update reads it on the game's. */
#include <stdio.h>
#include "intro.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file the engine already keeps, and a
 * number kept there. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
#else
/* The Mac and iOS keep their settings as preferences of their own: no keys. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
#endif

static volatile int s_skip;

#if defined(XBOXRECOMP_MENU)
static int  menu_skip(void) { return s_skip; }
static void menu_skip_flip(void)
{
    s_skip = !s_skip;
    fprintf(stderr, "[INTRO] skip %s (the menu)\n", s_skip ? "on" : "off");
    nv_window_pref_set_num("skipintro", s_skip);
}
#endif

void intro_start(void)
{
    double v;
    s_skip = nv_window_pref_num("skipintro", &v) > 0 && v != 0;
    fprintf(stderr, "[INTRO] skip %s (settings)\n", s_skip ? "on" : "off");
#if defined(XBOXRECOMP_MENU)
    {
        /* Below the window's rows, which are added once it is up. */
        const XBOX_MENU_ITEM row = {
            .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_TOGGLE, .label = "Skip intro",
            .state = menu_skip, .run = menu_skip_flip, .last = 1 };
        xbox_MenuAdd(&row, 1);
    }
#endif
}

int intro_skip(void)
{
    return s_skip;
}
