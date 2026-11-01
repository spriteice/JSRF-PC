/* The GP's effects settings, followed, and our own effects on the mixbins
 * and the 3D voices (apu_gpfx.c, apu_hrtf.h). */
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
 * is `bins` of them, each of `samples` floats one after another. Once an
 * image laid out as JSRF's has loaded, our own reverb runs on the reverb
 * send (bin 10) in the room the game set, and adds into the 3D bins (6 to
 * 9) (apu_reverb.h). Then the 3D bins go through a limiter of their own,
 * and come out one frame late. Returns 1 if it ran; 0, with the mixbins as
 * they were, if the switch is off or the frame isn't the mixbins' size. */
int apu_gpfx_frame(float *mixbins, unsigned bins, unsigned samples);

/* Whether Developer > Experimental > Xbox audio effects is on. With it, the
 * voice processor places its 3D voices from their HRTF settings
 * (apu_hrtf.h). */
int apu_gpfx_on(void);

/* For the log: whether apu_gpfx_voice wants the placing timed, which is
 * with the switch on and Developer > Logging > RECOMP_APU_STATS. */
int apu_gpfx_timed(void);

/* What placing a 3D voice's frame came to, as its HRTF setting has it: how
 * far its far ear is turned down, in dB, and how late it is, in samples,
 * which ear that is (side: -1 the left, 1 the right, 0 neither), and
 * whether the setting had the delay on the louder ear, so that it went to
 * the other. Then whether the setting changed since the voice's last frame,
 * and by how much: the most either ear's level moved, in dB, and the delay,
 * in samples. first: the voice's first frame since it started. */
typedef struct {
    float far_db, late;
    int side, moved;
    int changed;
    float change_db, change_late;
    int first;
} ApuGpfxPlaced;

/* For the log: 3D voice `voice` played a frame with the switch on, placed
 * as `placed` says, which took `ns` nanoseconds, or with no HRTF setting
 * if `placed` is NULL. `bins` are the voice's, the first four its HRTF
 * submixes. */
void apu_gpfx_voice(unsigned voice, const int *bins, const ApuGpfxPlaced *placed,
                    int64_t ns);
