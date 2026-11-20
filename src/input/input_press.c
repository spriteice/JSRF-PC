/* What the input latency log counts as a press (input_press.h). */
#include "input_press.h"

static const char *const k_pad_names[INPUT_PRESS_PAD] = {
    "d-pad up", "d-pad down", "d-pad left", "d-pad right", "Start", "Back",
    "left stick click", "right stick click",
    "A", "B", "X", "Y", "Black", "White", "left trigger", "right trigger",
    "left stick", "right stick",
};

/* Half way from the centre or more, round the circle. */
static int stick_held(short x, short y)
{
    long long d2 = (long long)x * x + (long long)y * y;
    return d2 >= (long long)INPUT_PRESS_STICK_HALF * INPUT_PRESS_STICK_HALF;
}

uint32_t input_press_pad(WORD buttons, const unsigned char analog[8], const short axes[4])
{
    uint32_t held = (uint32_t)(buttons & 0xFFu);   /* wButtons' eight, d-pad to right stick click */
    int i;
    for (i = 0; i < 8; i++)
        if (analog[i] >= INPUT_PRESS_ANALOG_HALF) held |= 1u << (8 + i);
    if (stick_held(axes[0], axes[1])) held |= 1u << INPUT_PRESS_LSTICK;
    if (stick_held(axes[2], axes[3])) held |= 1u << INPUT_PRESS_RSTICK;
    return held;
}

const char *input_press_pad_name(int control)
{
    return control >= 0 && control < INPUT_PRESS_PAD ? k_pad_names[control] : "?";
}
