/* Drives port/playback.c through port/pad.c and port/freecam.c with a
 * stand-in game: a loop of passes, each reading the pad as the game's
 * XInputGetState does, running the free camera once a frame and around the
 * skater as the follow camera does, moving the skater by what it read the way
 * the view faces, drawing random numbers and reaching anchors, and reporting
 * the skater, the view's heading and the free camera's angles to playback as
 * the camera update does; and the settings a recording keeps, and the mark
 * that a recording plays (tests/playback/run.py). */
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
#include "photo_mode.h"
#include "playback.h"
#include "playback_mark.h"

#define STATE      0x0100u          /* the game's XINPUT_STATE */
#define FRAMES     1000             /* the recorded run's passes */
#define READS      2                /* the game's reads of the pad a pass */
#define CAMERA     3u               /* the camera's mode, but in the scene */
#define SPEED      0.5f             /* units a pass at full tilt */
#define IDLE_FROM  200              /* nothing pressed from here ... */
#define IDLE_TO    330              /* ... to here */
#define CAM        0x00401000u      /* the camera the free camera follows */
#define PB_TEST_LINE 512            /* a recording's line, at most */
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
void photo_mode_pad(XBOX_INPUT_STATE *st) { (void)st; }

/* Where this run started, as port/missions.c and photo mode would say:
 * g_started 0 for the game's own start, else g_start (mission -1: Load
 * Game). */
static int g_started, g_photo;
static MISSIONS_START g_start;
int missions_started(MISSIONS_START *out)
{
    if (g_started) *out = g_start;
    return g_started;
}
int photo_mode_ticked(void) { return g_photo; }

/* The settings a recording keeps (port/playback.c's k_setting), as their
 * modules hold them: Default cam fix, No skater fade, Skip intro, Skip
 * cutscenes, Draw distance, Far buildings and Far shadows. */
#define SETTINGS 7
static int g_set[SETTINGS] = { 1, 0, 1, 1, 1, 3, 3 };
int camwall_on(void) { return g_set[0]; }
void camwall_set(int v) { g_set[0] = v; }
int camwall_whole(void) { return g_set[1]; }
void camwall_whole_set(int v) { g_set[1] = v; }
int intro_skip(void) { return g_set[2]; }
void intro_skip_set(int v) { g_set[2] = v; }
int cutscene_skip_on(void) { return g_set[3]; }
void cutscene_skip_set(int v) { g_set[3] = v; }
int draw_distance_step(void) { return g_set[4]; }
void draw_distance_set(int v) { g_set[4] = v; }
int draw_far_buildings(void) { return g_set[5]; }
void draw_far_buildings_set(int v) { g_set[5] = v; }
int draw_far_shadows(void) { return g_set[6]; }
void draw_far_shadows_set(int v) { g_set[6] = v; }
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
int nv_window_mouse_look(int (*want)(void)) { g_mouse_want = want; return 0; }
int nv_window_mouse_take(int asker, int *dx, int *dy)
{
    (void)asker;
    *dx = g_mouse[0];
    *dy = g_mouse[1];
    g_mouse[0] = g_mouse[1] = 0;
    return 1;
}
void nv_window_hotkey(const char *name, void (*run)(void)) { if (!strcmp(name, "F8")) g_f8 = run; }
void input_remap_pad_hotkey(int control, void (*run)(void)) { if (control == INPUT_CTL_FREECAM) g_r3 = run; }
void nv_window_file_path(const char *file, char *buf, size_t n) { snprintf(buf, n, "%s/%s", g_dir, file); }

/* The menu's rows by their labels (menu.h), as a run's steps choose them:
 * each path chosen kept, and the one row that isn't there. */
#define NO_ROW "Graphics > No such row"
static char g_chosen[8][128];
static int g_nchosen;
int menu_path_check(const char *path) { return strcmp(path, NO_ROW) ? MENU_PATH_OK : MENU_PATH_NO_ROW; }
static const XBOX_MENU_ITEM *cam_row(const char *label);
int menu_path_run(const char *path)
{
    if (!strcmp(path, NO_ROW)) return MENU_PATH_NO_ROW;
    if (!strcmp(path, "Input > Free camera > Invert Y")) cam_row("Invert Y")->run();
    if (g_nchosen < 8) snprintf(g_chosen[g_nchosen++], sizeof g_chosen[0], "%s", path);
    return MENU_PATH_OK;
}
const char *menu_path_why(int result) { return result == MENU_PATH_OK ? "done" : "nothing there has that label"; }
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
    unsigned ticks[FRAMES + 4000];              /* Camera's ticks as the camera update had them */
    uint32_t random_after[8];                   /* the generator after each anchor */
    uint32_t random_at[FRAMES + 4000];          /* the generator after the camera update */
    int dcamfix[FRAMES + 4000];                 /* Default cam fix as the frame had it */
    int anchors;
    uint8_t skater[PLAYBACK_SKATER_BYTES];      /* the skater's object: where it is, its turn and more */
    unsigned char viewed[FRAMES + 4000];        /* the camera update told playback of the skater */
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
    unsigned ticks, ticks2;     /* the player's Camera ticks, and from ticks_at (0: never) */
    int ticks_at;
    int flips;                  /* the window flips once a pass, running a run's steps */
    int yaw_at;                 /* the free camera turned yaw_by radians at this frame, after it placed
                                 * the eye, as nothing in the recording turned it (0: never) */
    double yaw_by;
    int set_at;                 /* the player turns Default cam fix off at this frame (0: never) */
    int state_at;               /* the skater's state changes at this frame where nothing follows it, at
                                 * byte SKATER_HIDDEN of its object (0: never) */
} How;

/* Where the stand-in skater's object keeps its place, its turn, and a value
 * nothing else follows. */
#define SKATER_PLACE  0x010
#define SKATER_TURN   0x01C
#define SKATER_HIDDEN 0x7F0

/* The window's picture written at a flip, as the renderer's shot_write
 * writes it: each path kept. */
static char g_shots[4][800];
static int g_nshots;
static int shot(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fputs("BM", f);
    fclose(f);
    if (g_nshots < 4) snprintf(g_shots[g_nshots++], sizeof g_shots[0], "%s", path);
    return 1;
}

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
        float lx = 0, ly = 0, heading = 0, look[2] = { 0, CAM_BACK }, eye[3] = { 0, 0, 0 };
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
        if (h->set_at && f == h->set_at) g_set[0] = 0;
        g->dcamfix[f] = g_set[0];
        if (!in_scene) {
            float seye[3] = { g->pos[0], g->pos[1] + CAM_UP, g->pos[2] - CAM_BACK };
            unsigned ticks = h->ticks_at && f >= h->ticks_at ? h->ticks2 : h->ticks;
            look[0] = 0;
            look[1] = CAM_BACK;
            memcpy(eye, seye, sizeof eye);
            if (freecam_orbit(CAM, CAMERA, g->pos, seye, 0, g->eye[f]) != FREECAM_OFF) {
                heading = atan2f(g->pos[0] - g->eye[f][0], g->pos[2] - g->eye[f][2]);
                look[0] = g->pos[0] - g->eye[f][0];
                look[1] = g->pos[2] - g->eye[f][2];
                memcpy(eye, g->eye[f], sizeof eye);
            }
            if (h->yaw_at && f == h->yaw_at) {
                double yaw, pitch;
                if (freecam_angles(&yaw, &pitch)) freecam_put(yaw + h->yaw_by, pitch);
            }
            freecam_ran(CAM);
            /* And the camera update's Camera ticks, as it reads them. */
            playback_camscenes(&ticks);
            g->ticks[f] = ticks;
        }
        /* The skater moves by the last read, forward the way the view
         * faces; the generator is drawn a number of times that follows the
         * host, not the passes, on the game's thread, and on playback once
         * every seventh frame on another, as the sound library might. */
        lx = *(int16_t *)(g_mem + STATE + 14) / 32767.0f;
        ly = *(int16_t *)(g_mem + STATE + 16) / 32767.0f;
        g->pos[0] += (lx * cosf(heading) + ly * sinf(heading)) * SPEED;
        g->pos[2] += (ly * cosf(heading) - lx * sinf(heading)) * SPEED;
        g->turn = (uint32_t)(f * 13) & 0xFFFFu;
        if (lx == 0 && ly == 0) g->turn = 0x4000u;  /* standing still, facing one way */
        for (i = 0; i < (h->recording ? 1 + f % 3 : 1 + f % 5); i++) {
            g->random = g->random * 0x55u + 0x7Fu;
            playback_drawn(1, 0x00015530u);
        }
        if (!h->recording && f % 7 == 3) {
            g->random = g->random * 0x55u + 0x7Fu;
            playback_drawn(0, 0x00130BD1u);
        }
        if (h->push_at && f == h->push_at) g->pos[0] += h->push;
        for (i = 0; i < ANCHORS; i++) {
            uint32_t value = i == h->wrong_anchor ? 0x99999999u : k_anchors[i].value;
            if (anchor_frame(h, i) != f) continue;
            playback_anchor(k_anchors[i].kind, value, &g->random);
            g->random_after[g->anchors++] = g->random;
        }
        if (h->state_at && f == h->state_at) g->skater[SKATER_HIDDEN] ^= 1;
        memcpy(g->skater + SKATER_PLACE, g->pos, sizeof g->pos);
        memcpy(g->skater + SKATER_TURN, &g->turn, sizeof g->turn);
        if (!in_scene) playback_view(g->pos, &g->turn, CAMERA, look, eye, g->skater, &g->random);
        g->viewed[f] = !in_scene;
        g->random_at[f] = g->random;
        memcpy(g->at[f], g->pos, sizeof g->pos);
        if (h->flips) playback_flip(shot);
    }
}

static int same(const XBOX_GAMEPAD *a, const XBOX_GAMEPAD *b) { return !memcmp(a, b, sizeof *a); }
static float distance3(const float a[3], const float b[3])
{
    return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

/* ---- the cases ----------------------------------------------------------- */

static char g_run[600];
/* The recording's checks and idle moment: each frame and its generator. */
static unsigned g_kept_frame[32], g_kept_random[32], g_idle_frame, g_idle_random;
static int g_kept, g_traces;

static void record(void)
{
    How h = { .frames = FRAMES, .skip_anchor = -1, .wrong_anchor = -1, .recording = 1, .set_at = 300 };
    char path[700], line[512];
    int pads = 0, anchors = 0, checks = 0, idles = 0, ends = 0, reads1 = 0, cams = 0, idles_kept = 0;
    int drawn_ok = 0, others = 0, settings = 0, first_settings = 0, set_later = 0, traces = 0, no_view = 0;
    int viewed = 0, fingerprinted = 0;
    unsigned trace_first = 0, trace_last = 0;
    unsigned fr, t, c, rnd, drawn, oth, before = 0;
    float x, y, z;
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
        /* Each check with the generator and the draws since the one before:
         * on the game's thread as many as the stand-in drew there, and none
         * on others; the idle moment with the generator. */
        if (!strncmp(line, "check ", 6) && g_kept < 32
            && sscanf(line + 6, "%u %f %f %f %x %u %x %u %u", &fr, &x, &y, &z, &t, &c, &rnd, &drawn, &oth) == 9) {
            unsigned want = 0, k;
            for (k = g_kept ? before + 1 : 1; k <= fr; k++) want += 1 + (k - 1) % 3;   /* frame k is pass k - 1 */
            drawn_ok += drawn == want;
            others += oth;
            g_kept_frame[g_kept] = fr;
            g_kept_random[g_kept++] = rnd;
            before = fr;
        }
        if (!strncmp(line, "idle ", 5)
            && sscanf(line + 5, "%u %f %f %f %x %u %x", &fr, &x, &y, &z, &t, &c, &rnd) == 7) {
            idles_kept++;
            g_idle_frame = fr;
            g_idle_random = rnd;
        }
        ends += !strcmp(line, "end 1000\n");
        cams += !strcmp(line, "freecam 1 0 0 0 5 5 0.1 0 1 5\n");
        /* The settings at the first frame, each once, and Default cam fix
         * off as the player turned it, at the next pass; the trace, every
         * frame from the first press; each check with the free camera's
         * angles, none while it steers no camera. */
        settings += !strncmp(line, "setting ", 8);
        first_settings += !strncmp(line, "setting 1 ", 10);
        set_later += !strcmp(line, "setting 302 dcamfix 0\n");
        if (sscanf(line, "trace %u", &fr) == 1) {
            const char *last = strrchr(line, ' ');
            unsigned rd = 0;
            float ex, ey, ez;
            if (!traces++) trace_first = fr;
            trace_last = fr;
            /* The eye, the reads (both of the pass's, before the camera
             * update) and the fingerprint's sixteen hashes, last. */
            fingerprinted += last && strlen(last + 1) == PLAYBACK_SKATER_PARTS * 8 + 1
                             && sscanf(line, "trace %*u %*f %*f %*f %*x %*u %*f %*f %*s %*s %f %f %f %u", &ex, &ey,
                                       &ez, &rd) == 4 && rd == READS;
        }
        no_view += !strncmp(line, "check ", 6) && strstr(line, " - -\n") != NULL;
    }
    fclose(f);
    g_set[0] = 1;                                   /* the player's again */
    CHECK(settings == SETTINGS + 1 && first_settings == SETTINGS && set_later == 1,
          "the recording's settings: %d lines, %d at the first frame, %d for Default cam fix turned off", settings,
          first_settings, set_later);
    for (fr = 50; fr < FRAMES; fr++) viewed += g_rec.viewed[fr];  /* pass f is recorded frame f + 1 */
    CHECK(traces == viewed && traces > 600 && trace_first == 51 && fingerprinted == traces,
          "the trace: %d frames, %u to %u, of the %d the skater was viewed from 51; %d with the eye, %d reads and"
          " the fingerprint", traces, trace_first, trace_last, viewed, fingerprinted, READS);
    g_traces = traces;
    CHECK(no_view == checks, "the checks' free camera: %d of %d with - -", no_view, checks);
    /* A check a second of frames from the first: 7 before the scene (400 to
     * 519, with no view), 8 after it. The free camera off, at its defaults,
     * from the first frame. */
    CHECK(anchors == ANCHORS && checks == 7 + 8 && idles == 1 && ends == 1 && pads > 100 && reads1 > 10
          && cams == 1,
          "the recording: %d pads (%d on a second read), %d anchors, %d checks, %d idle, %d ends, %d freecam",
          pads, reads1, anchors, checks, idles, ends, cams);
    CHECK(g_kept == checks && idles_kept == idles && drawn_ok == checks && others == 0,
          "the generator kept: %d of %d checks, %d of %d idle moments; %d checks with the stand-in's draws,"
          " %u on others", g_kept, checks, idles_kept, idles, drawn_ok, others);
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
    /* Drawn a number of times that follows the host: not as recorded from
     * the second check on, said, and put back at each. */
    for (i = 0; i < g_kept; i++)
        CHECK(g_play.random_at[g_kept_frame[i] - 1] == g_kept_random[i], "check at %u: the generator %08X,"
              " recorded %08X", g_kept_frame[i], g_play.random_at[g_kept_frame[i] - 1], g_kept_random[i]);
    CHECK(g_play.random_at[g_idle_frame - 1] == g_idle_random, "the idle moment: the generator %08X at frame %u,"
          " kept %08X", g_play.random_at[g_idle_frame - 1], g_idle_frame, g_idle_random);
    CHECK(res->random_checks == g_kept && res->random_same >= 1 && res->random_same < res->random_checks
          && res->random_apart_at == g_kept_frame[1],
          "the generator at the checks: %d compared, %d as recorded, the first not at %u", res->random_checks,
          res->random_same, res->random_apart_at);
    CHECK(res->state == PLAYBACK_FINISHED && res->anchors_met == ANCHORS && res->drift_most < 0.01f
          && res->idles_put_back == 1 && g_closed == 1,
          "as recorded: state %d, anchors %d, drift %.2f, idles %d, closed %d", res->state, res->anchors_met,
          res->drift_most, res->idles_put_back, g_closed);
    CHECK(res->trace_frames == g_traces && res->trace_same == res->trace_frames
          && res->freecam_checks == g_kept && res->freecam_same == res->freecam_checks && res->follows == 0,
          "as recorded: the trace %d of %d frames, the free camera %d of %d checks, %d takeovers", res->trace_same,
          res->trace_frames, res->freecam_same, res->freecam_checks, res->follows);
}

/* The recording's settings in place of the player's for the play: Skip
 * cutscenes, the player's off, and Draw distance, the player's 4x, as
 * recorded from the first frame, and Default cam fix turned off where the
 * recording's player turned it off; the player's back at the end. The mark
 * green as it ends. */
static void play_settings(void)
{
    How h = { .frames = FRAMES + 10, .skip_anchor = -1, .wrong_anchor = -1, .set_at = 0 };
    const PLAYBACK_RESULT *res = playback_result();
    PbMarkShape shape;
    g_set[3] = 0;
    g_set[4] = 4;
    CHECK(playback_play(g_run, 0), "playback couldn't start");
    run(&g_play, &h);
    CHECK(res->state == PLAYBACK_FINISHED && res->settings_changed == 2 && g_play.dcamfix[200] == 1
          && g_play.dcamfix[400] == 0 && g_set[0] == 1 && g_set[3] == 0 && g_set[4] == 4,
          "the recording's settings: state %d, %d not the player's, Default cam fix %d then %d, after the play"
          " %d, Skip cutscenes %d, Draw distance %d", res->state, res->settings_changed, g_play.dcamfix[200],
          g_play.dcamfix[400], g_set[0], g_set[3], g_set[4]);
    CHECK(playback_mark(pad_now() + 1.0, &shape) && shape.disc == (0xFF000000u | PB_MARK_GREEN),
          "the mark as the play ended on route: %08X", shape.disc);
    g_set[4] = 1;
}

/* The mark, frame by frame: in at its first draw, white while the play is
 * on route; green, red or blue as it ends, held, then out; and its quads. */
static void mark(void)
{
    static const struct { int how; uint32_t colour; } k_end[] = {
        { PLAYBACK_MARK_FINISHED, PB_MARK_GREEN }, { PLAYBACK_MARK_OFF_ROUTE, PB_MARK_RED },
        { PLAYBACK_MARK_TAKEN, PB_MARK_BLUE },
    };
    PbMark m;
    PbMarkShape s;
    uint8_t mem[PB_MARK_BYTES + 16];
    float x, y;
    unsigned k;
    memset(&m, 0, sizeof m);
    CHECK(!playback_mark_frame(&m, 1.0, &s), "the mark shown before a play");
    playback_mark_end(&m, PLAYBACK_MARK_FINISHED, 1.0);
    CHECK(!playback_mark_frame(&m, 1.0, &s), "the mark shown, ended with no play");
    for (k = 0; k < sizeof k_end / sizeof k_end[0]; k++) {
        playback_mark_play(&m);
        CHECK(!playback_mark_frame(&m, 10.0, &s), "shown at its first draw's start, at alpha %.3f", s.alpha);
        CHECK(playback_mark_frame(&m, 10.1, &s) && fabsf(s.alpha - 0.4f) < 1e-4f
              && fabsf(s.r - PB_MARK_SIZE * 0.5f * (float)(PB_MARK_IN_FROM - 0.15 * 0.4)) < 1e-3f,
              "coming in: alpha %.3f, radius %.3f", s.alpha, s.r);
        CHECK(playback_mark_frame(&m, 12.0, &s) && s.alpha == 1.0f && s.disc == 0xFFFFFFFFu
              && s.cx == PB_MARK_LEFT + PB_MARK_SIZE / 2 && s.cy == PB_MARK_MIDDLE_Y && s.r == PB_MARK_SIZE / 2,
              "on route: alpha %.3f, disc %08X at %.1f %.1f", s.alpha, s.disc, s.cx, s.cy);
        playback_mark_end(&m, k_end[k].how, 20.0);
        CHECK(playback_mark_frame(&m, 20.5, &s) && s.disc == (0xFF000000u | k_end[k].colour),
              "ended (%d): disc %08X, not %06X", k_end[k].how, s.disc, k_end[k].colour);
        CHECK(playback_mark_frame(&m, 22.1, &s) && s.disc == (0xFF000000u | k_end[k].colour),
              "ended (%d), held: disc %08X", k_end[k].how, s.disc);
        CHECK(playback_mark_frame(&m, 22.15 + PB_MARK_OUT_TIME / 2, &s) && s.alpha > 0.4f && s.alpha < 0.6f
              && s.r > PB_MARK_SIZE / 2, "going out: alpha %.3f, radius %.3f", s.alpha, s.r);
        CHECK(!playback_mark_frame(&m, 22.15 + PB_MARK_OUT_TIME + 0.01, &s), "still shown after going out");
    }
    /* Ended before it came in: straight to its colour. */
    playback_mark_play(&m);
    playback_mark_end(&m, PLAYBACK_MARK_TAKEN, 5.0);
    CHECK(playback_mark_frame(&m, 5.5, &s) && s.disc == (0xFF000000u | PB_MARK_BLUE),
          "taken before it came in: disc %08X", s.disc);
    /* Its quads: the disc's top band from its top point, its bottom band to
     * its bottom point, and the play sign's tip twice. */
    playback_mark_play(&m);
    playback_mark_frame(&m, 0.0, &s);
    playback_mark_frame(&m, 1.0, &s);
    memset(mem, 0, sizeof mem);
    playback_mark_vertices(mem, 8, &s);
    memcpy(&x, mem + 8, 4);
    memcpy(&y, mem + 8 + 4, 4);
    CHECK(x == s.cx && y == s.cy - s.r, "the disc's top: %.2f %.2f", x, y);
    memcpy(&y, mem + 8 + ((PB_MARK_BANDS - 1) * 4 + 3) * 0x18 + 4, 4);
    CHECK(fabsf(y - (s.cy + s.r)) < 1e-3f, "the disc's bottom: %.2f", y);
    memcpy(&x, mem + 8 + (PB_MARK_BANDS * 4 + 1) * 0x18, 4);
    memcpy(&y, mem + 8 + (PB_MARK_BANDS * 4 + 3) * 0x18 + 4, 4);
    CHECK(x > s.cx && y == s.cy, "the play sign's tip: %.2f %.2f", x, y);
    {
        uint32_t c;
        memcpy(&c, mem + 8 + (PB_MARK_BANDS * 4) * 0x18 + 8, 4);
        CHECK(c == (0xFF000000u | PB_MARK_SIGN), "the play sign's colour %08X", c);
        CHECK(mem[8 + PB_MARK_BYTES] == 0 && mem[7] == 0, "the quads written past their bytes");
    }
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
    CHECK(res->follows >= 2 && res->follows_same == res->follows && res->freecam_checks > 5
          && res->freecam_same == res->freecam_checks && res->trace_same == res->trace_frames
          && res->trace_frames > 600,
          "free camera, as recorded: %d of %d takeovers, %d of %d checks, the trace %d of %d frames",
          res->follows_same, res->follows, res->freecam_same, res->freecam_checks, res->trace_same,
          res->trace_frames);
    freecam_frame();
    CHECK(!freecam_on() && pref("freecam") == 0 && pref("sensitivity_x") == 2 && pref("deadzone") == 0.3
          && pref("autocentre") == 1 && pref("invert_x") == 1 && cam_row("Sensitivity Y")->value() == 9
          && pref("mouse") == 0 && pref("mouse_sensitivity") == 9 && !g_mouse_want(),
          "the player's free camera after playback: on %d, freecam= %g, sensitivity_x= %g, deadzone= %g",
          freecam_on(), pref("freecam"), pref("sensitivity_x"), pref("deadzone"));
}

/* The free camera turned a hundredth of a radian at frame 130, as nothing
 * in the recording turned it: the trace says so from that frame, the check
 * after it says so and puts it back, and from there the eye is where it
 * was from the skater (who went another way meanwhile, until the idle
 * moment put it back). */
static void play_cam_apart(void)
{
    How h = { .frames = FRAMES + 10, .skip_anchor = -1, .wrong_anchor = -1, .yaw_at = 130, .yaw_by = 0.01 };
    const PLAYBACK_RESULT *res = playback_result();
    int f, bad = 0;
    cam_yours();
    CHECK(playback_play(g_cam_run, 1), "playback with the free camera couldn't start");
    run(&g_play, &h);
    /* The eye from the skater, which went another way meanwhile. */
    for (f = 182; f < 300 && !bad; f++) {
        float px = g_play.eye[f][0] - g_play.at[f - 1][0], pz = g_play.eye[f][2] - g_play.at[f - 1][2];
        float rx = g_cam.eye[f][0] - g_cam.at[f - 1][0], rz = g_cam.eye[f][2] - g_cam.at[f - 1][2];
        if ((fabsf(px - rx) > 1e-4f || fabsf(pz - rz) > 1e-4f) && ++bad)
            CHECK(0, "the free camera put back: frame %d's eye from the skater %.5f %.5f, recorded %.5f %.5f", f,
                  px, pz, rx, rz);
    }
    CHECK(!memcmp(g_play.eye[130], g_cam.eye[130], sizeof g_cam.eye[130])
          && memcmp(g_play.eye[131], g_cam.eye[131], sizeof g_cam.eye[131]),
          "the free camera turned at 130: the eye as recorded at 130 %d, at 131 %d",
          !memcmp(g_play.eye[130], g_cam.eye[130], sizeof g_cam.eye[130]),
          !memcmp(g_play.eye[131], g_cam.eye[131], sizeof g_cam.eye[131]));
    CHECK(res->state == PLAYBACK_FINISHED && res->freecam_same == res->freecam_checks - 1
          && res->freecam_apart_at == 181 && res->trace_apart_at == 131
          && res->trace_same >= 131 - 51 && res->trace_same < res->trace_frames && res->drift_most > 0.0f,
          "the free camera apart: state %d, %d of %d checks, the first at %u, the trace %d of %d from %u,"
          " drift %.3f", res->state, res->freecam_same, res->freecam_checks, res->freecam_apart_at,
          res->trace_same, res->trace_frames, res->trace_apart_at, res->drift_most);
    /* What went first: the free camera at 131, turned after it placed the
     * eye, then the eye and the look at 132; the pad's reads never. */
    CHECK(res->trace_first[PLAYBACK_TRACE_FREECAM] == 131 && res->trace_first[PLAYBACK_TRACE_EYE] == 132
          && res->trace_first[PLAYBACK_TRACE_LOOK] == 132 && res->trace_first[PLAYBACK_TRACE_READS] == 0
          && (res->trace_first[PLAYBACK_TRACE_PLACE] == 0 || res->trace_first[PLAYBACK_TRACE_PLACE] >= 132),
          "the free camera apart, first: free camera %u, eye %u, look %u, reads %u, place %u",
          res->trace_first[PLAYBACK_TRACE_FREECAM], res->trace_first[PLAYBACK_TRACE_EYE],
          res->trace_first[PLAYBACK_TRACE_LOOK], res->trace_first[PLAYBACK_TRACE_READS],
          res->trace_first[PLAYBACK_TRACE_PLACE]);
}

/* The skater's state changed where nothing follows it, at frame 300: the
 * trace's fingerprint finds it at the first frame the skater is viewed from
 * there, in the part that holds it, with everything else as recorded, and
 * the play goes on to its end. */
static void play_state_apart(void)
{
    How h = { .frames = FRAMES + 10, .skip_anchor = -1, .wrong_anchor = -1, .state_at = 300 };
    const PLAYBACK_RESULT *res = playback_result();
    const int part = SKATER_HIDDEN / (PLAYBACK_SKATER_BYTES / PLAYBACK_SKATER_PARTS);
    unsigned at = 0, f;
    int k, others = 0, whats = 0, before = 0;
    g_closed = 0;
    CHECK(playback_play(g_run, 1), "playback couldn't start");
    run(&g_play, &h);
    for (f = 300; f < FRAMES && !g_play.viewed[f]; f++) { }
    at = f + 1;                                                   /* pass f is recorded frame f + 1 */
    for (f = 50; f + 1 < at; f++) before += g_rec.viewed[f];      /* the trace's frames before it */
    for (k = 0; k < PLAYBACK_SKATER_PARTS; k++) others += k != part && res->trace_part_first[k] != 0;
    for (k = 0; k < PLAYBACK_TRACE_WHATS; k++) whats += res->trace_first[k] != 0;
    CHECK(res->state == PLAYBACK_FINISHED && res->trace_apart_at == at && res->trace_part_first[part] == at
          && others == 0 && whats == 0 && res->trace_same == before,
          "the skater's state apart at 300: state %d, the trace first apart at %u, the part at %u (expected %u),"
          " %d other parts, %d other things, %d of %d as recorded", res->state, res->trace_apart_at,
          res->trace_part_first[part], at, others, whats, res->trace_same, res->trace_frames);
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
    /* Checks by hand: one made before the generator was kept leaves it as
     * it is; one with it puts it back, and its draws are compared. */
    write_file(folder, "jsrf-pc-recording 1\ncheck 3 0 0 0 0 3\nend 10\n");
    h.frames = 12;
    CHECK(playback_play(folder, 0), "a check without the generator refused");
    run(&g_play, &h);
    CHECK(playback_result()->random_checks == 0 && playback_state() == PLAYBACK_FINISHED,
          "a check without the generator: %d compared, state %d", playback_result()->random_checks,
          playback_state());
    write_file(folder, "jsrf-pc-recording 1\ncheck 3 0 0 0 0 3 12345678 9 0\nidle 5 0 0 0 0 3 0BADCAFE\n"
               "end 10\n");
    CHECK(playback_play(folder, 0), "a check with the generator refused");
    run(&g_play, &h);
    CHECK(g_play.random_at[2] == 0x12345678u && playback_result()->random_checks == 1
          && playback_result()->random_same == 0 && playback_result()->random_apart_at == 3,
          "a check with the generator: %08X at frame 3, %d compared, %d as recorded, apart at %u",
          g_play.random_at[2], playback_result()->random_checks, playback_result()->random_same,
          playback_result()->random_apart_at);
    h.frames = 30;
    write_file(folder, "jsrf-pc-recording 1\ncheck 3 0 0 0 0 3 12345678\nend 10\n");
    CHECK(!playback_play(folder, 0), "a check with the generator and no draws, played");
    write_file(folder, "jsrf-pc-recording 1\nidle 3 0 0 0 0 3 12345678 9 0\nend 10\n");
    CHECK(!playback_play(folder, 0), "an idle moment with draws, played");
    /* The free camera off at frame 1 and taking over at frame 2, by hand:
     * put back as the follow line has it, and a check's angles put back at
     * frame 3. */
    write_file(folder, "jsrf-pc-recording 1\nfreecam 1 0 0 0 5 5 0.15 0 0 5\nfreecam 2 1 0 0 5 5 0.15 0 0 5\n"
               "follow 2 0.5 0.125\ncheck 3 0 0 0 0 3 12345678 9 0 0.25 -0.0625\nend 10\n");
    h.frames = 2;
    CHECK(playback_play(folder, 0), "a takeover by hand refused");
    run(&g_play, &h);
    {
        double yaw = 0, pitch = 0;
        CHECK(freecam_angles(&yaw, &pitch) && yaw == 0.5 && pitch == 0.125 && playback_result()->follows == 1
              && playback_result()->follows_same == 0,
              "by hand, the takeover: heading %.4f, pitch %.4f, %d compared", yaw, pitch, playback_result()->follows);
        h.frames = 12;
        CHECK(playback_play(folder, 0), "a takeover by hand refused");
        run(&g_play, &h);
        freecam_angles(&yaw, &pitch);
        CHECK(playback_result()->freecam_checks == 1 && playback_result()->freecam_same == 0
              && playback_result()->freecam_apart_at == 3 && yaw == 0.25 && pitch == -0.0625,
              "by hand, the check's free camera: %d compared, %d as recorded, heading %.4f, pitch %.4f",
              playback_result()->freecam_checks, playback_result()->freecam_same, yaw, pitch);
        freecam_frame();
    }
    h.frames = 30;
    write_file(folder, "jsrf-pc-recording 1\ncheck 3 0 0 0 0 3 12345678 9 0 0.25\nend 10\n");
    CHECK(!playback_play(folder, 0), "a check with one of the free camera's two, played");
    write_file(folder, "jsrf-pc-recording 1\ncheck 3 0 0 0 0 3 12345678 9 0 - 0.25\nend 10\n");
    CHECK(!playback_play(folder, 0), "a check with the free camera steering and not, played");
    write_file(folder, "jsrf-pc-recording 1\ncheck 3 0 0 0 0 3 12345678 9 0 - -\n"
               "trace 4 0 0 0 0 3 0 10 - -\ntrace 5 0 0 0 0 3 0 10 0.5 0.25\nend 10\n");
    CHECK(playback_play(folder, 0), "checks and traces by hand refused");
    write_file(folder, "jsrf-pc-recording 1\ntrace 4 0 0 0 0 3 0 10 -\n");
    CHECK(!playback_play(folder, 0), "a trace line short of its pitch, played");
    /* Since playback5's: the eye, the reads and the fingerprint, or - for
     * none; a fingerprint that isn't sixteen hashes, or anything short or
     * after it, is refused. */
    {
        char line[600], parts[PLAYBACK_SKATER_PARTS * 8 + 1];
        int k;
        for (k = 0; k < PLAYBACK_SKATER_PARTS; k++) memcpy(parts + k * 8, "0123ABCD", 8);
        parts[PLAYBACK_SKATER_PARTS * 8] = 0;
        snprintf(line, sizeof line, "jsrf-pc-recording 1\ntrace 4 0 0 0 0 3 0 10 - - 1 2 3 2 -\n"
                 "trace 5 0 0 0 0 3 0 10 0.5 0.25 1 2 3 2 %s\nend 10\n", parts);
        write_file(folder, line);
        CHECK(playback_play(folder, 0), "traces with the eye, the reads and the fingerprint refused");
        snprintf(line, sizeof line, "jsrf-pc-recording 1\ntrace 5 0 0 0 0 3 0 10 - - 1 2 3 2 %.*s\n",
                 PLAYBACK_SKATER_PARTS * 8 - 1, parts);
        write_file(folder, line);
        CHECK(!playback_play(folder, 0), "a fingerprint a digit short, played");
        parts[9] = 'G';
        snprintf(line, sizeof line, "jsrf-pc-recording 1\ntrace 5 0 0 0 0 3 0 10 - - 1 2 3 2 %s\n", parts);
        write_file(folder, line);
        CHECK(!playback_play(folder, 0), "a fingerprint with a G in it, played");
        write_file(folder, "jsrf-pc-recording 1\ntrace 5 0 0 0 0 3 0 10 - - 1 2 3 2\n");
        CHECK(!playback_play(folder, 0), "a trace line short of its fingerprint, played");
        write_file(folder, "jsrf-pc-recording 1\ntrace 5 0 0 0 0 3 0 10 - - 1 2 3 2 - 7\n");
        CHECK(!playback_play(folder, 0), "a trace line with a word after its fingerprint, played");
    }
    write_file(folder, "jsrf-pc-recording 1\ntrace 5 0 0 0 0 3 0 10 - -\ntrace 5 0 0 0 0 3 0 10 - -\n");
    CHECK(!playback_play(folder, 0), "a trace line twice for a frame, played");
    write_file(folder, "jsrf-pc-recording 1\nfollow 3 - -\n");
    CHECK(!playback_play(folder, 0), "a takeover with no angles, played");
    write_file(folder, "jsrf-pc-recording 1\nsetting 1 dcamfix 1\nsetting 1 farshadowsahead 4\nend 10\n");
    CHECK(playback_play(folder, 0), "settings by hand refused");
    write_file(folder, "jsrf-pc-recording 1\nsetting 1 dcamfix 2\n");
    CHECK(!playback_play(folder, 0), "a Default cam fix of 2, played");
    write_file(folder, "jsrf-pc-recording 1\nsetting 1 drawdistance 5\n");
    CHECK(!playback_play(folder, 0), "a draw distance of 5, played");
    write_file(folder, "jsrf-pc-recording 1\nsetting 1 volume 3\n");
    CHECK(!playback_play(folder, 0), "a setting it doesn't keep, played");
    write_file(folder, "jsrf-pc-recording 1\nsetting 5 dcamfix 1\nsetting 4 skipintro 1\n");
    CHECK(!playback_play(folder, 0), "a setting line back in time, played");
    write_file(folder, "jsrf-pc-recording 1\nwait 10\n");
    CHECK(!playback_play(folder, 0), "a word it doesn't know, played");
    snprintf(folder, sizeof folder, "%s/none", g_dir);
    CHECK(!playback_play(folder, 0), "no recording, played");
}

/* The game restarting (port/main.c's restart): with nothing running,
 * nothing said, between the two marks run.py looks between; a recording
 * ends, kept, "the game restarted" its reason; a play stops, with its
 * summary. */
static void restart(void)
{
    char folder[700], path[760], line[256], last[256] = "";
    How rec = { .frames = 40, .skip_anchor = -1, .wrong_anchor = -1, .recording = 1 };
    How h = { .frames = 25, .skip_anchor = -1, .wrong_anchor = -1 };
    FILE *f;
    CHECK(playback_state() != PLAYBACK_RECORDING && playback_state() != PLAYBACK_PLAYING
          && playback_state() != PLAYBACK_WAITING, "something runs before the restart: state %d", playback_state());
    fflush(stderr);
    fprintf(stderr, "<restart, nothing running>\n");
    playback_restart();
    fprintf(stderr, "</restart>\n");
    fflush(stderr);
    snprintf(folder, sizeof folder, "%s/restart", g_dir);
    CHECK(playback_record(folder), "a recording to restart couldn't start");
    run(&g_play, &rec);
    playback_restart();
    CHECK(playback_state() == PLAYBACK_RECORDED && playback_result()->frames == 40,
          "restarted while recording: state %d after %u frames", playback_state(), playback_result()->frames);
    snprintf(path, sizeof path, "%s/" PLAYBACK_FILE, folder);
    f = fopen(path, "r");
    CHECK(f != NULL, "no %s", path);
    if (f) {
        while (fgets(line, sizeof line, f)) snprintf(last, sizeof last, "%s", line);
        fclose(f);
    }
    CHECK(!strcmp(last, "end 40\n"), "restarted while recording, its last line: %s", last);
    CHECK(playback_play(folder, 1), "the restarted recording refused");
    g_closed = 0;
    run(&g_play, &h);
    playback_restart();
    CHECK(playback_state() == PLAYBACK_OFF && !strcmp(playback_result()->why, "the game restarted at frame 25")
          && g_closed == 0, "restarted while playing: state %d (%s), closed %d", playback_state(),
          playback_result()->why, g_closed);
}

/* A run started from Developer > Missions: its recording says where, with
 * Record ticked from then; a play of it starts there again (playback_first
 * puts the start in Missions' variables) and goes on where the run started
 * there, as or with the same, and is off route at its first frame where it
 * didn't; so is a recording from the game's own start played in a run
 * started at a mission. Load Game the same. A start line that can't be read,
 * comes twice or after a frame is refused. */
static void start_line(const char *line, int want_ok, const char *what)
{
    char folder[700], text[300];
    snprintf(folder, sizeof folder, "%s/start-bad", g_dir);
    snprintf(text, sizeof text, "jsrf-pc-recording 1\n%send 5\n", line);
    write_file(folder, text);
    CHECK(!playback_play(folder, 0) == !want_ok, "%s: %s", what, want_ok ? "refused" : "played");
}

static void missions_start_line(void)
{
    char folder[700], load[700], refused[700], path[760], line[256];
    const char *heads[3] = { "jsrf-pc-recording 1\n", "version ", "# started " };
    How rec = { .frames = 40, .skip_anchor = -1, .wrong_anchor = -1, .recording = 1 };
    How h = { .frames = 30, .skip_anchor = -1, .wrong_anchor = -1 };
    FILE *f;
    int i;
    const char *m, *k, *l, *p;
    snprintf(folder, sizeof folder, "%s/from-mission", g_dir);
    CHECK(!playback_missions_ticked(), "Developer > Missions > Record ticked before a run from a mission");
    g_start.mission = 252;
    g_start.skater = 6;
    g_start.progress = 0x1A2B3C4Du;
    g_photo = 1;
    /* The mission refused after the recording began (missions_paths, before
     * the game's first pass): no start line, and Record left unticked. */
    snprintf(refused, sizeof refused, "%s/from-refused", g_dir);
    g_started = 1;
    CHECK(playback_record(refused), "a recording from a refused mission couldn't start");
    g_started = 0;
    run(&g_play, &rec);
    playback_stop();
    snprintf(path, sizeof path, "%s/" PLAYBACK_FILE, refused);
    f = fopen(path, "r");
    CHECK(f != NULL, "no %s", path);
    if (!f) return;
    i = 0;
    while (fgets(line, sizeof line, f)) i += !strncmp(line, "start ", 6);
    fclose(f);
    CHECK(i == 0 && !playback_missions_ticked(), "a refused mission's recording: %d start lines, Record %s", i,
          playback_missions_ticked() ? "ticked" : "not ticked");

    g_started = 1;
    CHECK(playback_record(folder), "a recording from a mission couldn't start");
    run(&g_play, &rec);
    CHECK(playback_missions_ticked(), "Record not ticked in a run recording from a mission");
    playback_stop();
    snprintf(path, sizeof path, "%s/" PLAYBACK_FILE, folder);
    f = fopen(path, "r");
    CHECK(f != NULL, "no %s", path);
    if (!f) return;
    for (i = 0; i < 3 && fgets(line, sizeof line, f); i++)
        CHECK(!strncmp(line, heads[i], strlen(heads[i])), "the recording from a mission, line %d: %s", i + 1, line);
    CHECK(fgets(line, sizeof line, f) && !strcmp(line, "start mission 0252 6 1 1A2B3C4D\n"),
          "its start line: %s", line);
    /* The settings as the run started, then its frames. */
    for (i = 0; i < SETTINGS; i++)
        CHECK(fgets(line, sizeof line, f) && !strncmp(line, "setting 1 ", 10), "a setting line next: %s", line);
    CHECK(fgets(line, sizeof line, f) && !strncmp(line, "pad ", 4), "a frame's line next: %s", line);
    fclose(f);
    playback_missions_flip();
    CHECK(!playback_missions_ticked(), "Record not unticked");
    playback_missions_flip();

    /* About to play it: Missions' and photo mode's variables, as the pick
     * set them; the play's own left for playback_start. */
    g_started = 0;
    g_photo = 0;
    setenv("JSRF_PLAY_MENU", folder, 1);
    setenv("JSRF_START_LOAD", "1", 1);
    playback_first();
    m = getenv("JSRF_START_MISSION");
    k = getenv("JSRF_START_SKATER");
    l = getenv("JSRF_START_LOAD");
    p = getenv(PHOTO_START_VAR);
    CHECK(m && !strcmp(m, "0252") && k && !strcmp(k, "6") && (!l || !*l) && p && !strcmp(p, "1")
          && getenv("JSRF_PLAY_MENU"), "playback_first: JSRF_START_MISSION=%s JSRF_START_SKATER=%s"
          " JSRF_START_LOAD=%s JSRF_START_PHOTO=%s", m ? m : "-", k ? k : "-", l ? l : "-", p ? p : "-");
    setenv("JSRF_START_MISSION", "", 1);
    setenv("JSRF_START_SKATER", "", 1);
    setenv(PHOTO_START_VAR, "", 1);
    setenv("JSRF_PLAY_MENU", "", 1);

    /* Played where it started: on route. */
    g_started = 1;
    g_photo = 1;
    CHECK(playback_play(folder, 1), "the recording from a mission refused");
    run(&g_play, &h);
    CHECK(playback_state() == PLAYBACK_PLAYING, "played from its mission: state %d (%s)", playback_state(),
          playback_result()->why);
    /* With other progress (another start-states.txt): off route at once,
     * and --play's game closed. */
    g_start.progress = 0x99u;
    g_closed = 0;
    CHECK(playback_play(folder, 1), "the recording from a mission refused");
    run(&g_play, &h);
    CHECK(playback_state() == PLAYBACK_OFF_ROUTE && playback_result()->frames == 0 && g_closed == 1
          && !strcmp(playback_result()->why, "the recording started at mission 0252, skater 6, progress 1A2B3C4D,"
                     " photo mode; this run at mission 0252, skater 6, progress 00000099, photo mode"),
          "played with other progress: state %d after %u frames (%s), closed %d", playback_state(),
          playback_result()->frames, playback_result()->why, g_closed);
    /* In a run from the game's own start. */
    g_started = 0;
    CHECK(playback_play(folder, 0), "the recording from a mission refused");
    run(&g_play, &h);
    CHECK(playback_state() == PLAYBACK_OFF_ROUTE && strstr(playback_result()->why, "this run at the game's own start"),
          "played from the game's own start: state %d (%s)", playback_state(), playback_result()->why);
    /* A recording from the game's own start, in a run from a mission. */
    g_started = 1;
    g_photo = 0;
    CHECK(playback_play(g_run, 0), "the first recording refused");
    run(&g_play, &h);
    CHECK(playback_state() == PLAYBACK_OFF_ROUTE
          && strstr(playback_result()->why, "the recording started at the game's own start; this run at mission"),
          "the game's own start's recording from a mission: state %d (%s)", playback_state(),
          playback_result()->why);

    /* Load Game: recorded, put in place, and played there. */
    g_start.mission = -1;
    g_start.skater = -1;
    g_start.progress = 0;
    snprintf(load, sizeof load, "%s/from-load", g_dir);
    CHECK(playback_record(load), "a recording from Load Game couldn't start");
    playback_stop();
    setenv("JSRF_PLAY", load, 1);
    setenv("JSRF_START_MISSION", "0110", 1);
    playback_first();
    m = getenv("JSRF_START_MISSION");
    l = getenv("JSRF_START_LOAD");
    p = getenv(PHOTO_START_VAR);
    CHECK((!m || !*m) && l && !strcmp(l, "1") && (!p || !*p), "playback_first at Load Game:"
          " JSRF_START_MISSION=%s JSRF_START_LOAD=%s JSRF_START_PHOTO=%s", m ? m : "-", l ? l : "-", p ? p : "-");
    setenv("JSRF_START_LOAD", "", 1);
    setenv("JSRF_PLAY", "", 1);
    CHECK(playback_play(load, 0), "the recording from Load Game refused");
    run(&g_play, &h);
    CHECK(playback_state() == PLAYBACK_FINISHED, "played from Load Game: state %d (%s)", playback_state(),
          playback_result()->why);
    /* Nothing put in place for a recording from the game's own start. */
    setenv("JSRF_PLAY", g_run, 1);
    playback_first();
    CHECK(!getenv("JSRF_START_MISSION") || !*getenv("JSRF_START_MISSION"), "playback_first without a start line");
    setenv("JSRF_PLAY", "", 1);
    g_started = 0;

    start_line("start mission 0252 6 0 1A2B3C4D\n", 1, "a start line");
    start_line("start load 1\n", 1, "a start line at Load Game");
    start_line("start mission 0252 -1 0 0\n", 1, "a start line with New Game's skater and progress");
    start_line("pad 0 0 0000 0000000000000000 0 0 0 0\nstart load 0\n", 0, "a start line after a frame");
    start_line("start load 0\nstart load 0\n", 0, "two start lines");
    start_line("start mission 252\n", 0, "a start line short of its skater, photo and progress");
    start_line("start mission 0252 24 0 0\n", 0, "a start line with skater 24");
    start_line("start mission 0252 6 2 0\n", 0, "a start line with photo 2");
    start_line("start load\n", 0, "a start line at Load Game without photo");
    start_line("start title 0\n", 0, "a start line at the title");
}

/* Input > Behaviour > Camera's ticks: recorded at the first camera update
 * and when they change, then played with the player's left as they were;
 * by hand, the player's before the first camera line and the recording's
 * from there; with none, the shots as before the ticks; the player's change
 * hands the game back; a line out of its range, short of a tick or back in
 * time, refused. And the free camera after the game's shots
 * (port/freecam.h): the recording's fifth field says how it glided, eased
 * back or neither, and one without it, made before, plays as before; the
 * player's own glides quicker, the controls following where it glides to. */
static void camera_ticks(void)
{
    char folder[700], path[760], line[256];
    unsigned at[2] = { 0, 0 }, t[2][3], back[2] = { 0, 0 };
    int lines = 0, f;
    FILE *fp;
    How rec = { .frames = 30, .skip_anchor = -1, .wrong_anchor = -1, .recording = 1,
                .ticks = 7, .ticks_at = 12, .ticks2 = 5 };
    How h = { .frames = 30, .skip_anchor = -1, .wrong_anchor = -1, .ticks = 7 };
    snprintf(folder, sizeof folder, "%s/ticks", g_dir);
    CHECK(playback_record(folder), "a recording of the Camera ticks couldn't start");
    run(&g_play, &rec);
    playback_stop();
    snprintf(path, sizeof path, "%s/" PLAYBACK_FILE, folder);
    fp = fopen(path, "r");
    CHECK(fp != NULL, "no %s", path);
    if (!fp) return;
    while (fgets(line, sizeof line, fp))
        if (!strncmp(line, "camera ", 7)) {
            if (lines < 2)
                CHECK(sscanf(line, "camera %u %u %u %u %u", &at[lines], &t[lines][0], &t[lines][1], &t[lines][2],
                             &back[lines]) == 5 && back[lines] == PLAYBACK_BACK_OUT,
                      "a camera line: %s", line);
            lines++;
        }
    fclose(fp);
    CHECK(lines == 2 && at[1] == at[0] + 12 && t[0][0] && t[0][1] && t[0][2] && t[1][0] && !t[1][1] && t[1][2],
          "the Camera ticks' recording: %d camera lines, at %u and %u", lines, at[0], at[1]);
    /* Played with the player's ticks all on: the recording's in their place. */
    CHECK(playback_freecam_back() == PLAYBACK_BACK_OUT, "the player's free camera doesn't glide after a shot");
    CHECK(playback_play(folder, 0), "the Camera ticks' recording refused");
    CHECK(playback_freecam_back() == PLAYBACK_BACK_OUT, "the recording's free camera doesn't glide after a shot");
    h.frames = 34;                  /* past its end */
    run(&g_play, &h);
    h.frames = 30;
    for (f = 0; f < 29; f++)
        CHECK(g_play.ticks[f] == (f < 12 ? 7u : 5u), "the Camera ticks played: %u at pass %d", g_play.ticks[f], f);
    CHECK(playback_state() == PLAYBACK_FINISHED, "the Camera ticks played: state %d", playback_state());
    /* By hand: Half pipe cam alone from frame 3, the player's before. Made
     * before the ease back, so none while it plays, and the player's own
     * once it ends. */
    write_file(folder, "jsrf-pc-recording 1\ncamera 3 0 1 0\nend 20\n");
    CHECK(playback_play(folder, 0), "Camera ticks written by hand refused");
    CHECK(playback_freecam_back() == PLAYBACK_BACK_NONE, "a recording made before the ease back, eased back");
    run(&g_play, &h);
    CHECK(g_play.ticks[0] == 7 && g_play.ticks[1] == 7 && g_play.ticks[2] == 2 && g_play.ticks[18] == 2
          && playback_state() == PLAYBACK_FINISHED, "by hand, the Camera ticks: %u %u %u, state %d",
          g_play.ticks[1], g_play.ticks[2], g_play.ticks[18], playback_state());
    CHECK(playback_freecam_back() == PLAYBACK_BACK_OUT, "the player's free camera after playback doesn't glide");
    /* By hand with the fifth field: 4 (cutting into the graffiti shot,
     * gliding out), 3 (gliding in too, quicker, the controls following
     * where it glides to, as camera5 wrote it), 2 (gliding, as camera4
     * wrote it), 1 (the angle eased back, as camera3 wrote it), and 0. */
    write_file(folder, "jsrf-pc-recording 1\ncamera 3 0 1 0 4\nend 20\n");
    CHECK(playback_play(folder, 0) && playback_freecam_back() == PLAYBACK_BACK_OUT,
          "a camera line cutting in, refused or not");
    write_file(folder, "jsrf-pc-recording 1\ncamera 3 0 1 0 3\nend 20\n");
    CHECK(playback_play(folder, 0) && playback_freecam_back() == PLAYBACK_BACK_STEADY,
          "a camera line gliding quicker, refused or not");
    write_file(folder, "jsrf-pc-recording 1\ncamera 3 0 1 0 2\nend 20\n");
    CHECK(playback_play(folder, 0) && playback_freecam_back() == PLAYBACK_BACK_GLIDE,
          "a camera line gliding, refused or not gliding");
    write_file(folder, "jsrf-pc-recording 1\ncamera 3 0 1 0 1\nend 20\n");
    CHECK(playback_play(folder, 0) && playback_freecam_back() == PLAYBACK_BACK_EASE,
          "a camera line easing back, refused or not easing");
    write_file(folder, "jsrf-pc-recording 1\ncamera 3 0 1 0 0\nend 20\n");
    CHECK(playback_play(folder, 0) && playback_freecam_back() == PLAYBACK_BACK_NONE,
          "a camera line not easing back, refused or easing");
    /* None: the shots as before the ticks, from the first, and no ease back. */
    write_file(folder, "jsrf-pc-recording 1\nend 10\n");
    CHECK(playback_play(folder, 0), "a recording with no camera line refused");
    CHECK(playback_freecam_back() == PLAYBACK_BACK_NONE, "a recording with no camera line, eased back");
    run(&g_play, &h);
    CHECK(g_play.ticks[0] == PLAYBACK_CAMSCENES_BEFORE && g_play.ticks[8] == PLAYBACK_CAMSCENES_BEFORE
          && playback_state() == PLAYBACK_FINISHED, "with no camera line: %08X, state %d", g_play.ticks[0],
          playback_state());
    /* The player's change while it plays: the game is theirs. */
    write_file(folder, "jsrf-pc-recording 1\ncamera 3 0 1 0\nend 20\n");
    CHECK(playback_play(folder, 0), "Camera ticks written by hand refused");
    h.ticks_at = 6;
    h.ticks2 = 3;
    run(&g_play, &h);
    CHECK(playback_state() == PLAYBACK_HANDED_BACK && g_play.ticks[6] == 3,
          "the player's Camera ticks changed: state %d, ticks %u", playback_state(), g_play.ticks[6]);
    write_file(folder, "jsrf-pc-recording 1\ncamera 3 0 2 0\n");
    CHECK(!playback_play(folder, 0), "a Camera tick of 2, played");
    write_file(folder, "jsrf-pc-recording 1\ncamera 3 0 1\n");
    CHECK(!playback_play(folder, 0), "a camera line short of a tick, played");
    write_file(folder, "jsrf-pc-recording 1\ncamera 3 0 1 0 5\n");
    CHECK(!playback_play(folder, 0), "a camera line with a fifth number of 5, played");
    write_file(folder, "jsrf-pc-recording 1\ncamera 5 1 1 1\ncamera 4 1 1 1\n");
    CHECK(!playback_play(folder, 0), "a camera line back in time, played");
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


/* ---- a run's folder: its steps, and a copy of its profile ---------------- */

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static int got(const char *path, const char *text)
{
    char buf[256] = "";
    size_t n;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);
    return !strcmp(buf, text);
}

/* A run's folder: the first recording, and these steps (none: no
 * steps.txt). */
static void run_folder(const char *run, const char *steps)
{
    char from[700], to[760], line[PB_TEST_LINE];
    FILE *in, *out;
    mkdir(run, 0755);
    snprintf(from, sizeof from, "%s/" PLAYBACK_FILE, g_run);
    snprintf(to, sizeof to, "%s/" PLAYBACK_FILE, run);
    in = fopen(from, "r");
    out = fopen(to, "w");
    while (in && out && fgets(line, sizeof line, in)) fputs(line, out);
    if (in) fclose(in);
    if (out) fclose(out);
    snprintf(to, sizeof to, "%s/" PLAYBACK_STEPS, run);
    if (steps) put(to, steps);
    else remove(to);
}

/* A step that can't be, refused before anything is played. */
static void step_refused(const char *steps, const char *what)
{
    char dir[700];
    snprintf(dir, sizeof dir, "%s/runs/refused", g_dir);
    run_folder(dir, steps);
    CHECK(!playback_play(dir, 0) && playback_state() == PLAYBACK_OFF, "%s: played", what);
}

/* --play's dir with a profile/: copied for the play into its plays/,
 * steps.txt's settings in the copy, and the settings file and both save
 * folders pointed there, yours untouched; then played with the window
 * flipping once a pass: each step at its moment, the idle moment's holding
 * the recording, a free camera row's change not the player's, a screenshot
 * in the play's folder, and on route to the end. A step that can't be is
 * refused before anything is played; a menu row that isn't there stops
 * the play at its first flip, and one never looked for (no window) when
 * the steps' wait runs out. Play from the menu leaves a profile/ out. */
static void steps(void)
{
    char dir[600], prof[660], path[900], own[700], play[700];
    const char *v;
    const PLAYBACK_RESULT *res = playback_result();
    How h = { .frames = FRAMES + 30, .skip_anchor = -1, .wrong_anchor = -1, .flips = 1 };
    snprintf(path, sizeof path, "%s/runs", g_dir);
    mkdir(path, 0755);
    snprintf(dir, sizeof dir, "%s/runs/garage", g_dir);
    run_folder(dir, "# a run's steps\n"
                    "setting mute=1\n"
                    "developer RECOMP_GL_PROFILE=1\n"
                    "frame 50: mark before the idle moment\n"
                    "idle 1: menu Input > Free camera > Invert Y\n"
                    "idle 1: screenshot\n"
                    "idle 1: mark at the idle moment\n"
                    "frame 600: screenshot\n");
    snprintf(prof, sizeof prof, "%s/" PLAYBACK_PROFILE, dir);
    mkdir(prof, 0755);
    snprintf(path, sizeof path, "%s/" PLAYBACK_PROFILE_SETTINGS, prof);
    put(path, "mute=0\nscale=2\n");
    snprintf(path, sizeof path, "%s/developer.cfg", prof);
    put(path, "RECOMP_GL_PROFILE=0\nRECOMP_APU_MIX=1");
    snprintf(path, sizeof path, "%s/saves", prof);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/saves/UDATA", prof);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/saves/UDATA/game.sav", prof);
    put(path, "a save");
    snprintf(own, sizeof own, "%s/own", g_dir);
    mkdir(own, 0755);
    snprintf(path, sizeof path, "%s/" PLAYBACK_PROFILE_SETTINGS, own);
    setenv("JSRF_SETTINGS_FILE", path, 1);
    snprintf(path, sizeof path, "%s/cache", own);
    setenv("JSRF_SAVE_DIR", path, 1);
    snprintf(path, sizeof path, "%s/saves", own);
    setenv("JSRF_USER_DIR", path, 1);
    setenv("JSRF_PLAY", dir, 1);
    playback_first();
    v = getenv("JSRF_SETTINGS_FILE");
    snprintf(path, sizeof path, "%s/" PLAYBACK_PLAYS "/", dir);
    CHECK(v && !strncmp(v, path, strlen(path)) && strlen(v) > 13
          && !strcmp(v + strlen(v) - 13, "/" PLAYBACK_PROFILE_SETTINGS), "the play's settings file: %s", v ? v : "-");
    snprintf(play, sizeof play, "%.*s", v ? (int)(strlen(v) - 13) : 0, v ? v : "");
    snprintf(path, sizeof path, "%s/cache", play);
    v = getenv("JSRF_SAVE_DIR");
    CHECK(v && !strcmp(v, path), "the play's save folder: %s", v ? v : "-");
    snprintf(path, sizeof path, "%s/saves", play);
    v = getenv("JSRF_USER_DIR");
    CHECK(v && !strcmp(v, path), "the play's saves: %s", v ? v : "-");
    snprintf(path, sizeof path, "%s/" PLAYBACK_PROFILE_SETTINGS, play);
    CHECK(got(path, "mute=1\nscale=2\n"), "the copy's settings, with steps.txt's");
    snprintf(path, sizeof path, "%s/developer.cfg", play);
    CHECK(got(path, "RECOMP_GL_PROFILE=1\nRECOMP_APU_MIX=1\n"), "the copy's developer.cfg, with steps.txt's");
    snprintf(path, sizeof path, "%s/saves/UDATA/game.sav", play);
    CHECK(got(path, "a save"), "the copy's saves");
    snprintf(path, sizeof path, "%s/" PLAYBACK_PROFILE_SETTINGS, prof);
    CHECK(got(path, "mute=0\nscale=2\n"), "the run's own profile changed");
    snprintf(path, sizeof path, "%s/" PLAYBACK_PROFILE_SETTINGS, own);
    CHECK(!got(path, ""), "your own settings file opened");

    /* Played, as playback_start plays --play's dir. */
    setenv("JSRF_PLAY", "", 1);
    g_closed = g_nshots = g_nchosen = 0;
    CHECK(playback_play(dir, 1), "the run with steps refused");
    run(&g_play, &h);
    CHECK(res->state == PLAYBACK_FINISHED && g_closed == 1 && res->idles_put_back == 1,
          "the run with steps: state %d (%s), closed %d, put back %d", res->state, res->why, g_closed,
          res->idles_put_back);
    CHECK(g_nchosen == 1 && !strcmp(g_chosen[0], "Input > Free camera > Invert Y"), "its menu row: %d chosen",
          g_nchosen);
    snprintf(path, sizeof path, "%s/shot-6.bmp", play);
    CHECK(g_nshots == 2 && !strcmp(g_shots[0], path) && got(path, "BM"), "its screenshots: %d, %s", g_nshots,
          g_nshots ? g_shots[0] : "-");
    snprintf(path, sizeof path, "%s/shot-8.bmp", play);
    CHECK(g_nshots == 2 && !strcmp(g_shots[1], path), "its frame's screenshot: %s", g_nshots > 1 ? g_shots[1] : "-");
    /* The idle moment's hold, its three steps and the row's wait, is in the
     * log: the next anchor met that much early (run.py). */
    cam_row("Invert Y")->run();             /* the player's free camera as it was */

    /* Refused before anything is played. */
    step_refused("frame 50: jump\n", "a word it doesn't know");
    step_refused("frame -5: mark x\n", "a frame before the first");
    step_refused("idle 2: mark x\n", "an idle moment the recording doesn't have");
    step_refused("idle 0: mark x\n", "idle moment 0");
    step_refused("frame 2000: screenshot\n", "a frame past the end");
    step_refused("frame 600: mark a\nframe 50: mark b\n", "a step out of order");
    step_refused("frame 50: mark\n", "a mark with nothing after it");
    step_refused("idle 1: menu Graphics > Anti-aliasing (SMAA) > Off\n", "a menu row without a profile");
    step_refused("setting mute=1\n", "a setting without a profile");
    step_refused("setting mute\n", "a setting that isn't key=value");

    /* A menu row that isn't there: the play stops at its first flip. */
    snprintf(dir, sizeof dir, "%s/runs/no-row", g_dir);
    run_folder(dir, "idle 1: menu " NO_ROW "\n");
    snprintf(prof, sizeof prof, "%s/" PLAYBACK_PROFILE, dir);
    mkdir(prof, 0755);
    setenv("JSRF_PLAY", dir, 1);
    playback_first();
    setenv("JSRF_PLAY", "", 1);
    g_closed = 0;
    CHECK(playback_play(dir, 1), "the run with a row that isn't there refused");
    run(&g_play, &h);
    CHECK(res->state == PLAYBACK_OFF && g_closed == 1 && res->frames <= 1
          && !strcmp(res->why, PLAYBACK_STEPS ", line 1: menu " NO_ROW ": nothing there has that label"),
          "a row that isn't there: state %d after %u frames (%s), closed %d", res->state, res->frames, res->why,
          g_closed);
    /* With no window to look for it: held, then stopped. */
    h.flips = 0;
    g_closed = 0;
    CHECK(playback_play(dir, 1), "the run with a row that isn't there refused");
    run(&g_play, &h);
    CHECK(res->state == PLAYBACK_OFF && g_closed == 1 && res->frames == PLAYBACK_STEP_WAIT + 2
          && strstr(res->why, "menu rows not looked for within 600 frames"),
          "no window: state %d after %u frames (%s), closed %d", res->state, res->frames, res->why, g_closed);

    /* Play, from the menu: a profile/ left out, marks and screenshots dir. */
    snprintf(dir, sizeof dir, "%s/runs/garage", g_dir);
    run_folder(dir, "frame 50: mark from the menu\nidle 1: screenshot\n");
    setenv("JSRF_PLAY_MENU", dir, 1);
    playback_first();
    setenv("JSRF_PLAY_MENU", "", 1);
    h.flips = 1;
    g_nshots = 0;
    CHECK(playback_play(dir, 0), "Play's run refused");
    run(&g_play, &h);
    snprintf(path, sizeof path, "%s/", play);
    CHECK(res->state == PLAYBACK_FINISHED && g_nshots == 1 && strncmp(g_shots[0], path, strlen(path)),
          "Play's run: state %d (%s), %d screenshots, in %s", res->state, res->why, g_nshots,
          g_nshots ? g_shots[0] : "-");
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
    CHECK(g_f8 && g_r3 && g_mouse_want && !g_mouse_want() && g_ncam_rows == 11 && !freecam_on(),
          "the free camera didn't start as it does");
    record();
    play_same();
    play_late(30);
    play_late(-20);
    play_off_route();
    play_idle();
    play_hand_back();
    play_settings();
    mark();
    record_cam();
    play_cam();
    play_cam_apart();
    play_state_apart();
    play_cam_take_over();
    by_hand();
    restart();
    missions_start_line();
    camera_ticks();
    start();
    steps();
    if (g_bad) return 1;
    printf("playback: all cases pass\n");
    return 0;
}
