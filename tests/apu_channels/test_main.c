/* Run with: python tests/apu_channels/run.py
 *
 * A made-up output for src/apu/apu_channels.c, 32 samples at a time as the
 * encode processor hands it over: a 440 Hz tone on each side at a level of
 * its own, its mixbins (0 left, 1 right), and the stereo voice it came from
 * (68, as JSRF's music voice), told to apu_ch_voice while it asks. The
 * output is the [AUDIO-CH] lines, which run.py reads. The argument picks the
 * case.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "apu_channels.h"

#define RATE 48000
#define TONE 440.0
#define PI   3.14159265358979

static float s_mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];

/* seconds of the tone at left and right (0 to 1) */
static void play(double seconds, float left, float right)
{
    static unsigned long n;
    int16_t out[NUM_SAMPLES_PER_FRAME][2];
    float voice[NUM_SAMPLES_PER_FRAME][2];
    const int bin[8] = { 0, 1, 0, 0, 0, 0, 0, 0 };
    const float gain[8] = { left, right, 0, 0, 0, 0, 0, 0 };
    long frames = (long)(seconds * RATE / NUM_SAMPLES_PER_FRAME + 0.5);
    int i;

    while (frames-- > 0) {
        for (i = 0; i < NUM_SAMPLES_PER_FRAME; i++, n++) {
            float s = (float)sin(2.0 * PI * TONE * n / RATE);
            voice[i][0] = voice[i][1] = s;
            s_mixbins[0][i] = s * left;
            s_mixbins[1][i] = s * right;
            out[i][0] = (int16_t)(s_mixbins[0][i] * 32767.0f);
            out[i][1] = (int16_t)(s_mixbins[1][i] * 32767.0f);
        }
        if (g_apu_ch_capture)
            apu_ch_voice(68, 2, (const float (*)[2])voice, bin, gain, 0);
        apu_ch_frame((const int16_t (*)[2])out,
                     (const float (*)[NUM_SAMPLES_PER_FRAME])s_mixbins);
    }
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";

    if (!strcmp(c, "even")) {                   /* both sides all along */
        play(12.0, 0.5f, 0.5f);
    } else if (!strcmp(c, "right_gone")) {      /* the right side gone for 1.5 s */
        play(2.0, 0.5f, 0.5f);
        play(1.5, 0.5f, 0.0f);
        play(8.5, 0.5f, 0.5f);
    } else if (!strcmp(c, "left_low")) {        /* a little left on the left: 22 dB down */
        play(2.0, 0.5f, 0.5f);
        play(1.0, 0.04f, 0.5f);
        play(9.0, 0.5f, 0.5f);
    } else if (!strcmp(c, "blip")) {            /* a tenth too short for a line */
        play(2.0, 0.5f, 0.5f);
        play(0.1, 0.5f, 0.0f);
        play(9.9, 0.5f, 0.5f);
    } else if (!strcmp(c, "twice")) {           /* a second spell within 10 s */
        play(2.0, 0.5f, 0.5f);
        play(1.0, 0.5f, 0.0f);
        play(2.0, 0.5f, 0.5f);
        play(1.0, 0.5f, 0.0f);
        play(6.0, 0.5f, 0.5f);
    } else if (!strcmp(c, "silence")) {         /* nothing playing */
        play(12.0, 0.0f, 0.0f);
    } else {
        printf("apu_channels: no case %s\n", c);
        return 1;
    }
    return 0;
}
