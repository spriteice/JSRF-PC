/* Draw distance (draw_distance.h): drawdistance= in the settings file, or
 * Display > Draw distance.
 *
 * Read once, at start (draw_distance_start). The menu's row changes it at once
 * and keeps it in the settings file; it runs on the window's thread, and the
 * gates read it on the game's. A step multiplies every distance a gate cuts or
 * fades at by the same number, so each kind of object keeps its own distances
 * in proportion, and a fade keeps its length in proportion too. */
#include <float.h>
#include <stdio.h>
#include "draw_distance.h"
#if defined(XBOXRECOMP_MENU)
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

/* The row goes right under the window's Render scale (nv2a_gl.c). */
#define DRAW_UNDER     "Render scale (Restart)"
#define DRAW_LOG_EVERY 5.0      /* seconds between [DRAW] lines */

static const char *const k_names[DRAW_DISTANCE_STEPS] = { "1x", "2x", "3x", "4x", "Unlimited" };
static const char *const k_gates[DRAW_GATES] = {
    "crowds", "pickups", "checkpoints", "props and traffic", "enemies", "attached part" };

static volatile int s_step;

int draw_distance_step(void)
{
    return s_step;
}

float draw_distance_far(float game)
{
    int step = s_step;
    return step == DRAW_DISTANCE_UNLIMITED ? FLT_MAX : game * (float)(step + 1);
}

float draw_distance_per(float game)
{
    int step = s_step;
    return step == DRAW_DISTANCE_UNLIMITED ? game : game / (float)(step + 1);
}

void draw_distance_with(float *const at[], const unsigned char per[], int n, void (*run)(void))
{
    float was[8];
    int i;
    if (n > (int)(sizeof was / sizeof was[0])) n = (int)(sizeof was / sizeof was[0]);
    for (i = 0; i < n; i++) {
        was[i] = *at[i];
        *at[i] = per[i] ? draw_distance_per(was[i]) : draw_distance_far(was[i]);
    }
    run();
    for (i = 0; i < n; i++) *at[i] = was[i];
}

float draw_distance_fade(float d, float game_far, float game_per)
{
    return (float)(((double)draw_distance_far(game_far) - d) * draw_distance_per(game_per));
}

static unsigned long s_drawn[DRAW_GATES], s_past[DRAW_GATES];
static double s_since = -1;     /* the time the counts start from; -1: not yet */
static unsigned s_since_frame;

void draw_distance_drawn(int gate, int past, double now, unsigned frame)
{
    char line[512];
    unsigned frames;
    int len, g;
    if (gate < 0 || gate >= DRAW_GATES) return;
    s_drawn[gate]++;
    if (past) s_past[gate]++;
    if (s_since < 0) { s_since = now; s_since_frame = frame; return; }
    if (now - s_since < DRAW_LOG_EVERY) return;
    frames = frame - s_since_frame;
    if (!frames) frames = 1;
    len = snprintf(line, sizeof line, "[DRAW] t=%.1f %s, a frame's objects (past 1x's distance):",
                   now, k_names[s_step]);
    for (g = 0; g < DRAW_GATES && len > 0 && len < (int)sizeof line; g++) {
        len += snprintf(line + len, sizeof line - len, "%s %s %.1f (%.1f)", g ? "," : "",
                        k_gates[g], (double)s_drawn[g] / frames, (double)s_past[g] / frames);
        s_drawn[g] = s_past[g] = 0;
    }
    fprintf(stderr, "%s\n", line);
    s_since = now;
    s_since_frame = frame;
}

#if defined(XBOXRECOMP_MENU)
static int  menu_step(void) { return s_step; }
static void menu_step_pick(int i)
{
    if (i < 0 || i >= DRAW_DISTANCE_STEPS) return;
    s_step = i;
    fprintf(stderr, "[DRAW] objects at %s (the menu)\n", k_names[i]);
    nv_window_pref_set_num("drawdistance", i);
}
#endif

void draw_distance_start(void)
{
    double v;
    int r = nv_window_pref_num("drawdistance", &v);
    s_step = 0;
    if (r > 0 && v >= 0 && v < DRAW_DISTANCE_STEPS && v == (int)v)
        s_step = (int)v;
    else if (r > 0)
        fprintf(stderr, "[DRAW] settings: drawdistance=%g is not 0 to %d; using 0\n", v,
                DRAW_DISTANCE_STEPS - 1);
    else if (r < 0)
        fprintf(stderr, "[DRAW] settings: drawdistance is not a number; using 0\n");
    fprintf(stderr, "[DRAW] objects at %s (settings)\n", k_names[s_step]);
#if defined(XBOXRECOMP_MENU)
    {
        /* Rows are drawn in the order added, and the window adds Render scale
         * once it is up, after this: so the row names the one it goes under. */
        const XBOX_MENU_ITEM row = {
            .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_CHOICE, .label = "Draw distance",
            .state = menu_step, .pick = menu_step_pick,
            .choices = k_names, .nchoices = DRAW_DISTANCE_STEPS, .under = DRAW_UNDER };
        xbox_MenuAdd(&row, 1);
    }
#endif
}
