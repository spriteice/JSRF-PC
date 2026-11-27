/*
 * A picture fitted to one of the game's graffiti sizes (graffiti_fit.h), for
 * JSRF-PC Tools' graffiti page (b3 row 35).
 *
 * The sizes are the game's own: the editor's tables give SS and S 128 x
 * 128, M 384 x 256, L 512 x 256 and XL 1024 x 256
 * (docs/formats/graffiti-save.md, The blocks). Auto's choice by shape, the
 * clear margins and the threshold are from the plan spriteice approved
 * (7:41pm on 2026-11-26, with "S, and SS by hand" and "Fit with clear
 * margins").
 *
 * The resample is separable: across, then down. Each target pixel takes a
 * weighted sum of source pixels, with weights that sum to 1. Shrinking, a
 * source pixel's weight is how much of it the target pixel covers, so a fine
 * pattern shrinks to its mean; enlarging, it is bilinear between the two
 * nearest pixel centres. The sums are of premultiplied colour, so a clear
 * pixel's colour never reaches its neighbours, and the colour is divided by
 * the summed alpha after.
 */
#include "graffiti_fit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *name;
    int width, height;
} sizes[GRAFFITI_SIZES] = {
    { "SS", 128, 128 },
    { "S", 128, 128 },
    { "M", 384, 256 },
    { "L", 512, 256 },
    { "XL", 1024, 256 },
};

/* The sizes Auto picks from, squarest first: SS never (spriteice: "S, and SS
 * by hand"). */
static const int auto_sizes[] = { GRAFFITI_S, GRAFFITI_M, GRAFFITI_L, GRAFFITI_XL };

/* The game's transparency is all or nothing: more than half solid is solid.
 * Alpha is summed as 0 to 255, so exactly half is exact. */
#define SOLID_FROM 127.5f

static int is_size(int size)
{
    return size >= 0 && size < GRAFFITI_SIZES;
}

int graffiti_width(int size)
{
    return is_size(size) ? sizes[size].width : 0;
}

int graffiti_height(int size)
{
    return is_size(size) ? sizes[size].height : 0;
}

const char *graffiti_size_name(int size)
{
    return is_size(size) ? sizes[size].name : "?";
}

static double aspect(int size)
{
    return (double)sizes[size].width / sizes[size].height;
}

int graffiti_auto_size(int w, int h)
{
    if (w <= 0 || h <= 0 || w <= h)
        return GRAFFITI_S;
    double shape = log((double)w / h);
    int best = auto_sizes[0];
    double best_off = fabs(shape - log(aspect(best)));
    for (size_t i = 1; i < sizeof auto_sizes / sizeof auto_sizes[0]; i++) {
        double off = fabs(shape - log(aspect(auto_sizes[i])));
        if (off < best_off) {   /* a tie keeps the squarer */
            best = auto_sizes[i];
            best_off = off;
        }
    }
    return best;
}

graffiti_box graffiti_crop_default(int w, int h, int size)
{
    graffiti_box box = { 0, 0, w, h };
    if (!is_size(size) || w <= 0 || h <= 0)
        return box;
    double a = aspect(size);
    if ((double)w / h > a) {
        box.w = h * a;
        box.x = (w - box.w) / 2;
    } else {
        box.h = w / a;
        box.y = (h - box.h) / 2;
    }
    return box;
}

graffiti_box graffiti_crop_clamp(graffiti_box box, int w, int h, int size)
{
    if (!is_size(size) || w <= 0 || h <= 0)
        return graffiti_crop_default(w, h, size);
    double a = aspect(size);
    double bw = box.w, bh;
    if (!(bw >= 1))                 /* also catches NaN */
        bw = 1;
    if (bw > w)
        bw = w;
    bh = bw / a;
    if (bh > h) {
        bh = h;
        bw = bh * a;
    }
    if (bh < 1) {                   /* a picture a pixel tall: the box is too */
        bh = 1;
        bw = bh * a < w ? bh * a : w;
    }
    double x = box.x, y = box.y;
    if (!(x >= 0)) x = 0;
    if (!(y >= 0)) y = 0;
    if (x > w - bw) x = w - bw;
    if (y > h - bh) y = h - bh;
    graffiti_box out = { x, y, bw, bh };
    return out;
}

/* The source pixels each of n target pixels takes, along one axis: a
 * stretch of the source from `from`, `span` pixels long, with `limit`
 * pixels in all. Target pixel i takes count[i] pixels from first[i], with
 * weights at weight[i * per], summing to 1. Only pixels the stretch touches
 * are taken, so a crop never shows what is outside its box. */
typedef struct taps {
    int n, per;
    int *first, *count;
    float *weight;
} taps;

static void taps_free(taps *t)
{
    free(t->first);
    free(t->count);
    free(t->weight);
}

static int taps_make(taps *t, double from, double span, int n, int limit)
{
    double step = span / n;         /* source pixels a target pixel */
    int lo = (int)floor(from), hi = (int)ceil(from + span);
    if (lo < 0) lo = 0;
    if (hi > limit) hi = limit;
    if (hi <= lo) hi = lo + 1;
    t->n = n;
    t->per = step > 1 ? (int)ceil(step) + 2 : 2;
    t->first = malloc(sizeof *t->first * n);
    t->count = malloc(sizeof *t->count * n);
    t->weight = calloc((size_t)n * t->per, sizeof *t->weight);
    if (!t->first || !t->count || !t->weight) {
        taps_free(t);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        float *w = t->weight + (size_t)i * t->per;
        if (step >= 1) {
            double a = from + i * step, b = a + step;
            if (a < 0) a = 0;
            if (b > limit) b = limit;
            int k0 = (int)floor(a), k1 = (int)ceil(b);
            if (k1 <= k0)
                k1 = k0 + 1;
            if (k1 - k0 > t->per)
                k1 = k0 + t->per;
            double sum = 0;
            for (int k = k0; k < k1; k++) {
                double lo = a > k ? a : k, hi = b < k + 1 ? b : k + 1;
                double c = hi > lo ? hi - lo : 0;
                w[k - k0] = (float)c;
                sum += c;
            }
            if (k0 >= limit)
                k0 = limit - 1;
            t->first[i] = k0;
            t->count[i] = k1 - k0;
            if (sum > 0) {
                for (int k = 0; k < k1 - k0; k++)
                    w[k] = (float)(w[k] / sum);
            } else {
                w[0] = 1;
                t->count[i] = 1;
            }
        } else {
            double c = from + (i + 0.5) * step - 0.5;
            int k = (int)floor(c);
            float f = (float)(c - k);
            if (k < lo) {
                k = lo;
                f = 0;
            }
            if (k >= hi - 1) {
                k = hi - 1;
                f = 0;
            }
            t->first[i] = k;
            t->count[i] = f > 0 ? 2 : 1;
            w[0] = 1 - f;
            w[1] = f;
        }
    }
    return 0;
}

/* src's box (x, y, bw, bh) resampled into the dw x dh block of out at
 * (dx, dy), out being ow pixels across, premultiplied in four floats a
 * pixel, then into RGBA: thresholded to the game's clear or solid, or with
 * alpha kept as it comes out. */
static int resample(const uint8_t *src, int w, int h, graffiti_box box,
                    uint8_t *out, int ow, int dx, int dy, int dw, int dh, int threshold)
{
    taps across, down;
    if (taps_make(&across, box.x, box.w, dw, w))
        return -1;
    if (taps_make(&down, box.y, box.h, dh, h)) {
        taps_free(&across);
        return -1;
    }
    int ylo = h, yhi = 0;
    for (int j = 0; j < dh; j++) {
        if (down.first[j] < ylo) ylo = down.first[j];
        if (down.first[j] + down.count[j] > yhi) yhi = down.first[j] + down.count[j];
    }
    int rows = yhi - ylo;
    float *mid = malloc(sizeof *mid * 4 * (size_t)dw * (size_t)rows);
    if (!mid) {
        taps_free(&across);
        taps_free(&down);
        return -1;
    }
    for (int y = 0; y < rows; y++) {
        const uint8_t *row = src + (size_t)(ylo + y) * w * 4;
        float *m = mid + (size_t)y * dw * 4;
        for (int i = 0; i < dw; i++) {
            const float *wt = across.weight + (size_t)i * across.per;
            float r = 0, g = 0, b = 0, a = 0;
            for (int k = 0; k < across.count[i]; k++) {
                const uint8_t *p = row + (size_t)(across.first[i] + k) * 4;
                float pa = p[3] * wt[k];
                r += p[0] * pa;
                g += p[1] * pa;
                b += p[2] * pa;
                a += pa;
            }
            m[i * 4 + 0] = r;
            m[i * 4 + 1] = g;
            m[i * 4 + 2] = b;
            m[i * 4 + 3] = a;
        }
    }
    for (int j = 0; j < dh; j++) {
        const float *wt = down.weight + (size_t)j * down.per;
        uint8_t *o = out + ((size_t)(dy + j) * ow + dx) * 4;
        for (int i = 0; i < dw; i++, o += 4) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int k = 0; k < down.count[j]; k++) {
                const float *m = mid + ((size_t)(down.first[j] - ylo + k) * dw + i) * 4;
                r += m[0] * wt[k];
                g += m[1] * wt[k];
                b += m[2] * wt[k];
                a += m[3] * wt[k];
            }
            if (threshold ? a > SOLID_FROM : a >= 0.5f) {
                float v[3] = { r / a, g / a, b / a };
                for (int c = 0; c < 3; c++)
                    o[c] = (uint8_t)(v[c] >= 255 ? 255 : v[c] <= 0 ? 0 : v[c] + 0.5f);
                o[3] = threshold || a >= 254.5f ? 255 : (uint8_t)(a + 0.5f);
            } else {
                memset(o, 0, 4);
            }
        }
    }
    free(mid);
    taps_free(&across);
    taps_free(&down);
    return 0;
}

int graffiti_fit(const uint8_t *rgba, int w, int h, int size, int mode,
                 const graffiti_box *crop, uint8_t *out)
{
    if (!rgba || !out || !is_size(size) || w <= 0 || h <= 0)
        return -1;
    int ow = sizes[size].width, oh = sizes[size].height;
    graffiti_box box = { 0, 0, w, h };
    int dx = 0, dy = 0, dw = ow, dh = oh;
    memset(out, 0, (size_t)ow * oh * 4);
    switch (mode) {
    case GRAFFITI_FIT_MARGINS: {
        double scale = fmin((double)ow / w, (double)oh / h);
        dw = (int)lround(w * scale);
        dh = (int)lround(h * scale);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        if (dw > ow) dw = ow;
        if (dh > oh) dh = oh;
        dx = (ow - dw) / 2;
        dy = (oh - dh) / 2;
        break;
    }
    case GRAFFITI_FIT_CROP:
        box = graffiti_crop_clamp(crop ? *crop : graffiti_crop_default(w, h, size), w, h, size);
        break;
    case GRAFFITI_FIT_STRETCH:
        break;
    default:
        return -1;
    }
    return resample(rgba, w, h, box, out, ow, dx, dy, dw, dh, 1);
}

int graffiti_scale(const uint8_t *rgba, int w, int h, uint8_t *out, int ow, int oh)
{
    if (!rgba || !out || w <= 0 || h <= 0 || ow <= 0 || oh <= 0)
        return -1;
    graffiti_box box = { 0, 0, w, h };
    return resample(rgba, w, h, box, out, ow, 0, 0, ow, oh, 0);
}

long graffiti_white_to_clear(uint8_t *rgba, int w, int h, int tolerance, int everywhere)
{
    if (!rgba || w <= 0 || h <= 0)
        return 0;
    if (tolerance < 0) tolerance = 0;
    if (tolerance > 255) tolerance = 255;
    const int from = 255 - tolerance;
    const size_t n = (size_t)w * h;
    long made = 0;
#define WHITE(p) ((p)[3] && (p)[0] >= from && (p)[1] >= from && (p)[2] >= from)
    if (everywhere) {
        for (size_t i = 0; i < n; i++) {
            uint8_t *p = rgba + i * 4;
            if (WHITE(p)) {
                p[3] = 0;
                made++;
            }
        }
        return made;
    }
    /* A fill from the edge: each pixel is put on the stack once, when first
     * seen, so the stack never holds more than the picture. */
    uint8_t *seen = calloc(n, 1);
    size_t cap = 1024, top = 0;
    size_t *stack = malloc(sizeof *stack * cap);
    if (!seen || !stack) {
        free(seen);
        free(stack);
        return -1;
    }
#define PASSABLE(i) (rgba[(i) * 4 + 3] == 0 || WHITE(rgba + (i) * 4))
#define PUSH(i) do { \
        size_t i_ = (i); \
        if (!seen[i_] && PASSABLE(i_)) { \
            seen[i_] = 1; \
            if (top == cap) { \
                size_t *more = realloc(stack, sizeof *stack * cap * 2); \
                if (!more) { made = -1; goto out; } \
                stack = more; \
                cap *= 2; \
            } \
            stack[top++] = i_; \
        } \
    } while (0)
    for (int x = 0; x < w; x++) {
        PUSH((size_t)x);
        PUSH((size_t)(h - 1) * w + x);
    }
    for (int y = 0; y < h; y++) {
        PUSH((size_t)y * w);
        PUSH((size_t)y * w + w - 1);
    }
    while (top) {
        size_t i = stack[--top];
        uint8_t *p = rgba + i * 4;
        if (p[3]) {
            p[3] = 0;
            made++;
        }
        int x = (int)(i % w), y = (int)(i / w);
        if (x > 0) PUSH(i - 1);
        if (x < w - 1) PUSH(i + 1);
        if (y > 0) PUSH(i - w);
        if (y < h - 1) PUSH(i + w);
    }
out:
#undef PUSH
#undef PASSABLE
#undef WHITE
    free(seen);
    free(stack);
    return made;
}
