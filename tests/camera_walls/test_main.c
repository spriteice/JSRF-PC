/* Run with: python tests/camera_walls/run.py
 *
 * The follow camera's view kept out of walls without the game:
 * port/camera_walls.c as it is, against a made-up world of planes and boxes
 * in place of the game's line test. Each case is its own run; run.py checks
 * the log.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "camera_walls.h"

static void near(double got, double want, const char *what)
{
    if (fabs(got - want) > 1e-3) {
        fprintf(stderr, "FAIL %s: %.5f, not %.5f\n", what, got, want);
        exit(1);
    }
}

/* A made-up world for camwall_clear: half-spaces (the side of a plane where
 * n.p < c) and boxes, each solid. world_line is the line test: the share of
 * the line from a to b before it enters one, 0 when a is in one. */
static struct { int box; double n[3], c, lo[3], hi[3]; } s_solid[4];
static int s_solids, s_lines;
static void add_plane(double nx, double ny, double nz, double c)
{
    s_solid[s_solids].box = 0;
    s_solid[s_solids].n[0] = nx; s_solid[s_solids].n[1] = ny; s_solid[s_solids].n[2] = nz;
    s_solid[s_solids++].c = c;
}
static void add_box(double x0, double y0, double z0, double x1, double y1, double z1)
{
    s_solid[s_solids].box = 1;
    s_solid[s_solids].lo[0] = x0; s_solid[s_solids].lo[1] = y0; s_solid[s_solids].lo[2] = z0;
    s_solid[s_solids].hi[0] = x1; s_solid[s_solids].hi[1] = y1; s_solid[s_solids++].hi[2] = z1;
}
static double world_line(void *ctx, const double a[3], const double b[3])
{
    double open = 1;
    int i, k;
    (void)ctx;
    s_lines++;
    for (i = 0; i < s_solids; i++) {
        double t0 = 0, t1 = 1;
        if (!s_solid[i].box) {
            double ga = -s_solid[i].c, gb = -s_solid[i].c;
            for (k = 0; k < 3; k++) { ga += s_solid[i].n[k] * a[k]; gb += s_solid[i].n[k] * b[k]; }
            if (ga < 0) return 0;
            if (gb < 0) open = fmin(open, ga / (ga - gb));
            continue;
        }
        for (k = 0; k < 3 && t0 <= t1; k++) {
            double dk = b[k] - a[k], lo = s_solid[i].lo[k], hi = s_solid[i].hi[k];
            if (dk == 0) {
                if (a[k] < lo || a[k] > hi) t0 = 2;
            } else {
                double ta = (lo - a[k]) / dk, tb = (hi - a[k]) / dk;
                t0 = fmax(t0, fmin(ta, tb));
                t1 = fmin(t1, fmax(ta, tb));
            }
        }
        if (t0 <= t1) open = fmin(open, t0);
    }
    return open;
}
/* Whether the view's start, for the eye at share s moved across by p, is
 * outside every solid: its middle and four corners, worked out as
 * camwall_clear does. */
static int view_clear(const float t[3], const float e[3], double s, const double p[3],
                      double ahead, double hw, double hh)
{
    double u[3], f[3], rt[3], up[3], d, rl;
    int i, k, j;
    for (k = 0; k < 3; k++) u[k] = e[k] - t[k];
    d = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (k = 0; k < 3; k++) f[k] = -u[k] / d;
    rl = sqrt(f[2] * f[2] + f[0] * f[0]);
    rt[0] = f[2] / rl; rt[1] = 0; rt[2] = -f[0] / rl;
    up[0] = f[1] * rt[2] - f[2] * rt[1];
    up[1] = f[2] * rt[0] - f[0] * rt[2];
    up[2] = f[0] * rt[1] - f[1] * rt[0];
    for (i = 0; i < 5; i++) {
        /* 4 the middle, 0 to 3 the corners */
        double c[3], sx = i == 4 ? 0 : (i & 1) ? hw : -hw, sy = i == 4 ? 0 : (i & 2) ? hh : -hh;
        for (k = 0; k < 3; k++)
            c[k] = t[k] + s * u[k] + p[k] + ahead * f[k] + sx * rt[k] + sy * up[k];
        for (j = 0; j < s_solids; j++) {
            if (!s_solid[j].box) {
                double g = -s_solid[j].c;
                for (k = 0; k < 3; k++) g += s_solid[j].n[k] * c[k];
                if (g < -1e-9) return 0;
            } else {
                for (k = 0; k < 3 && c[k] >= s_solid[j].lo[k] && c[k] <= s_solid[j].hi[k]; k++) {}
                if (k == 3) return 0;
            }
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    struct camwall w = CAMWALL_START;
    if (!strcmp(c, "wall")) {
        /* After the wall test: in at once; then, once nothing has brought it
         * in for 20 frames (CAMWALL_HOLD), back out a tenth of the gap
         * a frame, all the way, never past what is clear, and all the way out
         * again when the camera starts again. */
        static const double opens[] = { 1, 0.9, 0.2, 1, 0.5, 0.6, 1, 0.05, 1, 1 };
        double s = 1;
        int i;
        near(camwall_share(&w, 1), 1, "clear: the whole distance");
        near(camwall_share(&w, 0.4), 0.4, "a wall: in at once");
        for (i = 0; i < 20; i++) near(camwall_share(&w, 1), 0.4, "clear again: it waits");
        near(camwall_share(&w, 1), 0.46, "then a tenth of the way back out");
        near(camwall_share(&w, 1), 0.514, "and a tenth of what is left");
        near(camwall_share(&w, 0.3), 0.3, "another wall: in at once");
        for (i = 0; i < 20; i++) near(camwall_share(&w, 0.35), 0.3, "clear a little further: it waits");
        near(camwall_share(&w, 0.35), 0.305, "then out by degrees");
        near(camwall_share(&w, 0.32), 0.3065, "less clear, but more than where it is: on out");
        for (i = 0; i < 11; i++) s = camwall_share(&w, 1);
        near(s, 1 - 0.6935 * pow(0.9, 11), "two thirds of the way out in 11 frames");
        for (i = 0; i < 400 && s < 1; i++) s = camwall_share(&w, 1);
        assert(s == 1);
        for (i = 0; i < 100; i++) {
            double open = opens[i % (sizeof opens / sizeof opens[0])];
            s = camwall_share(&w, open);
            if (s > open) {
                fprintf(stderr, "FAIL frame %d: the eye at %.5f, past the clear %.5f\n", i, s, open);
                return 1;
            }
        }
        assert(camwall_share(&w, 0.01) == 0.01);
        camwall_reset(&w);
        near(camwall_share(&w, 1), 1, "started again: the whole distance, and no wait");
    } else if (!strcmp(c, "across")) {
        /* After camwall_clear, how far across: to what it moved the eye at
         * once; once a wall it moved away from ends, last frame's for 20
         * frames, then back a tenth of the way a frame, all the way; to it
         * at once again when the wall comes back; and nothing when the camera
         * starts again. */
        const float t3[3] = { 0, 3, 0 }, e3[3] = { 0, 3, -10 };
        double need[3], out[3], first;
        int by, i;
        s_solids = 0;
        add_plane(-1, 0, 0, -0.5);              /* a wall along the view, half a unit to the right */
        camwall_clear(t3, e3, 1.0, 1.0, 1.0, 0.57, world_line, NULL, need, &by);
        assert(by == CAMWALL_ACROSS && need[0] < 0);
        camwall_across(&w, t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        near(out[0], need[0], "a wall: across at once");
        first = out[0];
        s_solids = 0;                           /* skated past its end */
        camwall_clear(t3, e3, 1.0, 1.0, 1.0, 0.57, world_line, NULL, need, &by);
        assert(by == CAMWALL_CLEAR && need[0] == 0);
        for (i = 0; i < 20; i++) {
            camwall_across(&w, t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
            near(out[0], first, "the wall ends: it waits");
        }
        camwall_across(&w, t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        near(out[0], first * 0.9, "then a tenth of the way back");
        for (i = 0; i < 10; i++) camwall_across(&w, t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        near(out[0], first * pow(0.9, 11), "two thirds of the way back in 11 frames");
        add_plane(-1, 0, 0, -0.5);              /* the wall again */
        camwall_clear(t3, e3, 1.0, 1.0, 1.0, 0.57, world_line, NULL, need, &by);
        camwall_across(&w, t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        near(out[0], first, "the wall again, the view cut where it was: across at once");
        assert(view_clear(t3, e3, 1.0, out, 1.0, 1.0, 0.57));
        s_solids = 0;
        need[0] = need[1] = need[2] = 0;
        for (i = 0; i < 400 && out[0] != 0; i++)
            camwall_across(&w, t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        assert(out[0] == 0 && out[1] == 0 && out[2] == 0);
        add_plane(-1, 0, 0, -0.5);
        camwall_clear(t3, e3, 1.0, 1.0, 1.0, 0.57, world_line, NULL, need, &by);
        camwall_across(&w, t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        camwall_reset(&w);
        s_solids = 0;
        need[0] = need[1] = need[2] = 0;
        camwall_across(&w, t3, e3, 1.0, need, 1.0, 1.0, 0.57, world_line, NULL, out);
        assert(out[0] == 0 && out[1] == 0 && out[2] == 0);
    } else if (!strncmp(c, "clear_", 6)) {
        /* camwall_clear against a made-up world: the player at (0, 3, 0),
         * the eye 10 units behind, the view starting 1 ahead of it, 1 by
         * 0.57 either side (about what the game's is at 16:9). */
        static const double none[3] = { 0, 0, 0 };
        const double wl = sqrt(1.0 + 1.0 + 0.57 * 0.57), nearest = (1.0 + wl) / 10;
        const float t3[3] = { 0, 3, 0 };
        float e3[3] = { 0, 3, -10 };
        double got, push[3], moved;
        int by, want_by;
        s_solids = s_lines = 0;
        if (!strcmp(c, "clear_open")) {
            want_by = CAMWALL_CLEAR;
        } else if (!strcmp(c, "clear_back")) {
            add_plane(0, 0, 1, -6);             /* everything behind z = -6 */
            want_by = CAMWALL_EYE;
        } else if (!strcmp(c, "clear_floor")) {
            add_plane(0, 1, 0, 0);              /* the ground, y = 0, the eye just above it */
            e3[1] = 0.2f;
            want_by = CAMWALL_ACROSS;
        } else if (!strcmp(c, "clear_room_corner")) {
            add_box(0.5, -10, -20, 10, 10, -7); /* behind the eye and off to its right */
            want_by = CAMWALL_ACROSS;
        } else if (!strcmp(c, "clear_alongside")) {
            add_plane(-1, 0, 0, -0.5);          /* a wall along the view, half a unit to the right */
            want_by = CAMWALL_ACROSS;
        } else if (!strcmp(c, "clear_block_edge")) {
            add_box(-10, -10, -20, -0.3, 10, -8); /* a block's edge just past the eye, on the left */
            want_by = CAMWALL_ACROSS;
        } else if (!strcmp(c, "clear_corridor")) {
            add_plane(-1, 0, 0, -2);            /* a wall 2 to the right, */
            e3[0] = 3.42f; e3[2] = -9.4f;       /* the eye turned 20 degrees toward it */
            want_by = CAMWALL_ACROSS;
        } else if (!strcmp(c, "clear_beside_player")) {
            add_box(0.3, -10, -3, 5, 10, 3);    /* a block right beside the player, ending 3 behind */
            want_by = CAMWALL_CLEAR;
        } else if (!strcmp(c, "clear_pole")) {
            add_box(0.3, -10, -5, 0.6, 10, -4.5); /* a pole between the player and a corner */
            want_by = CAMWALL_CLEAR;
        } else if (!strcmp(c, "clear_gap_behind")) {
            add_box(0.5, -10, -30, 10, 10, -7); /* the eye in a gap narrower than the view, */
            add_box(-10, -10, -30, -0.5, 10, -7); /* which ends 7 behind the player */
            want_by = CAMWALL_CORNER;
        } else if (!strcmp(c, "clear_narrow")) {
            add_plane(-1, 0, 0, -0.6);          /* a gap narrower than the view, all the way */
            add_plane(1, 0, 0, -0.6);
            want_by = CAMWALL_NO_ROOM;
        } else if (!strcmp(c, "clear_hugging")) {
            add_plane(-1, 0, 0, -0.3);          /* the player right against a wall, */
            e3[0] = 8.66f; e3[2] = -5;          /* the eye turned 60 degrees into it */
            want_by = CAMWALL_NEAREST;
        } else {
            fprintf(stderr, "no case %s\n", c);
            return 2;
        }
        got = camwall_clear(t3, e3, 1.0, 1.0, 1.0, 0.57, world_line, NULL, push, &by);
        moved = sqrt(push[0] * push[0] + push[1] * push[1] + push[2] * push[2]);
        fprintf(stderr, "%s: share %.4f, %.2f across, by %d, %d lines\n", c, got, moved, by, s_lines);
        if (by != want_by) {
            fprintf(stderr, "FAIL %s: by %d, not %d\n", c, by, want_by);
            return 1;
        }
        if (by != CAMWALL_ACROSS) assert(moved == 0);
        if (!strcmp(c, "clear_open") || !strcmp(c, "clear_beside_player") || !strcmp(c, "clear_pole")) {
            /* Nothing that cuts across where the view starts: the eye stays
             * out, and only the eye's line and the four lines across the
             * view are tested. */
            near(got, 1, "the view is not cut: the eye stays out");
            assert(view_clear(t3, e3, got, push, 1.0, 1.0, 0.57));
            assert(s_lines == 5);
            if (!strcmp(c, "clear_pole")) {
                /* The line from the target to the view's top right corner,
                 * which the fourth build's corner lines went by, meets the
                 * pole. */
                const double a[3] = { 0, 3, 0 }, b[3] = { 1, 3.57, -9 };
                assert(world_line(NULL, a, b) < 1);
            }
        } else if (!strcmp(c, "clear_back")) {
            near(got, 0.6, "a wall behind the eye: in to it, as the game's line test");
        } else if (by == CAMWALL_ACROSS) {
            /* The eye stays out, moved across by less than the view's reach
             * for each corner, and the whole start of the view is out of
             * the ground, the block or the wall there; where it was, it
             * was not. Before, these brought the eye in, the wall along
             * the view right in to the player. */
            if (!strcmp(c, "clear_corridor"))   /* on the wall, where the eye's line meets it */
                near(got, 2 / 3.42, "moved across: the eye stays where its line puts it");
            else
                near(got, 1, "moved across: the eye stays out");
            assert(moved > 0 && moved <= 4 * wl);
            assert(view_clear(t3, e3, got, push, 1.0, 1.0, 0.57));
            assert(!view_clear(t3, e3, got, none, 1.0, 1.0, 0.57));
        } else if (by == CAMWALL_CORNER) {
            /* Moving across clears neither side of a gap narrower than the
             * view: the eye comes in, out of the gap, but not right in. */
            assert(got > nearest && got < 1);
            assert(view_clear(t3, e3, got, none, 1.0, 1.0, 0.57));
            assert(!view_clear(t3, e3, 1.0, none, 1.0, 1.0, 0.57));
        } else if (by == CAMWALL_NO_ROOM) {
            /* Neither clears a gap narrower than the view all the way in:
             * the eye's line alone, as in the game. */
            near(got, 1, "neither clears it: the eye's line alone");
        } else {
            /* The eye's line meets the wall 0.35 from the target: the eye
             * stays where its view starts in front of the player. */
            near(got, nearest, "as near as the eye goes");
        }
        /* And the held eye's view, tested where it is (camwall_clear up to
         * it), with the eye moved across there. */
        if (!strcmp(c, "clear_floor")) {
            double held = camwall_clear(t3, e3, 0.6, 1.0, 1.0, 0.57, world_line, NULL, push, &by);
            assert(held <= 0.6 && view_clear(t3, e3, held, push, 1.0, 1.0, 0.57));
        }
    } else {
        fprintf(stderr, "no case %s\n", c);
        return 2;
    }
    printf("ok %s\n", c);
    return 0;
}
