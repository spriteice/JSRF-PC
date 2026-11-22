/* Button prompts (docs/playing.md, Controller): which art each of the
 * game's button icons is drawn with, from the style a player chose in
 * Input > Button prompts, the pad last pressed or the keyboard, and the
 * layouts (input_layout.h), so an icon shows the input that presses its
 * control now. The renderer draws them (src/nv2a_gl/nv2a_prompts.c), and
 * the in-game menu's strip takes the style alone (src/menu/menu.cpp). No
 * SDL in it, for tests/input_prompts: xinput_device.c reads what SDL
 * reports of a pad. */
#ifndef XBOX_INPUT_PROMPTS_H
#define XBOX_INPUT_PROMPTS_H

#include "input_layout.h"
#include "prompt_art.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Style's choices, in the row's order, as button_prompts= keeps them.
 * PROMPT_STYLE_GAME is the row called Default: the game as the Xbox shows
 * it, nothing of ours drawn in it (spriteice, 2026-11-21). */
enum {
    PROMPT_STYLE_AUTO, PROMPT_STYLE_GAME, PROMPT_STYLE_XBOX, PROMPT_STYLE_PLAYSTATION,
    PROMPT_STYLE_NINTENDO, PROMPT_STYLE_KEYBOARD,
    PROMPT_STYLES
};
/* Before the settings file names one, and what each of File > Presets sets:
 * Default, the game's own (spriteice, 23:57 UTC on 2026-11-21). */
#define PROMPT_STYLE_START PROMPT_STYLE_GAME

/* A pad's kind, from what SDL reports of it (input_prompts_kind). */
enum { PROMPT_PAD_XBOX, PROMPT_PAD_PLAYSTATION, PROMPT_PAD_NINTENDO, PROMPT_PADS };

/* SDL's kinds of controller, as SDL_GameControllerType numbers them
 * (xinput_device.c checks these against it). */
enum {
    PROMPT_SDL_PS3 = 3, PROMPT_SDL_PS4 = 4, PROMPT_SDL_SWITCH_PRO = 5, PROMPT_SDL_PS5 = 7,
    PROMPT_SDL_JOYCON_LEFT = 11, PROMPT_SDL_JOYCON_RIGHT = 12, PROMPT_SDL_JOYCON_PAIR = 13
};

/* A pad's kind: a PlayStation type, a Switch Pro pad or Joy-Cons, and Xbox
 * for everything else: the Steam Deck's own controls and the pads Steam
 * Input shows too, as there is no Steam Deck style (spriteice,
 * 2026-11-21: "they can use xbox"). */
int input_prompts_kind(int sdl_type);

/* The art for a style: Default's the game's own (PROMPT_ART_GAME), Xbox,
 * PlayStation and Nintendo their pack's set whatever was used last,
 * Keyboard's the keys'. Only Auto follows what is in hand: the keys' while
 * the keyboard or the mouse was the last thing used, otherwise the set of
 * the pad last pressed (an Xbox-style pad, or one SDL doesn't know, the
 * pack's Xbox set, never the game's own). */
int input_prompts_art(int style, int pad_kind, int keys_last);

/* The style the in-game menu's strip takes for style: Default's strip
 * follows what is in hand, as Auto's does (spriteice, 2026-11-21: "our f1
 * menu will automatically detect the glyphs to use but change nothing in
 * game"); every other style its own. */
int input_prompts_strip_style(int style);

/* What control c's icon shows, in art: input, a host pad's input
 * (INPUT_PAD_..., input_layout.h) or a key or mouse button (INPUT_KEY_...),
 * the layout's first on c. In the game's own art, input is c itself, the
 * game's own icon of c whatever the layouts say (Default changes nothing in
 * the game), and INPUT_NONE for a control of the port's own. input is
 * INPUT_NONE where c has no input: the renderer keeps the game's icon for
 * c. */
PromptIcon input_prompts_icon(int art, const InputLayout *pad, const InputLayout *keys, int c);

/* ---- the prompts in use: the window's thread writes, any thread reads --- */

/* Style, as Input > Button prompts has it (button_prompts=). */
int  input_prompts_style(void);
void input_prompts_set_style(int style);

/* Each press, from the window's thread: keys, 1 for a key or a mouse
 * button, 0 for a pad's button or trigger, with that pad's kind. */
void input_prompts_used(int keys, int pad_kind);

/* The art in use now, and the kind of the pad last pressed. */
int input_prompts_art_now(void);
int input_prompts_pad_now(void);

/* A count that moves on whenever what an icon shows may have: the style,
 * the art in use, or a layout (input_remap.c calls input_prompts_changed).
 * The renderer makes the icons again when it has moved. */
unsigned input_prompts_generation(void);
void     input_prompts_changed(void);

/* At start, on the main thread: button_prompts= from the settings file,
 * and Input > Button prompts (input_remap.c, after the layouts' pages). */
void input_prompts_start(void);

#ifdef __cplusplus
}
#endif

#endif
