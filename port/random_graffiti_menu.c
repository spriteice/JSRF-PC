/* Gameplay > Graffiti: random graffiti (port/random_graffiti.h), its switch
 * and its options, kept in the settings file. The submenu is Gameplay's
 * second, under Skips and above Graffiti souls (src/nv2a_gl/nv2a_gl.c adds
 * it; spriteice, 28 November 2026), and its rows always show, the switch on
 * or off (spriteice, 27 November 2026: "less confusing that way"), in this
 * order: Random graffiti (Restart), Keep sprayed tags (at the top of the
 * options, spriteice the same day), Designs, Shuffle.
 *
 * random_graffiti_start runs once, at start, before the game does
 * (port/main.c). The switch is taken then, for the run: its row keeps the
 * change for the next start and, after asking, restarts the game, as Audio
 * > Audio effects does. JSRF_RANDOM_GRAFFITI in the environment is over the
 * settings, for a test. The other rows run on the window's thread and set
 * the module's options at once; the module takes them at the next area's
 * load (Graffiti soul's at the soul), and with the switch off they wait for
 * it. Each is kept in the settings file:
 *
 *     random_graffiti        the switch, 0 or 1
 *     graffiti_keep          Keep sprayed tags, 0 or 1
 *     graffiti_designs       0 Unlocked, 1 My own, 2 Both
 *     graffiti_shuffle_load  Shuffle > Area load, 0 or 1
 *     graffiti_shuffle_soul  Shuffle > Graffiti soul, 0 or 1
 *
 * The switch was Developer > Experimental's, JSRF_RANDOM_GRAFFITI=1 in
 * developer.cfg, until b3 row 43: with no random_graffiti= yet, that is
 * taken, once, and kept as random_graffiti=. The options' defaults are
 * spriteice's, from the card at 2:32pm on 27 November 2026: kept, Both, both
 * shuffles; b2.2-hotfix's were off, Unlocked, and both.
 *
 * File > Presets: Xbox (Restart) turns the switch off, as it turns off what
 * JSRF-PC adds to how the game plays, kept for the next start (keep), and
 * the others leave it (spriteice's card, 3:35pm on 27 November 2026, "Xbox
 * off, the rest leave it"); every preset leaves the options, the player's
 * own, which do nothing with the switch off.
 *
 * And the host's path of the kept tags' file, in the game save's folder
 * (random_graffiti_host_path), from the kernel's own path layer, UTF-8 on
 * Windows too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"              /* xbox_translate_path, the kernel's path layer */
#include "random_graffiti.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file, a number kept there, and a
 * number from a file of ours beside it. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
extern int  nv_window_file_get(const char *file, const char *key, int def);
#else
/* Without the SDL window (a headless build) there is no settings file: the
 * defaults, the switch off. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
static void nv_window_pref_set_num(const char *key, double v) { (void)key; (void)v; }
static int nv_window_file_get(const char *file, const char *key, int def)
{
    (void)file; (void)key;
    return def;
}
#endif

#if defined(XBOXRECOMP_MENU)
/* nv2a_gl.c: the game started again (File > Restart's), or nothing where it
 * can't be. */
extern void nv_window_restart(void);
#endif

#define RG_SWITCH_KEY       "random_graffiti"
#define RG_SWITCH_ENV       "JSRF_RANDOM_GRAFFITI"
#define RG_SWITCH_OLD_FILE  "developer.cfg"     /* Developer > Experimental's, before row 43 */
#define RG_SWITCH_DEFAULT   0
#define RG_DEFAULT_DESIGNS  RG_DESIGNS_BOTH
#define RG_DEFAULT_KEEP     1
#define RG_DEFAULT_AREA     1
#define RG_DEFAULT_SOUL     1

static const char *const k_key[RG_OPTS] = {
    "graffiti_designs", "graffiti_keep", "graffiti_shuffle_load", "graffiti_shuffle_soul"
};
static const int k_default[RG_OPTS] = { RG_DEFAULT_DESIGNS, RG_DEFAULT_KEEP, RG_DEFAULT_AREA, RG_DEFAULT_SOUL };
static const char *const k_designs[] = { "Unlocked", "My own", "Both" };

static volatile int s_next;     /* the switch for the next start, as its row has it */

#if defined(XBOXRECOMP_MENU)
/* Kept for the next start, which takes it (random_graffiti_start). */
static void switch_keep(int on)
{
    s_next = on != 0;
    nv_window_pref_set_num(RG_SWITCH_KEY, s_next);
    fprintf(stderr, "[GRAFFITI] Random graffiti %s from the next start (the menu)\n", s_next ? "on" : "off");
}
static int  switch_now(void) { return s_next; }
static void switch_flip(void)
{
    switch_keep(!s_next);
    nv_window_restart();        /* returns only where the game can't restart */
}

/* An option set, kept in the settings file, and said in the log. */
static void set(int which, int value, const char *how)
{
    random_graffiti_option(which, value);
    nv_window_pref_set_num(k_key[which], value);
    if (which == RG_OPT_DESIGNS)
        fprintf(stderr, "[GRAFFITI] Designs: %s (%s)\n", k_designs[value], how);
    else
        fprintf(stderr, "[GRAFFITI] %s %s (%s)\n", which == RG_OPT_KEEP ? "Keep sprayed tags"
                : which == RG_OPT_AREA ? "Shuffle at Area load" : "Shuffle at Graffiti soul",
                value ? "on" : "off", how);
}

static int  designs_now(void) { return random_graffiti_option_now(RG_OPT_DESIGNS); }
static void designs_pick(int i) { if (i >= 0 && i <= RG_DESIGNS_BOTH) set(RG_OPT_DESIGNS, i, "the menu"); }
static int  keep_now(void) { return random_graffiti_option_now(RG_OPT_KEEP); }
static void keep_flip(void) { set(RG_OPT_KEEP, !random_graffiti_option_now(RG_OPT_KEEP), "the menu"); }
static int  shuffle_now(void)
{
    return (random_graffiti_option_now(RG_OPT_AREA) ? 1 : 0) | (random_graffiti_option_now(RG_OPT_SOUL) ? 2 : 0);
}
static void shuffle_flip(int i)
{
    const int which = i == 0 ? RG_OPT_AREA : i == 1 ? RG_OPT_SOUL : -1;
    if (which >= 0) set(which, !random_graffiti_option_now(which), "the menu");
}

static const char *const k_designs_hint[] = {
    "Draws from the designs your save has unlocked.",
    "Draws from designs you made or imported, where a size has any.",
    "Draws from unlocked designs and your own together.",
};
static const char *const k_shuffle[] = { "Area load", "Graffiti soul" };
static const char *const k_shuffle_hint[] = {
    "Draws them again each time an area loads.",
    "Draws them again when you pick up a graffiti soul.",
};
#endif

void random_graffiti_start(void)
{
    const char *env = getenv(RG_SWITCH_ENV);
    double v;
    int k, from_old = 0;

    /* The switch: the settings', or developer.cfg's from before, or off. */
    if (nv_window_pref_num(RG_SWITCH_KEY, &v) > 0) {
        s_next = v != 0;
    } else {
        s_next = nv_window_file_get(RG_SWITCH_OLD_FILE, RG_SWITCH_ENV, RG_SWITCH_DEFAULT) != 0;
        if (s_next) {
            from_old = 1;
            nv_window_pref_set_num(RG_SWITCH_KEY, s_next);
        }
    }
    random_graffiti_switch(env ? *env && strcmp(env, "0") != 0 : s_next);
    for (k = 0; k < RG_OPTS; k++) {
        int value = nv_window_pref_num(k_key[k], &v) > 0 ? (int)v : k_default[k];
        if (k == RG_OPT_DESIGNS ? value < RG_DESIGNS_UNLOCKED || value > RG_DESIGNS_BOTH : value != 0 && value != 1)
            value = k_default[k];
        random_graffiti_option(k, value);
    }
    if (env)
        fprintf(stderr, "[GRAFFITI] Random graffiti %s (" RG_SWITCH_ENV "=%s in the environment, over "
                RG_SWITCH_KEY "=%d)\n", random_graffiti_on() ? "on" : "off", env, s_next);
    else
        fprintf(stderr, "[GRAFFITI] Random graffiti %s (%s)\n", random_graffiti_on() ? "on" : "off",
                from_old ? RG_SWITCH_ENV "=1 in " RG_SWITCH_OLD_FILE ", from Developer > Experimental, "
                           "now kept as " RG_SWITCH_KEY "=1" : "settings");
    fprintf(stderr, "[GRAFFITI] options: Keep sprayed tags %s, Designs %s, Shuffle at Area load %s and at "
            "Graffiti soul %s (settings)\n",
            random_graffiti_option_now(RG_OPT_KEEP) ? "on" : "off",
            k_designs[random_graffiti_option_now(RG_OPT_DESIGNS)],
            random_graffiti_option_now(RG_OPT_AREA) ? "on" : "off",
            random_graffiti_option_now(RG_OPT_SOUL) ? "on" : "off");
#if defined(XBOXRECOMP_MENU)
    {
        const XBOX_MENU_ITEM rows[] = {
            { .menu = XBOX_MENU_GAMEPLAY, .kind = XBOX_ITEM_TOGGLE, .label = "Random graffiti (Restart)",
              .state = switch_now, .run = switch_flip,
              .confirm = "Apply selection and restart, losing unsaved progress?",
              .group = XBOX_GROUP_GRAFFITI,
              .presets = XBOX_PRESETS_SET, .preset = { 0, 0, 0, 0, 0 },
              .leave = XBOX_PRESET_BIT(XBOX_PRESET_MODERN) | XBOX_PRESETS_LEVELS,
              .keep = switch_keep,
              .hint = "Sprays a random design at each spot, not the garage's choice." },
            { .menu = XBOX_MENU_GAMEPLAY, .kind = XBOX_ITEM_TOGGLE, .label = "Keep sprayed tags",
              .state = keep_now, .run = keep_flip, .group = XBOX_GROUP_GRAFFITI,
              .presets = XBOX_PRESETS_LEFT,
              .hint = "A tag you spray keeps its design until the tag is gone." },
            { .menu = XBOX_MENU_GAMEPLAY, .kind = XBOX_ITEM_CHOICE, .label = "Designs",
              .state = designs_now, .pick = designs_pick, .choices = k_designs, .nchoices = 3,
              .choice_hints = k_designs_hint, .group = XBOX_GROUP_GRAFFITI,
              .presets = XBOX_PRESETS_LEFT,
              .hint = "Picks which designs random graffiti draws from." },
            { .menu = XBOX_MENU_GAMEPLAY, .kind = XBOX_ITEM_TOGGLES, .label = "Shuffle",
              .state = shuffle_now, .pick = shuffle_flip, .choices = k_shuffle, .nchoices = 2,
              .choice_hints = k_shuffle_hint, .group = XBOX_GROUP_GRAFFITI,
              .presets = XBOX_PRESETS_LEFT,
              .hint = "Picks when the designs for new tags are drawn again." },
        };
        xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
    }
#endif
}

int random_graffiti_host_path(const char *xbox_path, char *out, uint32_t n)
{
#if defined(_WIN32)
    xbox_host_char host[MAX_PATH];
    if (!n || !xbox_translate_path(xbox_path, host, MAX_PATH))
        return 0;
    return WideCharToMultiByte(CP_UTF8, 0, host, -1, out, (int)n, NULL, NULL) > 0;
#else
    xbox_host_char host[MAX_PATH];
    if (!n || !xbox_translate_path(xbox_path, host, MAX_PATH) || strlen(host) >= n)
        return 0;
    memcpy(out, host, strlen(host) + 1);
    return 1;
#endif
}
