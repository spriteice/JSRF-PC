/* The GP's effects settings, followed.
 *
 * On the console the GP, the audio DSP that runs the effects, plays the
 * title's effects image (JSRF's is Media\Sounds\dsstdfx.bin): a reverb, the
 * crosstalk that places 3D sounds on the speakers, and the sends and mixers
 * after them. Here the GP is a stub, so none of them runs, and the settings
 * DirectSound gives them go nowhere. This follows those settings, so that the
 * log can show them, and so that our own versions of the effects can use
 * them later as the GP would.
 *
 * DirectSound talks to the GP through the GP's scratch memory, in guest RAM.
 * It puts the image there, then a block of six words at GPFX_BLOCK, and rings
 * the doorbell, which is the block's fifth word. The GP's kernel copies the
 * block into its own memory each frame and does what the doorbell says:
 *
 * - GPFX_LOAD_CODE: a new image. The kernel clears the effects' memory, from
 *   GPFX_X_STATE up, and loads their code.
 * - GPFX_LOAD_STATE: it copies GPFX_LENGTH words from GPFX_SOURCE in scratch
 *   to GPFX_X_STATE + GPFX_DEST in its memory.
 *
 * Downloading an image sends both, with every effect's state. Changing an
 * effect's settings sends the second alone, with the words DirectSound wrote.
 * Then the kernel clears the doorbell, which the port does in its place
 * (apu_dsp.c), just after calling this. So this keeps what the GP's memory
 * holds from GPFX_X_STATE up as the commands leave it. Of what the effects
 * themselves write there as they run, it keeps one thing: each effect, the
 * first time it runs, sets GPFX_STARTED in its flags.
 *
 * Before DirectSound works out a room's reverb settings, it reads the
 * reverb's state back through the window where the CPU sees the GP's memory
 * (APU_GPFX_X_WINDOW), for the flags and its delay lines' lengths. That
 * window is plain memory here, so this keeps the copy there too
 * (apu_gpfx_window), and DirectSound reads what the GP would hold.
 *
 * The image's effect table, after its state, says where each effect's state
 * is. In the file that is a byte offset, but DirectSound rewrites the table
 * in scratch before it rings for the download, and then it is where the CPU
 * sees that state in the GP's memory, through the APU's registers
 * (APU_GPFX_X_WINDOW). JSRF tells DirectSound that effect GPFX_REVERB is its
 * reverb and GPFX_XTALK its crosstalk, and apu_gpfx_studied says whether the
 * image is laid out as the one this was written from.
 *
 * Developer > Experimental > Xbox audio effects (RECOMP_APU_GPFX) runs our
 * own effects on the mixbins, before the port's fold takes them to stereo
 * (apu_gpfx_frame, below). They aim to sound good and cost little, not to
 * copy the console's. So far that's a limiter of their own for the 3D bins.
 *
 * Developer > Logging > RECOMP_APU_STATS: a line when an image loads, and
 * one when DirectSound changes an effect's settings, with the words it
 * changed. With the effects on, a line when they start, and one a second
 * on how hard the 3D bins' limiter works. Every line starts
 * "[APU] effects:".
 */
#include "apu_gpfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DirectSound's command block, at byte offsets in the GP's scratch memory. */
#define GPFX_BLOCK       0x800u   /* the block's six words */
#define GPFX_DOORBELL    0x810u   /* the doorbell: the block's word GPFX_COMMAND */
#define GPFX_IMAGE       0x818u   /* the image, after the block: code, state, table */
enum {                            /* the block's words */
    GPFX_DEST,                    /* where the state goes, from GPFX_X_STATE */
    GPFX_CODE,                    /* the image's code, in words */
    GPFX_SOURCE,                  /* the state's byte offset in scratch */
    GPFX_LENGTH,                  /* the state, in words */
    GPFX_COMMAND,                 /* the doorbell */
    GPFX_BLOCK_WORDS = 6
};
#define GPFX_LOAD_CODE   1u       /* the doorbell's bits */
#define GPFX_LOAD_STATE  2u

/* The GP's X memory, in DSP words of 24 bits. */
#define GPFX_X_WORDS     0x1000u
#define GPFX_X_STATE     0x80u    /* where the effects' state is loaded */
#define GPFX_WORD        0xFFFFFFu
typedef char gpfx_window_holds_x[APU_GPFX_X_BYTES == 4 * GPFX_X_WORDS ? 1 : -1];

/* The image's effect table: the number of effects and a word not used here,
 * then GPFX_ENTRY_WORDS for each effect, as DirectSound leaves them when it
 * rings for the download. */
#define GPFX_TABLE_HEAD  2u
#define GPFX_ENTRY_WORDS 8u
#define GPFX_ENTRY_STATE 2u       /* an entry's word: its state, where the CPU sees it */
#define GPFX_ENTRY_BYTES 3u       /* and the state's length in bytes */
#define GPFX_MAX_EFFECTS 32u

/* An effect's word that holds its flags, and the flag its code sets the
 * first time it runs, when it starts itself. Every effect in JSRF's image
 * begins so, and the GP runs them all each frame. */
#define GPFX_FLAGS       4u
#define GPFX_STARTED     1u

/* The image studied, JSRF's: its effects, and the length of the state of
 * the two it names. */
#define GPFX_STUDIED_EFFECTS 20u
#define GPFX_REVERB          0u
#define GPFX_REVERB_WORDS    554u
#define GPFX_XTALK           1u
#define GPFX_XTALK_WORDS     93u

#define GPFX_LOG_PER_LINE    8u   /* words on each line of the log */

static uint32_t s_x[GPFX_X_WORDS];      /* the GP's X memory, as the commands leave it */
static uint32_t s_before[GPFX_X_WORDS]; /* and as it was before the last, for the log */
static struct { uint32_t x, words; } s_fx[GPFX_MAX_EFFECTS];
static unsigned s_effects;              /* effects in the image */
static unsigned s_found;                /* and those whose state was found */
static int s_studied;
static int s_log = -1;
static uint8_t *s_window;               /* where the CPU sees the GP's X memory, or NULL */
static int s_window_said;

/* The dword at byte offset `at` in the window, if all of it is there. */
static int ram_dword(const uint8_t *ram, uint32_t size, uint64_t at, uint32_t *v)
{
    if (at + 4 > size)
        return 0;
    memcpy(v, ram + at, 4);
    return 1;
}

/* A new image: the GP's memory cleared, and each effect's state found from
 * the image's effect table. An effect whose state is not in the effects'
 * memory is left with none. */
static void load_image(const uint8_t *ram, uint32_t size, uint32_t scratch,
                       const uint32_t blk[GPFX_BLOCK_WORDS])
{
    uint64_t table = (uint64_t)scratch + GPFX_IMAGE
                   + 4ull * ((uint64_t)blk[GPFX_CODE] + blk[GPFX_LENGTH]);
    uint32_t count, e;

    memset(s_x, 0, sizeof s_x);
    memset(s_fx, 0, sizeof s_fx);
    s_effects = 0;
    s_found = 0;
    s_studied = 0;
    if (!ram_dword(ram, size, table, &count) || count > GPFX_MAX_EFFECTS)
        return;
    for (e = 0; e < count; e++) {
        uint64_t entry = table + 4ull * (GPFX_TABLE_HEAD + GPFX_ENTRY_WORDS * e);
        uint32_t at, bytes;
        uint64_t x;

        if (!ram_dword(ram, size, entry + 4 * GPFX_ENTRY_STATE, &at)
            || !ram_dword(ram, size, entry + 4 * GPFX_ENTRY_BYTES, &bytes))
            break;
        if (at < APU_GPFX_X_WINDOW || (at - APU_GPFX_X_WINDOW) % 4 || bytes % 4)
            continue;
        x = (at - APU_GPFX_X_WINDOW) / 4;
        if (x < GPFX_X_STATE || x + bytes / 4 > GPFX_X_WORDS)
            continue;
        s_fx[e].x = (uint32_t)x;
        s_fx[e].words = bytes / 4;
        s_found++;
    }
    s_effects = e;
    s_studied = s_effects == GPFX_STUDIED_EFFECTS && s_found == s_effects
             && s_fx[GPFX_REVERB].words == GPFX_REVERB_WORDS
             && s_fx[GPFX_XTALK].words == GPFX_XTALK_WORDS;
}

/* The state the block names, copied into the GP's memory. 0 if it is not
 * all in the window and in the GP's memory, and then nothing is copied. */
static int load_state(const uint8_t *ram, uint32_t size, uint32_t scratch,
                      const uint32_t blk[GPFX_BLOCK_WORDS])
{
    uint64_t x = (uint64_t)GPFX_X_STATE + blk[GPFX_DEST];
    uint64_t from = (uint64_t)scratch + blk[GPFX_SOURCE];
    uint32_t n = blk[GPFX_LENGTH], i;

    if (x + n > GPFX_X_WORDS || from + 4ull * n > size)
        return 0;
    for (i = 0; i < n; i++) {
        uint32_t v;
        memcpy(&v, ram + from + 4ull * i, 4);
        s_x[x + i] = v & GPFX_WORD;
    }
    return 1;
}

/* The GP's next frame, as far as the copy follows it: each effect that
 * hasn't started starts, as a new image's do, or one whose flags a change
 * cleared. */
static void start_effects(void)
{
    unsigned e;

    for (e = 0; e < s_effects; e++)
        if (s_fx[e].words > GPFX_FLAGS)
            s_x[s_fx[e].x + GPFX_FLAGS] |= GPFX_STARTED;
}

/* The copy, where the CPU reads the GP's memory back. A word of the GP's is
 * a little-endian dword there, with its top byte clear. */
static void show_copy(void)
{
    if (s_window)
        memcpy(s_window + 4 * GPFX_X_STATE, s_x + GPFX_X_STATE,
               4 * (GPFX_X_WORDS - GPFX_X_STATE));
}

static void log_image(const uint32_t blk[GPFX_BLOCK_WORDS])
{
    fprintf(stderr, "[APU] effects: an image loaded, %u effects in %u words of code and"
            " %u of state, %u of them found in the GP's memory; %s\n", s_effects,
            blk[GPFX_CODE], blk[GPFX_LENGTH], s_found,
            s_studied ? "laid out as JSRF's" : "not laid out as JSRF's");
}

/* The words a change sent, effect by effect, with those that changed. */
static void log_change(uint32_t x0, uint32_t n)
{
    uint32_t outside = n, e;

    for (e = 0; e < s_effects; e++) {
        uint32_t lo = s_fx[e].x > x0 ? s_fx[e].x : x0;
        uint32_t end = s_fx[e].x + s_fx[e].words, hi = end < x0 + n ? end : x0 + n;
        uint32_t x, changed = 0, shown = 0;
        char line[GPFX_LOG_PER_LINE * 16 + 16];
        int len = 0;

        if (lo >= hi)
            continue;
        outside -= hi - lo < outside ? hi - lo : outside;
        for (x = lo; x < hi; x++)
            changed += s_x[x] != s_before[x];
        fprintf(stderr, "[APU] effects: effect %u's settings, words %u to %u sent,"
                " %u changed%s\n", e, lo - s_fx[e].x, hi - 1 - s_fx[e].x, changed,
                changed ? ":" : "");
        for (x = lo; x < hi; x++) {
            if (s_x[x] == s_before[x])
                continue;
            len += snprintf(line + len, sizeof line - (size_t)len, " %u=%06X",
                            x - s_fx[e].x, s_x[x]);
            if (++shown % GPFX_LOG_PER_LINE == 0 || shown == changed) {
                fprintf(stderr, "[APU] effects:  %s\n", line);
                len = 0;
            }
        }
    }
    if (outside)
        fprintf(stderr, "[APU] effects: %u words sent outside every effect's state\n",
                outside);
}

static int log_on(void)
{
    if (s_log < 0)
        s_log = getenv("RECOMP_APU_STATS") ? 1 : 0;
    return s_log;
}

void apu_gpfx_command(const uint8_t *ram, uint32_t ram_size, uint32_t doorbell)
{
    uint32_t blk[GPFX_BLOCK_WORDS], scratch, cmd, i;

    log_on();
    if (!ram || doorbell < GPFX_DOORBELL
        || (uint64_t)doorbell - GPFX_DOORBELL + GPFX_IMAGE > ram_size)
        return;
    scratch = doorbell - GPFX_DOORBELL;
    for (i = 0; i < GPFX_BLOCK_WORDS; i++) {
        memcpy(&blk[i], ram + scratch + GPFX_BLOCK + 4 * i, 4);
        blk[i] &= GPFX_WORD;
    }
    cmd = blk[GPFX_COMMAND];
    if (!(cmd & (GPFX_LOAD_CODE | GPFX_LOAD_STATE)))
        return;
    if (s_log)
        memcpy(s_before, s_x, sizeof s_x);
    if (cmd & GPFX_LOAD_CODE)
        load_image(ram, ram_size, scratch, blk);
    if ((cmd & GPFX_LOAD_STATE) && !load_state(ram, ram_size, scratch, blk)) {
        if (s_log)
            fprintf(stderr, "[APU] effects: command %u not followed: %u words from"
                    " 0x%X in scratch to X:0x%X are not all there\n", cmd,
                    blk[GPFX_LENGTH], blk[GPFX_SOURCE], GPFX_X_STATE + blk[GPFX_DEST]);
    } else if (s_log && (cmd & GPFX_LOAD_CODE)) {
        log_image(blk);
    } else if (s_log) {
        log_change(GPFX_X_STATE + blk[GPFX_DEST], blk[GPFX_LENGTH]);
    }
    start_effects();
    show_copy();
    if (s_log && s_window && !s_window_said) {
        s_window_said = 1;
        fprintf(stderr, "[APU] effects: kept at 0x%08X too, where DirectSound reads"
                " the GP's memory back\n", APU_GPFX_X_WINDOW);
    }
    if (s_log)
        fflush(stderr);
}

void apu_gpfx_window(uint8_t *x_window)
{
    s_window = x_window;
    s_window_said = 0;
}

const uint32_t *apu_gpfx_effect(unsigned e, unsigned *words)
{
    if (e >= s_effects || !s_fx[e].words) {
        if (words)
            *words = 0;
        return NULL;
    }
    if (words)
        *words = s_fx[e].words;
    return s_x + s_fx[e].x;
}

int apu_gpfx_studied(void)
{
    return s_studied;
}

/* The 3D bins' limiter. JSRF plays its sound effects as 3D voices into the
 * crosstalk bins, GPFX_FX_BINS of them from GPFX_FX_FIRST, and at busy
 * moments they add up to well above full scale: 2.7 in one player's
 * session. The console's 24-bit bins stop at full scale. Folded to stereo as
 * they are, they push the output into its limiter (apu_dsp.c), and that
 * turns the music and voices down with them. So they go through a limiter of
 * their own first, at full scale. It looks one frame ahead: the bins come out
 * a frame late, and the gain has come down by the time a loud frame comes
 * out, so nothing is cut off. The gain glides within each frame and comes
 * back over GPFX_FX_RELEASE_MS, as the output's does. It's one gain for the
 * four bins, so a sound doesn't move between the speakers. */
#define GPFX_FX_FIRST       6u
#define GPFX_FX_BINS        4u
#define GPFX_FX_CEILING     1.0f     /* full scale */
#define GPFX_FX_RELEASE_MS  150.0f
#define GPFX_RATE           48000u   /* the mixbins' samples a second */
#define GPFX_FRAME          32u      /* the samples in a frame of them */
#define GPFX_MS             1000.0f
#define GPFX_LIMITED        0.999f   /* a gain under this is turned down, for the log */
#define GPFX_DB             20.0f    /* dB from a gain: GPFX_DB * log10 */
#define GPFX_PERCENT        100u

static int s_on = -1;                    /* RECOMP_APU_GPFX */
static int s_said;                       /* the line when the effects start */
static float s_late[GPFX_FX_BINS][GPFX_FRAME];  /* last frame's 3D bins, still to come out */
static float s_late_peak;                /* and their peak */
static float s_gain = 1.0f;              /* the limiter's gain, as the last frame ended */
static struct {
    unsigned frames, turned_down;
    float peak, least;
} s_second = { 0, 0, 0.0f, 1.0f };       /* for the log, a second at a time */

/* The log's line on the second gone, if the 3D bins had any sound in it. */
static void log_second(void)
{
    if (s_second.peak > 0.0f) {
        if (s_second.turned_down)
            fprintf(stderr, "[APU] effects: the 3D bins peaked at %.2f of full scale, and"
                    " were turned down for %u%% of the second, by up to %.1f dB\n",
                    (double)s_second.peak,
                    GPFX_PERCENT * s_second.turned_down / s_second.frames,
                    (double)(-GPFX_DB * log10f(s_second.least)));
        else
            fprintf(stderr, "[APU] effects: the 3D bins peaked at %.2f of full scale, and"
                    " weren't turned down\n", (double)s_second.peak);
        fflush(stderr);
    }
    s_second.frames = s_second.turned_down = 0;
    s_second.peak = 0.0f;
    s_second.least = 1.0f;
}

int apu_gpfx_frame(float *mixbins, unsigned bins, unsigned samples)
{
    float now[GPFX_FX_BINS][GPFX_FRAME], peak = 0.0f, back, end;
    unsigned b, i;

    if (s_on < 0) {
        const char *v = getenv("RECOMP_APU_GPFX");
        s_on = v && atoi(v) ? 1 : 0;
    }
    if (!s_on || !mixbins || bins < GPFX_FX_FIRST + GPFX_FX_BINS || samples != GPFX_FRAME)
        return 0;
    if (!s_said && log_on()) {
        s_said = 1;
        fprintf(stderr, "[APU] effects: the 3D bins (%u to %u) through a limiter of their"
                " own, at full scale, before the fold (RECOMP_APU_GPFX)\n",
                GPFX_FX_FIRST, GPFX_FX_FIRST + GPFX_FX_BINS - 1);
        fflush(stderr);
    }

    for (b = 0; b < GPFX_FX_BINS; b++) {
        const float *bin = mixbins + (size_t)(GPFX_FX_FIRST + b) * samples;
        for (i = 0; i < samples; i++) {
            float a = fabsf(bin[i]);
            now[b][i] = bin[i];
            if (a > peak)
                peak = a;
        }
    }

    /* The gain at the end of the frame coming out: back up toward unity,
     * but no higher than keeps that frame and the next under the ceiling.
     * It starts where the last one ended, which kept this frame under. */
    back = 1.0f - expf(-(float)samples * GPFX_MS / (GPFX_RATE * GPFX_FX_RELEASE_MS));
    end = s_gain + (1.0f - s_gain) * back;
    if (s_late_peak * end > GPFX_FX_CEILING)
        end = GPFX_FX_CEILING / s_late_peak;
    if (peak * end > GPFX_FX_CEILING)
        end = GPFX_FX_CEILING / peak;
    for (b = 0; b < GPFX_FX_BINS; b++) {
        float *bin = mixbins + (size_t)(GPFX_FX_FIRST + b) * samples;
        for (i = 0; i < samples; i++)
            bin[i] = s_late[b][i] * (s_gain + (end - s_gain) * (float)(i + 1) / (float)samples);
    }
    memcpy(s_late, now, sizeof s_late);

    if (log_on()) {
        float least = s_gain < end ? s_gain : end;
        if (s_late_peak > s_second.peak)
            s_second.peak = s_late_peak;
        if (least < GPFX_LIMITED)
            s_second.turned_down++;
        if (least < s_second.least)
            s_second.least = least;
        if (++s_second.frames >= GPFX_RATE / GPFX_FRAME)
            log_second();
    }
    s_late_peak = peak;
    s_gain = end;
    return 1;
}
