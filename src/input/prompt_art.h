/* The button prompts' art: the pictures of a host's inputs, a pad's
 * buttons in each style, and the keys and the mouse's buttons, from
 * Juliocacko's Free Input Prompts and Xelu's Free Controller & Key Prompts
 * (src/input/prompts/README.md). Only the pictures the prompts use
 * are in src/input/prompts/, as PNGs, built into the engine
 * (src/input/CMakeLists.txt makes prompt_art_data.c from them,
 * prompts/embed.cmake) and decoded here with miniz when first asked for.
 * Nothing comes from the game's files. No SDL or OpenGL in it, for
 * tests/prompts. */
#ifndef XBOX_PROMPT_ART_H
#define XBOX_PROMPT_ART_H

#include <stdint.h>
#include "input_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The art an icon is drawn from: the game's own, the pictures' set for
 * each kind of pad, or the keys (and the mouse's buttons). */
enum {
    PROMPT_ART_GAME, PROMPT_ART_XBOX, PROMPT_ART_PLAYSTATION, PROMPT_ART_NINTENDO,
    PROMPT_ART_KEYS,
    PROMPT_ARTS
};

/* What an icon shows: in art, input, a host pad's input (INPUT_PAD_...,
 * input_layout.h) or a key or mouse button (INPUT_KEY_...); in the game's
 * own art, the Xbox pad's control (INPUT_CTL_...) whose icon it is.
 * INPUT_NONE: the game's icon as it is. */
typedef struct { int art, input; } PromptIcon;

/* The blank key a key without a picture of its own is drawn on, with its
 * name over it (nv2a_prompts.h), and a pad's button without one, with the
 * set's name for it (prompt_art_label). */
#define PROMPT_ART_BLANK "key_blank"

/* The picture of input in art (PROMPT_ART_XBOX to PROMPT_ART_KEYS):
 * a host pad's input (INPUT_PAD_...) in a pad's set, or a key or a mouse
 * button (INPUT_KEY_...) in the keys'. Its name, the
 * file's without .png, or NULL where there is none: the pad's Guide,
 * which no layout puts on a control, the paddles, the PlayStation's Mute,
 * and a key without a picture, the last three drawn on PROMPT_ART_BLANK.
 * Every pad set takes the same touchpad. */
const char *prompt_art_name(int art, int input);

/* A pad's whole D-pad or left stick as one picture, for a line of the
 * game's that names it and not one of its ways (port/prompt_text.h, the
 * graffiti editor's help lines): the D-pad as it is, with its left and
 * right lit, and with its up and down; the left stick with every way's
 * arrow, with left and right's, and with up and down's. */
enum {
    PROMPT_GROUP_DPAD, PROMPT_GROUP_DPAD_ACROSS, PROMPT_GROUP_DPAD_UPDOWN,
    PROMPT_GROUP_LSTICK, PROMPT_GROUP_LSTICK_ACROSS, PROMPT_GROUP_LSTICK_UPDOWN,
    PROMPT_GROUPS
};

/* The picture of group (PROMPT_GROUP_...) in a pad's set (PROMPT_ART_XBOX
 * to PROMPT_ART_NINTENDO): its name, or NULL for any other art (the keys
 * have none: a line shows each way's key) or group. */
const char *prompt_art_group(int art, int group);

/* A pad set's name for an input it has no picture of, which a layout can
 * put on a control, to write on PROMPT_ART_BLANK: the paddles, P1 to P4 as
 * the Xbox Elite labels them, and the PlayStation's Mute (SDL's Misc).
 * NULL for any other, and for every key (the window names those). */
const char *prompt_art_label(int art, int input);

/* The host pad's input whose picture stands for the game's own icon of
 * the Xbox pad's control c: the one c is on by default, and the R3 for the
 * Right stick click and the pad's Back for the game's Back (whose defaults
 * have none). Where the game has no icon of c to copy, the renderer draws
 * the Xbox set's picture of it. INPUT_NONE for the sticks' directions and
 * the port's own. */
int prompt_art_pad_input(int c);

/* A picture, decoded: w x h, RGBA8, rows from the top, alpha straight. */
typedef struct { int w, h; const uint8_t *rgba; } PromptImage;

/* The picture called name (prompt_art_name, or PROMPT_ART_BLANK),
 * decoded the first time it is asked for and kept from then on. 1 with it
 * in *out; 0 where there is none, or it doesn't decode (and says so once).
 * Any thread. */
int prompt_art_image(const char *name, PromptImage *out);

/* The pictures built in, by name in strcmp's order (prompt_art_data.c,
 * made by prompts/embed.cmake): for tests/prompts, and for the lookup. */
typedef struct { const char *name; const unsigned char *png; unsigned size; } PromptArtFile;
extern const PromptArtFile prompt_art_files[];
extern const int prompt_art_nfiles;

/* A PNG as prompt_art_image decodes it: 8 bits a channel, not
 * interlaced, and RGBA (as the pictures are) or a palette with its alpha. Into a new buffer of RGBA (free() it), with its size; NULL for
 * any other. */
uint8_t *prompt_art_png(const unsigned char *png, unsigned size, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
