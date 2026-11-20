/* Run with: python tests/input_latency/run.py
 *
 * The input latency log without the game: src/input/input_press.c and
 * src/nv2a_gl/nv2a_latency.c as they are. The game's reads, the GPU
 * thread's flips, hand-overs and swaps are played here in turn, with their
 * times and addresses, and the lines the log writes are kept and checked.
 * "on" and "off": RECOMP_INPUT_LATENCY as run.py sets it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_press.h"
#include "nv2a_latency.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
                                  fprintf(stderr, "said:\n%s", s_said); exit(1); } } while (0)
#define FULL 32767

/* What the log said, since the last hear(). */
static char s_said[1 << 15];
static void keep(const char *line)
{
    size_t n = strlen(s_said);
    snprintf(s_said + n, sizeof s_said - n, "%s\n", line);
}
static int said(const char *text) { return strstr(s_said, text) != NULL; }
static int lines(void)
{
    int n = 0;
    const char *p;
    for (p = s_said; *p; p++) n += *p == '\n';
    return n;
}
static void hear(void) { s_said[0] = 0; }

static Nv2aLat L;
static void fresh(const char *title)
{
    printf("%s\n", title);
    memset(&L, 0, sizeof L);
    L.say = keep;
    hear();
}

static void presses(void)
{
    unsigned char an[8] = { 0 };
    short ax[4] = { 0, 0, 0, 0 };
    printf("the pad's controls\n");
    CHECK(input_press_pad(0, an, ax) == 0);
    CHECK(input_press_pad(0x0010, an, ax) == 1u << 4);           /* Start */
    CHECK(input_press_pad(0xFF00, an, ax) == 0);                 /* no control there */
    an[0] = 127;
    CHECK(input_press_pad(0, an, ax) == 0);                      /* A short of half way */
    an[0] = 128;
    CHECK(input_press_pad(0, an, ax) == 1u << 8);                /* A half way */
    an[0] = 0;
    an[6] = 128;
    CHECK(input_press_pad(0, an, ax) == 1u << 14);               /* the left trigger */
    an[6] = 0;
    ax[0] = 16383;
    CHECK(input_press_pad(0, an, ax) == 0);
    ax[0] = 16384;
    CHECK(input_press_pad(0, an, ax) == 1u << INPUT_PRESS_LSTICK);
    ax[0] = 11585; ax[1] = 11585;                                /* round the circle: just short */
    CHECK(input_press_pad(0, an, ax) == 0);
    ax[0] = 11586; ax[1] = -11586;
    CHECK(input_press_pad(0, an, ax) == 1u << INPUT_PRESS_LSTICK);
    ax[0] = ax[1] = 0;
    ax[2] = -32768;
    CHECK(input_press_pad(0, an, ax) == 1u << INPUT_PRESS_RSTICK);
    CHECK(!strcmp(input_press_pad_name(8), "A"));
    CHECK(!strcmp(input_press_pad_name(0), "d-pad up"));
    CHECK(!strcmp(input_press_pad_name(INPUT_PRESS_LSTICK), "left stick"));
    CHECK(!strcmp(input_press_pad_name(INPUT_PRESS_PAD), "?"));
    printf("  ok\n");
}

/* A frame from its flip to its swap: the flip at `at`, handed over 10 ms on,
 * swapped 0.5 ms after that, the next refresh 3 ms after that. */
static double frame(uint32_t at, double t, uint64_t serial)
{
    nv2a_lat_flip(&L, at, t);
    nv2a_lat_handed(&L, serial, t + 0.010);
    nv2a_lat_swapped(&L, serial, t + 0.0105, t + 0.0135);
    return t + 1.0 / 60.0;
}

static void frames(void)
{
    double t;

    fresh("a press, to the frame that shows it");
    nv2a_lat_flip(&L, 0x1000, 1.000);
    CHECK(said("[LATENCY] on:"));
    nv2a_lat_read(&L, "A", 0, 10, 1.005, 0.0, 1.005, 0x1100);
    nv2a_lat_flip(&L, 0x1080, 1.010);           /* written before the read: not its frame */
    nv2a_lat_handed(&L, 1, 1.012);
    nv2a_lat_swapped(&L, 1, 1.013, 0.0);
    nv2a_lat_flip(&L, 0x1200, 1.020);           /* past the read */
    CHECK(lines() == 1);
    nv2a_lat_handed(&L, 2, 1.030);
    nv2a_lat_swapped(&L, 2, 1.031, 1.0333);
    nv2a_lat_flip(&L, 0x1300, 1.040);
    CHECK(said("[LATENCY] pad A, read 10: frame reached +15.0 ms, handed over +25.0, "
               "swapped +26.0, next refresh +28.3; 28.3 ms in all, from the read\n"));
    CHECK(lines() == 2 && L.nlive == 0);
    printf("  ok\n");

    fresh("a flip with nothing drawn: the press waits for the next frame shown");
    nv2a_lat_flip(&L, 0x2000, 2.000);
    nv2a_lat_read(&L, "B", 0, 20, 2.001, 0.0, 2.001, 0x2010);
    nv2a_lat_flip(&L, 0x2100, 2.010);           /* past it, but nothing drawn: no hand-over */
    nv2a_lat_flip(&L, 0x2200, 2.026);
    nv2a_lat_handed(&L, 9, 2.036);
    nv2a_lat_swapped(&L, 9, 2.037, 0.0);
    nv2a_lat_flip(&L, 0x2300, 2.043);
    CHECK(said("[LATENCY] pad B, read 20: frame reached +25.0 ms, handed over +35.0, "
               "swapped +36.0; 36.0 ms in all, from the read\n"));
    printf("  ok\n");

    fresh("round the ring's end");
    nv2a_lat_flip(&L, 0x7F000, 3.000);
    nv2a_lat_read(&L, "X", 0, 30, 3.001, 0.0, 3.001, 0x00100);   /* the title has wrapped */
    nv2a_lat_read(&L, "Y", 0, 30, 3.001, 0.0, 3.001, 0x7F800);   /* before the ring's end */
    nv2a_lat_read(&L, "Black", 0, 30, 3.001, 0.0, 3.001, 0x00500);   /* after the next flip */
    t = frame(0x00400, 3.010, 1);
    t = frame(0x00600, t, 2);
    nv2a_lat_flip(&L, 0x00700, t);
    CHECK(said("[LATENCY] pad X, read 30: frame reached +9.0 ms"));
    CHECK(said("[LATENCY] pad Y, read 30: frame reached +9.0 ms"));
    CHECK(said("[LATENCY] pad Black, read 30: frame reached +25.7 ms"));
    printf("  ok\n");

    fresh("a frame the presenter replaced: the newer frame's swap");
    nv2a_lat_flip(&L, 0x3000, 4.000);
    nv2a_lat_read(&L, "White", 0, 40, 4.002, 0.0, 4.002, 0x3010);
    nv2a_lat_flip(&L, 0x3100, 4.010);
    nv2a_lat_handed(&L, 7, 4.012);
    nv2a_lat_flip(&L, 0x3200, 4.014);
    nv2a_lat_handed(&L, 8, 4.016);
    nv2a_lat_swapped(&L, 8, 4.020, 0.0);        /* 7 never shown */
    nv2a_lat_flip(&L, 0x3300, 4.030);
    CHECK(said("[LATENCY] pad White, read 40: frame reached +8.0 ms, handed over +10.0, "
               "swapped +18.0 (a newer frame: its own was replaced before it was shown); "
               "18.0 ms in all, from the read\n"));
    printf("  ok\n");

    fresh("a key: from its pump, and the pump before it");
    nv2a_lat_flip(&L, 0x4000, 5.000);
    nv2a_lat_read(&L, "W", 1, 50, 4.996, 4.980, 5.000, 0x4010);
    frame(0x4100, 5.010, 1);
    nv2a_lat_flip(&L, 0x4200, 5.030);
    CHECK(said("[LATENCY] key W, read 50: pumped 4.0 ms before the read (came in after the pump "
               "20.0 ms before it); frame reached +10.0 ms, handed over +20.0, swapped +20.5, "
               "next refresh +23.5; 27.5 ms in all, from the pump\n"));
    printf("  ok\n");

    fresh("a press no frame shows: given up after 2 s, and counted");
    nv2a_lat_flip(&L, 0x5000, 6.000);
    nv2a_lat_read(&L, "Start", 0, 60, 6.001, 0.0, 6.001, 0x9000);    /* the flips never reach it */
    for (t = 6.010; t < 8.1; t += 1.0 / 60.0) nv2a_lat_flip(&L, 0x5100, t);
    CHECK(said("[LATENCY] pad Start, read 60: not shown in 2 s (no flip past its read)\n"));
    CHECK(L.nlive == 0 && L.lost == 1);
    printf("  ok\n");

    fresh("the summary, every 10 s: each stage's median and worst");
    t = frame(0x6000, 10.000, 1);
    nv2a_lat_read(&L, "A", 0, 70, t, 0.0, t, 0x6010);
    t = frame(0x6100, t + 0.010, 2);             /* reached 10 ms after the read */
    nv2a_lat_read(&L, "A", 0, 71, t, 0.0, t, 0x6110);
    t = frame(0x6200, t + 0.030, 3);             /* 30 */
    nv2a_lat_read(&L, "A", 0, 72, t, 0.0, t, 0x6210);
    t = frame(0x6300, t + 0.020, 4);             /* 20 */
    nv2a_lat_read(&L, "Return", 1, 73, t - 0.008, 0.0, t, 0x6310);
    t = frame(0x6400, t + 0.005, 5);
    nv2a_lat_read(&L, "B", 0, 74, t, 0.0, t, 0xA000);     /* never shown */
    while (t < 20.5) t = frame(0x6500, t, 6);
    CHECK(said("[LATENCY] the last 10 s, pad: 3 presses; median (worst), ms: the read to the "
               "frame 20.0 (30.0), the frame to its hand-over 10.0 (10.0), the hand-over to the "
               "swap 0.5 (0.5), the swap to the next refresh 3.0 (3.0), in all 33.5 (43.5)\n"));
    CHECK(said("[LATENCY] the last 10 s, keys: 1 press; median (worst), ms: the pump to the read "
               "8.0 (8.0), the read to the frame 5.0 (5.0)"));
    CHECK(said("[LATENCY] the last 10 s: 1 press not shown in 2 s, 0 not kept"));
    hear();
    while (t < 31.0) t = frame(0x6500, t, 6);
    CHECK(!said("[LATENCY]"));                    /* nothing to say: nothing said */
    printf("  ok\n");

    fresh("more presses than the GPU thread has taken: the rest not kept, and counted");
    nv2a_lat_flip(&L, 0x7000, 40.000);
    {
        int i;
        for (i = 0; i < NV2A_LAT_QUEUE + 3; i++) nv2a_lat_read(&L, "A", 0, 80 + i, 40.001, 0.0, 40.001, 0x7010);
    }
    t = frame(0x7100, 40.010, 1);
    while (t < 52.0) t = frame(0x7200, t, 2);
    CHECK(said("[LATENCY] the last 10 s, pad: 64 presses"));
    CHECK(said("3 not kept (0 with the GPU thread's list full, 3 with the game's)"));
    printf("  ok\n");
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "on")) {
        CHECK(nv2a_lat_on());
        presses();
        frames();
    } else if (!strcmp(c, "off")) {
        CHECK(!nv2a_lat_on());
    } else {
        fprintf(stderr, "a case: on or off\n");
        return 2;
    }
    printf("input_latency %s: pass\n", c);
    return 0;
}
