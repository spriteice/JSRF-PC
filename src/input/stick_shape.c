/* The sticks' shape (stick_shape.h): stick_shape=0 (Square) or 1 (Round) in
 * the settings file, or Input > Behaviour > Stick shape.
 *
 * Read once, at start (stick_shape_start). The menu's row changes it at
 * once and keeps it in the settings file; it runs on the window's thread,
 * and the pad read uses it on the game's. */
#include <math.h>
#include <stdio.h>
#include "stick_shape.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

/* nv2a_gl.c: a number from the settings file the engine keeps, and a number
 * kept there. Weak, as the input layer is linked without the window in a
 * headless build, which has no settings file. */
extern int  nv_window_pref_num(const char *key, double *v) __attribute__((weak));
extern void nv_window_pref_set_num(const char *key, double v) __attribute__((weak));

static const char *const k_names[STICK_SHAPE_CHOICES] = { "Square", "Round" };
static volatile int s_shape = STICK_SHAPE_DEFAULT;

/* A sum back to an axis: rounded, and held to the axis's ends. */
static short to_axis(double v)
{
    long r = lround(v);
    return (short)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
}

/* (x, y) has length r and direction (x / r, y / r); m, the larger of that
 * direction's two parts, is 1 on an axis and 0.71 on a diagonal, and the
 * stick divided by m reaches the square's edge where it reached the
 * circle's. x / m is x times r over the larger of |x| and |y|. */
void stick_shape_square(short *x, short *y)
{
    double fx = *x, fy = *y;
    double big = fabs(fx) > fabs(fy) ? fabs(fx) : fabs(fy);
    double r;
    if (big <= 0.0) return;
    r = sqrt(fx * fx + fy * fy);
    *x = to_axis(fx * r / big);
    *y = to_axis(fy * r / big);
}

void stick_shape_pad(short axes[4])
{
    if (s_shape != STICK_SHAPE_SQUARE) return;
    stick_shape_square(&axes[0], &axes[1]);
    stick_shape_square(&axes[2], &axes[3]);
}

int stick_shape_get(void)
{
    return s_shape;
}

#if defined(XBOXRECOMP_MENU)
static int  menu_shape(void) { return s_shape; }
static void menu_shape_pick(int i)
{
    if (i < 0 || i >= STICK_SHAPE_CHOICES) return;
    s_shape = i;
    fprintf(stderr, "[PAD] stick shape: %s (the menu)\n", k_names[i]);
    if (nv_window_pref_set_num) nv_window_pref_set_num("stick_shape", i);
}
#endif

void stick_shape_start(void)
{
    double v;
    int r = nv_window_pref_num ? nv_window_pref_num("stick_shape", &v) : 0;
    s_shape = STICK_SHAPE_DEFAULT;
    if (r > 0 && (v == STICK_SHAPE_SQUARE || v == STICK_SHAPE_ROUND))
        s_shape = (int)v;
    else if (r != 0)
        fprintf(stderr, "[PAD] settings: stick_shape is not 0 or 1; using %d\n", STICK_SHAPE_DEFAULT);
    fprintf(stderr, "[PAD] stick shape: %s (settings)\n", k_names[s_shape]);
#if defined(XBOXRECOMP_MENU)
    {
        /* The last row of Input > Behaviour, after Keyboard moves
         * (port/keyboard_move.c): it adds the submenu too, which adds
         * nothing once that has (menu.h). File > Presets: Square in each,
         * Xbox mode too, as the console's own pads reach the square. */
        const XBOX_MENU_ITEM rows[] = {
            { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_GROUP, .label = XBOX_GROUP_BEHAVIOUR,
              .hint = XBOX_GROUP_BEHAVIOUR_HINT },
            { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_CHOICE, .label = "Stick shape",
              .group = XBOX_GROUP_BEHAVIOUR, .state = menu_shape, .pick = menu_shape_pick,
              .choices = k_names, .nchoices = STICK_SHAPE_CHOICES,
              .presets = XBOX_PRESETS_SET,
              .preset = { STICK_SHAPE_SQUARE, STICK_SHAPE_DEFAULT, STICK_SHAPE_DEFAULT },
              .hint = "Square stretches each stick to the Xbox pad's square reach." },
        };
        xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
    }
#endif
}
