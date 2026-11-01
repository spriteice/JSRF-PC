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
 * the 3D bins' limiter, with RECOMP_APU_GPFX set and without.
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
    } else {
        printf("apu_gpfx: no case %s\n", c);
        return 1;
    }
    return s_failed;
}
