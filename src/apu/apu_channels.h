/* Developer > Logging > RECOMP_APU_CHANNELS: the two sides of the sound
 * (apu_channels.c). */
#pragma once

#include <stdint.h>
#include "apu_regs.h"

/* Set while the voices are to report what they play (apu_ch_voice). */
extern int g_apu_ch_capture;

/* One encode-processor frame: the stereo pair it hands the device, and the
 * mixbins it was made from. */
void apu_ch_frame(const int16_t out[NUM_SAMPLES_PER_FRAME][2],
                  const float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);

/* One voice's frame as it goes into the mixbins: its samples after the
 * filters, and the gain into each of its eight bins (channel b % channels
 * feeds bin[b]). Only while g_apu_ch_capture is set. */
void apu_ch_voice(unsigned v, unsigned channels,
                  const float samples[NUM_SAMPLES_PER_FRAME][2],
                  const int bin[8], const float gain[8], int lpf);
