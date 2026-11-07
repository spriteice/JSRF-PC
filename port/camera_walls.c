/* The follow camera's view kept out of walls (camera_walls.h).
 *
 * What a camera keeps from one frame to the next, the share and the move
 * across, is each camera's own (camwall_for), or the caller's. */
#include <math.h>
#include "camera_walls.h"

/* How many times camwall_clear moves the eye across, and then brings it
 * in, for the corners of the view before it takes it that neither clears
 * them (a space narrower than the view). Each time costs four or five line
 * tests. */
#define CAMWALL_TRIES 3

void camwall_reset(struct camwall *w)
{
    const struct camwall start = CAMWALL_START;
    *w = start;
}

struct camwall *camwall_for(uint32_t cam, uint32_t key)
{
    static struct { uint32_t cam, key; unsigned seen; struct camwall w; } kept[CAMWALL_CAMERAS];
    static unsigned now;                /* calls so far: when each camera was last seen */
    int i, old = 0;
    now++;
    for (i = 0; i < CAMWALL_CAMERAS && !(kept[i].seen && kept[i].cam == cam); i++)
        if (kept[i].seen < kept[old].seen) old = i;
    if (i == CAMWALL_CAMERAS) {         /* not seen: the slot seen longest ago, or never */
        i = old;
        kept[i].cam = cam;
        kept[i].key = ~key;             /* so that it starts again, below */
    }
    kept[i].seen = now;
    if (kept[i].key != key) {
        kept[i].key = key;
        camwall_reset(&kept[i].w);
    }
    return &kept[i].w;
}

/* Where the view starts for an eye at share s of u from tgt, moved across
 * by p: ahead of the eye, toward the target (f), and its corner on side
 * (sx, sy) there. */
static void view_corner(const float tgt[3], const double u[3], double s, const double p[3],
                        double ahead, const double f[3], double sx, const double rt[3],
                        double sy, const double up[3], double c[3])
{
    int k;
    for (k = 0; k < 3; k++)
        c[k] = tgt[k] + s * u[k] + p[k] + ahead * f[k] + sx * rt[k] + sy * up[k];
}

/* Whether a surface cuts across the view where it starts, for an eye at
 * share s moved across by p: a line from its middle, on the eye's line, to
 * each corner (i: bit 0 right, bit 1 up). t[i] is how much of that line is
 * open; returns the least. */
static double view_cuts(const float tgt[3], const double u[3], double s, const double p[3],
                        double ahead, const double f[3], double half_w, const double rt[3],
                        double half_h, const double up[3], camwall_line_fn line, void *ctx,
                        double t[4])
{
    double m[3], c[3], least = 1.0;
    int i;
    view_corner(tgt, u, s, p, ahead, f, 0, rt, 0, up, m);
    for (i = 0; i < 4; i++) {
        view_corner(tgt, u, s, p, ahead, f, (i & 1) ? half_w : -half_w, rt,
                    (i & 2) ? half_h : -half_h, up, c);
        t[i] = line(ctx, m, c);
        least = fmin(least, t[i]);
    }
    return least;
}

/* The view, for an eye at eye looking at tgt: u from tgt to the eye and d
 * its length; it faces f, from the eye to the target; left-handed, y up:
 * right of it rt (as auto_step), and up, square to both. 0 when there is
 * none. The free camera keeps the pitch short of straight up or down, so
 * right is never nothing for it. */
static int view_frame(const float tgt[3], const float eye[3], double u[3], double *d,
                      double f[3], double rt[3], double up[3])
{
    double rl;
    int k;
    for (k = 0; k < 3; k++) u[k] = eye[k] - tgt[k];
    *d = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (!(*d > 0)) return 0;
    for (k = 0; k < 3; k++) f[k] = -u[k] / *d;
    rl = sqrt(f[2] * f[2] + f[0] * f[0]);
    if (!(rl > 0)) return 0;
    rt[0] = f[2] / rl; rt[1] = 0; rt[2] = -f[0] / rl;
    up[0] = f[1] * rt[2] - f[2] * rt[1];
    up[1] = f[2] * rt[0] - f[0] * rt[2];
    up[2] = f[0] * rt[1] - f[1] * rt[0];
    return 1;
}

double camwall_clear(const float tgt[3], const float eye[3], double most, int eye_line,
                     double ahead, double half_w, double half_h, camwall_line_fn line,
                     void *ctx, double push[3], int *by)
{
    static const double none[3] = { 0, 0, 0 };
    double a[3], b[3], u[3], f[3], rt[3], up[3], p[3], t[4], d, wl, nearest, s0, s;
    int i, k, n, viewed, framed = view_frame(tgt, eye, u, &d, f, rt, up);
    for (k = 0; k < 3; k++) {
        a[k] = tgt[k];
        b[k] = a[k] + most * u[k];
        push[k] = p[k] = 0;
    }
    *by = CAMWALL_CLEAR;
    if (!(d > 0 && most > 0)) return most;
    /* The eye's line, as the game tests it, out to most. */
    s0 = most * line(ctx, a, b);
    viewed = half_w > 0 && half_h > 0 && ahead >= 0 && framed;
    /* The game's own eye stands where the game's test left it, unless what
     * its line meets hides the whole of where the view starts from beside
     * the target: four lines, each as long as the eye's and beside it, from
     * a corner of the view's size at the target to the same corner where
     * the view starts. A pole, a rail or a sign between the player and the
     * eye, narrower than the view, leaves some of them open, and stands, as
     * the game lets it. A surface wider than the view hides them all: the
     * eye has gone behind it, as when a half pipe's curve or a wall comes
     * between as the player moves, which the game's own test, run only when
     * the eye's move crosses a surface, never sees. Then the eye comes in to
     * it, as the game's test would have put it. With no view to test by,
     * it stands. */
    if (!eye_line && s0 < most) {
        double c0[3], c1[3];
        if (!viewed) s0 = most;
        for (i = 0; i < 4 && s0 < most; i++) {
            double sx = (i & 1) ? half_w : -half_w, sy = (i & 2) ? half_h : -half_h;
            view_corner(tgt, u, 0, none, 0, f, sx, rt, sy, up, c0);
            view_corner(tgt, u, most, none, ahead, f, sx, rt, sy, up, c1);
            if (line(ctx, c0, c1) >= 1) s0 = most;
        }
    }
    if (s0 < most) *by = CAMWALL_EYE;
    if (!viewed) return s0;
    /* The eye no nearer the target than where its view starts a view's
     * reach (wl, from the eye to a corner of where the view starts) in
     * front of the target. Nearer, the view starts at or past the player:
     * it shows them from inside, and the game's view of a point so close
     * turns over. That is when the player is right against the wall the
     * eye's line meets: the eye stays this far out, and that wall, which
     * no eye on that side clears, is seen through, as the game's camera
     * would see through it with its eye on the player. */
    wl = sqrt(ahead * ahead + half_w * half_w + half_h * half_h);
    nearest = (ahead + wl) / d;
    if (s0 < nearest) {
        *by = CAMWALL_NEAREST;
        return most < nearest ? most : nearest;
    }
    /* When nothing cuts across the view where it starts, it stands: a pole,
     * a rail or the edge of a wall between the player and the eye that only
     * hides a corner of the picture hides it, as it would the game's, and
     * does not bring the eye in. */
    if (view_cuts(tgt, u, s0, none, ahead, f, half_w, rt, half_h, up, line, ctx, t) >= 1)
        return s0;
    /* Something does. First the eye moves across, away from what cuts it:
     * a wall along the view, the ground under it, the edge of a block. A
     * corner whose line from the middle is open for a share t of it has the
     * surface (1 - t) of its way short of it, so the eye moves back that
     * much from each corner cut. Two corners cut on one side, by a wall
     * along that side, take it twice that, which leaves the corners clear
     * of the wall rather than on it. A unit or two across, at the eye's
     * distance, turns the view a few degrees, where coming in would bring
     * it right up to the player. The line from the target to the middle of
     * where the view starts must be clear there as well: to that, not to
     * the eye, which sits on the wall where the eye's line met one, and
     * which the view does not show. */
    for (n = 0; n < CAMWALL_TRIES; n++) {
        double step[3] = { 0, 0, 0 }, mid[3];
        for (i = 0; i < 4; i++) {
            double sx = (i & 1) ? half_w : -half_w, sy = (i & 2) ? half_h : -half_h;
            if (t[i] < 1)
                for (k = 0; k < 3; k++) step[k] -= (1 - t[i]) * (sx * rt[k] + sy * up[k]);
        }
        if (!(step[0] != 0 || step[1] != 0 || step[2] != 0)) break;   /* cut either side alike */
        for (k = 0; k < 3; k++) {
            p[k] += step[k];
            mid[k] = a[k] + s0 * u[k] + p[k] + ahead * f[k];
        }
        if (line(ctx, a, mid) < 1) break;
        if (view_cuts(tgt, u, s0, p, ahead, f, half_w, rt, half_h, up, line, ctx, t) >= 1) {
            for (k = 0; k < 3; k++) push[k] = p[k];
            *by = CAMWALL_ACROSS;
            return s0;
        }
    }
    /* Moving across does not clear it -- a space narrower than the view, or
     * a wall the move meets -- so the eye comes in until the corners of the
     * view where it starts are clear, each by a line from the target. The
     * player stands in the open, so a wall or the ground beside them is not
     * in the way of a view that starts a unit off its line, as it was when
     * the lines ran beside it from the player. A corner wl from the eye's
     * line whose line is open for a share t of it, with the eye brought in
     * by t, sits (1 - t) wl past where that line stopped; so the eye comes
     * in that much further as well, and the corners are tested again where
     * it is then. */
    for (s = s0, n = 0; n < CAMWALL_TRIES; n++) {
        double open = 1.0, c[3];
        if (!(s >= nearest)) break;
        for (i = 0; i < 4; i++) {
            view_corner(tgt, u, s, none, ahead, f, (i & 1) ? half_w : -half_w, rt,
                        (i & 2) ? half_h : -half_h, up, c);
            open = fmin(open, line(ctx, a, c));
        }
        if (open >= 1) {
            if (n) *by = CAMWALL_CORNER;
            return s;
        }
        s = s * open - (1 - open) * wl / d;
    }
    /* Neither clears it -- a space narrower than the view all the way in --
     * so the eye stays where its own line puts it, as the game's camera
     * would. */
    *by = CAMWALL_NO_ROOM;
    return s0;
}

double camwall_share(struct camwall *w, double open)
{
    if (open < w->share) {                              /* in at once, and wait */
        w->share = open;
        w->share_wait = 0;
    } else if (w->share_wait < CAMWALL_HOLD) {
        w->share_wait++;
    } else {                                            /* then out by degrees */
        w->share += (open - w->share) / CAMWALL_EASE;
        if ((float)w->share == (float)open) w->share = open;  /* as near as the eye's floats go */
    }
    return w->share;
}

void camwall_across(struct camwall *w, const float tgt[3], const float eye[3], double s,
                    const double need[3], double ahead, double half_w, double half_h,
                    camwall_line_fn line, void *ctx, double out[3])
{
    double u[3], f[3], rt[3], up[3], t[4], mid[3], d;
    int k, moved = w->across[0] != 0 || w->across[1] != 0 || w->across[2] != 0, clear;
    if (!moved) {
        /* Not moved: camwall_clear found the view clear unless it moves it. */
        clear = !(need[0] != 0 || need[1] != 0 || need[2] != 0);
    } else if (!(half_w > 0 && half_h > 0 && ahead >= 0)
            || !view_frame(tgt, eye, u, &d, f, rt, up)) {
        clear = 1;                      /* no view to test: back by degrees */
    } else {
        /* Last frame's move, where the eye is now: the line from the target
         * to the middle of where the view starts open, and nothing across
         * it, as camwall_clear tests a move. */
        for (k = 0; k < 3; k++) mid[k] = tgt[k] + s * u[k] + w->across[k] + ahead * f[k];
        {
            const double a[3] = { tgt[0], tgt[1], tgt[2] };
            clear = line(ctx, a, mid) >= 1
                 && view_cuts(tgt, u, s, w->across, ahead, f, half_w, rt, half_h, up, line, ctx, t) >= 1;
        }
    }
    if (!clear) {                                       /* to it at once, and wait */
        for (k = 0; k < 3; k++) w->across[k] = need[k];
        w->across_wait = 0;
    } else if (w->across_wait < CAMWALL_HOLD) {
        w->across_wait++;
    } else {                                            /* then toward it by degrees */
        double gap = 0, far = 0;
        for (k = 0; k < 3; k++) {
            w->across[k] += (need[k] - w->across[k]) / CAMWALL_EASE;
            gap = fmax(gap, fabs(need[k] - w->across[k]));
            far += (eye[k] - tgt[k]) * (eye[k] - tgt[k]);
        }
        far = s * sqrt(far);                            /* the eye from the target */
        if ((float)(far + gap) == (float)far)           /* as near as the eye's floats go there */
            for (k = 0; k < 3; k++) w->across[k] = need[k];
    }
    for (k = 0; k < 3; k++) out[k] = w->across[k];
}
