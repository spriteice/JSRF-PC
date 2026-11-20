/* Run with: python tests/freecam/run.py
 *
 * The free camera without the game: port/freecam.c as it is, reading its keys
 * through the engine's own settings code (taken from src/nv2a_gl/nv2a_gl.c by
 * run.py), with a camera that looks at the origin from 10 units behind it
 * (-z), the window's mouse given by hand, the Input page's rows caught for
 * their submenu and their File > Presets values, and input playback's hooks
 * standing in for a recording's switch and tuning and its mouse. Each case is
 * its own run, as the settings are read once; run.py writes the settings file
 * and checks the log.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freecam.h"
#include "input_remap.h"
/* On Windows input_remap.h brings windows.h, whose near is a macro. */
#undef near
#include "menu.h"

static const float tgt[3] = { 0, 0, 0 }, seye[3] = { 0, 0, -10 };
static const double deg = 3.14159265358979323846 / 180.0;
#define CAM 0x00401000u

/* The window's key hook (nv2a_gl.c) and the controller layout's (src/input/
 * input_remap.c): the key and the pad's control freecam binds, and what
 * sdl_pump runs for each. */
static const char *s_key;
static int s_control = -1;
static void (*s_key_run)(void), (*s_button_run)(void);
void nv_window_hotkey(const char *name, void (*run)(void))
{
    s_key = name;
    s_key_run = run;
}
void input_remap_pad_hotkey(int control, void (*run)(void))
{
    s_control = control;
    s_button_run = run;
}

/* The window's mouse look (nv2a_gl.c): who asks whether the moves are
 * wanted, and the moves, given by mouse() and taken whole, as the window's
 * counts are, right and down. */
static int (*s_mouse_want)(void);
static int s_mouse_dx, s_mouse_dy;
void nv_window_mouse_look(int (*want)(void))
{
    s_mouse_want = want;
}
int nv_window_mouse_take(int *dx, int *dy)
{
    *dx = s_mouse_dx;
    *dy = s_mouse_dy;
    s_mouse_dx = s_mouse_dy = 0;
    return 1;
}
static void mouse(int dx, int dy)
{
    s_mouse_dx += dx;
    s_mouse_dy += dy;
}

/* Input playback (port/playback.c): while set, a recording's switch and
 * tuning in place of the player's; and its mouse, right and up, in place of
 * the player's, what freecam gave it kept. */
static const FREECAM_SETTINGS *s_played;
static const int *s_played_mouse;
static int s_given[2];
int playback_freecam(FREECAM_SETTINGS *fc)
{
    if (!s_played) return 0;
    *fc = *s_played;
    return 1;
}
int playback_mouse(int *dx, int *dy)
{
    s_given[0] = *dx;
    s_given[1] = *dy;
    if (!s_played_mouse) return 0;
    *dx = s_played_mouse[0];
    *dy = s_played_mouse[1];
    return 1;
}

/* The menu's xbox_MenuAdd: the Input page's rows, caught. */
#define ROWS_MOST 16
static XBOX_MENU_ITEM s_rows[ROWS_MOST];
static int s_nrows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    while (count-- > 0 && s_nrows < ROWS_MOST) s_rows[s_nrows++] = *items++;
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

/* One frame with the mouse moved (dx, dy) since the last, in its counts,
 * right and down, as the window gives them: the game's frame takes the move,
 * and the follow camera turns by it with the stick at (rx, ry). */
static int mframe(short rx, short ry, int dx, int dy, float eye[3])
{
    mouse(dx, dy);
    freecam_frame();
    return frame(rx, ry, eye);
}

/* As turn, with the mouse moved (dx, dy) as well as the stick. */
static void mturn(short rx, short ry, int dx, int dy, double *dh, double *dp)
{
    float eye[3];
    freecam_frame();
    freecam_ran(CAM);
    freecam_frame();
    assert(frame(0, 0, eye));
    near(eye[0], 0, "first frame: the game's eye x");
    assert(mframe(rx, ry, dx, dy, eye));
    angles(eye, dh, dp);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    double h, p;
    float eye[3];
    freecam_start();
    if (!strcmp(c, "off")) {
        /* The Free camera submenu, with its hint, first; then its rows in
         * it, in their order. */
        static const char *const labels[] = { "Enable free camera", "Auto centre", "Invert X",
                                              "Invert Y", "Sensitivity X", "Sensitivity Y", "Deadzone",
                                              "Mouse look", "Mouse sensitivity" };
        int i, p;
        assert(s_nrows == 10);
        assert(s_rows[0].menu == XBOX_MENU_INPUT && s_rows[0].kind == XBOX_ITEM_GROUP);
        assert(!strcmp(s_rows[0].label, XBOX_GROUP_FREECAM) && !strcmp(s_rows[0].label, "Free camera"));
        assert(s_rows[0].hint && !strcmp(s_rows[0].hint, "Free camera toggle and settings."));
        for (i = 1; i < s_nrows; i++) {
            assert(s_rows[i].menu == XBOX_MENU_INPUT && !strcmp(s_rows[i].label, labels[i - 1]));
            assert(s_rows[i].group && !strcmp(s_rows[i].group, XBOX_GROUP_FREECAM) && !s_rows[i].under);
            assert(s_rows[i].hint && strlen(s_rows[i].hint) <= MENU_HINT_LONGEST);
        }
        /* File > Presets: Enable free camera off in Xbox mode, and JSRF-PC
         * default's and Max quality's what a start with no key gives; the
         * rest of the page, how the free camera handles, what a start with
         * no key gives in every preset. */
        assert(s_rows[1].presets == XBOX_PRESETS_SET && !s_rows[1].keep);
        assert(s_rows[1].preset[XBOX_PRESET_XBOX] == 0);
        assert(s_rows[1].preset[XBOX_PRESET_JSRF_PC] == freecam_on());
        assert(s_rows[1].preset[XBOX_PRESET_MAX] == freecam_on());
        for (i = 2; i < s_nrows; i++) {
            const XBOX_MENU_ITEM *r = &s_rows[i];
            double now = r->kind == XBOX_ITEM_NUMBER ? r->value() : r->state();
            assert(r->presets == XBOX_PRESETS_SET && !r->keep);
            for (p = 0; p < XBOX_PRESETS; p++) assert(fabs(r->preset[p] - now) < 1e-9);
        }
        /* No key: off, and the game's eye stands, its reset too. The window
         * is asked for the mouse, and told it isn't wanted while freecam is
         * off. */
        assert(!freecam_on());
        assert(!frame(32767, 0, eye));
        assert(!freecam_orbit(CAM, 3, tgt, seye, 1, eye));
        assert(s_mouse_want && !s_mouse_want());
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
         * past the default deadzone, 0.15. */
        turn(32767, 0, &h, &p);
        near(h, 6, "full right at 10");
        turn(0, -32767, &h, &p);
        near(p, 1.5, "full down at 2.5");
        turn(16384, 0, &h, &p);
        near(h, 6.0 * (16384.0 / 32767 - 0.15) / 0.85, "half right at 10");
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
        /* Every key bad: the defaults, 5 and 0.15 and no invert. */
        turn(32767, 0, &h, &p);
        near(h, 3, "default sensitivity, not inverted");
        turn(4500, 0, &h, &p);                  /* 0.137: inside 0.15 */
        near(h, 0, "default deadzone");
        turn(6554, 0, &h, &p);                  /* 0.2: past it */
        near(h, 3 * (6554.0 / 32767 - 0.15) / 0.85, "past the default deadzone");
    } else if (!strcmp(c, "pitch")) {
        int i;
        for (i = 0; i < 100; i++) assert(frame(0, 32767, eye));
        angles(eye, &h, &p);
        near(p, -80, "pitch held at the limit below");
        for (i = 0; i < 100; i++) assert(frame(0, -32767, eye));
        angles(eye, &h, &p);
        near(p, 80, "pitch held at the limit above");
    } else if (!strcmp(c, "mouse")) {
        /* No mouse keys: Mouse look on at 5, 0.1 degrees a count. Right
         * turns the view right; up (the window's y going down) tilts it up,
         * as the stick's up does; the two add to the stick's. What freecam
         * turned by is what input playback was given, right and up. Wanted
         * while freecam is on, and not once F8 turns it off, from the next
         * frame. */
        assert(s_mouse_want && s_mouse_want());
        mturn(0, 0, 50, 0, &h, &p);
        near(h, 5, "the mouse 50 right at 5");
        near(p, 0, "no pitch from the mouse's x");
        assert(s_given[0] == 50 && s_given[1] == 0);
        mturn(0, 0, 0, -30, &h, &p);
        near(h, 0, "no heading from the mouse's y");
        near(p, -3, "the mouse 30 up at 5");
        assert(s_given[0] == 0 && s_given[1] == 30);
        mturn(32767, 0, 50, 0, &h, &p);
        near(h, 8, "the stick's 3 and the mouse's 5 together");
        mturn(32767, 0, -30, 0, &h, &p);
        near(h, 0, "the mouse back against the stick");
        /* A second follow camera run in the frame turns by the stick again,
         * as the stick is per run, and by the mouse's move not again. */
        assert(freecam_orbit(CAM, 3, tgt, seye, 0, eye));
        angles(eye, &h, &p);
        near(h, 3, "the frame's move taken once");
        s_key_run();
        assert(s_mouse_want());
        freecam_frame();
        assert(!freecam_on() && !s_mouse_want());
    } else if (!strcmp(c, "mouse_invert")) {
        mturn(0, 0, 50, 0, &h, &p);
        near(h, -5, "the mouse's x inverted");
        mturn(0, 0, 0, -30, &h, &p);
        near(p, 3, "the mouse's y inverted");
    } else if (!strcmp(c, "mouse_sensitivity")) {
        /* mouse_sensitivity=10: 0.2 degrees a count; the stick's keeps 5. */
        mturn(0, 0, 50, 0, &h, &p);
        near(h, 10, "the mouse 50 right at 10");
        mturn(32767, 0, 0, 0, &h, &p);
        near(h, 3, "the stick's own sensitivity");
    } else if (!strcmp(c, "mouse_fallback")) {
        /* mouse_sensitivity=11 and mouse=abc: the defaults, on at 5. */
        assert(s_mouse_want && s_mouse_want());
        mturn(0, 0, 50, 0, &h, &p);
        near(h, 5, "the default mouse sensitivity");
    } else if (!strcmp(c, "mouse_off")) {
        /* mouse=0: not wanted, and moves given anyway turn nothing, nor
         * reach input playback; the menu's Mouse look row turns it on, and
         * keeps that, from the next frame. */
        double v = -1;
        assert(freecam_on() && s_mouse_want && !s_mouse_want());
        mturn(0, 0, 50, -30, &h, &p);
        near(h, 0, "Mouse look off: no heading");
        near(p, 0, "Mouse look off: no pitch");
        assert(s_given[0] == 0 && s_given[1] == 0);
        s_rows[8].run();
        assert(s_rows[8].state() && !s_mouse_want());
        assert(nv_window_pref_num("mouse", &v) == 1 && v == 1);
        mturn(0, 0, 50, 0, &h, &p);
        assert(s_mouse_want());
        near(h, 5, "Mouse look on from the menu");
        s_rows[9].set(2);
        assert(s_rows[9].value() == 2);
        assert(nv_window_pref_num("mouse_sensitivity", &v) == 1 && v == 2);
        mturn(0, 0, 50, 0, &h, &p);
        near(h, 2, "Mouse sensitivity 2 from the menu");
    } else if (!strcmp(c, "mouse_dropped")) {
        /* Moves in frames freecam doesn't steer are dropped: while the
         * follow camera doesn't run (paused), and in a scene; the view is
         * where it was, or the game's, and only what comes after turns it. */
        int i;
        for (i = 0; i < 10; i++) { freecam_frame(); assert(frame(32767, 0, eye)); }
        for (i = 0; i < 100; i++) { mouse(10, 10); freecam_frame(); }
        assert(s_given[0] == 0 && s_given[1] == 0);
        assert(mframe(0, 0, 10, 10, eye));     /* the first frame back: not steering yet */
        angles(eye, &h, &p);
        near(h, 30, "after the pause: the view kept");
        near(p, 0, "after the pause: no pitch from the dropped moves");
        assert(mframe(0, 0, 10, 0, eye));
        angles(eye, &h, &p);
        near(h, 31, "after the pause, a move turns it");
        mouse(50, 50);
        freecam_frame();
        freecam_ran(CAM);                       /* a scene's frame: the camera runs without the follow camera */
        assert(mframe(0, 0, 50, 50, eye));
        near(eye[0], 0, "after a scene, the game's view, with nothing from its moves");
        near(eye[1], 0, "after a scene, no pitch from its moves");
    } else if (!strcmp(c, "mouse_centre")) {
        /* autocentre=3 (High: 60 frames, then 1/30): a move of the mouse
         * counts as a push of the stick, so the view waits again. */
        int i;
        double k = 1 - 1.0 / 30;
        for (i = 0; i < 10; i++) assert(frame(32767, 0, eye));
        for (i = 0; i < 59; i++) assert(frame(0, 0, eye));
        assert(mframe(0, 0, 10, 0, eye));
        angles(eye, &h, &p);
        near(h, 31, "the mouse's degree, and no easing");
        for (i = 0; i < 59; i++) assert(frame(0, 0, eye));
        angles(eye, &h, &p);
        near(h, 31, "waiting again after the mouse");
        assert(frame(0, 0, eye));
        angles(eye, &h, &p);
        near(h, 31 * k, "then easing back");
    } else if (!strcmp(c, "mouse_pitch")) {
        /* The mouse is held at the pitch limit too. */
        int i;
        freecam_frame();
        assert(frame(0, 0, eye));
        for (i = 0; i < 100; i++) assert(mframe(0, 0, 0, -100, eye));
        angles(eye, &h, &p);
        near(p, -80, "the mouse held at the limit below");
        for (i = 0; i < 100; i++) assert(mframe(0, 0, 0, 100, eye));
        angles(eye, &h, &p);
        near(p, 80, "the mouse held at the limit above");
    } else if (!strcmp(c, "mouse_played")) {
        /* mouse=0, the player's: a recording's switch and tuning with Mouse
         * look on at 10 in their place, and its mouse in place of the
         * player's, which input playback is still given: the recording's 20
         * counts right turn the view 4 degrees, and the player's 50 don't.
         * The window is asked for the mouse while the recording's are in
         * place. Once it stops, the player's again: Mouse look off. */
        static const FREECAM_SETTINGS rec = { 1, 0, 0, 0, 5, 5, 0.15, 1, 10 };
        static const int moved[2] = { 20, 0 };
        assert(freecam_on() && !s_mouse_want());
        s_played = &rec;
        s_played_mouse = moved;
        mturn(0, 0, 50, 0, &h, &p);
        assert(s_mouse_want());
        assert(s_given[0] == 50 && s_given[1] == 0);
        near(h, 4, "the recording's mouse, at its sensitivity");
        assert(!s_rows[8].state() && s_rows[9].value() == 5);
        s_played = NULL;
        s_played_mouse = NULL;
        mturn(0, 0, 50, 0, &h, &p);
        assert(!s_mouse_want());
        near(h, 0, "the player's again: Mouse look off");
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
         * the view, and turns from there. It says it started again, there
         * and on the camera's first frame, so what the eye kept after walls
         * starts again too (camera_walls.h); else it goes on. */
        static const float back[3] = { 10, 0, 0 };
        short rx = 0, ry = 0;
        int i;
        assert(frame(32767, 0, eye) == FREECAM_STARTED);
        for (i = 1; i < 10; i++) assert(frame(32767, 0, eye) == FREECAM_PLACED);
        angles(eye, &h, &p);
        near(h, 30, "turned before the reset");
        freecam_pad(0, &rx, &ry);
        assert(freecam_orbit(CAM, 3, tgt, back, 0, eye) == FREECAM_PLACED);
        angles(eye, &h, &p);
        near(h, 30, "the game's eye moving is no reset");
        assert(freecam_orbit(CAM, 3, tgt, back, 1, eye) == FREECAM_STARTED);
        near(eye[0], 10, "reset: the game's eye x");
        near(eye[1], 0, "reset: the game's eye y, not lifted");
        near(eye[2], 0, "reset: the game's eye z");
        assert(frame(32767, 0, eye) == FREECAM_PLACED);
        angles(eye, &h, &p);
        near(h, -87, "turning on from the game's view");
    } else if (!strcmp(c, "toggle")) {
        /* freecam=0 in the file: F8 turns it on and keeps that there,
         * leaving the other lines as they were, and off again; freecam takes
         * it up at the start of the next frame. */
        double v = -1;
        assert(s_key && !strcmp(s_key, "F8") && s_key_run);
        assert(!frame(0, 0, eye) && !freecam_on());
        s_key_run();
        assert(nv_window_pref_num("freecam", &v) == 1 && v == 1);
        assert(!frame(0, 0, eye) && !freecam_on());
        freecam_frame();
        assert(frame(0, 0, eye) && freecam_on());
        assert(nv_window_pref_num("sensitivity_x", &v) == 1 && v == 7);
        s_key_run();
        freecam_frame();
        assert(!frame(0, 0, eye) && !freecam_on());
        assert(nv_window_pref_num("freecam", &v) == 1 && v == 0);
    } else if (!strcmp(c, "r3")) {
        /* The pad's Toggle free camera (R3 as it starts) is bound, with no
         * setting: it turns freecam on and keeps that, as F8 does, and off,
         * from the next frame. */
        double v = -1;
        assert(s_control == INPUT_CTL_FREECAM && s_button_run);
        assert(!freecam_on());
        s_button_run();
        assert(!freecam_on());
        freecam_frame();
        assert(freecam_on() && frame(0, 0, eye));
        assert(nv_window_pref_num("freecam", &v) == 1 && v == 1);
        s_button_run();
        freecam_frame();
        assert(!freecam_on());
        assert(nv_window_pref_num("freecam", &v) == 1 && v == 0);
    } else if (!strcmp(c, "menu")) {
        /* A row changes the tuning from the next frame, as F8 does the
         * switch: Sensitivity X at 10 turns full right by 6 degrees, once a
         * frame has taken it up. */
        const XBOX_MENU_ITEM *sx = NULL;
        int i;
        double v = -1;
        for (i = 0; i < s_nrows; i++)
            if (!strcmp(s_rows[i].label, "Sensitivity X")) sx = &s_rows[i];
        assert(sx);
        sx->set(10);
        assert(sx->value() == 10 && nv_window_pref_num("sensitivity_x", &v) == 1 && v == 10);
        for (i = 0; i < 10; i++) assert(frame(32767, 0, eye));
        angles(eye, &h, &p);
        near(h, 30, "turned at 5, the change not taken up yet");
        turn(32767, 0, &h, &p);
        near(h, 6, "full right at 10, from the next frame");
    } else if (!strcmp(c, "played")) {
        /* A recording's switch and tuning in place of the player's (off, at
         * 5): on, at 10, so full right turns by 6 degrees; the player's own
         * stay in the file and the menu. Once it stops, the player's are back,
         * from the next frame, and the log says so. */
        static const FREECAM_SETTINGS rec = { 1, 0, 0, 0, 10, 10, 0.15, 1, 5 };
        double v = -1;
        assert(!freecam_on());
        s_played = &rec;
        turn(32767, 0, &h, &p);
        assert(freecam_on());
        near(h, 6, "the recording's sensitivity");
        assert(nv_window_pref_num("freecam", &v) == 1 && v == 0);
        assert(!s_rows[1].state());
        s_played = NULL;
        assert(freecam_on());
        freecam_frame();
        assert(!freecam_on() && !frame(32767, 0, eye));
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
    } else {
        fprintf(stderr, "no case %s\n", c);
        return 2;
    }
    printf("ok %s\n", c);
    return 0;
}
