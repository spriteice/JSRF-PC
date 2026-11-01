/* The 3D voices placed from their HRTF settings (Audio > Audio effects,
 * RECOMP_APU_GPFX).
 *
 * DirectSound places a 3D voice by giving the voice processor an HRTF
 * setting for it: a filter of HRTF_NUM_TAPS taps for each ear, in signed
 * bytes, and a delay of up to HRTF_MAX_DELAY_SAMPLES for the far ear.
 * xemu's filter (hrtf_filter_process in apu_state.h) runs that filter, and
 * it dulls the sound, being short and in 8 bits. Its setter also scales
 * each ear's taps to add up to one, which takes away most of the difference
 * in level between the ears, and makes the sound quieter. This keeps the
 * sound as it is, and takes two things from the setting:
 *
 * - Each ear's level: the size of its taps as the game set them, the root
 *   of the sum of their squares. The louder ear stays at full level, so a
 *   sound in front is as loud as with no setting, and the other is turned
 *   down by APU_HRTF_DEPTH of as much as the setting has it quieter, in dB:
 *   three quarters, so that a sound to one side, and each of the game's
 *   steps as it moves, is a little gentler, above all on speakers.
 * - The delay, on the quieter ear, blended between two samples where it
 *   falls between them. The setting's sign says which ear it's on; where
 *   that is the louder one, it goes to the other, and the log says how
 *   often.
 *
 * Both glide to each new setting through two smoothing stages, each taking
 * APU_HRTF_GLIDE_MS, a frame at a time, and within the frame in a straight
 * line. The game moves a sound in steps, a setting at a time, 20 to 30 a
 * second for most moving sounds, and xemu's filter glides to each in about
 * 2 ms, so a turn comes out as many small jumps. Two slower stages carry a
 * sound on between the steps, with no jump or corner, half-way to each
 * setting in about the time the game takes to send the next, and the delay
 * changes gently, so it hardly bends the sound's pitch. A voice's first
 * frame starts where its setting places it.
 * The delay's history is kept in the voice's HrtfFilter as
 * hrtf_filter_process keeps it, so hrtf_filter_clear_history works on it
 * as it is.
 */
#pragma once

#include "apu_state.h"
#include "apu_gpfx.h"

#include <math.h>

/* The history the delay reads: as far back as the most delay, and one more
 * for the blend. */
#define APU_HRTF_BACK        (HRTF_MAX_DELAY_SAMPLES + 1)
typedef char apu_hrtf_history_fits[APU_HRTF_BACK <= HRTF_BUFLEN ? 1 : -1];
/* Close enough to stop gliding: a level within this, 120 dB down, and the
 * delay within a thousandth of a sample. */
#define APU_HRTF_ARRIVED     1e-6f
#define APU_HRTF_ITD_ARRIVED 1e-3f
#define APU_HRTF_DB          20.0f   /* dB from a level: APU_HRTF_DB * log10 */
/* Each of the glide's two stages: the time it takes to go all but 1/e of
 * the way, so half-way takes about 1.7 times this, all but a tenth 3.9. */
#define APU_HRTF_GLIDE_MS    25.0f
/* How much of the setting's difference in level between the ears is kept,
 * in dB: the far ear is turned down by this much of what the setting has. */
#define APU_HRTF_DEPTH       0.75f
#define APU_HRTF_RATE        48000.0f   /* samples a second */
#define APU_HRTF_MS          1000.0f

/* A 3D voice's ears: their levels as its last frame ended, the glide's
 * first stage for them and for the delay (the voice's HrtfFilter keeps the
 * second, itd_cur), the setting it last had, for the log, and whether it
 * has played a frame since it started. */
typedef struct {
    float level[2], level_first[2], late_first;
    float set[2], set_late;
    int started;
} ApuHrtfEars;

/* Each of the glide's stages' step for a frame. */
static inline float apu_hrtf_frame_step(void)
{
    static float step = -1.0f;
    if (step < 0.0f)
        step = 1.0f - expf(-(float)HRTF_SAMPLES_PER_FRAME * APU_HRTF_MS
                           / (APU_HRTF_RATE * APU_HRTF_GLIDE_MS));
    return step;
}

/* A frame of the glide from `now` to `to`, its first stage in *first, by
 * `step`: where it ends the frame. Each stage lands on `to` once it's
 * within `arrived`, or once its step is too small to move it at all, as it
 * gets near a level of 1 with a slow glide; the first stage first, so that
 * the second comes all the way. */
static inline float apu_hrtf_glide(float *first, float now, float to, float step,
                                   float arrived)
{
    float next = *first + step * (to - *first);
    *first = fabsf(to - *first) <= arrived || next == *first ? to : next;
    next = now + step * (*first - now);
    if (*first == to && (fabsf(to - now) <= arrived || next == now))
        return to;
    return next;
}

/* A level in dB, for the log, no lower than APU_HRTF_ARRIVED's. */
static inline float apu_hrtf_db(float level)
{
    return APU_HRTF_DB * log10f(fmaxf(level, APU_HRTF_ARRIVED));
}

/* Where the setting `hrir` and `itd` places a voice: each ear's level, from
 * 0 to 1, with APU_HRTF_DEPTH of the setting's difference between them,
 * into level[], and the delay, in samples, into *late, the left ear
 * late if it's positive, as hrtf_filter_process takes it. Also, for the
 * log, into *placed. */
static inline void apu_hrtf_target(const float hrir[2][HRTF_NUM_TAPS], float itd,
                                   float level[2], float *late, ApuGpfxPlaced *placed)
{
    float size[2], louder, delay;

    for (int ch = 0; ch < 2; ch++) {
        size[ch] = 0.0f;
        for (int k = 0; k < HRTF_NUM_TAPS; k++)
            size[ch] += hrir[ch][k] * hrir[ch][k];
    }
    louder = fmaxf(size[0], size[1]);
    for (int ch = 0; ch < 2; ch++)
        level[ch] = louder > 0.0f ? powf(sqrtf(size[ch] / louder), APU_HRTF_DEPTH) : 1.0f;
    delay = fminf(fabsf(itd), (float)HRTF_MAX_DELAY_SAMPLES);
    *late = size[0] < size[1] ? delay : size[1] < size[0] ? -delay
          : fmaxf(-delay, fminf(itd, delay));

    placed->far_db = -apu_hrtf_db(fminf(level[0], level[1]));
    placed->side = level[0] < level[1] ? -1 : level[1] < level[0] ? 1 : 0;
    placed->late = delay;
    placed->moved = delay > 0.0f && (itd > 0.0f) != (*late > 0.0f);
}

/* One frame of a 3D voice, `io`, placed by its setting, `hrir` and `itd`, in
 * place: the left ear's sound into io[][0] from io[][0], the right's into
 * io[][1] from io[][1]. `f` keeps the delay's history and where it is, and
 * `e` the ears' levels. What the setting came to, for the log, goes into
 * *placed. */
static inline void apu_hrtf_place(HrtfFilter *f, ApuHrtfEars *e,
                                  const float hrir[2][HRTF_NUM_TAPS], float itd,
                                  float io[][2], ApuGpfxPlaced *placed)
{
    const int frame = HRTF_SAMPLES_PER_FRAME;
    const float step = apu_hrtf_frame_step(), per_sample = 1.0f / (float)frame;
    float level[2], late, itd0, itd1;

    apu_hrtf_target(hrir, itd, level, &late, placed);
    placed->first = !e->started;
    if (!e->started) {
        e->started = 1;
        for (int ch = 0; ch < 2; ch++)
            e->level[ch] = e->level_first[ch] = e->set[ch] = level[ch];
        f->itd_cur = e->late_first = e->set_late = late;
    }
    /* For the log: whether the setting changed, and by how much. */
    placed->changed = level[0] != e->set[0] || level[1] != e->set[1] || late != e->set_late;
    placed->change_db = placed->change_late = 0.0f;
    if (placed->changed) {
        for (int ch = 0; ch < 2; ch++) {
            float db = fabsf(apu_hrtf_db(level[ch]) - apu_hrtf_db(e->set[ch]));
            placed->change_db = fmaxf(placed->change_db, db);
            e->set[ch] = level[ch];
        }
        placed->change_late = fabsf(late - e->set_late);
        e->set_late = late;
    }
    itd0 = f->itd_cur;
    itd1 = apu_hrtf_glide(&e->late_first, itd0, late, step, APU_HRTF_ITD_ARRIVED);

    for (int ch = 0; ch < 2; ch++) {
        float *buf = f->ch[ch].buf;
        float line[APU_HRTF_BACK + HRTF_SAMPLES_PER_FRAME];
        const float *x = line + APU_HRTF_BACK;
        /* The left ear is late when the delay is positive, the right when
         * it's negative. */
        const float side = ch == 0 ? 1.0f : -1.0f;
        float g0 = e->level[ch];
        float g1 = apu_hrtf_glide(&e->level_first[ch], g0, level[ch], step, APU_HRTF_ARRIVED);

        /* The history, oldest first: buf[buf_pos] is where the frame's first
         * sample goes, so the sample m before it is m slots back, wrapping. */
        for (int m = 1, at = f->buf_pos - 1; m <= APU_HRTF_BACK; m++, at--) {
            if (at < 0)
                at += HRTF_BUFLEN;
            line[APU_HRTF_BACK - m] = buf[at];
        }
        for (int n = 0; n < frame; n++)
            line[APU_HRTF_BACK + n] = io[n][ch];

        /* Each sample at the level and the delay as they reach it: the
         * ear on time is the sound as it is. */
        for (int n = 0; n < frame; n++) {
            float t = (float)(n + 1) * per_sample;
            float d = side * (itd0 + (itd1 - itd0) * t), g = g0 + (g1 - g0) * t;
            if (d > 0.0f) {
                int di = (int)d;
                float fr = d - (float)di;
                io[n][ch] = g * (x[n - di] * (1.0f - fr) + x[n - di - 1] * fr);
            } else {
                io[n][ch] = g * x[n];
            }
        }

        /* The frame into the history, where hrtf_filter_process would have
         * put it. */
        for (int n = 0, at = f->buf_pos; n < frame; n++, at++) {
            if (at == HRTF_BUFLEN)
                at = 0;
            buf[at] = line[APU_HRTF_BACK + n];
        }
        e->level[ch] = g1;
    }
    f->itd_cur = itd1;
    f->buf_pos = (f->buf_pos + frame) % HRTF_BUFLEN;
}
