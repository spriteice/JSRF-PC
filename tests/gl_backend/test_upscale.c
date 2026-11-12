/* Run with: python tests/gl_backend/run.py
 *
 * Upscaling's copy in the OpenGL backend (tex_upscale in
 * src/nv2a_gl/nv2a_gfx_gl.c, its passes in nv2a_upscale_glsl.c), through the
 * device interface on a headless EGL context (context.h):
 *
 *  - the levels: 2x, 4x and 8x the size at level 0, each after it half the
 *    one before, and the last the game's texels exactly, from RGBA8 and from
 *    DXT1; with the game's mipmaps, its chain below that too;
 *  - where the picture lands: Super-xBR's filters are exact on a ramp, so a
 *    ramp across, down and diagonally comes back as the same ramp at the
 *    texel centres of the bigger image, at 2x, 4x and 8x -- which a pass
 *    reading the wrong texel, or the last pass not putting the image back
 *    where the source was, would not give; at Soft's and Sharp's filter
 *    weights and Soft's and Strong's edge strengths as at Normal's;
 *  - alpha: an opaque red half beside a clear blue one keeps red wherever
 *    anything shows (no fringe of the clear colour), clear blue where
 *    nothing does, and an edge that falls from opaque to clear and never
 *    rises;
 *  - wrap: with repeat, the copy of a picture moved round by (5, 3) is the
 *    copy moved round by (10, 6), texel for texel;
 *  - the title's own chain, from RGBA8 and from DXT1: below the new levels,
 *    the game's levels, each texel for texel, and no more;
 *  - the game's alpha kept: each new level's alpha the game's as the sampler
 *    magnifies it (nearest and linear, at repeat), and its colour Super-xBR's
 *    as without it;
 *  - the filter weight (Upscale sharpness): hard edges come back crisper the
 *    higher it is, every texel within the source's range, and a weight out
 *    of range is taken as 1;
 *  - the edge strength (Edge strength): a faint diagonal line is followed
 *    the more the lower it is, every texel within the source's range, and a
 *    strength out of range is taken as 0.6;
 *  - and what is refused (another factor, a side over the device's
 *    largest), with the target being drawn into and its viewport as they
 *    were, and no GL error.
 */
#include "context.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"

static const Nv2aGfx *G;

static void check(int ok, const char *what)
{
    GLenum e = glGetError();
    if (!ok || e != GL_NO_ERROR) {
        fprintf(stderr, "FAIL %s%s (GL error 0x%x)\n", what, ok ? "" : ": wrong", e);
        exit(1);
    }
}

/* Through a unit the backend's passes don't use (they use 4 and 5). */
static void bind(uint32_t tex)
{
    glActiveTexture(GL_TEXTURE0 + 6);
    glBindTexture(GL_TEXTURE_2D, tex);
}

static void level_size(uint32_t tex, int l, GLint *w, GLint *h)
{
    bind(tex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, l, GL_TEXTURE_WIDTH, w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, l, GL_TEXTURE_HEIGHT, h);
}

static GLint param(uint32_t tex, GLenum p)
{
    GLint v = -1;
    bind(tex);
    glGetTexParameteriv(GL_TEXTURE_2D, p, &v);
    return v;
}

/* Level l of tex, RGBA8, malloc'd. */
static uint8_t *read_level(uint32_t tex, int l)
{
    GLint w = 0, h = 0;
    uint8_t *px;
    level_size(tex, l, &w, &h);
    px = malloc((size_t)w * h * 4);
    bind(tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, l, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return px;
}

/* The copy's levels: sizes, how many, and the last the source's level 0. */
static void levels(uint32_t src, uint32_t w, uint32_t h, int factor,
                   const Nv2aSampler *s, const char *what)
{
    int steps = factor == 2 ? 1 : factor == 4 ? 2 : 3, l;
    uint32_t t = G->tex_upscale(src, w, h, factor, 0, 1.0f, 0.6f, s);
    uint8_t *want, *got;
    char msg[160];
    snprintf(msg, sizeof msg, "%s at %dx: made", what, factor);
    check(t != 0, msg);
    for (l = 0; l <= steps; l++) {
        GLint lw, lh;
        level_size(t, l, &lw, &lh);
        snprintf(msg, sizeof msg, "%s at %dx: level %d is %ux%u (it is %dx%d)", what, factor, l,
                 (w * (uint32_t)factor) >> l, (h * (uint32_t)factor) >> l, (int)lw, (int)lh);
        check(lw == (GLint)((w * (uint32_t)factor) >> l) && lh == (GLint)((h * (uint32_t)factor) >> l), msg);
    }
    want = read_level(src, 0);
    got = read_level(t, steps);
    snprintf(msg, sizeof msg, "%s at %dx: level %d is the source's texels", what, factor, steps);
    check(!memcmp(want, got, (size_t)w * h * 4), msg);
    free(want);
    free(got);
    if (s->mip) {
        GLint lw, lh;
        level_size(t, steps + 1, &lw, &lh);
        snprintf(msg, sizeof msg, "%s at %dx: the chain goes on below the source's size", what, factor);
        check(lw == (GLint)(w / 2) && lh == (GLint)(h / 2) && param(t, GL_TEXTURE_MAX_LEVEL) >= steps + 1
              && param(t, GL_TEXTURE_MIN_FILTER) == (s->min_linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR),
              msg);
    } else {
        snprintf(msg, sizeof msg, "%s at %dx: levels 0 to %d, mipmapped", what, factor, steps);
        check(param(t, GL_TEXTURE_MAX_LEVEL) == steps
              && param(t, GL_TEXTURE_MIN_FILTER) == (s->min_linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
              && param(t, GL_TEXTURE_MAG_FILTER) == (s->mag_linear ? GL_LINEAR : GL_NEAREST),
              msg);
    }
    printf("ok: %s at %dx: %u levels, the last the source's\n", what, factor, steps + 1);
    G->tex_free(t);
}

/* A ramp, v = ax * x + ay * y, comes back as the same ramp: texel (X, Y) of
 * the copy is at (X + 0.5) / f - 0.5 of the source's, away from the edges. */
static void ramp(int ax, int ay, int factor, float weight, float edge, const char *what)
{
    enum { N = 24 };
    static uint8_t px[N * N * 4];
    Nv2aSampler s = { 1, 1, 0, 0, 0, 0, 0 };
    uint32_t src, t;
    uint8_t *got;
    int x, y, worst = 0, n = N * factor, margin = 4 * factor;
    char msg[160];
    for (y = 0; y < N; y++)
        for (x = 0; x < N; x++) {
            uint8_t *p = px + (y * N + x) * 4;
            p[0] = (uint8_t)(ax * x + ay * y);
            p[1] = (uint8_t)(255 - (ax * x + ay * y));
            p[2] = 77;
            p[3] = 255;
        }
    src = G->tex_new(N, N, px, &s);
    t = G->tex_upscale(src, N, N, factor, 0, weight, edge, &s);
    check(src && t, "a ramp's copy made");
    got = read_level(t, 0);
    for (y = margin; y < n - margin; y++)
        for (x = margin; x < n - margin; x++) {
            double sx = (x + 0.5) / factor - 0.5, sy = (y + 0.5) / factor - 0.5;
            double v = ax * sx + ay * sy;
            const uint8_t *p = got + ((size_t)y * n + x) * 4;
            int d0 = abs(p[0] - (int)(v + 0.5)), d1 = abs(p[1] - (int)(255 - v + 0.5));
            int d2 = abs(p[2] - 77), d3 = abs(p[3] - 255);
            int d = d0 > d1 ? d0 : d1;
            if (d2 > d) d = d2;
            if (d3 > d) d = d3;
            if (d > worst) worst = d;
        }
    snprintf(msg, sizeof msg, "%s at %dx, weight %.1f, edge strength %.1f: the ramp where it "
             "should be (out by %d at most)", what, factor, (double)weight, (double)edge, worst);
    check(worst <= 1, msg);
    printf("ok: %s\n", msg);
    free(got);
    G->tex_free(t);
    G->tex_free(src);
}

/* Blocks of 2 x 2 texels, each light or dark at random, at the filter
 * weights of Soft, Normal and Sharp and one out of range: the higher the
 * weight, the further the copy's texels from the middle grey on average (its
 * edges crisper), each within the source's range (anti-ringing: no halo); the
 * one out of range the same as Normal's, texel for texel. */
static void weight(int factor)
{
    enum { N = 16, DARK = 40, LIGHT = 220 };
    static const float w[4] = { 0.5f, 1.0f, 1.5f, 9.0f };
    static uint8_t px[N * N * 4];
    Nv2aSampler s = { 1, 1, 0, 0, 0, 0, 0 };
    uint32_t src, t;
    uint8_t *got[4];
    double crisp[4];
    int x, y, i, n = N * factor;
    char msg[160];
    srand(11);
    for (y = 0; y < N; y += 2)
        for (x = 0; x < N; x += 2) {
            uint8_t v = (rand() & 1) ? LIGHT : DARK;
            int dx, dy;
            for (dy = 0; dy < 2; dy++)
                for (dx = 0; dx < 2; dx++) {
                    uint8_t *p = px + ((y + dy) * N + x + dx) * 4;
                    p[0] = p[1] = p[2] = v;
                    p[3] = 255;
                }
        }
    src = G->tex_new(N, N, px, &s);
    check(src != 0, "the blocks' texture made");
    for (i = 0; i < 4; i++) {
        long sum = 0;
        t = G->tex_upscale(src, N, N, factor, 0, w[i], 0.6f, &s);
        snprintf(msg, sizeof msg, "the blocks' copy at %dx, weight %.1f, made", factor, (double)w[i]);
        check(t != 0, msg);
        got[i] = read_level(t, 0);
        G->tex_free(t);
        for (y = 0; y < n; y++)
            for (x = 0; x < n; x++) {
                const uint8_t *p = got[i] + ((size_t)y * n + x) * 4;
                snprintf(msg, sizeof msg, "%dx, weight %.1f: texel %d,%d is %u,%u,%u,%u", factor,
                         (double)w[i], x, y, p[0], p[1], p[2], p[3]);
                check(p[0] >= DARK && p[0] <= LIGHT && p[1] == p[0] && p[2] == p[0], msg);
                check(p[3] == 255, msg);
                sum += abs(p[0] - (DARK + LIGHT) / 2);
            }
        crisp[i] = (double)sum / ((double)n * n);
    }
    snprintf(msg, sizeof msg, "%dx: crisper the higher the weight (%.2f, %.2f, %.2f from the middle)",
             factor, crisp[0], crisp[1], crisp[2]);
    check(crisp[0] < crisp[1] && crisp[1] < crisp[2], msg);
    printf("ok: %s, within the source's range\n", msg);
    snprintf(msg, sizeof msg, "%dx: a weight out of range taken as 1", factor);
    check(!memcmp(got[3], got[1], (size_t)n * n * 4), msg);
    printf("ok: %s\n", msg);
    for (i = 0; i < 4; i++) free(got[i]);
    G->tex_free(src);
}

/* A faint diagonal line, one texel wide and 10 brighter than what is beside
 * it, at the edge strengths of Soft, Normal and Strong and one out of range:
 * along the line the copy is brighter on average the lower the strength (the
 * line followed rather than blended with what is beside it), Strong's than
 * Soft's by more than rounding, each texel within the source's range; the
 * one out of range the same as Normal's, texel for texel. */
static void edge(int factor)
{
    enum { N = 16, BACK = 100, LINE = 110 };
    static const float e[4] = { 1.8f, 0.6f, 0.2f, 9.0f };
    static uint8_t px[N * N * 4];
    Nv2aSampler s = { 1, 1, 0, 0, 0, 0, 0 };
    uint32_t src, t;
    uint8_t *got[4];
    double along[4];
    int x, y, i, n = N * factor, margin = 4 * factor;
    char msg[160];
    for (y = 0; y < N; y++)
        for (x = 0; x < N; x++) {
            uint8_t *p = px + (y * N + x) * 4;
            p[0] = p[1] = p[2] = x == y ? LINE : BACK;
            p[3] = 255;
        }
    src = G->tex_new(N, N, px, &s);
    check(src != 0, "the line's texture made");
    for (i = 0; i < 4; i++) {
        long sum = 0;
        t = G->tex_upscale(src, N, N, factor, 0, 1.0f, e[i], &s);
        snprintf(msg, sizeof msg, "the line's copy at %dx, edge strength %.1f, made", factor,
                 (double)e[i]);
        check(t != 0, msg);
        got[i] = read_level(t, 0);
        G->tex_free(t);
        for (y = 0; y < n; y++)
            for (x = 0; x < n; x++) {
                const uint8_t *p = got[i] + ((size_t)y * n + x) * 4;
                snprintf(msg, sizeof msg, "%dx, edge strength %.1f: texel %d,%d is %u,%u,%u,%u",
                         factor, (double)e[i], x, y, p[0], p[1], p[2], p[3]);
                check(p[0] >= BACK && p[0] <= LINE && p[1] == p[0] && p[2] == p[0], msg);
                check(p[3] == 255, msg);
            }
        for (x = margin; x < n - margin; x++) sum += got[i][((size_t)x * n + x) * 4];
        along[i] = (double)sum / (n - 2 * margin);
    }
    snprintf(msg, sizeof msg, "%dx: the line followed the more the lower the strength "
             "(%.2f, %.2f, %.2f along it)", factor, along[0], along[1], along[2]);
    check(along[0] <= along[1] && along[1] <= along[2] && along[0] + 1.0 <= along[2], msg);
    printf("ok: %s, within the source's range\n", msg);
    snprintf(msg, sizeof msg, "%dx: an edge strength out of range taken as 0.6", factor);
    check(!memcmp(got[3], got[1], (size_t)n * n * 4), msg);
    printf("ok: %s\n", msg);
    for (i = 0; i < 4; i++) free(got[i]);
    G->tex_free(src);
}

/* Opaque red on the left, clear blue on the right. */
static void alpha_edge(int factor)
{
    enum { N = 16 };
    static uint8_t px[N * N * 4];
    Nv2aSampler s = { 1, 1, 0, 0, 0, 0, 0 };
    uint32_t src, t;
    uint8_t *got;
    int x, y, n = N * factor;
    char msg[160];
    for (y = 0; y < N; y++)
        for (x = 0; x < N; x++) {
            uint8_t *p = px + (y * N + x) * 4;
            p[0] = x < N / 2 ? 255 : 0;
            p[1] = 0;
            p[2] = x < N / 2 ? 0 : 255;
            p[3] = x < N / 2 ? 255 : 0;
        }
    src = G->tex_new(N, N, px, &s);
    t = G->tex_upscale(src, N, N, factor, 0, 1.0f, 0.6f, &s);
    check(src && t, "the alpha edge's copy made");
    got = read_level(t, 0);
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++) {
            const uint8_t *p = got + ((size_t)y * n + x) * 4;
            const uint8_t *q = got + (size_t)x * 4;        /* row 0 */
            int edge = n / 2;
            snprintf(msg, sizeof msg, "%dx: texel %d,%d is %u,%u,%u,%u", factor, x, y,
                     p[0], p[1], p[2], p[3]);
            if (p[3]) check(p[0] >= 253 && p[1] <= 2 && p[2] <= 2, msg);   /* red where it shows */
            else check(p[0] == 0 && p[1] == 0 && p[2] == 255, msg);       /* the clear's own */
            check(memcmp(p, q, 4) == 0, msg);                               /* every row the same */
            if (x < edge - factor) check(p[3] == 255, msg);
            if (x >= edge + factor) check(p[3] == 0, msg);
            if (x > 0) check(p[3] <= p[-1], msg);                           /* never rises */
        }
    printf("ok: alpha at %dx: red wherever it shows, the clear blue elsewhere, a falling edge\n",
           factor);
    free(got);
    G->tex_free(t);
    G->tex_free(src);
}

/* With repeat on both, moving the picture round moves the copy round. */
static void wrap(int factor)
{
    enum { N = 16, DX = 5, DY = 3 };
    static uint8_t a[N * N * 4], b[N * N * 4];
    Nv2aSampler s = { 1, 1, 0, 1, 1, 0, 0 };
    uint32_t ta, tb, ua, ub;
    uint8_t *ga, *gb;
    int x, y, n = N * factor, i;
    srand(9);
    for (i = 0; i < N * N * 4; i++) a[i] = (uint8_t)rand();
    for (i = 0; i < N * N; i++) a[i * 4 + 3] = (uint8_t)(a[i * 4 + 3] | 0x80);
    for (y = 0; y < N; y++)
        for (x = 0; x < N; x++)
            memcpy(b + (((y + DY) % N) * N + (x + DX) % N) * 4, a + (y * N + x) * 4, 4);
    ta = G->tex_new(N, N, a, &s);
    tb = G->tex_new(N, N, b, &s);
    ua = G->tex_upscale(ta, N, N, factor, 0, 1.0f, 0.6f, &s);
    ub = G->tex_upscale(tb, N, N, factor, 0, 1.0f, 0.6f, &s);
    check(ua && ub, "the wrap test's copies made");
    ga = read_level(ua, 0);
    gb = read_level(ub, 0);
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++) {
            int yy = (y + DY * factor) % n, xx = (x + DX * factor) % n;
            char msg[96];
            snprintf(msg, sizeof msg, "repeat at %dx: texel %d,%d moved round", factor, x, y);
            check(!memcmp(ga + ((size_t)y * n + x) * 4, gb + ((size_t)yy * n + xx) * 4, 4), msg);
        }
    printf("ok: repeat at %dx: the copy moves round with the picture\n", factor);
    free(ga);
    free(gb);
    G->tex_free(ua); G->tex_free(ub); G->tex_free(ta); G->tex_free(tb);
}

/* The title's own chain under the copy: level steps + l is the game's level
 * l, texel for texel, and the copy ends with the game's last. */
static void title_chain(uint32_t src, uint32_t w, uint32_t h, int factor,
                        const Nv2aSampler *s, const char *what)
{
    int steps = factor == 2 ? 1 : factor == 4 ? 2 : 3, l;
    uint32_t t = G->tex_upscale(src, w, h, factor, 1, 1.0f, 0.6f, s);
    char msg[160];
    snprintf(msg, sizeof msg, "%s at %dx: made", what, factor);
    check(t != 0, msg);
    for (l = 0; l < s->levels; l++) {
        GLint sw, sh, cw, ch;
        uint8_t *want, *got;
        level_size(src, l, &sw, &sh);
        level_size(t, steps + l, &cw, &ch);
        snprintf(msg, sizeof msg, "%s at %dx: level %d is the game's level %d (%dx%d, it is %dx%d)",
                 what, factor, steps + l, l, (int)sw, (int)sh, (int)cw, (int)ch);
        check(sw == cw && sh == ch, msg);
        want = read_level(src, l);
        got = read_level(t, steps + l);
        check(!memcmp(want, got, (size_t)sw * sh * 4), msg);
        free(want);
        free(got);
    }
    snprintf(msg, sizeof msg, "%s at %dx: %d levels and no more, mipmapped as asked", what,
             factor, steps + s->levels);
    check(param(t, GL_TEXTURE_MAX_LEVEL) == steps + s->levels - 1
          && param(t, GL_TEXTURE_MIN_FILTER) == (s->min_linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR),
          msg);
    printf("ok: %s at %dx: the game's %d levels under the copy's %d\n", what, factor,
           (int)s->levels, steps);
    G->tex_free(t);
}

/* The game's alpha kept: the same copy's colour as without, and each new
 * level's alpha the game's level 0 as `mag` magnifies it, at repeat. */
static void kept_alpha(int factor, int mag)
{
    enum { N = 16 };
    static uint8_t px[N * N * 4];
    Nv2aSampler s = { 1, (uint8_t)mag, 0, 1, 1, 0, 0 };
    int steps = factor == 2 ? 1 : factor == 4 ? 2 : 3, l, x, y, worst = 0;
    uint32_t src, a, b;
    char msg[160];
    srand(11);
    for (x = 0; x < N * N * 4; x++) px[x] = (uint8_t)rand();
    for (x = 0; x < N * N; x++)                 /* clear, opaque and between */
        px[x * 4 + 3] = (uint8_t)((x * 7) % 5 == 0 ? 0 : (x * 7) % 5 == 1 ? 128 : 255);
    src = G->tex_new(N, N, px, &s);
    a = G->tex_upscale(src, N, N, factor, 0, 1.0f, 0.6f, &s);
    b = G->tex_upscale(src, N, N, factor, 1, 1.0f, 0.6f, &s);
    check(src && a && b, "the kept alpha's copies made");
    for (l = 0; l < steps; l++) {
        int n = (N * factor) >> l, f = factor >> l;
        uint8_t *ga = read_level(a, l), *gb = read_level(b, l);
        for (y = 0; y < n; y++)
            for (x = 0; x < n; x++) {
                const uint8_t *pa = ga + ((size_t)y * n + x) * 4, *pb = gb + ((size_t)y * n + x) * 4;
                double want, gx = (x + 0.5) / f - 0.5, gy = (y + 0.5) / f - 0.5;
                int d;
                if (!mag) {
                    want = px[(((y / f) % N) * N + (x / f) % N) * 4 + 3];
                } else {
                    int ix = (int)floor(gx), iy = (int)floor(gy);
                    double fx = gx - ix, fy = gy - iy;
                    #define A(i, j) (double)px[((((j) + N) % N) * N + (((i) + N) % N)) * 4 + 3]
                    want = (A(ix, iy) * (1 - fx) + A(ix + 1, iy) * fx) * (1 - fy)
                         + (A(ix, iy + 1) * (1 - fx) + A(ix + 1, iy + 1) * fx) * fy;
                    #undef A
                }
                snprintf(msg, sizeof msg, "kept alpha at %dx, %s, level %d: texel %d,%d's colour as without",
                         factor, mag ? "linear" : "nearest", l, x, y);
                check(!memcmp(pa, pb, 3), msg);
                d = abs((int)pb[3] - (int)(want + 0.5));
                if (d > worst) worst = d;
            }
        free(ga);
        free(gb);
    }
    snprintf(msg, sizeof msg, "kept alpha at %dx, %s: the game's alpha as magnified (out by %d at most)",
             factor, mag ? "linear" : "nearest", worst);
    check(worst <= 1, msg);
    printf("ok: %s\n", msg);
    G->tex_free(a); G->tex_free(b); G->tex_free(src);
}

int main(void)
{
    static uint8_t px[32 * 16 * 4], blocks[4096];
    Nv2aSampler lin = { 1, 1, 0, 0, 0, 0, 0 }, near = { 0, 0, 0, 0, 0, 0, 0 };
    Nv2aSampler mip = { 1, 1, 2, 0, 0, 0, 0 }, plain = { 1, 1, 0, 1, 1, 1, 0 };
    uint32_t t, dxt, rt, i, *wide;
    GLint fbo0 = -1, fbo1 = -2, vp0[4], vp1[4];

    if (!gl_backend_context()) return 1;
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    check(G->tex_upscale != NULL, "the device has tex_upscale");

    /* Drawing into a target, as the front half is when the flip comes. */
    rt = G->target_new(48, 40);
    G->target_bind(rt);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo0);
    glGetIntegerv(GL_VIEWPORT, vp0);

    srand(3);
    for (i = 0; i < sizeof px; i++) px[i] = (uint8_t)rand();
    t = G->tex_new(32, 16, px, &lin);
    levels(t, 32, 16, 2, &lin, "RGBA 32x16, linear");
    levels(t, 32, 16, 4, &lin, "RGBA 32x16, linear");
    levels(t, 32, 16, 8, &lin, "RGBA 32x16, linear");
    G->tex_free(t);
    t = G->tex_new(32, 16, px, &near);
    levels(t, 32, 16, 4, &near, "RGBA 32x16, nearest");
    G->tex_free(t);
    t = G->tex_new(32, 16, px, &mip);
    levels(t, 32, 16, 2, &mip, "RGBA 32x16 with the driver's mipmaps");
    levels(t, 32, 16, 8, &mip, "RGBA 32x16 with the driver's mipmaps");
    G->tex_free(t);

    for (i = 0; i < sizeof blocks; i++) blocks[i] = (uint8_t)rand();
    dxt = G->tex_new_dxt(16, 16, 1, blocks, &plain);
    if (dxt) {
        levels(dxt, 16, 16, 2, &plain, "DXT1 16x16");
        levels(dxt, 16, 16, 4, &plain, "DXT1 16x16");
        levels(dxt, 16, 16, 8, &plain, "DXT1 16x16");
        G->tex_free(dxt);
    } else {
        printf("ok: no DXT here (no S3TC), so no DXT copy to check\n");
    }

    ramp(8, 0, 2, 1.0f, 0.6f, "across");
    ramp(0, 8, 2, 1.0f, 0.6f, "down");
    ramp(4, 6, 2, 1.0f, 0.6f, "diagonal");
    ramp(8, 0, 4, 1.0f, 0.6f, "across");
    ramp(0, 8, 4, 1.0f, 0.6f, "down");
    ramp(4, 6, 4, 1.0f, 0.6f, "diagonal");
    ramp(8, 0, 8, 1.0f, 0.6f, "across");
    ramp(0, 8, 8, 1.0f, 0.6f, "down");
    ramp(4, 6, 8, 1.0f, 0.6f, "diagonal");
    ramp(4, 6, 4, 0.5f, 0.6f, "diagonal");
    ramp(4, 6, 4, 1.5f, 0.6f, "diagonal");
    ramp(8, 0, 8, 0.5f, 0.6f, "across");
    ramp(0, 8, 8, 1.5f, 0.6f, "down");
    ramp(4, 6, 4, 1.0f, 1.8f, "diagonal");
    ramp(4, 6, 4, 1.0f, 0.2f, "diagonal");
    ramp(8, 0, 8, 1.0f, 0.2f, "across");
    ramp(0, 8, 8, 1.0f, 1.8f, "down");
    weight(2);
    weight(4);
    weight(8);
    edge(2);
    edge(4);
    edge(8);
    alpha_edge(2);
    alpha_edge(4);
    alpha_edge(8);
    wrap(2);
    wrap(4);
    wrap(8);
    kept_alpha(2, 0);
    kept_alpha(4, 0);
    kept_alpha(2, 1);
    kept_alpha(4, 1);
    kept_alpha(8, 1);

    {   /* The title's own chain: 32x16 has six levels, 16x16 five. */
        static uint8_t chain[(32 * 16 + 16 * 8 + 8 * 4 + 4 * 2 + 2 + 1) * 4];
        Nv2aSampler own = { 1, 1, 2, 1, 1, 0, 6 }, own_dxt = { 1, 1, 2, 1, 1, 0, 5 };
        for (i = 0; i < sizeof chain; i++) chain[i] = (uint8_t)rand();
        t = G->tex_new(32, 16, chain, &own);
        title_chain(t, 32, 16, 2, &own, "RGBA 32x16 with the title's chain");
        title_chain(t, 32, 16, 4, &own, "RGBA 32x16 with the title's chain");
        title_chain(t, 32, 16, 8, &own, "RGBA 32x16 with the title's chain");
        G->tex_free(t);
        dxt = G->tex_new_dxt(16, 16, 1, blocks, &own_dxt);
        if (dxt) {
            title_chain(dxt, 16, 16, 2, &own_dxt, "DXT1 16x16 with the title's chain");
            title_chain(dxt, 16, 16, 4, &own_dxt, "DXT1 16x16 with the title's chain");
            G->tex_free(dxt);
        }
    }

    t = G->tex_new(32, 16, px, &lin);
    check(G->tex_upscale(t, 32, 16, 3, 0, 1.0f, 0.6f, &lin) == 0, "3x is refused");
    check(G->tex_upscale(t, 32, 16, 16, 0, 1.0f, 0.6f, &lin) == 0, "16x is refused");
    check(G->tex_upscale(0, 32, 16, 2, 0, 1.0f, 0.6f, &lin) == 0, "no texture is refused");
    G->tex_free(t);
    {
        GLint max = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
        wide = calloc((size_t)max / 4 + 1, 4);
        t = G->tex_new((uint32_t)max / 4 + 1, 1, wide, &lin);
        check(t && G->tex_upscale(t, (uint32_t)max / 4 + 1, 1, 4, 0, 1.0f, 0.6f, &lin) == 0,
              "a side over the device's largest is refused");
        G->tex_free(t);
        t = G->tex_new((uint32_t)max / 8 + 1, 1, wide, &lin);
        check(t && G->tex_upscale(t, (uint32_t)max / 8 + 1, 1, 8, 0, 1.0f, 0.6f, &lin) == 0,
              "a side over the device's largest is refused at 8x");
        G->tex_free(t);
        free(wide);
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo1);
    glGetIntegerv(GL_VIEWPORT, vp1);
    check(fbo0 == fbo1 && !memcmp(vp0, vp1, sizeof vp0) && vp1[2] == 48 && vp1[3] == 40,
          "the target being drawn into, and its viewport, as they were");
    printf("ok: refused where it should be; the target and viewport left as they were\n");
    G->target_free(rt);
    printf("gl_backend: upscaling's copy passes\n");
    return 0;
}
