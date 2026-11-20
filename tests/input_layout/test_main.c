/* tests/input_layout: the layouts (src/input/input_layout.c), linked whole,
 * against the fixed tables they replaced, kept here as what the defaults
 * must give (run.py says which cases). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_layout.h"

static int g_fail;

#define CHECK(cond, ...) do { if (!(cond)) { g_fail = 1; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* ---- what the input layer did before the layouts -------------------------
 * xinput_device.c's pad table and kb_merge, and xbox_InputKeyboardMove, as
 * they were on ed19ec3, with SDL's buttons as the layout numbers them. */

static void old_pad(const int down[INPUT_PAD_BUTTONS], int back_is_games, unsigned char lt,
                    unsigned char rt, WORD *out, unsigned char analog[8])
{
    if (down[INPUT_PAD_DPAD_UP])    *out |= XBOX_GAMEPAD_DPAD_UP;
    if (down[INPUT_PAD_DPAD_DOWN])  *out |= XBOX_GAMEPAD_DPAD_DOWN;
    if (down[INPUT_PAD_DPAD_LEFT])  *out |= XBOX_GAMEPAD_DPAD_LEFT;
    if (down[INPUT_PAD_DPAD_RIGHT]) *out |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (down[INPUT_PAD_START])      *out |= XBOX_GAMEPAD_START;
    if (down[INPUT_PAD_BACK] && back_is_games) *out |= XBOX_GAMEPAD_BACK;
    if (down[INPUT_PAD_LSTICK])     *out |= XBOX_GAMEPAD_LEFT_THUMB;
    if (down[INPUT_PAD_RSTICK])     *out |= XBOX_GAMEPAD_RIGHT_THUMB;
    analog[0] = down[INPUT_PAD_A] ? 255 : 0;
    analog[1] = down[INPUT_PAD_B] ? 255 : 0;
    analog[2] = down[INPUT_PAD_X] ? 255 : 0;
    analog[3] = down[INPUT_PAD_Y] ? 255 : 0;
    analog[4] = down[INPUT_PAD_LSHOULDER] ? 255 : 0;
    analog[5] = down[INPUT_PAD_RSHOULDER] ? 255 : 0;
    analog[6] = lt;
    analog[7] = rt;
}

static void old_keys(unsigned short kb, const unsigned char ka[8], WORD *out,
                     unsigned char analog[8], short axes[4])
{
    int i;
    if (kb & 0x0001u) *out |= XBOX_GAMEPAD_DPAD_UP;
    if (kb & 0x0002u) *out |= XBOX_GAMEPAD_DPAD_DOWN;
    if (kb & 0x0004u) *out |= XBOX_GAMEPAD_DPAD_LEFT;
    if (kb & 0x0008u) *out |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (kb & 0x0010u) *out |= XBOX_GAMEPAD_START;
    if (kb & 0x0020u) *out |= XBOX_GAMEPAD_BACK;
    if (kb & 0x0400u) *out |= XBOX_GAMEPAD_LEFT_THUMB;
    if (kb & 0x0800u) *out |= XBOX_GAMEPAD_RIGHT_THUMB;
    for (i = 0; i < 8; i++) if (ka[i]) analog[i] = ka[i];
    {
        int lx = ((kb & 0x0008u) ? 1 : 0) - ((kb & 0x0004u) ? 1 : 0);
        int ly = ((kb & 0x0001u) ? 1 : 0) - ((kb & 0x0002u) ? 1 : 0);
        int rx = ((kb & 0x0200u) ? 1 : 0) - ((kb & 0x0100u) ? 1 : 0);
        int ry = ((kb & 0x0040u) ? 1 : 0) - ((kb & 0x0080u) ? 1 : 0);
        if (lx || ly) {
            short v = (lx && ly) ? 23170 : 32767;
            axes[0] = (short)(lx * v); axes[1] = (short)(ly * v);
        }
        if (rx || ry) {
            short v = (rx && ry) ? 23170 : 32767;
            axes[2] = (short)(rx * v); axes[3] = (short)(ry * v);
        }
    }
}

static int old_move(unsigned short kb, int *x, int *y)
{
    *x = ((kb & 0x0008u) ? 1 : 0) - ((kb & 0x0004u) ? 1 : 0);
    *y = ((kb & 0x0001u) ? 1 : 0) - ((kb & 0x0002u) ? 1 : 0);
    return (kb & 0x000Fu) != 0;
}

/* ---- the cases ---------------------------------------------------------- */

/* Each default names each of its controls' inputs, as the tables did. */
static void case_defaults(void)
{
    static const short pad[INPUT_CTL_STICKS] = {
        INPUT_PAD_A, INPUT_PAD_B, INPUT_PAD_X, INPUT_PAD_Y,
        INPUT_PAD_LSHOULDER, INPUT_PAD_RSHOULDER, INPUT_PAD_LTRIGGER, INPUT_PAD_RTRIGGER,
        INPUT_PAD_DPAD_UP, INPUT_PAD_DPAD_DOWN, INPUT_PAD_DPAD_LEFT, INPUT_PAD_DPAD_RIGHT,
        INPUT_PAD_START, INPUT_PAD_BACK, INPUT_PAD_LSTICK, INPUT_PAD_RSTICK,
    };
    static const short keys[INPUT_CTLS] = {
        INPUT_KEY_SPACE_J, INPUT_KEY_K, INPUT_KEY_H, INPUT_KEY_L,
        INPUT_KEY_R, INPUT_KEY_F, INPUT_KEY_Q, INPUT_KEY_E,
        INPUT_KEY_W, INPUT_KEY_S, INPUT_KEY_A, INPUT_KEY_D,
        INPUT_KEY_RETURN, INPUT_KEY_ESC, INPUT_KEY_C, INPUT_KEY_V,
        INPUT_KEY_W, INPUT_KEY_S, INPUT_KEY_A, INPUT_KEY_D,
        INPUT_KEY_UP, INPUT_KEY_DOWN, INPUT_KEY_LEFT, INPUT_KEY_RIGHT,
    };
    InputLayout l;
    int c, b;
    input_layout_pad_default(&l);
    for (c = 0; c < INPUT_CTLS; c++) {
        CHECK(l.bind[c][0] == (c < INPUT_CTL_STICKS ? pad[c] : INPUT_NONE),
              "pad default: control %d has %d", c, l.bind[c][0]);
        for (b = 1; b < INPUT_BINDS; b++)
            CHECK(l.bind[c][b] == INPUT_NONE, "pad default: control %d has a second input", c);
    }
    input_layout_keys_default(&l);
    for (c = 0; c < INPUT_CTLS; c++) {
        CHECK(l.bind[c][0] == keys[c], "keys default: control %d has %d", c, l.bind[c][0]);
        for (b = 1; b < INPUT_BINDS; b++)
            CHECK(l.bind[c][b] == INPUT_NONE, "keys default: control %d has a second input", c);
    }
}

/* The pad's layout gives what the pad's table gave: every one of the
 * fourteen buttons the table read, on or off together, with the triggers
 * at their edges and half way, the menu's Back either way; and the other
 * buttons SDL has change nothing. */
static void case_pad(void)
{
    static const int read[] = {
        INPUT_PAD_A, INPUT_PAD_B, INPUT_PAD_X, INPUT_PAD_Y, INPUT_PAD_BACK, INPUT_PAD_START,
        INPUT_PAD_LSTICK, INPUT_PAD_RSTICK, INPUT_PAD_LSHOULDER, INPUT_PAD_RSHOULDER,
        INPUT_PAD_DPAD_UP, INPUT_PAD_DPAD_DOWN, INPUT_PAD_DPAD_LEFT, INPUT_PAD_DPAD_RIGHT,
    };
    static const int others[] = {
        INPUT_PAD_GUIDE, INPUT_PAD_MISC1, INPUT_PAD_PADDLE1, INPUT_PAD_PADDLE2,
        INPUT_PAD_PADDLE3, INPUT_PAD_PADDLE4, INPUT_PAD_TOUCHPAD,
    };
    static const unsigned char trig[] = { 0, 1, 127, 128, 254, 255 };
    const int nread = (int)(sizeof read / sizeof read[0]);
    InputLayout l;
    unsigned mask;
    int t1, t2, back, i, n = 0, bad = 0;
    input_layout_pad_default(&l);
    for (mask = 0; mask < (1u << nread); mask++)
        for (t1 = 0; t1 < (int)sizeof trig; t1++)
            for (t2 = 0; t2 < (int)sizeof trig; t2++)
                for (back = 0; back < 2; back++) {
                    int down[INPUT_PAD_BUTTONS] = { 0 };
                    unsigned char value[INPUT_PAD_INPUTS] = { 0 }, a_old[8] = { 0 }, a_new[8] = { 0 };
                    WORD o_old = 0, o_new = 0;
                    for (i = 0; i < nread; i++) down[read[i]] = (mask >> i) & 1;
                    /* The others pressed on every other pass. */
                    for (i = 0; i < (int)(sizeof others / sizeof others[0]); i++)
                        down[others[i]] = (int)(mask & 1);
                    old_pad(down, back, trig[t1], trig[t2], &o_old, a_old);
                    for (i = 0; i < INPUT_PAD_BUTTONS; i++) value[i] = down[i] ? 255 : 0;
                    if (!back) value[INPUT_PAD_BACK] = 0;      /* as xinput_device.c does */
                    value[INPUT_PAD_LTRIGGER] = trig[t1];
                    value[INPUT_PAD_RTRIGGER] = trig[t2];
                    input_layout_pad(&l, value, &o_new, a_new);
                    n++;
                    if (o_old != o_new || memcmp(a_old, a_new, 8)) {
                        if (!bad++)
                            printf("FAIL: pad buttons %04x triggers %u %u back %d: %04x against %04x\n",
                                   mask, trig[t1], trig[t2], back, o_new, o_old);
                        g_fail = 1;
                    }
                }
    printf("pad: %d states, %d differ from the table\n", n, bad);
}

/* The keys' layout gives what kb_merge gave: every one of the window's
 * twenty keys, held or not together, over a pad that held nothing, one
 * that held everything, and one with its sticks and analog buttons part
 * way; and it says a key was held when one was. */
static void case_keys(void)
{
    static const struct { WORD out; unsigned char analog[8]; short axes[4]; } pads[] = {
        { 0, { 0 }, { 0, 0, 0, 0 } },
        { 0xFF, { 255, 255, 255, 255, 255, 255, 255, 255 }, { -32768, 32767, 32767, -32768 } },
        { 0x15, { 0, 64, 128, 200, 1, 0, 30, 31 }, { 1000, -2000, 15000, -16000 } },
    };
    InputLayout l;
    unsigned mask;
    int p, i, n = 0, bad = 0;
    input_layout_keys_default(&l);
    for (p = 0; p < (int)(sizeof pads / sizeof pads[0]); p++)
        for (mask = 0; mask < (1u << INPUT_KEYS); mask++) {
            unsigned short kb = (unsigned short)(mask & 0x0FFFu);
            unsigned char ka[8];
            uint32_t held[INPUT_HELD_WORDS] = { 0 };
            WORD o_old = pads[p].out, o_new = pads[p].out;
            unsigned char a_old[8], a_new[8];
            short x_old[4], x_new[4];
            int any;
            for (i = 0; i < 8; i++) ka[i] = ((mask >> (12 + i)) & 1) ? 255 : 0;
            for (i = 0; i < INPUT_KEYS; i++)
                if ((mask >> i) & 1) held[i / 32] |= 1u << (i % 32);
            memcpy(a_old, pads[p].analog, 8);
            memcpy(a_new, pads[p].analog, 8);
            memcpy(x_old, pads[p].axes, sizeof x_old);
            memcpy(x_new, pads[p].axes, sizeof x_new);
            old_keys(kb, ka, &o_old, a_old, x_old);
            any = input_layout_keys(&l, held, &o_new, a_new, x_new);
            n++;
            if (o_old != o_new || memcmp(a_old, a_new, 8) || memcmp(x_old, x_new, sizeof x_old)
             || any != (mask != 0)) {
                if (!bad++)
                    printf("FAIL: keys %05x over pad %d: %04x against %04x, any %d\n",
                           mask, p, o_new, o_old, any);
                g_fail = 1;
            }
        }
    printf("keys: %d states, %d differ from kb_merge\n", n, bad);
}

/* The movement keys alone, as xbox_InputKeyboardMove read them: every
 * one of the twenty held or not. */
static void case_move(void)
{
    InputLayout l;
    unsigned mask;
    int bad = 0;
    input_layout_keys_default(&l);
    for (mask = 0; mask < (1u << INPUT_KEYS); mask++) {
        uint32_t held[INPUT_HELD_WORDS] = { 0 };
        int i, xo, yo, xn, yn, ro, rn;
        for (i = 0; i < INPUT_KEYS; i++)
            if ((mask >> i) & 1) held[i / 32] |= 1u << (i % 32);
        ro = old_move((unsigned short)(mask & 0x0FFFu), &xo, &yo);
        rn = input_layout_keys_move(&l, held, &xn, &yn);
        if (ro != rn || xo != xn || yo != yn) {
            if (!bad++) printf("FAIL: move keys %05x: %d %d %d against %d %d %d\n",
                               mask, rn, xn, yn, ro, xo, yo);
            g_fail = 1;
        }
    }
    printf("move: %d differ\n", bad);
}

/* A layout's other uses, which the defaults don't show: two inputs on a
 * control, the most of them; a trigger on a wButtons control from half way;
 * an input out of range, or none, pressing nothing. */
static void case_binds(void)
{
    InputLayout l;
    unsigned char value[INPUT_PAD_INPUTS] = { 0 }, analog[8] = { 0 };
    uint32_t held[INPUT_HELD_WORDS] = { 0 };
    short axes[4] = { 0 };
    WORD out = 0;
    input_layout_pad_default(&l);
    l.bind[INPUT_CTL_A][1] = INPUT_PAD_RTRIGGER;
    l.bind[INPUT_CTL_START][0] = INPUT_PAD_LTRIGGER;
    l.bind[INPUT_CTL_BACK][0] = INPUT_PAD_INPUTS;           /* out of range */
    value[INPUT_PAD_RTRIGGER] = 90;
    value[INPUT_PAD_LTRIGGER] = INPUT_LAYOUT_DOWN - 1;
    input_layout_pad(&l, value, &out, analog);
    CHECK(analog[0] == 90, "A on the R trigger too: %u", analog[0]);
    CHECK(!(out & XBOX_GAMEPAD_START), "Start on the L trigger short of half way");
    value[INPUT_PAD_LTRIGGER] = INPUT_LAYOUT_DOWN;
    value[INPUT_PAD_A] = 255;
    input_layout_pad(&l, value, &out, analog);
    CHECK(analog[0] == 255, "A on both: %u", analog[0]);
    CHECK(out & XBOX_GAMEPAD_START, "Start on the L trigger at half way");
    CHECK(!(out & XBOX_GAMEPAD_BACK), "Back on no input");
    input_layout_keys_default(&l);
    l.bind[INPUT_CTL_A][0] = INPUT_NONE;
    l.bind[INPUT_CTL_B][1] = INPUT_KEY_SPACE_J;
    held[0] = 1u << INPUT_KEY_SPACE_J;
    memset(analog, 0, sizeof analog);
    out = 0;
    CHECK(input_layout_keys(&l, held, &out, analog, axes) == 1, "a key the layout uses");
    CHECK(analog[0] == 0 && analog[1] == 255, "Space on B, not A: %u %u", analog[0], analog[1]);
    l.bind[INPUT_CTL_B][1] = INPUT_NONE;
    CHECK(input_layout_keys(&l, held, &out, analog, axes) == 0, "a key no control uses");
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if      (!strcmp(c, "defaults")) case_defaults();
    else if (!strcmp(c, "pad"))      case_pad();
    else if (!strcmp(c, "keys"))     case_keys();
    else if (!strcmp(c, "move"))     case_move();
    else if (!strcmp(c, "binds"))    case_binds();
    else { printf("no case %s\n", c); return 2; }
    if (!g_fail) printf("input_layout %s: pass\n", c);
    return g_fail;
}
