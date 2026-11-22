/* What the parts of src/nv2a_gl/nv2a_gl.c that run.py cuts out need around
 * them: the renderer's counters, the window, the input latency log, the box
 * and the shader cache, as stand-ins. The backend and the clock are in
 * test_main.c, after the cut code. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"

typedef unsigned int GLuint;

/* The renderer's counters the cut code reads and writes: the game's presents
 * and the frames put in the window. */
static struct { uint32_t flips, shows; } S;
static const Nv2aGfx *G;

static double mono_now(void);
extern int  xbox_guest_lock_drop(void)   __attribute__((weak));
extern void xbox_guest_lock_retake(int)  __attribute__((weak));

/* The window: a 60 Hz display, and a count of the frames it was given. */
typedef struct { int refresh_rate; } SDL_DisplayMode;
static void *g_window;
static int g_presented;
static int SDL_GetWindowDisplayMode(void *window, SDL_DisplayMode *mode)
{
    (void)window;
    mode->refresh_rate = 60;
    return 0;
}
static int sdl_present(GLuint fbo, int w, int h, float shape)
{
    (void)fbo; (void)w; (void)h; (void)shape;
    g_presented++;
    return 1;
}
static void sdl_pump(void) {}

/* The input latency log, off. */
static int g_lat;
static int nv2a_lat_on(void) { return 0; }
static void nv2a_lat_handed(int *lat, uint64_t serial, double t)
{
    (void)lat; (void)serial; (void)t;
}
static void nv2a_lat_swapped(int *lat, uint64_t serial, double end, double next)
{
    (void)lat; (void)serial; (void)end; (void)next;
}
static double sdl_next_refresh(double t) { (void)t; return 0.0; }

/* The box: what it was last told. */
static int g_box_up, g_box_done, g_box_total;
static void xbox_MenuBusy(const char *what, int done, int total)
{
    g_box_up = what != NULL;
    g_box_done = done;
    g_box_total = total;
}

/* The shader cache, open, with nothing of another engine's to remove. */
static int nv2a_sc_is_open(void) { return 1; }
static int nv2a_sc_sweep(void) { return 0; }
static unsigned nv2a_gfx_gl_fbo(uint32_t rt) { return rt; }
