/* Run with: python tests/freecam/run.py
 *
 * The free camera without the game: port/freecam.c as it is, reading its keys
 * through the engine's own settings code (taken from src/nv2a_gl/nv2a_gl.c by
 * run.py), with a camera that looks at the origin from 10 units behind it
 * (-z) and raises its eye by 2. Each case is its own run, as the settings are
 * read once; run.py writes the settings file and checks the log.
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

/* The window's key hook (nv2a_gl.c): the key freecam binds, and what sdl_pump
 * runs for it. */
static const char *s_key;
static void (*s_key_run)(void);
void nv_window_hotkey(const char *name, void (*run)(void))
{
    s_key = name;
    s_key_run = run;
}

/* One frame: a pad read, then the camera. Returns 1 if freecam placed the eye. */
static int frame(short rx, short ry, float eye[3])
{
    freecam_pad(0, &rx, &ry);
    return freecam_orbit(CAM, 3, tgt, seye, 2.0f, 0, eye);
}

static void near(double got, double want, const char *what)
{
    if (fabs(got - want) > 1e-3) {
        fprintf(stderr, "FAIL %s: %.5f, not %.5f\n", what, got, want);
        exit(1);
    }
}

/* The view's heading and the eye's pitch, in degrees, back from the eye. */
static void angles(const float eye[3], double *heading, double *pitch)
{
    double vx = eye[0] - tgt[0], vy = eye[1] - 2.0 - tgt[1], vz = eye[2] - tgt[2];
    double d = sqrt(vx * vx + vy * vy + vz * vz);
    near(d, 10.0, "the game's distance is kept");
    *heading = atan2(-vx, -vz) / deg;
    *pitch = asin(vy / d) / deg;
}

/* From the game's view, one frame with the stick at (rx, ry): how far it
 * turned. Two frame ticks without the follow camera start freecam again from
 * the game's view, as after a cutscene. */
static void turn(short rx, short ry, double *dh, double *dp)
{
    float eye[3];
    freecam_frame();
    freecam_frame();
    assert(frame(0, 0, eye));
    near(eye[0], 0, "first frame: the game's eye x");
    near(eye[1], 2, "first frame: the game's eye y, raised");
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
        assert(!freecam_orbit(CAM, 3, tgt, seye, 2.0f, 1, eye));
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
        assert(freecam_orbit(CAM, 3, tgt, seye, 2.0f, 0, eye));
        angles(eye, &h, &p);
        near(h, 3, "the stick taken from the game moves the camera");
        /* A frame without the follow camera (a cutscene): the stick goes
         * back, and the next follow frame starts from the game's view. */
        freecam_frame();
        freecam_frame();
        rx = 5; freecam_pad(0, &rx, &ry);
        assert(rx == 5);
        assert(freecam_orbit(CAM, 3, tgt, seye, 2.0f, 0, eye));
        near(eye[0], 0, "started again from the game's view");
        /* A second pad: the one pushed counts, and an idle one does not hide it. */
        rx = 0; ry = 0; freecam_pad(0, &rx, &ry);
        rx = 32767; ry = 0; freecam_pad(1, &rx, &ry);
        assert(freecam_orbit(CAM, 3, tgt, seye, 2.0f, 0, eye));
        angles(eye, &h, &p);
        near(h, 3, "the second pad's stick");
        /* That pad unplugged: its stick stops counting. */
        freecam_pad(1, NULL, NULL);
        assert(freecam_orbit(CAM, 3, tgt, seye, 2.0f, 0, eye));
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
        assert(freecam_orbit(CAM, 3, tgt, back, 2.0f, 0, eye));
        angles(eye, &h, &p);
        near(h, 30, "the game's eye moving is no reset");
        assert(freecam_orbit(CAM, 3, tgt, back, 2.0f, 1, eye));
        near(eye[0], 10, "reset: the game's eye x");
        near(eye[1], 2, "reset: the game's eye y, raised");
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
    } else {
        fprintf(stderr, "no case %s\n", c);
        return 2;
    }
    printf("ok %s\n", c);
    return 0;
}
