/* The free camera (freecam.h): freecam=1 in the settings file, or F8.
 *
 * The follow camera keeps what it works out for itself -- what it looks at,
 * how far it stays, where a wall stops it -- and freecam only moves the eye
 * around that target, by the right stick. Two angles say where: yaw, the way
 * the view faces, and pitch, how far the eye is above (or below) the target.
 * They start from the game's own view each time freecam takes over, and again
 * whenever the game puts its camera back behind the player (the L trigger).
 *
 * Not kept: the follow camera's lift, up to 30 units on the eye's height,
 * which the game raises when the ground falls away ahead of the way its own
 * camera faces, or when the player drops below where they last stood. The
 * view freecam steers faces elsewhere, so the lift would come and go with
 * ground the view isn't looking at, and the eye stays on its orbit instead.
 * Taking over while the game's camera is lifted lowers the view by the lift,
 * once. While freecam is on, spraying graffiti does not turn the camera to
 * the spot, and talking to someone does not turn it to them.
 *
 * The stick is used as it comes: past the deadzone the turn per frame is in
 * proportion to how far it is pushed, with nothing carried from one frame to
 * the next -- no acceleration, no smoothing. The settings are read once, at
 * start (freecam_start):
 *
 *   freecam        0 / 1    on at start (F8 flips it and keeps it here)
 *   invert_x       0 / 1    0: right turns the view right
 *   invert_y       0 / 1    0: up tilts the view up
 *   sensitivity_x  1 - 10   36 degrees a second at full tilt per step
 *   sensitivity_y  1 - 10
 *   deadzone       0 - 1    0.05 = 5%, measured on the stick's distance from
 *                           centre, and the speed rescaled from 0 at its edge
 *
 * A key that is missing takes its default; one that is out of range, or not
 * a number, takes its default and says so in the log. */
#include <math.h>
#include <stdio.h>
#include "freecam.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

/* Degrees per game frame, at full tilt, for each step of sensitivity: 36 a
 * second at the game's 60 frames a second. */
#define FREECAM_STEP_DEG 0.6
/* The steps of sensitivity the settings file takes (and the in-game menu
 * offers). */
#define FREECAM_SENS_MIN 1.0
#define FREECAM_SENS_MAX 10.0
/* How far the eye goes above or below the target, in degrees. The view is
 * built from an eye, a target and an up direction, which turns over at
 * straight up or down; this stays short of it. */
#define FREECAM_PITCH_MAX_DEG 80.0

static const double k_rad = 3.14159265358979323846 / 180.0;

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file the engine already keeps, a
 * number kept there, and a key the game binds for itself. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
extern void nv_window_hotkey(const char *name, void (*run)(void));
#else
/* The Mac and iOS keep their settings as preferences of their own: no keys. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
#endif

static struct {
    int invert_x, invert_y;
    double sens_x, sens_y, deadzone;
} s_cfg = { 0, 0, 5.0, 5.0, 0.05 };      /* the defaults */
static volatile int s_on;
static volatile int s_steer;    /* on, and the follow camera ran last frame */
static int s_follow_ran;        /* the follow camera ran since freecam_frame */
static short s_stick[4][2];     /* each port's right stick, as last read */
static uint32_t s_cam;          /* the camera yaw and pitch are for; 0: start them again */
static double s_yaw, s_pitch;   /* radians */

/* A key in [lo, hi], or def. */
static double cfg_num(const char *key, double def, double lo, double hi)
{
    double v;
    int r = nv_window_pref_num(key, &v);
    if (r == 0) return def;
    if (r > 0 && v >= lo && v <= hi) return v;
    if (r > 0)
        fprintf(stderr, "[FREECAM] settings: %s=%g is outside %g to %g; using %g\n",
                key, v, lo, hi, def);
    else
        fprintf(stderr, "[FREECAM] settings: %s is not a number; using %g\n", key, def);
    return def;
}

#if defined(NV2A_GL_USE_SDL)
/* F8, and the menu's Free camera row: on or off, kept in the settings file as
 * freecam=. On the window's thread. */
static void freecam_flip(void)
{
    s_on = !s_on;
    s_cam = 0;
    fprintf(stderr, "[FREECAM] %s (F8 or the menu)\n", s_on ? "on" : "off");
    nv_window_pref_set_num("freecam", s_on);
}
#endif

#if defined(XBOXRECOMP_MENU)
static void freecam_menu(void);
#endif

void freecam_start(void)
{
    s_on = cfg_num("freecam", 0, -HUGE_VAL, HUGE_VAL) != 0;
    s_cfg.invert_x = cfg_num("invert_x", s_cfg.invert_x, -HUGE_VAL, HUGE_VAL) != 0;
    s_cfg.invert_y = cfg_num("invert_y", s_cfg.invert_y, -HUGE_VAL, HUGE_VAL) != 0;
    s_cfg.sens_x = cfg_num("sensitivity_x", s_cfg.sens_x, FREECAM_SENS_MIN, FREECAM_SENS_MAX);
    s_cfg.sens_y = cfg_num("sensitivity_y", s_cfg.sens_y, FREECAM_SENS_MIN, FREECAM_SENS_MAX);
    s_cfg.deadzone = cfg_num("deadzone", s_cfg.deadzone, 0, 1);
    fprintf(stderr, "[FREECAM] %s (settings); invert x %d y %d, sensitivity x %g y %g, deadzone %g\n",
            s_on ? "on" : "off", s_cfg.invert_x, s_cfg.invert_y, s_cfg.sens_x, s_cfg.sens_y,
            s_cfg.deadzone);
#if defined(XBOXRECOMP_MENU)
    freecam_menu();
#endif
#if defined(NV2A_GL_USE_SDL)
    nv_window_hotkey("F8", freecam_flip);
#endif
}

void freecam_frame(void)
{
    int steer = s_on && s_follow_ran;
    if (steer != s_steer)
        fprintf(stderr, "[FREECAM] %s\n", steer ? "steering the camera"
                : s_on ? "idle: the follow camera is not running" : "idle");
    if (!s_follow_ran) s_cam = 0;
    s_steer = steer;
    s_follow_ran = 0;
}

void freecam_pad(unsigned port, short *rx, short *ry)
{
    if (!rx) { s_stick[port & 3][0] = s_stick[port & 3][1] = 0; return; }
    s_stick[port & 3][0] = *rx;
    s_stick[port & 3][1] = *ry;
    if (s_steer) *rx = *ry = 0;
}

/* The farthest-pushed right stick, -1..1 on each axis, past a radial
 * deadzone and rescaled so that its edge is 0 and full tilt is 1. */
static void freecam_stick(double *x, double *y)
{
    double best = -1, m, k;
    int p;
    *x = *y = 0;
    for (p = 0; p < 4; p++) {
        double sx = s_stick[p][0] / 32767.0, sy = s_stick[p][1] / 32767.0;
        if (sx * sx + sy * sy > best) { best = sx * sx + sy * sy; *x = sx; *y = sy; }
    }
    m = sqrt(best);
    if (m <= s_cfg.deadzone || s_cfg.deadzone >= 1) { *x = *y = 0; return; }
    k = ((m > 1 ? 1 : m) - s_cfg.deadzone) / (1 - s_cfg.deadzone) / m;
    *x *= k; *y *= k;
}

int freecam_orbit(uint32_t cam, uint32_t mode, const float tgt[3], const float seye[3],
                  int reset, float eye[3])
{
    const double pmax = FREECAM_PITCH_MAX_DEG * k_rad, step = FREECAM_STEP_DEG * k_rad;
    double v[3], d, x, y, cp;
    s_follow_ran = 1;
    if (!s_on) return 0;
    v[0] = seye[0] - tgt[0]; v[1] = seye[1] - tgt[1]; v[2] = seye[2] - tgt[2];
    d = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(d > 0)) return 0;
    if (cam != s_cam || reset) {
        /* Left-handed, y up (as auto_step steers): the view faces from the
         * eye to the target, heading atan2(x, z). */
        s_yaw = atan2(-v[0], -v[2]);
        s_pitch = asin(v[1] / d);
        fprintf(stderr, "[FREECAM] following camera %08X (mode %u) from heading %.0f, pitch %.0f%s\n",
                cam, mode, s_yaw / k_rad, s_pitch / k_rad,
                cam == s_cam ? ", as the game put it back behind the player" : "");
        s_cam = cam;
    }
    freecam_stick(&x, &y);
    if (s_cfg.invert_x) x = -x;
    if (s_cfg.invert_y) y = -y;
    s_yaw = remainder(s_yaw + x * s_cfg.sens_x * step, 2 * 180.0 * k_rad);
    s_pitch -= y * s_cfg.sens_y * step;     /* up tilts the view up: the eye goes down */
    if (s_pitch > pmax) s_pitch = pmax;
    if (s_pitch < -pmax) s_pitch = -pmax;
    cp = cos(s_pitch);
    eye[0] = (float)(tgt[0] - d * cp * sin(s_yaw));
    eye[1] = (float)(tgt[1] + d * sin(s_pitch));
    eye[2] = (float)(tgt[2] - d * cp * cos(s_yaw));
    return 1;
}

int freecam_on(void)
{
    return s_on;
}

#if defined(XBOXRECOMP_MENU)
/* The in-game menu's Controller page (src/menu/menu.h). A row changes what
 * freecam uses at once and keeps it in the settings file, as F8 keeps
 * freecam=; it runs on the window's thread, as F8 does. The deadzone is
 * shown, and stepped, in percent. */

static int    menu_on(void)            { return s_on; }
static int    menu_inv_x(void)         { return s_cfg.invert_x; }
static void   menu_inv_x_run(void)     { s_cfg.invert_x = !s_cfg.invert_x; nv_window_pref_set_num("invert_x", s_cfg.invert_x); }
static int    menu_inv_y(void)         { return s_cfg.invert_y; }
static void   menu_inv_y_run(void)     { s_cfg.invert_y = !s_cfg.invert_y; nv_window_pref_set_num("invert_y", s_cfg.invert_y); }
static double menu_sens_x(void)        { return s_cfg.sens_x; }
static void   menu_sens_x_set(double v) { s_cfg.sens_x = v; nv_window_pref_set_num("sensitivity_x", v); }
static double menu_sens_y(void)        { return s_cfg.sens_y; }
static void   menu_sens_y_set(double v) { s_cfg.sens_y = v; nv_window_pref_set_num("sensitivity_y", v); }
static double menu_dz(void)            { return s_cfg.deadzone * 100; }
static void   menu_dz_set(double v)    { s_cfg.deadzone = v / 100; nv_window_pref_set_num("deadzone", v / 100); }

static void freecam_menu(void)
{
    const XBOX_MENU_ITEM rows[] = {
        { .menu = XBOX_MENU_CONTROLLER, .kind = XBOX_ITEM_TOGGLE, .label = "Free camera",
          .state = menu_on, .run = freecam_flip },
        { .menu = XBOX_MENU_CONTROLLER, .kind = XBOX_ITEM_TOGGLE, .label = "Invert X",
          .state = menu_inv_x, .run = menu_inv_x_run },
        { .menu = XBOX_MENU_CONTROLLER, .kind = XBOX_ITEM_TOGGLE, .label = "Invert Y",
          .state = menu_inv_y, .run = menu_inv_y_run },
        { .menu = XBOX_MENU_CONTROLLER, .kind = XBOX_ITEM_NUMBER, .label = "Sensitivity X",
          .value = menu_sens_x, .set = menu_sens_x_set,
          .min = FREECAM_SENS_MIN, .max = FREECAM_SENS_MAX, .step = 1, .format = "%g" },
        { .menu = XBOX_MENU_CONTROLLER, .kind = XBOX_ITEM_NUMBER, .label = "Sensitivity Y",
          .value = menu_sens_y, .set = menu_sens_y_set,
          .min = FREECAM_SENS_MIN, .max = FREECAM_SENS_MAX, .step = 1, .format = "%g" },
        { .menu = XBOX_MENU_CONTROLLER, .kind = XBOX_ITEM_NUMBER, .label = "Deadzone",
          .value = menu_dz, .set = menu_dz_set, .min = 0, .max = 100, .step = 5, .format = "%g%%" },
    };
    xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
}
#endif
