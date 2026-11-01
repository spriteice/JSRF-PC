/* Run with: python tests/apu_gpfx/run.py
 *
 * A made-up effects image for src/apu/apu_gpfx.c, in a made-up window of
 * guest RAM: DirectSound's command block, code, state and an effect table,
 * laid out as DirectSound lays them out in the GP's scratch memory, with
 * the table as DirectSound rewrites it before it rings for the download.
 * Each case rings the doorbell as DirectSound does, calls apu_gpfx_command
 * as the port's doorbell answer does, and checks what the GP's memory holds
 * after against what the GP's kernel would have copied. It prints a "check"
 * line for each check, and the module's own log lines with RECOMP_APU_STATS
 * set, which run.py reads. The argument picks the case. The "limit" cases
 * need no image: they run made-up mixbins through apu_gpfx_frame and check
 * the 3D bins' limiter, with RECOMP_APU_GPFX set and without. "placed"
 * tells the module about made-up 3D voices as the voice processor's placing
 * would, for run.py to read the log's lines on them. The "chosen" cases
 * pass in the settings file's audioeffects= as port/audio.c does, with
 * RECOMP_APU_GPFX unset or winning over it.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "apu_gpfx.h"

#define WINDOW     0x20000u   /* the made-up window */
#define SCRATCH    0x4000u    /* the GP's scratch memory in it */
#define BLOCK      0x800u     /* DirectSound's command block in scratch */
#define DOORBELL   0x810u     /* its doorbell */
#define IMAGE      0x818u     /* the image after it */
#define CODE_WORDS 64u
#define MAX_FX     20u
#define SMALL      10u        /* the state of the effects not named, in words */
#define MAX_STATE  1024u
#define DSP_WORD   0xFFFFFFu
#define X_WINDOW   0xFE830000u /* where the CPU sees the GP's X memory */
#define X_STATE    0x80u       /* where the effects' state starts in it */
#define X_WORDS    0x1000u
#define FLAGS      4u          /* an effect's word that holds its flags */
#define STARTED    1u          /* the flag its code sets the first time it runs */
#define UNTOUCHED  0xEEu       /* the made-up window's bytes before anything is shown */

/* The mixbins, for the limiter. */
#define BINS       32u
#define FRAME      32u        /* samples in a frame */
#define FX_FIRST   6u         /* the 3D bins: FX_BINS of them from here */
#define FX_BINS    4u
#define SECOND     1500u      /* frames in a second, at 48 kHz */
#define QUIET      0.5f       /* the 3D bins' peak, quiet and loud */
#define LOUD       2.7f
#define SHORT      300u       /* "placed": frames, a fifth of a second */
#define MOVES      25u        /* frames between a voice's moves: 60 a second */
#define TURNS      50u        /* frames between the camera's turns, which move most of them */
#define FAR_DB     3.0f
#define LATE       4.8f       /* samples: 0.1 ms */
#define STEP_DB    0.5f
#define STEP_LATE  0.48f
#define VOICE_NS   1000
#define UNDER      0.9f       /* the other 3D bins' peak, from the last one's */
#define CEILING    1.0f       /* the limiter's: full scale */
#define ROUNDING   1e-6f
#define HEARD      0.01f      /* a sample big enough to tell the gain from */
#define AT_150MS   225u       /* frames in the release's 150 ms */
#define BACK_LO    0.70f      /* where the gain is 150 ms after a loud moment: 0.77 */
#define BACK_HI    0.85f
#define BACK_BY    0.999f     /* and a second after, near enough to unity */
#define STEP       (1.0f / FRAME)   /* the most the gain may change in a sample */

static uint8_t s_ram[WINDOW];
static uint8_t s_window[X_WORDS * 4];  /* where the CPU would see the GP's X memory */
static unsigned s_fx_words[MAX_FX], s_fx_at[MAX_FX], s_count, s_state_words;
static uint32_t s_expect[MAX_STATE];  /* the state as the GP's kernel would leave it */
static int s_failed;

static void put(uint32_t at, uint32_t v) { memcpy(s_ram + SCRATCH + at, &v, 4); }
static uint32_t get(uint32_t at) { uint32_t v; memcpy(&v, s_ram + SCRATCH + at, 4); return v; }
static uint32_t state(unsigned word) { return IMAGE + 4 * (CODE_WORDS + word); }
/* The table's entry for effect e, and the word of it that says where its
 * state is: as DirectSound leaves it when it rings for the download, where
 * the CPU sees that word in the GP's X memory. */
static uint32_t entry(unsigned e) { return state(s_state_words) + 8 + 32 * e; }
static uint32_t in_x(unsigned x) { return X_WINDOW + 4 * x; }

static void check(const char *what, int ok)
{
    printf("check: %s %s\n", what, ok ? "ok" : "FAILED");
    s_failed |= !ok;
}

/* An image of `count` effects, the first two of `first` and `second` words,
 * written into scratch with its command block. Each word is made up, with a
 * top byte that is not part of a DSP word. */
static void make_image(unsigned count, unsigned first, unsigned second)
{
    uint32_t table, i, e, at = 0;

    memset(s_ram, 0, sizeof s_ram);
    s_count = count;
    for (i = 0; i < CODE_WORDS; i++)
        put(IMAGE + 4 * i, 0xC0DE00u + i);
    for (e = 0; e < count; e++) {
        s_fx_words[e] = e == 0 ? first : e == 1 ? second : SMALL;
        s_fx_at[e] = at;
        for (i = 0; i < s_fx_words[e]; i++)
            put(state(at + i), 0xAB000000u | (e << 12) | i);
        at += s_fx_words[e];
    }
    s_state_words = at;
    table = state(s_state_words);
    put(table, count);
    for (e = 0; e < count; e++) {
        put(entry(e) + 8, in_x(X_STATE + s_fx_at[e]));
        put(entry(e) + 12, 4 * s_fx_words[e]);
    }
    put(BLOCK + 0, 0);
    put(BLOCK + 4, CODE_WORDS);
    put(BLOCK + 8, state(0));
    put(BLOCK + 12, s_state_words);
}

/* The doorbell rung with `command`, and answered as the port answers it. */
static void ring(uint32_t command)
{
    put(DOORBELL, command);
    apu_gpfx_command(s_ram, WINDOW, SCRATCH + DOORBELL);
    put(DOORBELL, 0);
}

/* The GP's next frame: every effect that hasn't started starts. */
static void frame(void)
{
    unsigned e;
    for (e = 0; e < s_count; e++)
        if (s_fx_words[e] > FLAGS)
            s_expect[s_fx_at[e] + FLAGS] |= STARTED;
}

/* The image downloaded: command 3 with the whole state. */
static void load(void)
{
    unsigned i;
    ring(3);
    for (i = 0; i < s_state_words; i++)
        s_expect[i] = get(state(i)) & DSP_WORD;
    frame();
}

/* Words first to last of effect e changed in scratch, as DirectSound's
 * SetEffectData does before it sends them. */
static void write_words(unsigned e, unsigned first, unsigned last)
{
    unsigned i;
    for (i = first; i <= last; i++)
        put(state(s_fx_at[e] + i), 0x500000u + i);
}

/* Words first to last of effect e sent as DirectSound sends a change: the
 * range in the block, then command 2. */
static void send(unsigned e, unsigned first, unsigned last)
{
    unsigned i;
    put(BLOCK + 0, s_fx_at[e] + first);
    put(BLOCK + 8, state(s_fx_at[e] + first));
    put(BLOCK + 12, last - first + 1);
    ring(2);
    for (i = s_fx_at[e] + first; i <= s_fx_at[e] + last; i++)
        s_expect[i] = get(state(i)) & DSP_WORD;
    frame();
}

/* Every effect as the GP's kernel would have left it. */
static int as_expected(void)
{
    unsigned e, i, words;
    for (e = 0; e < s_count; e++) {
        const uint32_t *x = apu_gpfx_effect(e, &words);
        if (!x || words != s_fx_words[e])
            return 0;
        for (i = 0; i < words; i++)
            if (x[i] != s_expect[s_fx_at[e] + i])
                return 0;
    }
    return 1;
}

/* The made-up window as the copy should leave it: from X_STATE up, every
 * word of the GP's memory as expected, with nothing past the last effect;
 * below it, untouched. */
static int window_as_expected(void)
{
    unsigned x;
    for (x = 0; x < X_WORDS; x++) {
        uint32_t v;
        memcpy(&v, s_window + 4 * x, 4);
        if (x < X_STATE ? v != 0x01010101u * UNTOUCHED
            : v != (x - X_STATE < s_state_words ? s_expect[x - X_STATE] : 0))
            return 0;
    }
    return 1;
}

static int window_untouched(void)
{
    unsigned i;
    for (i = 0; i < sizeof s_window; i++)
        if (s_window[i] != UNTOUCHED)
            return 0;
    return 1;
}

/* Frame n of made-up mixbins: each bin a tone of its own, the last 3D bin
 * peaking at `level`, the other three a little under, and the rest at
 * QUIET, each frame's first sample at the peak. */
static void tone(float bins[BINS][FRAME], unsigned n, float level)
{
    unsigned b, i;

    for (b = 0; b < BINS; b++) {
        float peak = b == FX_FIRST + FX_BINS - 1 ? level
                   : b >= FX_FIRST && b < FX_FIRST + FX_BINS ? UNDER * level : QUIET;
        for (i = 0; i < FRAME; i++)
            bins[b][i] = i ? peak * sinf(0.05f * (float)((b + 1) * (n * FRAME + i)))
                           : peak;
    }
}

/* Whether every bin but the 3D bins is as it went in. */
static int others_untouched(float in[BINS][FRAME], float out[BINS][FRAME])
{
    unsigned b;

    for (b = 0; b < BINS; b++)
        if ((b < FX_FIRST || b >= FX_FIRST + FX_BINS)
            && memcmp(in[b], out[b], sizeof in[b]))
            return 0;
    return 1;
}

/* What the limiter did to frame `late` of the 3D bins, now in `out`: one
 * gain for the four bins at each sample, gliding from the last frame's, at
 * most STEP a sample, and the bins under the ceiling. *gain is the last gain
 * seen, and *since the samples since, both carried from frame to frame. */
static int limited(float late[BINS][FRAME], float out[BINS][FRAME], float *gain,
                   unsigned *since)
{
    unsigned b, i;

    for (i = 0; i < FRAME; i++) {
        float g = -1.0f;
        for (b = FX_FIRST; b < FX_FIRST + FX_BINS; b++) {
            if (fabsf(out[b][i]) > CEILING + ROUNDING)
                return 0;
            if (fabsf(late[b][i]) < HEARD)
                continue;
            if (g < 0.0f)
                g = out[b][i] / late[b][i];
            else if (fabsf(out[b][i] / late[b][i] - g) > ROUNDING)
                return 0;
        }
        if (g < 0.0f) {
            ++*since;
            continue;
        }
        if (fabsf(g - *gain) > STEP * (float)*since + ROUNDING)
            return 0;
        *gain = g;
        *since = 1;
    }
    return 1;
}

/* The reverb (apu_reverb.h), with a made-up layout and made-up rooms, its
 * words as apu_reverb.h names them. Delays are in bytes, four to a sample. */
#define SEND       10u        /* the reverb send */
#define RV_SIZE    43u
#define RV_REFL    70u
#define RV_INJECT  75u
#define RV_BUILD   76u
#define RV_COMB    84u
#define RV_LOOP    88u
#define RV_INJECT_LEVEL 89u
#define RV_LOOP_IN 97u
#define RV_LATE_IN 98u
#define RV_REFL_GAIN 99u
#define RV_LATE_GAIN 103u
#define RV_LOOP_GAIN 104u
#define RV_FILTER  106u       /* each low-pass: what it holds, its feedback, its input's level */
#define RV_ROOM_END 135u
#define RV_MATRIX  136u
#define RV_COMB_FB 143u
#define RV_INJECT_AT 147u
#define RV_LATE_LEN 155u
#define RV_LINES   14u
#define RV_OUTS    4u
#define RV_FILTERS 10u
#define RV_ONE     8388608.0f /* a DSP word's 1 */
#define RV_GLIDE   336u       /* frames the reverb takes to land on a new room */
#define RV_KEEP    2u         /* and frames for what it added to come out after: the limiter's
                                 frame, and the halfband's */
#define RV_SPREAD  48u        /* full-rate samples the halfbands spread an impulse over, at most */
#define RV_UNLIKE  0.5        /* the most two of the four outputs may be alike */
#define RV_TONE    0.0625f    /* a full-rate tone's step, in radians: 480 Hz */
#define RV_CLICK   2.0f       /* how much more a change may bend the sound than a room does */
#define RV_SOON    10u        /* frames into a fade to a dry room: 7 ms */
#define RV_PERIOD  4u         /* frames that hold a period of the tone, and more */
#define RV_STILL   0.25f      /* and what's left of the room's sound by then, at least */
#define RV_LOW     (2.0 * 3.14159265358979 * 4000.0 / 48000.0)   /* 4 kHz, radians a sample */
#define RV_HIGH    (2.0 * 3.14159265358979 * 20000.0 / 48000.0)  /* 20 kHz: 4 kHz's mirror at 24 kHz */
#define RV_SMOOTH  1e-5       /* the most a mirror may have, against the tone: 50 dB down */

static const unsigned rv_lines[RV_LINES] = { 3000, 3000, 3000, 3000, 301, 333, 367, 397,
                                             701, 809, 907, 1013, 8000, 8000 };
static const unsigned rv_inject_at[8] = { 0, 120, 280, 440, 40, 160, 320, 480 };
static const float rv_comb_fb[RV_OUTS] = { 0.3f, -0.3f, 0.3f, -0.3f };

typedef struct {
    unsigned refl[1 + RV_OUTS], inject, build[8], comb[RV_OUTS], loop;  /* in samples */
    float inject_level[8], loop_in, late_in, refl_gain, late_gain, loop_gain;
    float filter[RV_FILTERS][2];   /* each low-pass's feedback and input level */
} Room;

/* Two wet rooms, and a dry one. */
static const Room s_wet = {
    { 300, 220, 236, 252, 268 }, 400, { 500, 600, 700, 900, 800, 1000, 900, 1100 },
    { 250, 270, 290, 310 }, 7000,
    { 0.7f, -0.7f, -0.7f, 0.7f, 0.7f, 0.7f, -0.7f, -0.7f }, 0.2f, 0.2f, 0.003f, 0.004f, 0.002f,
    { { 0.6f, 0.4f }, { 0.3f, 0.7f }, { 0.3f, 0.7f }, { 0.3f, 0.7f }, { 0.25f, 0.6f },
      { 0.25f, 0.6f }, { 0.25f, 0.6f }, { 0.25f, 0.6f }, { 0.5f, 0.15f }, { 0.5f, 0.15f } }
};
static const Room s_wet2 = {
    { 260, 284, 300, 316, 332 }, 520, { 640, 760, 820, 1040, 940, 1200, 1060, 1300 },
    { 200, 230, 260, 290 }, 6000,
    { -0.6f, 0.6f, 0.6f, -0.6f, -0.6f, 0.6f, 0.6f, -0.6f }, 0.1f, 0.3f, 0.002f, 0.006f, 0.001f,
    { { 0.4f, 0.3f }, { 0.2f, 0.8f }, { 0.2f, 0.8f }, { 0.2f, 0.8f }, { 0.35f, 0.5f },
      { 0.35f, 0.5f }, { 0.35f, 0.5f }, { 0.35f, 0.5f }, { 0.4f, 0.2f }, { 0.4f, 0.2f } }
};
static const Room s_dry;
/* The long loops alone. */
static const Room s_loops = {
    { 300, 220, 236, 252, 268 }, 400, { 500, 600, 700, 900, 800, 1000, 900, 1100 },
    { 250, 270, 290, 310 }, 7000,
    { 0.7f, -0.7f, -0.7f, 0.7f, 0.7f, 0.7f, -0.7f, -0.7f }, 0.2f, 0.2f, 0.0f, 0.0f, 0.004f,
    { { 0.6f, 0.4f }, { 0.3f, 0.7f }, { 0.3f, 0.7f }, { 0.3f, 0.7f }, { 0.25f, 0.6f },
      { 0.25f, 0.6f }, { 0.25f, 0.6f }, { 0.25f, 0.6f }, { 0.5f, 0.15f }, { 0.5f, 0.15f } }
};

static uint32_t fraction(float f) { return (uint32_t)lrintf(f * RV_ONE) & DSP_WORD; }

/* The reverb's word w, in the image's state as loaded. */
static void rv_put(unsigned w, uint32_t v) { put(state(s_fx_at[0] + w), v); }

/* Room r's words, written into the reverb's state in scratch. */
static void rv_room(const Room *r)
{
    unsigned k;
    for (k = 0; k < 1 + RV_OUTS; k++)
        rv_put(RV_REFL + k, 4 * r->refl[k]);
    rv_put(RV_INJECT, 4 * r->inject);
    for (k = 0; k < 8; k++) {
        rv_put(RV_BUILD + k, 4 * r->build[k]);
        rv_put(RV_INJECT_LEVEL + k, fraction(r->inject_level[k]));
    }
    for (k = 0; k < RV_OUTS; k++) {
        rv_put(RV_COMB + k, 4 * r->comb[k]);
        rv_put(RV_REFL_GAIN + k, fraction(r->refl_gain));
    }
    rv_put(RV_LOOP, 4 * r->loop);
    rv_put(RV_LOOP_IN, fraction(r->loop_in));
    rv_put(RV_LATE_IN, fraction(r->late_in));
    rv_put(RV_LATE_GAIN, fraction(r->late_gain));
    rv_put(RV_LOOP_GAIN, fraction(r->loop_gain));
    for (k = 0; k < RV_FILTERS; k++) {
        rv_put(RV_FILTER + 3 * k, 0);
        rv_put(RV_FILTER + 3 * k + 1, fraction(r->filter[k][0]));
        rv_put(RV_FILTER + 3 * k + 2, fraction(r->filter[k][1]));
    }
}

/* An image of `count` effects, the first the made-up reverb in a dry room,
 * downloaded; `lines` its lines' lengths. Of 20 effects, it's laid out as
 * JSRF's. */
static void rv_image_of(unsigned count, const unsigned *lines)
{
    unsigned k;
    make_image(count, 554, 93);
    for (k = 0; k < RV_LINES; k++)
        rv_put(RV_SIZE + 2 * k, 4 * lines[k] - 1);
    for (k = 0; k < 7; k++)              /* the textbook mixing matrix: a half, less one off it */
        rv_put(RV_MATRIX + k, fraction(k == 3 ? 0.5f : -0.5f));
    for (k = 0; k < RV_OUTS; k++) {
        rv_put(RV_COMB_FB + k, fraction(rv_comb_fb[k]));
        rv_put(RV_LATE_LEN + k, 4 * lines[8 + k]);
    }
    for (k = 0; k < 8; k++)
        rv_put(RV_INJECT_AT + k, 4 * rv_inject_at[k]);
    rv_room(&s_dry);
    load();
}

static void rv_image(const unsigned *lines) { rv_image_of(20, lines); }

/* Room r sent, as DirectSound sends a room. */
static void rv_send(const Room *r)
{
    rv_room(r);
    send(0, RV_REFL, RV_ROOM_END);
}

/* A frame of mixbins with `in` in the reverb send and nothing else, run. */
static int rv_frame(float mix[BINS][FRAME], const float *in)
{
    memset(mix, 0, sizeof(float) * BINS * FRAME);
    memcpy(mix[SEND], in, sizeof(float) * FRAME);
    return apu_gpfx_frame(&mix[0][0], BINS, FRAME);
}

/* Whether every bin but the 3D bins is as rv_frame left it. */
static int rv_others(float mix[BINS][FRAME], const float *in)
{
    unsigned b, i;
    for (b = 0; b < BINS; b++)
        for (i = 0; i < FRAME; i++)
            if ((b < FX_FIRST || b >= FX_FIRST + FX_BINS)
                && mix[b][i] != (b == SEND ? in[i] : 0.0f))
                return 0;
    return 1;
}

static int rv_silent(float mix[BINS][FRAME])
{
    unsigned b, i;
    for (b = FX_FIRST; b < FX_FIRST + FX_BINS; b++)
        for (i = 0; i < FRAME; i++)
            if (mix[b][i] != 0.0f)
                return 0;
    return 1;
}

/* The most the 3D bins bend in a frame: their largest second difference,
 * carried on from the last two samples in prev. */
static float rv_bend(float mix[BINS][FRAME], float prev[FX_BINS][2])
{
    float most = 0.0f;
    unsigned b, i;
    for (b = 0; b < FX_BINS; b++)
        for (i = 0; i < FRAME; i++) {
            float x = mix[FX_FIRST + b][i], d = fabsf(x - 2.0f * prev[b][1] + prev[b][0]);
            if (d > most)
                most = d;
            prev[b][0] = prev[b][1];
            prev[b][1] = x;
        }
    return most;
}

/* Goertzel: a frame of the 3D bins, at w radians a sample, carried on in
 * st (two values for each bin); then their power at w, added up. */
static void rv_tone_in(float mix[BINS][FRAME], double st[FX_BINS][2], double w)
{
    unsigned b, i;
    for (b = 0; b < FX_BINS; b++)
        for (i = 0; i < FRAME; i++) {
            double v = mix[FX_FIRST + b][i] + 2.0 * cos(w) * st[b][0] - st[b][1];
            st[b][1] = st[b][0];
            st[b][0] = v;
        }
}

static double rv_power(double st[FX_BINS][2], double w)
{
    double sum = 0.0;
    unsigned b;
    for (b = 0; b < FX_BINS; b++)
        sum += st[b][0] * st[b][0] + st[b][1] * st[b][1] - 2.0 * cos(w) * st[b][0] * st[b][1];
    return sum;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";

    if (!strcmp(c, "studied")) {        /* JSRF's layout, and a change as the reverb gets */
        make_image(20, 554, 93);
        check("nothing before an image", !apu_gpfx_effect(0, NULL));
        load();
        check("laid out as JSRF's", apu_gpfx_studied());
        check("every effect as loaded, and started", as_expected()
              && (apu_gpfx_effect(0, NULL)[FLAGS] & STARTED)
              && (apu_gpfx_effect(19, NULL)[FLAGS] & STARTED));
        check("no effect past the last", !apu_gpfx_effect(20, NULL));
        write_words(0, 4, 4);
        write_words(0, 70, 135);
        check("words written but not sent change nothing", as_expected());
        send(0, 4, 135);
        check("the words sent, and nothing else", as_expected());
    } else if (!strcmp(c, "other")) {   /* another layout */
        make_image(3, 40, 30);
        load();
        check("not laid out as JSRF's", !apu_gpfx_studied());
        check("every effect as loaded", as_expected());
        write_words(2, 0, 9);
        send(2, 0, 9);
        check("effect 2 changed", as_expected());
    } else if (!strcmp(c, "reload")) {  /* a new image clears the last one's changes */
        make_image(20, 554, 93);
        load();
        write_words(1, 0, 92);
        send(1, 0, 92);
        make_image(20, 554, 93);
        load();
        check("as loaded again", as_expected());
    } else if (!strcmp(c, "window")) {  /* the copy where the CPU reads it back */
        memset(s_window, UNTOUCHED, sizeof s_window);
        apu_gpfx_window(s_window);
        make_image(20, 554, 93);
        check("nothing shown before an image", window_untouched());
        load();
        check("shown as loaded, every effect started", window_as_expected());
        write_words(0, 4, 4);
        write_words(0, 70, 135);
        check("words written but not sent not shown", window_as_expected());
        send(0, 4, 135);
        check("the words sent shown, and the reverb started again",
              window_as_expected() && s_expect[FLAGS] == (0x500000u | FLAGS | STARTED));
        apu_gpfx_window(NULL);
        memset(s_window, UNTOUCHED, sizeof s_window);
        write_words(1, 0, 0);
        send(1, 0, 0);
        check("not shown once let go", window_untouched() && as_expected());
    } else if (!strcmp(c, "bad")) {     /* commands that can't be followed */
        make_image(20, 554, 93);
        load();
        write_words(0, 0, 9);
        put(BLOCK + 0, 0xF00);          /* to past the GP's memory */
        put(BLOCK + 8, state(0));
        put(BLOCK + 12, 0x200);
        ring(2);
        put(BLOCK + 0, 0);              /* from past the window */
        put(BLOCK + 8, WINDOW);
        put(BLOCK + 12, 4);
        ring(2);
        put(BLOCK + 8, state(0));       /* no command */
        put(BLOCK + 12, 10);
        ring(0);
        apu_gpfx_command(s_ram, WINDOW, 0x10);          /* no room for a block */
        apu_gpfx_command(s_ram, WINDOW, WINDOW - 4);
        apu_gpfx_command(NULL, WINDOW, SCRATCH + DOORBELL);
        check("as loaded", as_expected());
        make_image(20, 554, 93);
        put(state(s_state_words), 1000);                /* a table too long */
        ring(3);
        check("no effects", !apu_gpfx_effect(0, NULL) && !apu_gpfx_studied());
        make_image(20, 554, 93);        /* state not in the effects' memory */
        put(entry(2) + 8, state(s_fx_at[2]));           /* the file's byte offset */
        put(entry(3) + 8, in_x(X_STATE - 1));           /* below the effects' memory */
        put(entry(4) + 8, in_x(X_WORDS - SMALL + 1));   /* reaching past it */
        put(entry(5) + 8, in_x(X_STATE + s_fx_at[5]) + 2);  /* not on a word */
        ring(3);
        check("only the effects found", !apu_gpfx_studied() && apu_gpfx_effect(1, NULL)
              && !apu_gpfx_effect(2, NULL) && !apu_gpfx_effect(3, NULL)
              && !apu_gpfx_effect(4, NULL) && !apu_gpfx_effect(5, NULL)
              && apu_gpfx_effect(6, NULL));
    } else if (!strcmp(c, "limit")) {   /* the 3D bins' limiter, quiet, loud, quiet */
        static float in[BINS][FRAME], late[BINS][FRAME], out[BINS][FRAME];
        float gain = 1.0f, loudest = 0.0f, at_150ms = 0.0f;
        int quiet = 1, loud = 1, others = 1, ran = 1;
        unsigned n, b, i, since = 1;

        tone(in, 0, QUIET);
        memcpy(out, in, sizeof out);
        check("not run on a frame of another size",
              !apu_gpfx_frame(&out[0][0], BINS, FRAME / 2) && !memcmp(in, out, sizeof in));
        check("not run with too few bins",
              !apu_gpfx_frame(&out[0][0], FX_FIRST + FX_BINS - 1, FRAME)
              && !memcmp(in, out, sizeof in));
        memset(late, 0, sizeof late);
        for (n = 0; n < 4 * SECOND; n++) {
            float level = n >= SECOND / 2 && n < SECOND ? LOUD : QUIET;
            tone(in, n, level);
            memcpy(out, in, sizeof out);
            ran &= apu_gpfx_frame(&out[0][0], BINS, FRAME);
            others &= others_untouched(in, out);
            if (n < SECOND / 2)         /* a frame late, and as it was */
                for (b = FX_FIRST; b < FX_FIRST + FX_BINS; b++)
                    quiet &= !memcmp(out[b], late[b], sizeof out[b]);
            else
                loud &= limited(late, out, &gain, &since);
            for (b = FX_FIRST; b < FX_FIRST + FX_BINS; b++)
                for (i = 0; i < FRAME; i++)
                    if (fabsf(out[b][i]) > loudest)
                        loudest = fabsf(out[b][i]);
            if (n == SECOND + AT_150MS)
                at_150ms = gain;
            memcpy(late, in, sizeof late);
        }
        check("run on every frame", ran);
        check("the other bins untouched", others);
        check("quiet 3D bins a frame late, and as they were", quiet);
        check("loud ones under full scale, one gain for the four, gliding", loud);
        check("and at full scale at their loudest", loudest > CEILING - 2 * ROUNDING * LOUD);
        check("back to 0.77 150 ms later", at_150ms > BACK_LO && at_150ms < BACK_HI);
        check("and to unity in a second", gain > BACK_BY);
    } else if (!strcmp(c, "placed")) {  /* the placing's log, over two seconds */
        /* Voice 0 on the left 3 dB down, then on the right, moved every
         * MOVES frames, until a little before the end; voice 1 never moved,
         * for a second; voice 2 too short for a line of its own, its delay
         * moved; voice 3 starting again with no gap. Every TURNS frames
         * while voice 0 plays, the camera turns: voices 0, 2 and 3 move,
         * but not voice 1, nor voice 3 on its first frame. */
        static float mix[BINS][FRAME];
        static const int bins[8] = { 6, 8, 7, 9, 20, 20, 20, 20 };
        unsigned n, v;

        for (n = 0; n < 2 * SECOND; n++) {
            int turn = n && n % TURNS == 0 && n < 2 * SECOND - SHORT;
            for (v = 0; v < 4; v++) {
                ApuGpfxPlaced p = { 0 };
                if ((v == 0 && n >= 2 * SECOND - SHORT) || (v == 1 && n >= SECOND)
                    || (v == 2 && n >= SHORT))
                    continue;
                p.far_db = v == 0 ? FAR_DB : 2.0f * FAR_DB;
                p.side = v == 0 && n < SECOND ? -1 : 1;
                p.late = LATE;
                p.moved = v == 2;
                p.first = !n || (v == 3 && n == SECOND + 2 * SHORT);
                p.changed = (v == 0 && n && n % MOVES == 0)
                            || ((v == 2 || v == 3) && turn && !p.first);
                p.change_db = p.changed ? STEP_DB : 0.0f;
                p.change_late = p.changed ? STEP_LATE : 0.0f;
                apu_gpfx_voice(v, bins, &p, VOICE_NS);
            }
            memset(mix, 0, sizeof mix);
            apu_gpfx_frame(&mix[0][0], BINS, FRAME);
        }
    } else if (!strcmp(c, "limit_off")) {   /* nothing, with the switch unset */
        static float in[BINS][FRAME], out[BINS][FRAME];
        int ran = 0, same = 1;
        unsigned n;

        for (n = 0; n < SECOND; n++) {
            tone(in, n, LOUD);
            memcpy(out, in, sizeof out);
            ran |= apu_gpfx_frame(&out[0][0], BINS, FRAME);
            same &= !memcmp(in, out, sizeof in);
        }
        check("nothing run with the switch off", !ran && same);
    } else if (!strcmp(c, "reverb")) {  /* a wet room, and an impulse in the send */
        static float mix[BINS][FRAME], in[FRAME];
        static double energy[4];         /* the 3D bins' in each second, the first four */
        double sum[FX_BINS][FX_BINS] = { { 0 } };
        const unsigned at = 10;          /* the frame the impulse is in */
        const unsigned first = 2 * s_wet.refl[1] + 2 * FRAME;  /* about when the first reflection comes */
        unsigned n, b, i, heard = 0, silent_from = 0, bin_heard[FX_BINS] = { 0 };
        int ran = 1, others = 1, early = 1;

        rv_image(rv_lines);
        rv_send(&s_wet);
        for (n = 0; n < 30 * SECOND; n++) {
            memset(in, 0, sizeof in);
            if (n == at)
                in[0] = 1.0f;
            ran &= rv_frame(mix, in);
            others &= rv_others(mix, in);
            for (b = 0; b < FX_BINS; b++)
                for (i = 0; i < FRAME; i++) {
                    float x = mix[FX_FIRST + b][i];
                    unsigned t = (n - at) * FRAME + i;
                    if (x != 0.0f && n >= at && !heard)
                        heard = t;
                    if (x != 0.0f && n >= at && !bin_heard[b])
                        bin_heard[b] = t;
                    if (x != 0.0f && n < at + first / FRAME - RV_SPREAD / FRAME)
                        early = 0;
                    if (n / SECOND < 4)
                        energy[n / SECOND] += (double)x * x;
                    for (unsigned o = 0; o < FX_BINS; o++)
                        sum[b][o] += (double)x * mix[FX_FIRST + o][i];
                }
            if (!rv_silent(mix))
                silent_from = n + 1;
        }
        check("run on every frame", ran);
        check("the other bins untouched", others);
        check("nothing before the room's first reflection", early && heard + RV_SPREAD >= first);
        check("and the reflections soon after it", heard && heard <= first + RV_SPREAD);
        check("each output from its own reflections, in its own bin",
              bin_heard[0] == heard
              && bin_heard[1] == bin_heard[0] + 2 * (s_wet.refl[2] - s_wet.refl[1])
              && bin_heard[2] == bin_heard[0] + 2 * (s_wet.refl[3] - s_wet.refl[1])
              && bin_heard[3] == bin_heard[0] + 2 * (s_wet.refl[4] - s_wet.refl[1]));
        check("the four outputs each their own", sum[0][0] > 0.0 && sum[1][1] > 0.0
              && sum[2][2] > 0.0 && sum[3][3] > 0.0
              && fabs(sum[0][1]) < RV_UNLIKE * sqrt(sum[0][0] * sum[1][1])
              && fabs(sum[0][2]) < RV_UNLIKE * sqrt(sum[0][0] * sum[2][2])
              && fabs(sum[1][3]) < RV_UNLIKE * sqrt(sum[1][1] * sum[3][3]));
        check("dying away, second by second", energy[1] < energy[0] / 100.0
              && energy[2] < energy[1] / 100.0 && energy[3] < energy[2]);
        check("and all gone, to nothing at all, by 20 s", silent_from && silent_from < 20 * SECOND);
    } else if (!strcmp(c, "reverb_loops")) {  /* the long loops alone: one front, one back */
        static float mix[BINS][FRAME], in[FRAME];
        int front = 1, back = 1, apart = 0, heard_front = 0, heard_back = 0;
        unsigned n, i;

        rv_image(rv_lines);
        rv_send(&s_loops);
        for (n = 0; n < 2 * SECOND; n++) {
            memset(in, 0, sizeof in);
            if (n == 10)
                in[0] = 1.0f;
            rv_frame(mix, in);
            for (i = 0; i < FRAME; i++) {
                front &= mix[FX_FIRST][i] == mix[FX_FIRST + 1][i];
                back &= mix[FX_FIRST + 2][i] == mix[FX_FIRST + 3][i];
                apart |= mix[FX_FIRST][i] != mix[FX_FIRST + 2][i];
                heard_front |= mix[FX_FIRST][i] != 0.0f;
                heard_back |= mix[FX_FIRST + 2][i] != 0.0f;
            }
        }
        check("the front pair one loop, the back pair the other",
              heard_front && heard_back && front && back && apart);
    } else if (!strcmp(c, "reverb_dry")) {  /* the image's own room, dry */
        static float mix[BINS][FRAME], in[FRAME];
        unsigned n, i;
        int silent = 1, others = 1;

        rv_image(rv_lines);
        for (n = 0; n < 2 * SECOND; n++) {
            for (i = 0; i < FRAME; i++)
                in[i] = i ? QUIET * sinf(RV_TONE * (float)(n * FRAME + i)) : QUIET;
            rv_frame(mix, in);
            silent &= rv_silent(mix);
            others &= rv_others(mix, in);
        }
        check("nothing added in a dry room", silent && others);
    } else if (!strcmp(c, "reverb_room")) {  /* from one room to another, then dry */
        static float mix[BINS][FRAME], in[FRAME];
        float prev[FX_BINS][2] = { { 0 } }, steady = 0.0f, change = 0.0f, last = 0.0f;
        float before = 0.0f, soon = 0.0f;
        unsigned n, i, silent_from = 0;

        rv_image(rv_lines);
        rv_send(&s_wet);
        for (n = 0; n < 10 * SECOND; n++) {
            float bend;
            if (n == 3 * SECOND)
                rv_send(&s_wet2);
            if (n == 6 * SECOND)
                rv_send(&s_dry);
            for (i = 0; i < FRAME; i++)
                in[i] = QUIET * sinf(RV_TONE * (float)(n * FRAME + i));
            rv_frame(mix, in);
            bend = rv_bend(mix, prev);
            /* Each room's sound once it has settled, against the change's. */
            if ((n >= 2 * SECOND && n < 3 * SECOND) || (n >= 5 * SECOND && n < 6 * SECOND))
                steady = bend > steady ? bend : steady;
            if (n >= 5 * SECOND && n < 6 * SECOND)
                before = bend > before ? bend : before;
            if (n >= 6 * SECOND + RV_SOON && n < 6 * SECOND + RV_SOON + RV_PERIOD)
                soon = bend > soon ? bend : soon;
            if (n >= 3 * SECOND && n < 4 * SECOND)
                change = bend > change ? bend : change;
            if (n >= 6 * SECOND && n < 6 * SECOND + RV_GLIDE)
                last = bend;
            if (!rv_silent(mix))
                silent_from = n + 1;
        }
        check("no click going from room to room", steady > 0.0f && change <= RV_CLICK * steady);
        check("fading out to a dry room, not cut off", soon > RV_STILL * before && last > 0.0f);
        check("and nothing added once there", silent_from > 6 * SECOND + RV_GLIDE
              && silent_from <= 6 * SECOND + RV_GLIDE + RV_KEEP);
    } else if (!strcmp(c, "reverb_smooth")) {  /* nothing mirrored about 12 kHz, either way */
        static float mix[BINS][FRAME], in[FRAME];
        double low[FX_BINS][2] = { { 0 } }, high[FX_BINS][2] = { { 0 } };
        double alias[FX_BINS][2] = { { 0 } };
        unsigned n, i;

        rv_image(rv_lines);
        rv_send(&s_wet);
        for (n = 0; n < 6 * SECOND; n++) {
            int tone_low = n < 2 * SECOND, tone_high = n >= 4 * SECOND;
            for (i = 0; i < FRAME; i++) {
                double t = (double)(n * FRAME + i);
                in[i] = tone_low ? QUIET * (float)sin(RV_LOW * t)
                      : tone_high ? QUIET * (float)sin(RV_HIGH * t) : 0.0f;
            }
            rv_frame(mix, in);
            if (n >= SECOND && n < 2 * SECOND) {        /* the low tone, settled */
                rv_tone_in(mix, low, RV_LOW);
                rv_tone_in(mix, high, RV_HIGH);
            }
            if (n >= 5 * SECOND)                       /* the high one, settled */
                rv_tone_in(mix, alias, RV_LOW);
        }
        check("a 4 kHz tone comes out", rv_power(low, RV_LOW) > 0.0);
        check("with nothing at 20 kHz, its mirror on the way up",
              rv_power(high, RV_HIGH) < RV_SMOOTH * rv_power(low, RV_LOW));
        check("and a 20 kHz tone with nothing at 4 kHz, its mirror on the way down",
              rv_power(alias, RV_LOW) < RV_SMOOTH * rv_power(low, RV_LOW));
    } else if (!strcmp(c, "reverb_reload")) {  /* the image again, then with other lines */
        static float mix[BINS][FRAME], in[FRAME];
        unsigned lines[RV_LINES], n;
        int added = 0;

        rv_image(rv_lines);
        rv_send(&s_wet);
        memset(in, 0, sizeof in);
        in[0] = 1.0f;
        for (n = 0; n < SECOND; n++)
            rv_frame(mix, in);
        rv_image(rv_lines);              /* the same lines: the reverb goes on, to its room */
        for (n = 0; n < SECOND; n++)
            rv_frame(mix, in);
        memcpy(lines, rv_lines, sizeof lines);
        lines[12] = lines[13] = 9000;    /* other lines: it starts again */
        rv_image(lines);
        rv_send(&s_wet);
        for (n = 0; n < SECOND; n++) {
            rv_frame(mix, in);
            added |= !rv_silent(mix);
        }
        check("run again with the other lines", added);
    } else if (!strcmp(c, "reverb_other")) {  /* an image not laid out as JSRF's, then one that is */
        static float mix[BINS][FRAME], in[FRAME];
        unsigned n;
        int silent = 1, added = 0;

        rv_image_of(3, rv_lines);        /* the reverb as JSRF's, in another image */
        rv_send(&s_wet);
        for (n = 0; n < SECOND; n++) {
            memset(in, 0, sizeof in);
            in[0] = 1.0f;
            rv_frame(mix, in);
            silent &= rv_silent(mix);
        }
        rv_image(rv_lines);
        rv_send(&s_wet);
        for (n = 0; n < SECOND; n++) {
            memset(in, 0, sizeof in);
            in[0] = 1.0f;
            rv_frame(mix, in);
            added |= !rv_silent(mix);
        }
        check("nothing added from another image, then the reverb from JSRF's", silent && added);
    } else if (!strcmp(c, "reverb_lines")) {  /* lines too long, and one too short */
        static float mix[BINS][FRAME], in[FRAME];
        unsigned lines[RV_LINES], n;
        int silent = 1;

        memcpy(lines, rv_lines, sizeof lines);
        lines[12] = lines[13] = 0x10000;
        rv_image(lines);
        rv_send(&s_wet);
        for (n = 0; n < SECOND; n++) {
            memset(in, 0, sizeof in);
            in[0] = 1.0f;
            rv_frame(mix, in);
            silent &= rv_silent(mix);
        }
        memcpy(lines, rv_lines, sizeof lines);
        lines[4] = 3;
        rv_image(lines);
        rv_send(&s_wet);
        for (n = 0; n < SECOND; n++)
            rv_frame(mix, in);
        silent &= rv_silent(mix);
        check("nothing added with lines that don't fit", silent);
    } else if (!strncmp(c, "chosen", 6)) {
        /* audioeffects=1 passed in ("chosen", "chosen_env") or 0
         * ("chosen_off", "chosen_off_env"), and run.py's environment: nothing
         * for the first of each, and RECOMP_APU_GPFX=0 or 1, which wins, for
         * the second. */
        static float in[BINS][FRAME], out[BINS][FRAME];
        int set = strncmp(c, "chosen_off", 10) != 0;
        int want = strstr(c, "_env") ? !set : set, ran;

        apu_gpfx_choose(set);
        tone(in, 0, QUIET);
        memcpy(out, in, sizeof out);
        ran = apu_gpfx_frame(&out[0][0], BINS, FRAME);
        check(want ? "run as the setting or the environment has it"
                   : "nothing run, as the setting or the environment has it",
              apu_gpfx_on() == want && ran == want && (want || !memcmp(in, out, sizeof in)));
    } else if (!strcmp(c, "reverb_off")) {   /* nothing, with the switch unset */
        static float mix[BINS][FRAME], in[FRAME];
        unsigned n;
        int ran = 0, same = 1;

        rv_image(rv_lines);
        rv_send(&s_wet);
        for (n = 0; n < SECOND; n++) {
            memset(in, 0, sizeof in);
            in[0] = 1.0f;
            ran |= rv_frame(mix, in);
            same &= rv_silent(mix) && rv_others(mix, in);
        }
        check("no reverb with the switch off", !ran && same);
    } else {
        printf("apu_gpfx: no case %s\n", c);
        return 1;
    }
    return s_failed;
}
