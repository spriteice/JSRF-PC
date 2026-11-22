/* Run with: python tests/photo_mode/run.py
 *
 * Photo mode without the game: port/photo_mode.c as it is, with
 * port/freecam.c for the player's look settings and the turn's maths, driven
 * the way recomp_manual.c's hooks drive it once a game frame -- the main
 * loop's pass (photo_mode_frame), the follow camera (photo_mode_followed),
 * the camera's draw (photo_mode_place) and the area set (photo_mode_area) --
 * with a game whose camera looks along +z from (0 5 -10). Stood in for: the
 * pads (pad_read), the HUD's hiding (nv2a_hud_hold), the settings file's
 * numbers, the window's keys and mouse look, the controller layout's hotkey
 * and input playback's hooks for the free camera; the map's portals are the
 * case's. Each case is its own run,
 * as the start variable and the settings are read once; run.py sets the
 * environment and checks the log. A "FAIL:" line for each check that
 * fails. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freecam.h"
#include "pad.h"
#include "photo_mode.h"

#define CAM       0x00401000u   /* the player's camera */
#define OTHER_CAM 0x00402000u
#define FULL      32767         /* a stick at full tilt */

static const double deg = 3.14159265358979323846 / 180.0;
static const float k_game_eye[3] = { 0, 5, -10 }, k_game_look[3] = { 0, 5, 0 };
static int g_bad;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); g_bad = 1; } } while (0)

/* ---- what photo_mode.c and freecam.c reach ---- */

/* The pads: port 0's, as set; the others have none. */
static XBOX_INPUT_STATE s_pad;
int pad_read(uint32_t port, XBOX_INPUT_STATE *st)
{
    memset(st, 0, sizeof *st);
    if (port != 0) return 0;
    *st = s_pad;
    return 1;
}

static int s_hud_held, s_hud_calls;
void nv2a_hud_hold(int on)
{
    s_hud_held = on;
    s_hud_calls++;
}

/* The settings file's numbers: the case's keys. */
#define KEYS_MOST 4
static const char *s_key[KEYS_MOST];
static double s_val[KEYS_MOST];
static int s_nkeys;
int nv_window_pref_num(const char *key, double *v)
{
    int i;
    for (i = 0; i < s_nkeys; i++)
        if (!strcmp(s_key[i], key)) { *v = s_val[i]; return 1; }
    return 0;
}
void nv_window_pref_set_num(const char *key, double v) { (void)key; (void)v; }
void nv_window_hotkey(const char *name, void (*run)(void)) { (void)name; (void)run; }
void input_remap_pad_hotkey(int control, void (*run)(void)) { (void)control; (void)run; }
int playback_freecam(FREECAM_SETTINGS *fc) { (void)fc; return 0; }
int playback_mouse(int *dx, int *dy) { (void)dx; (void)dy; return 0; }
int playback_freecam_back(void) { return 1; }

/* The window's mouse look: who asked (the free camera first, then photo
 * mode, as the engine starts them), and the moves, counted for each asker
 * apart, as the window counts them, and taken whole, right and down.
 * mouse_move() moves it for every asker. */
static int (*s_asker[2])(void);
static int s_naskers, s_mouse_dx[2], s_mouse_dy[2];
int nv_window_mouse_look(int (*want)(void))
{
    if (s_naskers == 2) return -1;
    s_asker[s_naskers] = want;
    return s_naskers++;
}
int nv_window_mouse_take(int asker, int *dx, int *dy)
{
    *dx = *dy = 0;
    if (asker < 0 || asker >= s_naskers) return 1;
    *dx = s_mouse_dx[asker];
    *dy = s_mouse_dy[asker];
    s_mouse_dx[asker] = s_mouse_dy[asker] = 0;
    return 1;
}
static void mouse_move(int dx, int dy)
{
    int i;
    for (i = 0; i < 2; i++) { s_mouse_dx[i] += dx; s_mouse_dy[i] += dy; }
}
static int photo_wants_mouse(void)
{
    return s_naskers == 2 && s_asker[1]();
}

/* ---- the game ---- */

static PHOTO_GAME s_game;
static int s_follows = 1;           /* the follow camera follows the player */
static int s_follows_held;          /* ... even while a hold is on */
static float s_eye[3], s_look[3];   /* the camera's view, as last drawn */
static int s_placed;

/* One game frame, as the hooks run it: the pass's start, where the hold
 * asked for is taken up or let go; the update, where the follow camera runs
 * unless a hold is on; and the camera's draw, from the game's own view. */
static int frame(void)
{
    int act = photo_mode_frame(&s_game);
    if (act == PHOTO_ASK_HOLD) s_game.holds |= PHOTO_HOLD_OURS;
    if (act == PHOTO_ASK_LET_GO) s_game.holds &= ~PHOTO_HOLD_OURS;
    if ((!s_game.holds || s_follows_held) && s_follows) photo_mode_followed(CAM);
    memcpy(s_eye, k_game_eye, sizeof s_eye);
    memcpy(s_look, k_game_look, sizeof s_look);
    s_placed = photo_mode_place(CAM, s_eye, s_look);
    return act;
}

/* Frames until photo mode asks for the hold, at most `most`: the count, or
 * -1. */
static int frames_to_hold(int most)
{
    int i;
    for (i = 1; i <= most; i++)
        if (frame() == PHOTO_ASK_HOLD) return i;
    return -1;
}

static void stick(short lx, short ly, short rx, short ry)
{
    s_pad.Gamepad.sThumbLX = lx; s_pad.Gamepad.sThumbLY = ly;
    s_pad.Gamepad.sThumbRX = rx; s_pad.Gamepad.sThumbRY = ry;
}

static void button(int b, int held)
{
    s_pad.Gamepad.bAnalogButtons[b] = held ? 255 : 0;
}

static int near3(const float *v, double x, double y, double z)
{
    return fabs(v[0] - x) < 1e-3 && fabs(v[1] - y) < 1e-3 && fabs(v[2] - z) < 1e-3;
}

/* The view's heading and tilt, in degrees, from the eye and look point
 * last drawn. */
static double heading(void) { return atan2(s_look[0] - s_eye[0], s_look[2] - s_eye[2]) / deg; }
static double tilt(void)
{
    double d[3] = { s_look[0] - s_eye[0], s_look[1] - s_eye[1], s_look[2] - s_eye[2] };
    return asin(d[1] / sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])) / deg;
}

/* Ticked, then held, from the game's view: the eye where the game's was. */
static void into_photo_mode(void)
{
    int n;
    if (!photo_mode_ticked()) photo_mode_flip();
    /* The follow camera ran in the frame before, or it didn't. */
    n = frames_to_hold(PHOTO_SETTLE_FRAMES + 5);
    CHECK(n == PHOTO_SETTLE_FRAMES || n == PHOTO_SETTLE_FRAMES + 1, "held after %d frames", n);
    CHECK(s_placed && near3(s_eye, 0, 5, -10) && near3(s_look, 0, 5, -10 + PHOTO_LOOK_AHEAD),
          "the first draw: placed %d, eye (%g %g %g), look (%g %g %g)", s_placed,
          s_eye[0], s_eye[1], s_eye[2], s_look[0], s_look[1], s_look[2]);
}

/* ---- the cases ---- */

/* JSRF_START_PHOTO, read at start and taken out: run.py sets it. */
static void start(int want)
{
    CHECK(photo_mode_ticked() == want, "ticked %d at start, not %d", photo_mode_ticked(), want);
    CHECK(!getenv(PHOTO_START_VAR), "%s left in the environment", PHOTO_START_VAR);
}

/* Waits for control: the follow camera following for PHOTO_SETTLE_FRAMES
 * frames in a row with no hold on; a hold, or a frame not followed, starts
 * the wait again. */
static void wait(void)
{
    int i;
    for (i = 0; i < 100; i++) CHECK(frame() == PHOTO_NOTHING, "held while not ticked");
    photo_mode_flip();
    CHECK(photo_mode_ticked(), "not ticked");
    for (i = 0; i < 10; i++) frame();
    s_game.holds = PHOTO_HOLD_PAUSE;            /* the pause menu */
    for (i = 0; i < 100; i++) CHECK(frame() == PHOTO_NOTHING, "held over the game's pause");
    s_game.holds = PHOTO_HOLD_2;                /* a hold the follow camera runs on through */
    s_follows_held = 1;
    for (i = 0; i < 100; i++) CHECK(frame() == PHOTO_NOTHING, "held over the game's second hold");
    s_follows_held = 0;
    s_game.holds = 0;
    for (i = 0; i < 10; i++) frame();
    s_follows = 0;                              /* a scene: the follow camera doesn't run */
    for (i = 0; i < 100; i++) CHECK(frame() == PHOTO_NOTHING, "held with no control");
    s_follows = 1;
    s_game.holds = PHOTO_HOLD_3;
    frame();
    s_game.holds = 0;
    CHECK(!s_hud_calls && !photo_mode_holding(), "the HUD or the hold before control");
    CHECK(frames_to_hold(PHOTO_SETTLE_FRAMES + 5) == PHOTO_SETTLE_FRAMES + 1, "the wait isn't the settle");
    CHECK(photo_mode_holding() && s_hud_held && s_hud_calls == 1, "holding %d, HUD hidden %d (%d calls)",
          photo_mode_holding(), s_hud_held, s_hud_calls);
    for (i = 0; i < 10; i++) CHECK(frame() == PHOTO_NOTHING, "asked again while held");
}

/* The flight: the left stick along the view, the triggers straight up and
 * down, R1 (White) four times as fast, L1 (Black) a quarter, both one; A and
 * X nothing. */
static void fly(void)
{
    const double v = PHOTO_SPEED;
    into_photo_mode();
    stick(0, FULL, 0, 0);
    frame();
    CHECK(near3(s_eye, 0, 5, -10 + v), "forward: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    stick(0, -FULL, 0, 0);
    frame(); frame();
    CHECK(near3(s_eye, 0, 5, -10 - v), "back: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    stick(FULL, 0, 0, 0);
    frame();
    CHECK(near3(s_eye, v, 5, -10 - v), "right: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    stick(0, 0, 0, 0);
    button(XBOX_BUTTON_RTRIGGER, 1);
    frame();
    CHECK(near3(s_eye, v, 5 + v, -10 - v), "R trigger: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    button(XBOX_BUTTON_RTRIGGER, 0);
    button(XBOX_BUTTON_LTRIGGER, 1);
    frame(); frame();
    CHECK(near3(s_eye, v, 5 - v, -10 - v), "L trigger: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    button(XBOX_BUTTON_LTRIGGER, 0);
    CHECK(near3(s_look, v, 5 - v, -10 - v + PHOTO_LOOK_AHEAD), "the look point isn't PHOTO_LOOK_AHEAD ahead");
    stick(0, FULL, 0, 0);
    button(XBOX_BUTTON_WHITE, 1);
    frame();
    CHECK(near3(s_eye, v, 5 - v, -10 - v + v * PHOTO_FAST), "R1: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    button(XBOX_BUTTON_BLACK, 1);
    frame();
    CHECK(near3(s_eye, v, 5 - v, -10 - v + v * PHOTO_FAST + v * PHOTO_FAST * PHOTO_SLOW),
          "R1 and L1: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    button(XBOX_BUTTON_WHITE, 0);
    frame();
    CHECK(near3(s_eye, v, 5 - v, -10 - v + v * PHOTO_FAST + v * PHOTO_FAST * PHOTO_SLOW + v * PHOTO_SLOW),
          "L1: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    button(XBOX_BUTTON_BLACK, 0);
    {
        float e[3];
        memcpy(e, s_eye, sizeof e);
        button(XBOX_BUTTON_A, 1);
        button(XBOX_BUTTON_X, 1);
        frame();
        CHECK(near3(s_eye, e[0], e[1], e[2] + v), "A and X: eye (%g %g %g) from (%g %g %g)",
              s_eye[0], s_eye[1], s_eye[2], e[0], e[1], e[2]);
        button(XBOX_BUTTON_A, 0);
        button(XBOX_BUTTON_X, 0);
    }
    /* Along the view: tilted up, forward goes up too. */
    stick(0, 0, 0, FULL);
    frame();
    stick(0, 0, 0, 0);
    {
        float e[3];
        double t = tilt() * deg;
        memcpy(e, s_eye, sizeof e);
        stick(0, FULL, 0, 0);
        frame();
        CHECK(near3(s_eye, e[0], e[1] + v * sin(t), e[2] + v * cos(t)),
              "forward tilted %.1f: eye (%g %g %g) from (%g %g %g)", t / deg, s_eye[0], s_eye[1], s_eye[2],
              e[0], e[1], e[2]);
    }
}

/* The right stick turns the view by Input > Free camera's tuning: right
 * turns right, up tilts up, at its sensitivity's steps a frame; inverted
 * as it asks. run.py gives the settings. */
static void turn(int inverted, double sens)
{
    const double step = 0.6 * sens;             /* freecam.c's FREECAM_STEP_DEG a sensitivity */
    const double sx = inverted ? -1 : 1;
    into_photo_mode();
    stick(0, 0, FULL, 0);
    frame();
    CHECK(fabs(heading() - sx * step) < 1e-3, "right: heading %g, not %g", heading(), sx * step);
    CHECK(s_look[0] - s_eye[0] > 0 ? !inverted : inverted, "right stick: the view turned the wrong way");
    frame();
    CHECK(fabs(heading() - 2 * sx * step) < 1e-3, "right twice: heading %g", heading());
    stick(0, 0, 0, FULL);
    frame();
    CHECK(fabs(tilt() - sx * step) < 1e-3, "up: tilt %g, not %g", tilt(), sx * step);
    stick(0, 0, 0, 0);
    frame();
    CHECK(fabs(tilt() - sx * step) < 1e-3 && fabs(heading() - 2 * sx * step) < 1e-3, "moved with no stick");
    /* Strafing goes square to the view on the level, whatever the tilt. */
    {
        float e[3];
        double h = heading() * deg;
        memcpy(e, s_eye, sizeof e);
        stick(FULL, 0, 0, 0);
        frame();
        CHECK(near3(s_eye, e[0] + PHOTO_SPEED * cos(h), e[1], e[2] - PHOTO_SPEED * sin(h)),
              "right, turned %.1f: eye (%g %g %g)", h / deg, s_eye[0], s_eye[1], s_eye[2]);
    }
}

/* Input > Free camera's deadzone, for both sticks: inside it nothing; past
 * it, scaled from its edge. run.py gives 0.5. */
static void deadzone(void)
{
    into_photo_mode();
    stick(0, (short)(0.4 * FULL), (short)(0.4 * FULL), 0);
    frame();
    CHECK(near3(s_eye, 0, 5, -10) && fabs(heading()) < 1e-6, "moved inside the deadzone");
    stick(0, (short)(0.75 * FULL), 0, 0);
    frame();
    CHECK(fabs(s_eye[2] - (-10 + PHOTO_SPEED * 0.5)) < 1e-3, "three quarters with a half deadzone: z %g", s_eye[2]);
}

/* How far across, on the level, the look point is from the eye. */
static double across(void)
{
    return hypot(s_look[0] - s_eye[0], s_look[2] - s_eye[2]);
}

/* The tilt stops short of straight up and down, and there the look point is
 * still more than PHOTO_VIEW_LEVEL across from the eye, so the game's view
 * is built its usual way. */
static void pitch(void)
{
    int i;
    into_photo_mode();
    stick(0, 0, 0, FULL);
    for (i = 0; i < 100; i++) frame();
    CHECK(fabs(tilt() - 89.0) < 1e-2, "up all the way: tilt %g", tilt());
    CHECK(across() > PHOTO_VIEW_LEVEL, "up all the way: the look point %g across", across());
    stick(0, 0, 0, -FULL);
    for (i = 0; i < 200; i++) frame();
    CHECK(fabs(tilt() + 89.0) < 1e-2, "down all the way: tilt %g", tilt());
    CHECK(across() > PHOTO_VIEW_LEVEL, "down all the way: the look point %g across", across());
}

/* A portal as the game makes one from a quad's corners (sub_0006F7B0): the
 * triangles (0, 3, 2) and (0, 2, 1), and its offset. */
static void quad(PHOTO_PORTAL *p, const float c[4][3], float bx, float by, float bz)
{
    static const int k_tri[2][3] = { { 0, 3, 2 }, { 0, 2, 1 } };
    int t, i;
    for (t = 0; t < 2; t++)
        for (i = 0; i < 3; i++) memcpy(p->tri[t][i], c[k_tri[t][i]], sizeof p->tri[t][i]);
    p->by[0] = bx; p->by[1] = by; p->by[2] = bz;
}

/* Frames of the left stick held, then let go. */
static void hold_stick(short lx, short ly, int frames)
{
    stick(lx, ly, 0, 0);
    while (frames-- > 0) frame();
    stick(0, 0, 0, 0);
}

/* Two portals that join two parts of a map, as a tunnel's are: across the
 * way the game's camera faces, at z 1 from x -5 to 5, facing back towards
 * it, by (100 0 50); and the way back, at z 51 from x 95 to 105, facing on,
 * by (-100 0 -50). */
static const float k_door[4][3] = { { -5, 0, 1 }, { 5, 0, 1 }, { 5, 10, 1 }, { -5, 10, 1 } };
static const float k_door_back[4][3] = { { 105, 0, 51 }, { 95, 0, 51 }, { 95, 10, 51 }, { 105, 10, 51 } };

/* The map's portals: crossed against its face, the eye moves by its
 * offset, the look point with it; through and back by a pair; Y goes back
 * to where photo mode started, in the part it started in; passed beside
 * the quad, or crossed the other way, it doesn't move. */
static void portal(void)
{
    const double v = PHOTO_SPEED;
    PHOTO_PORTAL p[2];
    quad(&p[0], k_door, 100, 0, 50);
    quad(&p[1], k_door_back, -100, 0, -50);
    s_game.portals = p;
    s_game.nportals = 2;
    into_photo_mode();
    hold_stick(0, FULL, 5);                     /* to z 0, short of it */
    CHECK(near3(s_eye, 0, 5, 0), "short of the portal: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    hold_stick(0, FULL, 1);                     /* through it, 0 to 2 */
    CHECK(near3(s_eye, 100, 5, 2 + 50) && near3(s_look, 100, 5, 52 + PHOTO_LOOK_AHEAD),
          "through: eye (%g %g %g), look (%g %g %g)", s_eye[0], s_eye[1], s_eye[2], s_look[0], s_look[1], s_look[2]);
    hold_stick(0, -FULL, 1);                    /* back through the other, 52 to 50 */
    CHECK(near3(s_eye, 0, 5, 0), "back through: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    hold_stick(0, FULL, 1);                     /* and through again */
    CHECK(near3(s_eye, 100, 5, 52), "through again: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    button(XBOX_BUTTON_Y, 1);
    frame();
    button(XBOX_BUTTON_Y, 0);
    CHECK(near3(s_eye, 0, 5, -10) && fabs(heading()) < 1e-3, "Y after a crossing: eye (%g %g %g), heading %g",
          s_eye[0], s_eye[1], s_eye[2], heading());
    hold_stick(FULL, 0, 10);                    /* right, to x 20 */
    hold_stick(0, FULL, 7);                     /* past it beside its quad, to z 4 */
    CHECK(near3(s_eye, 10 * v, 5, -10 + 7 * v), "beside: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    hold_stick(-FULL, 0, 10);                   /* left, behind it, to x 0 */
    hold_stick(0, -FULL, 2);                    /* back through it the other way, 4 to 0 */
    CHECK(near3(s_eye, 0, 5, 0), "the other way: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
}

/* Once a frame at most, the first portal crossed: one with no offset (the
 * game moves nothing by it) before another in the same place stops it; on
 * its own, the other moves the eye once. */
static void portal_once(void)
{
    PHOTO_PORTAL p[3];
    quad(&p[0], k_door, 0, 0, 0);
    quad(&p[1], k_door, 100, 0, 50);
    quad(&p[2], k_door, 0, 0, 1000);
    s_game.portals = p;
    s_game.nportals = 3;
    into_photo_mode();
    hold_stick(0, FULL, 6);
    CHECK(near3(s_eye, 0, 5, 2), "through one with no offset first: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
    s_game.portals = p + 1;
    s_game.nportals = 2;
    button(XBOX_BUTTON_Y, 1);
    frame();
    button(XBOX_BUTTON_Y, 0);
    hold_stick(0, FULL, 6);
    CHECK(near3(s_eye, 100, 5, 52), "through two at once: eye (%g %g %g)", s_eye[0], s_eye[1], s_eye[2]);
}

/* Y: back to where photo mode started, once a press. */
static void back(void)
{
    int i;
    into_photo_mode();
    stick(FULL, FULL, FULL, FULL);
    for (i = 0; i < 10; i++) frame();
    stick(0, FULL, 0, 0);
    button(XBOX_BUTTON_Y, 1);
    frame();
    CHECK(near3(s_eye, 0, 5, -10 + PHOTO_SPEED) && fabs(heading()) < 1e-3 && fabs(tilt()) < 1e-3,
          "Y: eye (%g %g %g), heading %g, tilt %g", s_eye[0], s_eye[1], s_eye[2], heading(), tilt());
    frame();
    CHECK(near3(s_eye, 0, 5, -10 + 2 * PHOTO_SPEED), "Y held: back again, eye z %g", s_eye[2]);
    button(XBOX_BUTTON_Y, 0);
}

/* Unticked: the hold let go, the HUD back, the game's view given back, and
 * the camera the game's again; ticked again, held again after the wait. */
static void untick(void)
{
    uint32_t cam = 0;
    float eye[3], look[3];
    int i;
    into_photo_mode();
    stick(0, FULL, 0, 0);
    for (i = 0; i < 5; i++) frame();
    stick(0, 0, 0, 0);
    photo_mode_flip();
    CHECK(frame() == PHOTO_ASK_LET_GO, "untick didn't let go");
    CHECK(!photo_mode_holding() && !s_hud_held, "holding %d, HUD hidden %d", photo_mode_holding(), s_hud_held);
    CHECK(!s_placed && near3(s_eye, 0, 5, -10), "placed after letting go");
    CHECK(photo_mode_game_view(&cam, eye, look) && cam == CAM && near3(eye, 0, 5, -10) && near3(look, 0, 5, 0),
          "the game's view to put back: cam %08X", cam);
    CHECK(!photo_mode_game_view(&cam, eye, look), "the game's view given twice");
    for (i = 0; i < 100; i++) CHECK(frame() == PHOTO_NOTHING, "held again while unticked");
    into_photo_mode();
}

/* The game lets go of the hold (a fade's end): taken again, and said at
 * most once a second. */
static void retake(void)
{
    int i, asked = 0;
    into_photo_mode();
    for (i = 0; i < 100; i++) {
        s_game.holds &= ~PHOTO_HOLD_OURS;
        asked += frame() == PHOTO_ASK_HOLD;
    }
    CHECK(asked == 100 && photo_mode_holding(), "taken again %d times of 100", asked);
}

/* Input playback: never while it records or plays, and let go when it
 * starts. */
static void playback(void)
{
    int i;
    photo_mode_flip();
    s_game.playback = 1;
    for (i = 0; i < 100; i++) CHECK(frame() == PHOTO_NOTHING, "held while playback plays");
    s_game.playback = 0;
    into_photo_mode();
    s_game.playback = 1;
    CHECK(frame() == PHOTO_ASK_LET_GO && !photo_mode_holding() && !s_hud_held, "held on into playback");
}

/* The areas: from the last point the eye was in one, or the game's eye at
 * the start. */
static void area(void)
{
    const float in[3] = { 1, 2, 3 }, out[3] = { 9, 9, 9 };
    float from[3] = { 0, 0, 0 };
    into_photo_mode();
    CHECK(photo_mode_area(1, out, from) && near3(from, 0, 5, -10), "none yet: from (%g %g %g)",
          from[0], from[1], from[2]);
    CHECK(!photo_mode_area(0, in, from), "made again from a point in an area");
    CHECK(photo_mode_area(1, out, from) && near3(from, 1, 2, 3), "after one: from (%g %g %g)",
          from[0], from[1], from[2]);
    photo_mode_flip();
    frame();
    into_photo_mode();
    CHECK(photo_mode_area(1, out, from) && near3(from, 0, 5, -10), "held again: from (%g %g %g)",
          from[0], from[1], from[2]);
}

/* The mouse, with Mouse look on: asked for while held, a move from before
 * dropped, then the view turned by Mouse sensitivity's steps a count; and
 * off: not asked for, and moves left alone. run.py gives the settings. */
static void mouse(int on)
{
    const double step = 0.02 * 5;               /* freecam.c's FREECAM_MOUSE_STEP_DEG at 5 */
    CHECK(s_naskers == 2, "%d ask for the mouse", s_naskers);
    CHECK(!photo_wants_mouse(), "the mouse wanted before the hold");
    mouse_move(500, 0);
    into_photo_mode();
    frame();
    CHECK(fabs(heading()) < 1e-6, "a move from before the hold turned the view: heading %g", heading());
    CHECK(photo_wants_mouse() == on, "the mouse wanted %d while held", photo_wants_mouse());
    mouse_move(30, -20);                          /* right, and up */
    {
        int dx, dy;                             /* the free camera takes its own */
        nv_window_mouse_take(0, &dx, &dy);
    }
    frame();
    CHECK(fabs(heading() - (on ? 30 * step : 0)) < 1e-3 && fabs(tilt() - (on ? 20 * step : 0)) < 1e-3,
          "the mouse: heading %g, tilt %g", heading(), tilt());
    photo_mode_flip();
    frame();
    CHECK(!photo_wants_mouse(), "the mouse wanted after letting go");
}

/* The game reads an empty pad while photo mode holds the world, and its own
 * otherwise; another camera's draw is left alone. */
static void pad(void)
{
    XBOX_INPUT_STATE st;
    float eye[3] = { 7, 7, 7 }, look[3] = { 8, 8, 8 };
    memset(&st, 0, sizeof st);
    st.dwPacketNumber = 3;
    st.Gamepad.wButtons = XBOX_GAMEPAD_START;
    st.Gamepad.bAnalogButtons[XBOX_BUTTON_A] = 255;
    st.Gamepad.sThumbLX = 100;
    photo_mode_pad(&st);
    CHECK(st.Gamepad.wButtons == XBOX_GAMEPAD_START && st.Gamepad.sThumbLX == 100, "emptied while off");
    into_photo_mode();
    photo_mode_pad(&st);
    CHECK(st.dwPacketNumber == 3 && !st.Gamepad.wButtons && !st.Gamepad.bAnalogButtons[XBOX_BUTTON_A]
          && !st.Gamepad.sThumbLX, "not emptied while held");
    CHECK(!photo_mode_place(OTHER_CAM, eye, look) && eye[0] == 7 && look[0] == 8, "another camera placed");
}

int main(int argc, char **argv)
{
    const char *c = argc == 2 ? argv[1] : "";
    if (!strcmp(c, "invert")) {
        s_key[0] = "invert_x"; s_val[0] = 1;
        s_key[1] = "invert_y"; s_val[1] = 1;
        s_nkeys = 2;
    } else if (!strcmp(c, "sensitivity")) {
        s_key[0] = "sensitivity_x"; s_val[0] = 10;
        s_key[1] = "sensitivity_y"; s_val[1] = 10;
        s_nkeys = 2;
    } else if (!strcmp(c, "deadzone")) {
        s_key[0] = "deadzone"; s_val[0] = 0.5;
        s_nkeys = 1;
    } else if (!strcmp(c, "mouse_off")) {
        s_key[0] = "mouse"; s_val[0] = 0;
        s_nkeys = 1;
    }
    freecam_start();
    photo_mode_start();
    if (!strcmp(c, "off"))              start(0);
    else if (!strcmp(c, "start"))       start(1);
    else if (!strcmp(c, "start_bad"))   start(0);
    else if (!strcmp(c, "wait"))        wait();
    else if (!strcmp(c, "fly"))         fly();
    else if (!strcmp(c, "turn"))        turn(0, 5);
    else if (!strcmp(c, "invert"))      turn(1, 5);
    else if (!strcmp(c, "sensitivity")) turn(0, 10);
    else if (!strcmp(c, "deadzone"))    deadzone();
    else if (!strcmp(c, "pitch"))       pitch();
    else if (!strcmp(c, "portal"))      portal();
    else if (!strcmp(c, "portal_once")) portal_once();
    else if (!strcmp(c, "back"))        back();
    else if (!strcmp(c, "untick"))      untick();
    else if (!strcmp(c, "retake"))      retake();
    else if (!strcmp(c, "playback"))    playback();
    else if (!strcmp(c, "area"))        area();
    else if (!strcmp(c, "mouse"))       mouse(1);
    else if (!strcmp(c, "mouse_off"))   mouse(0);
    else if (!strcmp(c, "pad"))         pad();
    else { printf("no case %s\n", c); return 2; }
    if (g_bad) return 1;
    printf("photo_mode %s: pass\n", c);
    return 0;
}
