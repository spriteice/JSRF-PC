/* The layouts: which of the host's buttons and keys press each of the Xbox
 * pad's controls. One for the host's pads, one for the window's keyboard;
 * xinput_device.c reads what is held and asks them for the Xbox pad's state.
 * Each control has up to INPUT_BINDS inputs, and is pressed while any of
 * them is. A pad's sticks are the game's as they are: only the keyboard
 * pushes a stick, through the sticks' directions. No SDL in it, for
 * tests/input_layout. */
#ifndef XBOX_INPUT_LAYOUT_H
#define XBOX_INPUT_LAYOUT_H

#include <stdint.h>
#include "xinput_xbox.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Xbox pad's controls a layout presses: bAnalogButtons' eight in its
 * order, then wButtons' eight in its bits' order, then the sticks'
 * directions. */
enum {
    INPUT_CTL_A, INPUT_CTL_B, INPUT_CTL_X, INPUT_CTL_Y,
    INPUT_CTL_BLACK, INPUT_CTL_WHITE, INPUT_CTL_LTRIGGER, INPUT_CTL_RTRIGGER,
    INPUT_CTL_DPAD_UP, INPUT_CTL_DPAD_DOWN, INPUT_CTL_DPAD_LEFT, INPUT_CTL_DPAD_RIGHT,
    INPUT_CTL_START, INPUT_CTL_BACK, INPUT_CTL_LTHUMB, INPUT_CTL_RTHUMB,
    INPUT_CTL_LSTICK_UP, INPUT_CTL_LSTICK_DOWN, INPUT_CTL_LSTICK_LEFT, INPUT_CTL_LSTICK_RIGHT,
    INPUT_CTL_RSTICK_UP, INPUT_CTL_RSTICK_DOWN, INPUT_CTL_RSTICK_LEFT, INPUT_CTL_RSTICK_RIGHT,
    INPUT_CTLS
};
#define INPUT_CTL_BUTTONS INPUT_CTL_DPAD_UP     /* from here, wButtons' bit (c - this) */
#define INPUT_CTL_STICKS  INPUT_CTL_LSTICK_UP   /* from here, the sticks' directions */

/* A host pad's inputs: its buttons in SDL's order (SDL_GameControllerButton,
 * which xinput_device.c checks against it), then its two triggers. */
enum {
    INPUT_PAD_A, INPUT_PAD_B, INPUT_PAD_X, INPUT_PAD_Y,
    INPUT_PAD_BACK, INPUT_PAD_GUIDE, INPUT_PAD_START,
    INPUT_PAD_LSTICK, INPUT_PAD_RSTICK, INPUT_PAD_LSHOULDER, INPUT_PAD_RSHOULDER,
    INPUT_PAD_DPAD_UP, INPUT_PAD_DPAD_DOWN, INPUT_PAD_DPAD_LEFT, INPUT_PAD_DPAD_RIGHT,
    INPUT_PAD_MISC1, INPUT_PAD_PADDLE1, INPUT_PAD_PADDLE2, INPUT_PAD_PADDLE3, INPUT_PAD_PADDLE4,
    INPUT_PAD_TOUCHPAD,
    INPUT_PAD_BUTTONS,
    INPUT_PAD_LTRIGGER = INPUT_PAD_BUTTONS, INPUT_PAD_RTRIGGER,
    INPUT_PAD_INPUTS
};

/* The keyboard's and the mouse's inputs: a key by its place, as SDL numbers
 * them (SDL_Scancode, which xinput_device.c checks these against), then the
 * mouse's buttons from SDL's first, its left (SDL_BUTTON_LEFT): left,
 * middle, right, back and forward. The keys the defaults use are named. */
enum {
    INPUT_KEY_A = 4, INPUT_KEY_C = 6, INPUT_KEY_D = 7, INPUT_KEY_E = 8, INPUT_KEY_F = 9,
    INPUT_KEY_H = 11, INPUT_KEY_J = 13, INPUT_KEY_K = 14, INPUT_KEY_L = 15,
    INPUT_KEY_Q = 20, INPUT_KEY_R = 21, INPUT_KEY_S = 22, INPUT_KEY_V = 25, INPUT_KEY_W = 26,
    INPUT_KEY_RETURN = 40, INPUT_KEY_ESCAPE = 41, INPUT_KEY_SPACE = 44,
    INPUT_KEY_RIGHT = 79, INPUT_KEY_LEFT = 80, INPUT_KEY_DOWN = 81, INPUT_KEY_UP = 82,
    INPUT_KEY_KP_ENTER = 88,
    INPUT_KEY_AC_BACK = 270                 /* a keyboard's Back key */
};
#define INPUT_KEY_SCANCODES     512         /* SDL_NUM_SCANCODES */
#define INPUT_KEY_MOUSE         INPUT_KEY_SCANCODES
#define INPUT_KEY_MOUSE_BUTTONS 5
#define INPUT_KEYS              (INPUT_KEY_MOUSE + INPUT_KEY_MOUSE_BUTTONS)

/* The keys and mouse buttons held: a bit for each of the keyboard's
 * inputs, as the window holds them (nv_window_keys). */
#define INPUT_HELD_WORDS ((INPUT_KEYS + 31) / 32)

/* No input, in a control's place for one. */
#define INPUT_NONE (-1)
/* The inputs a control can have. */
#define INPUT_BINDS 2
/* A wButtons control is pressed from half way on an input that has more
 * than on and off (a trigger), as the latency log counts a press. */
#define INPUT_LAYOUT_DOWN 128

typedef struct InputLayout {
    short bind[INPUT_CTLS][INPUT_BINDS];   /* each control's inputs, INPUT_NONE where it has fewer */
} InputLayout;

/* The layouts as they always were: the host pad's buttons at their Xbox
 * places, with the bumpers as Black and White, and its Back none of the
 * game's (xinput_device.c gives it to the game when there is no menu); and
 * the keys as docs/playing.md lists them, with a second key on A (J), on
 * Start (keypad Enter) and on Back (a keyboard's Back key). */
void input_layout_pad_default(InputLayout *l);
void input_layout_keys_default(InputLayout *l);

/* A host pad's state through the layout: value is how far each input is
 * pressed, 0 to 255 (a button 0 or 255). Each analog control gets the most
 * of its inputs, or of what analog[] has already; each wButtons control is
 * pressed into *buttons from INPUT_LAYOUT_DOWN. */
void input_layout_pad(const InputLayout *l, const unsigned char value[INPUT_PAD_INPUTS],
                      WORD *buttons, unsigned char analog[8]);

/* The keys held (held[], a bit each) through the layout, over what the pad
 * gave: a control held presses its button into *buttons, or its analog
 * button fully; a stick a direction is held on is at full tilt that way, a
 * diagonal scaled onto the circle, in place of the pad's (axes: the left
 * stick's x and y, then the right's); a stick with none keeps the pad's.
 * Returns 1 when a key the layout uses is held. */
int input_layout_keys(const InputLayout *l, const uint32_t held[INPUT_HELD_WORDS],
                      WORD *buttons, unsigned char analog[8], short axes[4]);

/* The left stick's keys alone (xbox_InputKeyboardMove): *x right minus
 * left, *y up minus down, each -1, 0 or 1. Returns 1 while one is held. */
int input_layout_keys_move(const InputLayout *l, const uint32_t held[INPUT_HELD_WORDS],
                           int *x, int *y);

/* Control c pressed by input alone, as a layout's row binds it: every other
 * control that had input has c's first input in its place instead (none if
 * c had none, or if that control has it already), so the two swap and no
 * input presses two controls that it didn't before. */
void input_layout_bind(InputLayout *l, int c, int input);

/* The inputs any control has, a bit each (the latency log counts a press
 * of those alone). */
void input_layout_used(const InputLayout *l, uint32_t used[INPUT_HELD_WORDS]);

#ifdef __cplusplus
}
#endif

#endif
