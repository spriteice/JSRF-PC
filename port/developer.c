/* The Developer menu (menu.h), the last on the bar: Log to file, and
 * Logging, the engine's own diagnostic switches.
 *
 * Log to file is logging= in the settings file, on when it is not there. Off,
 * the game's output -- what the launchers keep as last-run.log -- goes to the
 * null device instead; on again, back to the log. The log file itself is left
 * as it is. It changes at once.
 *
 * The switches are environment variables that the engine reads once, at
 * start or at first use (RECOMP_GL_PROFILE and the rest), so each applies
 * from the next start, and its row says so. They are kept in developer.cfg,
 * beside the settings file, as NAME=1 or NAME=0, and the ones that are on go
 * into the environment at the start of the run -- unless it already has them:
 * the environment wins, as it does over the settings file. */
#define _POSIX_C_SOURCE 200112L    /* setenv, unsetenv */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#if defined(_WIN32)
#  include <io.h>
#  define DEV_NULL "NUL"
#else
#  include <unistd.h>
#  define DEV_NULL "/dev/null"
#endif
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: the settings file, and a file of ours beside it. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
extern int  nv_window_file_get(const char *file, const char *key, int def);
extern void nv_window_file_set(const char *file, const char *key, int val);
#define DEV_FILE "developer.cfg"

/* The switches, and their rows in Developer > Logging: each only adds lines
 * to the log. */
static const char *const k_switch[] = {
    "RECOMP_GL_PROFILE", "RECOMP_LOCK_WATCH", "RECOMP_WAITLOG", "RECOMP_KERNEL_IO",
    "RECOMP_APU_STATS", "RECOMP_GL_UNHANDLED", "RECOMP_GL_VERBOSE", "RECOMP_USB_TRACE",
};
static const char *const k_switch_row[] = {
    "GPU time profile (Restart)", "Guest lock (Restart)", "Waits and DPCs (Restart)",
    "File reads and events (Restart)", "Audio levels (Restart)",
    "Unhandled GPU methods (Restart)", "Renderer details (Restart)", "Controller USB (Restart)",
};
#define DEV_SWITCHES ((int)(sizeof k_switch / sizeof k_switch[0]))
_Static_assert(sizeof k_switch == sizeof k_switch_row, "a row for each switch");
_Static_assert(DEV_SWITCHES <= 31, "a bit of an int for each switch");

static int s_on;        /* a bit for each switch on in developer.cfg */
static int s_ours;      /* ... and for each this run put in the environment */
static int s_log = 1;
static int s_out = -1, s_err = -1;     /* the log, kept while it is not written to */

static void set_env(const char *name, const char *val)
{
#if defined(_WIN32)
    _putenv_s(name, val);               /* "" takes it out */
#else
    if (*val) setenv(name, val, 1);
    else      unsetenv(name);
#endif
}

/* Standard output and error to the null device, or back to the log. */
static void log_output(int on)
{
    int null;
    fflush(stdout);
    fflush(stderr);
    if (!on && s_err < 0) {
        if ((null = open(DEV_NULL, O_WRONLY)) < 0) return;
        s_out = dup(1);
        s_err = dup(2);
        dup2(null, 1);
        dup2(null, 2);
        close(null);
    } else if (on && s_err >= 0) {
        dup2(s_out, 1);
        dup2(s_err, 2);
        close(s_out);
        close(s_err);
        s_out = s_err = -1;
    }
}

#if defined(XBOXRECOMP_MENU)
static int  log_on(void)       { return s_log; }
static void log_flip(void)
{
    s_log = !s_log;
    nv_window_pref_set_num("logging", s_log);
    if (!s_log) fprintf(stderr, "[DEV] Log to file off: nothing more is written here until it is on again\n");
    log_output(s_log);
    if (s_log) fprintf(stderr, "[DEV] Log to file on again\n");
}
static int  switches_on(void)  { return s_on; }
static void switch_flip(int i)
{
    s_on ^= 1 << i;
    nv_window_file_set(DEV_FILE, k_switch[i], (s_on >> i) & 1);
    fprintf(stderr, "[DEV] %s %s from the next start\n", k_switch[i], (s_on >> i) & 1 ? "on" : "off");
}
#endif
#endif

/* At the start of the run, before anything reads the switches. */
void developer_start(void);
void developer_start(void)
{
#if defined(NV2A_GL_USE_SDL)
    double v;
    int i;
    for (i = 0; i < DEV_SWITCHES; i++) {
        if (!nv_window_file_get(DEV_FILE, k_switch[i], 0)) continue;
        s_on |= 1 << i;
        if (getenv(k_switch[i])) continue;
        set_env(k_switch[i], "1");
        s_ours |= 1 << i;
        fprintf(stderr, "[DEV] %s=1 from " DEV_FILE "\n", k_switch[i]);
    }
#if defined(XBOXRECOMP_MENU)
    {
        const XBOX_MENU_ITEM rows[] = {
            { .menu = XBOX_MENU_DEVELOPER, .kind = XBOX_ITEM_TOGGLE, .label = "Log to file",
              .state = log_on, .run = log_flip },
            { .menu = XBOX_MENU_DEVELOPER, .kind = XBOX_ITEM_TOGGLES, .label = "Logging",
              .state = switches_on, .pick = switch_flip, .choices = k_switch_row, .nchoices = DEV_SWITCHES },
        };
        xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
    }
#endif
    if (nv_window_pref_num("logging", &v) > 0 && v == 0) {
        s_log = 0;
        fprintf(stderr, "[DEV] Log to file off (logging=0 in the settings): nothing more is written here\n");
        log_output(0);
    }
#endif
}

/* Before the game starts itself again: what this run put in the environment
 * comes out, and the log comes back, so the next run starts from developer.cfg
 * and the settings file afresh -- and with the log to write to. */
void developer_restart(void);
void developer_restart(void)
{
#if defined(NV2A_GL_USE_SDL)
    int i;
    for (i = 0; i < DEV_SWITCHES; i++)
        if (s_ours & (1 << i)) set_env(k_switch[i], "");
    log_output(1);
#endif
}
