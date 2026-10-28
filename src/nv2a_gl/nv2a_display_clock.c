/*
 * The display's refreshes, as the kernel counts them, for frame pacing on
 * Linux.
 *
 * The game paces its frames on its own 60 Hz clock and hands each one over
 * with vsync off, and the compositor (KWin, or gamescope in Game Mode) shows
 * the newest finished frame at each of its refreshes: at 120 Hz, each frame
 * should stay two. But the game's clock and the display's are never exactly
 * in step, so the moment each frame is handed over drifts through the
 * refresh. Where it passes the point at which the compositor takes a frame,
 * the small jitter of each wait decides which refresh a frame lands in, and
 * frames stay one refresh, then three.
 *
 * The kernel counts the display's refreshes and knows when the last one
 * began, on the monotonic clock mono_now reads (DRM_IOCTL_CRTC_GET_SEQUENCE,
 * which any program that may open /dev/dri/card* may ask). The X server
 * can't say: Xwayland, which KWin and gamescope both run the game under,
 * counts a frame each time the compositor calls a window back, or on a timer
 * of its own, not the display's refreshes. Two counts far apart give the
 * refresh's length; frame_pace then paces at a whole number of refreshes
 * (when that is within 3% of the game's period, as it does with the presenter
 * on Windows) and hands each frame over at the same point in the refresh.
 *
 * That point is early in it, 15% by default. The compositor takes a frame a
 * millisecond or two before the next refresh, and only once the card has
 * finished it, which on the Ally X was 1.5 to 2.5 ms after hand-over on
 * average and 4.3 at worst: the frame's commands waited in the driver until
 * the swap. So frame_pace now sends them before it waits, and the card draws
 * the frame meanwhile; and early in the refresh leaves most of it to spare
 * either side. RECOMP_PACE_PHASE sets the point, in hundredths of a refresh.
 *
 * The display is the only one the kernel has on (any card), or of several,
 * the one at the rate SDL reports. Its counts are used only once they have
 * kept time for a second: each new one agreeing with the ones before it to a
 * quarter of a refresh, the refresh it names a recent one, and the rate they
 * give within 2% of the rate its mode gives. With a variable refresh rate on
 * (the kernel's VRR_ENABLED) the display follows the frames rather than
 * timing them, and there is nothing to keep in step with: the game paces on
 * its own clock as before, as it does when no whole number of refreshes is
 * its frame (144 Hz). When the counts stop agreeing -- the display set to
 * another rate, or turned off -- its mode is read again and they are counted
 * afresh. Asking can hold a frame while the compositor sets the display's
 * mode, which is a moment's work; once asking has held one over 20 ms three
 * times, the kernel is not asked again. RECOMP_PACE_DISPLAY=0 paces on the
 * game's clock always, and still reports.
 *
 * Every 10 s a line says where in the refresh the frames were handed over:
 * all in one eighth of it when paced by it, and spread through it when the
 * game's clock drifts.
 */
#include "nv2a_display_clock.h"

#if defined(NV2A_DISPLAY_CLOCK)

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <drm/drm.h>          /* the kernel's own headers (linux-libc-dev) */
#include <drm/drm_mode.h>

#define DC_PHASE     15     /* RECOMP_PACE_PHASE's default: hundredths of a refresh */
#define DC_PHASE_MOST 99    /* ... and the most it may be */
#define DC_CARDS     16     /* /dev/dri/card0 to card15 */
#define DC_CRTCS     16     /* displays a card can drive, the most looked at */
#define DC_DISPLAYS  16     /* displays on, the most told apart */
#define DC_PROPS     32     /* a display's properties, the most looked through */
#define DC_ASK_EVERY 0.25   /* seconds between counts */
#define DC_SETTLE    1.0    /* seconds of counts that agree before they are used */
#define DC_AGREE     0.25   /* of a refresh: how far a count may be from the others */
#define DC_RECENT    0.1    /* seconds: how long ago the refresh a count names may be */
#define DC_RATE      0.02   /* how far the counts' rate may be from the mode's, or a mode's from SDL's */
#define DC_STALL     0.02   /* seconds: a count that takes longer held its frame ... */
#define DC_STALLS    3      /* ... and after this many, the kernel is not asked again */
#define DC_LOCK      0.03   /* a whole number of refreshes within this of the period */
#define DC_ASTRAY    0.25   /* of a refresh: a paced frame handed over further off is counted */
#define DC_SLICES    8      /* the report's parts of a refresh */
#define DC_SAID      20     /* lines it says when the counting changes, at most */
#define NS           1e-9   /* seconds in a nanosecond */

enum { DC_OFF, DC_COUNTING, DC_KEEPING };

static struct {
    int      fd, card;
    uint32_t crtc, vrr;             /* the display, and its VRR_ENABLED property (0: none) */
    int      opened, state, hz, pace, said, stalls, remode, tell;
    double   mode_hz;               /* the rate its mode gives */
    double   at;                    /* where in a refresh frames are handed over */
    int64_t  ns0, ns;               /* the count this measurement started from, and the latest: */
    uint64_t seq0, seq;             /* when the refresh began, and its number */
    double   asked, period;         /* when it last counted; seconds a refresh, as last kept */
    int      paced;                 /* the frame being paced is paced by it */
    int      answered;              /* the kernel has answered a count */
    char     why[160];              /* why frames are paced on the game's clock */
    /* since the last report */
    unsigned long frames, paced_frames, off, slice[DC_SLICES], counts;
    double   off_sum, count_most;
} g_dc = { .fd = -1 };

typedef struct { int card; uint32_t crtc; double hz; } dc_display;

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * NS;
}

static void say(const char *what)
{
    if (g_dc.said++ < DC_SAID) {
        fprintf(stderr, "[WIN] %s\n", what);
        fflush(stderr);
    }
}

static int open_card(int card)
{
    char path[32];
    int fd;
    snprintf(path, sizeof path, "/dev/dri/card%d", card);
    if ((fd = open(path, O_RDWR | O_CLOEXEC)) < 0) return -1;
    /* The first to open a card while nothing has it to show (no compositor,
     * or one on another console) is given it, and one that comes back then
     * cannot have it. This gives it straight back; on a card the compositor
     * has, it is refused and changes nothing. */
    ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
    return fd;
}

/* The rate a display's mode gives, 0 when it has none (it is off). */
static double mode_rate(int fd, uint32_t crtc)
{
    struct drm_mode_crtc c;
    memset(&c, 0, sizeof c);
    c.crtc_id = crtc;
    if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &c) != 0 || !c.mode_valid
        || !c.mode.clock || !c.mode.htotal || !c.mode.vtotal)
        return 0.0;
    return c.mode.clock * 1000.0 / ((double)c.mode.htotal * c.mode.vtotal);
}

/* The displays the kernel has on: each card's CRTCs with a mode set. */
static int list_displays(dc_display *d)
{
    int card, n = 0;
    for (card = 0; card < DC_CARDS; card++) {
        struct drm_mode_card_res res;
        uint32_t ids[DC_CRTCS];
        unsigned i, m = 0;
        int fd = open_card(card);
        if (fd < 0) continue;
        memset(&res, 0, sizeof res);
        if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0 && res.count_crtcs) {
            m = res.count_crtcs < DC_CRTCS ? res.count_crtcs : DC_CRTCS;
            memset(&res, 0, sizeof res);
            res.count_crtcs = m;
            res.crtc_id_ptr = (uint64_t)(uintptr_t)ids;
            if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) m = 0;
            if (m > res.count_crtcs) m = res.count_crtcs;
        }
        for (i = 0; i < m && n < DC_DISPLAYS; i++) {
            double hz = mode_rate(fd, ids[i]);
            if (hz <= 0.0) continue;
            d[n].card = card;
            d[n].crtc = ids[i];
            d[n].hz = hz;
            n++;
        }
        close(fd);
    }
    return n;
}

/* A display's properties and their values; how many, 0 if the kernel would
 * not say. */
static unsigned crtc_props(int fd, uint32_t crtc, uint32_t *ids, uint64_t *values)
{
    struct drm_mode_obj_get_properties q;
    memset(&q, 0, sizeof q);
    q.obj_id = crtc;
    q.obj_type = DRM_MODE_OBJECT_CRTC;
    q.count_props = DC_PROPS;
    q.props_ptr = (uint64_t)(uintptr_t)ids;
    q.prop_values_ptr = (uint64_t)(uintptr_t)values;
    if (ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &q) != 0) return 0;
    return q.count_props < DC_PROPS ? q.count_props : DC_PROPS;
}

/* A display's property, by name: its id, 0 if it has none. */
static uint32_t find_prop(int fd, uint32_t crtc, const char *name)
{
    uint32_t ids[DC_PROPS];
    uint64_t values[DC_PROPS];
    unsigned i, n = crtc_props(fd, crtc, ids, values);
    for (i = 0; i < n; i++) {
        struct drm_mode_get_property p;
        memset(&p, 0, sizeof p);
        p.prop_id = ids[i];
        if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &p) == 0
            && strncmp(p.name, name, sizeof p.name) == 0)
            return ids[i];
    }
    return 0;
}

/* Whether the display's refresh rate follows the frames: 1 if it does, 0 if
 * not or the kernel has no such setting for it, -1 if it would not say. */
static int vrr_on(void)
{
    uint32_t ids[DC_PROPS];
    uint64_t values[DC_PROPS];
    unsigned i, n;
    if (!g_dc.vrr) return 0;
    n = crtc_props(g_dc.fd, g_dc.crtc, ids, values);
    for (i = 0; i < n; i++)
        if (ids[i] == g_dc.vrr) return values[i] != 0;
    return -1;
}

/* Measure again, from the next count, with the display's mode read again. */
static void count_again(const char *why)
{
    if (g_dc.state == DC_KEEPING) {
        char line[220];
        snprintf(line, sizeof line, "the display's refreshes: %s; frames are paced by the "
                 "game's clock until the counts agree again", why);
        say(line);
    }
    g_dc.state = DC_COUNTING;
    g_dc.ns0 = 0;
    g_dc.remode = 1;
    snprintf(g_dc.why, sizeof g_dc.why, "%s", why);
}

/* Ask the kernel for the display's last refresh, and see whether the counts
 * keep time. */
static void count(void)
{
    struct drm_crtc_get_sequence q;
    double t0 = now_s(), took, last;
    int ok, err, vrr = 0;

    memset(&q, 0, sizeof q);
    q.crtc_id = g_dc.crtc;
    if (g_dc.remode) g_dc.mode_hz = mode_rate(g_dc.fd, g_dc.crtc);
    ok = ioctl(g_dc.fd, DRM_IOCTL_CRTC_GET_SEQUENCE, &q) == 0;
    err = errno;
    if (ok) vrr = vrr_on();
    took = now_s() - t0;
    g_dc.asked = t0;
    g_dc.counts++;
    if (took > g_dc.count_most) g_dc.count_most = took;
    if (took > DC_STALL) {
        char why[220];
        if (++g_dc.stalls >= DC_STALLS) {
            snprintf(g_dc.why, sizeof g_dc.why, "asking the kernel held a frame %.0f ms, %d "
                     "times now, so it is not asked again", took * 1000.0, g_dc.stalls);
            snprintf(why, sizeof why, "the display's refreshes: %s", g_dc.why);
            say(why);
            g_dc.state = DC_OFF;
            return;
        }
        snprintf(why, sizeof why, "asking the kernel held a frame %.0f ms", took * 1000.0);
        if (g_dc.state != DC_KEEPING) say(why);
        count_again(why);
        return;
    }
    if (!ok && !g_dc.answered) {
        snprintf(g_dc.why, sizeof g_dc.why, "the kernel does not say when this display "
                 "refreshes (%s)", strerror(err));
        g_dc.state = DC_OFF;
        return;
    }
    g_dc.answered = 1;
    if (!ok || !q.active || g_dc.mode_hz <= 0.0) {
        count_again(ok ? "the display is off" : "the kernel no longer counts the display's refreshes");
        return;
    }
    if (vrr) {
        count_again(vrr > 0 ? "its refresh rate follows the frames (VRR)"
                            : "the kernel no longer says whether its refresh rate follows the frames");
        return;
    }
    g_dc.remode = 0;
    last = (double)q.sequence_ns * NS;
    if (q.sequence_ns <= 0 || last > t0 + took || last < t0 - DC_RECENT) {
        count_again("the kernel names no recent refresh");
        return;
    }
    if (!g_dc.ns0) {                            /* the first count */
        g_dc.ns0 = g_dc.ns = q.sequence_ns;
        g_dc.seq0 = g_dc.seq = q.sequence;
        return;
    }
    if (q.sequence < g_dc.seq || q.sequence_ns < g_dc.ns
        || (q.sequence == g_dc.seq) != (q.sequence_ns == g_dc.ns)) {
        count_again("the kernel's count went back");
        return;
    }
    if (q.sequence == g_dc.seq) return;         /* the same refresh as last time */
    if (g_dc.seq > g_dc.seq0) {
        /* Against the rate the counts so far give. */
        double p = (double)(g_dc.ns - g_dc.ns0) * NS / (double)(g_dc.seq - g_dc.seq0);
        if (fabs((double)(q.sequence_ns - g_dc.ns) * NS - (double)(q.sequence - g_dc.seq) * p)
            > DC_AGREE * p) {
            count_again("the kernel's counts stopped agreeing");
            return;
        }
    }
    g_dc.ns = q.sequence_ns;
    g_dc.seq = q.sequence;
    {
        double p = (double)(g_dc.ns - g_dc.ns0) * NS / (double)(g_dc.seq - g_dc.seq0);
        if (g_dc.state == DC_KEEPING) {
            g_dc.period = p;                    /* from ever further apart */
        } else if ((double)(g_dc.ns - g_dc.ns0) * NS >= DC_SETTLE) {
            if (fabs(1.0 / p - g_dc.mode_hz) > g_dc.mode_hz * DC_RATE) {
                char why[160];
                snprintf(why, sizeof why, "the display refreshes %.1f times a second, and its "
                         "mode says %.1f", 1.0 / p, g_dc.mode_hz);
                count_again(why);
            } else {
                g_dc.state = DC_KEEPING;
                g_dc.period = p;
                g_dc.tell = 1;
            }
        }
    }
}

/* The display to count: the only one on, or the only one at SDL's rate. */
static void find_display(void)
{
    dc_display d[DC_DISPLAYS];
    int n = list_displays(d), i, pick = -1, at_sdl = 0;
    for (i = 0; i < n; i++)
        if (g_dc.hz > 0 && fabs(d[i].hz - g_dc.hz) <= g_dc.hz * DC_RATE) {
            at_sdl++;
            pick = i;
        }
    if (n == 1) pick = 0;
    else if (at_sdl != 1) pick = -1;
    if (!n)
        snprintf(g_dc.why, sizeof g_dc.why, "no display the kernel lets this program read "
                 "(/dev/dri/card*)");
    else if (pick < 0 && at_sdl)
        snprintf(g_dc.why, sizeof g_dc.why, "%d displays on at the %d Hz SDL reports, and "
                 "nothing to say which is the window's", at_sdl, g_dc.hz);
    else if (pick < 0 && g_dc.hz > 0)
        snprintf(g_dc.why, sizeof g_dc.why, "%d displays on, none at the %d Hz SDL reports",
                 n, g_dc.hz);
    else if (pick < 0)
        snprintf(g_dc.why, sizeof g_dc.why, "%d displays on, and SDL does not say the "
                 "window's rate", n);
    else if ((g_dc.fd = open_card(d[pick].card)) < 0)
        snprintf(g_dc.why, sizeof g_dc.why, "/dev/dri/card%d could not be opened again (%s)",
                 d[pick].card, strerror(errno));
    if (g_dc.fd < 0) return;
    g_dc.card = d[pick].card;
    g_dc.crtc = d[pick].crtc;
    g_dc.mode_hz = d[pick].hz;
    g_dc.vrr = find_prop(g_dc.fd, g_dc.crtc, "VRR_ENABLED");
}

void nv2a_dclock_open(int hz)
{
    const char *e = getenv("RECOMP_PACE_DISPLAY"), *phase = getenv("RECOMP_PACE_PHASE");
    if (g_dc.fd >= 0) close(g_dc.fd);
    memset(&g_dc, 0, sizeof g_dc);
    g_dc.fd = -1;
    g_dc.opened = 1;
    g_dc.hz = hz;
    g_dc.pace = !(e && e[0] == '0');
    g_dc.at = phase && *phase ? atof(phase) : DC_PHASE;
    if (g_dc.at < 0.0) g_dc.at = 0.0;
    if (g_dc.at > DC_PHASE_MOST) g_dc.at = DC_PHASE_MOST;
    g_dc.at /= 100.0;                           /* hundredths */
    find_display();
    if (g_dc.fd < 0) {
        fprintf(stderr, "[WIN] frames are paced by the game's clock: %s\n", g_dc.why);
        return;
    }
    g_dc.state = DC_COUNTING;
    snprintf(g_dc.why, sizeof g_dc.why, "%s", "still counting the display's refreshes");
    fprintf(stderr, "[WIN] counting the display's refreshes from the kernel: /dev/dri/card%d, "
            "CRTC %u, %.3f Hz by its mode (SDL says %d)%s\n", g_dc.card, g_dc.crtc,
            g_dc.mode_hz, hz, g_dc.vrr ? "" : "; the kernel has no setting that says whether "
            "its rate follows the frames");
}

double nv2a_dclock_lock(double base, double now, double *ref, double *refresh, double *at)
{
    double n, last;
    int fits;
    g_dc.paced = 0;
    if (g_dc.state == DC_OFF || base <= 0.0) return 0.0;
    if (now - g_dc.asked >= DC_ASK_EVERY) count();
    if (g_dc.state != DC_KEEPING) return 0.0;
    n = floor(base / g_dc.period + 0.5);
    fits = n >= 1.0 && fabs(n * g_dc.period - base) <= base * DC_LOCK;
    if (g_dc.tell) {                            /* the counts kept time from here */
        char line[200];
        g_dc.tell = 0;
        if (!g_dc.pace)
            snprintf(g_dc.why, sizeof g_dc.why, "%s", "RECOMP_PACE_DISPLAY=0");
        else if (!fits)
            snprintf(g_dc.why, sizeof g_dc.why, "no whole number of the display's refreshes "
                     "(%.2f Hz) is the game's frame", 1.0 / g_dc.period);
        snprintf(line, sizeof line, "the display refreshes at %.3f Hz by the kernel's count: %s",
                 1.0 / g_dc.period, g_dc.pace && fits ? "frames are paced by it" : g_dc.pace
                 ? "no whole number of its refreshes is the game's frame, which the game's clock "
                   "paces" : "measured only (RECOMP_PACE_DISPLAY=0)");
        say(line);
    }
    if (!g_dc.pace || !fits) return 0.0;
    last = (double)g_dc.ns * NS;
    *ref = last + floor((now - last) / g_dc.period) * g_dc.period;
    *refresh = g_dc.period;
    *at = g_dc.at;
    g_dc.paced = 1;
    return n * g_dc.period;
}

void nv2a_dclock_handed_over(double t)
{
    double x, off;
    int paced = g_dc.paced;
    g_dc.paced = 0;
    if (g_dc.state != DC_KEEPING) return;
    /* In refreshes since the last one counted: which part of a refresh, and
     * how far from the point frames are handed over at. */
    x = (t - (double)g_dc.ns * NS) / g_dc.period;
    g_dc.slice[(int)((x - floor(x)) * DC_SLICES) % DC_SLICES]++;
    off = x - g_dc.at;
    off -= floor(off + 0.5);
    g_dc.frames++;
    if (paced) {
        g_dc.paced_frames++;
        g_dc.off_sum += fabs(off) * g_dc.period;
        if (fabs(off) > DC_ASTRAY) g_dc.off++;
    }
}

void nv2a_dclock_report(void)
{
    if (!g_dc.opened) return;
    if (g_dc.frames) {
        char paced[220];
        snprintf(paced, sizeof paced, "by the game's clock (%s)", g_dc.why);
        if (g_dc.paced_frames)
            snprintf(paced, sizeof paced, "%lu paced by it, %.2f ms into a refresh, %.2f ms "
                     "from that on average and %lu more than a quarter of a refresh off",
                     g_dc.paced_frames, g_dc.at * g_dc.period * 1000.0,
                     g_dc.off_sum / (double)g_dc.paced_frames * 1000.0, g_dc.off);
        fprintf(stderr, "  [PACE] the display: %.3f Hz by the kernel's count; since the last "
                "report, %lu frames handed over while its counts kept time, %s; in each eighth of a refresh %lu %lu %lu %lu "
                "%lu %lu %lu %lu; counted %lu times, the longest %.3f ms\n",
                1.0 / g_dc.period, g_dc.frames, paced, g_dc.slice[0], g_dc.slice[1],
                g_dc.slice[2], g_dc.slice[3], g_dc.slice[4], g_dc.slice[5], g_dc.slice[6],
                g_dc.slice[7], g_dc.counts, g_dc.count_most * 1000.0);
    } else {
        fprintf(stderr, "  [PACE] the display: frames paced by the game's clock (%s); counted "
                "%lu times since the last report, the longest %.3f ms\n", g_dc.why, g_dc.counts,
                g_dc.count_most * 1000.0);
    }
    g_dc.frames = g_dc.paced_frames = g_dc.off = g_dc.counts = 0;
    memset(g_dc.slice, 0, sizeof g_dc.slice);
    g_dc.off_sum = g_dc.count_most = 0.0;
}

#endif /* NV2A_DISPLAY_CLOCK */
