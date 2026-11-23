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
#include "playback.h"
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
 * wanted, its number (7, to show it is the one taken by), and the moves,
 * given by mouse() and taken whole, as the window's counts are, right and
 * down. */
#define MOUSE_ASKER 7
static int (*s_mouse_want)(void);
static int s_mouse_dx, s_mouse_dy;
int nv_window_mouse_look(int (*want)(void))
{
    s_mouse_want = want;
    return MOUSE_ASKER;
}
int nv_window_mouse_take(int asker, int *dx, int *dy)
{
    assert(asker == MOUSE_ASKER);
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
/* And what the camera does after the game's shots (port/playback.h's
 * PLAYBACK_BACK_): here eases back, as camera3's recordings play, unless a
 * case says otherwise; the glide is the glide case's. */
static int s_back_mode = PLAYBACK_BACK_EASE;
int playback_freecam_back(void) { return s_back_mode; }
void playback_freecam_took(double *yaw, double *pitch) { (void)yaw; (void)pitch; }

/* The menu's xbox_MenuAdd: the Input page's rows, caught. */
#define ROWS_MOST 16
static XBOX_MENU_ITEM s_rows[ROWS_MOST];
static int s_nrows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    while (count-- > 0 && s_nrows < ROWS_MOST) s_rows[s_nrows++] = *items++;
}

/* One frame: a pad read, then the camera's post-update, its follow
 * camera in it. Returns 1 if freecam placed the eye. */
static int frame(short rx, short ry, float eye[3])
{
    int placed;
    freecam_pad(0, &rx, &ry);
    placed = freecam_orbit(CAM, 3, tgt, seye, 0, eye);
    freecam_ran(CAM);
    return placed;
}

/* One frame in the game's order: the camera's update, no shot playing
 * (freecam_shot), freecam_frame, then frame's. */
static int step(short rx, short ry, float eye[3])
{
    freecam_shot(CAM, 0);
    freecam_frame();
    return frame(rx, ry, eye);
}

/* n frames of one of the game's shots on cam, in the game's order: the
 * camera's update says so, freecam_frame, then CAM's post-update, in the
 * game's mode, without the follow camera. */
static void shot(uint32_t cam, int n)
{
    while (n-- > 0) {
        freecam_shot(cam, 1);
        freecam_frame();
        freecam_ran(CAM);
    }
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

/* What the post-update's freecam_ahead gave, the last frame gframe or gshot
 * ran: 1, with the view the glide was headed to, or 0. */
static int s_ahead;
static float s_ahead_eye[3], s_ahead_at[3];

/* The glide (freecam_view): one frame in the game's order, the follow
 * camera's hook as recomp_manual.c has it, the game's view from game_eye,
 * what it looks at tgt, then the post-update's end (freecam_ran,
 * freecam_ahead, freecam_shown). eye and at: the view the frame shows.
 * Returns what freecam_orbit did. */
static int gframe(short rx, short ry, const float game_eye[3], float eye[3], float at[3])
{
    int placed;
    freecam_shot(CAM, 0);
    freecam_frame();
    freecam_pad(0, &rx, &ry);
    memcpy(at, tgt, sizeof tgt);
    placed = freecam_orbit(CAM, 3, tgt, game_eye, 0, eye);
    if (placed == FREECAM_OFF) memcpy(eye, game_eye, sizeof tgt);
    else freecam_view(CAM, 0, eye, at);
    freecam_ran(CAM);
    s_ahead = freecam_ahead(CAM, s_ahead_eye, s_ahead_at);
    freecam_shown(CAM, eye, at);
    return placed;
}

/* And a frame of the graffiti shot, as the game frames it, shot_eye looking
 * at shot_at; eye and at, the view shown. Returns whether freecam glided. */
static int gshot(const float shot_eye[3], const float shot_at[3], float eye[3], float at[3])
{
    int glided;
    freecam_shot(CAM, 1);
    freecam_frame();
    memcpy(eye, shot_eye, sizeof tgt);
    memcpy(at, shot_at, sizeof tgt);
    glided = freecam_view(CAM, 1, eye, at);
    freecam_ran(CAM);
    s_ahead = freecam_ahead(CAM, s_ahead_eye, s_ahead_at);
    freecam_shown(CAM, eye, at);
    return glided;
}

/* Where the glide puts the eye k of the way from the view (fe, ft) to
 * (te, tt): what it looks at straight there, and the eye's heading by the
 * shorter way round, its pitch and its distance. */
static void glide_want(const float fe[3], const float ft[3], const float te[3], const float tt[3],
                       double k, double eye[3], double at[3])
{
    double fo[3], to[3], fd, td, fy, ty, fp, tp, y, p, d;
    int i;
    for (i = 0; i < 3; i++) {
        fo[i] = fe[i] - ft[i];
        to[i] = te[i] - tt[i];
        at[i] = ft[i] + (tt[i] - ft[i]) * k;
    }
    fd = sqrt(fo[0] * fo[0] + fo[1] * fo[1] + fo[2] * fo[2]);
    td = sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    fy = atan2(fo[0], fo[2]); ty = atan2(to[0], to[2]);
    fp = asin(fo[1] / fd); tp = asin(to[1] / td);
    y = fy + remainder(ty - fy, 2 * 180.0 * deg) * k;
    p = fp + (tp - fp) * k;
    d = fd + (td - fd) * k;
    eye[0] = at[0] + d * cos(p) * sin(y);
    eye[1] = at[1] + d * sin(p);
    eye[2] = at[2] + d * cos(p) * cos(y);
}

static void near3(const float got[3], const double want[3], const char *what)
{
    char line[200];
    int i;
    for (i = 0; i < 3; i++) {
        snprintf(line, sizeof line, "%s, [%d]", what, i);
        near(got[i], want[i], line);
    }
}

static double gap(const float a[3], const float b[3])
{
    double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrt(x * x + y * y + z * z);
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
                                              "Mouse look", "Mouse sensitivity", "Reset to default" };
        const XBOX_MENU_ITEM *reset = &s_rows[10];
        int i;
        assert(s_nrows == 11);
        assert(s_rows[0].menu == XBOX_MENU_INPUT && s_rows[0].kind == XBOX_ITEM_GROUP);
        assert(!strcmp(s_rows[0].label, XBOX_GROUP_FREECAM) && !strcmp(s_rows[0].label, "Free camera"));
        assert(s_rows[0].hint && !strcmp(s_rows[0].hint, "Free camera toggle and settings."));
        for (i = 1; i < s_nrows; i++) {
            assert(s_rows[i].menu == XBOX_MENU_INPUT && !strcmp(s_rows[i].label, labels[i - 1]));
            assert(s_rows[i].group && !strcmp(s_rows[i].group, XBOX_GROUP_FREECAM) && !s_rows[i].under);
            assert(s_rows[i].hint && strlen(s_rows[i].hint) <= MENU_HINT_LONGEST);
        }
        /* File > Presets: Enable free camera off in Xbox, and left as the
         * player has it by the others; the rest of the page, how the free
         * camera handles, set by no preset, with Reset to default at its
         * foot, which asks first (spriteice, 22 November 2026, the cards
         * "Free camera too" and "Add a Reset row"; and 23 November 2026,
         * the card "Leave them all"). */
        assert(s_rows[1].presets == XBOX_PRESETS_SET && !s_rows[1].keep);
        assert(s_rows[1].preset[XBOX_PRESET_XBOX] == 0 && s_rows[1].preset[XBOX_PRESET_MODERN] == 1);
        assert(s_rows[1].leave == XBOX_PRESETS_LEVELS);
        for (i = 2; i < s_nrows; i++) assert(s_rows[i].presets == XBOX_PRESETS_LEFT && !s_rows[i].leave);
        assert(reset->kind == XBOX_ITEM_ACTION && reset->run && reset->confirm);
        assert(reset->hint[strlen(reset->hint) - 1] == '.');
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
         * past the default deadzone, 0.10. */
        turn(32767, 0, &h, &p);
        near(h, 6, "full right at 10");
        turn(0, -32767, &h, &p);
        near(p, 1.5, "full down at 2.5");
        turn(16384, 0, &h, &p);
        near(h, 6.0 * (16384.0 / 32767 - 0.10) / 0.90, "half right at 10");
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
        /* Every key bad: the defaults, 5 and 0.10 and no invert. */
        turn(32767, 0, &h, &p);
        near(h, 3, "default sensitivity, not inverted");
        turn(2900, 0, &h, &p);                  /* 0.089: inside 0.10 */
        near(h, 0, "default deadzone");
        turn(6554, 0, &h, &p);                  /* 0.2: past it */
        near(h, 3 * (6554.0 / 32767 - 0.10) / 0.90, "past the default deadzone");
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
    } else if (!strcmp(c, "menu_reset")) {
        /* Reset to default: every row but the switch back at its default,
         * kept in the settings file; the free camera stays on. */
        const XBOX_MENU_ITEM *reset = &s_rows[10];
        FREECAM_SETTINGS now;
        double v = -1;
        freecam_settings(&now);
        assert(now.on && now.invert_x && now.sens_x == 8 && now.deadzone == 0.5 && now.centre == 2);
        assert(!now.mouse && now.mouse_sens == 9);
        assert(!strcmp(reset->label, "Reset to default"));
        reset->run();
        freecam_settings(&now);
        assert(now.on && !now.invert_x && !now.invert_y && now.sens_x == 5 && now.sens_y == 5);
        assert(fabs(now.deadzone - 0.10) < 1e-9 && now.centre == 0 && now.mouse && now.mouse_sens == 5);
        assert(nv_window_pref_num("deadzone", &v) == 1 && fabs(v - 0.10) < 1e-9);
        assert(nv_window_pref_num("sensitivity_x", &v) == 1 && v == 5);
        assert(nv_window_pref_num("invert_x", &v) == 1 && v == 0);
        assert(nv_window_pref_num("autocentre", &v) == 1 && v == 0);
        assert(nv_window_pref_num("mouse", &v) == 1 && v == 1);
        assert(nv_window_pref_num("mouse_sensitivity", &v) == 1 && v == 5);
        assert(nv_window_pref_num("freecam", &v) == 1 && v == 1);
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
    } else if (!strcmp(c, "shot_back")) {
        /* One of the game's shots (port/camera_scenes.h), the view turned
         * before it: kept as it starts, as far from the game's view as it
         * was, and eased back to once the follow camera runs again,
         * smoothly over FREECAM_BACK_FRAMES_BEFORE (as camera3's recordings
         * play), from wherever the game's view is then (spriteice, 22
         * November 2026), in whatever mode: here the
         * grind's (5), as a shot on a rail ends, then on foot (3). Each
         * frame in the game's order: the camera's update, with its word on
         * a shot (freecam_shot), then freecam_frame (the sound manager's
         * update), then the post-update, the follow camera's in it or not
         * (freecam_orbit, then freecam_ran). The update's word that the
         * shot is over comes before the follow camera runs again. */
        static const float side[3] = { 10, 0, 0 };     /* the game's view, heading -90 */
        short rx = 0, ry = 0;
        int i;
        for (i = 0; i < 10; i++) assert(step(32767, 0, eye));
        for (i = 0; i < 5; i++) assert(step(0, 32767, eye));
        angles(eye, &h, &p);
        near(h, 30, "turned before the shot");
        near(p, -15, "tilted before the shot");
        /* The shot: the camera runs in the game's mode, which says so, for
         * 40 frames; the stick is the game's meanwhile. */
        shot(CAM, 40);
        freecam_shot(CAM, 0);
        freecam_frame();
        rx = 32767; freecam_pad(0, &rx, &ry);
        assert(rx == 32767);
        rx = 0; freecam_pad(0, &rx, &ry);
        /* The follow camera again, on the rail, the game's view now on +x. */
        assert(freecam_orbit(CAM, 5, tgt, side, 0, eye) == FREECAM_STARTED);
        freecam_ran(CAM);
        for (i = 1; i <= FREECAM_BACK_FRAMES_BEFORE; i++) {
            double t = (double)i / FREECAM_BACK_FRAMES_BEFORE, k = t * t * (3 - 2 * t);
            if (i > 1) {
                freecam_shot(CAM, 0);
                freecam_frame();
                freecam_pad(0, &rx, &ry);
                assert(freecam_orbit(CAM, 5, tgt, side, 0, eye) == FREECAM_PLACED);
                freecam_ran(CAM);
            }
            angles(eye, &h, &p);
            near(h, -90 + 30 * k, "easing back: heading");
            near(p, -15 * k, "easing back: pitch");
            if (i == FREECAM_BACK_FRAMES_BEFORE / 2) near(h, -75, "half way, half the turn");
        }
        freecam_shot(CAM, 0);
        freecam_frame();
        assert(freecam_orbit(CAM, 5, tgt, side, 0, eye) == FREECAM_PLACED);
        freecam_ran(CAM);
        angles(eye, &h, &p);
        near(h, -60, "eased back: as far from the game's view as before");
        near(p, -15, "eased back: tilted as before");
        /* Another shot, on foot, the game's view back on -z: the stick
         * pushed on the fifth frame of the ease takes over from where it
         * is. */
        shot(CAM, 10);
        for (i = 1; i <= 6; i++) {
            rx = i == 5 ? 32767 : 0;
            assert(step(rx, 0, eye) == (i == 1 ? FREECAM_STARTED : FREECAM_PLACED));
        }
        {
            double t = 4.0 / FREECAM_BACK_FRAMES_BEFORE, k = t * t * (3 - 2 * t);
            angles(eye, &h, &p);
            near(h, 30 * k + 3, "the stick took over: heading");
            near(p, -15 * k, "the stick took over: pitch");
        }
        /* The game paused in a shot: the camera's update runs without its
         * post-update. The view kept stays, and is eased back to after. */
        {
            double h0, p0;
            for (i = 0; i < 10; i++) assert(step(32767, 0, eye));
            angles(eye, &h0, &p0);
            shot(CAM, 10);
            for (i = 0; i < 20; i++) { freecam_shot(CAM, 1); freecam_frame(); }
            shot(CAM, 5);
            assert(step(0, 0, eye) == FREECAM_STARTED);
            for (i = 1; i < FREECAM_BACK_FRAMES_BEFORE; i++) assert(step(0, 0, eye) == FREECAM_PLACED);
            angles(eye, &h, &p);
            near(h, h0, "paused in a shot, then eased back: heading");
            near(p, p0, "paused in a shot, then eased back: pitch");
        }
        /* A mode of the game's that is no shot after one (the level's exit):
         * the view kept is dropped, and the follow camera starts from the
         * game's view. */
        shot(CAM, 10);
        freecam_shot(CAM, 0); freecam_frame(); freecam_ran(CAM);
        assert(step(0, 0, eye) == FREECAM_STARTED);
        near(eye[0], 0, "after another mode, the game's view");
        near(eye[2], -10, "after another mode, the game's view, z");
        /* A scene after a shot: the camera post-updated, and no word from
         * its update, as the pad no longer moves its skater. */
        for (i = 0; i < 10; i++) assert(step(32767, 0, eye));
        shot(CAM, 10);
        freecam_frame(); freecam_ran(CAM);
        assert(step(0, 0, eye) == FREECAM_STARTED);
        near(eye[0], 0, "a scene after a shot: the game's view");
        /* Free camera turned off during a shot and on again: the game's view. */
        for (i = 0; i < 10; i++) assert(step(32767, 0, eye));
        shot(CAM, 10);
        s_key_run();
        shot(CAM, 1);
        s_key_run();
        freecam_shot(CAM, 0);
        freecam_frame();
        assert(frame(0, 0, eye) == FREECAM_STARTED);
        near(eye[0], 0, "off and on in a shot: the game's view");
        /* A recording made before the ease back: the game's view after a
         * shot, as then. */
        s_back_mode = PLAYBACK_BACK_NONE;
        for (i = 0; i < 10; i++) assert(step(32767, 0, eye));
        shot(CAM, 10);
        assert(step(0, 0, eye) == FREECAM_STARTED);
        near(eye[0], 0, "a recording made before: the game's view");
        near(eye[2], -10, "a recording made before: the game's view, z");
        /* The game's reset (the L trigger) as the shot ends: its view. */
        s_back_mode = PLAYBACK_BACK_EASE;
        for (i = 0; i < 10; i++) assert(step(32767, 0, eye));
        shot(CAM, 10);
        freecam_shot(CAM, 0);
        freecam_frame();
        freecam_pad(0, &rx, &ry);
        rx = 0; freecam_pad(0, &rx, &ry);
        assert(freecam_orbit(CAM, 3, tgt, seye, 1, eye) == FREECAM_STARTED);
        freecam_ran(CAM);
        near(eye[0], 0, "reset as a shot ends: the game's view");
        /* A shot on a camera freecam does not follow, this camera
         * post-updated in a mode of its own meanwhile: nothing kept. */
        for (i = 0; i < 10; i++) assert(step(32767, 0, eye));
        for (i = 0; i < 10; i++) {
            freecam_shot(CAM + 0x400, 1);
            freecam_shot(CAM, 0);
            freecam_frame();
            freecam_ran(CAM + 0x400);
            freecam_ran(CAM);
        }
        assert(step(0, 0, eye) == FREECAM_STARTED);
        near(eye[0], 0, "another camera's shot: the game's view");
    } else if (!strcmp(c, "shot_glide")) {
        /* The glide (spriteice, 22 November 2026, the card "Graffiti in and
         * out"): the view turned 30 degrees, then the graffiti shot as the
         * game frames it, a spot 8 along z looked at from 6 to its side and
         * 3 up. Into it a cut, as the game's camera does (spriteice, 22
         * November 2026: "the transition in for the graffiti cam in free cam
         * mode has to go ... the transition out stays"), and out of it to
         * the free camera's view, 30 degrees from the game's view as before,
         * over FREECAM_BACK_FRAMES: what it looks at straight there, the eye
         * around it by the shorter way round, its pitch and distance, each
         * smoothly. Through the glide, the controls follow the view it is
         * headed to (freecam_ahead), and the view shown at any other time
         * ("can the skater continue in the same direction the stick is
         * pushing? ... make the ramp in/out slightly faster"). Each frame in
         * the game's order, the follow camera's hook as recomp_manual.c has
         * it (gframe, gshot). */
        static const float spot[3] = { 0, 0, 8 }, shot_eye[3] = { 6, 3, 8 };
        float at[3], free_eye[3], last[3], turned[3];
        double want[3], want_at[3];
        int i;
        s_back_mode = PLAYBACK_BACK_OUT;
        for (i = 0; i < 10; i++) {
            assert(gframe(32767, 0, seye, eye, at));
            assert(!s_ahead);
        }
        angles(eye, &h, &p);
        near(h, 30, "the player's: turned before the shot");
        memcpy(free_eye, eye, sizeof eye);
        for (i = 0; i < 20; i++) {
            assert(!gshot(shot_eye, spot, eye, at));
            near(gap(eye, shot_eye) + gap(at, spot), 0, "the player's: the shot cuts in");
            assert(!s_ahead);
        }
        assert(FREECAM_BACK_FRAMES == 24);
        for (i = 1; i <= FREECAM_BACK_FRAMES; i++) {
            double t = (double)i / FREECAM_BACK_FRAMES, k = t * t * (3 - 2 * t);
            assert(gframe(0, 0, seye, eye, at) == (i == 1 ? FREECAM_STARTED : FREECAM_PLACED));
            glide_want(shot_eye, spot, free_eye, tgt, k, want, want_at);
            near3(eye, want, "the player's, gliding out of the shot: the eye");
            near3(at, want_at, "the player's, gliding out of the shot: what it looks at");
            assert(s_ahead);
            near(gap(s_ahead_eye, free_eye) + gap(s_ahead_at, tgt), 0,
                 "the player's, gliding out of the shot: the controls follow the free camera's view");
        }
        assert(gframe(0, 0, seye, eye, at) == FREECAM_PLACED);
        assert(!s_ahead);
        angles(eye, &h, &p);
        near(h, 30, "the player's, glided out: as far from the game's view as before");
        for (i = 0; i < 10; i++) assert(gframe(-32767, 0, seye, eye, at));
        angles(eye, &h, &p);
        near(h, 0, "the player's: turned back behind the player");
        /* As camera5's recordings play (PLAYBACK_BACK_STEADY): into the shot
         * from the free camera's last view too, over FREECAM_INTO_FRAMES,
         * the controls following the shot's view; and the rest of the glide
         * out's cases, which are the player's too. */
        s_back_mode = PLAYBACK_BACK_STEADY;
        for (i = 0; i < 10; i++) {
            assert(gframe(32767, 0, seye, eye, at));
            assert(!s_ahead);
        }
        angles(eye, &h, &p);
        near(h, 30, "turned before the shot");
        memcpy(free_eye, eye, sizeof eye);
        assert(FREECAM_INTO_FRAMES == 12 && FREECAM_BACK_FRAMES == 24);
        for (i = 1; i <= FREECAM_INTO_FRAMES; i++) {
            double t = (double)i / FREECAM_INTO_FRAMES, k = t * t * (3 - 2 * t);
            assert(gshot(shot_eye, spot, eye, at));
            glide_want(free_eye, tgt, shot_eye, spot, k, want, want_at);
            near3(eye, want, "gliding into the shot: the eye");
            near3(at, want_at, "gliding into the shot: what it looks at");
            if (i == 1) assert(gap(eye, free_eye) < 0.5);
            assert(s_ahead);
            near(gap(s_ahead_eye, shot_eye) + gap(s_ahead_at, spot), 0,
                 "gliding into the shot: the controls follow the shot's view");
        }
        near(gap(eye, shot_eye) + gap(at, spot), 0, "glided into the shot: the game's view");
        for (i = 0; i < 20; i++) {
            assert(!gshot(shot_eye, spot, eye, at));
            near(gap(eye, shot_eye) + gap(at, spot), 0, "in the shot: the game's view");
            assert(!s_ahead);
        }
        /* Out of it, on foot, the game's view behind the player again. */
        for (i = 1; i <= FREECAM_BACK_FRAMES; i++) {
            double t = (double)i / FREECAM_BACK_FRAMES, k = t * t * (3 - 2 * t);
            assert(gframe(0, 0, seye, eye, at) == (i == 1 ? FREECAM_STARTED : FREECAM_PLACED));
            glide_want(shot_eye, spot, free_eye, tgt, k, want, want_at);
            near3(eye, want, "gliding out of the shot: the eye");
            near3(at, want_at, "gliding out of the shot: what it looks at");
            if (i == 1) assert(gap(eye, shot_eye) < 0.25);
            assert(s_ahead);
            near(gap(s_ahead_eye, free_eye) + gap(s_ahead_at, tgt), 0,
                 "gliding out of the shot: the controls follow the free camera's view");
        }
        assert(gframe(0, 0, seye, eye, at) == FREECAM_PLACED);
        assert(!s_ahead);
        angles(eye, &h, &p);
        near(h, 30, "glided out: as far from the game's view as before");
        near(p, 0, "glided out: as tilted as before");
        near(gap(at, tgt), 0, "glided out: looking at the player");
        /* Again, the stick pushed on the tenth frame out: where it glides to
         * turns with it, 3 degrees, and the glide goes on there. */
        for (i = 0; i < 20; i++) gshot(shot_eye, spot, eye, at);
        turned[0] = (float)(-10 * sin(33 * deg));
        turned[1] = 0;
        turned[2] = (float)(-10 * cos(33 * deg));
        for (i = 1; i <= FREECAM_BACK_FRAMES; i++) {
            assert(gframe(i == 10 ? 32767 : 0, 0, seye, eye, at));
            if (i == 10) {
                double t = 10.0 / FREECAM_BACK_FRAMES, k = t * t * (3 - 2 * t);
                glide_want(shot_eye, spot, turned, tgt, k, want, want_at);
                near3(eye, want, "the stick in the glide out: the eye");
                near(gap(s_ahead_eye, turned), 0, "the stick in the glide out: the controls follow it turned");
            }
        }
        angles(eye, &h, &p);
        near(h, 33, "the stick in the glide out: turned where it glided to");
        /* A shot shorter than the glide in: out from where the glide in got
         * to, with no jump. */
        for (i = 0; i < 5; i++) assert(gshot(shot_eye, spot, eye, at));
        memcpy(last, eye, sizeof eye);
        assert(gframe(0, 0, seye, eye, at) == FREECAM_STARTED);
        assert(gap(eye, last) < 0.25);
        for (i = 1; i < FREECAM_BACK_FRAMES; i++) assert(gframe(0, 0, seye, eye, at));
        angles(eye, &h, &p);
        near(h, 33, "out of a short shot: where it was before it");
        /* A scene in the glide in, then the shot again: the glide was let
         * go, and the shot is the game's, as no free camera view was shown
         * the frame before it. */
        for (i = 0; i < 5; i++) assert(gframe(0, 0, seye, eye, at));
        for (i = 0; i < 5; i++) assert(gshot(shot_eye, spot, eye, at));
        freecam_frame();
        freecam_ran(CAM);
        freecam_shown(CAM, seye, tgt);
        assert(!gshot(shot_eye, spot, eye, at));
        near(gap(eye, shot_eye) + gap(at, spot), 0, "a scene in the glide in: the game's shot after it");
        for (i = 0; i < 5; i++) gframe(0, 0, seye, eye, at);
        /* A scene in the glide out (the camera post-updated without its
         * follow camera): the glide is let go, and the follow camera starts
         * from the game's view after it. */
        for (i = 0; i < 20; i++) gshot(shot_eye, spot, eye, at);
        for (i = 0; i < 5; i++) assert(gframe(0, 0, seye, eye, at));
        freecam_frame();
        freecam_ran(CAM);
        freecam_shown(CAM, shot_eye, spot);
        assert(gframe(0, 0, seye, eye, at) == FREECAM_STARTED);
        near(gap(eye, seye) + gap(at, tgt), 0, "a scene in the glide: the game's view after it");
        /* The game's reset (the L trigger) in the glide out: its view. */
        for (i = 0; i < 10; i++) assert(gframe(32767, 0, seye, eye, at));
        for (i = 0; i < 20; i++) gshot(shot_eye, spot, eye, at);
        for (i = 0; i < 5; i++) assert(gframe(0, 0, seye, eye, at));
        freecam_shot(CAM, 0);
        freecam_frame();
        memcpy(at, tgt, sizeof tgt);
        assert(freecam_orbit(CAM, 3, tgt, seye, 1, eye) == FREECAM_STARTED);
        assert(!freecam_view(CAM, 0, eye, at));
        freecam_ran(CAM);
        freecam_shown(CAM, eye, at);
        near(gap(eye, seye) + gap(at, tgt), 0, "reset in the glide: the game's view");
        /* As camera4's recordings play (PLAYBACK_BACK_GLIDE): the glides as
         * long as they were, 15 frames in and 30 out, and the controls
         * following the view the glide shows. */
        s_back_mode = PLAYBACK_BACK_GLIDE;
        for (i = 0; i < 40; i++) assert(gframe(0, 0, seye, eye, at));
        for (i = 0; i < 10; i++) assert(gframe(32767, 0, seye, eye, at));
        memcpy(free_eye, eye, sizeof eye);
        assert(FREECAM_INTO_FRAMES_BEFORE == 15 && FREECAM_BACK_FRAMES_BEFORE == 30);
        for (i = 1; i <= FREECAM_INTO_FRAMES_BEFORE; i++) {
            double t = (double)i / FREECAM_INTO_FRAMES_BEFORE, k = t * t * (3 - 2 * t);
            assert(gshot(shot_eye, spot, eye, at));
            glide_want(free_eye, tgt, shot_eye, spot, k, want, want_at);
            near3(eye, want, "a camera4 recording, gliding into the shot: the eye");
            assert(!s_ahead);
        }
        assert(!gshot(shot_eye, spot, eye, at));
        for (i = 1; i <= FREECAM_BACK_FRAMES_BEFORE; i++) {
            double t = (double)i / FREECAM_BACK_FRAMES_BEFORE, k = t * t * (3 - 2 * t);
            assert(gframe(0, 0, seye, eye, at));
            glide_want(shot_eye, spot, free_eye, tgt, k, want, want_at);
            near3(eye, want, "a camera4 recording, gliding out of the shot: the eye");
            assert(!s_ahead);
        }
        for (i = 0; i < 10; i++) assert(gframe(-32767, 0, seye, eye, at));
        angles(eye, &h, &p);
        near(h, 0, "a camera4 recording: turned back behind the player");
        /* As camera3's recordings play (the angle eased back, no glide):
         * the shot cuts in, and out its angle eases from the game's view. */
        s_back_mode = PLAYBACK_BACK_EASE;
        for (i = 0; i < 10; i++) assert(gframe(32767, 0, seye, eye, at));
        for (i = 0; i < 20; i++) {
            assert(!gshot(shot_eye, spot, eye, at));
            near(gap(eye, shot_eye) + gap(at, spot), 0, "a camera3 recording: the shot cuts in");
        }
        assert(gframe(0, 0, seye, eye, at) == FREECAM_STARTED);
        {
            double t = 1.0 / FREECAM_BACK_FRAMES_BEFORE, k = t * t * (3 - 2 * t);
            angles(eye, &h, &p);
            near(h, 30 * k, "a camera3 recording: easing back from the game's view");
            near(gap(at, tgt), 0, "a camera3 recording: looking at the player");
        }
        /* As older ones play: the game's view after the shot. */
        s_back_mode = PLAYBACK_BACK_NONE;
        for (i = 0; i < 40; i++) assert(gframe(32767, 0, seye, eye, at));
        for (i = 0; i < 20; i++) assert(!gshot(shot_eye, spot, eye, at));
        assert(gframe(0, 0, seye, eye, at) == FREECAM_STARTED);
        near(gap(eye, seye), 0, "an older recording: the game's view");
        /* The free camera off: the game's shot and view, as the game has
         * them (the default camera untouched). */
        s_back_mode = PLAYBACK_BACK_GLIDE;
        for (i = 0; i < 10; i++) assert(gframe(32767, 0, seye, eye, at));
        s_key_run();
        for (i = 0; i < 20; i++) assert(!gshot(shot_eye, spot, eye, at));
        assert(gframe(0, 0, seye, eye, at) == FREECAM_OFF);
        assert(!freecam_view(CAM, 0, eye, at));
        near(gap(eye, seye) + gap(at, tgt), 0, "off: the game's view");
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
