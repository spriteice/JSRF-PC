/*
 * The input latency log (RECOMP_INPUT_LATENCY): for each press on the first
 * pad or the keyboard, how long it took to reach the screen, stage by stage,
 * a line each, and every 10 s each stage's median and worst.
 *
 * The press is the input layer's: what counts as one is
 * src/input/input_press.h, seen at the game's read of the first pad, and
 * handed here by nv_window_latency_press (nv2a_gl.c). For a key, when SDL had
 * it is the window's pump that took it from the system, and the pump before
 * that bounds when it came in; a pad's state is read afresh at the game's
 * read (xbox_InputGetState), so for a pad that is the read.
 *
 * The frame is the renderer's. A press is noted with where the title had sent
 * its pushbuffer to at the read (DMA_PUT, xbox_Nv2aDmaPut): D3D sends its
 * commands on at each flip, so a flip further on in the ring was written
 * after the read, and the first one the GPU thread runs past that point ends
 * the first frame that can show the press. Each flip is placed by the address
 * of its command (nv2a_pb_scan_at), and a press by the flips either side of
 * its point, going round the ring: no flip is a whole ring after the one
 * before it. A flip with nothing drawn shows nothing new, so the press waits
 * for the next frame shown, and its time is that one's. Then: when that
 * frame was handed over (frame_pace done), when the swap that put it up
 * ended (or, when the presenter replaced it before it could, the newer
 * one's that did), and on Linux the display's next refresh after that
 * (nv2a_dclock_next): the soonest it can be on screen. The compositor's
 * work and the panel's come after that, and only a camera can time them.
 *
 * No SDL, GL or kernel in it, for tests/input_latency: the times (seconds,
 * on CLOCK_MONOTONIC as mono_now reads it) and addresses are handed in.
 * Threads: nv2a_lat_read on the game's thread, nv2a_lat_swapped on the one
 * that swaps (the presenter's, or the GPU thread), the rest on the GPU
 * thread, which also writes the lines. A press reaches the GPU thread before
 * any flip past its point does: the title writes DMA_PUT after it noted the
 * press, and x86 keeps two writes in their order for another thread.
 */
#ifndef NV2A_LATENCY_H
#define NV2A_LATENCY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NV2A_LAT_QUEUE  64      /* presses read, not yet taken by the GPU thread */
#define NV2A_LAT_LIVE   64      /* presses waiting for their frame or its swap */
#define NV2A_LAT_SWAPS  16      /* swaps ended, not yet taken by the GPU thread */
#define NV2A_LAT_KEEP   256     /* a stage's times kept for the 10 s median */
#define NV2A_LAT_LOST   2.0     /* seconds: a press no frame has shown by then is given up */
#define NV2A_LAT_REPORT 10.0    /* seconds between the summaries */

typedef struct {
    const char *what;           /* the control or key, named */
    int      key;               /* 1: a key; 0: the pad */
    unsigned pass;              /* the game's read that saw it (the pad's packet number) */
    double   sdl;               /* when SDL had it: a key's pump, the pad's read */
    double   before;            /* a key: the pump before, after which it came in; else 0 */
    double   read;              /* when the game read it */
    uint32_t put;               /* the title's DMA_PUT at the read, physical */
    /* the GPU thread's, from here */
    int      state;
    double   reached;           /* the GPU thread reached the flip of the frame that shows it */
    double   handed;            /* that frame handed over */
    double   swapped;           /* the swap that put it, or a newer one, up ended */
    double   refresh;           /* the display's next refresh after that; 0 when not known */
    uint64_t serial;            /* the frame handed over */
    uint64_t shown;             /* the frame the swap put up */
} Nv2aLatPress;

typedef struct {
    uint64_t serial;
    double   at, refresh;
} Nv2aLatSwap;

/* A stage's times in the last 10 s, in milliseconds. */
typedef struct {
    float    v[NV2A_LAT_KEEP];
    unsigned n, kept;
    float    worst;
} Nv2aLatStage;

/* The stages a press's line and the summary give. */
enum {
    NV2A_LAT_PUMP,              /* a key: its pump to the read */
    NV2A_LAT_FRAME,             /* the read to the flip of the frame that shows it */
    NV2A_LAT_PACE,              /* that flip to the frame's hand-over */
    NV2A_LAT_SWAP,              /* the hand-over to the swap's end */
    NV2A_LAT_SHOW,              /* the swap's end to the next refresh (Linux) */
    NV2A_LAT_ALL,               /* SDL having it to the next refresh, or to the swap's end */
    NV2A_LAT_STAGES
};

typedef struct {
    void (*say)(const char *line);      /* where the lines go; stderr when 0 */
    /* the game's thread puts presses here, the GPU thread takes them */
    Nv2aLatPress queue[NV2A_LAT_QUEUE];
    unsigned     q_in, q_out, q_full;
    /* the swapping thread puts swaps here, the GPU thread takes them */
    Nv2aLatSwap  swaps[NV2A_LAT_SWAPS];
    unsigned     s_in, s_out, s_full;
    /* the GPU thread's */
    Nv2aLatPress live[NV2A_LAT_LIVE];
    int          nlive, flipped, told;
    uint32_t     flip_at;               /* the last flip's address */
    double       report_at;
    Nv2aLatStage st[2][NV2A_LAT_STAGES];    /* the pad's, the keys' */
    unsigned     presses[2], lost, dropped, q_full_seen, s_full_seen;
} Nv2aLat;

/* RECOMP_INPUT_LATENCY set (and not 0). */
int nv2a_lat_on(void);

/* The game's thread, at its read of the first pad: a press, as
 * Nv2aLatPress's fields say. what lasts as long as the program. */
void nv2a_lat_read(Nv2aLat *L, const char *what, int key, unsigned pass,
                   double sdl, double before, double read, uint32_t put);

/* The GPU thread, at each flip, drawn or not: the address of its command
 * (physical, as DMA_PUT), and when it was reached. Takes the presses read
 * and the swaps ended since the last, writes the lines of those finished,
 * and the summary when it is due. */
void nv2a_lat_flip(Nv2aLat *L, uint32_t at, double t);

/* The GPU thread, as a frame is handed over to be swapped: serial counts the
 * frames handed over (the presenter's, or the GPU thread's own swaps). */
void nv2a_lat_handed(Nv2aLat *L, uint64_t serial, double t);

/* The thread that swaps, as a swap ends: the frame it put up, when, and the
 * display's next refresh after that (0 when not known). */
void nv2a_lat_swapped(Nv2aLat *L, uint64_t serial, double at, double refresh);

#ifdef __cplusplus
}
#endif

#endif
