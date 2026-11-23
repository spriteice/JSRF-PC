/* Input playback (playback.h says what it is, and how a recording reads).
 *
 * Everything here is called from the game's own code, which runs one guest
 * thread at a time, but for the menu's rows and playback_stop, on the
 * window's thread, and the end of a recording when the game closes: a lock
 * keeps them apart. */
#define _POSIX_C_SOURCE 200112L    /* setenv, unsetenv */
#include "playback.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#if defined(_WIN32)
#  include <direct.h>
#endif

#include "camera_walls.h"
#include "cutscene_skip.h"
#include "draw_distance.h"
#include "intro.h"
#include "pad.h"
#include "photo_mode.h"
#include "playback_mark.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

#ifndef JSRF_VERSION
#define JSRF_VERSION "dev"
#endif
#if defined(_WIN32)
#  define PB_PLATFORM "windows"
#else
#  define PB_PLATFORM "linux"
#endif
#define PB_FORMAT     "jsrf-pc-recording 1"

/* What counts as pressed: a button, an analog button past the game's own
 * threshold, or a stick past XInput's dead zone for it
 * (XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, ..._RIGHT_...): a stick at rest reads
 * a little off centre. */
#define PB_LEFT_DEAD  7849
#define PB_RIGHT_DEAD 8689

#define PB_LINE       512
#define PB_LIST       16          /* recordings Play offers at most, newest first */
#define PB_NAME       32          /* a recording's folder name: its date and time */
#define PB_STEP_TEXT  256         /* a step's menu path or mark */
#define PB_COPY_DEPTH 16          /* folders within folders a profile's copy goes down */

typedef struct { uint32_t frame, read; int none; XBOX_GAMEPAD pad; } PbPad;
typedef struct { uint32_t frame; PLAYBACK_ANCHOR kind; uint32_t value, random; } PbAnchor;
typedef struct {                                /* a check or an idle moment */
    uint32_t frame, turn, camera;
    float pos[3];
    int has_random;                             /* its random generator kept, and a check's draws */
    uint32_t random, drawn, others;
    int has_view, view_on;                      /* a check's free camera kept; steering then */
    double yaw, pitch;
} PbSpot;
typedef struct { uint32_t frame; double yaw, pitch; } PbFollow;   /* the free camera taking over */
typedef struct {                                /* a frame of the trace */
    uint32_t frame, turn, camera;
    float pos[3], look[2];
    int view_on;
    double yaw, pitch;
} PbTrace;
typedef struct { uint32_t frame; int key, value; } PbSetting;
typedef struct { uint32_t frame; FREECAM_SETTINGS fc; } PbFreecam;
typedef struct { uint32_t frame; int dx, dy; } PbMouse;
typedef struct { uint32_t frame; unsigned ticks; } PbCamera;   /* Camera's ticks, a bit each */
typedef struct { void *at; int n, cap; } PbList;
/* Where a run started (a recording's start line): set, at Load Game or a
 * mission (missions.h), with photo mode's tick. */
typedef struct { int set, load, mission, skater, photo; uint32_t progress; } PbStart;
/* A run's step (steps.txt): at an idle moment of the recording (idle, at
 * its number from 1) or at a recorded frame, a menu row, a mark or a
 * screenshot, with its line in the file. */
enum { PB_STEP_MENU, PB_STEP_MARK, PB_STEP_SHOT };
typedef struct { int line, idle, what; uint32_t at; char text[PB_STEP_TEXT]; } PbStep;

static const char *const k_kind[PLAYBACK_ANCHOR_KINDS] = {
    "mission.bin", "mission.dat", "stage", "scene", "scene-end"
};

/* The settings a recording keeps (playback.h): each by its key in the
 * settings file and its name in the menu, its module's reading and setting
 * of it now, and its most. */
static const struct {
    const char *key, *name;
    int (*get)(void);
    void (*set)(int v);
    int most;
} k_setting[PLAYBACK_SETTINGS] = {
    { "dcamfix",           "Default cam fix", camwall_on,         camwall_set,            1 },
    { "noskaterfade",      "No skater fade",  camwall_whole,      camwall_whole_set,      1 },
    { "skipintro",         "Skip intro",      intro_skip,         intro_skip_set,         1 },
    { "skipcutscenes",     "Skip cutscenes",  cutscene_skip_on,   cutscene_skip_set,      1 },
    { "drawdistance",      "Draw distance",   draw_distance_step, draw_distance_set,      DRAW_DISTANCE_STEPS - 1 },
    { "farbuildingsahead", "Far buildings",   draw_far_buildings, draw_far_buildings_set, DRAW_FAR_STEPS - 1 },
    { "farshadowsahead",   "Far shadows",     draw_far_shadows,   draw_far_shadows_set,   DRAW_FAR_STEPS - 1 },
};

static volatile int s_lock;
static void pb_lock(void)   { while (__atomic_exchange_n(&s_lock, 1, __ATOMIC_ACQUIRE)) { } }
static void pb_unlock(void) { __atomic_store_n(&s_lock, 0, __ATOMIC_RELEASE); }

static PLAYBACK_RESULT s_res;
static uint32_t s_frame;                    /* the game's passes so far */
static uint32_t s_reads[PLAYBACK_PORTS];    /* each port's reads in this pass */
static char s_folder[1024];
static int s_exit_hooked;
static int s_missions_tick;                 /* Developer > Missions > Record */
static int s_start_due;                     /* the start line not yet written (rec) */

/* Both: whether the skater is idle, from what the game read; and the start
 * of a run, its frame 0 (a run starts before the game's first pass). */
static uint32_t s_pressed_at;               /* the last frame anything was pressed */
static int s_pressed_ever;
static float s_still_pos[3];
static uint32_t s_still_since, s_still_camera, s_idle_said;
static int s_still_valid;

/* Both: the draws from the game's random generator since the last check, on
 * the game's own thread ([1]) and on others ([0]), and by the address each
 * returns to (playback_drawn, from any thread). */
#define PB_DRAW_SITES 16
static uint32_t s_drawn[2];
static struct { uint32_t from, n[2]; } s_draw_site[PB_DRAW_SITES];

/* The draws since the last check, taken, and the sites with them as
 * "<from> <game's>+<others'>" (sites may be NULL), counting from 0 again. */
static void drawn_take(uint32_t n[2], char *sites, size_t len)
{
    size_t at = 0;
    int i;
    n[0] = __atomic_exchange_n(&s_drawn[0], 0, __ATOMIC_RELAXED);
    n[1] = __atomic_exchange_n(&s_drawn[1], 0, __ATOMIC_RELAXED);
    if (sites && len) sites[0] = 0;
    for (i = 0; i < PB_DRAW_SITES; i++) {
        uint32_t from = __atomic_load_n(&s_draw_site[i].from, __ATOMIC_RELAXED);
        uint32_t g = __atomic_exchange_n(&s_draw_site[i].n[1], 0, __ATOMIC_RELAXED);
        uint32_t o = __atomic_exchange_n(&s_draw_site[i].n[0], 0, __ATOMIC_RELAXED);
        if (!from || (!g && !o) || !sites || at + 32 > len) continue;
        at += (size_t)snprintf(sites + at, len - at, "%s%08X %u+%u", at ? ", " : "", from, g, o);
    }
}

void playback_drawn(int game_thread, uint32_t from)
{
    int t = game_thread ? 1 : 0, i;
    __atomic_add_fetch(&s_drawn[t], 1, __ATOMIC_RELAXED);
    for (i = 0; i < PB_DRAW_SITES; i++) {
        uint32_t was = __atomic_load_n(&s_draw_site[i].from, __ATOMIC_RELAXED);
        if (!was) {
            uint32_t none = 0;
            was = __atomic_compare_exchange_n(&s_draw_site[i].from, &none, from, 0, __ATOMIC_RELAXED,
                                              __ATOMIC_RELAXED) ? from : none;
        }
        if (was == from) {
            __atomic_add_fetch(&s_draw_site[i].n[t], 1, __ATOMIC_RELAXED);
            return;
        }
    }
}

static void run_start(void)
{
    s_frame = 0;
    memset(s_reads, 0, sizeof s_reads);
    s_pressed_ever = 0;
    s_still_valid = 0;
    s_idle_said = 0;
    memset(&s_res, 0, sizeof s_res);
    { uint32_t n[2]; drawn_take(n, NULL, 0); }
}

/* Recording. */
static FILE *s_out;
static struct { int seen, none; XBOX_GAMEPAD pad; } s_last[PLAYBACK_PORTS];
static struct { int seen; FREECAM_SETTINGS fc; } s_fc_last;
static struct { int seen; unsigned ticks; } s_cam_last;
static uint32_t s_next_check;
static struct { int seen, value; } s_set_last[PLAYBACK_SETTINGS];
static int s_trace_on;                      /* the trace started (both) */
static uint32_t s_trace_from;               /* at this frame */

/* Playing. */
static PbList s_pads[PLAYBACK_PORTS], s_freecams, s_mice, s_cameras, s_anchors, s_checks, s_idles;
static PbList s_follows, s_traces, s_settings;
static int s_follow_next, s_trace_next, s_setting_next;
static int s_freecam_said, s_follow_said, s_trace_said;
/* The settings a play put in place (k_setting): the player's from before,
 * and what it set, to give the player's back at its end. */
static struct { int put, player, now; } s_set_play[PLAYBACK_SETTINGS];
static PbMark s_mark;                       /* the mark at the HUD's left edge (playback_mark.h) */
static int s_cur[PLAYBACK_PORTS] = { -1, -1, -1, -1 };
static int s_fc_cur = -1, s_fc_player_seen;
static int s_mouse_next, s_mouse_armed;     /* the next mouse line to take; the player's mouse still once */
static FREECAM_SETTINGS s_fc_player;        /* the player's, as the last frame had them */
static int s_cam_cur = -1, s_cam_player_seen, s_cam_before_said;
/* What the recording's camera lines say the free camera did after the
 * game's shots (their fifth number, PLAYBACK_BACK_): eased back (1),
 * glided (2), glided quicker, the controls following where it glided to
 * (3), or that and cut into the graffiti shot (4), as builds since have
 * it. */
static int s_cam_back;
static unsigned s_cam_player;               /* the player's ticks, as the last camera update had them */
static int s_anchor_next, s_check_next, s_idle_next;
static int64_t s_offset;                    /* the frame played less the frame recorded */
static uint32_t s_end, s_wait_from;
static PbStart s_start;                     /* the recording's start line */
static int s_start_check;                   /* this run's start not yet checked against it */
static PbList s_steps;                      /* the run's steps, in steps.txt's order */
static int s_step_next, s_steps_run;
static int s_steps_checked;                 /* its menu rows found, on the window's thread */
static int s_step_running;                  /* one runs there: its changes aren't the player's */
static uint32_t s_step_settle;              /* flips left to wait after a menu row */
static int s_holding;                       /* an idle moment's steps hold the recording */
static uint32_t s_hold_from;
/* This play's folder (a run's plays/<date>-<time>/), the run it is for, and
 * whether it holds a copy of the run's profile/ (playback_first) or that
 * copy failed. */
static char s_play_dir[1100], s_play_for[1024];
static int s_profile, s_profile_bad;
static int s_exit_at_end, s_drift_run, s_armed[PLAYBACK_PORTS];
static float s_drift_said = -1;
static uint32_t s_packet[PLAYBACK_PORTS];
/* The random generator at the checks: whether the recording keeps it, the
 * times it wasn't as recorded said so far, and whether the play held its
 * pad (steps, or an anchor waited for) since the last check, so that the
 * draws can't be compared there. */
static int s_random_kept, s_random_said, s_window_held;
#define PB_RANDOM_SAY 5

#define PB_PADS(p)  ((PbPad *)s_pads[p].at)
#define PB_FREECAM  ((PbFreecam *)s_freecams.at)
#define PB_MOUSE    ((PbMouse *)s_mice.at)
#define PB_CAMERA   ((PbCamera *)s_cameras.at)
#define PB_ANCHOR   ((PbAnchor *)s_anchors.at)
#define PB_CHECK    ((PbSpot *)s_checks.at)
#define PB_IDLE     ((PbSpot *)s_idles.at)
#define PB_STEP     ((PbStep *)s_steps.at)
#define PB_FOLLOW   ((PbFollow *)s_follows.at)
#define PB_TRACE    ((PbTrace *)s_traces.at)
#define PB_SETTING  ((PbSetting *)s_settings.at)

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a file beside the settings file, the restart, and the window's
 * close, as File > Exit closes it. */
extern void nv_window_file_path(const char *file, char *buf, size_t n);
extern void nv_window_restart(void);
extern void nv_window_close(void);
#endif

static void set_env(const char *name, const char *val)
{
#if defined(_WIN32)
    _putenv_s(name, val);               /* "" takes it out */
#else
    if (*val) setenv(name, val, 1);
    else      unsetenv(name);
#endif
}

static int make_folder(const char *path)
{
    struct stat st;
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
    return !stat(path, &st) && S_ISDIR(st.st_mode);
}

static int pressed(const XBOX_GAMEPAD *g)
{
    int k;
    if (g->wButtons) return 1;
    for (k = 0; k < 8; k++)
        if (g->bAnalogButtons[k] > XBOX_ANALOG_BUTTON_THRESHOLD) return 1;
    return abs(g->sThumbLX) > PB_LEFT_DEAD || abs(g->sThumbLY) > PB_LEFT_DEAD
        || abs(g->sThumbRX) > PB_RIGHT_DEAD || abs(g->sThumbRY) > PB_RIGHT_DEAD;
}

static int same_pad(const XBOX_GAMEPAD *a, const XBOX_GAMEPAD *b)
{
    return a->wButtons == b->wButtons
        && !memcmp(a->bAnalogButtons, b->bAnalogButtons, sizeof a->bAnalogButtons)
        && a->sThumbLX == b->sThumbLX && a->sThumbLY == b->sThumbLY
        && a->sThumbRX == b->sThumbRX && a->sThumbRY == b->sThumbRY;
}

static int same_freecam(const FREECAM_SETTINGS *a, const FREECAM_SETTINGS *b)
{
    return a->on == b->on && a->invert_x == b->invert_x && a->invert_y == b->invert_y
        && a->centre == b->centre && a->sens_x == b->sens_x && a->sens_y == b->sens_y
        && a->deadzone == b->deadzone && a->mouse == b->mouse && a->mouse_sens == b->mouse_sens;
}

static float distance(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

static void *add(PbList *l, size_t size)
{
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 256;
        void *at = realloc(l->at, (size_t)cap * size);
        if (!at) return NULL;
        l->at = at;
        l->cap = cap;
    }
    return (char *)l->at + (size_t)l->n++ * size;
}

/* ---- where a run started ------------------------------------------------ */

/* This run's start, as Developer > Missions made it (missions.h), and photo
 * mode's tick. */
static PbStart start_now(void)
{
    PbStart st;
    MISSIONS_START m;
    memset(&st, 0, sizeof st);
    if (missions_started(&m)) {
        st.set = 1;
        st.load = m.mission < 0;
        st.mission = m.mission;
        st.skater = m.skater;
        st.progress = m.progress;
        st.photo = photo_mode_ticked() != 0;
    }
    return st;
}

static int same_start(const PbStart *a, const PbStart *b)
{
    if (a->set != b->set) return 0;
    if (!a->set) return 1;
    if (a->load || b->load) return a->load == b->load && a->photo == b->photo;
    return a->mission == b->mission && a->skater == b->skater && a->photo == b->photo
        && a->progress == b->progress;
}

/* For the log: "the game's own start", "Load Game", or "mission 0252,
 * skater 6, progress 1A2B3C4D", with ", photo mode" when it was ticked. */
static void say_start(const PbStart *st, char *b, size_t n)
{
    if (!st->set) snprintf(b, n, "the game's own start");
    else if (st->load) snprintf(b, n, "Load Game%s", st->photo ? ", photo mode" : "");
    else
        snprintf(b, n, "mission %04d, skater %d, progress %08X%s", st->mission, st->skater, st->progress,
                 st->photo ? ", photo mode" : "");
}

/* A start line's words after "start": 0 for one that can't be read. */
static int read_start(const char *s, PbStart *st)
{
    char kind[16];
    int used = 0;
    memset(st, 0, sizeof st[0]);
    if (sscanf(s, "%15s %n", kind, &used) != 1 || !used) return 0;
    s += used;
    if (!strcmp(kind, "load")) {
        st->set = st->load = 1;
        st->mission = st->skater = -1;
        return sscanf(s, "%d", &st->photo) == 1 && (st->photo == 0 || st->photo == 1);
    }
    st->set = 1;
    return !strcmp(kind, "mission")
        && sscanf(s, "%d %d %d %x", &st->mission, &st->skater, &st->photo, &st->progress) == 4
        && st->mission >= 0 && st->mission <= 9999 && st->skater >= -1 && st->skater < MISSIONS_CHARACTERS
        && (st->photo == 0 || st->photo == 1);
}

/* ---- the end of a run -------------------------------------------------- */

static const char *outcome(void)
{
    switch (s_res.state) {
    case PLAYBACK_FINISHED:    return "finished on route";
    case PLAYBACK_OFF_ROUTE:   return "off route";
    case PLAYBACK_HANDED_BACK: return "handed back";
    default:                   return "stopped";
    }
}

static void summary(void)
{
    fprintf(stderr, "[PLAYBACK] t=%.1f summary: %s%s%s; %u frames played; anchors met %d of %d"
            " (%d skipped); %d checks, the largest drift %.1f units at recorded frame %u;"
            " idle moments put back %d of %d\n",
            pad_now(), outcome(), s_res.why[0] ? ": " : "", s_res.why, s_res.frames,
            s_res.anchors_met, s_res.anchors, s_res.anchors_skipped, s_res.checks,
            s_res.drift_most, s_res.drift_most_at, s_res.idles_put_back, s_idles.n);
    if (s_steps.n)
        fprintf(stderr, "[PLAYBACK] steps run %d of %d (" PLAYBACK_STEPS ")\n", s_steps_run, s_steps.n);
    if (s_random_kept && s_res.random_checks > s_res.random_same)
        fprintf(stderr, "[PLAYBACK] the game's random numbers at the checks: as recorded at %d of %d,"
                " first not at recorded frame %u; put back at each\n", s_res.random_same,
                s_res.random_checks, s_res.random_apart_at);
    else if (s_random_kept)
        fprintf(stderr, "[PLAYBACK] the game's random numbers at the checks: as recorded at %d of %d\n",
                s_res.random_same, s_res.random_checks);
    if (s_res.follows)
        fprintf(stderr, "[PLAYBACK] the free camera taking over: as recorded at %d of %d%s\n",
                s_res.follows_same, s_res.follows, s_res.follows_same < s_res.follows ? "; put back at each" : "");
    if (s_res.freecam_checks && s_res.freecam_checks > s_res.freecam_same)
        fprintf(stderr, "[PLAYBACK] the free camera at the checks: as recorded at %d of %d, first not at"
                " recorded frame %u; put back at each\n", s_res.freecam_same, s_res.freecam_checks,
                s_res.freecam_apart_at);
    else if (s_res.freecam_checks)
        fprintf(stderr, "[PLAYBACK] the free camera at the checks: as recorded at %d of %d\n",
                s_res.freecam_same, s_res.freecam_checks);
    if (s_res.trace_frames && s_res.trace_frames > s_res.trace_same)
        fprintf(stderr, "[PLAYBACK] the trace from the first press: as recorded at %d of %d frames, first not"
                " at recorded frame %u\n", s_res.trace_same, s_res.trace_frames, s_res.trace_apart_at);
    else if (s_res.trace_frames)
        fprintf(stderr, "[PLAYBACK] the trace from the first press: as recorded at %d of %d frames\n",
                s_res.trace_same, s_res.trace_frames);
}

/* The player's settings back, those a play put in place (k_setting), but
 * one the player changed while it played: said. */
static void settings_back(void)
{
    char said[512] = "";
    int k, n = 0;
    for (k = 0; k < PLAYBACK_SETTINGS; k++) {
        size_t len = strlen(said);
        if (!s_set_play[k].put) continue;
        s_set_play[k].put = 0;
        if (k_setting[k].get() != s_set_play[k].now) continue;     /* the player's own since */
        if (s_set_play[k].player == s_set_play[k].now) continue;
        k_setting[k].set(s_set_play[k].player);
        snprintf(said + len, sizeof said - len, "%s%s %d", n++ ? ", " : "", k_setting[k].name,
                 s_set_play[k].player);
    }
    if (n) fprintf(stderr, "[PLAYBACK] your settings back, as the play ends: %s\n", said);
}

/* The mark's colour for how a play ended. */
static int mark_how(PLAYBACK_STATE state)
{
    return state == PLAYBACK_FINISHED ? PLAYBACK_MARK_FINISHED
         : state == PLAYBACK_HANDED_BACK ? PLAYBACK_MARK_TAKEN : PLAYBACK_MARK_OFF_ROUTE;
}

/* A playback over: said, and the game closed for an unattended run. */
static void play_over(PLAYBACK_STATE state)
{
    s_res.state = state;
    summary();
    settings_back();
    playback_mark_end(&s_mark, mark_how(state), pad_now());
    if (!s_exit_at_end || state == PLAYBACK_HANDED_BACK) return;
#if defined(NV2A_GL_USE_SDL)
    fprintf(stderr, "[PLAYBACK] closing the game, as --play asks\n");
    nv_window_close();
#else
    fprintf(stderr, "[PLAYBACK] no window to close: the game goes on\n");
#endif
}

static void off_route(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vsnprintf(s_res.why, sizeof s_res.why, fmt, a);
    va_end(a);
    fprintf(stderr, "[PLAYBACK] frame %u: off route: %s\n", s_frame, s_res.why);
    play_over(PLAYBACK_OFF_ROUTE);
}

/* The recording's file for its next line, with the start line first: where
 * this run started, written once Developer > Missions has started it or
 * refused (missions_paths, before the game's first pass). */
static FILE *rec(void)
{
    if (s_start_due) {
        PbStart st = start_now();
        char said[96];
        s_start_due = 0;
        if (st.load) fprintf(s_out, "start load %d\n", st.photo);
        else if (st.set)
            fprintf(s_out, "start mission %04d %d %d %08X\n", st.mission, st.skater, st.photo, st.progress);
        /* A run started at a mission records its next pick too. */
        if (st.set) {
            s_missions_tick = 1;
            say_start(&st, said, sizeof said);
            fprintf(stderr, "[PLAYBACK] this run started at %s (Developer > Missions): so will its plays\n",
                    said);
        }
    }
    return s_out;
}

/* Not under the lock. */
static void end_recording(const char *why)
{
    if (!s_out) return;
    fprintf(rec(), "end %u\n", s_frame);
    fclose(s_out);
    s_out = NULL;
    s_res.state = PLAYBACK_RECORDED;
    s_res.frames = s_frame;
    fprintf(stderr, "[PLAYBACK] t=%.1f recording ended at frame %u (%s): %s/" PLAYBACK_FILE "\n",
            pad_now(), s_frame, why, s_folder);
}

/* The game closing or restarting, which ends a recording, or a play with its
 * summary: said, or nothing while neither runs. Not under the lock. */
static void game_gone(const char *why)
{
    end_recording(why);
    if (s_res.state == PLAYBACK_PLAYING || s_res.state == PLAYBACK_WAITING) {
        snprintf(s_res.why, sizeof s_res.why, "%s at frame %u", why, s_frame);
        s_res.state = PLAYBACK_OFF;
        summary();
        settings_back();
    }
}

static void at_exit(void)
{
    pb_lock();
    game_gone("the game closed");
    pb_unlock();
}

void playback_restart(void)
{
    pb_lock();
    game_gone("the game restarted");
    pb_unlock();
}

static void hook_exit(void)
{
    if (!s_exit_hooked) { s_exit_hooked = 1; atexit(at_exit); }
}

/* ---- recording ---------------------------------------------------------- */

int playback_record(const char *folder)
{
    char path[sizeof s_folder + 32];
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    pb_lock();
    snprintf(s_folder, sizeof s_folder, "%s", folder);
    snprintf(path, sizeof path, "%s/" PLAYBACK_FILE, folder);
    if (!make_folder(folder) || !(s_out = fopen(path, "w"))) {
        fprintf(stderr, "[PLAYBACK] can't write %s: nothing is recorded\n", path);
        pb_unlock();
        return 0;
    }
    fprintf(s_out, PB_FORMAT "\nversion %s " PB_PLATFORM "\n", JSRF_VERSION);
    if (utc)
        fprintf(s_out, "# started %04d-%02d-%02d %02d:%02d:%02d UTC\n", utc->tm_year + 1900,
                utc->tm_mon + 1, utc->tm_mday, utc->tm_hour, utc->tm_min, utc->tm_sec);
    s_start_due = 1;
    fflush(s_out);
    run_start();
    s_res.state = PLAYBACK_RECORDING;
    s_next_check = 0;
    memset(s_last, 0, sizeof s_last);
    s_fc_last.seen = 0;
    s_cam_last.seen = 0;
    memset(s_set_last, 0, sizeof s_set_last);
    s_trace_on = 0;
    hook_exit();
    fprintf(stderr, "[PLAYBACK] recording from the first frame into %s (JSRF-PC %s, " PB_PLATFORM ")\n",
            path, JSRF_VERSION);
    pb_unlock();
    return 1;
}

void playback_stop(void)
{
    pb_lock();
    if (s_out) end_recording("Stop recording");
    else fprintf(stderr, "[PLAYBACK] Stop recording: nothing is being recorded\n");
    pb_unlock();
}

static void record_pad(uint32_t port, const XBOX_GAMEPAD *p, int none)
{
    if (s_last[port].seen && s_last[port].none == none && (none || same_pad(&s_last[port].pad, p)))
        return;
    s_last[port].seen = 1;
    s_last[port].none = none;
    s_last[port].pad = *p;
    if (none) {
        fprintf(rec(), "pad %u.%u %u none\n", s_frame, s_reads[port], port);
        return;
    }
    fprintf(rec(), "pad %u.%u %u %04X %02X%02X%02X%02X%02X%02X%02X%02X %d %d %d %d\n", s_frame,
            s_reads[port], port, p->wButtons, p->bAnalogButtons[0], p->bAnalogButtons[1],
            p->bAnalogButtons[2], p->bAnalogButtons[3], p->bAnalogButtons[4], p->bAnalogButtons[5],
            p->bAnalogButtons[6], p->bAnalogButtons[7], p->sThumbLX, p->sThumbLY, p->sThumbRX,
            p->sThumbRY);
}

/* A number, as short as reads back the same. */
static void put_number(double v)
{
    char b[32];
    int digits;
    for (digits = 15; digits < 17; digits++) {
        snprintf(b, sizeof b, "%.*g", digits, v);
        if (strtod(b, NULL) == v) break;
    }
    fprintf(rec(), " %.*g", digits, v);
}

static void record_freecam(const FREECAM_SETTINGS *fc)
{
    if (s_fc_last.seen && same_freecam(&s_fc_last.fc, fc)) return;
    s_fc_last.seen = 1;
    s_fc_last.fc = *fc;
    fprintf(rec(), "freecam %u %d %d %d", s_frame, fc->on, fc->invert_x, fc->invert_y);
    put_number(fc->sens_x);
    put_number(fc->sens_y);
    put_number(fc->deadzone);
    fprintf(rec(), " %d %d", fc->centre, fc->mouse);
    put_number(fc->mouse_sens);
    fputc('\n', rec());
}

/* Input > Behaviour > Camera's ticks, three of them (camera_scenes.h). */
#define PB_CAMERA_TICKS 3
static void record_camscenes(unsigned ticks)
{
    if (s_cam_last.seen && s_cam_last.ticks == ticks) return;
    s_cam_last.seen = 1;
    s_cam_last.ticks = ticks;
    fprintf(rec(), "camera %u %u %u %u %d\n", s_frame, ticks & 1u, (ticks >> 1) & 1u, (ticks >> 2) & 1u,
            PLAYBACK_BACK_OUT);
}

/* ---- reading a recording ------------------------------------------------ */

static int read_pad(const char *s, PbPad *p, uint32_t *port)
{
    unsigned b, a[8], f, r = 0;
    int lx, ly, rx, ry, used = 0, k;
    char none[8];
    if (sscanf(s, "%u.%u %u %n", &f, &r, port, &used) < 3 || !used) {
        r = 0;
        used = 0;
        if (sscanf(s, "%u %u %n", &f, port, &used) < 2 || !used) return 0;
    }
    if (*port >= PLAYBACK_PORTS) return 0;
    p->frame = f;
    p->read = r;
    memset(&p->pad, 0, sizeof p->pad);
    s += used;
    if (sscanf(s, "%7s", none) == 1 && !strcmp(none, "none")) { p->none = 1; return 1; }
    p->none = 0;
    if (sscanf(s, "%x %2x%2x%2x%2x%2x%2x%2x%2x %d %d %d %d", &b, &a[0], &a[1], &a[2], &a[3], &a[4],
               &a[5], &a[6], &a[7], &lx, &ly, &rx, &ry) != 13)
        return 0;
    p->pad.wButtons = (WORD)b;
    for (k = 0; k < 8; k++) p->pad.bAnalogButtons[k] = (BYTE)a[k];
    p->pad.sThumbLX = (SHORT)lx; p->pad.sThumbLY = (SHORT)ly;
    p->pad.sThumbRX = (SHORT)rx; p->pad.sThumbRY = (SHORT)ry;
    return 1;
}

/* A freecam line, each value in its range in the settings file; without
 * the mouse's two (made before the mouse turned the free camera), Mouse look
 * off, at 5. */
static int read_freecam(const char *s, PbFreecam *e)
{
    FREECAM_SETTINGS *c = &e->fc;
    int n = sscanf(s, "%u %d %d %d %lf %lf %lf %d %d %lf", &e->frame, &c->on, &c->invert_x, &c->invert_y,
                   &c->sens_x, &c->sens_y, &c->deadzone, &c->centre, &c->mouse, &c->mouse_sens);
    if (n == 8) { c->mouse = 0; c->mouse_sens = 5; }
    return (n == 8 || n == 10)
        && (c->on == 0 || c->on == 1) && (c->invert_x == 0 || c->invert_x == 1)
        && (c->invert_y == 0 || c->invert_y == 1)
        && c->sens_x >= FREECAM_SENS_MIN && c->sens_x <= FREECAM_SENS_MAX
        && c->sens_y >= FREECAM_SENS_MIN && c->sens_y <= FREECAM_SENS_MAX
        && c->deadzone >= 0 && c->deadzone <= 1 && c->centre >= 0 && c->centre < FREECAM_CENTRE_LEVELS
        && (c->mouse == 0 || c->mouse == 1)
        && c->mouse_sens >= FREECAM_SENS_MIN && c->mouse_sens <= FREECAM_SENS_MAX;
}

/* A check's line, or an idle moment's: with its random generator, and a
 * check's draws, or (made before they were kept) without. */
/* An angle of the free camera's, or "-" while it steers none: 1, with
 * whether it steered, or 0. */
static int read_angle(const char *word, int *on, double *v)
{
    char *end;
    if (!strcmp(word, "-")) { *on = 0; *v = 0; return 1; }
    *v = strtod(word, &end);
    *on = 1;
    return end != word && !*end && isfinite(*v);
}

static int read_spot(const char *s, PbSpot *p, int check)
{
    char yaw[40] = "", pitch[40] = "", more[8];
    int used = 0, n = sscanf(s, "%u %f %f %f %x %u %x %u %u%n", &p->frame, &p->pos[0], &p->pos[1], &p->pos[2],
                             &p->turn, &p->camera, &p->random, &p->drawn, &p->others, &used);
    int angles = 0, on_yaw = 0, on_pitch = 0;
    p->has_random = n > 6;
    if (!p->has_random) p->random = p->drawn = p->others = 0;
    if (!check) p->drawn = p->others = 0;
    p->has_view = p->view_on = 0;
    p->yaw = p->pitch = 0;
    /* A check's free camera, after its draws: both, or neither. */
    if (check && n == 9) {
        angles = sscanf(s + used, "%39s %39s %7s", yaw, pitch, more);
        if (angles == 2) {
            if (!read_angle(yaw, &on_yaw, &p->yaw) || !read_angle(pitch, &on_pitch, &p->pitch)
                || on_yaw != on_pitch)
                return 0;
            p->has_view = 1;
            p->view_on = on_yaw;
        } else if (angles > 0)
            return 0;
    }
    return n == 6 || n == (check ? 9 : 7);
}

/* A trace line: the skater, the camera's look and the free camera's two. */
static int read_trace(const char *s, PbTrace *t)
{
    char yaw[40], pitch[40];
    int on_pitch = 0;
    if (sscanf(s, "%u %f %f %f %x %u %f %f %39s %39s", &t->frame, &t->pos[0], &t->pos[1], &t->pos[2],
               &t->turn, &t->camera, &t->look[0], &t->look[1], yaw, pitch) != 10)
        return 0;
    return read_angle(yaw, &t->view_on, &t->yaw) && read_angle(pitch, &on_pitch, &t->pitch)
           && on_pitch == t->view_on;
}

static void forget(void)
{
    int p;
    for (p = 0; p < PLAYBACK_PORTS; p++) { free(s_pads[p].at); memset(&s_pads[p], 0, sizeof s_pads[p]); }
    free(s_freecams.at); free(s_mice.at); free(s_cameras.at); free(s_anchors.at); free(s_checks.at);
    free(s_idles.at);
    memset(&s_freecams, 0, sizeof s_freecams);
    memset(&s_mice, 0, sizeof s_mice);
    memset(&s_cameras, 0, sizeof s_cameras);
    s_cam_back = 0;
    memset(&s_anchors, 0, sizeof s_anchors);
    memset(&s_checks, 0, sizeof s_checks);
    memset(&s_idles, 0, sizeof s_idles);
    free(s_steps.at);
    memset(&s_steps, 0, sizeof s_steps);
    free(s_follows.at); free(s_traces.at); free(s_settings.at);
    memset(&s_follows, 0, sizeof s_follows);
    memset(&s_traces, 0, sizeof s_traces);
    memset(&s_settings, 0, sizeof s_settings);
    s_random_kept = 0;
}

/* The recording in folder, every line checked: 0, saying where, at the
 * first that can't be read or goes back in time. */
static int load(const char *path)
{
    char line[PB_LINE], word[16] = "", version[64], platform[16];
    uint32_t last = 0, port;
    int n = 0, format = 0, ended = 0, framed = 0;
    FILE *f = fopen(path, "r");
    forget();
    s_end = 0;
    memset(&s_start, 0, sizeof s_start);
    if (!f) { fprintf(stderr, "[PLAYBACK] can't read %s: nothing is played\n", path); return 0; }
    while (fgets(line, sizeof line, f)) {
        const char *s = line;
        int used = 0, ok = 1;
        uint32_t frame = 0;
        n++;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#' || *s == '\r' || *s == '\n') continue;
        if (ended) { ok = 0; goto bad; }
        if (!format) {
            format = !strncmp(s, PB_FORMAT, strlen(PB_FORMAT));
            if (!format) goto bad;
            continue;
        }
        if (sscanf(s, "%15s %n", word, &used) != 1) { ok = 0; goto bad; }
        s += used;
        if (!strcmp(word, "version")) {
            if (sscanf(s, "%63s %15s", version, platform) == 2)
                fprintf(stderr, "[PLAYBACK] recorded on JSRF-PC %s, %s; this is %s, " PB_PLATFORM "\n",
                        version, platform, JSRF_VERSION);
            continue;
        }
        /* Once, before any line with a frame. */
        if (!strcmp(word, "start")) {
            if (!(ok = !framed && !s_start.set && read_start(s, &s_start))) goto bad;
            continue;
        }
        framed = 1;
        if (!strcmp(word, "pad")) {
            PbPad p, *to;
            ok = read_pad(s, &p, &port);
            if (ok && s_pads[port].n) {
                const PbPad *before = &PB_PADS(port)[s_pads[port].n - 1];
                ok = p.frame > before->frame || (p.frame == before->frame && p.read > before->read);
            }
            if (ok && (ok = (to = add(&s_pads[port], sizeof *to)) != NULL)) *to = p;
            frame = p.frame;
        } else if (!strcmp(word, "freecam")) {
            PbFreecam e, *to;
            memset(&e, 0, sizeof e);
            ok = read_freecam(s, &e) && (!s_freecams.n || e.frame >= PB_FREECAM[s_freecams.n - 1].frame);
            if (ok && (ok = (to = add(&s_freecams, sizeof *to)) != NULL)) *to = e;
            frame = e.frame;
        } else if (!strcmp(word, "mouse")) {
            PbMouse m, *to;
            ok = sscanf(s, "%u %d %d", &m.frame, &m.dx, &m.dy) == 3
                 && (!s_mice.n || m.frame >= PB_MOUSE[s_mice.n - 1].frame);
            if (ok && (ok = (to = add(&s_mice, sizeof *to)) != NULL)) *to = m;
            frame = m.frame;
        } else if (!strcmp(word, "camera")) {
            PbCamera e, *to;
            unsigned t[PB_CAMERA_TICKS], back = 0;
            int k, n = sscanf(s, "%u %u %u %u %u", &e.frame, &t[0], &t[1], &t[2], &back);
            /* Its fifth, what the free camera did after a shot, left out
             * by those made before it eased back. */
            ok = (n == 1 + PB_CAMERA_TICKS || (n == 2 + PB_CAMERA_TICKS && back <= PLAYBACK_BACK_OUT))
                 && (!s_cameras.n || e.frame >= PB_CAMERA[s_cameras.n - 1].frame);
            if (ok && n == 2 + PB_CAMERA_TICKS) s_cam_back = (int)back;
            for (k = 0, e.ticks = 0; ok && k < PB_CAMERA_TICKS; k++) {
                ok = t[k] <= 1;
                e.ticks |= t[k] << k;
            }
            if (ok && (ok = (to = add(&s_cameras, sizeof *to)) != NULL)) *to = e;
            frame = e.frame;
        } else if (!strcmp(word, "anchor")) {
            PbAnchor a, *to;
            char kind[16];
            int k;
            ok = sscanf(s, "%u %15s %x %x", &a.frame, kind, &a.value, &a.random) == 4;
            for (k = 0; ok && k < PLAYBACK_ANCHOR_KINDS && strcmp(kind, k_kind[k]); k++) { }
            ok = ok && k < PLAYBACK_ANCHOR_KINDS
                 && (!s_anchors.n || a.frame >= PB_ANCHOR[s_anchors.n - 1].frame);
            a.kind = (PLAYBACK_ANCHOR)k;
            if (ok && (ok = (to = add(&s_anchors, sizeof *to)) != NULL)) *to = a;
            frame = a.frame;
        } else if (!strcmp(word, "check") || !strcmp(word, "idle")) {
            PbList *l = word[0] == 'c' ? &s_checks : &s_idles;
            PbSpot sp, *to;
            ok = read_spot(s, &sp, l == &s_checks)
                 && (!l->n || sp.frame > ((PbSpot *)l->at)[l->n - 1].frame);
            if (ok && (ok = (to = add(l, sizeof *to)) != NULL)) *to = sp;
            if (ok && l == &s_checks && sp.has_random) s_random_kept = 1;
            frame = sp.frame;
        } else if (!strcmp(word, "follow")) {
            PbFollow e, *to;
            char yaw[40], pitch[40];
            int on_yaw = 0, on_pitch = 0;
            ok = sscanf(s, "%u %39s %39s", &e.frame, yaw, pitch) == 3 && read_angle(yaw, &on_yaw, &e.yaw)
                 && read_angle(pitch, &on_pitch, &e.pitch) && on_yaw && on_pitch
                 && (!s_follows.n || e.frame >= PB_FOLLOW[s_follows.n - 1].frame);
            if (ok && (ok = (to = add(&s_follows, sizeof *to)) != NULL)) *to = e;
            frame = e.frame;
        } else if (!strcmp(word, "trace")) {
            PbTrace t, *to;
            ok = read_trace(s, &t) && (!s_traces.n || t.frame > PB_TRACE[s_traces.n - 1].frame);
            if (ok && (ok = (to = add(&s_traces, sizeof *to)) != NULL)) *to = t;
            frame = t.frame;
        } else if (!strcmp(word, "setting")) {
            PbSetting e, *to;
            char key[32];
            int k;
            ok = sscanf(s, "%u %31s %d", &e.frame, key, &e.value) == 3;
            for (k = 0; ok && k < PLAYBACK_SETTINGS && strcmp(key, k_setting[k].key); k++) { }
            ok = ok && k < PLAYBACK_SETTINGS && e.value >= 0 && e.value <= k_setting[k].most
                 && (!s_settings.n || e.frame >= PB_SETTING[s_settings.n - 1].frame);
            e.key = k;
            if (ok && (ok = (to = add(&s_settings, sizeof *to)) != NULL)) *to = e;
            frame = e.frame;
        } else if (!strcmp(word, "end")) {
            ok = sscanf(s, "%u", &s_end) == 1 && s_end >= last;
            frame = s_end;
        } else
            ok = 0;
        if (ok && frame > last) last = frame;
        ended = ok && !strcmp(word, "end");
bad:
        if (!ok || !format) {
            fprintf(stderr, "[PLAYBACK] %s, line %d: %s; nothing is played\n", path, n,
                    !format ? "not a recording (no \"" PB_FORMAT "\" first)"
                    : ended ? "a line after the end"
                    : !strcmp(word, "start") ? "a start line that can't be read, comes twice or after a frame"
                    : "can't be read, or goes back in time");
            fclose(f);
            forget();
            return 0;
        }
    }
    fclose(f);
    if (!format) {
        fprintf(stderr, "[PLAYBACK] %s: empty; nothing is played\n", path);
        return 0;
    }
    if (!ended) {
        s_end = last;
        fprintf(stderr, "[PLAYBACK] %s has no end line (the game closed while recording?):"
                " played to its last, frame %u\n", path, last);
    }
    return 1;
}

/* ---- a run's folder: its steps, and a copy of its profile -------------- */

/* A folder of its own for this play in the run's plays/, by the time in
 * UTC, as recordings are named: s_play_dir, or 0. */
static int play_folder(const char *run)
{
    char plays[1100];
    struct stat st;
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    int i;
    s_play_dir[0] = '\0';
    snprintf(s_play_for, sizeof s_play_for, "%s", run);
    if (!utc || (size_t)snprintf(plays, sizeof plays, "%s/" PLAYBACK_PLAYS, run) >= sizeof plays
        || !make_folder(plays))
        return 0;
    for (i = 1; i <= 9; i++) {
        char name[48];
        snprintf(name, sizeof name, i > 1 ? "%04d-%02d-%02d-%02d%02d%02d-%d" : "%04d-%02d-%02d-%02d%02d%02d",
                 utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday, utc->tm_hour, utc->tm_min, utc->tm_sec, i);
        if ((size_t)snprintf(s_play_dir, sizeof s_play_dir, "%s/%s", plays, name) >= sizeof s_play_dir) break;
        if (stat(s_play_dir, &st) && make_folder(s_play_dir)) return 1;
    }
    s_play_dir[0] = '\0';
    return 0;
}

static int copy_file(const char *from, const char *to)
{
    char buf[65536];
    size_t n;
    int bad = 0;
    FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
    if (!out) { if (in) fclose(in); return 0; }
    while (!bad && (n = fread(buf, 1, sizeof buf, in)) > 0) bad = fwrite(buf, 1, n, out) != n;
    bad |= ferror(in);
    fclose(in);
    return !(fclose(out) | bad);
}

/* Every file and folder in from, into to (made): 0, saying which, at the
 * first that can't be copied. */
static int copy_tree(const char *from, const char *to, int depth)
{
    char a[1100], b[1100];
    struct stat st;
    struct dirent *e;
    DIR *d;
    int ok = 1;
    if (depth > PB_COPY_DEPTH || !make_folder(to) || !(d = opendir(from))) {
        fprintf(stderr, "[PLAYBACK] can't copy %s to %s\n", from, to);
        return 0;
    }
    while (ok && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if ((size_t)snprintf(a, sizeof a, "%s/%s", from, e->d_name) >= sizeof a
            || (size_t)snprintf(b, sizeof b, "%s/%s", to, e->d_name) >= sizeof b || stat(a, &st)) {
            fprintf(stderr, "[PLAYBACK] can't copy %s/%s\n", from, e->d_name);
            ok = 0;
        } else if (S_ISDIR(st.st_mode))
            ok = copy_tree(a, b, depth + 1);    /* which says what it can't */
        else if (S_ISREG(st.st_mode) && !(ok = copy_file(a, b)))
            fprintf(stderr, "[PLAYBACK] can't copy %s to %s\n", a, b);
    }
    closedir(d);
    return ok;
}

/* A setting or developer line's key=value, after its first word: the key
 * (no space or =) into key, and where its value starts; 0 for one that
 * can't be read. */
static int read_setting(const char *s, char *key, size_t n, const char **value)
{
    const char *eq = strchr(s, '=');
    size_t k = eq ? (size_t)(eq - s) : 0;
    if (!k || k >= n || strcspn(s, " \t") < k) return 0;
    memcpy(key, s, k);
    key[k] = '\0';
    *value = eq + 1;
    return 1;
}

/* key=value in a file of key=value lines (the settings file, developer.cfg):
 * its line replaced, or added at the end; the file made if need be. */
static int set_key(const char *path, const char *key, const char *value)
{
    char line[PB_LINE];
    char *text = NULL;
    size_t len = 0, k = strlen(key);
    int found = 0, bad = 0;
    FILE *f = fopen(path, "r");
    while (f && fgets(line, sizeof line, f)) {
        size_t l;
        char *grown;
        if (!found && !strncmp(line, key, k) && line[k] == '=') {
            found = 1;
            snprintf(line, sizeof line, "%s=%s\n", key, value);
        }
        l = strlen(line);
        if (!(grown = (char *)realloc(text, len + l + 1))) { bad = 1; break; }
        text = grown;
        memcpy(text + len, line, l + 1);
        len += l;
    }
    if (f) fclose(f);
    if (!bad && (f = fopen(path, "w"))) {
        if (len) fputs(text, f);
        if (len && text[len - 1] != '\n') fputc('\n', f);
        if (!found) fprintf(f, "%s=%s\n", key, value);
        bad = ferror(f);
        bad |= fclose(f);
    } else
        bad = 1;
    free(text);
    return !bad;
}

/* A line of steps.txt, its ends trimmed, in place. */
static char *trimmed(char *s)
{
    size_t n;
    while (*s == ' ' || *s == '\t') s++;
    n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
    return s;
}

/* --play's run with a profile/ (the settings file, developer.cfg,
 * start-states.txt, saves/ and cache/, as a portable folder has them): a
 * copy of it in a folder of this play's own (plays/<date>-<time>/), with
 * steps.txt's setting and developer lines written into it, and the
 * settings file, the engine's save folder and the game's saves pointed at
 * it, before anything reads them. The player's own are never opened. A
 * copy that fails is said, and playback_play refuses to play on it. */
static void profile_copy(const char *run)
{
    char from[1100], path[1200], line[PB_LINE], key[64];
    struct stat st;
    FILE *f;
    int n = 0, ok;
    s_profile = s_profile_bad = 0;
    s_play_dir[0] = s_play_for[0] = '\0';
    snprintf(from, sizeof from, "%s/" PLAYBACK_PROFILE, run);
    if (stat(from, &st) || !S_ISDIR(st.st_mode)) return;
    s_profile = 1;
    if (!play_folder(run)) {
        fprintf(stderr, "[PLAYBACK] no folder for this play in %s/" PLAYBACK_PLAYS "\n", run);
        s_profile_bad = 1;
        return;
    }
    ok = copy_tree(from, s_play_dir, 0);
    snprintf(path, sizeof path, "%s/saves", s_play_dir);
    ok = make_folder(path) && ok;
    set_env("JSRF_USER_DIR", path);
    snprintf(path, sizeof path, "%s/cache", s_play_dir);
    ok = make_folder(path) && ok;
    set_env("JSRF_SAVE_DIR", path);
    snprintf(path, sizeof path, "%s/" PLAYBACK_PROFILE_SETTINGS, s_play_dir);
    set_env("JSRF_SETTINGS_FILE", path);
    fprintf(stderr, "[PLAYBACK] a copy of %s for this play, in %s: its settings, Developer switches and saves;"
            " yours aren't opened\n", from, s_play_dir);
    snprintf(path, sizeof path, "%s/" PLAYBACK_STEPS, run);
    f = fopen(path, "r");
    while (ok && f && fgets(line, sizeof line, f)) {
        char *s = trimmed(line);
        const char *value;
        int dev = !strncmp(s, "developer ", 10);
        n++;
        if (!dev && strncmp(s, "setting ", 8)) continue;
        if (!read_setting(s + (dev ? 10 : 8), key, sizeof key, &value)) continue;  /* playback_play says */
        snprintf(path, sizeof path, "%s/%s", s_play_dir, dev ? "developer.cfg" : PLAYBACK_PROFILE_SETTINGS);
        ok = set_key(path, key, value);
        fprintf(stderr, "[PLAYBACK] " PLAYBACK_STEPS ", line %d: %s=%s %s the copy's %s\n", n, key, value,
                ok ? "in" : "can't be written into", dev ? "developer.cfg" : PLAYBACK_PROFILE_SETTINGS);
    }
    if (f) fclose(f);
    s_profile_bad = !ok;
}

/* The run's steps.txt beside its recording, if it has one, every line
 * checked against the recording just loaded: 0, saying where, at the first
 * that can't be read, names a moment the recording doesn't have or comes
 * before the one above it, or changes a setting with no copy of a profile
 * to change it in. */
static int load_steps(const char *run)
{
    char path[1100], line[PB_LINE], key[64];
    FILE *f;
    int n = 0, shots = 0, menus = 0;
    uint32_t last = 0;
    snprintf(path, sizeof path, "%s/" PLAYBACK_STEPS, run);
    if (!(f = fopen(path, "r"))) return 1;          /* none: the recording alone */
    while (fgets(line, sizeof line, f)) {
        char *s = trimmed(line), *e;
        const char *why = NULL, *value;
        unsigned long v;
        PbStep st, *to;
        n++;
        if (!*s || *s == '#') continue;
        if (!strncmp(s, "setting ", 8) || !strncmp(s, "developer ", 10)) {
            if (!read_setting(s + (s[0] == 's' ? 8 : 10), key, sizeof key, &value))
                why = "not key=value";
            else if (!s_profile)
                why = "a setting goes into a copy of the run's " PLAYBACK_PROFILE "/, which only --play makes";
            if (!why) continue;                     /* written into the copy (profile_copy) */
        } else {
            memset(&st, 0, sizeof st);
            st.line = n;
            st.idle = !strncmp(s, "idle ", 5);
            if (!st.idle && strncmp(s, "frame ", 6)) { why = "not a step"; goto said; }
            s += st.idle ? 5 : 6;
            v = strtoul(s, &e, 10);
            if (e == s || *s == '-' || *s == '+' || e[0] != ':' || e[1] != ' ') { why = "not a step"; goto said; }
            s = e + 2;
            if (st.idle ? v < 1 || v > (unsigned long)s_idles.n : v > s_end) {
                why = st.idle ? "the recording has no such idle moment" : "past the recording's end";
                goto said;
            }
            st.at = (uint32_t)v;
            if ((st.idle ? PB_IDLE[v - 1].frame : st.at) < last) {
                why = "comes before the step above it in the recording";
                goto said;
            }
            last = st.idle ? PB_IDLE[v - 1].frame : st.at;
            if (!strcmp(s, "screenshot")) st.what = PB_STEP_SHOT, s += 10;
            else if (!strncmp(s, "mark ", 5)) st.what = PB_STEP_MARK, s += 5;
            else if (!strncmp(s, "menu ", 5)) st.what = PB_STEP_MENU, s += 5;
            else { why = "not a step"; goto said; }
#if !defined(XBOXRECOMP_MENU)
            if (st.what == PB_STEP_MENU) { why = "a menu row, in a build without the in-game menu"; goto said; }
#endif
            if (st.what == PB_STEP_MENU && !s_profile)
                why = "a menu row changes settings: it needs a copy of the run's " PLAYBACK_PROFILE "/, which only"
                      " --play makes";
            else if (st.what != PB_STEP_SHOT && !*s)
                why = "nothing after it";
            else if (strlen(s) >= sizeof st.text)
                why = "too long";
            else if (!(to = (PbStep *)add(&s_steps, sizeof st)))
                why = "no memory for it";
            else {
                snprintf(st.text, sizeof st.text, "%s", s);
                *to = st;
                shots += st.what == PB_STEP_SHOT;
                menus += st.what == PB_STEP_MENU;
            }
        }
said:
        if (why) {
            fprintf(stderr, "[PLAYBACK] %s, line %d: %s; nothing is played\n", path, n, why);
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    if (shots && !s_play_dir[0] && !play_folder(run)) {
        fprintf(stderr, "[PLAYBACK] %s: no folder in %s/" PLAYBACK_PLAYS " for its screenshots; nothing is"
                " played\n", path, run);
        return 0;
    }
    s_steps_checked = !menus;
    fprintf(stderr, "[PLAYBACK] %s: %d steps, %d of them menu rows, %d screenshots%s%s\n", path, s_steps.n, menus,
            shots, shots ? ", into " : "", shots ? s_play_dir : "");
    return 1;
}

int playback_play(const char *folder, int exit_at_end)
{
    char path[sizeof s_folder + 32];
    int p, pads = 0;
    pb_lock();
    snprintf(s_folder, sizeof s_folder, "%s", folder);
    snprintf(path, sizeof path, "%s/" PLAYBACK_FILE, folder);
    run_start();
    /* This run's copy of its profile/ (playback_first), only if it is this
     * run's. */
    if (strcmp(s_play_for, folder) || !s_profile) {
        s_profile = s_profile_bad = 0;
        s_play_dir[0] = s_play_for[0] = '\0';
    }
    settings_back();                        /* a play before this one's, not ended */
    s_step_next = s_steps_run = s_holding = 0;
    s_step_settle = 0;
    s_steps_checked = 1;
    if (s_profile_bad)
        fprintf(stderr, "[PLAYBACK] %s/" PLAYBACK_PROFILE " couldn't be copied for this play; nothing is played\n",
                folder);
    if (s_profile_bad || !load(path) || !load_steps(folder)) {
        forget();
        s_res.state = PLAYBACK_OFF;
        pb_unlock();
        return 0;
    }
    for (p = 0; p < PLAYBACK_PORTS; p++) {
        s_cur[p] = -1;
        s_armed[p] = 0;
        pads += s_pads[p].n;
    }
    s_fc_cur = -1;
    s_fc_player_seen = 0;
    s_mouse_next = s_mouse_armed = 0;
    s_cam_cur = -1;
    s_cam_player_seen = s_cam_before_said = 0;
    s_anchor_next = s_check_next = s_idle_next = 0;
    s_offset = 0;
    s_exit_at_end = exit_at_end;
    s_drift_run = 0;
    s_drift_said = -1;
    s_random_said = s_window_held = 0;
    s_follow_next = s_trace_next = s_setting_next = 0;
    s_freecam_said = s_follow_said = s_trace_said = 0;
    memset(s_set_play, 0, sizeof s_set_play);
    s_start_check = 1;
    s_res.state = PLAYBACK_PLAYING;
    s_res.anchors = s_anchors.n;
    playback_mark_play(&s_mark);
    hook_exit();
    fprintf(stderr, "[PLAYBACK] playing %s: %d pad changes, %d free camera changes, %d mouse moves,"
            " %d camera tick changes, %d setting lines, %d anchors, %d checks, %d free camera takeovers,"
            " %d trace frames, %d idle moments, %u frames%s\n", path, pads, s_freecams.n, s_mice.n,
            s_cameras.n, s_settings.n, s_anchors.n, s_checks.n, s_follows.n, s_traces.n, s_idles.n, s_end,
            exit_at_end ? "; the game closes at its end" : "; the game is yours at its end");
    pb_unlock();
    return 1;
}

/* ---- playing ------------------------------------------------------------- */

static int playing(void)
{
    return s_res.state == PLAYBACK_PLAYING || s_res.state == PLAYBACK_WAITING;
}

/* The recording's frame now: held at the next anchor's while waiting. */
static uint32_t recorded_frame(void)
{
    int64_t f = (int64_t)s_frame - s_offset;
    if (s_res.state == PLAYBACK_WAITING) return PB_ANCHOR[s_anchor_next].frame;
    return f < 0 ? 0u : (uint32_t)f;
}

/* At the first pass of a play: this run started where the recording did
 * (playback_first put a start line's start in place), as or with the same,
 * or it is off route before anything is pressed. Missions has refused, or
 * not, by then (missions_paths). */
static int play_started_there(void)
{
    PbStart now = start_now();
    char rec[96], here[96];
    if (same_start(&s_start, &now)) return 1;
    say_start(&s_start, rec, sizeof rec);
    say_start(&now, here, sizeof here);
    off_route("the recording started at %s; this run at %s", rec, here);
    return 0;
}

/* A step's moment come: its idle moment reached (put back or not), or its
 * frame. */
static int step_due(const PbStep *st)
{
    return st->idle ? s_idle_next >= (int)st->at : recorded_frame() >= st->at;
}

/* The recording held where it is (the pad as it has it there, still at an
 * idle moment): while the steps' menu rows are still to be found, and while
 * an idle moment's steps run. */
static int steps_hold(void)
{
    if (!s_steps_checked) return 1;
    return s_step_next < s_steps.n && PB_STEP[s_step_next].idle && step_due(&PB_STEP[s_step_next]);
}

/* A play stopped by its steps: a menu row that isn't there, or steps the
 * window never ran. */
static void steps_stop(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vsnprintf(s_res.why, sizeof s_res.why, fmt, a);
    va_end(a);
    fprintf(stderr, "[PLAYBACK] frame %u: %s: the play stops\n", s_frame, s_res.why);
    play_over(PLAYBACK_OFF);
}

/* The recording's setting lines up to frame f put in place (k_setting), the
 * player's kept to give back at the end: said, with where the player's
 * differ. */
static void play_settings(uint32_t f)
{
    char said[512] = "";
    int n = 0, first = s_setting_next == 0;
    while (s_setting_next < s_settings.n && PB_SETTING[s_setting_next].frame <= f) {
        const PbSetting *e = &PB_SETTING[s_setting_next++];
        int k = e->key, was = k_setting[k].get();
        size_t len = strlen(said);
        if (!s_set_play[k].put) {
            s_set_play[k].put = 1;
            s_set_play[k].player = was;
            if (was != e->value) s_res.settings_changed++;
        }
        s_set_play[k].now = e->value;
        if (was != e->value) k_setting[k].set(e->value);
        snprintf(said + len, sizeof said - len, "%s%s %d%s", n++ ? ", " : "", k_setting[k].name, e->value,
                 was != e->value ? (first ? " (yours differs)" : " (changed)") : "");
    }
    if (n)
        fprintf(stderr, "[PLAYBACK] frame %u: %s settings as recorded at frame %u, for this play: %s\n",
                s_frame, first ? "the" : "more", f, said);
}

/* At the start of each pass: the recording's settings, the next anchor
 * waited for, or past the end. */
static void play_pass(void)
{
    uint32_t f = recorded_frame();
    if (s_start_check) {
        s_start_check = 0;
        if (!play_started_there()) return;
    }
    play_settings(f);
    if (s_res.state == PLAYBACK_PLAYING && steps_hold()) {
        if (!s_holding) { s_holding = 1; s_hold_from = s_frame; }
        s_window_held = 1;
        s_offset++;
        s_res.frames++;
        if (s_frame - s_hold_from > PLAYBACK_STEP_WAIT) {
            if (!s_steps_checked)
                steps_stop(PLAYBACK_STEPS "'s menu rows not looked for within %u frames (no picture shown)",
                           PLAYBACK_STEP_WAIT);
            else
                steps_stop(PLAYBACK_STEPS ", line %d: not run within %u frames (no picture shown)",
                           PB_STEP[s_step_next].line, PLAYBACK_STEP_WAIT);
        }
        return;
    }
    s_holding = 0;
    if (s_res.state == PLAYBACK_PLAYING && s_anchor_next < s_anchors.n
        && f >= PB_ANCHOR[s_anchor_next].frame) {
        const PbAnchor *a = &PB_ANCHOR[s_anchor_next];
        s_res.state = PLAYBACK_WAITING;
        s_wait_from = s_frame;
        fprintf(stderr, "[PLAYBACK] frame %u: waiting for anchor %d of %d (%s %08X), recorded at frame %u\n",
                s_frame, s_anchor_next + 1, s_anchors.n, k_kind[a->kind], a->value, a->frame);
    }
    if (s_res.state == PLAYBACK_WAITING) {
        s_res.frames++;
        if (s_frame - s_wait_from > PLAYBACK_ANCHOR_WAIT)
            off_route("anchor %d (%s %08X) not reached within %u frames of recorded frame %u",
                      s_anchor_next + 1, k_kind[PB_ANCHOR[s_anchor_next].kind],
                      PB_ANCHOR[s_anchor_next].value, PLAYBACK_ANCHOR_WAIT, PB_ANCHOR[s_anchor_next].frame);
        return;
    }
    if (f > s_end) {
        fprintf(stderr, "[PLAYBACK] frame %u: past the recording's end, frame %u\n", s_frame, s_end);
        play_over(PLAYBACK_FINISHED);
        return;
    }
    s_res.frames++;
}

static void play_anchor(PLAYBACK_ANCHOR kind, uint32_t value, uint32_t *random)
{
    uint32_t was = recorded_frame();
    int j;
    for (j = s_anchor_next; j < s_anchors.n && j <= s_anchor_next + PLAYBACK_ANCHOR_AHEAD; j++)
        if (PB_ANCHOR[j].kind == kind && PB_ANCHOR[j].value == value) break;
    if (j >= s_anchors.n || j > s_anchor_next + PLAYBACK_ANCHOR_AHEAD) {
        if (s_anchor_next < s_anchors.n)
            off_route("%s %08X reached where the recording has %s %08X next", k_kind[kind], value,
                      k_kind[PB_ANCHOR[s_anchor_next].kind], PB_ANCHOR[s_anchor_next].value);
        else
            off_route("%s %08X reached after the recording's last anchor", k_kind[kind], value);
        return;
    }
    /* Reached late, the pad was held for it (recorded_frame); reached
     * early, the recording skips to it. Either way it goes on from there. */
    fprintf(stderr, "[PLAYBACK] frame %u: anchor %d of %d met (%s %08X), recorded at frame %u: %u frames"
            " %s%s\n", s_frame, j + 1, s_anchors.n, k_kind[kind], value, PB_ANCHOR[j].frame,
            s_res.state == PLAYBACK_WAITING ? s_frame - s_wait_from : PB_ANCHOR[j].frame - was,
            s_res.state == PLAYBACK_WAITING ? "late" : "early",
            j > s_anchor_next ? ", past anchors the game didn't reach" : "");
    s_res.anchors_skipped += j - s_anchor_next;
    s_res.anchors_met++;
    if ((int64_t)s_frame - PB_ANCHOR[j].frame != s_offset) s_window_held = 1;   /* skipped to */
    s_offset = (int64_t)s_frame - PB_ANCHOR[j].frame;
    s_anchor_next = j + 1;
    s_res.state = PLAYBACK_PLAYING;
    if (random) *random = PB_ANCHOR[j].random;
}

/* The recording's pad for this read of this port. */
static int play_pad(uint32_t port, XBOX_INPUT_STATE *st, int connected)
{
    uint32_t f = recorded_frame(), r = s_reads[port];
    const PbPad *ev = PB_PADS(port);
    int c = s_cur[port];
    /* A real pad or the keyboard, once let go of since playback started. */
    if (connected && pressed(&st->Gamepad)) {
        if (s_armed[port]) {
            fprintf(stderr, "[PLAYBACK] frame %u: something was pressed on port %u: the game is yours\n",
                    s_frame, port);
            play_over(PLAYBACK_HANDED_BACK);
            return connected;
        }
    } else
        s_armed[port] = 1;
    while (c + 1 < s_pads[port].n
           && (ev[c + 1].frame < f || (ev[c + 1].frame == f && ev[c + 1].read <= r)))
        c++;
    s_cur[port] = c;
    memset(st, 0, sizeof *st);
    st->dwPacketNumber = ++s_packet[port];
    if (c < 0) return port == 0;          /* before the recording's first read: as pad_read has it */
    if (ev[c].none) return 0;
    if (!s_holding) st->Gamepad = ev[c].pad;    /* held still while steps hold the recording */
    return 1;
}

/* The recording's free camera for this frame, in place of the player's:
 * from its first freecam line (before it, or with none, the player's). */
static int play_freecam(FREECAM_SETTINGS *fc)
{
    uint32_t f = recorded_frame();
    int c = s_fc_cur;
    /* The player's own change, by F8, the pad or the menu: the game is theirs. */
    if (s_fc_player_seen && !same_freecam(fc, &s_fc_player) && !s_step_running) {
        fprintf(stderr, "[PLAYBACK] frame %u: the free camera was changed (F8, the pad or the menu): the game is yours\n",
                s_frame);
        play_over(PLAYBACK_HANDED_BACK);
        return 0;
    }
    s_fc_player = *fc;
    s_fc_player_seen = 1;
    while (c + 1 < s_freecams.n && PB_FREECAM[c + 1].frame <= f) c++;
    if (c < 0) return 0;
    if (c != s_fc_cur) {
        const FREECAM_SETTINGS *r = &PB_FREECAM[c].fc;
        fprintf(stderr, "[PLAYBACK] frame %u: the free camera as recorded at frame %u: %s; invert x %d y %d,"
                " sensitivity x %g y %g, deadzone %g, auto centre %s, mouse look %s at %g\n", s_frame,
                PB_FREECAM[c].frame, r->on ? "on" : "off", r->invert_x, r->invert_y, r->sens_x, r->sens_y,
                r->deadzone, freecam_centre_name(r->centre), r->mouse ? "on" : "off", r->mouse_sens);
    }
    s_fc_cur = c;
    *fc = PB_FREECAM[c].fc;
    return 1;
}

/* The recording's mouse for this frame, in place of the player's: every
 * move up to the recording's frame not yet taken. */
static int play_mouse(int *dx, int *dy)
{
    uint32_t f = recorded_frame();
    /* The player's own mouse, once still since playback started. */
    if (*dx || *dy) {
        if (s_mouse_armed) {
            fprintf(stderr, "[PLAYBACK] frame %u: the mouse was moved: the game is yours\n", s_frame);
            play_over(PLAYBACK_HANDED_BACK);
            return 0;
        }
    } else
        s_mouse_armed = 1;
    *dx = *dy = 0;
    for (; s_mouse_next < s_mice.n && PB_MOUSE[s_mouse_next].frame <= f; s_mouse_next++) {
        *dx += PB_MOUSE[s_mouse_next].dx;
        *dy += PB_MOUSE[s_mouse_next].dy;
    }
    return 1;
}

/* The recording's Camera ticks for this frame, in place of the player's:
 * from its first camera line (before it, the player's), or, with none,
 * PLAYBACK_CAMSCENES_BEFORE. */
static int play_camscenes(unsigned *ticks)
{
    uint32_t f = recorded_frame();
    int c = s_cam_cur;
    /* The player's own change, in the menu: the game is theirs. */
    if (s_cam_player_seen && *ticks != s_cam_player && !s_step_running) {
        fprintf(stderr, "[PLAYBACK] frame %u: the camera ticks were changed (the menu): the game is yours\n",
                s_frame);
        play_over(PLAYBACK_HANDED_BACK);
        return 0;
    }
    s_cam_player = *ticks;
    s_cam_player_seen = 1;
    if (!s_cameras.n) {
        if (!s_cam_before_said++)
            fprintf(stderr, "[PLAYBACK] frame %u: no camera line: the game's own shots as before the Camera"
                    " ticks (the graffiti shot only while the free camera is off)\n", s_frame);
        *ticks = PLAYBACK_CAMSCENES_BEFORE;
        return 1;
    }
    while (c + 1 < s_cameras.n && PB_CAMERA[c + 1].frame <= f) c++;
    if (c < 0) return 0;
    if (c != s_cam_cur) {
        unsigned r = PB_CAMERA[c].ticks;
        fprintf(stderr, "[PLAYBACK] frame %u: the camera ticks as recorded at frame %u: Graffiti cam %s,"
                " Half pipe cam %s, Loop cam %s\n", s_frame, PB_CAMERA[c].frame, r & 1u ? "on" : "off",
                r & 2u ? "on" : "off", r & 4u ? "on" : "off");
    }
    s_cam_cur = c;
    *ticks = PB_CAMERA[c].ticks;
    return 1;
}

/* At a check the play reached on its frame: the random generator against
 * the recording's, with the draws since the check before (unless the play
 * held its pad since), said when they differ; then put back as recorded. */
static void play_random(const PbSpot *c, uint32_t *random)
{
    uint32_t n[2];
    char sites[PB_DRAW_SITES * 24];
    int held = s_window_held;
    drawn_take(n, sites, sizeof sites);
    s_window_held = 0;
    if (!c->has_random || !random) return;
    if (!held) {
        s_res.random_checks++;
        if (*random == c->random && n[1] == c->drawn && n[0] == c->others)
            s_res.random_same++;
        else {
            if (s_res.random_checks - s_res.random_same == 1) s_res.random_apart_at = c->frame;
            if (s_random_said++ < PB_RANDOM_SAY)
                fprintf(stderr, "[PLAYBACK] frame %u: check at recorded frame %u: the game's random numbers"
                        " %08X, recorded %08X: drawn %u times on the game's thread since the check before"
                        " (recorded %u), %u on others (recorded %u); where they return to: %s; put back"
                        " as recorded%s\n", s_frame, c->frame, *random, c->random, n[1], c->drawn, n[0],
                        c->others, sites[0] ? sites : "none",
                        s_random_said == PB_RANDOM_SAY ? " (no more said: the summary counts them)" : "");
        }
    }
    *random = c->random;
}

/* An angle in degrees, for the log. */
static double degrees(double radians)
{
    return radians * 180.0 / 3.14159265358979323846;
}

/* The free camera at a check reached on its frame: compared with the
 * recording's, said where it isn't, and put back as recorded while both
 * steer. */
static void play_freecam_check(const PbSpot *c)
{
    double yaw = 0, pitch = 0;
    int on;
    if (!c->has_view) return;
    on = freecam_angles(&yaw, &pitch);
    s_res.freecam_checks++;
    if (on == c->view_on && (!on || (yaw == c->yaw && pitch == c->pitch))) {
        s_res.freecam_same++;
        return;
    }
    if (s_res.freecam_checks - s_res.freecam_same == 1) s_res.freecam_apart_at = c->frame;
    if (on && c->view_on) freecam_put(c->yaw, c->pitch);
    if (s_freecam_said++ >= PLAYBACK_SAY) return;
    if (on && c->view_on)
        fprintf(stderr, "[PLAYBACK] frame %u: check at recorded frame %u: the free camera's heading %.6f"
                " degrees, pitch %.6f, recorded %.6f, %.6f; put back as recorded%s\n", s_frame, c->frame,
                degrees(yaw), degrees(pitch), degrees(c->yaw), degrees(c->pitch),
                s_freecam_said == PLAYBACK_SAY ? " (no more said: the summary counts them)" : "");
    else
        fprintf(stderr, "[PLAYBACK] frame %u: check at recorded frame %u: the free camera %s, where the"
                " recording's %s; left as it is\n", s_frame, c->frame, on ? "steers" : "steers no camera",
                c->view_on ? "steers" : "steers none");
}

/* A frame of the trace reached on its frame: compared with the recording's,
 * and said what isn't as it was. Nothing is put back. */
static void play_trace(const float position[3], uint32_t turn, uint32_t camera, const float look[2])
{
    uint32_t f = recorded_frame();
    const PbTrace *t;
    double yaw = 0, pitch = 0;
    char said[640] = "";
    size_t len;
    int on;
    while (s_trace_next < s_traces.n && PB_TRACE[s_trace_next].frame < f) s_trace_next++;
    if (s_trace_next >= s_traces.n || PB_TRACE[s_trace_next].frame != f) return;
    t = &PB_TRACE[s_trace_next++];
    on = freecam_angles(&yaw, &pitch);
    if (!s_res.trace_frames)
        fprintf(stderr, "[PLAYBACK] frame %u: the trace from recorded frame %u, %d frames: compared frame by"
                " frame\n", s_frame, t->frame, s_traces.n);
    s_res.trace_frames++;
    if (memcmp(position, t->pos, sizeof t->pos)) {
        len = strlen(said);
        snprintf(said + len, sizeof said - len, "; the skater at %.9g %.9g %.9g, recorded %.9g %.9g %.9g",
                 position[0], position[1], position[2], t->pos[0], t->pos[1], t->pos[2]);
    }
    if (turn != t->turn || camera != t->camera) {
        len = strlen(said);
        snprintf(said + len, sizeof said - len, "; turn %08X, camera %u, recorded %08X, %u", turn, camera,
                 t->turn, t->camera);
    }
    if (look[0] != t->look[0] || look[1] != t->look[1]) {
        len = strlen(said);
        snprintf(said + len, sizeof said - len, "; the camera looking %.9g %.9g, recorded %.9g %.9g", look[0],
                 look[1], t->look[0], t->look[1]);
    }
    if (on != t->view_on || (on && (yaw != t->yaw || pitch != t->pitch))) {
        len = strlen(said);
        if (on && t->view_on)
            snprintf(said + len, sizeof said - len, "; the free camera's heading %.6f degrees, pitch %.6f,"
                     " recorded %.6f, %.6f", degrees(yaw), degrees(pitch), degrees(t->yaw), degrees(t->pitch));
        else
            snprintf(said + len, sizeof said - len, "; the free camera %s, recorded %s",
                     on ? "steering" : "steering none", t->view_on ? "steering" : "none");
    }
    if (!said[0]) { s_res.trace_same++; return; }
    if (s_res.trace_frames - s_res.trace_same == 1) s_res.trace_apart_at = t->frame;
    if (s_trace_said++ < PLAYBACK_SAY)
        fprintf(stderr, "[PLAYBACK] frame %u: trace at recorded frame %u: not as recorded%s%s\n", s_frame,
                t->frame, said, s_trace_said == PLAYBACK_SAY ? " (no more said: the summary counts them)" : "");
}

static void play_view(float position[3], uint32_t *turn, uint32_t camera, const float look[2], uint32_t *random,
                      int still)
{
    uint32_t f = recorded_frame();
    int last = -1;
    play_trace(position, *turn, camera, look);
    while (s_check_next < s_checks.n && PB_CHECK[s_check_next].frame <= f) last = s_check_next++;
    if (last >= 0 && f == PB_CHECK[last].frame) {
        play_random(&PB_CHECK[last], random);
        play_freecam_check(&PB_CHECK[last]);
    }
    else if (last >= 0) {
        uint32_t n[2];
        drawn_take(n, NULL, 0);             /* reached late: the draws can't be compared */
        s_window_held = 0;
    }
    if (last >= 0 && f - PB_CHECK[last].frame < PLAYBACK_CHECK_EVERY) {
        const PbSpot *c = &PB_CHECK[last];
        float d = distance(position, c->pos);
        s_res.checks++;
        if (d > s_res.drift_most) { s_res.drift_most = d; s_res.drift_most_at = c->frame; }
        /* Said when it changes much: by half, or a unit, whichever is more. */
        if (s_drift_said < 0 || d - s_drift_said > (s_drift_said > 2 ? s_drift_said / 2 : 1)
            || s_drift_said - d > (s_drift_said > 2 ? s_drift_said / 2 : 1)) {
            fprintf(stderr, "[PLAYBACK] frame %u: check at recorded frame %u: %.1f units off,"
                    " camera %u (recorded %u)\n", s_frame, c->frame, d, camera, c->camera);
            s_drift_said = d;
        }
        s_drift_run = d > PLAYBACK_DRIFT_LIMIT ? s_drift_run + 1 : 0;
        if (s_drift_run >= PLAYBACK_DRIFT_CHECKS) {
            off_route("%.1f units from the recorded spot at recorded frame %u, %d checks in a row"
                      " past %.0f", d, c->frame, s_drift_run, PLAYBACK_DRIFT_LIMIT);
            return;
        }
    }
    /* An idle moment: the skater put back, once still here too. */
    while (s_idle_next < s_idles.n && f >= PB_IDLE[s_idle_next].frame) {
        const PbSpot *i = &PB_IDLE[s_idle_next];
        float d = distance(position, i->pos);
        if (f - i->frame >= PLAYBACK_CHECK_EVERY) {
            fprintf(stderr, "[PLAYBACK] frame %u: idle moment %d (recorded frame %u) passed without the skater"
                    " still: not put back\n", s_frame, s_idle_next + 1, i->frame);
            s_idle_next++;
            continue;
        }
        if (!still) break;                  /* not yet: wait for it */
        if (camera != i->camera || d > PLAYBACK_DRIFT_LIMIT)
            fprintf(stderr, "[PLAYBACK] frame %u: idle moment %d: not put back (%.1f units off, camera %u,"
                    " recorded %u)\n", s_frame, s_idle_next + 1, d, camera, i->camera);
        else {
            fprintf(stderr, "[PLAYBACK] frame %u: idle moment %d: the skater put back on the recorded spot,"
                    " %.1f units away, turn %08X to %08X\n", s_frame, s_idle_next + 1, d, *turn, i->turn);
            memcpy(position, i->pos, sizeof i->pos);
            *turn = i->turn;
            if (i->has_random && random && f == i->frame) *random = i->random;
            s_res.idles_put_back++;
        }
        s_idle_next++;
    }
}

/* ---- the game's calls ---------------------------------------------------- */

#if defined(XBOXRECOMP_MENU)
/* At the play's first flip: every menu row its steps name, looked for as
 * the menu has its rows now; the play stops at the first that isn't there,
 * or can't be chosen so. */
static void steps_check(void)
{
    int i, n, r = MENU_PATH_OK;
    PbStep st;
    pb_lock();
    n = s_steps.n;
    pb_unlock();
    for (i = 0; i < n; i++) {
        pb_lock();
        st = PB_STEP[i];
        pb_unlock();
        if (st.what != PB_STEP_MENU || (r = menu_path_check(st.text)) == MENU_PATH_OK) continue;
        pb_lock();
        if (playing())
            steps_stop(PLAYBACK_STEPS ", line %d: menu %s: %s", st.line, st.text, menu_path_why(r));
        pb_unlock();
        return;
    }
    pb_lock();
    if (playing() && !s_steps_checked) {
        s_steps_checked = 1;
        fprintf(stderr, "[PLAYBACK] frame %u: each menu row in " PLAYBACK_STEPS " is there\n", s_frame);
    }
    pb_unlock();
}
#endif

void playback_flip(int (*shot)(const char *path))
{
    char path[1200];
    PbStep st;
    uint32_t frame;
    int ok = 1, r = 0;
    pb_lock();
    if (!playing() || !s_steps.n) { pb_unlock(); return; }
#if defined(XBOXRECOMP_MENU)
    if (!s_steps_checked) {
        pb_unlock();
        steps_check();
        return;
    }
#endif
    if (s_step_next >= s_steps.n || !step_due(&PB_STEP[s_step_next])) { pb_unlock(); return; }
    if (s_step_settle) { s_step_settle--; pb_unlock(); return; }
    st = PB_STEP[s_step_next];
    frame = s_frame;
    s_step_running = 1;
    snprintf(path, sizeof path, "%s/shot-%d.bmp", s_play_dir, st.line);
    pb_unlock();
    /* Outside the lock: a row's own run may reach playback (Developer >
     * Playback's). */
#if defined(XBOXRECOMP_MENU)
    if (st.what == PB_STEP_MENU) ok = (r = menu_path_run(st.text)) == MENU_PATH_OK;
#endif
    if (st.what == PB_STEP_SHOT) ok = shot && shot(path);
    pb_lock();
    /* What the row changed is the player's from here (play_freecam,
     * play_camscenes). */
    s_step_running = 0;
    s_fc_player_seen = 0;
    s_cam_player_seen = 0;
    if (!playing()) { pb_unlock(); return; }
    switch (st.what) {
    case PB_STEP_MENU:
#if defined(XBOXRECOMP_MENU)
        if (!ok) {
            steps_stop(PLAYBACK_STEPS ", line %d: menu %s: %s", st.line, st.text, menu_path_why(r));
            pb_unlock();
            return;
        }
#endif
        fprintf(stderr, "[PLAYBACK] frame %u: menu %s (" PLAYBACK_STEPS ", line %d)\n", frame, st.text, st.line);
        s_step_settle = PLAYBACK_STEP_SETTLE;
        break;
    case PB_STEP_MARK:
        fprintf(stderr, "[PLAYBACK] frame %u: mark: %s (" PLAYBACK_STEPS ", line %d)\n", frame, st.text, st.line);
        break;
    default:
        fprintf(stderr, "[PLAYBACK] frame %u: screenshot %s%s (" PLAYBACK_STEPS ", line %d)\n", frame, path,
                ok ? "" : " couldn't be written", st.line);
        break;
    }
    s_step_next++;
    s_steps_run++;
    pb_unlock();
}

/* The settings a recording keeps (k_setting), at its first pass and as they
 * change. */
static void record_settings(void)
{
    int k;
    for (k = 0; k < PLAYBACK_SETTINGS; k++) {
        int v = k_setting[k].get();
        if (s_set_last[k].seen && s_set_last[k].value == v) continue;
        s_set_last[k].seen = 1;
        s_set_last[k].value = v;
        fprintf(rec(), "setting %u %s %d\n", s_frame, k_setting[k].key, v);
    }
}

void playback_pass(void)
{
    pb_lock();
    s_frame++;
    memset(s_reads, 0, sizeof s_reads);
    if (playing()) play_pass();
    else if (s_out) record_settings();
    pb_unlock();
}

int playback_mark(double now, PbMarkShape *s)
{
    int shown;
    pb_lock();
    shown = playback_mark_frame(&s_mark, now, s);
    pb_unlock();
    return shown;
}

/* The free camera's two, as a recording's line has them: each as short as
 * reads back the same, or - - while it steers none. */
static void put_angles(void)
{
    double yaw, pitch;
    if (!freecam_angles(&yaw, &pitch)) { fputs(" - -", rec()); return; }
    put_number(yaw);
    put_number(pitch);
}

void playback_freecam_took(double *yaw, double *pitch)
{
    uint32_t f;
    const PbFollow *e;
    pb_lock();
    if (s_out) {
        fprintf(rec(), "follow %u", s_frame);
        put_number(*yaw);
        put_number(*pitch);
        fputc('\n', rec());
        fprintf(stderr, "[PLAYBACK] frame %u: the free camera took over, heading %.6f degrees, pitch %.6f:"
                " recorded\n", s_frame, degrees(*yaw), degrees(*pitch));
    } else if (playing()) {
        f = recorded_frame();
        while (s_follow_next < s_follows.n && PB_FOLLOW[s_follow_next].frame < f) s_follow_next++;
        e = s_follow_next < s_follows.n && PB_FOLLOW[s_follow_next].frame == f ? &PB_FOLLOW[s_follow_next++] : NULL;
        if (!e) {
            if (s_follows.n && s_follow_said++ < PLAYBACK_SAY)
                fprintf(stderr, "[PLAYBACK] frame %u: the free camera took over at recorded frame %u, heading"
                        " %.6f degrees, pitch %.6f, where the recording has it take over at no frame:"
                        " left as it is\n", s_frame, f, degrees(*yaw), degrees(*pitch));
        } else {
            int same = *yaw == e->yaw && *pitch == e->pitch;
            s_res.follows++;
            s_res.follows_same += same;
            if (s_follow_said++ < PLAYBACK_SAY)
                fprintf(stderr, "[PLAYBACK] frame %u: the free camera took over at recorded frame %u, heading"
                        " %.6f degrees, pitch %.6f%s%.6f, %.6f%s\n", s_frame, f, degrees(*yaw), degrees(*pitch),
                        same ? ", as recorded (" : "; recorded ", degrees(e->yaw), degrees(e->pitch),
                        same ? ")" : ": put back as recorded");
            *yaw = e->yaw;
            *pitch = e->pitch;
        }
    }
    pb_unlock();
}

int playback_pad(uint32_t port, XBOX_INPUT_STATE *st, int connected)
{
    if (port >= PLAYBACK_PORTS) return connected;
    pb_lock();
    if (playing()) connected = play_pad(port, st, connected);
    else if (s_out) record_pad(port, &st->Gamepad, !connected);
    if (connected && pressed(&st->Gamepad)) { s_pressed_at = s_frame; s_pressed_ever = 1; }
    s_reads[port]++;
    pb_unlock();
    return connected;
}

int playback_freecam(FREECAM_SETTINGS *fc)
{
    int played = 0;
    pb_lock();
    if (playing()) played = play_freecam(fc);
    else if (s_out) record_freecam(fc);
    pb_unlock();
    return played;
}

int playback_mouse(int *dx, int *dy)
{
    int played = 0;
    pb_lock();
    if (playing()) played = play_mouse(dx, dy);
    else if (s_out && (*dx || *dy)) fprintf(rec(), "mouse %u %d %d\n", s_frame, *dx, *dy);
    pb_unlock();
    return played;
}

int playback_camscenes(unsigned *ticks)
{
    int played = 0;
    pb_lock();
    if (playing()) played = play_camscenes(ticks);
    else if (s_out) record_camscenes(*ticks);
    pb_unlock();
    return played;
}

int playback_freecam_back(void)
{
    int back;
    pb_lock();
    back = !playing() ? PLAYBACK_BACK_OUT : s_cam_back;
    pb_unlock();
    return back;
}

void playback_anchor(PLAYBACK_ANCHOR kind, uint32_t value, uint32_t *random)
{
    if ((unsigned)kind >= PLAYBACK_ANCHOR_KINDS) return;
    pb_lock();
    if (s_out) {
        fprintf(rec(), "anchor %u %s %08X %08X\n", s_frame, k_kind[kind], value, random ? *random : 0u);
        fflush(s_out);
    } else if (playing())
        play_anchor(kind, value, random);
    pb_unlock();
}

void playback_view(float position[3], uint32_t *turn, uint32_t camera, const float look[2],
                   uint32_t *random)
{
    int still;
    pb_lock();
    if (!s_out && !playing()) { pb_unlock(); return; }
    /* Still: within PLAYBACK_STILL of where it stopped, in one camera mode,
     * with nothing pressed since, for PLAYBACK_IDLE_FRAMES. */
    if (!s_still_valid || distance(position, s_still_pos) > PLAYBACK_STILL || camera != s_still_camera
        || (s_pressed_ever && s_pressed_at >= s_still_since)) {
        memcpy(s_still_pos, position, sizeof s_still_pos);
        s_still_since = s_frame;
        s_still_camera = camera;
        s_still_valid = 1;
    }
    still = s_frame - s_still_since >= PLAYBACK_IDLE_FRAMES;
    if (s_out) {
        /* The trace: from the first frame something is pressed while the
         * camera follows the player, PLAYBACK_TRACE_FRAMES of them. */
        if (!s_trace_on && s_pressed_ever && s_pressed_at == s_frame) {
            s_trace_on = 1;
            s_trace_from = s_frame;
        }
        if (s_trace_on && s_frame - s_trace_from < PLAYBACK_TRACE_FRAMES) {
            fprintf(rec(), "trace %u %.9g %.9g %.9g %08X %u %.9g %.9g", s_frame, position[0], position[1],
                    position[2], *turn, camera, look[0], look[1]);
            put_angles();
            fputc('\n', rec());
        }
        if (s_frame >= s_next_check) {
            uint32_t n[2];
            drawn_take(n, NULL, 0);
            fprintf(rec(), "check %u %.9g %.9g %.9g %08X %u", s_frame, position[0], position[1],
                    position[2], *turn, camera);
            if (random) {
                fprintf(rec(), " %08X %u %u", *random, n[1], n[0]);
                put_angles();
            }
            fputc('\n', rec());
            fflush(s_out);
            s_next_check = s_frame + PLAYBACK_CHECK_EVERY;
            s_res.checks++;
        }
        if (still && s_idle_said != s_still_since + 1) {
            fprintf(rec(), "idle %u %.9g %.9g %.9g %08X %u", s_frame, position[0], position[1],
                    position[2], *turn, camera);
            if (random) fprintf(rec(), " %08X", *random);
            fputc('\n', rec());
            s_idle_said = s_still_since + 1;
        }
    } else
        play_view(position, turn, camera, look, random, still);
    pb_unlock();
}

PLAYBACK_STATE playback_state(void)
{
    PLAYBACK_STATE s;
    pb_lock();
    s = s_res.state;
    pb_unlock();
    return s;
}

const PLAYBACK_RESULT *playback_result(void) { return &s_res; }

/* ---- starting, and Developer > Playback -------------------------------- */

/* The folder recordings are kept in, beside the settings file: "" when
 * there is no settings file (a build without the window). */
static void recordings_folder(char *buf, size_t n)
{
#if defined(NV2A_GL_USE_SDL)
    nv_window_file_path(PLAYBACK_FOLDER, buf, n);
#else
    if (n) buf[0] = '\0';
#endif
}

#if defined(XBOXRECOMP_MENU) && defined(NV2A_GL_USE_SDL)
static char s_list[PB_LIST][PB_NAME];
static const char *s_choice[PB_LIST];
static int s_nlist;

/* The recordings in the folder, newest first: each folder with a
 * recording.txt in it, by its name, the date and time it was made. */
static int newest_first(const void *a, const void *b) { return strcmp(b, a); }
static void list_recordings(const char *folder)
{
    char all[256][PB_NAME], path[1200];
    struct stat st;
    struct dirent *e;
    DIR *d = opendir(folder);
    int n = 0, i;
    if (!d) return;
    while ((e = readdir(d)) != NULL && n < 256) {
        size_t len = strlen(e->d_name);
        if (e->d_name[0] == '.' || len >= PB_NAME) continue;
        snprintf(path, sizeof path, "%s/%s/" PLAYBACK_FILE, folder, e->d_name);
        if (!stat(path, &st)) memcpy(all[n++], e->d_name, len + 1);
    }
    closedir(d);
    qsort(all, (size_t)n, sizeof all[0], newest_first);
    for (i = 0; i < n && i < PB_LIST; i++) {
        memcpy(s_list[i], all[i], PB_NAME);
        s_choice[i] = s_list[i];
    }
    s_nlist = i;
}

static void record_pick(void)
{
    set_env(PLAYBACK_RECORD_VAR, "1");
    fprintf(stderr, "[PLAYBACK] Record picked in Developer > Playback: restarting\n");
    nv_window_restart();                /* does not return when it restarts */
    set_env(PLAYBACK_RECORD_VAR, "");
    fprintf(stderr, "[PLAYBACK] Record: the game couldn't restart; nothing is recorded\n");
}

static int  play_none(void) { return -1; }     /* no recording is ticked */
static void play_pick(int i)
{
    char folder[1024], path[1100];
    if (i < 0 || i >= s_nlist) return;
    recordings_folder(folder, sizeof folder);
    snprintf(path, sizeof path, "%s/%s", folder, s_list[i]);
    set_env("JSRF_PLAY_MENU", path);
    fprintf(stderr, "[PLAYBACK] Play %s picked in Developer > Playback: restarting\n", s_list[i]);
    nv_window_restart();                /* does not return when it restarts */
    set_env("JSRF_PLAY_MENU", "");
    fprintf(stderr, "[PLAYBACK] Play: the game couldn't restart; nothing is played\n");
}

static void rows(void)
{
    char folder[1024];
    XBOX_MENU_ITEM row[3];
    int n = 0;
    recordings_folder(folder, sizeof folder);
    list_recordings(folder);
    memset(row, 0, sizeof row);
    row[n].menu = XBOX_MENU_DEVELOPER;
    row[n].kind = XBOX_ITEM_ACTION;
    row[n].label = "Record (Restart)";
    row[n].group = XBOX_GROUP_PLAYBACK;
    row[n].run = record_pick;
    row[n].confirm = "Restart the game and record from the start, losing unsaved progress?";
    row[n++].hint = "Restarts the game and records every press, until Stop recording.";
    row[n].menu = XBOX_MENU_DEVELOPER;
    row[n].kind = XBOX_ITEM_ACTION;
    row[n].label = "Stop recording";
    row[n].group = XBOX_GROUP_PLAYBACK;
    row[n].run = playback_stop;
    row[n++].hint = "Ends the recording and keeps it.";
    /* Play only with a recording to play: the ones there were as the game
     * started, so one made since shows from the next start. */
    if (s_nlist) {
        row[n].menu = XBOX_MENU_DEVELOPER;
        row[n].kind = XBOX_ITEM_CHOICE;
        row[n].label = "Play (Restart)";
        row[n].group = XBOX_GROUP_PLAYBACK;
        row[n].state = play_none;
        row[n].pick = play_pick;
        row[n].choices = s_choice;
        row[n].nchoices = s_nlist;
        row[n].confirm = "Restart the game and play this recording, losing unsaved progress?";
        row[n++].hint = "Restarts the game and plays a recording. Any press ends it.";
    }
    xbox_MenuAdd(row, n);
    fprintf(stderr, "[PLAYBACK] Developer > Playback: %d recordings in %s%s\n", s_nlist, folder,
            s_nlist == PB_LIST ? " shown (the newest)" : "");
}
#endif

int playback_missions_ticked(void) { return s_missions_tick; }

void playback_missions_flip(void)
{
    s_missions_tick = !s_missions_tick;
    fprintf(stderr, "[PLAYBACK] Developer > Missions > Record %s\n", s_missions_tick ? "ticked" : "unticked");
}

/* The start line of the recording in folder, if it has one before its first
 * frame (as load() takes it), into *st; 0 when the file can't be read as a
 * recording that far, which playback_play then says. */
static int start_of(const char *folder, PbStart *st)
{
    char path[1100], line[PB_LINE], word[16];
    int format = 0, ok = 0;
    FILE *f;
    memset(st, 0, sizeof *st);
    snprintf(path, sizeof path, "%s/" PLAYBACK_FILE, folder);
    if (!(f = fopen(path, "r"))) return 0;
    while (fgets(line, sizeof line, f)) {
        const char *s = line;
        int used = 0;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#' || *s == '\r' || *s == '\n') continue;
        if (!format) {
            if (!(format = !strncmp(s, PB_FORMAT, strlen(PB_FORMAT)))) break;
            continue;
        }
        if (sscanf(s, "%15s %n", word, &used) != 1) break;
        if (!strcmp(word, "version")) continue;
        ok = strcmp(word, "start") || read_start(s + used, st);
        break;
    }
    fclose(f);
    return format && ok;
}

/* Missions' variables, as Developer > Missions' pick sets them
 * (missions_menu.c), and photo mode's. */
void playback_first(void)
{
    const char *p = getenv("JSRF_PLAY"), *m = getenv("JSRF_PLAY_MENU"), *folder = p && *p ? p : m;
    PbStart st;
    struct stat ps;
    char v[16], said[96], profile[1100];
    if (!folder || !*folder) return;
    /* --play's run on a copy of its profile/, before the settings file and
     * the rest are read. */
    if (p && *p) profile_copy(p);
    else {
        snprintf(profile, sizeof profile, "%s/" PLAYBACK_PROFILE, m);
        if (!stat(profile, &ps) && S_ISDIR(ps.st_mode))
            fprintf(stderr, "[PLAYBACK] %s left out: only --play plays on a copy of a run's profile; Play plays"
                    " on your own settings and saves\n", profile);
    }
    if (!start_of(folder, &st) || !st.set) return;
    if (getenv("JSRF_START_MISSION") || getenv("JSRF_START_LOAD"))
        fprintf(stderr, "[PLAYBACK] JSRF_START_MISSION or JSRF_START_LOAD replaced by the recording's start\n");
    snprintf(v, sizeof v, "%04d", st.mission);
    set_env("JSRF_START_MISSION", st.load ? "" : v);
    snprintf(v, sizeof v, "%d", st.skater);
    set_env("JSRF_START_SKATER", st.load || st.skater < 0 ? "" : v);
    set_env("JSRF_START_LOAD", st.load ? "1" : "");
    set_env(PHOTO_START_VAR, st.photo ? "1" : "");
    say_start(&st, said, sizeof said);
    fprintf(stderr, "[PLAYBACK] the recording to play started at %s: the game starts there, as Developer >"
            " Missions starts it\n", said);
}

/* A variable read once and taken out of the environment, so the game's own
 * restart (File > Restart) starts afresh: its value, kept, or NULL. */
static const char *take_env(const char *name, char *buf, size_t n)
{
    const char *v = getenv(name);
    if (!v || !*v) return NULL;
    snprintf(buf, n, "%s", v);
    set_env(name, "");
    return buf;
}

void playback_start(void)
{
    char rec[16], play[1024], menu[1024], folder[1024], path[1100];
    const char *r = take_env(PLAYBACK_RECORD_VAR, rec, sizeof rec);
    const char *p = take_env("JSRF_PLAY", play, sizeof play);
    const char *m = take_env("JSRF_PLAY_MENU", menu, sizeof menu);
#if defined(XBOXRECOMP_MENU) && defined(NV2A_GL_USE_SDL)
    rows();
#endif
    if (p || m) {
        if (r) fprintf(stderr, "[PLAYBACK] " PLAYBACK_RECORD_VAR " left out: a recording is played, not made\n");
        playback_play(p ? p : m, p != NULL);
        return;
    }
    if (!r) return;
    recordings_folder(folder, sizeof folder);
    if (!folder[0] || !make_folder(folder)) {
        fprintf(stderr, "[PLAYBACK] " PLAYBACK_RECORD_VAR ": no folder for recordings beside the settings file"
                " (%s); nothing is recorded\n", folder[0] ? folder : "none");
        return;
    }
    {
        time_t now = time(NULL);
        struct tm *utc = gmtime(&now);
        if (!utc) return;
        snprintf(path, sizeof path, "%s/%04d-%02d-%02d-%02d%02d%02d", folder, utc->tm_year + 1900,
                 utc->tm_mon + 1, utc->tm_mday, utc->tm_hour, utc->tm_min, utc->tm_sec);
    }
    playback_record(path);
}
