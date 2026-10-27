/* The Developer menu (menu.h), the last on the bar: Log to file, Logging,
 * the engine's own diagnostic switches, Engine switches, which change what
 * the engine does, to compare one way with the other, and Experimental, which
 * changes what the game does, for a comparison.
 *
 * Log to file is logging= in the settings file, on when it is not there. Off,
 * the game's output -- what the launchers keep as last-run.log -- goes to the
 * null device instead; on again, back to the log. The log file itself is left
 * as it is. It changes at once.
 *
 * The switches are environment variables that the engine reads once, at
 * start or at first use (RECOMP_GL_PROFILE and the rest), so each applies
 * from the next start -- File > Restart's, or the next launch -- and its row
 * says so. They are kept in developer.cfg, beside the settings file, as the
 * value each is to have, NAME=1 or NAME=0 (1 for one whose value is a word),
 * and the ones that are ticked go
 * into the environment at the start of the run -- unless it already has them:
 * the environment wins, as it does over the settings file. */
#define _POSIX_C_SOURCE 200112L    /* setenv, unsetenv */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void cpuprof_start(void);
void cpuprof_stop(void);

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: the settings file, and a file of ours beside it. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
extern int  nv_window_file_get(const char *file, const char *key, int def);
extern void nv_window_file_set(const char *file, const char *key, int val);
#define DEV_FILE "developer.cfg"

/* The switches: Developer > Logging's, each of which only adds lines to the
 * log, then Developer > Engine switches', each of which changes what the
 * engine does, then Developer > Experimental's, each of which changes what
 * the game does. A switch is ticked to give its variable the value on: 1, or 0
 * for one that turns off something the engine does by default, or the word
 * the engine reads for it (RECOMP_GL_CURTAIN's skip). Its row is the variable
 * as ticking sets it -- NAME, NAME=0, or NAME=skip -- but Experimental's,
 * which have rows of their own; and with nothing ticked the engine does what
 * it always does. The CPU profile is Linux's and Windows' only for now
 * (port/cpuprof.c).
 * A RECOMP_GL_ switch that never reads converted vertices back goes on the
 * vertex cache's list too (vc_debug_switch in nv2a_gl.c), or setting it turns
 * the cache off as well. */
typedef struct { const char *name; const char *on; } DevSwitch;
static const DevSwitch k_logging[] = {
    { "RECOMP_GL_PROFILE", "1" },
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(_WIN32)
    { "RECOMP_CPU_PROFILE", "1" },
#endif
    { "RECOMP_LOCK_WATCH", "1" }, { "RECOMP_WAITLOG", "1" }, { "RECOMP_KERNEL_IO", "1" },
    { "RECOMP_APU_STATS", "1" }, { "RECOMP_GL_UNHANDLED", "1" }, { "RECOMP_GL_VERBOSE", "1" },
    { "RECOMP_GL_ERRORS_PER_DRAW", "1" }, { "RECOMP_USB_TRACE", "1" },
    { "JSRF_CAM_LOG", "1" },                /* the game's camera (port/recomp_manual.c) */
};
static const DevSwitch k_engine[] = {
    { "RECOMP_GL_NOREVZ", "1" },            /* no reversed depth */
    { "RECOMP_GL_NOPRECZ", "1" },           /* no precise fixed-function depth */
    { "RECOMP_GL_TITLE_MIPS", "0" },        /* a texture's base level alone, not its mipmaps */
    { "RECOMP_GL_TEX_ONESAMPLER", "1" },    /* one texture however it is sampled */
    { "RECOMP_GL_TEXGEN", "0" },            /* no texture coordinates the T&L unit makes */
    { "RECOMP_GL_VCACHE", "0" },            /* no vertex cache */
    { "RECOMP_GL_IMMEDIATE", "0" },         /* no batches sent a vertex at a time (the shadows) */
    { "RECOMP_GL_STENCILVIS", "1" },        /* the stencil buffer in place of the shadows */
    { "RECOMP_GL_CURTAIN", "skip" },        /* no fades to black (the full-screen black quad) */
    { "RECOMP_NV2A_QUEUE", "0" },           /* the game no more than a kick ahead of the GPU thread */
};
/* Experimental's: none just now -- the last, the GPU queue, is the engine's
 * way since b1.2, with its switch above. A new one goes before the end, with
 * its row in k_tries_row, and Experimental shows again. */
static const DevSwitch k_tries[] = {
    { NULL, NULL },                         /* the end */
};
#define DEV_LOGGING  ((int)(sizeof k_logging / sizeof k_logging[0]))
#define DEV_ENGINE   ((int)(sizeof k_engine / sizeof k_engine[0]))
#define DEV_TRIES    ((int)(sizeof k_tries / sizeof k_tries[0]) - 1)
#define DEV_NAMED    (DEV_LOGGING + DEV_ENGINE)     /* the switches whose rows are their names */
#define DEV_SWITCHES (DEV_NAMED + DEV_TRIES)
_Static_assert(DEV_SWITCHES <= 31, "a bit of an int for each switch");

/* What developer.cfg keeps for a switch ticked: 0 for one set to 0, and 1 for
 * the rest, whose value is 1 or a word. */
static int dev_cfg_on(const char *on) { return strcmp(on, "0") != 0; }

/* Switch i: Logging's first, then Engine switches', then Experimental's, as
 * their bits are. */
static const DevSwitch *dev_switch(int i)
{
    if (i < DEV_LOGGING) return &k_logging[i];
    i -= DEV_LOGGING;
    return i < DEV_ENGINE ? &k_engine[i] : &k_tries[i - DEV_ENGINE];
}

#if defined(XBOXRECOMP_MENU)
#define DEV_ROW_MAX 64          /* a row: the name, =0 and " (Restart)" */
static char        s_row_text[DEV_NAMED][DEV_ROW_MAX];
static const char *s_row[DEV_NAMED];
/* Experimental's rows, one for each of k_tries. */
static const char *const k_tries_row[] = {
    NULL,                                   /* the end */
};
_Static_assert(sizeof k_tries_row / sizeof k_tries_row[0] == DEV_TRIES + 1,
               "a row for each experiment");
#endif

static int s_on;        /* a bit for each switch ticked in developer.cfg */
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
static void switch_flip(int i)
{
    const DevSwitch *sw = dev_switch(i);
    int on = dev_cfg_on(sw->on), bare = !strcmp(sw->on, "1");
    int ticked;
    s_on ^= 1 << i;
    ticked = (s_on >> i) & 1;
    nv_window_file_set(DEV_FILE, sw->name, ticked ? on : !on);
    fprintf(stderr, "[DEV] %s%s%s %s from the next start\n", sw->name, bare ? "" : "=",
            bare ? "" : sw->on, ticked ? "on" : "off");
}
/* A page's ticks, from its first switch's bit. A TOGGLES row's state and pick
 * take no context, so each page has its own pair. */
static int  page_on(int first, int n) { return (s_on >> first) & ((1 << n) - 1); }
static int  logging_on(void)    { return page_on(0, DEV_LOGGING); }
static void logging_flip(int i) { switch_flip(i); }
static int  engine_on(void)     { return page_on(DEV_LOGGING, DEV_ENGINE); }
static void engine_flip(int i)  { switch_flip(DEV_LOGGING + i); }
static int  tries_on(void)      { return page_on(DEV_NAMED, DEV_TRIES); }
static void tries_flip(int i)   { switch_flip(DEV_NAMED + i); }
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
        const DevSwitch *sw = dev_switch(i);
        int on = dev_cfg_on(sw->on);
        /* Ticked when developer.cfg gives it its value on (any but 0, for 1). */
        if ((nv_window_file_get(DEV_FILE, sw->name, !on) != 0) != on) continue;
        s_on |= 1 << i;
        if (getenv(sw->name)) continue;
        set_env(sw->name, sw->on);
        s_ours |= 1 << i;
        fprintf(stderr, "[DEV] %s=%s from " DEV_FILE "\n", sw->name, sw->on);
    }
#if defined(XBOXRECOMP_MENU)
    for (i = 0; i < DEV_NAMED; i++) {
        const DevSwitch *sw = dev_switch(i);
        int bare = !strcmp(sw->on, "1");
        snprintf(s_row_text[i], DEV_ROW_MAX, "%s%s%s (Restart)", sw->name, bare ? "" : "=",
                 bare ? "" : sw->on);
        s_row[i] = s_row_text[i];
    }
    {
        const XBOX_MENU_ITEM rows[] = {
            { .menu = XBOX_MENU_DEVELOPER, .kind = XBOX_ITEM_TOGGLE, .label = "Log to file",
              .state = log_on, .run = log_flip },
            { .menu = XBOX_MENU_DEVELOPER, .kind = XBOX_ITEM_TOGGLES, .label = "Logging",
              .state = logging_on, .pick = logging_flip, .choices = s_row, .nchoices = DEV_LOGGING },
            { .menu = XBOX_MENU_DEVELOPER, .kind = XBOX_ITEM_TOGGLES, .label = "Engine switches",
              .state = engine_on, .pick = engine_flip, .choices = s_row + DEV_LOGGING,
              .nchoices = DEV_ENGINE },
            { .menu = XBOX_MENU_DEVELOPER, .kind = XBOX_ITEM_TOGGLES, .label = "Experimental",
              .state = tries_on, .pick = tries_flip, .choices = k_tries_row,
              .nchoices = DEV_TRIES },
        };
        /* Experimental, the last, only while it has a row. */
        xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]) - !DEV_TRIES);
    }
#endif
    if (nv_window_pref_num("logging", &v) > 0 && v == 0) {
        s_log = 0;
        fprintf(stderr, "[DEV] Log to file off (logging=0 in the settings): nothing more is written here\n");
        log_output(0);
    }
#endif
    /* The CPU profile is the one switch no engine code reads: it starts here,
     * before the game does (port/cpuprof.c). */
    cpuprof_start();
}

/* Before the game starts itself again: what this run put in the environment
 * comes out, and the log comes back, so the next run starts from developer.cfg
 * and the settings file afresh -- and with the log to write to. */
void developer_restart(void);
void developer_restart(void)
{
    cpuprof_stop();
#if defined(NV2A_GL_USE_SDL)
    int i;
    for (i = 0; i < DEV_SWITCHES; i++)
        if (s_ours & (1 << i)) set_env(dev_switch(i)->name, "");
    log_output(1);
#endif
}
