/* The Audio menu (audio.h): audioeffects= in the settings file, or Audio >
 * Audio effects.
 *
 * Read once, at start (audio_start), and passed to the effects
 * (apu_gpfx_choose) before the APU starts. The effects are set up as the
 * game starts, so the menu's row keeps the change in the settings file and,
 * after asking, restarts the game, as Graphics > Shader cache does. */
#include <stdio.h>
#include <stdlib.h>
#include "audio.h"
#include "apu_gpfx.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

#define AUDIO_EFFECTS_KEY "audioeffects"
#define AUDIO_EFFECTS_ENV "RECOMP_APU_GPFX"
/* On with no audioeffects= in the settings. */
#define AUDIO_EFFECTS_DEFAULT 1

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file the engine already keeps, and a
 * number kept there. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
#else
/* Without the SDL window (a headless build) there is no settings file: no
 * keys, so the effects are on. */
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

static volatile int s_effects;

#if defined(XBOXRECOMP_MENU)
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
#if defined(XBOXRECOMP_MENU)
    {
        const XBOX_MENU_ITEM row = {
            .menu = XBOX_MENU_AUDIO, .kind = XBOX_ITEM_TOGGLE, .label = "Audio effects (Restart)",
            .state = menu_effects, .run = menu_effects_flip,
            .confirm = "Apply selection and restart, losing unsaved progress?",
            /* File > Presets: on in each, Xbox mode's too, as they are the
             * console's own; kept for the restart it gives after every row. */
            .presets = XBOX_PRESETS_SET, .keep = menu_effects_keep,
            .preset = { 1, AUDIO_EFFECTS_DEFAULT, AUDIO_EFFECTS_DEFAULT },
            .hint = "Replicates the console's 3D sound, voices, and reverb." };
        xbox_MenuAdd(&row, 1);
    }
#endif
}

int audio_effects(void)
{
    return s_effects;
}
