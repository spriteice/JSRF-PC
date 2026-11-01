/* The Audio menu: Audio > Audio effects, kept as audioeffects= in the
 * settings file.
 *
 * On (audioeffects=1, the default), our own versions of the console's sound
 * effects run: the 3D sounds placed to the left or right (src/apu/apu_hrtf.h),
 * the characters' voice lines kept on them (port/voice_follow.c), the reverb
 * the game sets for a place (src/apu/apu_reverb.h), and a limiter for the 3D
 * sounds (src/apu/apu_gpfx.c). Off (audioeffects=0), the sound is as before
 * them. RECOMP_APU_GPFX=1 or 0 in the environment wins over the setting.
 * docs/technical/audio-effects.md says how they work. */
#ifndef JSRF_AUDIO_H
#define JSRF_AUDIO_H

/* At start, before the APU does: reads the setting, passes it to the
 * effects, and adds its row to the in-game menu's Audio page. */
void audio_start(void);

/* Whether the settings file has the effects on: what the row shows, and
 * what the next start uses unless the environment says otherwise. */
int audio_effects(void);

#endif
