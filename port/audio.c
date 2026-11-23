/* The Audio menu (audio.h): mute= and audioeffects= in the settings file,
 * or Audio > Mute and Audio > Audio effects.
 *
 * Both read once, at start (audio_start). Mute is passed to the sound
 * output (xa2_set_mute), which plays silence in place of the game's sound
 * while the game and its sound run on as ever, and its row changes it at
 * once. The effects are passed to apu_gpfx_choose before the APU starts;
 * they are set up as the game starts, so their row keeps the change in the
 * settings file and, after asking, restarts the game, as Graphics > Shader
 * cache does. */
#include <stdio.h>
#include <stdlib.h>
#include "audio.h"
#include "apu_gpfx.h"
#include "apu_xaudio2.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

#define AUDIO_EFFECTS_KEY "audioeffects"
#define AUDIO_EFFECTS_ENV "RECOMP_APU_GPFX"
/* On with no audioeffects= in the settings. */
#define AUDIO_EFFECTS_DEFAULT 1
#define AUDIO_MUTE_KEY "mute"
/* Off with no mute= in the settings. */
#define AUDIO_MUTE_DEFAULT 0

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file the engine already keeps, and a
 * number kept there. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
#else
/* Without the SDL window (a headless build) there is no settings file: no
 * keys, so the effects are on and the sound isn't muted. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
#endif

#if defined(XBOXRECOMP_MENU)
/* nv2a_gl.c: the game started again (File > Restart's), or nothing where it
 * can't be. */
extern void nv_window_restart(void);
#endif

static volatile int s_effects, s_mute;

#if defined(XBOXRECOMP_MENU)
static int  menu_mute(void) { return s_mute; }
/* At once, and kept. */
static void menu_mute_flip(void)
{
    s_mute = !s_mute;
    xa2_set_mute(s_mute);
    nv_window_pref_set_num(AUDIO_MUTE_KEY, s_mute);
    fprintf(stderr, "[AUDIO] %s (the menu)\n", s_mute ? "muted" : "unmuted");
}

static int  menu_effects(void) { return s_effects; }
/* Kept for the next start, which reads it (audio_start). */
static void menu_effects_keep(int on)
{
    s_effects = on;
    nv_window_pref_set_num(AUDIO_EFFECTS_KEY, s_effects);
    fprintf(stderr, "[AUDIO] effects %s from the next start (the menu)\n",
            s_effects ? "on" : "off");
}
static void menu_effects_flip(void)
{
    menu_effects_keep(!s_effects);
    nv_window_restart();        /* returns only where the game can't restart */
}
#endif

void audio_start(void)
{
    const char *env = getenv(AUDIO_EFFECTS_ENV);
    double v;
    /* As the settings file has it, or the default. */
    s_effects = nv_window_pref_num(AUDIO_EFFECTS_KEY, &v) > 0 ? v != 0 : AUDIO_EFFECTS_DEFAULT;
    apu_gpfx_choose(s_effects);
    if (env)
        fprintf(stderr, "[AUDIO] effects %s (" AUDIO_EFFECTS_ENV "=%s in the environment, over "
                AUDIO_EFFECTS_KEY "=%d)\n", atoi(env) ? "on" : "off", env, s_effects);
    else
        fprintf(stderr, "[AUDIO] effects %s (settings)\n", s_effects ? "on" : "off");
    s_mute = nv_window_pref_num(AUDIO_MUTE_KEY, &v) > 0 ? v != 0 : AUDIO_MUTE_DEFAULT;
    xa2_set_mute(s_mute);
    if (s_mute) fprintf(stderr, "[AUDIO] muted (settings)\n");
#if defined(XBOXRECOMP_MENU)
    {
        const XBOX_MENU_ITEM rows[] = {
            /* First in Audio. File > Presets leave it: the player's own,
             * with its tick the way back. */
            { .menu = XBOX_MENU_AUDIO, .kind = XBOX_ITEM_TOGGLE, .label = "Mute",
              .state = menu_mute, .run = menu_mute_flip, .presets = XBOX_PRESETS_LEFT,
              .hint = "Silences the game's sound." },
            { .menu = XBOX_MENU_AUDIO, .kind = XBOX_ITEM_TOGGLE, .label = "Audio effects (Restart)",
              .state = menu_effects, .run = menu_effects_flip,
              .confirm = "Apply selection and restart, losing unsaved progress?",
              /* File > Presets > Xbox (Restart) turns them on, as the
               * Xbox's own sound has them, kept for the next start (keep)
               * and the game restarted once (menu_sdl.cpp, preset_pick);
               * the other presets leave it, the player's own, as Mute is
               * (spriteice, 23 November 2026). */
              .presets = XBOX_PRESETS_SET, .preset = { 1, 0, 0, 0 }, .leave = XBOX_PRESETS_BUT_XBOX,
              .keep = menu_effects_keep,
              .hint = "Replicates the console's 3D sound, voices, and reverb." },
        };
        xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
    }
#endif
}

int audio_effects(void)
{
    return s_effects;
}
