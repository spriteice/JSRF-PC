/* The game's own camera shots (camera_scenes.h).
 *
 * The ticks are graffiti_cam=, half_pipe_cam= and loop_cam= in the settings
 * file, or Input > Behaviour > Camera (camscene_start), whose first two
 * ticks are camera_walls.h's. Its row runs on the window's thread, and the
 * camera update's hook reads them on the game's. */
#include <stdio.h>
#include "camera_scenes.h"
#if defined(XBOXRECOMP_MENU)
#  include "camera_walls.h"
#  include "menu.h"
#endif

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file the engine already keeps, and a
 * number kept there. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
#else
/* Without the SDL window (a headless build) there is no settings file: no
 * keys. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
#endif

/* Each tick's name in the row, in the log, and its key in the settings
 * file, in CAMSCENE_'s order. */
static const char *const k_names[CAMSCENES] = { "Graffiti cam", "Half pipe cam", "Loop cam" };
static const char *const k_keys[CAMSCENES] = { "graffiti_cam", "half_pipe_cam", "loop_cam" };

static volatile unsigned s_ticks = CAMSCENE_DEFAULT;

int camscene_shot(uint32_t mode)
{
    return mode == CAMSCENE_MODE_GRAFFITI;
}

#if defined(XBOXRECOMP_MENU)
/* The row's ticks (CAMROW_), and the line each shows at the bottom. */
static const char *const k_row_names[CAMROW_TICKS] = {
    "Default cam fix", "No skater fade", "Graffiti cam", "Half pipe cam", "Loop cam"
};
static const char *const k_row_hints[CAMROW_TICKS] = {
    "Fixes the default camera clipping through walls, mostly in widescreen.",
    "Keeps the camera back at a wall instead of fading your skater.",
    "Lets the game turn the camera to the graffiti you spray.",
    "Lets the game frame your jumps out of a half pipe.",
    "Lets the game place its fixed shots, as on a loop.",
};

static int menu_ticks(void)
{
    return (camwall_on() != 0) << CAMROW_FIX | (camwall_whole() != 0) << CAMROW_FADE
         | (int)(s_ticks << CAMROW_SHOTS);
}
static void menu_pick(int i)
{
    unsigned bit;
    if (i == CAMROW_FIX) {
        camwall_flip();
        return;
    }
    if (i == CAMROW_FADE) {
        camwall_whole_flip();
        return;
    }
    i -= CAMROW_SHOTS;
    if (i < 0 || i >= CAMSCENES) return;
    bit = CAMSCENE_BIT(i);
    s_ticks ^= bit;
    fprintf(stderr, "[CAMSCENE] %s %s (the menu)\n", k_names[i], s_ticks & bit ? "on" : "off");
    nv_window_pref_set_num(k_keys[i], (s_ticks & bit) != 0);
}
#endif

void camscene_start(void)
{
    unsigned ticks = 0;
    double v;
    int i;
    for (i = 0; i < CAMSCENES; i++) {
        int r = nv_window_pref_num(k_keys[i], &v), on = (CAMSCENE_DEFAULT & CAMSCENE_BIT(i)) != 0;
        if (r > 0 && (v == 0 || v == 1))
            on = v != 0;
        else if (r != 0)
            fprintf(stderr, "[CAMSCENE] settings: %s is not 0 or 1; using %d\n", k_keys[i], on);
        if (on) ticks |= CAMSCENE_BIT(i);
    }
    s_ticks = ticks;
    fprintf(stderr, "[CAMSCENE] %s %s, %s %s, %s %s (settings)\n",
            k_names[0], ticks & CAMSCENE_BIT(0) ? "on" : "off",
            k_names[1], ticks & CAMSCENE_BIT(1) ? "on" : "off",
            k_names[2], ticks & CAMSCENE_BIT(2) ? "on" : "off");
#if defined(XBOXRECOMP_MENU)
    {
        /* Input > Behaviour's first row, before Keyboard moves: it adds the
         * submenu too, which adds nothing once that has (menu.h). Each tick
         * has its own line at the bottom. File > Presets: Xbox's
         * (CAMROW_XBOX); Modern's, the walls' fix alone (CAMROW_MODERN); and
         * left by the others. */
        const XBOX_MENU_ITEM rows[] = {
            { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_GROUP, .label = XBOX_GROUP_BEHAVIOUR,
              .hint = XBOX_GROUP_BEHAVIOUR_HINT },
            { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_TOGGLES, .label = "Camera",
              .group = XBOX_GROUP_BEHAVIOUR, .state = menu_ticks, .pick = menu_pick,
              .choices = k_row_names, .nchoices = CAMROW_TICKS, .choice_hints = k_row_hints,
              .presets = XBOX_PRESETS_SET, .preset = { CAMROW_XBOX, CAMROW_MODERN, 0, 0, 0 },
              .leave = XBOX_PRESETS_LEVELS,
              .hint = "Sets how the camera handles walls and the game's own shots." },
        };
        xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
    }
#endif
}

unsigned camscene_ticks(void)
{
    return s_ticks;
}

unsigned camscene_asked(uint32_t asked, uint32_t fixed, uint32_t exit, uint32_t graffiti)
{
    unsigned shots = 0;
    if (graffiti) shots |= CAMSCENE_BIT(CAMSCENE_GRAFFITI);
    if (asked == CAMSCENE_MODE_HALF_PIPE) shots |= CAMSCENE_BIT(CAMSCENE_HALF_PIPE);
    if (fixed && !exit) shots |= CAMSCENE_BIT(CAMSCENE_FIXED);
    return shots;
}

unsigned camscene_hidden(unsigned shots, unsigned ticks)
{
    return shots & ~ticks & CAMSCENE_ALL;
}

unsigned camscene_ticks_before(int freecam)
{
    return CAMSCENE_BIT(CAMSCENE_HALF_PIPE) | CAMSCENE_BIT(CAMSCENE_FIXED)
         | (freecam ? 0u : CAMSCENE_BIT(CAMSCENE_GRAFFITI));
}

double camscene_rise(CAMSCENE_RISE *r, int jump, double skater, double target)
{
    if (jump) {
        /* A jump starts, from what is added now: none, or what is still
         * being let go of after the last. */
        if (!r->on || r->out) {
            r->target0 = target + (r->on ? r->lift : 0);
            r->skater0 = skater;
            r->on = 1;
            r->out = 0;
        }
        r->lift = (skater - r->skater0) - (target - r->target0);
    } else if (r->on) {
        double t, k;
        if (!r->out) r->from = r->lift;
        if (++r->out >= CAMSCENE_RISE_OUT) {
            r->on = r->out = 0;
            r->lift = 0;
        } else {
            t = (double)r->out / CAMSCENE_RISE_OUT;
            k = t * t * (3 - 2 * t);
            r->lift = r->from * (1 - k);
        }
    }
    return r->on ? r->lift : 0;
}
