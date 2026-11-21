/* The layouts (input_layout.h). */
#include "input_layout.h"

/* A stick at full tilt, and on a diagonal, scaled onto the circle
 * (32767 / sqrt 2). */
#define INPUT_STICK_FULL 32767
#define INPUT_STICK_DIAG 23170
#define INPUT_FULL       255        /* a key held: its analog button fully pressed */

static void clear(InputLayout *l)
{
    int c, b;
    for (c = 0; c < INPUT_CTLS; c++)
        for (b = 0; b < INPUT_BINDS; b++) l->bind[c][b] = INPUT_NONE;
}

/* The defaults name every control of theirs: a place left out would be 0,
 * which is an input (tests/input_layout checks each control's). */
void input_layout_pad_default(InputLayout *l)
{
    static const short k_pad[INPUT_CTL_STICKS] = {
        [INPUT_CTL_A] = INPUT_PAD_A, [INPUT_CTL_B] = INPUT_PAD_B,
        [INPUT_CTL_X] = INPUT_PAD_X, [INPUT_CTL_Y] = INPUT_PAD_Y,
        [INPUT_CTL_BLACK] = INPUT_PAD_LSHOULDER, [INPUT_CTL_WHITE] = INPUT_PAD_RSHOULDER,
        [INPUT_CTL_LTRIGGER] = INPUT_PAD_LTRIGGER, [INPUT_CTL_RTRIGGER] = INPUT_PAD_RTRIGGER,
        [INPUT_CTL_DPAD_UP] = INPUT_PAD_DPAD_UP, [INPUT_CTL_DPAD_DOWN] = INPUT_PAD_DPAD_DOWN,
        [INPUT_CTL_DPAD_LEFT] = INPUT_PAD_DPAD_LEFT, [INPUT_CTL_DPAD_RIGHT] = INPUT_PAD_DPAD_RIGHT,
        [INPUT_CTL_START] = INPUT_PAD_START, [INPUT_CTL_BACK] = INPUT_NONE,
        [INPUT_CTL_LTHUMB] = INPUT_PAD_LSTICK, [INPUT_CTL_RTHUMB] = INPUT_NONE,
    };
    int c;
    clear(l);
    for (c = 0; c < INPUT_CTL_STICKS; c++) l->bind[c][0] = k_pad[c];
    l->bind[INPUT_CTL_FREECAM][0] = INPUT_PAD_RSTICK;
}

void input_layout_keys_default(InputLayout *l)
{
    /* W A S D are the left stick and the d-pad both: the skater reads the
     * stick, the menus the d-pad. */
    static const short k_keys[INPUT_CTLS] = {
        [INPUT_CTL_A] = INPUT_KEY_SPACE, [INPUT_CTL_B] = INPUT_KEY_K,
        [INPUT_CTL_X] = INPUT_KEY_H, [INPUT_CTL_Y] = INPUT_KEY_L,
        [INPUT_CTL_BLACK] = INPUT_KEY_R, [INPUT_CTL_WHITE] = INPUT_KEY_F,
        [INPUT_CTL_LTRIGGER] = INPUT_KEY_Q, [INPUT_CTL_RTRIGGER] = INPUT_KEY_E,
        [INPUT_CTL_DPAD_UP] = INPUT_KEY_W, [INPUT_CTL_DPAD_DOWN] = INPUT_KEY_S,
        [INPUT_CTL_DPAD_LEFT] = INPUT_KEY_A, [INPUT_CTL_DPAD_RIGHT] = INPUT_KEY_D,
        [INPUT_CTL_START] = INPUT_KEY_RETURN, [INPUT_CTL_BACK] = INPUT_KEY_ESCAPE,
        [INPUT_CTL_LTHUMB] = INPUT_KEY_C, [INPUT_CTL_RTHUMB] = INPUT_KEY_V,
        [INPUT_CTL_LSTICK_UP] = INPUT_KEY_W, [INPUT_CTL_LSTICK_DOWN] = INPUT_KEY_S,
        [INPUT_CTL_LSTICK_LEFT] = INPUT_KEY_A, [INPUT_CTL_LSTICK_RIGHT] = INPUT_KEY_D,
        [INPUT_CTL_RSTICK_UP] = INPUT_KEY_UP, [INPUT_CTL_RSTICK_DOWN] = INPUT_KEY_DOWN,
        [INPUT_CTL_RSTICK_LEFT] = INPUT_KEY_LEFT, [INPUT_CTL_RSTICK_RIGHT] = INPUT_KEY_RIGHT,
        [INPUT_CTL_FREECAM] = INPUT_NONE,
    };
    int c;
    clear(l);
    for (c = 0; c < INPUT_CTLS; c++) l->bind[c][0] = k_keys[c];
    l->bind[INPUT_CTL_A][1] = INPUT_KEY_J;
    l->bind[INPUT_CTL_START][1] = INPUT_KEY_KP_ENTER;
    l->bind[INPUT_CTL_BACK][1] = INPUT_KEY_AC_BACK;
}

void input_layout_pad(const InputLayout *l, const unsigned char value[INPUT_PAD_INPUTS],
                      WORD *buttons, unsigned char analog[8])
{
    int c, b;
    for (c = 0; c < INPUT_CTL_STICKS; c++) {
        unsigned char v = 0;
        for (b = 0; b < INPUT_BINDS; b++) {
            int in = l->bind[c][b];
            if (in >= 0 && in < INPUT_PAD_INPUTS && value[in] > v) v = value[in];
        }
        if (c < INPUT_CTL_BUTTONS) {
            if (v > analog[c]) analog[c] = v;
        } else if (v >= INPUT_LAYOUT_DOWN)
            *buttons |= (WORD)(1u << (c - INPUT_CTL_BUTTONS));
    }
}

static int key_held(const uint32_t held[INPUT_HELD_WORDS], int in)
{
    return in >= 0 && in < INPUT_KEYS && ((held[in / 32] >> (in % 32)) & 1u);
}

static int ctl_held(const InputLayout *l, const uint32_t held[INPUT_HELD_WORDS], int c)
{
    int b;
    for (b = 0; b < INPUT_BINDS; b++)
        if (key_held(held, l->bind[c][b])) return 1;
    return 0;
}

/* A stick's keys: x right minus left, y up minus down; at full tilt, in
 * place of the pad's, when either is held. */
static void key_stick(int x, int y, short *ax, short *ay)
{
    short v;
    if (!x && !y) return;
    v = (x && y) ? INPUT_STICK_DIAG : INPUT_STICK_FULL;
    *ax = (short)(x * v);
    *ay = (short)(y * v);
}

int input_layout_keys(const InputLayout *l, const uint32_t held[INPUT_HELD_WORDS],
                      WORD *buttons, unsigned char analog[8], short axes[4])
{
    int on[INPUT_CTL_OWN], c, any = 0;
    for (c = 0; c < INPUT_CTL_OWN; c++) {
        on[c] = ctl_held(l, held, c);
        any |= on[c];
    }
    for (c = 0; c < INPUT_CTL_BUTTONS; c++)
        if (on[c]) analog[c] = INPUT_FULL;
    for (c = INPUT_CTL_BUTTONS; c < INPUT_CTL_STICKS; c++)
        if (on[c]) *buttons |= (WORD)(1u << (c - INPUT_CTL_BUTTONS));
    key_stick(on[INPUT_CTL_LSTICK_RIGHT] - on[INPUT_CTL_LSTICK_LEFT],
              on[INPUT_CTL_LSTICK_UP] - on[INPUT_CTL_LSTICK_DOWN], &axes[0], &axes[1]);
    key_stick(on[INPUT_CTL_RSTICK_RIGHT] - on[INPUT_CTL_RSTICK_LEFT],
              on[INPUT_CTL_RSTICK_UP] - on[INPUT_CTL_RSTICK_DOWN], &axes[2], &axes[3]);
    return any;
}

int input_layout_keys_move(const InputLayout *l, const uint32_t held[INPUT_HELD_WORDS],
                           int *x, int *y)
{
    int up = ctl_held(l, held, INPUT_CTL_LSTICK_UP), down = ctl_held(l, held, INPUT_CTL_LSTICK_DOWN),
        left = ctl_held(l, held, INPUT_CTL_LSTICK_LEFT), right = ctl_held(l, held, INPUT_CTL_LSTICK_RIGHT);
    *x = right - left;
    *y = up - down;
    return up || down || left || right;
}

static int has(const InputLayout *l, int c, int input)
{
    int b;
    for (b = 0; b < INPUT_BINDS; b++)
        if (l->bind[c][b] == input) return 1;
    return 0;
}

void input_layout_bind(InputLayout *l, int c, int input)
{
    int was = INPUT_NONE, o, b;
    if (c < 0 || c >= INPUT_CTLS) return;
    for (b = INPUT_BINDS - 1; b >= 0; b--)
        if (l->bind[c][b] != INPUT_NONE) was = l->bind[c][b];
    for (o = 0; o < INPUT_CTLS; o++)
        for (b = 0; b < INPUT_BINDS; b++)
            if (o != c && input != INPUT_NONE && input != was && l->bind[o][b] == input)
                l->bind[o][b] = (short)(has(l, o, was) ? INPUT_NONE : was);
    for (b = 0; b < INPUT_BINDS; b++) l->bind[c][b] = INPUT_NONE;
    l->bind[c][0] = (short)input;
}

void input_layout_bind_key(InputLayout *l, int c, int place, int input)
{
    int was, o, b;
    if (c < 0 || c >= INPUT_CTLS || place < 0 || place >= INPUT_BINDS) return;
    was = l->bind[c][place];
    if (input == was) return;
    if (input != INPUT_NONE)
        for (o = 0; o < INPUT_CTLS; o++)
            for (b = 0; b < INPUT_BINDS; b++)
                if ((o != c || b != place) && l->bind[o][b] == input)
                    l->bind[o][b] = (short)(o != c && has(l, o, was) ? INPUT_NONE : was);
    l->bind[c][place] = (short)input;
}

void input_layout_used(const InputLayout *l, uint32_t used[INPUT_HELD_WORDS])
{
    int c, b;
    for (c = 0; c < INPUT_HELD_WORDS; c++) used[c] = 0;
    for (c = 0; c < INPUT_CTLS; c++)
        for (b = 0; b < INPUT_BINDS; b++) {
            int in = l->bind[c][b];
            if (in >= 0 && in < INPUT_KEYS) used[in / 32] |= 1u << (in % 32);
        }
}
