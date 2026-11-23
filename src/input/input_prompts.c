/* Button prompts (input_prompts.h). */
#include <stdio.h>
#include "input_prompts.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

/* The settings file: the SDL window's (nv2a_gl.c), so weak, as a run
 * without it has none. */
extern int  nv_window_pref_num(const char *key, double *v) __attribute__((weak));
extern void nv_window_pref_set_num(const char *key, double v) __attribute__((weak));

#define PROMPT_SETTING "button_prompts"

static const char *const k_style_names[PROMPT_STYLES] = {
    "Auto", "Default", "Xbox", "PlayStation", "Nintendo", "Keyboard"
};

int input_prompts_kind(int sdl_type)
{
    if (sdl_type == PROMPT_SDL_PS3 || sdl_type == PROMPT_SDL_PS4 || sdl_type == PROMPT_SDL_PS5)
        return PROMPT_PAD_PLAYSTATION;
    if (sdl_type == PROMPT_SDL_SWITCH_PRO || sdl_type == PROMPT_SDL_JOYCON_LEFT
     || sdl_type == PROMPT_SDL_JOYCON_RIGHT || sdl_type == PROMPT_SDL_JOYCON_PAIR)
        return PROMPT_PAD_NINTENDO;
    return PROMPT_PAD_XBOX;
}

int input_prompts_art(int style, int pad_kind, int keys_last)
{
    static const int k_kind_art[PROMPT_PADS] = {
        [PROMPT_PAD_XBOX] = PROMPT_ART_XBOX, [PROMPT_PAD_PLAYSTATION] = PROMPT_ART_PLAYSTATION,
        [PROMPT_PAD_NINTENDO] = PROMPT_ART_NINTENDO,
    };
    switch (style) {
    case PROMPT_STYLE_GAME:        return PROMPT_ART_GAME;
    case PROMPT_STYLE_XBOX:        return PROMPT_ART_XBOX;
    case PROMPT_STYLE_PLAYSTATION: return PROMPT_ART_PLAYSTATION;
    case PROMPT_STYLE_NINTENDO:    return PROMPT_ART_NINTENDO;
    case PROMPT_STYLE_KEYBOARD:    return PROMPT_ART_KEYS;
    default:
        if (keys_last) return PROMPT_ART_KEYS;
        return pad_kind >= 0 && pad_kind < PROMPT_PADS ? k_kind_art[pad_kind] : PROMPT_ART_XBOX;
    }
}

int input_prompts_strip_style(int style)
{
    return style == PROMPT_STYLE_GAME ? PROMPT_STYLE_AUTO : style;
}

/* A layout's first input on c, or INPUT_NONE. */
static int first(const InputLayout *l, int c)
{
    int b;
    if (!l) return INPUT_NONE;
    for (b = 0; b < INPUT_BINDS; b++)
        if (l->bind[c][b] != INPUT_NONE) return l->bind[c][b];
    return INPUT_NONE;
}

PromptIcon input_prompts_icon(int art, const InputLayout *pad, const InputLayout *keys, int c)
{
    PromptIcon icon = { art, INPUT_NONE };
    if (c < 0 || c >= INPUT_CTLS) return icon;
    if (art == PROMPT_ART_GAME) icon.input = c < INPUT_CTL_OWN ? c : INPUT_NONE;
    else icon.input = first(art == PROMPT_ART_KEYS ? keys : pad, c);
    return icon;
}

/* ---- the prompts in use ------------------------------------------------- */

static volatile int g_style = PROMPT_STYLE_START;
static volatile int g_keys_last;
static volatile int g_pad_kind = PROMPT_PAD_XBOX;
static volatile int g_art = -1;                 /* input_prompts_art's, once known */
static unsigned g_gen;

void input_prompts_changed(void)
{
    __atomic_add_fetch(&g_gen, 1u, __ATOMIC_RELEASE);
}

unsigned input_prompts_generation(void)
{
    return __atomic_load_n(&g_gen, __ATOMIC_ACQUIRE);
}

/* The art again, from what is known now; a change moves the count on. */
static void retake(void)
{
    int art = input_prompts_art(g_style, g_pad_kind, g_keys_last);
    if (art != g_art) {
        g_art = art;
        input_prompts_changed();
    }
}

int input_prompts_style(void) { return g_style; }

void input_prompts_set_style(int style)
{
    if (style < 0 || style >= PROMPT_STYLES || style == g_style) return;
    g_style = style;
    fprintf(stderr, "[INPUT] button prompts: %s\n", k_style_names[style]);
    retake();
}

void input_prompts_used(int keys, int pad_kind)
{
    g_keys_last = keys != 0;
    if (!keys) g_pad_kind = pad_kind;
    retake();
}

int input_prompts_art_now(void)
{
    if (g_art < 0) retake();
    return g_art;
}

int input_prompts_pad_now(void) { return g_pad_kind; }

#if defined(XBOXRECOMP_MENU)
static int  menu_style(void)       { return g_style; }
static void menu_style_pick(int i)
{
    input_prompts_set_style(i);
    if (nv_window_pref_set_num) nv_window_pref_set_num(PROMPT_SETTING, g_style);
}
#endif

void input_prompts_start(void)
{
    double v;
    if (nv_window_pref_num && nv_window_pref_num(PROMPT_SETTING, &v)) {
        int s = (int)v;
        if (s >= 0 && s < PROMPT_STYLES && (double)s == v) input_prompts_set_style(s);
        else fprintf(stderr, "[INPUT] settings: %s=%g is no style; %s kept\n", PROMPT_SETTING, v,
                     k_style_names[PROMPT_STYLE_START]);
    }
    retake();
#if defined(XBOXRECOMP_MENU)
    {
        /* File > Presets: Default in Xbox, the game's own, which a new
         * install starts with too (PROMPT_STYLE_START), and left by the
         * others, as all of Input is (docs/menu-format.md, File > Presets). */
        static const XBOX_MENU_ITEM rows[] = {
            { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_GROUP, .label = XBOX_GROUP_PROMPTS,
              .hint = "Sets the button icons the game shows." },
            { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_CHOICE, .label = "Style",
              .group = XBOX_GROUP_PROMPTS, .state = menu_style, .pick = menu_style_pick,
              .choices = k_style_names, .nchoices = PROMPT_STYLES, .presets = XBOX_PRESETS_SET,
              .preset = { PROMPT_STYLE_GAME, 0, 0, 0 }, .leave = XBOX_PRESETS_BUT_XBOX,
              .hint = "Auto follows the controller or keyboard in use. Default is the game's." },
        };
        xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
    }
#endif
}
