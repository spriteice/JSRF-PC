/* Drives port/playback.c through port/pad.c and port/freecam.c with a
 * stand-in game: a loop of passes, each reading the pad as the game's
 * XInputGetState does, running the free camera once a frame and around the
 * skater as the follow camera does, moving the skater by what it read the way
 * the view faces, drawing random numbers and reaching anchors, and reporting
 * the skater to playback as the camera update does (tests/playback/run.py). */
#define _POSIX_C_SOURCE 200809L    /* setenv */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)
/* Windows has no setenv: _putenv_s, as tests/setup and playback.c's own
 * set_env use. */
#  define setenv(name, value, overwrite) _putenv_s(name, value)
#endif

#include "freecam.h"
#include "input_remap.h"
#include "menu.h"
#include "pad.h"
#include "playback.h"

#define STATE      0x0100u          /* the game's XINPUT_STATE */
#define FRAMES     1000             /* the recorded run's passes */
#define READS      2                /* the game's reads of the pad a pass */
#define CAMERA     3u               /* the camera's mode, but in the scene */
#define SPEED      0.5f             /* units a pass at full tilt */
#define IDLE_FROM  200              /* nothing pressed from here ... */
#define IDLE_TO    330              /* ... to here */
#define CAM        0x00401000u      /* the camera the free camera follows */
#define CAM_BACK   10.0f            /* the game's own eye: this far behind the skater ... */
#define CAM_UP     3.0f             /* ... and this far above */

static uint8_t g_mem[0x1000];
static int g_bad;
static char g_dir[512];

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); g_bad = 1; } } while (0)

/* ---- what pad.c and playback.c reach -------------------------------------- */

static XBOX_INPUT_STATE g_host;     /* the real pad, on port 0 */
void xbox_InputInit(void) {}
DWORD xbox_InputGetState(DWORD port, XBOX_INPUT_STATE *st)
{
    if (port) return 1;
    *st = g_host;
    return 0;
}
DWORD xbox_InputSetState(DWORD port, const XBOX_VIBRATION *v) { (void)port; (void)v; return 0; }
void kbmove_pad(unsigned port, short *lx, short *ly) { (void)port; (void)lx; (void)ly; }
void pad_test_start(void) {}
int pad_test_inputs(uint32_t port, XBOX_INPUT_STATE *st) { (void)port; (void)st; return 0; }

/* The menu: Developer > Playback's rows, and the free camera's on the Input
 * page, caught. */
static XBOX_MENU_ITEM g_rows[8], g_cam_rows[12];
static int g_nrows, g_ncam_rows, g_closed, g_restarts;
static char g_restart_env[1100];
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    int cam = count > 0 && items[0].menu == XBOX_MENU_INPUT;
    memcpy(cam ? g_cam_rows : g_rows, items, (size_t)count * sizeof *items);
    *(cam ? &g_ncam_rows : &g_nrows) = count;
}
static const XBOX_MENU_ITEM *cam_row(const char *label)
{
    int i;
    for (i = 0; i < g_ncam_rows; i++)
        if (!strcmp(g_cam_rows[i].label, label)) return &g_cam_rows[i];
    printf("FAIL: no free camera row %s\n", label);
    exit(1);
}

/* The settings file the free camera reads and keeps its keys in, and F8 and
 * the pad's Toggle free camera (R3 as it starts), which it binds. */
static struct { char key[32]; double v; } g_prefs[16];
static int g_nprefs;
int nv_window_pref_num(const char *key, double *v)
{
    int i;
    for (i = 0; i < g_nprefs; i++)
        if (!strcmp(g_prefs[i].key, key)) { *v = g_prefs[i].v; return 1; }
    return 0;
}
void nv_window_pref_set_num(const char *key, double v)
{
    int i;
    for (i = 0; i < g_nprefs && strcmp(g_prefs[i].key, key); i++) { }
    if (i == (int)(sizeof g_prefs / sizeof g_prefs[0])) return;
    if (i == g_nprefs) snprintf(g_prefs[g_nprefs++].key, sizeof g_prefs[i].key, "%s", key);
    g_prefs[i].v = v;
}
static double pref(const char *key)
{
    double v = -1;
    nv_window_pref_num(key, &v);
    return v;
}
static void (*g_f8)(void), (*g_r3)(void);
static int (*g_mouse_want)(void);
static int g_mouse[2];              /* the player's mouse, right and down, since the game's frame took it */
void nv_window_mouse_look(int (*want)(void)) { g_mouse_want = want; }
int nv_window_mouse_take(int *dx, int *dy)
{
    *dx = g_mouse[0];
    *dy = g_mouse[1];
    g_mouse[0] = g_mouse[1] = 0;
    return 1;
}
void nv_window_hotkey(const char *name, void (*run)(void)) { if (!strcmp(name, "F8")) g_f8 = run; }
void input_remap_pad_hotkey(int control, void (*run)(void)) { if (control == INPUT_CTL_FREECAM) g_r3 = run; }
void nv_window_file_path(const char *file, char *buf, size_t n) { snprintf(buf, n, "%s/%s", g_dir, file); }
void nv_window_close(void) { g_closed++; }
void nv_window_restart(void)
{
    const char *r = getenv("JSRF_RECORD"), *p = getenv("JSRF_PLAY_MENU");
    g_restarts++;
    snprintf(g_restart_env, sizeof g_restart_env, "%s", r ? "JSRF_RECORD" : p ? p : "");
}

/* ---- the stand-in game ------------------------------------------------- */

typedef struct {
    float pos[3];
    uint32_t turn, random;
    XBOX_GAMEPAD seen[FRAMES + 4000][READS];    /* what it read, each pass */
    float at[FRAMES + 4000][3];                 /* where the skater was after it */
    float eye[FRAMES + 4000][3];                /* where the free camera put the eye: 0s for none */
    uint32_t random_after[8];                   /* the generator after each anchor */
    int anchors;
} Game;
static Game g_rec, g_play;

/* The player, while recording: the left stick and A and B, by the frame and
 * the read, with nothing pressed from IDLE_FROM to IDLE_TO. With the free
 * camera (cam), the right stick too, and the game's Right stick click at
 * CAM_R3, which the free camera leaves to the game. */
#define CAM_F8_ON  60               /* the free camera's player: F8 turns it on */
#define CAM_R3     600              /* R3, the pad's Toggle free camera, turns it off */
#define CAM_F8_ON2 650              /* and F8 on again */
static void player(int frame, int read, int cam, XBOX_INPUT_STATE *st)
{
    memset(st, 0, sizeof *st);
    if (frame < 50 || (frame >= IDLE_FROM && frame <= IDLE_TO)) return;
    st->Gamepad.sThumbLY = (SHORT)(20000 + (frame * 37) % 5000);
    st->Gamepad.sThumbLX = (SHORT)(frame < 600 ? 9000 : -12000);
    if (frame % 20 < 3) st->Gamepad.bAnalogButtons[XBOX_BUTTON_A] = 255;
    if (read == 1 && frame % 10 == 0) st->Gamepad.wButtons = XBOX_GAMEPAD_START;
    if (!cam) return;
    if (frame >= 120 && frame <= 180) { st->Gamepad.sThumbRX = 30000; st->Gamepad.sThumbRY = -6000; }
    if (frame >= 380 && frame <= 395) st->Gamepad.sThumbRX = -20000;
    if (frame >= 700 && frame <= 760) { st->Gamepad.sThumbRX = -25000; st->Gamepad.sThumbRY = 9000; }
    if (frame == CAM_R3) st->Gamepad.wButtons |= XBOX_GAMEPAD_RIGHT_THUMB;
}

/* The free camera's player, on the window's thread, before the pass's reads:
 * F8 and R3 (Toggle free camera) turn it on, off and on, and the menu's
 * rows change its sensitivity, its invert, its auto centre and the mouse's
 * sensitivity while it steers. The mouse turns it, standing still and
 * moving, and moves while it is off, which turns nothing. */
#define CAM_MOUSE_OFF 610           /* the mouse moved while R3 has it off */
static void cam_player(int frame)
{
    if (frame == CAM_F8_ON || frame == CAM_F8_ON2) g_f8();
    if (frame == CAM_R3) g_r3();
    if (frame == 340) cam_row("Sensitivity X")->set(8);
    if (frame == 350) cam_row("Invert Y")->run();
    if (frame == 360) cam_row("Auto centre")->pick(3);
    if (frame == 365) cam_row("Mouse sensitivity")->set(7);
    if (frame >= 240 && frame <= 260) { g_mouse[0] += 9; g_mouse[1] += 4; }
    if (frame >= 560 && frame <= 580) { g_mouse[0] -= 6; g_mouse[1] -= 2; }
    if (frame >= CAM_MOUSE_OFF && frame <= CAM_MOUSE_OFF + 10) g_mouse[0] += 25;
}

/* An anchor of the run, by the recorded frame it comes at. */
typedef struct { int frame; PLAYBACK_ANCHOR kind; uint32_t value; } Anchor;
static const Anchor k_anchors[] = {
    { 100, PLAYBACK_MISSION_BIN, 0x00010001u },
    { 102, PLAYBACK_MISSION_DAT, 0x00010001u },
    { 150, PLAYBACK_STAGE,       0x00010002u },
    { 400, PLAYBACK_SCENE,       0 },
    { 520, PLAYBACK_SCENE_END,   0 },
    { 700, PLAYBACK_MISSION_BIN, 0x00010003u },
};
#define ANCHORS ((int)(sizeof k_anchors / sizeof k_anchors[0]))

/* How a playback run goes another way than the recording. */
typedef struct {
    int frames;                 /* passes to run */
    int late, late_anchor;      /* that anchor, and all after it, come this many frames late
                                 * (negative: early) */
    int skip_anchor;            /* this anchor never comes (-1: none) */
    int wrong_anchor;           /* this anchor comes with another value (-1: none) */
    int push_at;                /* the skater pushed this far off at this frame (0: never) */
    float push;
    int press_at;               /* a real pad pressed from this frame (0: never) */
    int hold_from_start;        /* a real pad held for the first ten frames */
    int right_at;               /* a real pad's right stick pushed from this frame (0: never) */
    int mouse_at;               /* the player's mouse moved from this frame (0: never) */
    int f8_at;                  /* F8 pressed at this frame (0: never) */
    int recording;              /* the host is the player: recording; 2, with the free camera */
} How;

static int anchor_frame(const How *h, int i)
{
    if (i == h->skip_anchor) return -1;
    return k_anchors[i].frame + (h->late && i >= h->late_anchor ? h->late : 0);
}

static void run(Game *g, const How *h)
{
    int f, r, i;
    memset(g, 0, sizeof *g);
    g->random = 0x416Cu;
    for (f = 0; f < h->frames; f++) {
        float lx = 0, ly = 0, heading = 0;
        int in_scene = 0;
        PLAYBACK_STATE was = playback_state();
        playback_pass();
        if (was != PLAYBACK_OFF && was != PLAYBACK_RECORDING && was != PLAYBACK_PLAYING
            && was != PLAYBACK_WAITING)
            break;                                  /* a playback over: the run with it */
        if (h->recording == 2) cam_player(f);
        if (h->f8_at && f == h->f8_at) g_f8();
        if (h->mouse_at && f >= h->mouse_at) g_mouse[0] += 5;
        for (r = 0; r < READS; r++) {
            if (h->recording) player(f, r, h->recording == 2, &g_host);
            else {
                memset(&g_host, 0, sizeof g_host);
                if ((h->press_at && f >= h->press_at) || (h->hold_from_start && f < 10))
                    g_host.Gamepad.bAnalogButtons[XBOX_BUTTON_B] = 255;
                if (h->right_at && f >= h->right_at) g_host.Gamepad.sThumbRX = 30000;
            }
            pad_get_state(g_mem, XPP_HANDLE_BASE, STATE);
            memcpy(&g->seen[f][r], g_mem + STATE + 4, sizeof g->seen[f][r]);
        }
        for (i = 0; i + 1 < ANCHORS; i++)
            if (k_anchors[i].kind == PLAYBACK_SCENE && f >= anchor_frame(h, i) && f < anchor_frame(h, i + 1))
                in_scene = 1;
        /* Once a game frame, as the sound manager's update calls it; then
         * the follow camera, but in the scene: the game's eye behind the
         * skater, or the free camera's around it, whose view the skater is
         * steered by. */
        freecam_frame();
        if (!in_scene) {
            float seye[3] = { g->pos[0], g->pos[1] + CAM_UP, g->pos[2] - CAM_BACK };
            if (freecam_orbit(CAM, CAMERA, g->pos, seye, 0, g->eye[f]) != FREECAM_OFF)
                heading = atan2f(g->pos[0] - g->eye[f][0], g->pos[2] - g->eye[f][2]);
            freecam_ran(CAM);
        }
        /* The skater moves by the last read, forward the way the view
         * faces; the generator is drawn a number of times that follows the
         * host, not the passes. */
        lx = *(int16_t *)(g_mem + STATE + 14) / 32767.0f;
        ly = *(int16_t *)(g_mem + STATE + 16) / 32767.0f;
        g->pos[0] += (lx * cosf(heading) + ly * sinf(heading)) * SPEED;
        g->pos[2] += (ly * cosf(heading) - lx * sinf(heading)) * SPEED;
        g->turn = (uint32_t)(f * 13) & 0xFFFFu;
        if (lx == 0 && ly == 0) g->turn = 0x4000u;  /* standing still, facing one way */
        for (i = 0; i < (h->recording ? 1 + f % 3 : 1 + f % 5); i++) g->random = g->random * 0x55u + 0x7Fu;
        if (h->push_at && f == h->push_at) g->pos[0] += h->push;
        for (i = 0; i < ANCHORS; i++) {
            uint32_t value = i == h->wrong_anchor ? 0x99999999u : k_anchors[i].value;
            if (anchor_frame(h, i) != f) continue;
            playback_anchor(k_anchors[i].kind, value, &g->random);
            g->random_after[g->anchors++] = g->random;
        }
        if (!in_scene) playback_view(g->pos, &g->turn, CAMERA);
        memcpy(g->at[f], g->pos, sizeof g->pos);
    }
}

static int same(const XBOX_GAMEPAD *a, const XBOX_GAMEPAD *b) { return !memcmp(a, b, sizeof *a); }
static float distance3(const float a[3], const float b[3])
{
    return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

/* ---- the cases ----------------------------------------------------------- */

static char g_run[600];

static void record(void)
{
    How h = { .frames = FRAMES, .skip_anchor = -1, .wrong_anchor = -1, .recording = 1 };
    char path[700], line[256];
    int pads = 0, anchors = 0, checks = 0, idles = 0, ends = 0, reads1 = 0, cams = 0;
    FILE *f;
    snprintf(g_run, sizeof g_run, "%s/recordings/2026-11-19-090000", g_dir);
    CHECK(playback_record(g_run), "a recording couldn't start");
    CHECK(playback_state() == PLAYBACK_RECORDING, "not recording");
    run(&g_rec, &h);
    playback_stop();
    CHECK(playback_state() == PLAYBACK_RECORDED, "the recording didn't end");
    playback_stop();                                /* nothing to stop: said, nothing else */
    snprintf(path, sizeof path, "%s/" PLAYBACK_FILE, g_run);
    f = fopen(path, "r");
    CHECK(f != NULL, "no %s", path);
    if (!f) return;
    CHECK(fgets(line, sizeof line, f) && !strcmp(line, "jsrf-pc-recording 1\n"), "the first line: %s", line);
    CHECK(fgets(line, sizeof line, f) && !strncmp(line, "version ", 8), "the second line: %s", line);
    while (fgets(line, sizeof line, f)) {
        pads += !strncmp(line, "pad ", 4);
        reads1 += !strncmp(line, "pad ", 4) && strstr(line, ".1 0 ") != NULL;
        anchors += !strncmp(line, "anchor ", 7);
        checks += !strncmp(line, "check ", 6);
        idles += !strncmp(line, "idle ", 5);
        ends += !strcmp(line, "end 1000\n");
        cams += !strcmp(line, "freecam 1 0 0 0 5 5 0.15 0 1 5\n");
    }
    fclose(f);
    /* A check a second of frames from the first: 7 before the scene (400 to
     * 519, with no view), 8 after it. The free camera off, at its defaults,
     * from the first frame. */
    CHECK(anchors == ANCHORS && checks == 7 + 8 && idles == 1 && ends == 1 && pads > 100 && reads1 > 10
          && cams == 1,
          "the recording: %d pads (%d on a second read), %d anchors, %d checks, %d idle, %d ends, %d freecam",
          pads, reads1, anchors, checks, idles, ends, cams);
}

/* Played back as recorded: the game reads what it read, pass for pass and
 * read for read, with nobody at the pad; the generator put back at each
 * anchor; the idle moment taken; the end on route; the game closed. */
static void play_same(void)
{
    How h = { .frames = FRAMES + 10, .skip_anchor = -1, .wrong_anchor = -1 };
    int f, r, i, bad = 0;
    const PLAYBACK_RESULT *res = playback_result();
    g_closed = 0;
    CHECK(playback_play(g_run, 1), "playback couldn't start");
    run(&g_play, &h);
    for (f = 0; f < FRAMES; f++)
        for (r = 0; r < READS; r++)
            if (!same(&g_play.seen[f][r], &g_rec.seen[f][r]) && !bad++)
                CHECK(0, "frame %d read %d: not the recorded pad", f, r);
    for (i = 0; i < ANCHORS; i++)
        CHECK(g_play.random_after[i] == g_rec.random_after[i], "anchor %d: the generator %08X, recorded %08X",
              i, g_play.random_after[i], g_rec.random_after[i]);
    CHECK(res->state == PLAYBACK_FINISHED && res->anchors_met == ANCHORS && res->drift_most < 0.01f
          && res->idles_put_back == 1 && g_closed == 1,
          "as recorded: state %d, anchors %d, drift %.2f, idles %d, closed %d", res->state, res->anchors_met,
          res->drift_most, res->idles_put_back, g_closed);
}

/* An anchor reached late: the pad held as recorded until it comes, the
 * presses after it moved by as much, none before; one reached early: the
 * recording skips to it. */
static void play_late(int late)
{
    How h = { .frames = FRAMES + 60, .late = late, .late_anchor = 2, .skip_anchor = -1, .wrong_anchor = -1 };
    int at = k_anchors[2].frame, f, r, bad = 0;
    const PLAYBACK_RESULT *res = playback_result();
    CHECK(playback_play(g_run, 0), "playback couldn't start");
    run(&g_play, &h);
    g_closed = 0;
    for (f = 0; f < FRAMES && !bad; f++)
        for (r = 0; r < READS && !bad; r++) {
            const XBOX_GAMEPAD *want;
            if (f <= (late < 0 ? at + late : at)) want = &g_rec.seen[f][r];
            else if (late > 0 && f <= at + late) want = &g_rec.seen[at][READS - 1];
            else if (f - late < FRAMES) want = &g_rec.seen[f - late][r];
            else continue;
            if (!same(&g_play.seen[f][r], want)) {
                bad = 1;
                CHECK(0, "anchor %d frames %s: frame %d read %d not as recorded", late < 0 ? -late : late,
                      late < 0 ? "early" : "late", f, r);
            }
        }
    CHECK(res->state == PLAYBACK_FINISHED && res->anchors_met == ANCHORS && g_closed == 0,
          "anchor %d frames off: state %d, anchors %d, closed %d (the menu's play closes nothing)",
          late, res->state, res->anchors_met, g_closed);
}

static void play_off_route(void)
{
    const PLAYBACK_RESULT *res = playback_result();
    How drift = { .frames = FRAMES, .skip_anchor = -1, .wrong_anchor = -1, .push_at = 600, .push = 100 };
    How wrong = { .frames = FRAMES, .skip_anchor = -1, .wrong_anchor = 2 };
    How never = { .frames = FRAMES + PLAYBACK_ANCHOR_WAIT + 10, .skip_anchor = 5, .wrong_anchor = -1 };
    g_closed = 0;
    CHECK(playback_play(g_run, 1), "playback couldn't start");
    run(&g_play, &drift);
    CHECK(res->state == PLAYBACK_OFF_ROUTE && strstr(res->why, "units from the recorded spot")
          && res->drift_most > 99 && g_closed == 1,
          "pushed 100 units off: state %d (%s), drift %.1f, closed %d", res->state, res->why, res->drift_most,
          g_closed);
    CHECK(playback_play(g_run, 0), "playback couldn't start");
    run(&g_play, &wrong);
    CHECK(res->state == PLAYBACK_OFF_ROUTE && strstr(res->why, "where the recording has stage 00010002 next"),
          "another stage: state %d (%s)", res->state, res->why);
    CHECK(playback_play(g_run, 0), "playback couldn't start");
    run(&g_play, &never);
    CHECK(res->state == PLAYBACK_OFF_ROUTE && strstr(res->why, "not reached within"),
          "an anchor never reached: state %d (%s)", res->state, res->why);
}

/* The skater put back at the idle moment, from where a push left it. */
static void play_idle(void)
{
    const PLAYBACK_RESULT *res = playback_result();
    How h = { .frames = 400, .skip_anchor = -1, .wrong_anchor = -1, .push_at = 230, .push = 5 };
    CHECK(playback_play(g_run, 0), "playback couldn't start");
    run(&g_play, &h);
    CHECK(res->idles_put_back == 1 && distance3(g_play.at[399], g_rec.at[399]) < 1e-3f,
          "idle: put back %d, then %.3f units from the recorded path", res->idles_put_back,
          distance3(g_play.at[399], g_rec.at[399]));
    CHECK(res->state == PLAYBACK_PLAYING && res->drift_most > 4 && res->drift_most < 6,
          "the push, and after the idle moment: state %d, drift %.1f", res->state, res->drift_most);
}

/* A real pad: held from the start, nothing; pressed once let go of, the
 * game is handed back, and what it reads from then is the pad's. */
static void play_hand_back(void)
{
    const PLAYBACK_RESULT *res = playback_result();
    How h = { .frames = 400, .skip_anchor = -1, .wrong_anchor = -1, .press_at = 300, .hold_from_start = 1 };
    g_closed = 0;
    CHECK(playback_play(g_run, 1), "playback couldn't start");
    run(&g_play, &h);
    CHECK(res->state == PLAYBACK_HANDED_BACK && res->frames == 301 && g_closed == 0,
          "a press at 300: state %d at frame %u, closed %d", res->state, res->frames, g_closed);
    CHECK(g_play.seen[5][0].bAnalogButtons[XBOX_BUTTON_B] == 0 && g_play.seen[300][0].bAnalogButtons[XBOX_BUTTON_B] == 255,
          "the pad held from the start reached the game, or the press after didn't");
}

/* ---- the free camera ------------------------------------------------------ */

static Game g_cam;
static char g_cam_run[600];

/* The player's own free camera before a playback: off, and every setting
 * other than the recording's, taken up at a frame. */
static void cam_yours(void)
{
    if (cam_row("Enable free camera")->state()) g_f8();
    cam_row("Sensitivity X")->set(2);
    cam_row("Sensitivity Y")->set(9);
    cam_row("Deadzone")->set(30);
    cam_row("Auto centre")->pick(1);
    if (!cam_row("Invert X")->state()) cam_row("Invert X")->run();
    if (cam_row("Invert Y")->state()) cam_row("Invert Y")->run();
    if (cam_row("Mouse look")->state()) cam_row("Mouse look")->run();
    cam_row("Mouse sensitivity")->set(9);
    freecam_frame();
}

/* Recorded with the free camera: turned on by F8, steered by the right stick
 * and the mouse, its settings changed in the menu while it steers, off by R3
 * and on again by F8. The right stick is in the recording as the pad gave it,
 * and the game read it zeroed while the free camera steered; the mouse's
 * moves are in it right and up, each frame the free camera turned by them,
 * and not those while it was off. */
static void record_cam(void)
{
    How h = { .frames = FRAMES, .skip_anchor = -1, .wrong_anchor = -1, .recording = 2 };
    char path[700], line[256];
    int cams = 0, right = 0, mice = 0, mice_off = 0, mouse_on = 0, frame, dx, dy;
    FILE *f;
    freecam_frame();
    snprintf(g_cam_run, sizeof g_cam_run, "%s/recordings/2026-11-20-020000", g_dir);
    CHECK(playback_record(g_cam_run), "a recording with the free camera couldn't start");
    run(&g_cam, &h);
    playback_stop();
    snprintf(path, sizeof path, "%s/" PLAYBACK_FILE, g_cam_run);
    f = fopen(path, "r");
    CHECK(f != NULL, "no %s", path);
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        cams += !strncmp(line, "freecam ", 8);
        right += !strncmp(line, "pad ", 4) && strstr(line, " 30000 -6000\n") != NULL;
        if (sscanf(line, "mouse %d %d %d", &frame, &dx, &dy) == 3) {
            mice++;
            mice_off += frame >= CAM_MOUSE_OFF && frame <= CAM_MOUSE_OFF + 10;
            CHECK((dx == 9 && dy == -4) || (dx == -6 && dy == 2), "a mouse line: %s", line);
        }
    }
    fclose(f);
    /* At the start; on; sensitivity, invert and auto centre; the mouse's
     * sensitivity; off; on. */
    CHECK(cams == 8 && right > 10, "the free camera's recording: %d freecam lines, the right stick in %d", cams, right);
    CHECK(mice == 42 && mice_off == 0, "the mouse's recording: %d lines, %d while the free camera was off", mice,
          mice_off);
    CHECK(g_cam.seen[150][0].sThumbRX == 0 && g_cam.seen[30][0].sThumbRX == 0 && g_cam.eye[150][0] != 0
          && g_cam.seen[600][0].wButtons & XBOX_GAMEPAD_RIGHT_THUMB,
          "the free camera's stick reached the game (%d), or it didn't steer", g_cam.seen[150][0].sThumbRX);
    mouse_on = g_mouse_want && g_mouse_want();
    CHECK(mouse_on && freecam_on() && pref("freecam") == 1 && pref("sensitivity_x") == 8 && pref("autocentre") == 3
          && pref("mouse_sensitivity") == 7,
          "the player's free camera after recording: on %d, freecam= %g, sensitivity_x= %g, autocentre= %g",
          freecam_on(), pref("freecam"), pref("sensitivity_x"), pref("autocentre"));
}

/* Played with the player's free camera off and set otherwise: the recording's
 * in its place from its first frame, so the game reads what it read, the free
 * camera turns as it did and the skater goes where it went; the player's are
 * back at its end, and the settings file is as the player left it. */
static void play_cam(void)
{
    How h = { .frames = FRAMES + 10, .skip_anchor = -1, .wrong_anchor = -1 };
    const PLAYBACK_RESULT *res = playback_result();
    int f, r, bad = 0;
    cam_yours();
    CHECK(playback_play(g_cam_run, 1), "playback with the free camera couldn't start");
    run(&g_play, &h);
    for (f = 0; f < FRAMES && !bad; f++) {
        for (r = 0; r < READS && !bad; r++)
            if (!same(&g_play.seen[f][r], &g_cam.seen[f][r]) && ++bad)
                CHECK(0, "free camera: frame %d read %d: not the recorded pad", f, r);
        if (!bad && (memcmp(g_play.eye[f], g_cam.eye[f], sizeof g_cam.eye[f])
                     || memcmp(g_play.at[f], g_cam.at[f], sizeof g_cam.at[f])) && ++bad)
            CHECK(0, "free camera: frame %d: the eye %.3f %.3f, recorded %.3f %.3f; the skater %.3f units off", f,
                  g_play.eye[f][0], g_play.eye[f][2], g_cam.eye[f][0], g_cam.eye[f][2],
                  distance3(g_play.at[f], g_cam.at[f]));
    }
    CHECK(res->state == PLAYBACK_FINISHED && res->anchors_met == ANCHORS && res->drift_most < 0.01f,
          "free camera, as recorded: state %d (%s), anchors %d, drift %.2f", res->state, res->why,
          res->anchors_met, res->drift_most);
    freecam_frame();
    CHECK(!freecam_on() && pref("freecam") == 0 && pref("sensitivity_x") == 2 && pref("deadzone") == 0.3
          && pref("autocentre") == 1 && pref("invert_x") == 1 && cam_row("Sensitivity Y")->value() == 9
          && pref("mouse") == 0 && pref("mouse_sensitivity") == 9 && !g_mouse_want(),
          "the player's free camera after playback: on %d, freecam= %g, sensitivity_x= %g, deadzone= %g",
          freecam_on(), pref("freecam"), pref("sensitivity_x"), pref("deadzone"));
}

/* While it plays: the real right stick hands the game back, though the free
 * camera steers; so does F8, which the game then has, as the player turned
 * it; so does the player's mouse, while the recording's free camera steers
 * with Mouse look on, though the player's own is off. */
static void play_cam_take_over(void)
{
    How stick = { .frames = 400, .skip_anchor = -1, .wrong_anchor = -1, .right_at = 150 };
    How f8 = { .frames = 400, .skip_anchor = -1, .wrong_anchor = -1, .f8_at = 100 };
    How mouse = { .frames = 400, .skip_anchor = -1, .wrong_anchor = -1, .mouse_at = 250 };
    const PLAYBACK_RESULT *res = playback_result();
    cam_yours();
    CHECK(playback_play(g_cam_run, 1), "playback couldn't start");
    run(&g_play, &stick);
    CHECK(res->state == PLAYBACK_HANDED_BACK && res->frames == 151 && g_play.eye[149][0] != 0,
          "the right stick at 150, the free camera steering: state %d at frame %u", res->state, res->frames);
    cam_yours();
    CHECK(playback_play(g_cam_run, 1), "playback couldn't start");
    run(&g_play, &mouse);
    g_mouse[0] = g_mouse[1] = 0;
    CHECK(res->state == PLAYBACK_HANDED_BACK && res->frames == 251 && g_play.eye[249][0] != 0,
          "the mouse at 250, the free camera steering: state %d at frame %u", res->state, res->frames);
    cam_yours();
    CHECK(playback_play(g_cam_run, 1), "playback couldn't start");
    run(&g_play, &f8);
    CHECK(res->state == PLAYBACK_HANDED_BACK && res->frames == 101 && freecam_on() && pref("freecam") == 1,
          "F8 at 100: state %d at frame %u, the free camera %d", res->state, res->frames, freecam_on());
}

static void write_file(const char *folder, const char *text)
{
    char path[700];
    FILE *f;
    mkdir(folder, 0755);
    snprintf(path, sizeof path, "%s/" PLAYBACK_FILE, folder);
    f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

/* Written by hand: a frame with no read, a comment, no version; frame 5 is
 * the game's fifth pass. Refused:
 * not a recording, a line back in time, a line after the end, a word it
 * doesn't know. */
static void by_hand(void)
{
    char folder[700];
    How h = { .frames = 30, .skip_anchor = -1, .wrong_anchor = -1 };
    snprintf(folder, sizeof folder, "%s/hand", g_dir);
    write_file(folder, "jsrf-pc-recording 1\n# Start at frame 5, let go at 6\n"
               "pad 5 0 0010 0000000000000000 0 0 0 0\npad 6 0 0000 0000000000000000 0 0 0 0\nend 20\n");
    CHECK(freecam_on() && playback_play(folder, 0), "a recording written by hand refused");
    run(&g_play, &h);
    CHECK(g_play.seen[3][1].wButtons == 0 && g_play.seen[4][0].wButtons == XBOX_GAMEPAD_START
          && g_play.seen[4][1].wButtons == XBOX_GAMEPAD_START && g_play.seen[5][0].wButtons == 0
          && playback_state() == PLAYBACK_FINISHED && playback_result()->frames == 20,
          "by hand: frames 4 to 6 read %04X %04X %04X, state %d after %u frames", g_play.seen[3][1].wButtons,
          g_play.seen[4][0].wButtons, g_play.seen[5][0].wButtons, playback_state(), playback_result()->frames);
    CHECK(freecam_on(), "by hand, with no freecam line: the player's free camera, on, turned off");
    /* The free camera off from frame 3, by hand; the player's, on, before
     * and after. */
    write_file(folder, "jsrf-pc-recording 1\nfreecam 3 0 1 1 10 1 1 4\nend 20\n");
    CHECK(freecam_on() && playback_play(folder, 0), "a free camera written by hand refused");
    h.frames = 6;
    run(&g_play, &h);
    CHECK(!freecam_on(), "by hand: the free camera on at frame 6");
    h.frames = 30;
    run(&g_play, &h);
    freecam_frame();
    CHECK(freecam_on() && playback_state() == PLAYBACK_FINISHED, "by hand: after, the free camera %d, state %d",
          freecam_on(), playback_state());
    /* The mouse by hand, with the free camera on: 10 counts right at
     * recorded frames 50 and 60, a degree each at 5; the first anchor,
     * recorded at 100, reached at frame 5, so frame 6 turns by both at once,
     * and the next anchor, which the recording doesn't have, stops it. */
    write_file(folder, "jsrf-pc-recording 1\nfreecam 1 1 0 0 5 5 0.15 0 1 5\nmouse 50 10 0\nmouse 60 10 0\n"
               "anchor 100 mission.bin 00010001 0000416C\nend 200\n");
    {
        How early = { .frames = 30, .late = -95, .late_anchor = 0, .skip_anchor = -1, .wrong_anchor = -1 };
        double h4, h5, h6;
        CHECK(playback_play(folder, 0), "a mouse written by hand refused");
        run(&g_play, &early);
        h4 = atan2(-g_play.eye[4][0], -g_play.eye[4][2]);
        h5 = atan2(-g_play.eye[5][0], -g_play.eye[5][2]);
        h6 = atan2(-g_play.eye[6][0], -g_play.eye[6][2]);
        CHECK(fabs(h5 - h4) < 1e-6 && fabs((h6 - h5) * 180 / 3.14159265358979 - 2) < 1e-3
              && playback_state() == PLAYBACK_OFF_ROUTE,
              "by hand, the mouse past an early anchor: %.4f then %.4f degrees, state %d",
              (h5 - h4) * 180 / 3.14159265358979, (h6 - h5) * 180 / 3.14159265358979, playback_state());
        freecam_frame();
    }
    write_file(folder, "pad 5 0 0010 0000000000000000 0 0 0 0\n");
    CHECK(!playback_play(folder, 0) && playback_state() == PLAYBACK_OFF, "not a recording, played");
    write_file(folder, "jsrf-pc-recording 1\nfreecam 3 0 0 0 11 5 0.15 0\n");
    CHECK(!playback_play(folder, 0), "a sensitivity of 11, played");
    write_file(folder, "jsrf-pc-recording 1\nfreecam 3 0 0 0 5 5 0.15 0 1 11\n");
    CHECK(!playback_play(folder, 0), "a mouse sensitivity of 11, played");
    write_file(folder, "jsrf-pc-recording 1\nfreecam 3 0 0 0 5 5 0.15 0 2 5\n");
    CHECK(!playback_play(folder, 0), "a Mouse look of 2, played");
    write_file(folder, "jsrf-pc-recording 1\nfreecam 3 0 0 0 5 5 0.15 0 1\n");
    CHECK(!playback_play(folder, 0), "a freecam line with Mouse look and no sensitivity, played");
    write_file(folder, "jsrf-pc-recording 1\nmouse 5 10 0\nmouse 4 10 0\n");
    CHECK(!playback_play(folder, 0), "a mouse line back in time, played");
    write_file(folder, "jsrf-pc-recording 1\nmouse 5 10\n");
    CHECK(!playback_play(folder, 0), "a mouse line without its y, played");
    write_file(folder, "jsrf-pc-recording 1\nfreecam 3 0 0 0 5 5 0.15 0\nfreecam 2 1 0 0 5 5 0.15 0\n");
    CHECK(!playback_play(folder, 0), "a freecam line back in time, played");
    write_file(folder, "jsrf-pc-recording 1\npad 5 0 0010 0000000000000000 0 0 0 0\n"
               "pad 4 0 0000 0000000000000000 0 0 0 0\n");
    CHECK(!playback_play(folder, 0), "a line back in time, played");
    write_file(folder, "jsrf-pc-recording 1\nend 9\ncheck 10 0 0 0 0 3\n");
    CHECK(!playback_play(folder, 0), "a line after the end, played");
    write_file(folder, "jsrf-pc-recording 1\nwait 10\n");
    CHECK(!playback_play(folder, 0), "a word it doesn't know, played");
    snprintf(folder, sizeof folder, "%s/none", g_dir);
    CHECK(!playback_play(folder, 0), "no recording, played");
}

/* As the engine starts: the environment read and taken out; JSRF_RECORD
 * makes a recording beside the settings file; Developer > Playback's rows,
 * Play's choices the recordings there, newest first. */
static void start(void)
{
    char folder[700], older[700];
    int i;
    snprintf(older, sizeof older, "%s/recordings/2026-11-18-120000", g_dir);
    write_file(older, "jsrf-pc-recording 1\nend 5\n");
    snprintf(folder, sizeof folder, "%s/recordings/not-one", g_dir);
    mkdir(folder, 0755);
    setenv("JSRF_PLAY_MENU", g_run, 1);
    setenv("JSRF_RECORD", "1", 1);
    playback_start();
    CHECK(playback_state() == PLAYBACK_PLAYING && !getenv("JSRF_PLAY_MENU") && !getenv("JSRF_RECORD"),
          "JSRF_PLAY_MENU: state %d, left in the environment", playback_state());
    CHECK(g_nrows == 3 && !strcmp(g_rows[0].label, "Record (Restart)") && !strcmp(g_rows[1].label, "Stop recording")
          && !strcmp(g_rows[2].label, "Play (Restart)") && g_rows[2].nchoices == 3
          && !strcmp(g_rows[2].choices[0], "2026-11-20-020000") && !strcmp(g_rows[2].choices[1], "2026-11-19-090000")
          && !strcmp(g_rows[2].choices[2], "2026-11-18-120000"),
          "the rows: %d, Play's choices %d", g_nrows, g_nrows == 3 ? g_rows[2].nchoices : -1);
    for (i = 0; i < g_nrows; i++) {
        const char *hint = g_rows[i].hint;
        CHECK(hint && strlen(hint) <= MENU_HINT_LONGEST && hint[strlen(hint) - 1] == '.'
              && g_rows[i].group && !strcmp(g_rows[i].group, XBOX_GROUP_PLAYBACK),
              "%s: its hint or its submenu", g_rows[i].label);
        CHECK(!strstr(g_rows[i].label, "(Restart)") == !g_rows[i].confirm, "%s: asks first, or doesn't",
              g_rows[i].label);
    }
    g_restarts = 0;
    g_rows[0].run();
    CHECK(g_restarts == 1 && !strcmp(g_restart_env, "JSRF_RECORD") && !getenv("JSRF_RECORD"),
          "Record: restarted %d with %s, then left", g_restarts, g_restart_env);
    g_rows[2].pick(2);
    CHECK(g_restarts == 2 && !strcmp(g_restart_env, older) && !getenv("JSRF_PLAY_MENU"),
          "Play: restarted %d with %s", g_restarts, g_restart_env);

    setenv("JSRF_RECORD", "1", 1);
    playback_start();
    CHECK(playback_state() == PLAYBACK_RECORDING && !getenv("JSRF_RECORD"), "JSRF_RECORD: state %d",
          playback_state());
    playback_stop();
    setenv("JSRF_PLAY", g_run, 1);
    playback_start();
    CHECK(playback_state() == PLAYBACK_PLAYING && !getenv("JSRF_PLAY"), "JSRF_PLAY: state %d", playback_state());
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: test <folder>\n"); return 2; }
    snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
    {
        char folder[600];
        snprintf(folder, sizeof folder, "%s/recordings", g_dir);
        mkdir(folder, 0755);
    }
    freecam_start();                    /* no keys in the settings: off, at its defaults */
    CHECK(g_f8 && g_r3 && g_mouse_want && !g_mouse_want() && g_ncam_rows == 10 && !freecam_on(),
          "the free camera didn't start as it does");
    record();
    play_same();
    play_late(30);
    play_late(-20);
    play_off_route();
    play_idle();
    play_hand_back();
    record_cam();
    play_cam();
    play_cam_take_over();
    by_hand();
    start();
    if (g_bad) return 1;
    printf("playback: all cases pass\n");
    return 0;
}
