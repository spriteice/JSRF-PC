/* Photo mode (photo_mode.h): Developer > Missions > Photo mode.
 *
 * It waits, holds and lets go:
 *
 *   off       not ticked, or input playback records or plays
 *   waiting   ticked: for the player to have control, PHOTO_SETTLE_FRAMES
 *             game frames in a row with none of the game's holds on and the
 *             follow camera working out the player's view (photo_mode_followed)
 *   holding   from asking the game for its hold until asking it to let go:
 *             the HUD hidden, the camera flown, the pads kept from the game
 *
 * The flight keeps an eye and two angles: yaw, the way the view faces
 * (left-handed, y up, heading atan2(x, z), as the free camera's), and pitch,
 * how far it tilts up. They start from the game's camera's own eye and look
 * point, at the first draw after the hold (photo_mode_place), and Y puts them
 * back there. Each game frame, while holding, the pads and the mouse move
 * them: the sticks past Input > Free camera's deadzone, the turn by its
 * tuning (freecam_turn), the moves at PHOTO_SPEED units a frame at full
 * tilt, along the view for the left stick and straight up or down for the
 * triggers. Nothing stops it: no wall test, no floor. The look point drawn is
 * PHOTO_LOOK_AHEAD ahead of the eye (photo_mode.h). Where the map joins two
 * of its parts by a portal, the eye's step each frame is tested against it
 * as the game tests a skater's (sub_0006F8C0), and the eye moved by its
 * offset as the game moves a skater (sub_00080340); Y still goes back to
 * where photo mode started.
 *
 * The mouse: the window holds the pointer for photo mode while it holds the
 * world and Mouse look is on (nv_window_mouse_look), and counts its moves for
 * photo mode apart from the free camera's, which photo mode takes once a game
 * frame; a move made before the hold is dropped as it starts, so the view
 * doesn't jump. */
#define _POSIX_C_SOURCE 200112L    /* unsetenv */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freecam.h"
#include "pad.h"
#include "photo_mode.h"

/* nv2a_gl.c: Display > HUD > Disable HUD's hiding, for photo mode's hold,
 * leaving that row's own setting as it is. */
extern void nv2a_hud_hold(int on);

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: the mouse's look, as the free camera has it. */
extern int  nv_window_mouse_look(int (*want)(void));
extern int  nv_window_mouse_take(int asker, int *dx, int *dy);
#else
/* Without the SDL window (a headless build): no mouse. */
static int nv_window_mouse_take(int asker, int *dx, int *dy)
{
    (void)asker;
    *dx = *dy = 0;
    return 0;
}
#endif

/* How far the view tilts up or down, in degrees: the view is built from an
 * eye, a look point and an up direction, which turns over at straight up or
 * down; this stays short of it. */
#define PHOTO_PITCH_MAX_DEG 89.0
#define PHOTO_PADS          4
#define PHOTO_STICK_FULL    32767.0
#define PHOTO_TRIGGER_FULL  255.0
/* The log says a repeated line at most once in this many game frames: a
 * second. */
#define PHOTO_LOG_EVERY     60

enum { PHOTO_OFF, PHOTO_WAITING, PHOTO_HOLDS };
static const char *const k_state_names[] = { "off", "waiting for control", "holding the world" };

static const double k_rad = 3.14159265358979323846 / 180.0;

static volatile int s_ticked;       /* the row's tick, flipped on the window's thread */
static volatile int s_mouse_wanted; /* holding, with Mouse look on: the window asks it */
static int s_mouse_asker = -1;      /* photo mode's number with the window, for its own counts */
static int s_state = PHOTO_OFF;
static int s_followed;              /* the follow camera worked out the player's view since the last frame */
static uint32_t s_follow_cam;       /* through this camera */
static int s_settled;               /* frames in a row the player has had control */
static uint32_t s_cam;              /* the camera photo mode places while it holds */
static int s_started;               /* the flight taken from the game's view */
static float s_game_eye[3], s_game_look[3];
static double s_eye[3], s_yaw, s_pitch;
static double s_start_eye[3], s_start_yaw, s_start_pitch;
static int s_y_held;                /* Y, as last read: a press goes back once */
static float s_area_point[3];       /* the last point the eye was in an area */
static int s_area_known;
static unsigned long s_said_retake; /* the frame the log may say it again */
static unsigned long s_frames;

static void take_env(const char *name)
{
#if defined(_WIN32)
    _putenv_s(name, "");                /* "" takes it out */
#else
    unsetenv(name);
#endif
}

#if defined(NV2A_GL_USE_SDL)
/* Asked by the window as it pumps its events (it adds: and the in-game menu
 * is shut, and it has focus). */
static int photo_mouse_wanted(void)
{
    return s_mouse_wanted;
}
#endif

void photo_mode_start(void)
{
    const char *e = getenv(PHOTO_START_VAR);
    s_ticked = e && !strcmp(e, "1");
    if (e && *e && !s_ticked)
        fprintf(stderr, "[PHOTO] %s=%s isn't 1; photo mode off\n", PHOTO_START_VAR, e);
    fprintf(stderr, "[PHOTO] photo mode %s at start%s\n", s_ticked ? "ticked" : "off",
            s_ticked ? ", carried over a pick in Developer > Missions" : "");
    if (e) take_env(PHOTO_START_VAR);
#if defined(NV2A_GL_USE_SDL)
    s_mouse_asker = nv_window_mouse_look(photo_mouse_wanted);
#endif
}

int photo_mode_ticked(void)
{
    return s_ticked;
}

void photo_mode_flip(void)
{
    s_ticked = !s_ticked;
    fprintf(stderr, "[PHOTO] Developer > Missions > Photo mode %s\n", s_ticked ? "ticked" : "unticked");
}

void photo_mode_followed(uint32_t cam)
{
    s_followed = 1;
    s_follow_cam = cam;
}

int photo_mode_holding(void)
{
    return s_state == PHOTO_HOLDS;
}

static void set_state(int state, const char *why)
{
    if (state == s_state) return;
    fprintf(stderr, "[PHOTO] frame %lu: %s (%s)\n", s_frames, k_state_names[state], why);
    s_state = state;
}

/* The view's facing, from the angles. */
static void facing(double f[3])
{
    double cp = cos(s_pitch);
    f[0] = cp * sin(s_yaw);
    f[1] = sin(s_pitch);
    f[2] = cp * cos(s_yaw);
}

/* The pads, as the game would read them: the farthest-pushed of each stick,
 * -1..1 past the deadzone, the most-pulled of each trigger, 0..1, and White
 * (R1), Black (L1) and Y held on any. */
typedef struct {
    double lx, ly, rx, ry, lt, rt;
    int fast, slow, back;
} PhotoPads;

static void read_pads(const FREECAM_SETTINGS *c, PhotoPads *r)
{
    double lbest = -1, rbest = -1;
    XBOX_INPUT_STATE st;
    uint32_t p;
    memset(r, 0, sizeof *r);
    for (p = 0; p < PHOTO_PADS; p++) {
        const XBOX_GAMEPAD *g = &st.Gamepad;
        double x, y, t;
        if (!pad_read(p, &st)) continue;
        x = g->sThumbLX / PHOTO_STICK_FULL; y = g->sThumbLY / PHOTO_STICK_FULL;
        if (x * x + y * y > lbest) { lbest = x * x + y * y; r->lx = x; r->ly = y; }
        x = g->sThumbRX / PHOTO_STICK_FULL; y = g->sThumbRY / PHOTO_STICK_FULL;
        if (x * x + y * y > rbest) { rbest = x * x + y * y; r->rx = x; r->ry = y; }
        t = g->bAnalogButtons[XBOX_BUTTON_LTRIGGER] / PHOTO_TRIGGER_FULL;
        if (t > r->lt) r->lt = t;
        t = g->bAnalogButtons[XBOX_BUTTON_RTRIGGER] / PHOTO_TRIGGER_FULL;
        if (t > r->rt) r->rt = t;
        r->fast |= g->bAnalogButtons[XBOX_BUTTON_WHITE] > XBOX_ANALOG_BUTTON_THRESHOLD;
        r->slow |= g->bAnalogButtons[XBOX_BUTTON_BLACK] > XBOX_ANALOG_BUTTON_THRESHOLD;
        r->back |= g->bAnalogButtons[XBOX_BUTTON_Y] > XBOX_ANALOG_BUTTON_THRESHOLD;
    }
    freecam_deadzone(c, &r->lx, &r->ly);
    freecam_deadzone(c, &r->rx, &r->ry);
}

static void sub3(double r[3], const double a[3], const double b[3])
{
    int k;
    for (k = 0; k < 3; k++) r[k] = a[k] - b[k];
}

static double dot3(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross3(double r[3], const double a[3], const double b[3])
{
    r[0] = a[1] * b[2] - a[2] * b[1];
    r[1] = a[2] * b[0] - a[0] * b[2];
    r[2] = a[0] * b[1] - a[1] * b[0];
}

/* Whether the step from `from` to `to` crosses a portal's triangle (corners
 * A, B, C) as the game's test has it (sub_0006F8C0): against its face, whose
 * normal is (B - A) x (C - B); its line meeting the plane within the step;
 * and there, inside the triangle or on its edge. */
static int crosses(const float t[3][3], const double from[3], const double to[3])
{
    double v[3][3], e1[3], e2[3], n[3], d[3], p[3], a[3], b[3], c[3], dn, s;
    int i, k;
    for (i = 0; i < 3; i++)
        for (k = 0; k < 3; k++) v[i][k] = t[i][k];
    sub3(e1, v[1], v[0]);
    sub3(e2, v[2], v[1]);
    cross3(n, e1, e2);
    sub3(d, to, from);
    dn = dot3(d, n);
    if (!(dn < 0)) return 0;                /* along its face, or not against it */
    sub3(a, v[0], from);
    s = dot3(a, n) / dn;                    /* how far along the step it meets the plane */
    if (s < 0 || s > 1) return 0;
    for (k = 0; k < 3; k++) p[k] = from[k] + d[k] * s;
    for (i = 0; i < 3; i++) {
        sub3(a, v[i], p);
        sub3(b, v[(i + 1) % 3], p);
        cross3(c, a, b);
        if (dot3(c, n) < 0) return 0;
    }
    return 1;
}

/* The eye's step from `from` through the map's portals, as the game moves a
 * skater (sub_00080340): the first portal it crosses, by either triangle,
 * moves the eye by its offset, which the game skips when the offset is
 * none; once a frame at most. */
static void through_portals(const PHOTO_GAME *g, const double from[3])
{
    int i, k;
    for (i = 0; i < g->nportals; i++) {
        const PHOTO_PORTAL *p = &g->portals[i];
        if (!crosses(p->tri[0], from, s_eye) && !crosses(p->tri[1], from, s_eye)) continue;
        if (!p->by[0] && !p->by[1] && !p->by[2]) return;
        for (k = 0; k < 3; k++) s_eye[k] += p->by[k];
        fprintf(stderr, "[PHOTO] frame %lu: through portal %d of %d, the camera moved by (%.1f %.1f %.1f)\n",
                s_frames, i + 1, g->nportals, p->by[0], p->by[1], p->by[2]);
        return;
    }
}

/* A game frame of the flight, while holding. */
static void fly(const PHOTO_GAME *g)
{
    const double pmax = PHOTO_PITCH_MAX_DEG * k_rad;
    FREECAM_SETTINGS c;
    PhotoPads r;
    double f[3], from[3], right, up, speed;
    int dx, dy, k;
    freecam_settings(&c);
    s_mouse_wanted = c.mouse;
    nv_window_mouse_take(s_mouse_asker, &dx, &dy);
    dy = -dy;                               /* the window's y points down */
    if (!c.mouse) dx = dy = 0;
    read_pads(&c, &r);
    if (!s_started) {                       /* not drawn yet: nothing to move */
        s_y_held = r.back;
        return;
    }
    if (r.back && !s_y_held) {
        memcpy(s_eye, s_start_eye, sizeof s_eye);
        s_yaw = s_start_yaw;
        s_pitch = s_start_pitch;
        fprintf(stderr, "[PHOTO] frame %lu: Y, back to where photo mode started\n", s_frames);
    }
    s_y_held = r.back;
    freecam_turn(&c, r.rx, r.ry, dx, dy, &right, &up);
    s_yaw = remainder(s_yaw + right, 2 * 180.0 * k_rad);
    s_pitch += up;
    if (s_pitch > pmax) s_pitch = pmax;
    if (s_pitch < -pmax) s_pitch = -pmax;
    speed = PHOTO_SPEED * (r.fast ? PHOTO_FAST : 1.0) * (r.slow ? PHOTO_SLOW : 1.0);
    facing(f);
    memcpy(from, s_eye, sizeof from);
    /* Forward along the view, right square to it on the level, and the
     * triggers straight up and down. */
    for (k = 0; k < 3; k++) s_eye[k] += f[k] * r.ly * speed;
    s_eye[0] += cos(s_yaw) * r.lx * speed;
    s_eye[2] -= sin(s_yaw) * r.lx * speed;
    s_eye[1] += (r.rt - r.lt) * speed;
    through_portals(g, from);
}

int photo_mode_frame(const PHOTO_GAME *g)
{
    int ticked = s_ticked, followed = s_followed, dx, dy;
    s_frames++;
    s_followed = 0;
    if (s_state == PHOTO_HOLDS) {
        if (!ticked || g->playback) {
            set_state(PHOTO_OFF, !ticked ? "unticked: the world goes on"
                      : "input playback started: the world goes on");
            s_mouse_wanted = 0;
            s_settled = 0;
            nv2a_hud_hold(0);
            return PHOTO_ASK_LET_GO;
        }
        fly(g);
        if (!(g->holds & PHOTO_HOLD_OURS)) {
            if (s_frames >= s_said_retake) {
                s_said_retake = s_frames + PHOTO_LOG_EVERY;
                fprintf(stderr, "[PHOTO] frame %lu: the game let go of the hold; taking it again\n", s_frames);
            }
            return PHOTO_ASK_HOLD;
        }
        return PHOTO_NOTHING;
    }
    if (!ticked || g->playback) {
        set_state(PHOTO_OFF, !ticked ? "unticked" : "input playback records or plays");
        s_settled = 0;
        return PHOTO_NOTHING;
    }
    set_state(PHOTO_WAITING, "ticked");
    if (g->holds || !followed) s_settled = 0;
    else s_settled++;
    if (s_settled < PHOTO_SETTLE_FRAMES) return PHOTO_NOTHING;
    s_cam = s_follow_cam;
    s_started = 0;
    s_area_known = 0;
    s_settled = 0;
    nv_window_mouse_take(s_mouse_asker, &dx, &dy);  /* a move from before is dropped */
    nv2a_hud_hold(1);
    fprintf(stderr, "[PHOTO] frame %lu: camera %08X flies, the HUD hidden\n", s_frames, s_cam);
    fprintf(stderr, "[PHOTO] frame %lu: the map has %d portal%s\n", s_frames, g->nportals,
            g->nportals == 1 ? "" : "s");
    set_state(PHOTO_HOLDS, "the player has control");
    return PHOTO_ASK_HOLD;
}

int photo_mode_place(uint32_t cam, float eye[3], float look[3])
{
    double f[3], d[3], n;
    int k;
    if (s_state != PHOTO_HOLDS || cam != s_cam) return 0;
    if (!s_started) {
        memcpy(s_game_eye, eye, sizeof s_game_eye);
        memcpy(s_game_look, look, sizeof s_game_look);
        for (k = 0; k < 3; k++) { s_eye[k] = eye[k]; d[k] = (double)look[k] - eye[k]; }
        n = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        s_yaw = n > 0 ? atan2(d[0], d[2]) : 0;
        s_pitch = n > 0 ? asin(d[1] / n) : 0;
        memcpy(s_start_eye, s_eye, sizeof s_start_eye);
        s_start_yaw = s_yaw;
        s_start_pitch = s_pitch;
        s_started = 1;
        fprintf(stderr, "[PHOTO] frame %lu: from the game's view at (%.1f %.1f %.1f), heading %.0f, pitch %.0f\n",
                s_frames, s_eye[0], s_eye[1], s_eye[2], s_yaw / k_rad, s_pitch / k_rad);
    }
    facing(f);
    for (k = 0; k < 3; k++) {
        eye[k] = (float)s_eye[k];
        look[k] = (float)(s_eye[k] + f[k] * PHOTO_LOOK_AHEAD);
    }
    return 1;
}

int photo_mode_game_view(uint32_t *cam, float eye[3], float look[3])
{
    if (!s_started) return 0;
    *cam = s_cam;
    memcpy(eye, s_game_eye, sizeof s_game_eye);
    memcpy(look, s_game_look, sizeof s_game_look);
    s_started = 0;
    return 1;
}

int photo_mode_area(int empty, const float eye[3], float from[3])
{
    static unsigned long said;
    if (!empty) {
        memcpy(s_area_point, eye, sizeof s_area_point);
        s_area_known = 1;
        return 0;
    }
    if (!s_area_known) {                    /* from the game's own eye, where it started */
        memcpy(s_area_point, s_game_eye, sizeof s_area_point);
        s_area_known = 1;
    }
    memcpy(from, s_area_point, sizeof s_area_point);
    if (s_frames >= said) {
        said = s_frames + PHOTO_LOG_EVERY;
        fprintf(stderr, "[PHOTO] frame %lu: the eye at (%.1f %.1f %.1f) is in no area; the areas drawn are "
                "those from (%.1f %.1f %.1f), where it last was in one\n",
                s_frames, eye[0], eye[1], eye[2], from[0], from[1], from[2]);
    }
    return 1;
}

void photo_mode_pad(XBOX_INPUT_STATE *st)
{
    if (s_state != PHOTO_HOLDS) return;
    memset(&st->Gamepad, 0, sizeof st->Gamepad);
}
