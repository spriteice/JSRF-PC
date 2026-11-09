/* The setup's steps, shared by its window and its console (setup_core.c). */
#ifndef JSRF_SETUP_JOB_H
#define JSRF_SETUP_JOB_H

#include <stdio.h>
#include "setup.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct js_download_item {
    const char *name, *url, *sha256;
    uint64_t size;   /* 0: not known ahead */
} js_download_item;

/* How the worker reports: frac_from and frac_to are fractions of the whole
 * (-1 for no change); the bar moves from one towards the other over about
 * secs seconds. text is for the player, log for the log (either NULL). */
typedef void (*js_update_fn)(void *ctx, double frac_from, double frac_to, int secs,
                             const char *text, const char *log);

/* Where a step reports, and the flag that stops it (the window's close). */
typedef struct js_progress {
    js_update_fn update;
    volatile int *cancel;
    void *ctx;
} js_progress;

typedef struct js_job {
    jsrf_setup *s;
    int download;                 /* the tools first, from their sites */
    struct {
        js_update_fn update;
        volatile int *cancel;
        void *ctx;
    } prog;
    /* out */
    char err[2048];
    char log_path[4096];
    char engine_sha[65], engine_check[16], gen_sha[65], gen_check[16];
    /* the worker's own */
    FILE *log;
    double base;
} js_job;

int js_job_run(js_job *j);                       /* 0 when the engine was built */
void js_accept_engine(const char *root, const char *sha);
int js_delete_tools(const char *root);
uint64_t js_tools_size(const char *root);
int js_tools_ready(const char *root);
void js_engine_path(const char *root, char *out, size_t n);
void js_path(char *out, size_t n, const char *a, const char *b);
void js_kill_child(void);
FILE *js_fopen(const char *path, const char *mode);
const js_download_item *js_downloads(size_t *n);
/* One file, checked by its SHA-256 and moved to dest; a failed try is tried
 * again a while later, up to four tries in all, and closing the window
 * (p->cancel) stops the wait at once. 0 when it's there; else err says why,
 * "cancelled" when it was stopped. */
int js_download(const char *url, const char *dest, const char *sha256, uint64_t size,
                js_progress *p, double frac0, double frac1, char *err, size_t errlen);
const char *js_tools_id(void);

#ifdef __cplusplus
}
#endif

#endif
