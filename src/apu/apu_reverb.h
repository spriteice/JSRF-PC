/* Our own reverb, run from the room the game sets (Audio > Audio effects,
 * RECOMP_APU_GPFX). apu_gpfx.c includes this, once, and runs it on each
 * frame of the mixbins.
 *
 * On the console the GP runs the effects image's reverb on the reverb send,
 * bin 10, and adds its four outputs into the 3D bins, 6 to 9: front left,
 * front right, back left and back right. When the game sets a room,
 * DirectSound works the room out into the reverb's settings and sends them
 * to the GP, and the copy in apu_gpfx.c keeps them. This reverb is built the
 * same way and runs from the same settings, so that a room sounds as the
 * game set it:
 *
 * - The send, halved, through a low-pass at the full rate, then down to half
 *   the rate, 24 kHz, where the rest runs, APU_RV_BLOCK samples a frame.
 * - Fourteen delay lines, as long as the image makes them. The input line,
 *   which every tap of the room's reads from first. Three lines of build-up,
 *   each fed from a tap of the one before, through a low-pass. A feedback
 *   comb for each output: its early reflections. Four lines of late reverb,
 *   each read at its end with two taps of the input line added in, then
 *   mixed by a matrix, with some of the build-up, and written back through a
 *   low-pass. And two long loops of the build-up, through a low-pass each.
 * - Each output is its reflections, its late reverb and a long loop, at the
 *   room's levels, taken back up to 48 kHz and added into its bin.
 *
 * What it does its own way, to sound better at little cost:
 *
 * - On the way down the console keeps every other sample, and on the way up
 *   it plays each twice, which is harsh above 12 kHz. Here each way goes
 *   through a halfband filter, flat to 9 kHz and 75 dB down from 16 kHz.
 * - When the room changes, the console glides its levels a frame at a time,
 *   and fades each tap that moves out, then back in where it goes. Here
 *   every level and filter glides each sample, all but 1/e of the way in
 *   APU_RV_GLIDE_MS, and lands on the room's after APU_RV_GLIDE_FRAMES, as
 *   the console's do. Each tap that moves fades across from where it was to
 *   where it goes, over APU_RV_FADE samples. The filters keep what they hold,
 *   where DirectSound clears them, so there's no click.
 * - It's in floats, and what dies away is let go to zero before it gets so
 *   small that the CPU slows down on it.
 */
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define APU_RV_FRAME       32u       /* the mixbins' samples in a frame */
#define APU_RV_IN_RATE     48000.0f  /* and a second */
#define APU_RV_HALF        2u        /* the reverb runs at half that rate */
#define APU_RV_BLOCK       (APU_RV_FRAME / APU_RV_HALF)
#define APU_RV_RATE        (APU_RV_IN_RATE / APU_RV_HALF)
#define APU_RV_MS          1000.0f
#define APU_RV_OUTS        4u        /* front left, front right, back left, back right */
#define APU_RV_LINES       14u
#define APU_RV_POOL        0x18000u  /* the samples the lines can have between them: 384 KB */
#define APU_RV_SAMPLE_BYTES 4u       /* a line's sample, in the settings' bytes */
#define APU_RV_ALIGN       4u        /* a tap starts on this many samples, as the console reads them */
#define APU_RV_ALIGN_BYTES (APU_RV_ALIGN * APU_RV_SAMPLE_BYTES)

/* A DSP word, of 24 bits, as a fraction. */
#define APU_RV_WORD        0xFFFFFFu
#define APU_RV_WORD_SIGN   0x800000u
#define APU_RV_WORD_ONE    8388608.0f

#define APU_RV_SEND_LEVEL  0.5f      /* the send is halved first */
#define APU_RV_GAIN_SCALE  128.0f    /* the outputs' levels are kept at 1/128 of theirs */
#define APU_RV_LOOP_SHARE  0.5f      /* each output has half its long loop */

#define APU_RV_GLIDE_MS    23.0f     /* a level glides all but 1/e of the way in this */
#define APU_RV_GLIDE_FRAMES 336u     /* and is the room's after this many frames: 0.22 s */
#define APU_RV_FADE        128u      /* a tap that moves fades across over this many samples */
#define APU_RV_TINY        1e-20f    /* what dies away to this goes to zero: 400 dB down */

/* The halfband filter each way: APU_RV_HB_SIDE taps each side of the
 * middle one, at odd distances from it (the others are zero in a halfband),
 * from a sinc in a Kaiser window. */
#define APU_RV_HB_SIDE     8u
#define APU_RV_HB_BETA     7.0
#define APU_RV_HB_MID      0.5f      /* the middle tap */
#define APU_RV_HB_SIDE_SUM 0.25      /* each side's taps add to this, for a gain of 1 */
#define APU_RV_HB_DOWN_KEEP (4u * APU_RV_HB_SIDE - 2u)  /* full-rate samples kept from a frame */
#define APU_RV_HB_UP_KEEP  (2u * APU_RV_HB_SIDE - 1u)   /* and half-rate ones */
#define APU_RV_PI          3.14159265358979323846
#define APU_RV_I0_DONE     1e-12     /* the Bessel series' last term, against its sum */

typedef char apu_rv_fade_in_blocks[APU_RV_FADE % APU_RV_BLOCK == 0 ? 1 : -1];

/* The reverb's settings, words of its state. Delays are in bytes, four to a
 * sample; levels are fractions. DirectSound writes words APU_RV_W_REFL to
 * APU_RV_W_ROOM_END for a room; the image fixes the rest. */
enum {
    APU_RV_W_SIZE = 43,        /* line k's length in bytes, less one, at APU_RV_W_SIZE + 2k */
    APU_RV_W_REFL = 70,        /* the reflections' taps on the input line: one shared, then each output's */
    APU_RV_W_INJECT = 75,      /* the late reverb's taps on the input line start here */
    APU_RV_W_BUILD = 76,       /* the build-up's taps: two on the input line, then two on each of its lines */
    APU_RV_W_COMB = 84,        /* each comb's delay */
    APU_RV_W_LOOP = 88,        /* the long loops' delay */
    APU_RV_W_INJECT_LEVEL = 89,/* the levels of the late reverb's taps, two to a line */
    APU_RV_W_LOOP_IN = 97,     /* the build-up's level into the long loops */
    APU_RV_W_LATE_IN = 98,     /* and into the late reverb */
    APU_RV_W_REFL_GAIN = 99,   /* each output's reflections' level */
    APU_RV_W_LATE_GAIN = 103,  /* the late reverb's */
    APU_RV_W_LOOP_GAIN = 104,  /* the long loops' */
    APU_RV_W_FILTER = 106,     /* the low-passes, from the input's: what it holds, its feedback, its input's level */
    APU_RV_W_ROOM_END = 135,
    APU_RV_W_MATRIX = 136,     /* the late reverb's matrix: row k, column j at APU_RV_W_MATRIX_MID - k + j */
    APU_RV_W_MATRIX_MID = 139,
    APU_RV_W_COMB_FB = 143,    /* each comb's feedback */
    APU_RV_W_INJECT_AT = 147,  /* where the late reverb's taps are, after APU_RV_W_INJECT */
    APU_RV_W_LATE_LEN = 155,   /* where each late line is read */
    APU_RV_WORDS = 159         /* the words read */
};
#define APU_RV_FILTER_WORDS 3u
#define APU_RV_INJECTS      (2u * APU_RV_OUTS)
#define APU_RV_MATRIX_WORDS (2u * APU_RV_OUTS - 1u)
#define APU_RV_BUILDS       3u   /* the build-up's lines */

/* The lines. */
enum {
    APU_RV_L_IN = 0,
    APU_RV_L_BUILD = 1,                         /* APU_RV_BUILDS of them */
    APU_RV_L_COMB = APU_RV_L_BUILD + APU_RV_BUILDS,
    APU_RV_L_LATE = APU_RV_L_COMB + APU_RV_OUTS,
    APU_RV_L_LOOP = APU_RV_L_LATE + APU_RV_OUTS /* two */
};

/* The taps, each on one line. */
enum {
    APU_RV_T_REFL = 0,                          /* the shared reflections' tap, then each output's */
    APU_RV_T_INJECT = APU_RV_T_REFL + 1 + APU_RV_OUTS,
    APU_RV_T_BUILD = APU_RV_T_INJECT + APU_RV_INJECTS,  /* two on each line, from the input line */
    APU_RV_T_COMB = APU_RV_T_BUILD + 2 * (1 + APU_RV_BUILDS),
    APU_RV_T_LATE = APU_RV_T_COMB + APU_RV_OUTS,
    APU_RV_T_LOOP = APU_RV_T_LATE + APU_RV_OUTS,  /* two */
    APU_RV_TAPS = APU_RV_T_LOOP + 2
};

/* The low-passes after the input's: the build-up's, the late reverb's and
 * the long loops'. */
enum {
    APU_RV_LP_BUILD = 0,
    APU_RV_LP_LATE = APU_RV_LP_BUILD + APU_RV_BUILDS,
    APU_RV_LP_LOOP = APU_RV_LP_LATE + APU_RV_OUTS,
    APU_RV_LOWPASSES = APU_RV_LP_LOOP + 2
};

/* The levels, which glide: the input low-pass's first, which runs at the
 * full rate, then the rest. */
enum {
    APU_RV_P_IN_FB = 0,
    APU_RV_P_IN_GAIN,
    APU_RV_P_NET,
    APU_RV_P_FB = APU_RV_P_NET,                 /* each low-pass's feedback */
    APU_RV_P_GAIN = APU_RV_P_FB + APU_RV_LOWPASSES,     /* and its input's level */
    APU_RV_P_INJECT = APU_RV_P_GAIN + APU_RV_LOWPASSES,
    APU_RV_P_LOOP_IN = APU_RV_P_INJECT + APU_RV_INJECTS,
    APU_RV_P_LATE_IN,
    APU_RV_P_REFL,
    APU_RV_P_LATE = APU_RV_P_REFL + APU_RV_OUTS,
    APU_RV_P_LOOP,
    APU_RV_P_MATRIX,
    APU_RV_P_COMB_FB = APU_RV_P_MATRIX + APU_RV_MATRIX_WORDS,
    APU_RV_LEVELS = APU_RV_P_COMB_FB + APU_RV_OUTS
};

typedef struct {
    int ready;                                  /* laid out */
    uint32_t seen[APU_RV_WORDS];                /* the settings it runs from */
    struct { uint32_t at, n, w; } line[APU_RV_LINES];  /* where in the pool, how long, where it writes next */
    uint32_t need;                              /* the samples the lines have between them */
    struct { uint32_t now, to, want, at; } tap[APU_RV_TAPS];  /* delays, fading from now to to; at: 0, or how far */
    float cur[APU_RV_LEVELS], target[APU_RV_LEVELS];
    unsigned glide;                             /* frames since the room changed, up to APU_RV_GLIDE_FRAMES */
    float step_in, step;                        /* how far a level glides in a sample, at each rate */
    float lp_in, lp[APU_RV_LOWPASSES];          /* what the low-passes hold */
    float down[APU_RV_HB_DOWN_KEEP + APU_RV_FRAME];
    float up[APU_RV_OUTS][APU_RV_HB_UP_KEEP + APU_RV_BLOCK];
    float hb[APU_RV_HB_SIDE];                   /* the halfband's taps, out from the middle */
    float fade[APU_RV_FADE];                    /* how far across a fade is at each of its samples */
    float pool[APU_RV_POOL];
} ApuRv;

static float apu_rv_frac(uint32_t v)
{
    return (float)((int32_t)((v & APU_RV_WORD) ^ APU_RV_WORD_SIGN) - (int32_t)APU_RV_WORD_SIGN)
           / APU_RV_WORD_ONE;
}

/* The line tap t reads, and where, in bytes, as the settings `w` have it. */
static unsigned apu_rv_tap_line(unsigned t)
{
    if (t < APU_RV_T_BUILD + 2)
        return APU_RV_L_IN;
    if (t < APU_RV_T_COMB)
        return APU_RV_L_BUILD + (t - APU_RV_T_BUILD - 2) / 2;
    if (t < APU_RV_T_LATE)
        return APU_RV_L_COMB + t - APU_RV_T_COMB;
    if (t < APU_RV_T_LOOP)
        return APU_RV_L_LATE + t - APU_RV_T_LATE;
    return APU_RV_L_LOOP + t - APU_RV_T_LOOP;
}

static uint64_t apu_rv_tap_bytes(const uint32_t *w, unsigned t)
{
    if (t < APU_RV_T_INJECT)
        return w[APU_RV_W_REFL + t - APU_RV_T_REFL];
    if (t < APU_RV_T_BUILD)
        return (uint64_t)w[APU_RV_W_INJECT] + w[APU_RV_W_INJECT_AT + t - APU_RV_T_INJECT];
    if (t < APU_RV_T_COMB)
        return w[APU_RV_W_BUILD + t - APU_RV_T_BUILD];
    if (t < APU_RV_T_LATE)
        return w[APU_RV_W_COMB + t - APU_RV_T_COMB];
    if (t < APU_RV_T_LOOP)
        return w[APU_RV_W_LATE_LEN + t - APU_RV_T_LATE];
    return w[APU_RV_W_LOOP];
}

/* A delay in bytes, in samples on a line of n: on APU_RV_ALIGN samples, as
 * the console reads it, and at least a block, which is written before it's
 * read. */
static uint32_t apu_rv_delay(uint64_t bytes, uint32_t n)
{
    uint64_t d = (bytes + APU_RV_ALIGN_BYTES - 1) / APU_RV_ALIGN_BYTES * APU_RV_ALIGN;
    return d < APU_RV_BLOCK ? APU_RV_BLOCK : d > n ? n : (uint32_t)d;
}

/* Line k's length in samples, as the settings `w` have it. */
static uint32_t apu_rv_line_samples(const uint32_t *w, unsigned k)
{
    return (uint32_t)(((uint64_t)w[APU_RV_W_SIZE + 2 * k] + 1) / APU_RV_SAMPLE_BYTES);
}

/* The samples the lines need between them, or 0 if one is shorter than a
 * block. */
static uint32_t apu_rv_needs(const uint32_t *w)
{
    uint64_t sum = 0;
    unsigned k;
    for (k = 0; k < APU_RV_LINES; k++) {
        uint32_t n = apu_rv_line_samples(w, k);
        if (n < APU_RV_BLOCK)
            return 0;
        sum += n;
    }
    return sum > UINT32_MAX ? UINT32_MAX : (uint32_t)sum;
}

/* Whether the settings `w` have the lines the reverb was laid out with. */
static int apu_rv_same_lines(const ApuRv *rv, const uint32_t *w)
{
    unsigned k;
    for (k = 0; k < APU_RV_LINES; k++)
        if (w[APU_RV_W_SIZE + 2 * k] != rv->seen[APU_RV_W_SIZE + 2 * k])
            return 0;
    return 1;
}

/* The room the settings `w` set: each level's target, and each tap's. */
static void apu_rv_targets(ApuRv *rv, const uint32_t *w)
{
    unsigned i, t;

    rv->target[APU_RV_P_IN_FB] = apu_rv_frac(w[APU_RV_W_FILTER + 1]);
    rv->target[APU_RV_P_IN_GAIN] = apu_rv_frac(w[APU_RV_W_FILTER + 2]);
    for (i = 0; i < APU_RV_LOWPASSES; i++) {
        unsigned at = APU_RV_W_FILTER + APU_RV_FILTER_WORDS * (1 + i);
        rv->target[APU_RV_P_FB + i] = apu_rv_frac(w[at + 1]);
        rv->target[APU_RV_P_GAIN + i] = apu_rv_frac(w[at + 2]);
    }
    for (i = 0; i < APU_RV_INJECTS; i++)
        rv->target[APU_RV_P_INJECT + i] = apu_rv_frac(w[APU_RV_W_INJECT_LEVEL + i]);
    rv->target[APU_RV_P_LOOP_IN] = apu_rv_frac(w[APU_RV_W_LOOP_IN]);
    rv->target[APU_RV_P_LATE_IN] = apu_rv_frac(w[APU_RV_W_LATE_IN]);
    for (i = 0; i < APU_RV_OUTS; i++) {
        rv->target[APU_RV_P_REFL + i] = apu_rv_frac(w[APU_RV_W_REFL_GAIN + i]);
        rv->target[APU_RV_P_COMB_FB + i] = apu_rv_frac(w[APU_RV_W_COMB_FB + i]);
    }
    rv->target[APU_RV_P_LATE] = apu_rv_frac(w[APU_RV_W_LATE_GAIN]);
    rv->target[APU_RV_P_LOOP] = apu_rv_frac(w[APU_RV_W_LOOP_GAIN]);
    for (i = 0; i < APU_RV_MATRIX_WORDS; i++)
        rv->target[APU_RV_P_MATRIX + i] = apu_rv_frac(w[APU_RV_W_MATRIX + i]);
    for (t = 0; t < APU_RV_TAPS; t++)
        rv->tap[t].want = apu_rv_delay(apu_rv_tap_bytes(w, t),
                                       rv->line[apu_rv_tap_line(t)].n);
    memcpy(rv->seen, w, sizeof rv->seen);
}

/* The Bessel function I0, by its series, for the Kaiser window. */
static double apu_rv_i0(double x)
{
    double sum = 1.0, term = 1.0;
    unsigned k;
    for (k = 1; term > APU_RV_I0_DONE * sum; k++) {
        double h = x / (2.0 * k);
        term *= h * h;
        sum += term;
    }
    return sum;
}

/* Laid out from the settings `w`, APU_RV_WORDS of them, in the room they
 * set, with nothing in its lines. 0 if the lines don't fit
 * (apu_rv_needs). */
static int apu_rv_start(ApuRv *rv, const uint32_t *w)
{
    uint32_t need = apu_rv_needs(w), at = 0;
    double side[APU_RV_HB_SIDE], sum = 0.0;
    unsigned k, t;

    memset(rv, 0, sizeof *rv);
    if (!need || need > APU_RV_POOL)
        return 0;
    for (k = 0; k < APU_RV_LINES; k++) {
        rv->line[k].at = at;
        rv->line[k].n = apu_rv_line_samples(w, k);
        at += rv->line[k].n;
    }
    rv->need = need;
    apu_rv_targets(rv, w);
    memcpy(rv->cur, rv->target, sizeof rv->cur);
    for (t = 0; t < APU_RV_TAPS; t++)
        rv->tap[t].now = rv->tap[t].want;
    rv->glide = APU_RV_GLIDE_FRAMES;
    rv->step_in = 1.0f - expf(-APU_RV_MS / (APU_RV_IN_RATE * APU_RV_GLIDE_MS));
    rv->step = 1.0f - expf(-APU_RV_MS / (APU_RV_RATE * APU_RV_GLIDE_MS));
    for (k = 0; k < APU_RV_HB_SIDE; k++) {
        double d = 2.0 * k + 1.0, r = d / (2.0 * APU_RV_HB_SIDE);
        side[k] = (k % 2 ? -1.0 : 1.0) / (APU_RV_PI * d)
                * apu_rv_i0(APU_RV_HB_BETA * sqrt(1.0 - r * r)) / apu_rv_i0(APU_RV_HB_BETA);
        sum += side[k];
    }
    for (k = 0; k < APU_RV_HB_SIDE; k++)
        rv->hb[k] = (float)(side[k] * APU_RV_HB_SIDE_SUM / sum);
    for (k = 0; k < APU_RV_FADE; k++)    /* a raised cosine, ending at 1 */
        rv->fade[k] = (float)(0.5 - 0.5 * cos(APU_RV_PI * (k + 1) / APU_RV_FADE));
    rv->ready = 1;
    return 1;
}

/* The settings `w` again, a frame on: 1 if they set another room, which the
 * reverb then glides to. */
static int apu_rv_room(ApuRv *rv, const uint32_t *w)
{
    if (!memcmp(rv->seen, w, sizeof rv->seen))
        return 0;
    apu_rv_targets(rv, w);
    rv->glide = 0;
    return 1;
}

static float apu_rv_settle(float v)
{
    return fabsf(v) < APU_RV_TINY ? 0.0f : v;
}

/* A block from line l, d samples back from where it writes next. */
static void apu_rv_read(const ApuRv *rv, unsigned l, uint32_t d, float *out)
{
    const float *buf = rv->pool + rv->line[l].at;
    uint32_t n = rv->line[l].n, at = (rv->line[l].w + n - d) % n, first = n - at;

    if (first >= APU_RV_BLOCK) {
        memcpy(out, buf + at, APU_RV_BLOCK * sizeof *out);
    } else {
        memcpy(out, buf + at, first * sizeof *out);
        memcpy(out + first, buf, (APU_RV_BLOCK - first) * sizeof *out);
    }
}

/* Tap t's block, fading across to where the room wants it if it moved. */
static void apu_rv_tap(ApuRv *rv, unsigned t, float *out)
{
    unsigned l = apu_rv_tap_line(t), i;
    float to[APU_RV_BLOCK];

    if (!rv->tap[t].at && rv->tap[t].want != rv->tap[t].now) {
        rv->tap[t].to = rv->tap[t].want;
        rv->tap[t].at = 1;
    }
    apu_rv_read(rv, l, rv->tap[t].now, out);
    if (!rv->tap[t].at)
        return;
    apu_rv_read(rv, l, rv->tap[t].to, to);
    for (i = 0; i < APU_RV_BLOCK; i++)
        out[i] += (to[i] - out[i]) * rv->fade[rv->tap[t].at - 1 + i];
    rv->tap[t].at += APU_RV_BLOCK;
    if (rv->tap[t].at > APU_RV_FADE) {
        rv->tap[t].now = rv->tap[t].to;
        rv->tap[t].at = 0;
    }
}

static void apu_rv_write(ApuRv *rv, unsigned l, const float *in)
{
    float *buf = rv->pool + rv->line[l].at;
    uint32_t n = rv->line[l].n, w = rv->line[l].w, i;

    for (i = 0; i < APU_RV_BLOCK; i++) {
        buf[w] = apu_rv_settle(in[i]);
        if (++w == n)
            w = 0;
    }
    rv->line[l].w = w;
}

static void apu_rv_glide(float *cur, const float *target, unsigned first, unsigned end,
                         float step)
{
    unsigned i;
    for (i = first; i < end; i++)
        cur[i] += (target[i] - cur[i]) * step;
}

/* One frame: the send's APU_RV_FRAME samples in, and the reverb added into
 * its APU_RV_OUTS bins, the first at `bins` and each `stride` floats after
 * the last. Returns the most it added to a sample. */
static float apu_rv_frame(ApuRv *rv, const float *send, float *bins, size_t stride)
{
    float d[APU_RV_TAPS][APU_RV_BLOCK], out[APU_RV_OUTS][APU_RV_BLOCK];
    float u[APU_RV_BLOCK], e[APU_RV_OUTS][APU_RV_BLOCK], g[APU_RV_OUTS][APU_RV_BLOCK];
    float build[APU_RV_BUILDS][APU_RV_BLOCK], loop[2][APU_RV_BLOCK], most = 0.0f;
    float *c = rv->cur, *h = rv->down, y;
    const int gliding = rv->glide < APU_RV_GLIDE_FRAMES;
    unsigned i, k, j, n, t;

    /* In: halved, the low-pass, and down to half the rate through the
     * halfband, whose middle tap for block sample m is full-rate sample
     * 2m + 2 * APU_RV_HB_SIDE of those kept and this frame's. */
    y = rv->lp_in;
    for (i = 0; i < APU_RV_FRAME; i++) {
        if (gliding)
            apu_rv_glide(c, rv->target, APU_RV_P_IN_FB, APU_RV_P_NET, rv->step_in);
        y = c[APU_RV_P_IN_FB] * y + c[APU_RV_P_IN_GAIN] * (APU_RV_SEND_LEVEL * send[i]);
        h[APU_RV_HB_DOWN_KEEP + i] = y;
    }
    rv->lp_in = apu_rv_settle(y);
    for (n = 0; n < APU_RV_BLOCK; n++) {
        const float *mid = h + 2 * n + 2 * APU_RV_HB_SIDE;
        float v = APU_RV_HB_MID * mid[0];
        for (j = 0; j < APU_RV_HB_SIDE; j++)
            v += rv->hb[j] * (mid[-(int)(2 * j + 1)] + mid[2 * j + 1]);
        u[n] = v;
    }
    memmove(h, h + APU_RV_FRAME, APU_RV_HB_DOWN_KEEP * sizeof *h);

    for (t = 0; t < APU_RV_TAPS; t++)
        apu_rv_tap(rv, t, d[t]);

    for (n = 0; n < APU_RV_BLOCK; n++) {
        float late[APU_RV_OUTS], p, q, mix;
        if (gliding)
            apu_rv_glide(c, rv->target, APU_RV_P_NET, APU_RV_LEVELS, rv->step);
        for (k = 0; k < APU_RV_OUTS; k++) {
            /* The reflections: the shared tap, the output's own, and the
             * comb's feedback. The late reverb: its line's end, with its two
             * taps of the input line. */
            e[k][n] = d[APU_RV_T_REFL][n] + d[APU_RV_T_REFL + 1 + k][n]
                    + c[APU_RV_P_COMB_FB + k] * d[APU_RV_T_COMB + k][n];
            late[k] = d[APU_RV_T_LATE + k][n]
                    + c[APU_RV_P_INJECT + 2 * k] * d[APU_RV_T_INJECT + 2 * k][n]
                    + c[APU_RV_P_INJECT + 2 * k + 1] * d[APU_RV_T_INJECT + 2 * k + 1][n];
        }
        /* The build-up's two sums, of a tap on each of its four lines (the
         * input line's first): p of the second tap on the first and last,
         * and the first on the two between; q of the others. */
        p = d[APU_RV_T_BUILD + 1][n] + d[APU_RV_T_BUILD + 2][n]
          + d[APU_RV_T_BUILD + 4][n] + d[APU_RV_T_BUILD + 7][n];
        q = d[APU_RV_T_BUILD + 0][n] + d[APU_RV_T_BUILD + 3][n]
          + d[APU_RV_T_BUILD + 5][n] + d[APU_RV_T_BUILD + 6][n];
        loop[0][n] = p * c[APU_RV_P_LOOP_IN] + d[APU_RV_T_LOOP][n];
        loop[1][n] = q * c[APU_RV_P_LOOP_IN] + d[APU_RV_T_LOOP + 1][n];
        mix = (p + q) * c[APU_RV_P_LATE_IN];
        /* Each build-up line from its last one's second tap. */
        for (k = 0; k < APU_RV_BUILDS; k++) {
            float *m = &rv->lp[APU_RV_LP_BUILD + k];
            *m = c[APU_RV_P_FB + APU_RV_LP_BUILD + k] * *m
               + c[APU_RV_P_GAIN + APU_RV_LP_BUILD + k] * d[APU_RV_T_BUILD + 2 * k + 1][n];
            build[k][n] = *m;
        }
        for (k = 0; k < 2; k++) {
            float *m = &rv->lp[APU_RV_LP_LOOP + k];
            *m = c[APU_RV_P_FB + APU_RV_LP_LOOP + k] * *m
               + c[APU_RV_P_GAIN + APU_RV_LP_LOOP + k] * loop[k][n];
        }
        for (k = 0; k < APU_RV_OUTS; k++) {
            float *m = &rv->lp[APU_RV_LP_LATE + k], v = mix;
            for (j = 0; j < APU_RV_OUTS; j++)
                v += c[APU_RV_P_MATRIX + (APU_RV_W_MATRIX_MID - APU_RV_W_MATRIX) - k + j] * late[j];
            *m = c[APU_RV_P_FB + APU_RV_LP_LATE + k] * *m + c[APU_RV_P_GAIN + APU_RV_LP_LATE + k] * v;
            g[k][n] = *m;
            out[k][n] = APU_RV_GAIN_SCALE * (c[APU_RV_P_REFL + k] * e[k][n]
                      + c[APU_RV_P_LATE] * late[k]
                      + APU_RV_LOOP_SHARE * c[APU_RV_P_LOOP] * loop[k < 2 ? 0 : 1][n]);
        }
        /* The long loops' low-passes, written back. */
        loop[0][n] = rv->lp[APU_RV_LP_LOOP];
        loop[1][n] = rv->lp[APU_RV_LP_LOOP + 1];
    }
    for (k = 0; k < APU_RV_LOWPASSES; k++)
        rv->lp[k] = apu_rv_settle(rv->lp[k]);

    apu_rv_write(rv, APU_RV_L_IN, u);
    for (k = 0; k < APU_RV_BUILDS; k++)
        apu_rv_write(rv, APU_RV_L_BUILD + k, build[k]);
    for (k = 0; k < APU_RV_OUTS; k++) {
        apu_rv_write(rv, APU_RV_L_COMB + k, e[k]);
        apu_rv_write(rv, APU_RV_L_LATE + k, g[k]);
    }
    apu_rv_write(rv, APU_RV_L_LOOP, loop[0]);
    apu_rv_write(rv, APU_RV_L_LOOP + 1, loop[1]);

    /* Out: back up to the full rate through the halfband, each block sample
     * as it is, and one between each two from the taps either side. */
    for (k = 0; k < APU_RV_OUTS; k++) {
        float *x = rv->up[k], *bin = bins + k * stride;
        memcpy(x + APU_RV_HB_UP_KEEP, out[k], sizeof out[k]);
        for (n = 0; n < APU_RV_BLOCK; n++) {
            const float *mid = x + n + APU_RV_HB_SIDE - 1;
            float between = 0.0f, a, b;
            for (j = 0; j < APU_RV_HB_SIDE; j++)
                between += rv->hb[j] * (mid[-(int)j] + mid[j + 1]);
            a = mid[0];
            b = APU_RV_HALF * between;
            bin[APU_RV_HALF * n] += a;
            bin[APU_RV_HALF * n + 1] += b;
            if (fabsf(a) > most)
                most = fabsf(a);
            if (fabsf(b) > most)
                most = fabsf(b);
        }
        memmove(x, x + APU_RV_BLOCK, APU_RV_HB_UP_KEEP * sizeof *x);
    }

    if (gliding && ++rv->glide == APU_RV_GLIDE_FRAMES)
        memcpy(rv->cur, rv->target, sizeof rv->cur);
    return most;
}
