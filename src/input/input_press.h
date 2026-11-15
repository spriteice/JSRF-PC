/* What the input latency log counts as a press (RECOMP_INPUT_LATENCY; the
 * frames are src/nv2a_gl/nv2a_latency.h's): at each of the game's reads of
 * the first pad, a control held that wasn't at the read before. Held: a
 * button down; an analog button or a trigger at half way or more (128 of
 * 255); a stick half way or more from its centre (16384 of 32767, round the
 * circle). The pad and the keyboard apart, so the log can tell them apart:
 * the pad's controls as the game sees them, the keys as the window holds
 * them (nv_window_keys). No SDL in it, for tests/input_latency. */
#ifndef XBOX_INPUT_PRESS_H
#define XBOX_INPUT_PRESS_H

#include <stdint.h>
#include "xinput_xbox.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The pad's controls, a bit each: wButtons' eight, then the eight analog
 * buttons in bAnalogButtons' order, then the left stick and the right. */
#define INPUT_PRESS_PAD     18
#define INPUT_PRESS_LSTICK  16
#define INPUT_PRESS_RSTICK  17

/* The keyboard's, a bit each: nv_window_keys' twelve bits, then its eight
 * analog keys. */
#define INPUT_PRESS_KEYS    20
#define INPUT_PRESS_KEY_BITS 12

/* Half way: an analog button or trigger, and a stick's distance from its
 * centre. */
#define INPUT_PRESS_ANALOG_HALF 128
#define INPUT_PRESS_STICK_HALF  16384

/* The pad's controls held: buttons as wButtons, analog as bAnalogButtons,
 * axes as the left stick's x and y, then the right's. */
uint32_t input_press_pad(WORD buttons, const unsigned char analog[8], const short axes[4]);

/* The keys held: keys and analog as nv_window_keys gives them. */
uint32_t input_press_keys(unsigned short keys, const unsigned char analog[8]);

/* A pad's control's name, for the log ("A", "d-pad up", "left stick"). */
const char *input_press_pad_name(int control);

#ifdef __cplusplus
}
#endif

#endif
