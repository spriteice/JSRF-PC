/* The free camera (freecam.h): freecam=1 in the settings file, or F8.
 *
 * The follow camera keeps what it works out for itself -- what it looks at,
 * how far it stays, where a wall stops it -- and freecam only moves the eye
 * around that target, by the right stick. Two angles say where: yaw, the way
 * the view faces, and pitch, how far the eye is above (or below) the target.
 * They start from the game's own view each time freecam takes over, and again
 * whenever the game puts its camera back behind the player (the L trigger).
 * A camera the game does not post-update -- paused, the task manager runs
 * none of its tasks' post-updates -- keeps them; one the game post-updates in
 * a mode of its own, for a scene, starts them again when it follows the
 * player once more.
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
 * the next -- no acceleration, no smoothing. With auto centre, a stick let go
 * for a while lets the view ease back to the game's own, behind the player,
 * and keep following it there until the stick is pushed: how long it waits
 * and how fast it eases are the level's (k_centre). A pad's R3 flips freecam,
 * as F8 does: the game reads R3 only as one screen's "any button" (0x7DE20,
 * 0x7DED0), so it is free. The settings are read once, at start
 * (freecam_start):
 *
 *   freecam        0 / 1    on at start (F8 and R3 flip it and keep it here)
 *   autocentre     0 - 4    off, mild, medium, high, aggressive
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
/* Auto centre's levels, as the menu names them and autocentre= numbers
 * them: for each, the game frames the stick is let go before the view eases
 * back, and the share of what is left that each frame then closes. 1/N a
 * frame brings two thirds of the way back in about 1.1 N frames. */
static const char *const k_centre_names[] = { "Off", "Mild", "Medium", "High", "Aggressive" };
static const struct { int wait; double ease; } k_centre[] = {
    { 0, 0 },           /* off */
    { 120, 90.0 },      /* 2 s, then two thirds back in 1.6 s */
    { 90, 60.0 },       /* 1.5 s, then 1.1 s */
    { 60, 30.0 },       /* 1 s, then 0.55 s */
    { 30, 12.0 },       /* 0.5 s, then 0.2 s */
};
#define FREECAM_CENTRE_LEVELS ((int)(sizeof k_centre / sizeof k_centre[0]))
_Static_assert(sizeof k_centre_names / sizeof k_centre_names[0] == FREECAM_CENTRE_LEVELS,
               "a name for each level");
/* After a wall (freecam_wall): game frames the eye stays where the wall put
 * it, once nothing brings it further in, before it goes back out -- a third
 * of a second, so a wall that comes and goes, as in a tight space, does not
 * shake it in and out -- and then the share of the way back out it closes
 * each frame. 1/10 brings it two thirds of the way in 11 frames, about a
 * fifth of a second, and nearly all of it in half a second. */
#define FREECAM_WALL_HOLD 20
#define FREECAM_WALL_EASE 10.0
/* How many times freecam_clear moves the eye across, and then brings it
 * in, for the corners of the view before it takes it that neither clears
 * them (a space narrower than the view). Each time costs four or five line
 * tests. */
#define FREECAM_WALL_TRIES 3

static const double k_rad = 3.14159265358979323846 / 180.0;

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file the engine already keeps, a
 * number kept there, and a key the game binds for itself. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
extern void nv_window_hotkey(const char *name, void (*run)(void));
extern void nv_window_pad_hotkey(const char *name, void (*run)(void));
#else
/* The Mac and iOS keep their settings as preferences of their own: no keys. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
#endif

static struct {
    int invert_x, invert_y, centre;     /* centre: auto centre's level, 0 off */
    double sens_x, sens_y, deadzone;
} s_cfg = { 0, 0, 0, 5.0, 5.0, 0.05 };       /* the defaults */
static volatile int s_on;
static volatile int s_steer;    /* on, and the follow camera ran last frame */
static int s_follow_ran;        /* the follow camera ran since freecam_frame */
static int s_cam_ran;           /* and s_cam's update did */
static int s_held;              /* s_cam's update did not run last frame */
static int s_idle;              /* frames the stick has been let go, up to the level's wait */
static double s_wall = 1.0;     /* the share of its distance the eye had last frame, after walls */
static int s_wall_wait = FREECAM_WALL_HOLD; /* frames since a wall last brought it in, up to the hold */
static double s_across[3];      /* how far across the eye moved last frame, after walls */
static int s_across_wait = FREECAM_WALL_HOLD; /* frames since a wall last moved it across, up to the hold */
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
/* F8, R3 and the menu's Free camera row: on or off, kept in the settings file
 * as freecam=. On the window's thread. */
static void freecam_flip(void)
{
    s_on = !s_on;
    s_cam = 0;
    fprintf(stderr, "[FREECAM] %s (F8, R3 or the menu)\n", s_on ? "on" : "off");
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
    s_cfg.centre = (int)cfg_num("autocentre", s_cfg.centre, 0, FREECAM_CENTRE_LEVELS - 1);
    fprintf(stderr, "[FREECAM] %s (settings); invert x %d y %d, sensitivity x %g y %g, deadzone %g, auto centre %s\n",
            s_on ? "on" : "off", s_cfg.invert_x, s_cfg.invert_y, s_cfg.sens_x, s_cfg.sens_y,
            s_cfg.deadzone, k_centre_names[s_cfg.centre]);
#if defined(XBOXRECOMP_MENU)
    freecam_menu();
#endif
#if defined(NV2A_GL_USE_SDL)
    nv_window_hotkey("F8", freecam_flip);
    nv_window_pad_hotkey("rightstick", freecam_flip);
#endif
}

void freecam_frame(void)
{
    int steer = s_on && s_follow_ran;
    int held = s_on && s_cam && !s_follow_ran && !s_cam_ran;
    if (steer != s_steer || held != s_held)
        fprintf(stderr, "[FREECAM] %s\n", steer ? "steering the camera"
                : held ? "held: its camera did not run; keeping the view"
                : s_on ? "idle: the follow camera is not running" : "idle");
    if (!s_follow_ran && s_cam_ran) s_cam = 0;      /* the game has it, in a mode of its own */
    s_steer = steer;
    s_held = held;
    s_follow_ran = s_cam_ran = 0;
}

void freecam_ran(uint32_t cam)
{
    if (cam == s_cam) s_cam_ran = 1;
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
    double v[3], d, x, y, cp, gyaw, gpitch;
    s_follow_ran = 1;
    if (!s_on) return 0;
    v[0] = seye[0] - tgt[0]; v[1] = seye[1] - tgt[1]; v[2] = seye[2] - tgt[2];
    d = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(d > 0)) return 0;
    /* The game's own view. Left-handed, y up (as auto_step steers): the view
     * faces from the eye to the target, heading atan2(x, z). */
    gyaw = atan2(-v[0], -v[2]);
    gpitch = asin(v[1] / d);
    if (cam != s_cam || reset) {
        s_yaw = gyaw;
        s_pitch = gpitch;
        s_idle = 0;
        s_wall = 1.0;
        s_wall_wait = FREECAM_WALL_HOLD;
        s_across[0] = s_across[1] = s_across[2] = 0;
        s_across_wait = FREECAM_WALL_HOLD;
        fprintf(stderr, "[FREECAM] following camera %08X (mode %u) from heading %.0f, pitch %.0f%s\n",
                cam, mode, s_yaw / k_rad, s_pitch / k_rad,
                cam == s_cam ? ", as the game put it back behind the player" : "");
        s_cam = cam;
    }
    freecam_stick(&x, &y);
    if (x != 0 || y != 0) s_idle = 0;
    else if (s_idle < k_centre[s_cfg.centre].wait) s_idle++;
    if (s_cfg.invert_x) x = -x;
    if (s_cfg.invert_y) y = -y;
    s_yaw = remainder(s_yaw + x * s_cfg.sens_x * step, 2 * 180.0 * k_rad);
    s_pitch -= y * s_cfg.sens_y * step;     /* up tilts the view up: the eye goes down */
    if (s_cfg.centre && s_idle >= k_centre[s_cfg.centre].wait) {
        double ease = k_centre[s_cfg.centre].ease;
        s_yaw = remainder(s_yaw + remainder(gyaw - s_yaw, 2 * 180.0 * k_rad) / ease, 2 * 180.0 * k_rad);
        s_pitch += (gpitch - s_pitch) / ease;
    }
    if (s_pitch > pmax) s_pitch = pmax;
    if (s_pitch < -pmax) s_pitch = -pmax;
    cp = cos(s_pitch);
    eye[0] = (float)(tgt[0] - d * cp * sin(s_yaw));
    eye[1] = (float)(tgt[1] + d * sin(s_pitch));
    eye[2] = (float)(tgt[2] - d * cp * cos(s_yaw));
    return 1;
}

/* Where the view starts for an eye at share s of u from tgt, moved across
 * by p: ahead of the eye, toward the target (f), and its corner on side
 * (sx, sy) there. */
static void view_corner(const float tgt[3], const double u[3], double s, const double p[3],
                        double ahead, const double f[3], double sx, const double rt[3],
                        double sy, const double up[3], double c[3])
{
    int k;
    for (k = 0; k < 3; k++)
        c[k] = tgt[k] + s * u[k] + p[k] + ahead * f[k] + sx * rt[k] + sy * up[k];
}

/* Whether a surface cuts across the view where it starts, for an eye at
 * share s moved across by p: a line from its middle, on the eye's line, to
 * each corner (i: bit 0 right, bit 1 up). t[i] is how much of that line is
 * open; returns the least. */
static double view_cuts(const float tgt[3], const double u[3], double s, const double p[3],
                        double ahead, const double f[3], double half_w, const double rt[3],
                        double half_h, const double up[3], freecam_line_fn line, void *ctx,
                        double t[4])
{
    double m[3], c[3], least = 1.0;
    int i;
    view_corner(tgt, u, s, p, ahead, f, 0, rt, 0, up, m);
    for (i = 0; i < 4; i++) {
        view_corner(tgt, u, s, p, ahead, f, (i & 1) ? half_w : -half_w, rt,
                    (i & 2) ? half_h : -half_h, up, c);
        t[i] = line(ctx, m, c);
        least = fmin(least, t[i]);
    }
    return least;
}

/* The view, for an eye at eye looking at tgt: u from tgt to the eye and d
 * its length; it faces f, from the eye to the target; left-handed, y up:
 * right of it rt (as auto_step), and up, square to both. 0 when there is
 * none. freecam keeps the pitch short of straight up or down, so right is
 * never nothing. */
static int view_frame(const float tgt[3], const float eye[3], double u[3], double *d,
                      double f[3], double rt[3], double up[3])
{
    double rl;
    int k;
    for (k = 0; k < 3; k++) u[k] = eye[k] - tgt[k];
    *d = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (!(*d > 0)) return 0;
    for (k = 0; k < 3; k++) f[k] = -u[k] / *d;
    rl = sqrt(f[2] * f[2] + f[0] * f[0]);
    if (!(rl > 0)) return 0;
    rt[0] = f[2] / rl; rt[1] = 0; rt[2] = -f[0] / rl;
    up[0] = f[1] * rt[2] - f[2] * rt[1];
    up[1] = f[2] * rt[0] - f[0] * rt[2];
    up[2] = f[0] * rt[1] - f[1] * rt[0];
    return 1;
}

double freecam_clear(const float tgt[3], const float eye[3], double most, double ahead,
                     double half_w, double half_h, freecam_line_fn line, void *ctx,
                     double push[3], int *by)
{
    static const double none[3] = { 0, 0, 0 };
    double a[3], b[3], u[3], f[3], rt[3], up[3], p[3], t[4], d, wl, nearest, s0, s;
    int i, k, n, framed = view_frame(tgt, eye, u, &d, f, rt, up);
    for (k = 0; k < 3; k++) {
        a[k] = tgt[k];
        b[k] = a[k] + most * u[k];
        push[k] = p[k] = 0;
    }
    *by = FREECAM_WALL_CLEAR;
    if (!(d > 0 && most > 0)) return most;
    /* The eye's line, as the game tests it, out to most. */
    s0 = most * line(ctx, a, b);
    if (s0 < most) *by = FREECAM_WALL_EYE;
    if (!(half_w > 0 && half_h > 0 && ahead >= 0 && framed)) return s0;
    /* The eye no nearer the target than where its view starts a view's
     * reach (wl, from the eye to a corner of where the view starts) in
     * front of the target. Nearer, the view starts at or past the player:
     * it shows them from inside, and the game's view of a point so close
     * turns over. That is when the player is right against the wall the
     * eye's line meets: the eye stays this far out, and that wall, which
     * no eye on that side clears, is seen through, as the game's camera
     * would see through it with its eye on the player. */
    wl = sqrt(ahead * ahead + half_w * half_w + half_h * half_h);
    nearest = (ahead + wl) / d;
    if (s0 < nearest) {
        *by = FREECAM_WALL_NEAREST;
        return most < nearest ? most : nearest;
    }
    /* When nothing cuts across the view where it starts, it stands: a pole,
     * a rail or the edge of a wall between the player and the eye that only
     * hides a corner of the picture hides it, as it would the game's, and
     * does not bring the eye in. */
    if (view_cuts(tgt, u, s0, none, ahead, f, half_w, rt, half_h, up, line, ctx, t) >= 1)
        return s0;
    /* Something does. First the eye moves across, away from what cuts it:
     * a wall along the view, the ground under it, the edge of a block. A
     * corner whose line from the middle is open for a share t of it has the
     * surface (1 - t) of its way short of it, so the eye moves back that
     * much from each corner cut. Two corners cut on one side, by a wall
     * along that side, take it twice that, which leaves the corners clear
     * of the wall rather than on it. A unit or two across, at the eye's
     * distance, turns the view a few degrees, where coming in would bring
     * it right up to the player. The line from the target to the middle of
     * where the view starts must be clear there as well: to that, not to
     * the eye, which sits on the wall where the eye's line met one, and
     * which the view does not show. */
    for (n = 0; n < FREECAM_WALL_TRIES; n++) {
        double step[3] = { 0, 0, 0 }, mid[3];
        for (i = 0; i < 4; i++) {
            double sx = (i & 1) ? half_w : -half_w, sy = (i & 2) ? half_h : -half_h;
            if (t[i] < 1)
                for (k = 0; k < 3; k++) step[k] -= (1 - t[i]) * (sx * rt[k] + sy * up[k]);
        }
        if (!(step[0] != 0 || step[1] != 0 || step[2] != 0)) break;   /* cut either side alike */
        for (k = 0; k < 3; k++) {
            p[k] += step[k];
            mid[k] = a[k] + s0 * u[k] + p[k] + ahead * f[k];
        }
        if (line(ctx, a, mid) < 1) break;
        if (view_cuts(tgt, u, s0, p, ahead, f, half_w, rt, half_h, up, line, ctx, t) >= 1) {
            for (k = 0; k < 3; k++) push[k] = p[k];
            *by = FREECAM_WALL_ACROSS;
            return s0;
        }
    }
    /* Moving across does not clear it -- a space narrower than the view, or
     * a wall the move meets -- so the eye comes in until the corners of the
     * view where it starts are clear, each by a line from the target. The
     * player stands in the open, so a wall or the ground beside them is not
     * in the way of a view that starts a unit off its line, as it was when
     * the lines ran beside it from the player. A corner wl from the eye's
     * line whose line is open for a share t of it, with the eye brought in
     * by t, sits (1 - t) wl past where that line stopped; so the eye comes
     * in that much further as well, and the corners are tested again where
     * it is then. */
    for (s = s0, n = 0; n < FREECAM_WALL_TRIES; n++) {
        double open = 1.0, c[3];
        if (!(s >= nearest)) break;
        for (i = 0; i < 4; i++) {
            view_corner(tgt, u, s, none, ahead, f, (i & 1) ? half_w : -half_w, rt,
                        (i & 2) ? half_h : -half_h, up, c);
            open = fmin(open, line(ctx, a, c));
        }
        if (open >= 1) {
            if (n) *by = FREECAM_WALL_CORNER;
            return s;
        }
        s = s * open - (1 - open) * wl / d;
    }
    /* Neither clears it -- a space narrower than the view all the way in --
     * so the eye stays where its own line puts it, as the game's camera
     * would. */
    *by = FREECAM_WALL_NO_ROOM;
    return s0;
}

double freecam_wall(double open)
{
    if (open < s_wall) {                                /* in at once, and wait */
        s_wall = open;
        s_wall_wait = 0;
    } else if (s_wall_wait < FREECAM_WALL_HOLD) {
        s_wall_wait++;
    } else {                                            /* then out by degrees */
        s_wall += (open - s_wall) / FREECAM_WALL_EASE;
        if ((float)s_wall == (float)open) s_wall = open;    /* as near as the eye's floats go */
    }
    return s_wall;
}

void freecam_across(const float tgt[3], const float eye[3], double s, const double need[3],
                    double ahead, double half_w, double half_h, freecam_line_fn line, void *ctx,
                    double out[3])
{
    double u[3], f[3], rt[3], up[3], t[4], mid[3], d;
    int k, moved = s_across[0] != 0 || s_across[1] != 0 || s_across[2] != 0, clear;
    if (!moved) {
        /* Not moved: freecam_clear found the view clear unless it moves it. */
        clear = !(need[0] != 0 || need[1] != 0 || need[2] != 0);
    } else if (!(half_w > 0 && half_h > 0 && ahead >= 0)
            || !view_frame(tgt, eye, u, &d, f, rt, up)) {
        clear = 1;                      /* no view to test: back by degrees */
    } else {
        /* Last frame's move, where the eye is now: the line from the target
         * to the middle of where the view starts open, and nothing across
         * it, as freecam_clear tests a move. */
        for (k = 0; k < 3; k++) mid[k] = tgt[k] + s * u[k] + s_across[k] + ahead * f[k];
        {
            const double a[3] = { tgt[0], tgt[1], tgt[2] };
            clear = line(ctx, a, mid) >= 1
                 && view_cuts(tgt, u, s, s_across, ahead, f, half_w, rt, half_h, up, line, ctx, t) >= 1;
        }
    }
    if (!clear) {                                       /* to it at once, and wait */
        for (k = 0; k < 3; k++) s_across[k] = need[k];
        s_across_wait = 0;
    } else if (s_across_wait < FREECAM_WALL_HOLD) {
        s_across_wait++;
    } else {                                            /* then toward it by degrees */
        double gap = 0, far = 0;
        for (k = 0; k < 3; k++) {
            s_across[k] += (need[k] - s_across[k]) / FREECAM_WALL_EASE;
            gap = fmax(gap, fabs(need[k] - s_across[k]));
            far += (eye[k] - tgt[k]) * (eye[k] - tgt[k]);
        }
        far = s * sqrt(far);                            /* the eye from the target */
        if ((float)(far + gap) == (float)far)           /* as near as the eye's floats go there */
            for (k = 0; k < 3; k++) s_across[k] = need[k];
    }
    for (k = 0; k < 3; k++) out[k] = s_across[k];
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
static int    menu_centre(void)        { return s_cfg.centre; }
static void   menu_centre_pick(int i)  { s_cfg.centre = i; nv_window_pref_set_num("autocentre", i); }
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
        { .menu = XBOX_MENU_CONTROLLER, .kind = XBOX_ITEM_CHOICE, .label = "Auto centre",
          .state = menu_centre, .pick = menu_centre_pick,
          .choices = k_centre_names, .nchoices = FREECAM_CENTRE_LEVELS },
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
