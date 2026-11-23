/* The free camera (freecam.h): freecam=1 in the settings file, or F8.
 *
 * The follow camera keeps what it works out for itself -- what it looks at,
 * how far it stays, where a wall stops it -- and freecam only moves the eye
 * around that target, by the right stick or the mouse. The follow camera's
 * hook then keeps that eye's view out of walls, and, with No skater fade on
 * (off by default; camera_walls.h), the eye no nearer the player than the
 * game draws them whole, as the game fades your skater out when its camera's
 * eye comes nearer. While
 * that eye is in none of the map's areas, the areas drawn are the game's own
 * eye's, and your skater is drawn while in none itself (recomp_manual.c,
 * sub_0006A970 and sub_0006AC10). Two angles say where:
 * yaw, the way the view faces, and pitch, how far the eye is above (or
 * below) the target.
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
 * once. While freecam is on, talking to someone does not turn the camera to
 * them.
 *
 * The game's own shots -- the graffiti shot, the loop's and the half pipe's
 * (camera_scenes.h) -- are the game's to frame. Out of one, the view glides
 * from the shot's last view to the free camera's, as far from the game's
 * own as the player had it as the shot started, over FREECAM_BACK_FRAMES:
 * what it looks at, the way it faces, how far and how high, each smoothly,
 * none of the way at the start and all of it at the end. Into every shot
 * it cuts, as the game's camera does (spriteice, 22 November 2026: "the
 * transition in for the graffiti cam in free cam mode has to go ... the
 * transition out stays"); only as recordings made before play does it
 * glide into the graffiti shot, from the free camera's last view to the
 * shot as the game frames it, over FREECAM_INTO_FRAMES (the card "Graffiti
 * in and out") or FREECAM_INTO_FRAMES_BEFORE. The stick and the mouse steer
 * where it glides to. The glide is the picture's alone: the controls follow the view it is headed to
 * (freecam_ahead), so the skater keeps going the way the stick sends it, as
 * through the game's own cut to a shot and back.
 *
 * The stick is used as it comes: past the deadzone the turn per frame is in
 * proportion to how far it is pushed, with nothing carried from one frame to
 * the next -- no acceleration, no smoothing. With auto centre, a stick let go
 * for a while lets the view ease back to the game's own, behind the player,
 * and keep following it there until the stick is pushed: how long it waits
 * and how fast it eases are the level's (k_centre).
 *
 * The mouse turns it too, while Mouse look is on: the window holds the
 * pointer while freecam is on and the in-game menu is shut
 * (nv_window_mouse_look), and freecam takes how far it moved once a game
 * frame, counted for freecam apart from anyone else who asks, which input
 * playback records and plays back as it does the stick (freecam_frame). Its
 * turn is in proportion to the mouse's counts, with nothing else -- no
 * acceleration, no smoothing, and the frame rate plays no part -- and adds
 * to the stick's. Invert X and Y turn both, and moving it
 * counts as pushing the stick for auto centre. A move in a frame freecam
 * doesn't steer (paused, a scene) is dropped. The mouse is player 1's, as the
 * keyboard is (port 0, xinput_device.c's kb_merge): with a free camera for
 * each player, it would turn port 0's alone. A pad's button flips freecam,
 * as F8 does: Input > Controller layout's Toggle free camera row, R3 by
 * default (src/input/input_remap.c), with none for the game's own Right
 * stick click, which costs it little: the game reads R3 only as one screen's
 * "any button" (0x7DE20, 0x7DED0). The settings are read once, at start
 * (freecam_start); F8, the pad's button and the menu's rows change them on
 * the window's thread, and freecam takes up the change at the start of the
 * game's next frame (freecam_frame), so that input playback can record it
 * by the frame and play it back there (port/playback.c):
 *
 *   freecam        0 / 1    on at start (F8 and the pad's button flip it,
 *                           and keep it here)
 *   autocentre     0 - 4    off, mild, medium, high, aggressive
 *   invert_x       0 / 1    0: right turns the view right
 *   invert_y       0 / 1    0: up tilts the view up
 *   sensitivity_x  1 - 10   36 degrees a second at full tilt per step
 *   sensitivity_y  1 - 10
 *   deadzone       0 - 1    0.10 = 10%, the default, measured on the stick's
 *                           distance from centre, and the speed rescaled from 0
 *                           at its edge
 *   mouse          0 / 1    1, the default: the mouse turns it too
 *   mouse_sensitivity
 *                  1 - 10   0.02 degrees a count of the mouse per step
 *
 * A key that is missing takes its default; one that is out of range, or not
 * a number, takes its default and says so in the log. */
#include <math.h>
#include <stdio.h>
#include "freecam.h"
#include "input_remap.h"
#include "playback.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

/* Degrees per game frame, at full tilt, for each step of sensitivity: 36 a
 * second at the game's 60 frames a second. */
#define FREECAM_STEP_DEG 0.6
/* Degrees per count of the mouse, for each step of its sensitivity (the
 * same steps): 5 turns 0.1 degrees a count, a full turn in 3,600 counts,
 * about 11 cm of mouse at 800 counts an inch. */
#define FREECAM_MOUSE_STEP_DEG 0.02
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
_Static_assert(sizeof k_centre / sizeof k_centre[0] == FREECAM_CENTRE_LEVELS, "freecam.h's levels");
_Static_assert(sizeof k_centre_names / sizeof k_centre_names[0] == FREECAM_CENTRE_LEVELS,
               "a name for each level");

static const double k_rad = 3.14159265358979323846 / 180.0;

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file the engine already keeps, a
 * number kept there, and a key the game binds for itself. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
extern void nv_window_hotkey(const char *name, void (*run)(void));
/* And the mouse's look: who wants the mouse's moves, and the moves. */
extern int  nv_window_mouse_look(int (*want)(void));
extern int  nv_window_mouse_take(int asker, int *dx, int *dy);
#else
/* Without the SDL window (a headless build) there is no settings file: no
 * keys. And no mouse. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
static int nv_window_mouse_take(int asker, int *dx, int *dy)
{
    (void)asker;
    *dx = *dy = 0;
    return 0;
}
#endif

/* With no keys in the settings: off (the game's own camera), neither way
 * inverted, auto centre off, 5 either way, a 10% deadzone (spriteice, 22
 * November 2026, from 15%), and the mouse on at 5. */
#define FREECAM_ON_DEFAULT         0
#define FREECAM_INVERT_DEFAULT     0
#define FREECAM_CENTRE_DEFAULT     0
#define FREECAM_SENS_DEFAULT       5.0
#define FREECAM_DEADZONE_DEFAULT   0.10
#define FREECAM_MOUSE_DEFAULT      1
#define FREECAM_MOUSE_SENS_DEFAULT 5.0
/* The player's: the settings file's, then as F8, the pad's button and the
 * menu's rows change them, on the window's thread. */
static volatile FREECAM_SETTINGS s_asked = {
    FREECAM_ON_DEFAULT, FREECAM_INVERT_DEFAULT, FREECAM_INVERT_DEFAULT, FREECAM_CENTRE_DEFAULT,
    FREECAM_SENS_DEFAULT, FREECAM_SENS_DEFAULT, FREECAM_DEADZONE_DEFAULT,
    FREECAM_MOUSE_DEFAULT, FREECAM_MOUSE_SENS_DEFAULT };
/* What freecam goes by: the player's as freecam_frame last took them, or a
 * recording's. */
static FREECAM_SETTINGS s_cfg;
static int s_played;            /* s_cfg is a recording's */
static volatile int s_steer;    /* on, and the follow camera ran last frame */
static volatile int s_mouse_on; /* on, with Mouse look: the window asks it */
static int s_mouse_asker = -1;  /* freecam's number with the window, for its own counts */
static int s_mouse[2];          /* the mouse's move for this frame, counts right and up */
static int s_follow_ran;        /* the follow camera ran since freecam_frame */
static int s_cam_ran;           /* and s_cam's update did */
static int s_held;              /* s_cam's update did not run last frame */
static int s_idle;              /* frames the stick has been let go, up to the level's wait */
static short s_stick[4][2];     /* each port's right stick, as last read */
static uint32_t s_cam;          /* the camera yaw and pitch are for; 0: start them again */
static double s_yaw, s_pitch;   /* radians */
static double s_gyaw, s_gpitch; /* the game's own view, as freecam_orbit last saw it */
/* A game's own shot (freecam_shot): the camera that was steered as it
 * started, and how far the view was from the game's own then (radians), to
 * ease back to once the follow camera runs it again. */
static uint32_t s_kept_cam;
static double s_kept_yaw, s_kept_pitch;
/* The camera update's word on a camera, this frame's: s_said_cam, and a
 * shot playing on it (1) or none (-1); its post-update takes it up
 * (freecam_ran). */
static uint32_t s_said_cam;
static int s_said;
/* The ease back, as camera3's recordings play: frames into it, of
 * FREECAM_BACK_FRAMES_BEFORE; 0: none. */
static int s_back;
/* A glide (freecam_view): the camera, into the graffiti shot (1) or out of
 * one of the game's shots (0), frames into it of s_glide_len (0: none), its
 * follow camera's hook stepped it this frame, whether the controls follow
 * where it is headed (freecam_ahead), and the view it started from. */
static uint32_t s_glide_cam;
static int s_glide, s_glide_len, s_glide_in, s_glide_stepped, s_glide_ahead;
static double s_from_eye[3], s_from_tgt[3];
/* The view this frame's glide step was headed to, of s_ahead_cam (0:
 * none this frame), for freecam_ahead. */
static uint32_t s_ahead_cam;
static float s_ahead_eye[3], s_ahead_tgt[3];
/* The view each post-update showed (freecam_shown), of a camera freecam
 * placed, glides or keeps the view of: the camera, whether freecam placed it
 * (freecam_orbit, s_placed_cam until then), and the view. */
static uint32_t s_shown_cam, s_placed_cam;
static int s_shown_free;
static double s_shown_eye[3], s_shown_tgt[3];

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

/* The switch and tuning, to the log: why is where they came from. */
static void say(const char *why, const FREECAM_SETTINGS *c)
{
    fprintf(stderr, "[FREECAM] %s (%s); invert x %d y %d, sensitivity x %g y %g, deadzone %g, auto centre %s,"
            " mouse look %s at %g\n", c->on ? "on" : "off", why, c->invert_x, c->invert_y, c->sens_x, c->sens_y,
            c->deadzone, k_centre_names[c->centre], c->mouse ? "on" : "off", c->mouse_sens);
}

#if defined(NV2A_GL_USE_SDL)
/* F8, the pad's button (Toggle free camera) and the menu's Free camera row:
 * on or off, kept in the settings file as freecam=. On the window's thread;
 * freecam_frame takes it up. */
static void freecam_flip(void)
{
    s_asked.on = !s_asked.on;
    fprintf(stderr, "[FREECAM] %s (F8, the pad or the menu)\n", s_asked.on ? "on" : "off");
    nv_window_pref_set_num("freecam", s_asked.on);
}
#endif

#if defined(XBOXRECOMP_MENU)
static void freecam_menu(void);
#endif

#if defined(NV2A_GL_USE_SDL)
/* Asked by the window as it pumps its events: the mouse's moves are wanted
 * while freecam and its Mouse look are on, the player's or a recording's (the
 * window adds: and the in-game menu is shut, and it has focus). */
static int freecam_mouse_wanted(void)
{
    return s_mouse_on;
}
#endif

void freecam_start(void)
{
    s_asked.on = cfg_num("freecam", s_asked.on, -HUGE_VAL, HUGE_VAL) != 0;
    s_asked.invert_x = cfg_num("invert_x", s_asked.invert_x, -HUGE_VAL, HUGE_VAL) != 0;
    s_asked.invert_y = cfg_num("invert_y", s_asked.invert_y, -HUGE_VAL, HUGE_VAL) != 0;
    s_asked.sens_x = cfg_num("sensitivity_x", s_asked.sens_x, FREECAM_SENS_MIN, FREECAM_SENS_MAX);
    s_asked.sens_y = cfg_num("sensitivity_y", s_asked.sens_y, FREECAM_SENS_MIN, FREECAM_SENS_MAX);
    s_asked.deadzone = cfg_num("deadzone", s_asked.deadzone, 0, 1);
    s_asked.centre = (int)cfg_num("autocentre", s_asked.centre, 0, FREECAM_CENTRE_LEVELS - 1);
    s_asked.mouse = cfg_num("mouse", s_asked.mouse, -HUGE_VAL, HUGE_VAL) != 0;
    s_asked.mouse_sens = cfg_num("mouse_sensitivity", s_asked.mouse_sens, FREECAM_SENS_MIN, FREECAM_SENS_MAX);
    s_cfg = s_asked;
    s_mouse_on = s_cfg.on && s_cfg.mouse;
    say("settings", &s_cfg);
#if defined(XBOXRECOMP_MENU)
    freecam_menu();
#endif
#if defined(NV2A_GL_USE_SDL)
    nv_window_hotkey("F8", freecam_flip);
    input_remap_pad_hotkey(INPUT_CTL_FREECAM, freecam_flip);
    s_mouse_asker = nv_window_mouse_look(freecam_mouse_wanted);
#endif
}

void freecam_frame(void)
{
    FREECAM_SETTINGS c = s_asked;
    int played = playback_freecam(&c), steer, held, mouse, dx, dy;
    if (!played && s_played) say("yours again, as playback ended", &c);
    if (c.on != s_cfg.on) {                         /* on or off: from the game's view next time */
        s_cam = s_kept_cam = 0;
        s_glide = 0;
    }
    s_cfg = c;
    s_played = played;
    s_mouse_on = s_cfg.on && s_cfg.mouse;
    steer = s_cfg.on && s_follow_ran;
    held = s_cfg.on && s_cam && !s_follow_ran && !s_cam_ran;
    /* The mouse's move since the last frame, right and up (the window's y
     * points down), for the follow camera to turn by this frame: only while
     * it steers with Mouse look on, else dropped. Recorded, or the
     * recording's in its place. */
    nv_window_mouse_take(s_mouse_asker, &dx, &dy);
    dy = -dy;
    mouse = steer && s_cfg.mouse;
    if (!mouse) dx = dy = 0;
    playback_mouse(&dx, &dy);
    s_mouse[0] = mouse ? dx : 0;
    s_mouse[1] = mouse ? dy : 0;
    if (steer != s_steer || held != s_held)
        fprintf(stderr, "[FREECAM] %s\n", steer ? "steering the camera"
                : held ? "held: its camera did not run; keeping the view"
                : s_cfg.on ? "idle: the follow camera is not running" : "idle");
    if (!s_follow_ran && s_cam_ran) s_cam = 0;      /* the game has it, in a mode of its own */
    s_steer = steer;
    s_held = held;
    s_follow_ran = s_cam_ran = 0;
}

void freecam_ran(uint32_t cam)
{
    int said = cam && cam == s_said_cam ? s_said : 0;
    if (cam == s_said_cam) s_said_cam = 0;
    if (!cam) return;
    if (cam == s_cam) s_cam_ran = 1;
    /* As one of the game's shots starts (the update that says so put the
     * camera in the shot's mode, so freecam_orbit did not run), the view
     * is kept, to ease back to. Kept, a post-update with no shot playing
     * lets it go: another mode of the game's, or a scene, whose update
     * says nothing. Had the follow camera run in it instead, freecam_orbit
     * would have taken the view up, and let it go, already. */
    if (cam == s_cam && said > 0 && s_cfg.on) {
        s_kept_cam = s_cam;
        s_kept_yaw = remainder(s_yaw - s_gyaw, 2 * 180.0 * k_rad);
        s_kept_pitch = s_pitch - s_gpitch;
        fprintf(stderr, "[FREECAM] the game's shot on camera %08X: heading %.0f, pitch %.0f"
                " from the game's view kept, to ease back to\n",
                s_cam, s_kept_yaw / k_rad, s_kept_pitch / k_rad);
    } else if (cam == s_kept_cam && said <= 0)
        s_kept_cam = 0;
}

void freecam_shot(uint32_t cam, int playing)
{
    if (!cam) return;
    s_said_cam = cam;
    s_said = playing ? 1 : -1;
}

void freecam_shown(uint32_t cam, const float eye[3], const float tgt[3])
{
    int k;
    if (!cam) return;
    if (cam == s_glide_cam) {
        if (s_glide && !s_glide_stepped) {
            fprintf(stderr, "[FREECAM] the glide %s the game's shot on camera %08X let go: its follow camera"
                    " did not run\n", s_glide_in ? "into" : "out of", cam);
            s_glide = 0;
        }
        s_glide_stepped = 0;
    }
    if (cam == s_ahead_cam) s_ahead_cam = 0;
    if (cam != s_placed_cam && cam != s_glide_cam && cam != s_kept_cam) return;
    s_shown_cam = cam;
    s_shown_free = cam == s_placed_cam;
    if (cam == s_placed_cam) s_placed_cam = 0;
    for (k = 0; k < 3; k++) {
        s_shown_eye[k] = eye[k];
        s_shown_tgt[k] = tgt[k];
    }
}

/* A glide from the view shown last (s_shown_), into the graffiti shot (in)
 * or out of one of the game's shots, as back has it (playback_freecam_back):
 * quicker, the controls following where it is headed, or as recordings made
 * before had it. */
static void glide_start(uint32_t cam, int in, int back)
{
    int k, now = back >= PLAYBACK_BACK_STEADY;
    s_glide_cam = cam;
    s_glide_in = in;
    s_glide = 1;
    s_glide_len = in ? (now ? FREECAM_INTO_FRAMES : FREECAM_INTO_FRAMES_BEFORE)
                     : (now ? FREECAM_BACK_FRAMES : FREECAM_BACK_FRAMES_BEFORE);
    s_glide_ahead = now;
    s_glide_stepped = 0;
    for (k = 0; k < 3; k++) {
        s_from_eye[k] = s_shown_eye[k];
        s_from_tgt[k] = s_shown_tgt[k];
    }
}

/* An eye's way from what it looks at, o = eye - tgt: its heading y, pitch
 * p and distance d, o = d (cos p sin y, sin p, cos p cos y). Returns d, 0
 * for an eye on what it looks at. */
static double glide_way(const double o[3], double *yaw, double *pitch)
{
    double d = sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]), s;
    if (!(d > 0)) return 0;
    s = o[1] / d;
    *yaw = atan2(o[0], o[2]);
    *pitch = asin(s > 1 ? 1 : s < -1 ? -1 : s);
    return d;
}

/* This frame of the glide: from the view it started from to eye and tgt,
 * in place, smoothly (none of the way at the start, all of it at the end,
 * fastest half way): what it looks at straight there, and the eye around
 * it, by the shorter way round, its pitch and its distance. */
static void glide_step(float eye[3], float tgt[3])
{
    double t = (double)s_glide / s_glide_len, k = t * t * (3 - 2 * t);
    double fo[3], to[3], ft[3], fy = 0, fp = 0, ty = 0, tp = 0, fd, td, y, p, d, cp;
    int i;
    if (s_glide_ahead) {
        s_ahead_cam = s_glide_cam;
        for (i = 0; i < 3; i++) {
            s_ahead_eye[i] = eye[i];
            s_ahead_tgt[i] = tgt[i];
        }
    }
    for (i = 0; i < 3; i++) {
        fo[i] = s_from_eye[i] - s_from_tgt[i];
        to[i] = eye[i] - tgt[i];
        ft[i] = s_from_tgt[i] + (tgt[i] - s_from_tgt[i]) * k;
    }
    fd = glide_way(fo, &fy, &fp);
    td = glide_way(to, &ty, &tp);
    if (fd > 0 && td > 0) {
        y = fy + remainder(ty - fy, 2 * 180.0 * k_rad) * k;
        p = fp + (tp - fp) * k;
        d = fd + (td - fd) * k;
        cp = cos(p);
        eye[0] = (float)(ft[0] + d * cp * sin(y));
        eye[1] = (float)(ft[1] + d * sin(p));
        eye[2] = (float)(ft[2] + d * cp * cos(y));
    } else
        for (i = 0; i < 3; i++) eye[i] = (float)(s_from_eye[i] + (eye[i] - s_from_eye[i]) * k);
    for (i = 0; i < 3; i++) tgt[i] = (float)ft[i];
    s_glide_stepped = 1;
    s_glide = s_glide < s_glide_len ? s_glide + 1 : 0;
    if (!s_glide)
        fprintf(stderr, "[FREECAM] glided %s the game's shot\n", s_glide_in ? "into" : "out of");
}

int freecam_view(uint32_t cam, int shot, float eye[3], float tgt[3])
{
    shot = shot != 0;
    if (!cam || !s_cfg.on) return 0;
    if (shot && !(s_glide && s_glide_in && cam == s_glide_cam)) {
        /* The graffiti shot, as it starts: a cut, but as recordings that
         * glided into it play, from the free camera's view, the last
         * frame's. */
        int back = playback_freecam_back();
        if (cam == s_glide_cam) s_glide = 0;
        if (cam != s_shown_cam || !s_shown_free || back < PLAYBACK_BACK_GLIDE || back > PLAYBACK_BACK_STEADY)
            return 0;
        glide_start(cam, 1, back);
        fprintf(stderr, "[FREECAM] gliding into the game's graffiti shot on camera %08X, over %d frames%s\n",
                cam, s_glide_len, s_glide_ahead ? ", the controls following the shot's view" : "");
    }
    if (!s_glide || cam != s_glide_cam || s_glide_in != shot) return 0;
    glide_step(eye, tgt);
    return 1;
}

int freecam_ahead(uint32_t cam, float eye[3], float tgt[3])
{
    int k;
    if (!cam || cam != s_ahead_cam) return 0;
    s_ahead_cam = 0;
    for (k = 0; k < 3; k++) {
        eye[k] = s_ahead_eye[k];
        tgt[k] = s_ahead_tgt[k];
    }
    return 1;
}

void freecam_pad(unsigned port, short *rx, short *ry)
{
    if (!rx) { s_stick[port & 3][0] = s_stick[port & 3][1] = 0; return; }
    s_stick[port & 3][0] = *rx;
    s_stick[port & 3][1] = *ry;
    if (s_steer) *rx = *ry = 0;
}

void freecam_deadzone(const FREECAM_SETTINGS *c, double *x, double *y)
{
    double m = sqrt(*x * *x + *y * *y), k;
    if (m <= c->deadzone || c->deadzone >= 1) { *x = *y = 0; return; }
    k = ((m > 1 ? 1 : m) - c->deadzone) / (1 - c->deadzone) / m;
    *x *= k; *y *= k;
}

void freecam_turn(const FREECAM_SETTINGS *c, double x, double y, double mx, double my,
                  double *right, double *up)
{
    const double step = FREECAM_STEP_DEG * k_rad, mstep = FREECAM_MOUSE_STEP_DEG * k_rad;
    *right = x * c->sens_x * step + mx * c->mouse_sens * mstep;
    *up = y * c->sens_y * step + my * c->mouse_sens * mstep;
    if (c->invert_x) *right = -*right;
    if (c->invert_y) *up = -*up;
}

/* The farthest-pushed right stick, -1..1 on each axis, past the deadzone. */
static void freecam_stick(double *x, double *y)
{
    double best = -1;
    int p;
    *x = *y = 0;
    for (p = 0; p < 4; p++) {
        double sx = s_stick[p][0] / 32767.0, sy = s_stick[p][1] / 32767.0;
        if (sx * sx + sy * sy > best) { best = sx * sx + sy * sy; *x = sx; *y = sy; }
    }
    freecam_deadzone(&s_cfg, x, y);
}

int freecam_orbit(uint32_t cam, uint32_t mode, const float tgt[3], const float seye[3],
                  int reset, float eye[3])
{
    const double pmax = FREECAM_PITCH_MAX_DEG * k_rad;
    double v[3], d, x, y, mx, my, cp, gyaw, gpitch;
    int again = 0;
    s_follow_ran = 1;
    if (!s_cfg.on) return FREECAM_OFF;
    v[0] = seye[0] - tgt[0]; v[1] = seye[1] - tgt[1]; v[2] = seye[2] - tgt[2];
    d = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(d > 0)) return FREECAM_OFF;
    /* The game's own view. Left-handed, y up (as auto_step steers): the view
     * faces from the eye to the target, heading atan2(x, z). */
    gyaw = atan2(-v[0], -v[2]);
    gpitch = asin(v[1] / d);
    if (cam != s_cam || reset) {
        /* From the game's view; after one of its shots, back to where the
         * player had it, gliding there from the shot's view (freecam_view),
         * or as a recording made before plays, easing the angle back from
         * the game's view, or not. */
        int back = !reset && cam == s_kept_cam ? playback_freecam_back() : PLAYBACK_BACK_NONE;
        int glide = back >= PLAYBACK_BACK_GLIDE && cam == s_shown_cam;
        s_yaw = glide ? remainder(gyaw + s_kept_yaw, 2 * 180.0 * k_rad) : gyaw;
        s_pitch = glide ? gpitch + s_kept_pitch : gpitch;
        s_idle = 0;
        s_back = back && !glide ? 1 : 0;
        s_glide = 0;
        if (glide) glide_start(cam, 0, back);
        again = 1;
        fprintf(stderr, "[FREECAM] following camera %08X (mode %u) from heading %.0f, pitch %.0f%s%s\n",
                cam, mode, s_yaw / k_rad, s_pitch / k_rad,
                glide ? ", where it was before the game's shot, gliding there from the shot's view"
                : back ? ", easing back to where it was before the game's shot"
                : cam == s_cam ? ", as the game put it back behind the player" : "",
                glide && s_glide_ahead ? ", the controls following where it glides to" : "");
        s_cam = cam;
        s_kept_cam = 0;
    }
    freecam_stick(&x, &y);
    mx = s_mouse[0];                        /* this frame's, once */
    my = s_mouse[1];
    s_mouse[0] = s_mouse[1] = 0;
    if (x != 0 || y != 0 || mx != 0 || my != 0) {
        s_idle = 0;
        if (s_back) fprintf(stderr, "[FREECAM] easing back: the stick or the mouse took over\n");
        s_back = 0;                         /* the player's again, from where it is */
    } else if (s_idle < k_centre[s_cfg.centre].wait) s_idle++;
    if (s_back) {
        /* From the game's view to the kept one, as far from it as the view
         * was, smoothly: none of the way at the start, all of it at the
         * end, fastest half way. Only as camera3's recordings play, at
         * their length. */
        double t = (double)s_back / FREECAM_BACK_FRAMES_BEFORE, k = t * t * (3 - 2 * t);
        s_yaw = remainder(gyaw + s_kept_yaw * k, 2 * 180.0 * k_rad);
        s_pitch = gpitch + s_kept_pitch * k;
        s_back = s_back < FREECAM_BACK_FRAMES_BEFORE ? s_back + 1 : 0;
    }
    freecam_turn(&s_cfg, x, y, mx, my, &x, &y);
    s_yaw = remainder(s_yaw + x, 2 * 180.0 * k_rad);
    s_pitch -= y;                           /* up tilts the view up: the eye goes down */
    if (s_cfg.centre && s_idle >= k_centre[s_cfg.centre].wait) {
        double ease = k_centre[s_cfg.centre].ease;
        s_yaw = remainder(s_yaw + remainder(gyaw - s_yaw, 2 * 180.0 * k_rad) / ease, 2 * 180.0 * k_rad);
        s_pitch += (gpitch - s_pitch) / ease;
    }
    if (s_pitch > pmax) s_pitch = pmax;
    if (s_pitch < -pmax) s_pitch = -pmax;
    s_gyaw = gyaw;
    s_gpitch = gpitch;
    cp = cos(s_pitch);
    eye[0] = (float)(tgt[0] - d * cp * sin(s_yaw));
    eye[1] = (float)(tgt[1] + d * sin(s_pitch));
    eye[2] = (float)(tgt[2] - d * cp * cos(s_yaw));
    s_placed_cam = cam;
    return again ? FREECAM_STARTED : FREECAM_PLACED;
}

int freecam_on(void)
{
    return s_cfg.on;
}

void freecam_settings(FREECAM_SETTINGS *out)
{
    *out = s_asked;
}

#if defined(XBOXRECOMP_MENU)
/* The in-game menu's Input page, its Free camera submenu (src/menu/menu.h,
 * XBOX_GROUP_FREECAM), below the layouts' two. A row changes what the player
 * asks of freecam, which takes it up from the next frame, and keeps it in the
 * settings file, as F8 keeps freecam=; it runs on the window's thread, as F8
 * does. The rows show the player's, also while a recording plays its own. The
 * deadzone is shown, and stepped, in percent. */

static int    menu_on(void)            { return s_asked.on; }
static int    menu_centre(void)        { return s_asked.centre; }
static void   menu_centre_pick(int i)  { s_asked.centre = i; nv_window_pref_set_num("autocentre", i); }
static int    menu_inv_x(void)         { return s_asked.invert_x; }
static void   menu_inv_x_run(void)     { s_asked.invert_x = !s_asked.invert_x; nv_window_pref_set_num("invert_x", s_asked.invert_x); }
static int    menu_inv_y(void)         { return s_asked.invert_y; }
static void   menu_inv_y_run(void)     { s_asked.invert_y = !s_asked.invert_y; nv_window_pref_set_num("invert_y", s_asked.invert_y); }
static double menu_sens_x(void)        { return s_asked.sens_x; }
static void   menu_sens_x_set(double v) { s_asked.sens_x = v; nv_window_pref_set_num("sensitivity_x", v); }
static double menu_sens_y(void)        { return s_asked.sens_y; }
static void   menu_sens_y_set(double v) { s_asked.sens_y = v; nv_window_pref_set_num("sensitivity_y", v); }
static double menu_dz(void)            { return s_asked.deadzone * 100; }
static void   menu_dz_set(double v)    { s_asked.deadzone = v / 100; nv_window_pref_set_num("deadzone", v / 100); }
static int    menu_mouse(void)         { return s_asked.mouse; }
static void   menu_mouse_run(void)     { s_asked.mouse = !s_asked.mouse; nv_window_pref_set_num("mouse", s_asked.mouse); }
static double menu_mouse_sens(void)    { return s_asked.mouse_sens; }
static void   menu_mouse_sens_set(double v) { s_asked.mouse_sens = v; nv_window_pref_set_num("mouse_sensitivity", v); }

/* Reset to default: every row of Free camera but its switch, at its
 * default, kept in the settings file as each row keeps its own. */
static void menu_reset(void)
{
    s_asked.centre = FREECAM_CENTRE_DEFAULT;
    s_asked.invert_x = s_asked.invert_y = FREECAM_INVERT_DEFAULT;
    s_asked.sens_x = s_asked.sens_y = FREECAM_SENS_DEFAULT;
    s_asked.deadzone = FREECAM_DEADZONE_DEFAULT;
    s_asked.mouse = FREECAM_MOUSE_DEFAULT;
    s_asked.mouse_sens = FREECAM_MOUSE_SENS_DEFAULT;
    nv_window_pref_set_num("autocentre", s_asked.centre);
    nv_window_pref_set_num("invert_x", s_asked.invert_x);
    nv_window_pref_set_num("invert_y", s_asked.invert_y);
    nv_window_pref_set_num("sensitivity_x", s_asked.sens_x);
    nv_window_pref_set_num("sensitivity_y", s_asked.sens_y);
    nv_window_pref_set_num("deadzone", s_asked.deadzone);
    nv_window_pref_set_num("mouse", s_asked.mouse);
    nv_window_pref_set_num("mouse_sensitivity", s_asked.mouse_sens);
    {
        FREECAM_SETTINGS now = s_asked;
        say("reset to default (the menu)", &now);
    }
}

static void freecam_menu(void)
{
    const XBOX_MENU_ITEM rows[] = {
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_GROUP, .label = XBOX_GROUP_FREECAM,
          .hint = "Free camera toggle and settings." },
        /* File > Presets: the game's own camera in Xbox, which the others
         * leave as the player has it; and how it handles, which no preset
         * sets, with Reset to default at the foot (spriteice, 22 November
         * 2026, the card "Free camera too"; and 23 November 2026, the card
         * "Leave them all"). */
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_TOGGLE, .label = "Enable free camera",
          .group = XBOX_GROUP_FREECAM, .state = menu_on, .run = freecam_flip,
          .presets = XBOX_PRESETS_SET, .preset = { 0, 0, 0, 0 },
          .leave = XBOX_PRESETS_BUT_XBOX,
          .hint = "Lets the right stick, or the mouse, control the camera." },
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_CHOICE, .label = "Auto centre",
          .group = XBOX_GROUP_FREECAM,
          .state = menu_centre, .pick = menu_centre_pick,
          .choices = k_centre_names, .nchoices = FREECAM_CENTRE_LEVELS,
          .presets = XBOX_PRESETS_LEFT,
          .hint = "Resets the camera shortly after the right stick rests." },
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_TOGGLE, .label = "Invert X",
          .group = XBOX_GROUP_FREECAM,
          .state = menu_inv_x, .run = menu_inv_x_run,
          .presets = XBOX_PRESETS_LEFT,
          .hint = "Swaps the camera's left and right." },
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_TOGGLE, .label = "Invert Y",
          .group = XBOX_GROUP_FREECAM,
          .state = menu_inv_y, .run = menu_inv_y_run,
          .presets = XBOX_PRESETS_LEFT,
          .hint = "Swaps the camera's up and down." },
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_NUMBER, .label = "Sensitivity X",
          .group = XBOX_GROUP_FREECAM,
          .value = menu_sens_x, .set = menu_sens_x_set,
          .min = FREECAM_SENS_MIN, .max = FREECAM_SENS_MAX, .step = 1, .format = "%g",
          .presets = XBOX_PRESETS_LEFT,
          .hint = "Sets how fast the right stick turns the camera left and right." },
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_NUMBER, .label = "Sensitivity Y",
          .group = XBOX_GROUP_FREECAM,
          .value = menu_sens_y, .set = menu_sens_y_set,
          .min = FREECAM_SENS_MIN, .max = FREECAM_SENS_MAX, .step = 1, .format = "%g",
          .presets = XBOX_PRESETS_LEFT,
          .hint = "Sets how fast the right stick turns the camera up and down." },
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_NUMBER, .label = "Deadzone",
          .group = XBOX_GROUP_FREECAM,
          .value = menu_dz, .set = menu_dz_set, .min = 0, .max = 100, .step = 5, .format = "%g%%",
          .presets = XBOX_PRESETS_LEFT,
          .hint = "Sets how far the right stick moves before the camera turns." },
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_TOGGLE, .label = "Mouse look",
          .group = XBOX_GROUP_FREECAM,
          .state = menu_mouse, .run = menu_mouse_run,
          .presets = XBOX_PRESETS_LEFT,
          .hint = "Lets the mouse turn the free camera." },
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_NUMBER, .label = "Mouse sensitivity",
          .group = XBOX_GROUP_FREECAM,
          .value = menu_mouse_sens, .set = menu_mouse_sens_set,
          .min = FREECAM_SENS_MIN, .max = FREECAM_SENS_MAX, .step = 1, .format = "%g",
          .presets = XBOX_PRESETS_LEFT,
          .hint = "Sets how fast the mouse turns the camera." },
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_ACTION, .label = "Reset to default",
          .group = XBOX_GROUP_FREECAM, .run = menu_reset, .presets = XBOX_PRESETS_LEFT,
          .confirm = "Put the free camera's settings back to their defaults?",
          .hint = "Puts every setting above but Enable free camera back to its default." },
    };
    xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
}
#endif
