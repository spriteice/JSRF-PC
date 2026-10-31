/* Run with: python tests/freecam/run.py
 *
 * The free camera without the game: port/freecam.c as it is, reading its keys
 * through the engine's own settings code (taken from src/nv2a_gl/nv2a_gl.c by
 * run.py), with a camera that looks at the origin from 10 units behind it
 * (-z). Each case is its own run, as the settings are read once; run.py
 * writes the settings file and checks the log.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freecam.h"

static const float tgt[3] = { 0, 0, 0 }, seye[3] = { 0, 0, -10 };
static const double deg = 3.14159265358979323846 / 180.0;
#define CAM 0x00401000u

/* The window's key hooks (nv2a_gl.c): the key and the pad's button freecam
 * binds, and what sdl_pump runs for each. */
static const char *s_key, *s_button;
static void (*s_key_run)(void), (*s_button_run)(void);
void nv_window_hotkey(const char *name, void (*run)(void))
{
    s_key = name;
    s_key_run = run;
}
void nv_window_pad_hotkey(const char *name, void (*run)(void))
{
    s_button = name;
    s_button_run = run;
}

/* One frame: a pad read, then the camera's update and its follow camera.
 * Returns 1 if freecam placed the eye. */
static int frame(short rx, short ry, float eye[3])
{
    freecam_pad(0, &rx, &ry);
    freecam_ran(CAM);
    return freecam_orbit(CAM, 3, tgt, seye, 0, eye);
}

static void near(double got, double want, const char *what)
{
    if (fabs(got - want) > 1e-3) {
        fprintf(stderr, "FAIL %s: %.5f, not %.5f\n", what, got, want);
        exit(1);
    }
}

/* A made-up world for freecam_clear: half-spaces (the side of a plane where
 * n.p < c) and boxes, each solid. world_line is the line test: the share of
 * the line from a to b before it enters one, 0 when a is in one. */
static struct { int box; double n[3], c, lo[3], hi[3]; } s_solid[4];
static int s_solids, s_lines;
static void add_plane(double nx, double ny, double nz, double c)
{
    s_solid[s_solids].box = 0;
    s_solid[s_solids].n[0] = nx; s_solid[s_solids].n[1] = ny; s_solid[s_solids].n[2] = nz;
    s_solid[s_solids++].c = c;
}
static void add_box(double x0, double y0, double z0, double x1, double y1, double z1)
{
    s_solid[s_solids].box = 1;
    s_solid[s_solids].lo[0] = x0; s_solid[s_solids].lo[1] = y0; s_solid[s_solids].lo[2] = z0;
    s_solid[s_solids].hi[0] = x1; s_solid[s_solids].hi[1] = y1; s_solid[s_solids++].hi[2] = z1;
}
static double world_line(void *ctx, const double a[3], const double b[3])
{
    double open = 1;
    int i, k;
    (void)ctx;
    s_lines++;
    for (i = 0; i < s_solids; i++) {
        double t0 = 0, t1 = 1;
        if (!s_solid[i].box) {
            double ga = -s_solid[i].c, gb = -s_solid[i].c;
            for (k = 0; k < 3; k++) { ga += s_solid[i].n[k] * a[k]; gb += s_solid[i].n[k] * b[k]; }
            if (ga < 0) return 0;
            if (gb < 0) open = fmin(open, ga / (ga - gb));
            continue;
        }
        for (k = 0; k < 3 && t0 <= t1; k++) {
            double dk = b[k] - a[k], lo = s_solid[i].lo[k], hi = s_solid[i].hi[k];
            if (dk == 0) {
                if (a[k] < lo || a[k] > hi) t0 = 2;
            } else {
                double ta = (lo - a[k]) / dk, tb = (hi - a[k]) / dk;
                t0 = fmax(t0, fmin(ta, tb));
                t1 = fmin(t1, fmax(ta, tb));
            }
        }
        if (t0 <= t1) open = fmin(open, t0);
    }
    return open;
}
/* Whether the view's start, for the eye at share s moved across by p, is
 * outside every solid: its middle and four corners, worked out as
 * freecam_clear does. */
static int view_clear(const float t[3], const float e[3], double s, const double p[3],
                      double ahead, double hw, double hh)
{
    double u[3], f[3], rt[3], up[3], d, rl;
    int i, k, j;
    for (k = 0; k < 3; k++) u[k] = e[k] - t[k];
    d = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (k = 0; k < 3; k++) f[k] = -u[k] / d;
    rl = sqrt(f[2] * f[2] + f[0] * f[0]);
    rt[0] = f[2] / rl; rt[1] = 0; rt[2] = -f[0] / rl;
    up[0] = f[1] * rt[2] - f[2] * rt[1];
    up[1] = f[2] * rt[0] - f[0] * rt[2];
    up[2] = f[0] * rt[1] - f[1] * rt[0];
    for (i = 0; i < 5; i++) {
        /* 4 the middle, 0 to 3 the corners */
        double c[3], sx = i == 4 ? 0 : (i & 1) ? hw : -hw, sy = i == 4 ? 0 : (i & 2) ? hh : -hh;
        for (k = 0; k < 3; k++)
            c[k] = t[k] + s * u[k] + p[k] + ahead * f[k] + sx * rt[k] + sy * up[k];
        for (j = 0; j < s_solids; j++) {
            if (!s_solid[j].box) {
                double g = -s_solid[j].c;
                for (k = 0; k < 3; k++) g += s_solid[j].n[k] * c[k];
                if (g < -1e-9) return 0;
            } else {
                for (k = 0; k < 3 && c[k] >= s_solid[j].lo[k] && c[k] <= s_solid[j].hi[k]; k++) {}
                if (k == 3) return 0;
            }
        }
    }
    return 1;
}

/* The view's heading and the eye's pitch, in degrees, back from the eye,
 * which is on its orbit: as far from the target as the game's, and nothing
 * added to its height. */
static void angles(const float eye[3], double *heading, double *pitch)
{
    double vx = eye[0] - tgt[0], vy = eye[1] - tgt[1], vz = eye[2] - tgt[2];
    double d = sqrt(vx * vx + vy * vy + vz * vz);
    near(d, 10.0, "the game's distance is kept");
    *heading = atan2(-vx, -vz) / deg;
    *pitch = asin(vy / d) / deg;
}

/* From the game's view, one frame with the stick at (rx, ry): how far it
 * turned. A frame where the camera runs without its follow camera (the game
 * has it, as in a cutscene) starts freecam again from the game's view. */
static void turn(short rx, short ry, double *dh, double *dp)
{
    float eye[3];
    freecam_frame();
    freecam_ran(CAM);
    freecam_frame();
    assert(frame(0, 0, eye));
    near(eye[0], 0, "first frame: the game's eye x");
    near(eye[1], 0, "first frame: the game's eye y, not lifted");
    near(eye[2], -10, "first frame: the game's eye z");
    assert(frame(rx, ry, eye));
    angles(eye, dh, dp);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    double h, p;
    float eye[3];
    freecam_start();
    if (!strcmp(c, "off")) {
        /* No key: off, and the game's eye stands, its reset too. */
        assert(!freecam_on());
        assert(!frame(32767, 0, eye));
        assert(!freecam_orbit(CAM, 3, tgt, seye, 1, eye));
    } else if (!strcmp(c, "turn")) {
        /* Full right turns the view right by 0.6 x 5 = 3 degrees; full up
         * tilts it up (the eye goes down) by 3. */
        assert(freecam_on());
        turn(32767, 0, &h, &p);
        near(h, 3, "full right at 5");
        near(p, 0, "no pitch from x");
        turn(0, 32767, &h, &p);
        near(h, 0, "no heading from y");
        near(p, -3, "full up at 5");
    } else if (!strcmp(c, "invert")) {
        turn(32767, 0, &h, &p);
        near(h, -3, "inverted x");
        turn(0, 32767, &h, &p);
        near(p, 3, "inverted y");
    } else if (!strcmp(c, "sensitivity")) {
        /* sensitivity_x=10, sensitivity_y=2.5; half tilt is half the speed
         * past the default deadzone, 0.05. */
        turn(32767, 0, &h, &p);
        near(h, 6, "full right at 10");
        turn(0, -32767, &h, &p);
        near(p, 1.5, "full down at 2.5");
        turn(16384, 0, &h, &p);
        near(h, 6.0 * (16384.0 / 32767 - 0.05) / 0.95, "half right at 10");
    } else if (!strcmp(c, "deadzone")) {
        /* deadzone=0.5, radial and rescaled. */
        turn(13107, 0, &h, &p);                 /* 0.4: inside */
        near(h, 0, "inside the deadzone");
        turn(24575, 0, &h, &p);                 /* 0.75: half way past it */
        near(h, 0.5 * 3, "half way past the deadzone");
        turn(23170, 23170, &h, &p);             /* full tilt, diagonally */
        near(sqrt(h * h + p * p), 3, "full tilt on a diagonal");
        turn(16000, 16000, &h, &p);             /* 0.49 each, 0.69 radially */
        near(h, -p, "radial: a diagonal keeps its direction");
        near(h, (16000 * sqrt(2.0) / 32767 - 0.5) / 0.5 * sqrt(0.5) * 3,
             "radial: a diagonal past the deadzone moves, each axis inside it");
    } else if (!strcmp(c, "deadzone_all")) {
        /* deadzone=1: the stick does nothing. */
        turn(32767, 32767, &h, &p);
        near(h, 0, "deadzone 1, x"); near(p, 0, "deadzone 1, y");
    } else if (!strcmp(c, "fallback")) {
        /* Every key bad: the defaults, 5 and 0.05 and no invert. */
        turn(32767, 0, &h, &p);
        near(h, 3, "default sensitivity, not inverted");
        turn(1500, 0, &h, &p);                  /* 0.046: inside 0.05 */
        near(h, 0, "default deadzone");
    } else if (!strcmp(c, "pitch")) {
        int i;
        for (i = 0; i < 100; i++) assert(frame(0, 32767, eye));
        angles(eye, &h, &p);
        near(p, -80, "pitch held at the limit below");
        for (i = 0; i < 100; i++) assert(frame(0, -32767, eye));
        angles(eye, &h, &p);
        near(p, 80, "pitch held at the limit above");
    } else if (!strcmp(c, "steer")) {
        short rx = 32767, ry = 32767;
        /* Until the follow camera has run, the stick is the game's. */
        freecam_frame();
        freecam_pad(0, &rx, &ry);
        assert(rx == 32767 && ry == 32767);
        assert(frame(0, 0, eye));
        freecam_frame();
        rx = 32767; ry = -1;
        freecam_pad(0, &rx, &ry);
        assert(rx == 0 && ry == 0);             /* now it is freecam's */
        assert(freecam_orbit(CAM, 3, tgt, seye, 0, eye));
        angles(eye, &h, &p);
        near(h, 3, "the stick taken from the game moves the camera");
        /* A frame without the follow camera (a cutscene): the stick goes
         * back, and the next follow frame starts from the game's view. */
        freecam_frame();
        freecam_ran(CAM);
        freecam_frame();
        rx = 5; freecam_pad(0, &rx, &ry);
        assert(rx == 5);
        assert(freecam_orbit(CAM, 3, tgt, seye, 0, eye));
        near(eye[0], 0, "started again from the game's view");
        /* A second pad: the one pushed counts, and an idle one does not hide it. */
        rx = 0; ry = 0; freecam_pad(0, &rx, &ry);
        rx = 32767; ry = 0; freecam_pad(1, &rx, &ry);
        assert(freecam_orbit(CAM, 3, tgt, seye, 0, eye));
        angles(eye, &h, &p);
        near(h, 3, "the second pad's stick");
        /* That pad unplugged: its stick stops counting. */
        freecam_pad(1, NULL, NULL);
        assert(freecam_orbit(CAM, 3, tgt, seye, 0, eye));
        angles(eye, &h, &p);
        near(h, 3, "an unplugged pad stops turning the camera");
    } else if (!strcmp(c, "reset")) {
        /* The game puts its eye back behind the player (the L trigger), here
         * on +x: freecam starts again from it, wherever the stick had turned
         * the view, and turns from there. */
        static const float back[3] = { 10, 0, 0 };
        short rx = 0, ry = 0;
        int i;
        for (i = 0; i < 10; i++) assert(frame(32767, 0, eye));
        angles(eye, &h, &p);
        near(h, 30, "turned before the reset");
        freecam_pad(0, &rx, &ry);
        assert(freecam_orbit(CAM, 3, tgt, back, 0, eye));
        angles(eye, &h, &p);
        near(h, 30, "the game's eye moving is no reset");
        assert(freecam_orbit(CAM, 3, tgt, back, 1, eye));
        near(eye[0], 10, "reset: the game's eye x");
        near(eye[1], 0, "reset: the game's eye y, not lifted");
        near(eye[2], 0, "reset: the game's eye z");
        assert(frame(32767, 0, eye));
        angles(eye, &h, &p);
        near(h, -87, "turning on from the game's view");
    } else if (!strcmp(c, "toggle")) {
        /* freecam=0 in the file: F8 turns it on and keeps that there,
         * leaving the other lines as they were, and off again. */
        double v = -1;
        assert(s_key && !strcmp(s_key, "F8") && s_key_run);
        assert(!frame(0, 0, eye) && !freecam_on());
        s_key_run();
        assert(frame(0, 0, eye) && freecam_on());
        assert(nv_window_pref_num("freecam", &v) == 1 && v == 1);
        assert(nv_window_pref_num("sensitivity_x", &v) == 1 && v == 7);
        s_key_run();
        assert(!frame(0, 0, eye) && !freecam_on());
        assert(nv_window_pref_num("freecam", &v) == 1 && v == 0);
    } else if (!strcmp(c, "r3")) {
        /* R3 is bound, with no setting: it turns freecam on and keeps that,
         * as F8 does, and off. */
        double v = -1;
        assert(s_button && !strcmp(s_button, "rightstick") && s_button_run);
        assert(!freecam_on());
        s_button_run();
        assert(freecam_on() && frame(0, 0, eye));
        assert(nv_window_pref_num("freecam", &v) == 1 && v == 1);
        s_button_run();
        assert(!freecam_on());
        assert(nv_window_pref_num("freecam", &v) == 1 && v == 0);
    } else if (!strcmp(c, "held")) {
        /* The view turned, then frames where the camera does not run at all
         * (the game paused): the view stays, and the stick is the game's
         * meanwhile. A frame where it runs in a mode of its own starts it
         * again from the game's view. */
        short rx = 32767, ry = 0;
        int i;
        for (i = 0; i < 10; i++) { freecam_frame(); assert(frame(32767, 0, eye)); }
        angles(eye, &h, &p);
        near(h, 30, "turned before the pause");
        for (i = 0; i < 100; i++) freecam_frame();
        freecam_pad(0, &rx, &ry);
        assert(rx == 32767);
        freecam_frame();
        assert(frame(0, 0, eye));
        angles(eye, &h, &p);
        near(h, 30, "after the pause, the view is where it was");
        freecam_frame();
        freecam_ran(CAM);
        freecam_frame();
        assert(frame(0, 0, eye));
        near(eye[0], 0, "after a scene, the game's view again");
    } else if (!strncmp(c, "centre_", 7)) {
        /* centre_<wait>_<ease> (0 0: off). Turned 30 degrees, then the stick
         * let go. Off, the view stays; on, it stays for <wait> frames (the
         * first of them the turn's last) and then eases back, 1/<ease> of
         * what is left a frame, so that pushing the stick again waits once
         * more before easing. */
        int i, wait = 0;
        double ease = 0, k, eased;
        assert(sscanf(c + 7, "%d_%lf", &wait, &ease) == 2);
        k = ease ? 1 - 1 / ease : 1;
        if (!wait) wait = 60;                   /* off: the same frames */
        for (i = 0; i < 10; i++) assert(frame(32767, 0, eye));
        for (i = 0; i < wait - 1; i++) assert(frame(0, 0, eye));
        angles(eye, &h, &p);
        near(h, 30, "let go: not yet");
        assert(frame(0, 0, eye));
        angles(eye, &h, &p);
        near(h, 30 * k, "the first frame of easing back");
        for (i = 0; i < 300; i++) assert(frame(0, 0, eye));
        angles(eye, &h, &p);
        eased = 30 * pow(k, 301);
        near(h, eased, "eased back behind the player");
        for (i = 0; i < 10; i++) assert(frame(32767, 0, eye));
        for (i = 0; i < wait - 1; i++) assert(frame(0, 0, eye));
        angles(eye, &h, &p);
        near(h, eased + 30, "a push waits again");
    } else if (!strcmp(c, "wall")) {
        /* After the wall test: in at once; then, once nothing has brought it
         * in for 20 frames (FREECAM_WALL_HOLD), back out a tenth of the gap
         * a frame, all the way, never past what is clear, and all the way out
         * again when freecam starts again. */
        static const double opens[] = { 1, 0.9, 0.2, 1, 0.5, 0.6, 1, 0.05, 1, 1 };
        double w = 1;
        int i;
        assert(frame(0, 0, eye));
        near(freecam_wall(1), 1, "clear: the whole distance");
        near(freecam_wall(0.4), 0.4, "a wall: in at once");
        for (i = 0; i < 20; i++) near(freecam_wall(1), 0.4, "clear again: it waits");
        near(freecam_wall(1), 0.46, "then a tenth of the way back out");
        near(freecam_wall(1), 0.514, "and a tenth of what is left");
        near(freecam_wall(0.3), 0.3, "another wall: in at once");
        for (i = 0; i < 20; i++) near(freecam_wall(0.35), 0.3, "clear a little further: it waits");
        near(freecam_wall(0.35), 0.305, "then out by degrees");
        near(freecam_wall(0.32), 0.3065, "less clear, but more than where it is: on out");
        for (i = 0; i < 11; i++) w = freecam_wall(1);
        near(w, 1 - 0.6935 * pow(0.9, 11), "two thirds of the way out in 11 frames");
        for (i = 0; i < 400 && w < 1; i++) w = freecam_wall(1);
        assert(w == 1);
        for (i = 0; i < 100; i++) {
            double open = opens[i % (sizeof opens / sizeof opens[0])];
            w = freecam_wall(open);
            if (w > open) {
                fprintf(stderr, "FAIL frame %d: the eye at %.5f, past the clear %.5f\n", i, w, open);
                return 1;
            }
        }
        assert(freecam_wall(0.01) == 0.01);
        assert(freecam_orbit(CAM, 3, tgt, seye, 1, eye));
        near(freecam_wall(1), 1, "freecam started again: the whole distance, and no wait");
    } else if (!strcmp(c, "across")) {
        /* After freecam_clear, how far across: to what it moved the eye at
         * once; once a wall it moved away from ends, last frame's for 20
         * frames, then back a tenth of the way a frame, all the way; to it
         * at once again when the wall comes back; and nothing when freecam
         * starts again. */
        const float t3[3] = { 0, 3, 0 }, e3[3] = { 0, 3, -10 };
        double need[3], out[3], first;
        int by, i;
        assert(frame(0, 0, eye));
        s_solids = 0;
        add_plane(-1, 0, 0, -0.5);              /* a wall along the view, half a unit to the right */
        freecam_clear(t3, e3, 1.0, 1.0, 1.0, 0.57, world_line, NULL, need, &by);
        assert(by == FREECAM_WALL_ACROSS && need[0] < 0);
        freecam_across(t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        near(out[0], need[0], "a wall: across at once");
        first = out[0];
        s_solids = 0;                           /* skated past its end */
        freecam_clear(t3, e3, 1.0, 1.0, 1.0, 0.57, world_line, NULL, need, &by);
        assert(by == FREECAM_WALL_CLEAR && need[0] == 0);
        for (i = 0; i < 20; i++) {
            freecam_across(t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
            near(out[0], first, "the wall ends: it waits");
        }
        freecam_across(t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        near(out[0], first * 0.9, "then a tenth of the way back");
        for (i = 0; i < 10; i++) freecam_across(t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        near(out[0], first * pow(0.9, 11), "two thirds of the way back in 11 frames");
        add_plane(-1, 0, 0, -0.5);              /* the wall again */
        freecam_clear(t3, e3, 1.0, 1.0, 1.0, 0.57, world_line, NULL, need, &by);
        freecam_across(t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        near(out[0], first, "the wall again, the view cut where it was: across at once");
        assert(view_clear(t3, e3, 1.0, out, 1.0, 1.0, 0.57));
        s_solids = 0;
        need[0] = need[1] = need[2] = 0;
        for (i = 0; i < 400 && out[0] != 0; i++)
            freecam_across(t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        assert(out[0] == 0 && out[1] == 0 && out[2] == 0);
        add_plane(-1, 0, 0, -0.5);
        freecam_clear(t3, e3, 1.0, 1.0, 1.0, 0.57, world_line, NULL, need, &by);
        freecam_across(t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        assert(freecam_orbit(CAM, 3, tgt, seye, 1, eye));
        s_solids = 0;
        need[0] = need[1] = need[2] = 0;
        freecam_across(t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        assert(out[0] == 0 && out[1] == 0 && out[2] == 0);
    } else if (!strncmp(c, "clear_", 6)) {
        /* freecam_clear against a made-up world: the player at (0, 3, 0),
         * the eye 10 units behind, the view starting 1 ahead of it, 1 by
         * 0.57 either side (about what the game's is at 16:9). */
        static const double none[3] = { 0, 0, 0 };
        const double wl = sqrt(1.0 + 1.0 + 0.57 * 0.57), nearest = (1.0 + wl) / 10;
        const float t3[3] = { 0, 3, 0 };
        float e3[3] = { 0, 3, -10 };
        double got, push[3], moved;
        int by, want_by;
        s_solids = s_lines = 0;
        if (!strcmp(c, "clear_open")) {
            want_by = FREECAM_WALL_CLEAR;
        } else if (!strcmp(c, "clear_back")) {
            add_plane(0, 0, 1, -6);             /* everything behind z = -6 */
            want_by = FREECAM_WALL_EYE;
        } else if (!strcmp(c, "clear_floor")) {
            add_plane(0, 1, 0, 0);              /* the ground, y = 0, the eye just above it */
            e3[1] = 0.2f;
            want_by = FREECAM_WALL_ACROSS;
        } else if (!strcmp(c, "clear_room_corner")) {
            add_box(0.5, -10, -20, 10, 10, -7); /* behind the eye and off to its right */
            want_by = FREECAM_WALL_ACROSS;
        } else if (!strcmp(c, "clear_alongside")) {
            add_plane(-1, 0, 0, -0.5);          /* a wall along the view, half a unit to the right */
            want_by = FREECAM_WALL_ACROSS;
        } else if (!strcmp(c, "clear_block_edge")) {
            add_box(-10, -10, -20, -0.3, 10, -8); /* a block's edge just past the eye, on the left */
            want_by = FREECAM_WALL_ACROSS;
        } else if (!strcmp(c, "clear_corridor")) {
            add_plane(-1, 0, 0, -2);            /* a wall 2 to the right, */
            e3[0] = 3.42f; e3[2] = -9.4f;       /* the eye turned 20 degrees toward it */
            want_by = FREECAM_WALL_ACROSS;
        } else if (!strcmp(c, "clear_beside_player")) {
            add_box(0.3, -10, -3, 5, 10, 3);    /* a block right beside the player, ending 3 behind */
            want_by = FREECAM_WALL_CLEAR;
        } else if (!strcmp(c, "clear_pole")) {
            add_box(0.3, -10, -5, 0.6, 10, -4.5); /* a pole between the player and a corner */
            want_by = FREECAM_WALL_CLEAR;
        } else if (!strcmp(c, "clear_gap_behind")) {
            add_box(0.5, -10, -30, 10, 10, -7); /* the eye in a gap narrower than the view, */
            add_box(-10, -10, -30, -0.5, 10, -7); /* which ends 7 behind the player */
            want_by = FREECAM_WALL_CORNER;
        } else if (!strcmp(c, "clear_narrow")) {
            add_plane(-1, 0, 0, -0.6);          /* a gap narrower than the view, all the way */
            add_plane(1, 0, 0, -0.6);
            want_by = FREECAM_WALL_NO_ROOM;
        } else if (!strcmp(c, "clear_hugging")) {
            add_plane(-1, 0, 0, -0.3);          /* the player right against a wall, */
            e3[0] = 8.66f; e3[2] = -5;          /* the eye turned 60 degrees into it */
            want_by = FREECAM_WALL_NEAREST;
        } else {
            fprintf(stderr, "no case %s\n", c);
            return 2;
        }
        got = freecam_clear(t3, e3, 1.0, 1.0, 1.0, 0.57, world_line, NULL, push, &by);
        moved = sqrt(push[0] * push[0] + push[1] * push[1] + push[2] * push[2]);
        fprintf(stderr, "%s: share %.4f, %.2f across, by %d, %d lines\n", c, got, moved, by, s_lines);
        if (by != want_by) {
            fprintf(stderr, "FAIL %s: by %d, not %d\n", c, by, want_by);
            return 1;
        }
        if (by != FREECAM_WALL_ACROSS) assert(moved == 0);
        if (!strcmp(c, "clear_open") || !strcmp(c, "clear_beside_player") || !strcmp(c, "clear_pole")) {
            /* Nothing that cuts across where the view starts: the eye stays
             * out, and only the eye's line and the four lines across the
             * view are tested. */
            near(got, 1, "the view is not cut: the eye stays out");
            assert(view_clear(t3, e3, got, push, 1.0, 1.0, 0.57));
            assert(s_lines == 5);
            if (!strcmp(c, "clear_pole")) {
                /* The line from the target to the view's top right corner,
                 * which the fourth build's corner lines went by, meets the
                 * pole. */
                const double a[3] = { 0, 3, 0 }, b[3] = { 1, 3.57, -9 };
                assert(world_line(NULL, a, b) < 1);
            }
        } else if (!strcmp(c, "clear_back")) {
            near(got, 0.6, "a wall behind the eye: in to it, as the game's line test");
        } else if (by == FREECAM_WALL_ACROSS) {
            /* The eye stays out, moved across by less than the view's reach
             * for each corner, and the whole start of the view is out of
             * the ground, the block or the wall there; where it was, it
             * was not. Before, these brought the eye in, the wall along
             * the view right in to the player. */
            if (!strcmp(c, "clear_corridor"))   /* on the wall, where the eye's line meets it */
                near(got, 2 / 3.42, "moved across: the eye stays where its line puts it");
            else
                near(got, 1, "moved across: the eye stays out");
            assert(moved > 0 && moved <= 4 * wl);
            assert(view_clear(t3, e3, got, push, 1.0, 1.0, 0.57));
            assert(!view_clear(t3, e3, got, none, 1.0, 1.0, 0.57));
        } else if (by == FREECAM_WALL_CORNER) {
            /* Moving across clears neither side of a gap narrower than the
             * view: the eye comes in, out of the gap, but not right in. */
            assert(got > nearest && got < 1);
            assert(view_clear(t3, e3, got, none, 1.0, 1.0, 0.57));
            assert(!view_clear(t3, e3, 1.0, none, 1.0, 1.0, 0.57));
        } else if (by == FREECAM_WALL_NO_ROOM) {
            /* Neither clears a gap narrower than the view all the way in:
             * the eye's line alone, as in the game. */
            near(got, 1, "neither clears it: the eye's line alone");
        } else {
            /* The eye's line meets the wall 0.35 from the target: the eye
             * stays where its view starts in front of the player. */
            near(got, nearest, "as near as the eye goes");
        }
        /* And the held eye's view, tested where it is (freecam_clear up to
         * it), with the eye moved across there. */
        if (!strcmp(c, "clear_floor")) {
            double held = freecam_clear(t3, e3, 0.6, 1.0, 1.0, 0.57, world_line, NULL, push, &by);
            assert(held <= 0.6 && view_clear(t3, e3, held, push, 1.0, 1.0, 0.57));
        }
    } else {
        fprintf(stderr, "no case %s\n", c);
        return 2;
    }
    printf("ok %s\n", c);
    return 0;
}
