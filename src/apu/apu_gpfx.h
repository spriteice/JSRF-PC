/* The GP's effects settings, followed (apu_gpfx.c). */
#pragma once

#include <stdint.h>

/* DirectSound has rung the GP's doorbell: `doorbell` is its offset in the
 * window at `ram`, which is `ram_size` bytes. Copies what the GP's kernel
 * would copy into the GP's memory. Called before the doorbell is cleared. */
void apu_gpfx_command(const uint8_t *ram, uint32_t ram_size, uint32_t doorbell);

/* Effect e's state as the GP holds it, a DSP word (24 bits) to a uint32_t,
 * and its length in *words; NULL, with 0, if the image has no effect e. */
const uint32_t *apu_gpfx_effect(unsigned e, unsigned *words);

/* Whether the image loaded is laid out as the one studied: JSRF's. */
int apu_gpfx_studied(void);
