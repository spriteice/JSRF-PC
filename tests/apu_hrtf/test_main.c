/* Run with: python tests/apu_hrtf/run.py
 *
 * Placing the 3D voices from their HRTF settings (src/apu/apu_hrtf.h), with
 * settings made the way DirectSound gives them: each ear's taps, and a
 * delay. Each case prints a "check" line for each check, which run.py
 * reads, and "cost" its timings, which it shows. The argument picks the
 * case.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "apu_hrtf.h"

#define FRAME       HRTF_SAMPLES_PER_FRAME
#define TAPS        HRTF_NUM_TAPS
#define SECOND      1500             /* frames in a second, at 48 kHz */
#define RATE        48000.0
#define TWO_PI      6.283185307179586
#define UPDATE      25               /* frames between DirectSound's updates: 60 a second */
#define SETTLE      200              /* frames for a glide to arrive */
#define EXACT       1e-6f
#define GLIDE_CLOSE 1e-5f            /* a frame's end against the glide's sum */
#define LATE_CLOSE  0.01f            /* a delay read back from the sound, in samples */
#define DB_CLOSE    1e-3f
#define RAMP        1e-3f            /* the ramp's rise a sample */
#define COST_VOICES 32
#define LONG        300              /* frames: long enough for the history to wrap often */
#define MS          1000.0
#define GLIDE_MS    25.0             /* each of the glide's two stages */
#define HALF_WAY    1.678            /* two stages go half-way in this many of a stage's time */
#define BEND_CLOSE  1e-3f
#define MOVING      400              /* frames for the delay to arrive */
#define DEPTH       0.75f            /* of the setting's difference between the ears, in dB */
#define HALF_DB     4.5154f          /* a half's 6.02 dB, at DEPTH */
#define QUARTER_DB  9.0309f          /* and a quarter's 12.04 */
#define ITD_ARRIVED 1e-3             /* where the first stage lands on the delay */

static int s_failed;

static void check(const char *what, int ok)
{
    printf("check: %s %s\n", what, ok ? "ok" : "FAILED");
    s_failed |= !ok;
}

/* A setting's taps: each ear a short shape scaled to a size, so that its
 * root of the sum of the squares is `left` and `right`. */
static void taps(float h[2][TAPS], float left, float right)
{
    static const float shape[2][4] = { { 0.8f, 0.0f, 0.0f, 0.6f },
                                       { 0.6f, 0.8f, 0.0f, 0.0f } };
    memset(h, 0, sizeof(float) * 2 * TAPS);
    for (int k = 0; k < 4; k++) {
        h[0][k] = left * shape[0][k];
        h[1][k] = right * shape[1][k];
    }
}

/* A voice: its delay's history and its ears, as it starts. */
typedef struct {
    HrtfFilter f;
    ApuHrtfEars e;
    ApuGpfxPlaced placed;
} Voice;

static void start(Voice *v)
{
    hrtf_filter_init(&v->f);
    memset(&v->e, 0, sizeof v->e);
}

static void place(Voice *v, float h[2][TAPS], float itd, float io[FRAME][2])
{
    apu_hrtf_place(&v->f, &v->e, (const float (*)[TAPS])h, itd, io, &v->placed);
}

/* An ear's level, for a setting with its taps `size` times the louder ear's. */
static float played(float size)
{
    return powf(size, DEPTH);
}

static float noise(void)
{
    return (float)rand() / (float)RAND_MAX - 0.5f;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    static Voice voices[COST_VOICES];
    float h[2][TAPS], x[FRAME][2], io[FRAME][2];

    srand(1);
    if (!strcmp(c, "levels")) {           /* each ear's level, from its taps' size */
        static const struct {
            float left, right, size[2], db;
            int side;
            const char *what;
        } k[] = {
            { 1.0f, 0.5f,  { 1.0f, 0.5f }, HALF_DB, 1,
              "the right half the size, three quarters of its dB down" },
            { 0.25f, 1.0f, { 0.25f, 1.0f }, QUARTER_DB, -1, "the left a quarter, the same" },
            { 0.7f, 0.7f,  { 1.0f, 1.0f }, 0.0f, 0, "the same size, both as they are" },
            { 0.0f, 0.0f,  { 1.0f, 1.0f }, 0.0f, 0, "no taps, both as they are" },
            { 1.0f, 0.0f,  { 1.0f, 0.0f }, 120.0f, 1, "no taps on the right, the right silent" },
        };
        for (unsigned j = 0; j < sizeof k / sizeof k[0]; j++) {
            const float want[2] = { played(k[j].size[0]), played(k[j].size[1]) };
            int ok = 1;
            start(&voices[0]);
            taps(h, k[j].left, k[j].right);
            for (int n = 0; n < 4; n++) {
                for (int i = 0; i < FRAME; i++)
                    x[i][0] = x[i][1] = noise();
                memcpy(io, x, sizeof io);
                place(&voices[0], h, 0.0f, io);
                for (int i = 0; i < FRAME; i++)
                    for (int e = 0; e < 2; e++)
                        ok &= want[e] == 1.0f ? io[i][e] == x[i][e]
                            : fabsf(io[i][e] - want[e] * x[i][e]) <= EXACT;
            }
            check(k[j].what, ok && fabsf(voices[0].placed.far_db - k[j].db) < DB_CLOSE
                  && voices[0].placed.side == k[j].side && !voices[0].placed.moved);
        }
    } else if (!strcmp(c, "delay")) {     /* the delay, on the quieter ear */
        static const struct {
            float left, right, itd, late[2];
            int moved;
            const char *what;
        } k[] = {
            { 1.0f, 0.5f, -10.0f, { 0.0f, 10.0f }, 0, "the right quieter and late, as set" },
            { 1.0f, 0.5f, 10.0f, { 0.0f, 10.0f }, 1, "set on the louder left, so the right" },
            { 0.5f, 1.0f, 10.25f, { 10.25f, 0.0f }, 0, "the left, between two samples" },
            { 0.5f, 1.0f, -7.5f, { 7.5f, 0.0f }, 1, "set on the louder right, so the left" },
            { 0.7f, 0.7f, 5.0f, { 5.0f, 0.0f }, 0, "the same size, on the ear set: left" },
            { 0.7f, 0.7f, -5.0f, { 0.0f, 5.0f }, 0, "and right" },
            { 0.5f, 1.0f, 60.0f, { (float)HRTF_MAX_DELAY_SAMPLES, 0.0f }, 0,
              "no later than the most there is" },
        };
        /* Single samples at times all through the frames, the late ear's
         * landing in the same frame, the next, or the one after. */
        static const int at[] = { 0, 31, 40, 95, 160, 223, 300, 351 };
        for (unsigned j = 0; j < sizeof k / sizeof k[0]; j++) {
            float want[16 * FRAME][2] = {{0}}, level[2] = { k[j].left, k[j].right };
            int ok = 1;
            for (int e = 0; e < 2; e++) {
                level[e] = played(level[e] / fmaxf(k[j].left, k[j].right));
                for (unsigned p = 0; p < sizeof at / sizeof at[0]; p++) {
                    int di = (int)k[j].late[e];
                    float fr = k[j].late[e] - (float)di;
                    want[at[p] + di][e] += level[e] * (1.0f - fr);
                    want[at[p] + di + 1][e] += level[e] * fr;
                }
            }
            start(&voices[0]);
            taps(h, k[j].left, k[j].right);
            for (int n = 0; n < 16; n++) {
                for (int i = 0; i < FRAME; i++) {
                    float one = 0.0f;
                    for (unsigned p = 0; p < sizeof at / sizeof at[0]; p++)
                        one += at[p] == n * FRAME + i;
                    io[i][0] = io[i][1] = one;
                }
                place(&voices[0], h, k[j].itd, io);
                for (int i = 0; i < FRAME; i++)
                    for (int e = 0; e < 2; e++)
                        ok &= fabsf(io[i][e] - want[n * FRAME + i][e]) <= EXACT;
            }
            check(k[j].what, ok && voices[0].placed.moved == k[j].moved
                  && voices[0].placed.late == fmaxf(k[j].late[0], k[j].late[1]));
        }
    } else if (!strcmp(c, "long")) {      /* a long sound, against the same sum done whole */
        static float in[LONG * FRAME], out[LONG * FRAME][2];
        float most = 0.0f;
        int exact = 1;
        start(&voices[0]);
        taps(h, 0.5f, 1.0f);                 /* the left half the size, 7.5 late */
        for (int t = 0; t < LONG * FRAME; t++)
            in[t] = noise();
        for (int n = 0; n < LONG; n++) {
            for (int i = 0; i < FRAME; i++)
                io[i][0] = io[i][1] = in[n * FRAME + i];
            place(&voices[0], h, 7.5f, io);
            memcpy(out[n * FRAME], io, sizeof io);
        }
        for (int t = 0; t < LONG * FRAME; t++) {
            float a = t >= 7 ? in[t - 7] : 0.0f, b = t >= 8 ? in[t - 8] : 0.0f;
            most = fmaxf(most, fabsf(out[t][0] - played(0.5f) * (a * 0.5f + b * 0.5f)));
            exact &= out[t][1] == in[t];
        }
        check("the late ear is the sound before it, all the way through", most <= EXACT);
        check("and the one on time is the sound as it is", exact);
    } else if (!strcmp(c, "glide")) {     /* a voice moving from one side to the other */
        /* The glide as it should be: two stages, each all but 1/e of the way
         * in GLIDE_MS, a frame at a time. */
        const double a = 1.0 - exp(-FRAME * MS / (RATE * GLIDE_MS));
        const float from_size[2] = { 1.0f, 0.5f }, to_size[2] = { 0.25f, 1.0f };
        const float from[2] = { played(from_size[0]), played(from_size[1]) };
        const float to[2] = { played(to_size[0]), played(to_size[1]) };
        double first[2] = { from[0], from[1] }, sum[2] = { from[0], from[1] };
        float last[2] = { from[0], from[1] }, end[2] = { from[0], from[1] };
        float slope[2] = { 0.0f, 0.0f };
        float most_step = 0.0f, most_frame = 0.0f, most_bend = 0.0f, ends = 0.0f;
        int arrived = 0, stayed = 1, half = 0, changes = 0, counted = 0;
        start(&voices[0]);
        taps(h, from_size[0], from_size[1]);
        for (int n = 0; n < 4; n++) {
            for (int i = 0; i < FRAME; i++)
                io[i][0] = io[i][1] = 1.0f;     /* a steady sound: what comes out is the level */
            place(&voices[0], h, 0.0f, io);
            changes += voices[0].placed.changed;
        }
        taps(h, to_size[0], to_size[1]);
        for (int n = 1; n <= SECOND; n++) {
            for (int i = 0; i < FRAME; i++)
                io[i][0] = io[i][1] = 1.0f;
            place(&voices[0], h, 0.0f, io);
            if (n == 1)                          /* the left from 1 to a quarter */
                counted = voices[0].placed.changed
                       && fabsf(voices[0].placed.change_db - QUARTER_DB) < DB_CLOSE;
            else
                changes += voices[0].placed.changed;
            for (int e = 0; e < 2; e++) {
                float d = io[FRAME - 1][e] - end[e];
                first[e] += a * (to[e] - first[e]);
                sum[e] += a * (first[e] - sum[e]);
                for (int i = 0; i < FRAME; i++) {
                    most_step = fmaxf(most_step, fabsf(io[i][e] - last[e]));
                    last[e] = io[i][e];
                }
                if (io[FRAME - 1][e] != to[e])
                    ends = fmaxf(ends, (float)fabs(io[FRAME - 1][e] - sum[e]));
                most_frame = fmaxf(most_frame, fabsf(d));
                most_bend = fmaxf(most_bend, fabsf(d - slope[e]));    /* a corner */
                slope[e] = d;
                end[e] = io[FRAME - 1][e];
            }
            if (!half && io[FRAME - 1][0] <= 0.5f * (from[0] + to[0]))
                half = n;
            if (!arrived && io[0][0] == to[0] && io[0][1] == to[1])
                arrived = n;
            else if (arrived)
                stayed &= io[0][0] == to[0] && io[FRAME - 1][1] == to[1];
        }
        printf("glide: half-way in %.1f ms, arrived in %d frames, at most %.5f a sample\n",
               half * FRAME * MS / RATE, arrived, most_step);
        check("each frame ends where two stages put it", ends < GLIDE_CLOSE);
        check("half-way in about 1.7 times a stage's time",
              fabs(half * FRAME * MS / RATE - HALF_WAY * GLIDE_MS) < MS * FRAME / RATE);
        check("moving within each frame in a straight line",
              most_step <= most_frame / FRAME * (1.0f + BEND_CLOSE));
        check("with no corner where the setting changed",
              most_bend <= (float)(a * a) * (from[0] - to[0]) * (1.0f + BEND_CLOSE));
        check("then arrives and stays", arrived && stayed);
        check("the change counted once, by its dB", counted && !changes);
    } else if (!strcmp(c, "moving")) {    /* the delay gliding, read back from the sound */
        const double a = 1.0 - exp(-FRAME * MS / (RATE * GLIDE_MS));
        double first = 5.0, sum = 5.0;
        float ends = 0.0f, heard = 0.0f, prev = 5.0f;
        start(&voices[0]);
        taps(h, 1.0f, 0.5f);                 /* the right half the size, 5 then 20 late */
        for (int n = 0; n < SETTLE + MOVING; n++) {
            float itd = n < SETTLE ? -5.0f : -20.0f;
            for (int i = 0; i < FRAME; i++)
                io[i][0] = io[i][1] = RAMP * (float)(n * FRAME + i - SETTLE * FRAME);
            place(&voices[0], h, itd, io);
            if (n < SETTLE)
                continue;
            first = fabs(20.0 - first) <= ITD_ARRIVED ? 20.0 : first + a * (20.0 - first);
            sum += a * (first - sum);
            if (voices[0].f.itd_cur != -20.0f)       /* not yet arrived */
                ends = fmaxf(ends, (float)fabs(-voices[0].f.itd_cur - sum));
            /* The ramp, late by d, is what the ramp was d before. */
            for (int i = 0; i < FRAME; i++) {
                float t = (float)(n * FRAME + i - SETTLE * FRAME);
                float d = t - io[i][1] / (played(0.5f) * RAMP);
                float here = prev + ((float)sum - prev) * (float)(i + 1) / FRAME;
                heard = fmaxf(heard, fabsf(d - here));
            }
            prev = (float)sum;
        }
        printf("moving: the sound's delay within %.4f samples of the glide\n", heard);
        check("the delay's frames end where two stages put it", ends < 1e-4f);
        check("and the sound is late by it, sample by sample", heard < LATE_CLOSE);
    } else if (!strcmp(c, "start")) {     /* a voice starting again */
        int ok = 1;
        start(&voices[0]);
        taps(h, 1.0f, 0.25f);
        for (int n = 0; n < SETTLE; n++) {
            for (int i = 0; i < FRAME; i++)
                io[i][0] = io[i][1] = 1.0f;
            place(&voices[0], h, -12.0f, io);
        }
        hrtf_filter_clear_history(&voices[0].f);     /* as voice_reset_filters does */
        voices[0].e.started = 0;
        taps(h, 0.5f, 1.0f);                          /* now on the right */
        for (int i = 0; i < FRAME; i++)
            io[i][0] = io[i][1] = 1.0f;
        place(&voices[0], h, 4.0f, io);
        ok &= voices[0].placed.first;
        for (int i = 0; i < FRAME; i++)
            ok &= io[i][1] == 1.0f && io[i][0] == (i < 4 ? 0.0f : played(0.5f));
        place(&voices[0], h, 4.0f, io);
        ok &= !voices[0].placed.first;
        check("its first frame where its new setting places it, the late ear after nothing",
              ok);
    } else if (!strcmp(c, "cost")) {      /* the time for 32 voices, still and moving */
        for (int v = 0; v < COST_VOICES; v++)
            start(&voices[v]);
        for (int i = 0; i < FRAME; i++)
            x[i][0] = x[i][1] = (float)(0.5 * sin(TWO_PI * 440.0 * i / RATE));
        for (int moving = 0; moving < 2; moving++) {
            double took = 0.0;
            for (int n = 0; n < SECOND; n++) {
                float ph = (float)(TWO_PI * n / SECOND);
                if (!moving || n % UPDATE == 0)
                    taps(h, 0.75f + 0.25f * sinf(ph), 0.75f - 0.25f * sinf(ph));
                double t0 = now_ms();
                for (int v = 0; v < COST_VOICES; v++) {
                    memcpy(io, x, sizeof io);
                    place(&voices[v], h, moving ? 30.0f * sinf(ph) : 12.5f, io);
                }
                took += now_ms() - t0;
            }
            printf("cost: %d voices %s for a second: %.2f ms\n",
                   COST_VOICES, moving ? "moving" : "still", took);
        }
    } else {
        printf("apu_hrtf: no case %s\n", c);
        return 1;
    }
    return s_failed;
}
