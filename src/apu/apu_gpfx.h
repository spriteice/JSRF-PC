/* The GP's effects settings, followed, and our own effects on the mixbins
 * (apu_gpfx.c). */
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

/* Where the CPU sees the GP's X memory, four bytes to each of its words:
 * the APU's registers at 0xFE800000, and the GP's X memory 0x30000 in.
 * DirectSound reads the effects' state back from here. */
#define APU_GPFX_X_WINDOW 0xFE830000u
#define APU_GPFX_X_BYTES  0x4000u

/* Keep the copy where the CPU reads the GP's memory too, so that what
 * DirectSound reads back is what the GP would hold: `x_window` is the host
 * memory behind APU_GPFX_X_WINDOW, APU_GPFX_X_BYTES of it, or NULL to keep
 * the copy to itself. */
void apu_gpfx_window(uint8_t *x_window);

/* Developer > Experimental > Xbox audio effects (RECOMP_APU_GPFX): our own
 * effects on one frame of the mixbins, before the fold to stereo. `mixbins`
 * is `bins` of them, each of `samples` floats one after another. So far the
 * 3D bins (6 to 9) go through a limiter of their own, and come out one frame
 * late. Returns 1 if it ran; 0, with the mixbins as they were, if the switch
 * is off or the frame isn't the mixbins' size. */
int apu_gpfx_frame(float *mixbins, unsigned bins, unsigned samples);
