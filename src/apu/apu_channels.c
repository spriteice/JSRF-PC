/* The two sides of the sound (Developer > Logging > RECOMP_APU_CHANNELS).
 *
 * Now and then the sound drops to one side: everything on the other side
 * goes, or only some of it is left, for a moment or until a later scene
 * (known bug 5). Nothing in the log said what either side was carrying, so a
 * log from such a run could not say which side went or why.
 *
 * This watches the stereo pair the encode processor hands the device
 * (apu_dsp.c), by its peaks, a tenth of a second at a time:
 *
 * - When one side has stayed CH_QUIET_RATIO (20 dB) or more below the other
 *   for CH_ONSET tenths while the other plays, a line says which, with both
 *   sides' peaks; another says when it is back.
 * - With the first line of a spell, at most once every CH_DETAIL_GAP tenths,
 *   the tenth of a second after it, voice by voice: each voice playing, the
 *   lowest and highest value on each of its channels (a channel stuck at one
 *   value shows as one), and its gain into each mixbin (apu_vp.c), then each
 *   mixbin's peak. That says whether one voice lost a side, a voice was sent
 *   to one side only, or the whole mix lost it -- or, with both sides full
 *   here while one is silent on the speakers, that it is lost after us.
 * - Every CH_SUMMARY tenths, a summary: each side's peak, how long it was
 *   one-sided (counting spells too short for a line), and the widest gap
 *   between the sides.
 *
 * Every line starts [AUDIO-CH]. Times are seconds of sound made since the
 * first frame. Off, it costs a test of one flag per frame and per voice.
 */
#include "apu_channels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CH_RATE        48000           /* the encode processor's samples a second */
#define CH_WINDOW      (CH_RATE / 10)  /* a tenth of a second */
#define CH_FULL        32767           /* full scale, 0 dBFS */
#define CH_PLAYING     (CH_FULL / 100) /* -40 dBFS: a side below this is not playing */
#define CH_SHOWN       (CH_FULL / 1000) /* -60 dBFS: a mixbin below this is left out */
#define CH_QUIET_RATIO 10              /* 20 dB: a side this far below the other is quiet */
#define CH_ONSET       2               /* tenths in a row that open or close a spell */
#define CH_SUMMARY     100             /* tenths between summaries: 10 s */
#define CH_DETAIL_GAP  100             /* tenths between voice reports: 10 s */

int g_apu_ch_capture;

static int s_on = -1;
static int s_samples;                  /* samples in this tenth so far */
static int s_pk[2];                    /* this tenth's peaks, left and right */
static unsigned long s_tenths;         /* tenths since the first frame */

static int s_side = -1;                /* the side quiet in the last tenth, or -1 */
static int s_run;                      /* tenths in a row it has been */
static int s_spell = -1;               /* the side of the open spell, or -1 */
static int s_back;                     /* tenths in a row the spell's side has not been quiet */
static unsigned long s_spell_start;
static unsigned long s_last_detail;

static int s_sum_pk[2];                /* for the summary */
static int s_sum_onesided, s_sum_spells;
static int s_gap_pk[2];                /* the tenth with the widest gap */
static float s_gap_db;
static int s_gap_seen;                 /* a tenth with something playing */

static int s_capture_left;             /* samples still to capture */
static float s_bin_pk[NUM_MIXBINS];
static struct {
    int seen, lpf;
    unsigned channels;
    float lo[2], hi[2];
    int bin[8];
    float gain[8];
} s_voice[MCPX_HW_MAX_VOICES];

static const char *const k_side[2] = { "left", "right" };

static float ch_db(float peak, float full)
{
    return 20.0f * log10f(peak / full);
}

/* A peak as the lines show it: "-12 dBFS", or "silent". */
static const char *ch_level(int peak, char buf[16])
{
    if (!peak) return "silent";
    snprintf(buf, 16, "%.0f dBFS", (double)ch_db((float)peak, CH_FULL));
    return buf;
}

/* The tenth of a second after a spell opened: each voice that played, and
 * each mixbin. */
static void ch_report_voices(void)
{
    char line[512], a[16];
    int n, b, v, c, shown = 0;

    n = snprintf(line, sizeof line, "[AUDIO-CH]   in the next 0.1 s, mixbins:");
    for (b = 0; b < NUM_MIXBINS; b++)
        if (s_bin_pk[b] * CH_FULL >= CH_SHOWN && n < (int)sizeof line - 16) {
            n += snprintf(line + n, sizeof line - n, " %d:%.0f", b,
                          (double)ch_db(s_bin_pk[b], 1.0f));
            shown++;
        }
    fprintf(stderr, "%s%s\n", line, shown ? " dBFS" : " all below -60 dBFS");

    for (v = 0; v < MCPX_HW_MAX_VOICES; v++) {
        if (!s_voice[v].seen) continue;
        n = snprintf(line, sizeof line, "[AUDIO-CH]   voice %d, %s%s:", v,
                     s_voice[v].channels == 2 ? "stereo" : "mono",
                     s_voice[v].lpf ? ", low-pass" : "");
        for (c = 0; c < (int)s_voice[v].channels; c++) {
            float lo = s_voice[v].lo[c], hi = s_voice[v].hi[c];
            float pk = fmaxf(fabsf(lo), fabsf(hi));
            n += snprintf(line + n, sizeof line - n, " channel %d %s (%.3f..%.3f);",
                          c, ch_level((int)(pk * CH_FULL), a), (double)lo, (double)hi);
        }
        n += snprintf(line + n, sizeof line - n, " into");
        for (b = 0; b < 8; b++)
            if (s_voice[v].gain[b] > 0.0f && n < (int)sizeof line - 32)
                n += snprintf(line + n, sizeof line - n, " bin %d (channel %u) %.1f dB,",
                              s_voice[v].bin[b], b % s_voice[v].channels,
                              (double)ch_db(s_voice[v].gain[b], 1.0f));
        if (line[n - 1] == ',') line[n - 1] = 0;
        else snprintf(line + n, sizeof line - n, " no bin");
        fprintf(stderr, "%s\n", line);
    }
    fflush(stderr);
}

static void ch_tenth(void)
{
    char a[16], b[16];
    int loud = s_pk[1] > s_pk[0], quiet = -1, s;

    s_tenths++;
    for (s = 0; s < 2; s++)
        if (s_pk[s] > s_sum_pk[s]) s_sum_pk[s] = s_pk[s];
    if (s_pk[loud] >= CH_PLAYING) {
        float gap = ch_db((float)s_pk[loud], (float)(s_pk[!loud] ? s_pk[!loud] : 1));
        if (s_pk[!loud] * CH_QUIET_RATIO <= s_pk[loud]) quiet = !loud;
        if (!s_gap_seen || gap > s_gap_db) {
            s_gap_seen = 1;
            s_gap_db = gap;
            s_gap_pk[0] = s_pk[0];
            s_gap_pk[1] = s_pk[1];
        }
    }
    if (quiet >= 0) s_sum_onesided++;

    s_run = quiet >= 0 && quiet == s_side ? s_run + 1 : quiet >= 0;
    s_side = quiet;
    if (s_spell < 0 && s_run >= CH_ONSET) {
        s_spell = quiet;
        s_back = 0;
        s_spell_start = s_tenths - CH_ONSET;
        s_sum_spells++;
        fprintf(stderr, "[AUDIO-CH] after %.1f s of sound: the %s side went quiet:"
                " left %s, right %s\n", s_spell_start / 10.0, k_side[quiet],
                ch_level(s_pk[0], a), ch_level(s_pk[1], b));
        fflush(stderr);
        if (!s_last_detail || s_tenths - s_last_detail >= CH_DETAIL_GAP) {
            s_last_detail = s_tenths;
            memset(s_voice, 0, sizeof s_voice);
            memset(s_bin_pk, 0, sizeof s_bin_pk);
            s_capture_left = CH_WINDOW;
            g_apu_ch_capture = 1;
        }
    } else if (s_spell >= 0) {
        s_back = quiet == s_spell ? 0 : s_back + 1;
        if (s_back >= CH_ONSET) {
            fprintf(stderr, "[AUDIO-CH] after %.1f s of sound: the %s side is back"
                    " after %.1f s: left %s, right %s\n", s_tenths / 10.0,
                    k_side[s_spell], (s_tenths - CH_ONSET - s_spell_start) / 10.0,
                    ch_level(s_pk[0], a), ch_level(s_pk[1], b));
            fflush(stderr);
            s_spell = -1;
        }
    }

    if (s_tenths % CH_SUMMARY == 0) {
        fprintf(stderr, "[AUDIO-CH] the last %d s: peaks left %s, right %s;"
                " one-sided for %.1f s, quiet spells: %d", CH_SUMMARY / 10,
                ch_level(s_sum_pk[0], a), ch_level(s_sum_pk[1], b),
                s_sum_onesided / 10.0, s_sum_spells);
        if (s_gap_seen)
            fprintf(stderr, "; the widest gap %.0f dB (left %s, right %s)\n",
                    (double)s_gap_db, ch_level(s_gap_pk[0], a), ch_level(s_gap_pk[1], b));
        else
            fprintf(stderr, "; nothing playing\n");
        fflush(stderr);
        memset(s_sum_pk, 0, sizeof s_sum_pk);
        s_sum_onesided = s_sum_spells = s_gap_seen = 0;
    }
}

void apu_ch_frame(const int16_t out[NUM_SAMPLES_PER_FRAME][2],
                  const float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    int i, s, b;

    if (s_on < 0) {
        const char *v = getenv("RECOMP_APU_CHANNELS");
        s_on = v && atoi(v) ? 1 : 0;
        if (s_on)
            fprintf(stderr, "[AUDIO-CH] on: a line when one side is %.0f dB below the"
                    " other for %.1f s, a summary every %d s\n",
                    20.0 * log10(CH_QUIET_RATIO), CH_ONSET / 10.0, CH_SUMMARY / 10);
    }
    if (!s_on) return;

    for (i = 0; i < NUM_SAMPLES_PER_FRAME; i++)
        for (s = 0; s < 2; s++) {
            int a = out[i][s] < 0 ? -out[i][s] : out[i][s];
            if (a > s_pk[s]) s_pk[s] = a;
        }

    if (g_apu_ch_capture) {
        for (b = 0; b < NUM_MIXBINS; b++)
            for (i = 0; i < NUM_SAMPLES_PER_FRAME; i++)
                s_bin_pk[b] = fmaxf(s_bin_pk[b], fabsf(mixbins[b][i]));
        s_capture_left -= NUM_SAMPLES_PER_FRAME;
        if (s_capture_left <= 0) {
            g_apu_ch_capture = 0;
            ch_report_voices();
        }
    }

    s_samples += NUM_SAMPLES_PER_FRAME;
    if (s_samples >= CH_WINDOW) {
        ch_tenth();
        s_samples = 0;
        s_pk[0] = s_pk[1] = 0;
    }
}

void apu_ch_voice(unsigned v, unsigned channels,
                  const float samples[NUM_SAMPLES_PER_FRAME][2],
                  const int bin[8], const float gain[8], int lpf)
{
    unsigned c;
    int i;

    if (v >= MCPX_HW_MAX_VOICES) return;
    if (!s_voice[v].seen) {
        s_voice[v].seen = 1;
        for (c = 0; c < 2; c++)
            s_voice[v].lo[c] = s_voice[v].hi[c] = samples[0][c];
    }
    s_voice[v].channels = channels;
    s_voice[v].lpf = lpf;
    memcpy(s_voice[v].bin, bin, sizeof s_voice[v].bin);
    memcpy(s_voice[v].gain, gain, sizeof s_voice[v].gain);
    for (i = 0; i < NUM_SAMPLES_PER_FRAME; i++)
        for (c = 0; c < channels; c++) {
            s_voice[v].lo[c] = fminf(s_voice[v].lo[c], samples[i][c]);
            s_voice[v].hi[c] = fmaxf(s_voice[v].hi[c], samples[i][c]);
        }
}
