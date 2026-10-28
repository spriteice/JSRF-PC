/* Run with: python tests/display_clock/run.py
 *
 * nv2a_display_clock.c with the kernel played here: the cards there are, the
 * displays on them, how far apart they really refresh and what their modes
 * say, on a clock of the test's own. And frame_pace's part that paces by it,
 * as it is in nv2a_gl.c (run.py puts it in pace_step). A game takes 2 to 9 ms
 * of each frame, then waits for its deadline and hands the frame over; the
 * card finishes it 1.1 to 2.4 ms later (4.3 for one in fifty), or 0.3 to 0.9
 * ms later when its commands were sent before the wait; a compositor shows
 * the newest finished frame at each refresh, taking it 1.5 ms before.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define SIM_DISPLAYS 4
#define SIM_FD       100            /* card n is SIM_FD + n */
#define SIM_FRAMES   20000
#define LATCH        0.0015         /* the compositor takes a frame this long before a refresh */
#define VRR_SLOWEST  (1.0 / 48.0)   /* a display with VRR on refreshes at least this often */
#define PROP_GAMMA   20             /* the display properties the kernel has */
#define PROP_CTM     21
#define PROP_VRR     22

typedef struct {
    int      card;
    uint32_t crtc;
    int      mode;                  /* a mode set */
    int      active;                /* as GET_SEQUENCE says */
    int      stopped;               /* its refreshes no longer counted */
    double   stop_t;
    double   mode_hz;               /* what its mode says */
    double   period;                /* how far apart it really refreshes */
    double   base_t;                /* a refresh, and its number */
    uint64_t base_k;
    int      vrr, vrr_prop, seq_errno;
} sim_display;

typedef struct { double handed, ready; int paced; } sim_frame;

static struct {
    double      t;
    int         n;
    sim_display d[SIM_DISPLAYS];
    unsigned    cards;              /* a bit for each card there is */
    int         opens, closes, drops, stall_next, seq_asked;
    sim_frame   f[SIM_FRAMES];
    int         frames, shown;      /* frames handed over; shown so far with VRR on */
    double      vrr_last;           /* with VRR on: its last refresh, and its number */
    uint64_t    vrr_k;
} K;

/* With VRR on, the display refreshes when a new frame is finished (and taken),
 * but not sooner than its mode's refresh after the last, nor later than
 * VRR_SLOWEST after it. */
static void vrr_advance(const sim_display *s, double t)
{
    for (;;) {
        double next = K.vrr_last + VRR_SLOWEST;
        if (K.shown < K.frames) {
            double r = K.f[K.shown].ready + LATCH;
            if (r < K.vrr_last + s->period) r = K.vrr_last + s->period;
            if (r < next) next = r;
        }
        if (next > t) return;
        while (K.shown < K.frames && K.f[K.shown].ready + LATCH <= next) K.shown++;
        K.vrr_last = next;
        K.vrr_k++;
    }
}

static void last_refresh(const sim_display *s, double t, uint64_t *k, double *tk)
{
    if (s->vrr) {
        vrr_advance(s, t);
        *k = K.vrr_k;
        *tk = K.vrr_last;
        return;
    }
    if (s->stopped) t = s->stop_t;
    *k = s->base_k + (uint64_t)floor((t - s->base_t) / s->period);
    *tk = s->base_t + (double)(*k - s->base_k) * s->period;
}

static sim_display *display(int card, uint32_t crtc)
{
    int i;
    for (i = 0; i < K.n; i++)
        if (K.d[i].card == card && K.d[i].crtc == crtc) return &K.d[i];
    return NULL;
}

static int fake_open(const char *path, int flags, ...)
{
    int card;
    (void)flags;
    if (sscanf(path, "/dev/dri/card%d", &card) != 1 || card < 0 || card > 31
        || !(K.cards & (1u << card))) {
        errno = ENOENT;
        return -1;
    }
    K.opens++;
    return SIM_FD + card;
}

static int fake_close(int fd)
{
    (void)fd;
    K.closes++;
    return 0;
}

static int fake_clock_gettime(clockid_t clock, struct timespec *ts)
{
    (void)clock;
    ts->tv_sec = (time_t)K.t;
    ts->tv_nsec = (long)((K.t - (double)ts->tv_sec) * 1e9);
    return 0;
}

static int fake_ioctl(int fd, unsigned long request, ...)
{
    int card = fd - SIM_FD, i;
    void *arg;
    va_list ap;
    if (request == DRM_IOCTL_DROP_MASTER) {
        K.drops++;
        errno = EINVAL;                 /* the compositor has the card */
        return -1;
    }
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    switch (request) {
    case DRM_IOCTL_MODE_GETRESOURCES: {
        struct drm_mode_card_res *r = arg;
        uint32_t n = 0;
        for (i = 0; i < K.n; i++) {
            if (K.d[i].card != card) continue;
            if (n < r->count_crtcs) ((uint32_t *)(uintptr_t)r->crtc_id_ptr)[n] = K.d[i].crtc;
            n++;
        }
        r->count_crtcs = n;
        return 0;
    }
    case DRM_IOCTL_MODE_GETCRTC: {
        struct drm_mode_crtc *c = arg;
        sim_display *s = display(card, c->crtc_id);
        if (!s) { errno = ENOENT; return -1; }
        c->mode_valid = (uint32_t)s->mode;
        if (s->mode) {
            c->mode.htotal = 2000;
            c->mode.vtotal = 1000;
            c->mode.clock = (uint32_t)llround(s->mode_hz * 2000.0);   /* kHz */
        }
        return 0;
    }
    case DRM_IOCTL_MODE_OBJ_GETPROPERTIES: {
        struct drm_mode_obj_get_properties *q = arg;
        sim_display *s = display(card, q->obj_id);
        uint32_t ids[3] = { PROP_GAMMA, PROP_CTM, PROP_VRR }, n;
        uint64_t values[3] = { 0, 0, 0 };
        if (!s || q->obj_type != DRM_MODE_OBJECT_CRTC) { errno = ENOENT; return -1; }
        values[2] = (uint64_t)s->vrr;
        n = s->vrr_prop ? 3 : 2;
        for (i = 0; i < (int)n && i < (int)q->count_props; i++) {
            ((uint32_t *)(uintptr_t)q->props_ptr)[i] = ids[i];
            ((uint64_t *)(uintptr_t)q->prop_values_ptr)[i] = values[i];
        }
        q->count_props = n;
        return 0;
    }
    case DRM_IOCTL_MODE_GETPROPERTY: {
        struct drm_mode_get_property *p = arg;
        const char *name = p->prop_id == PROP_GAMMA ? "GAMMA_LUT" : p->prop_id == PROP_CTM ? "CTM"
                         : p->prop_id == PROP_VRR ? "VRR_ENABLED" : NULL;
        if (!name) { errno = ENOENT; return -1; }
        memset(p->name, 0, sizeof p->name);
        strcpy(p->name, name);
        return 0;
    }
    case DRM_IOCTL_CRTC_GET_SEQUENCE: {
        struct drm_crtc_get_sequence *q = arg;
        sim_display *s = display(card, q->crtc_id);
        uint64_t k;
        double tk;
        K.seq_asked++;
        if (!s) { errno = ENOENT; return -1; }
        if (s->seq_errno) { errno = s->seq_errno; return -1; }
        if (K.stall_next) { K.stall_next--; K.t += 0.03; }
        last_refresh(s, K.t, &k, &tk);
        q->active = (uint32_t)s->active;
        q->sequence = k;
        q->sequence_ns = (int64_t)llround(tk * 1e9);
        return 0;
    }
    }
    errno = ENOTTY;
    return -1;
}

#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define clock_gettime fake_clock_gettime
#include "nv2a_display_clock.c"
#undef open
#undef close
#undef ioctl
#undef clock_gettime

/* frame_pace's part that sets the deadline, from nv2a_gl.c; the presenter's
 * lock played here, off unless a case turns it on. */
static double stub_lp, stub_ref, stub_refresh;
static double pres_lock(double base, double *ref, double *refresh)
{
    (void)base;
    *ref = stub_ref;
    *refresh = stub_refresh;
    return stub_lp;
}

static double deadline;

static int pace_step(double period, double now)
{
    int by_display = 0;
    {
/* FRAME_PACE_LOCK */
    }
    return by_display;
}

/* ---- the game ---- */

static uint64_t rng = 88172645463325252ull;
static double urand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (double)(rng >> 11) * (1.0 / 9007199254740992.0);
}

/* Frames until the clock reads until, as frame_pace waits for them. */
static void play(double until)
{
    const double period = 1.0 / 60.0;
    while (K.t < until && K.frames < SIM_FRAMES) {
        double now, gpu;
        int by;
        K.t += 0.002 + 0.007 * urand();
        now = K.t;
        if (deadline == 0.0 || deadline < now - 1.0) deadline = now;
        by = pace_step(period, now);
        if (now > deadline + period * 4.0) deadline = now + period;
        else if (K.t < deadline) K.t = deadline + 0.00003 * urand();
        nv2a_dclock_handed_over(K.t);
        gpu = by ? 0.0003 + 0.0006 * urand() : urand() < 0.02 ? 0.0043 : 0.0011 + 0.0013 * urand();
        K.f[K.frames].handed = K.t;
        K.f[K.frames].ready = K.t + gpu;
        K.f[K.frames].paced = by;
        K.frames++;
    }
}

/* Of the frames handed over from a to b, how many stayed on a display of a
 * fixed rate 0, 1, 2, 3, 4 and more refreshes; and how many were paced by
 * it, and the furthest any was handed over from the point aimed at. */
typedef struct { int stays[5], frames, paced; double aim_off; } verdict;

static verdict judge(const sim_display *s, double a, double b, double at)
{
    static int on_screen[SIM_FRAMES];
    verdict v;
    double tk;
    int j, p = -1;
    memset(&v, 0, sizeof v);
    memset(on_screen, 0, sizeof on_screen);
    tk = s->base_t + ceil((a - 0.1 - s->base_t) / s->period) * s->period;
    for (; tk < b + 0.1; tk += s->period) {
        double latch = tk - LATCH;
        while (p + 1 < K.frames && K.f[p + 1].handed <= latch) p++;
        for (j = p; j >= 0 && K.f[j].ready > latch; j--) { }
        if (j >= 0) on_screen[j]++;
    }
    for (j = 0; j < K.frames; j++) {
        double x, off;
        if (K.f[j].handed < a || K.f[j].handed > b) continue;
        v.frames++;
        v.stays[on_screen[j] < 4 ? on_screen[j] : 4]++;
        if (K.f[j].paced) v.paced++;
        x = (K.f[j].handed - s->base_t) / s->period;
        off = x - floor(x) - at;
        off -= floor(off + 0.5);
        if (fabs(off) * s->period > v.aim_off) v.aim_off = fabs(off) * s->period;
    }
    return v;
}

/* ---- what the module says ---- */

static FILE *cap;
static int cap_fd = -1;
static char said_text[1 << 16];

static void start_listening(void)
{
    fflush(stderr);
    cap = tmpfile();
    cap_fd = dup(2);
    dup2(fileno(cap), 2);
}

/* What it said since start_listening(), and shown in the test's output. */
static const char *heard(void)
{
    size_t n;
    char *line;
    fflush(stderr);
    dup2(cap_fd, 2);
    close(cap_fd);
    rewind(cap);
    n = fread(said_text, 1, sizeof said_text - 1, cap);
    said_text[n] = 0;
    fclose(cap);
    for (line = said_text; *line; ) {
        char *e = strchr(line, '\n');
        int len = e ? (int)(e - line) : (int)strlen(line);
        printf("    | %.*s\n", len, line);
        line += len + (e ? 1 : 0);
    }
    return said_text;
}

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) fails++;
    printf("  %s: %s\n", ok ? "ok" : "FAIL", what);
}

static void show(const char *what, verdict v)
{
    printf("  %s: %d frames, %d paced by the display; on screen 0 refreshes %d, 1 %d, 2 %d, "
           "3 %d, more %d; handed over at most %.3f ms from the point aimed at\n", what,
           v.frames, v.paced, v.stays[0], v.stays[1], v.stays[2], v.stays[3], v.stays[4],
           v.aim_off * 1000.0);
}

/* ---- the cases ---- */

static sim_display *add(int card, uint32_t crtc, double hz)
{
    sim_display *s = &K.d[K.n++];
    memset(s, 0, sizeof *s);
    s->card = card;
    s->crtc = crtc;
    s->mode = s->active = 1;
    s->mode_hz = hz;
    s->period = 1.0 / hz;
    s->base_t = K.t - 0.0031;       /* a refresh 3.1 ms ago */
    s->base_k = 5000;
    s->vrr_prop = 1;
    K.cards |= 1u << card;
    return s;
}

static void fresh(const char *title, const char *pace_display, const char *phase)
{
    printf("%s\n", title);
    memset(&K, 0, sizeof K);
    if (g_dc.fd >= 0) K.closes = -1;    /* nv2a_dclock_open closes the last case's */
    K.t = 1000.0;
    deadline = 0.0;
    stub_lp = 0.0;
    if (pace_display) setenv("RECOMP_PACE_DISPLAY", pace_display, 1);
    else unsetenv("RECOMP_PACE_DISPLAY");
    if (phase) setenv("RECOMP_PACE_PHASE", phase, 1);
    else unsetenv("RECOMP_PACE_PHASE");
}

/* A display's rate changed at the clock's time, its mode with it. */
static void set_rate(sim_display *s, double hz)
{
    uint64_t k;
    double tk;
    last_refresh(s, K.t, &k, &tk);
    s->base_k = k;
    s->base_t = tk;
    s->period = 1.0 / hz;
    s->mode_hz = hz;
}

static void vrr_switch(sim_display *s, int on)
{
    uint64_t k;
    double tk;
    last_refresh(s, K.t, &k, &tk);
    if (on) {
        K.vrr_last = tk;
        K.vrr_k = k;
        K.shown = K.frames;
    } else {
        s->base_k = k;
        s->base_t = tk;
    }
    s->vrr = on;
}

/* When the frames were first paced by the display from t on: the clock's
 * time, 0 if never. */
static double paced_from(double t)
{
    int j;
    for (j = 0; j < K.frames; j++)
        if (K.f[j].handed >= t && K.f[j].paced) return K.f[j].handed;
    return 0.0;
}

static int paced_between(double a, double b)
{
    int j, n = 0;
    for (j = 0; j < K.frames; j++)
        if (K.f[j].handed >= a && K.f[j].handed <= b && K.f[j].paced) n++;
    return n;
}

int main(void)
{
    sim_display *s;
    verdict v, game;
    const char *said;
    double t0, t;
    int i;

    /* Not opened: the presenter paces on Windows, or with RECOMP_GL_PRESENTER. */
    printf("never opened\n");
    {
        double ref = 0, refresh = 0, at = 0;
        start_listening();
        nv2a_dclock_report();
        said = heard();
        check(nv2a_dclock_lock(1.0 / 60.0, 1.0, &ref, &refresh, &at) == 0.0 && !*said,
              "it does not pace, ask the kernel or report");
    }

    fresh("one display, 119.95 Hz (an Ally X's panel); SDL says 120", NULL, NULL);
    s = add(1, 51, 119.95);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(120);
    play(t0 + 30.0);
    i = (int)g_dc.slice[1];
    nv2a_dclock_report();
    said = heard();
    check(i >= 0.99 * (K.frames - 60), "frames handed over in the second eighth of a refresh, 99% "
          "of those after the first second");
    check(strstr(said, "counting the display's refreshes from the kernel: /dev/dri/card1, CRTC 51, "
                       "119.950 Hz by its mode (SDL says 120)\n") != NULL, "it counts card1's display");
    t = paced_from(t0);
    printf("  paced by the display from %.2f s\n", t - t0);
    check(t > t0 + 1.0 && t < t0 + 1.6, "from a second and a half at the latest");
    check(fabs(1.0 / g_dc.period - 119.95) < 0.0005, "its rate measured to within 0.0005 Hz");
    v = judge(s, t0 + 3.0, t0 + 29.5, 0.15);
    show("from 3 s", v);
    check(v.paced == v.frames && v.stays[2] == v.frames,
          "every frame paced by it and on screen exactly 2 refreshes");
    check(v.aim_off < 0.0001, "each handed over within 0.1 ms of 15% into a refresh");
    check(strstr(said, "[PACE] the display: 119.950 Hz by the kernel's count") != NULL
          && strstr(said, "paced by it, 1.25 ms into a refresh") != NULL, "and it says so");
    check(K.drops == K.opens && K.opens == K.closes + 1,
          "every card opened handed straight back, one kept open");
    printf("  the kernel asked %d times in 30 s\n", K.seq_asked);
    check(K.seq_asked >= 100 && K.seq_asked <= 125, "about four times a second, at the frame after each quarter");

    fresh("the same with RECOMP_PACE_DISPLAY=0: the game's clock, as before", "0", NULL);
    s = add(1, 51, 119.95);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(120);
    play(t0 + 60.0);
    nv2a_dclock_report();
    said = heard();
    game = judge(s, t0 + 3.0, t0 + 59.5, 0.15);
    show("from 3 s", game);
    check(game.paced == 0, "no frame paced by the display");
    check(game.stays[1] + game.stays[3] > 20,
          "frames on screen 1 refresh or 3 where the hand-over drifts past the compositor's");
    check(strstr(said, "measured only (RECOMP_PACE_DISPLAY=0)") != NULL
          && strstr(said, "by the game's clock (RECOMP_PACE_DISPLAY=0)") != NULL, "and it says so");

    fresh("RECOMP_PACE_PHASE=50: half a refresh in", NULL, "50");
    s = add(1, 51, 119.95);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(120);
    play(t0 + 20.0);
    nv2a_dclock_report();
    said = heard();
    v = judge(s, t0 + 3.0, t0 + 19.5, 0.5);
    show("from 3 s", v);
    check(v.paced == v.frames && v.aim_off < 0.0001, "each handed over within 0.1 ms of half a refresh in");
    check(strstr(said, "4.17 ms into a refresh") != NULL, "and it says so");

    fresh("a 59.94 Hz display: every frame one refresh", NULL, NULL);
    s = add(0, 40, 59.94);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(60);
    play(t0 + 20.0);
    heard();
    v = judge(s, t0 + 3.0, t0 + 19.5, 0.15);
    show("from 3 s", v);
    check(v.paced == v.frames && v.stays[1] == v.frames, "every frame paced by it and on screen once");

    fresh("a 144 Hz display: no whole number of refreshes is a 60th of a second", NULL, NULL);
    s = add(0, 40, 144.0);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(144);
    play(t0 + 10.0);
    nv2a_dclock_report();
    said = heard();
    check(K.frames > 500 && paced_between(t0, K.t) == 0, "no frame paced by it");
    check(strstr(said, "by the game's clock (no whole number of the display's refreshes (144.00 Hz) "
                       "is the game's frame)") != NULL
          && strstr(said, "no whole number of its refreshes is the game's frame, which the game's "
                          "clock paces") != NULL
          && strstr(said, "paced by it") == NULL, "and it says why");

    fresh("VRR on, the kernel saying so", NULL, NULL);
    s = add(1, 51, 120.0);
    vrr_switch(s, 1);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(120);
    play(t0 + 10.0);
    nv2a_dclock_report();
    said = heard();
    check(K.frames > 500 && paced_between(t0, K.t) == 0 && g_dc.state == DC_COUNTING,
          "no frame paced by it");
    check(strstr(said, "frames paced by the game's clock (its refresh rate follows the frames (VRR))")
          != NULL, "and it says why");

    fresh("VRR on, with no VRR_ENABLED from the kernel", NULL, NULL);
    s = add(1, 51, 120.0);
    s->vrr_prop = 0;
    vrr_switch(s, 1);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(120);
    play(t0 + 10.0);
    nv2a_dclock_report();
    said = heard();
    check(strstr(said, "the kernel has no setting that says whether its rate follows the frames")
          != NULL, "it says it cannot tell");
    check(K.frames > 500 && paced_between(t0, K.t) == 0, "no frame paced by it: its counts do not "
          "give its mode's rate");

    fresh("VRR turned on after 10 s, off again after 20", NULL, NULL);
    s = add(1, 51, 119.95);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(120);
    play(t0 + 10.0);
    vrr_switch(s, 1);
    play(t0 + 20.0);
    vrr_switch(s, 0);
    play(t0 + 30.0);
    said = heard();
    check(paced_between(t0 + 3.0, t0 + 10.0) > 400, "paced by the display before");
    check(paced_between(t0 + 10.3, t0 + 20.0) == 0, "not within a quarter of a second of VRR on");
    t = paced_from(t0 + 20.0);
    check(t > 0.0 && t < t0 + 21.6, "and by it again within a second and a half of VRR off");
    check(strstr(said, "the display's refreshes: its refresh rate follows the frames (VRR); frames are "
                       "paced by the game's clock until the counts agree again") != NULL, "it says so");
    v = judge(s, t0 + 23.0, t0 + 29.5, 0.15);
    show("from 23 s", v);
    check(v.paced == v.frames && v.stays[2] == v.frames, "every frame 2 refreshes again");

    fresh("asking the kernel held a frame 30 ms, after 5 s; twice more after 10 s", NULL, NULL);
    s = add(1, 51, 119.95);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(120);
    play(t0 + 5.0);
    K.stall_next = 1;
    play(t0 + 10.0);
    check(g_dc.state == DC_KEEPING && paced_from(t0 + 7.0) > 0.0, "once: counted again, and paced again");
    K.stall_next = 2;
    play(t0 + 15.0);
    i = K.seq_asked;
    play(t0 + 20.0);
    nv2a_dclock_report();
    said = heard();
    check(strstr(said, "asking the kernel held a frame 30 ms; frames are paced by the game's clock") != NULL,
          "it says so the first time");
    check(g_dc.state == DC_OFF && i == K.seq_asked && paced_between(t0 + 15.0, t0 + 20.0) == 0,
          "three times: the kernel is not asked again, and the game's clock paces");
    check(strstr(said, "held a frame 30 ms, 3 times now, so it is not asked again") != NULL,
          "and it says why");
    check(strstr(said, "[PACE] the display: 119.950 Hz by the kernel's count") != NULL,
          "the report gives the rate it last kept");

    fresh("the kernel does not count the display's refreshes", NULL, NULL);
    s = add(0, 40, 60.0);
    s->seq_errno = EOPNOTSUPP;
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(60);
    play(t0 + 3.0);
    i = K.seq_asked;
    play(t0 + 6.0);
    nv2a_dclock_report();
    said = heard();
    check(i == 1 && K.seq_asked == 1 && g_dc.state == DC_OFF, "asked once, then not again");
    check(strstr(said, "the kernel does not say when this display refreshes (Operation not supported)")
          != NULL, "and it says why");

    fresh("no card it may open", NULL, NULL);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(60);
    play(t0 + 3.0);
    nv2a_dclock_report();
    said = heard();
    check(paced_between(t0, K.t) == 0 && K.seq_asked == 0, "the game's clock paces");
    check(strstr(said, "frames are paced by the game's clock: no display the kernel lets this program "
                       "read (/dev/dri/card*)") != NULL, "and it says why");

    fresh("a 60 Hz display on card0 and a 120 Hz one on card1, SDL saying 120", NULL, NULL);
    add(0, 40, 60.0);
    add(1, 51, 120.0);
    start_listening();
    nv2a_dclock_open(120);
    heard();
    check(g_dc.card == 1 && g_dc.crtc == 51 && K.opens == K.closes + 1, "card1's is counted");

    fresh("the same, SDL saying nothing", NULL, NULL);
    add(0, 40, 60.0);
    add(1, 51, 120.0);
    start_listening();
    nv2a_dclock_open(0);
    said = heard();
    check(g_dc.fd < 0 && K.opens == K.closes && strstr(said, "2 displays on, and SDL does not say the "
          "window's rate") != NULL, "neither, and it says why");

    fresh("the same, SDL saying 75", NULL, NULL);
    add(0, 40, 60.0);
    add(1, 51, 120.0);
    start_listening();
    nv2a_dclock_open(75);
    said = heard();
    check(g_dc.fd < 0 && strstr(said, "2 displays on, none at the 75 Hz SDL reports") != NULL,
          "neither, and it says why");

    fresh("two 120 Hz displays on one card", NULL, NULL);
    add(0, 40, 120.0);
    add(0, 41, 120.0);
    start_listening();
    nv2a_dclock_open(120);
    said = heard();
    check(g_dc.fd < 0 && strstr(said, "2 displays on at the 120 Hz SDL reports, and nothing to say "
          "which is the window's") != NULL, "neither, and it says why");

    fresh("one display at 120 Hz, SDL saying 60 (a nested X server's own rate)", NULL, NULL);
    s = add(0, 40, 119.95);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(60);
    play(t0 + 10.0);
    heard();
    v = judge(s, t0 + 3.0, t0 + 9.5, 0.15);
    check(v.paced == v.frames && v.stays[2] == v.frames, "it is counted, and paces at 2 refreshes");

    fresh("a card with a display off and one on", NULL, NULL);
    s = add(0, 40, 60.0);
    s->mode = s->active = 0;
    add(0, 41, 119.95);
    start_listening();
    nv2a_dclock_open(144);
    heard();
    check(g_dc.crtc == 41, "the one on is counted");

    for (i = 0; i < 3; i++) {
        static const char *how[3] = {
            "the display turned off after 8 s, the kernel refusing to count, and on after 12",
            "the display turned off after 8 s, the kernel saying so, and on after 12",
            "the display's refreshes stop after 8 s, and start again after 12" };
        static const double gone[3] = { 0.3, 0.3, 0.6 };   /* the last is seen when the
                                                               * refresh it names is old */
        static const char *why[3] = {
            "the kernel no longer counts the display's refreshes",
            "the display is off", "the kernel names no recent refresh" };
        char line[200];
        fresh(how[i], NULL, NULL);
        s = add(1, 51, 119.95);
        t0 = K.t;
        start_listening();
        nv2a_dclock_open(120);
        play(t0 + 8.0);
        if (i == 0) s->seq_errno = EINVAL;
        if (i <= 1) s->mode = 0;
        if (i == 1) s->active = 0;
        if (i == 2) { s->stopped = 1; s->stop_t = K.t; }
        play(t0 + 12.0);
        s->seq_errno = 0;
        s->mode = s->active = 1;
        s->stopped = 0;
        s->base_t = K.t - 0.0057;
        play(t0 + 20.0);
        said = heard();
        check(paced_between(t0 + 8.0 + gone[i], t0 + 12.0) == 0, "not paced by it once it is seen");
        t = paced_from(t0 + 12.0);
        check(t > 0.0 && t < t0 + 13.6, "and by it again within a second and a half of its coming back");
        snprintf(line, sizeof line, "the display's refreshes: %s; frames are paced by the game's clock",
                 why[i]);
        check(strstr(said, line) != NULL, "it says why");
        v = judge(s, t0 + 15.0, t0 + 19.5, 0.15);
        check(v.paced == v.frames && v.stays[2] == v.frames, "every frame 2 refreshes again");
    }

    fresh("the display set from 120 Hz to 60 after 8 s", NULL, NULL);
    s = add(1, 51, 119.95);
    t0 = K.t;
    start_listening();
    nv2a_dclock_open(120);
    play(t0 + 8.0);
    set_rate(s, 59.94);
    play(t0 + 20.0);
    said = heard();
    check(paced_between(t0 + 8.3, t0 + 9.0) == 0, "not paced by it within a quarter of a second");
    t = paced_from(t0 + 9.0);
    check(t > 0.0 && t < t0 + 9.6 + 0.3, "and by it again within a second and a half");
    v = judge(s, t0 + 11.0, t0 + 19.5, 0.15);
    show("from 11 s", v);
    check(v.paced == v.frames && v.stays[1] == v.frames, "every frame 1 refresh, at 59.94 Hz");
    check(strstr(said, "the display refreshes at 59.940 Hz by the kernel's count") != NULL, "it says so");

    fresh("the presenter's lock (Windows): as it was, half a refresh in, and no flush", NULL, NULL);
    for (i = 0; i < 100000; i++) {
        double period = 1.0 / 60.0, now = 50.0 + urand(), d0, ph, old;
        stub_refresh = 1.0 / (60.0 + 100.0 * urand());
        stub_lp = stub_refresh * (1 + (int)(3 * urand()));
        stub_ref = now - stub_refresh * urand();
        d0 = deadline = now + 0.02 * (urand() - 0.5);
        /* frame_pace before this change */
        old = d0 + stub_lp;
        ph = fmod(old - stub_ref, stub_refresh);
        if (ph < 0.0) ph += stub_refresh;
        old -= (ph - 0.5 * stub_refresh) * 0.2;
        if (pace_step(period, now) != 0 || fabs(deadline - old) > 1e-9) break;
    }
    check(i == 100000, "100000 random frames, each deadline the same to a nanosecond");

    printf(fails ? "display_clock: %d case(s) FAIL\n" : "display_clock: all cases pass\n", fails);
    return fails ? 1 : 0;
}
