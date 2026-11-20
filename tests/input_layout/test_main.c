/* tests/input_layout: the layouts (src/input/input_layout.c), linked whole,
 * against the fixed tables they replaced, kept here as what the defaults
 * must give; and the layouts in use, their settings and their menu pages
 * (src/input/input_remap.c), linked whole, with remap.cfg, the
 * window's keys, the inputs' names and the menu stood in for (run.py says
 * which cases). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_layout.h"
#include "input_remap.h"
#include "menu.h"

static int g_fail;

/* The window's fixed places for keys before the layouts. */
#define INPUT_KEYS_OLD 20

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
        INPUT_PAD_START, INPUT_NONE, INPUT_PAD_LSTICK, INPUT_PAD_RSTICK,
    };
    static const short keys[INPUT_CTLS] = {
        INPUT_KEY_SPACE, INPUT_KEY_K, INPUT_KEY_H, INPUT_KEY_L,
        INPUT_KEY_R, INPUT_KEY_F, INPUT_KEY_Q, INPUT_KEY_E,
        INPUT_KEY_W, INPUT_KEY_S, INPUT_KEY_A, INPUT_KEY_D,
        INPUT_KEY_RETURN, INPUT_KEY_ESCAPE, INPUT_KEY_C, INPUT_KEY_V,
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
        /* A second key on A, Start and Back, as the window held them. */
        short second = c == INPUT_CTL_A ? INPUT_KEY_J : c == INPUT_CTL_START ? INPUT_KEY_KP_ENTER
                     : c == INPUT_CTL_BACK ? INPUT_KEY_AC_BACK : INPUT_NONE;
        CHECK(l.bind[c][0] == keys[c], "keys default: control %d has %d", c, l.bind[c][0]);
        CHECK(l.bind[c][1] == second, "keys default: control %d has %d second", c, l.bind[c][1]);
    }
}

/* The pad's layout gives what the pad's table gave with the menu there
 * (the pad's Back is none of the layout's: xinput_device.c gives it to the
 * game itself when there is no menu): every one of the fourteen buttons
 * the table read, on or off together, with the triggers at their edges and
 * half way, and the pad's Back handed to the layout or not; and the other
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
                    old_pad(down, 0, trig[t1], trig[t2], &o_old, a_old);
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

/* The window's twenty fixed places before the layouts held keys by their
 * own (nv2a_gl.c's sdl_key): its twelve bits, then its eight analog keys,
 * each a key, or either of two. */
static const short k_old_place[INPUT_KEYS_OLD][2] = {
    { INPUT_KEY_W, INPUT_NONE }, { INPUT_KEY_S, INPUT_NONE }, { INPUT_KEY_A, INPUT_NONE },
    { INPUT_KEY_D, INPUT_NONE }, { INPUT_KEY_RETURN, INPUT_KEY_KP_ENTER },
    { INPUT_KEY_ESCAPE, INPUT_KEY_AC_BACK }, { INPUT_KEY_UP, INPUT_NONE },
    { INPUT_KEY_DOWN, INPUT_NONE }, { INPUT_KEY_LEFT, INPUT_NONE }, { INPUT_KEY_RIGHT, INPUT_NONE },
    { INPUT_KEY_C, INPUT_NONE }, { INPUT_KEY_V, INPUT_NONE },
    { INPUT_KEY_SPACE, INPUT_KEY_J }, { INPUT_KEY_K, INPUT_NONE }, { INPUT_KEY_H, INPUT_NONE },
    { INPUT_KEY_L, INPUT_NONE }, { INPUT_KEY_R, INPUT_NONE }, { INPUT_KEY_F, INPUT_NONE },
    { INPUT_KEY_Q, INPUT_NONE }, { INPUT_KEY_E, INPUT_NONE },
};

static void hold(uint32_t held[INPUT_HELD_WORDS], int in)
{
    if (in >= 0) held[in / 32] |= 1u << (in % 32);
}

/* The keys held for the old places in mask: each place's first key (how
 * 0), its second where it has one (1), or both (2). */
static void old_held(unsigned mask, int how, uint32_t held[INPUT_HELD_WORDS])
{
    int i;
    memset(held, 0, INPUT_HELD_WORDS * sizeof held[0]);
    for (i = 0; i < INPUT_KEYS_OLD; i++) {
        if (!((mask >> i) & 1)) continue;
        if (how != 1 || k_old_place[i][1] == INPUT_NONE) hold(held, k_old_place[i][0]);
        if (how != 0) hold(held, k_old_place[i][1]);
    }
}

/* The keys' layout gives what kb_merge gave: every one of the window's
 * twenty places, held or not together, by its first key, its second, or
 * both, over a pad that held nothing, one that held everything, and one
 * with its sticks and analog buttons part way; and it says a key was held
 * when one was. */
static void case_keys(void)
{
    static const struct { WORD out; unsigned char analog[8]; short axes[4]; } pads[] = {
        { 0, { 0 }, { 0, 0, 0, 0 } },
        { 0xFF, { 255, 255, 255, 255, 255, 255, 255, 255 }, { -32768, 32767, 32767, -32768 } },
        { 0x15, { 0, 64, 128, 200, 1, 0, 30, 31 }, { 1000, -2000, 15000, -16000 } },
    };
    InputLayout l;
    unsigned mask;
    int p, i, how, n = 0, bad = 0;
    input_layout_keys_default(&l);
    for (p = 0; p < (int)(sizeof pads / sizeof pads[0]); p++)
        for (how = 0; how < 3; how++)
        for (mask = 0; mask < (1u << INPUT_KEYS_OLD); mask++) {
            unsigned short kb = (unsigned short)(mask & 0x0FFFu);
            unsigned char ka[8];
            uint32_t held[INPUT_HELD_WORDS];
            WORD o_old = pads[p].out, o_new = pads[p].out;
            unsigned char a_old[8], a_new[8];
            short x_old[4], x_new[4];
            int any;
            for (i = 0; i < 8; i++) ka[i] = ((mask >> (12 + i)) & 1) ? 255 : 0;
            old_held(mask, how, held);
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
                    printf("FAIL: keys %05x (%d) over pad %d: %04x against %04x, any %d\n",
                           mask, how, p, o_new, o_old, any);
                g_fail = 1;
            }
        }
    printf("keys: %d states, %d differ from kb_merge\n", n, bad);
}

/* The movement keys alone, as xbox_InputKeyboardMove read them: every
 * one of the twenty places held or not, by either key or both. */
static void case_move(void)
{
    InputLayout l;
    unsigned mask;
    int bad = 0, how;
    input_layout_keys_default(&l);
    for (how = 0; how < 3; how++)
    for (mask = 0; mask < (1u << INPUT_KEYS_OLD); mask++) {
        uint32_t held[INPUT_HELD_WORDS];
        int xo, yo, xn, yn, ro, rn;
        old_held(mask, how, held);
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
    l.bind[INPUT_CTL_A][1] = INPUT_NONE;
    l.bind[INPUT_CTL_B][1] = INPUT_KEY_SPACE;
    hold(held, INPUT_KEY_SPACE);
    memset(analog, 0, sizeof analog);
    out = 0;
    CHECK(input_layout_keys(&l, held, &out, analog, axes) == 1, "a key the layout uses");
    CHECK(analog[0] == 0 && analog[1] == 255, "Space on B, not A: %u %u", analog[0], analog[1]);
    l.bind[INPUT_CTL_B][1] = INPUT_NONE;
    CHECK(input_layout_keys(&l, held, &out, analog, axes) == 0, "a key no control uses");
    /* Space and J each A's: letting go of one leaves A held while the other
     * is (the window held them as one place, so letting go of J let A go
     * with Space still down). */
    input_layout_keys_default(&l);
    memset(held, 0, sizeof held);
    hold(held, INPUT_KEY_SPACE);
    memset(analog, 0, sizeof analog);
    input_layout_keys(&l, held, &out, analog, axes);
    CHECK(analog[0] == 255, "Space alone: A %u", analog[0]);
    memset(held, 0, sizeof held);
    hold(held, INPUT_KEY_J);
    memset(analog, 0, sizeof analog);
    input_layout_keys(&l, held, &out, analog, axes);
    CHECK(analog[0] == 255, "J alone: A %u", analog[0]);
    /* A mouse button, and the last key, as any other input. */
    l.bind[INPUT_CTL_Y][1] = INPUT_KEYS - 1;
    l.bind[INPUT_CTL_X][1] = INPUT_KEY_SCANCODES - 1;
    memset(held, 0, sizeof held);
    hold(held, INPUT_KEYS - 1);
    hold(held, INPUT_KEY_SCANCODES - 1);
    memset(analog, 0, sizeof analog);
    CHECK(input_layout_keys(&l, held, &out, analog, axes) == 1, "a mouse button the layout uses");
    CHECK(analog[2] == 255 && analog[3] == 255, "X and Y by the last key and the last mouse button: %u %u",
          analog[2], analog[3]);
}

/* A row's bind, as the menu hands it a press: the swap. */
static void case_bind(void)
{
    InputLayout l, def;
    input_layout_pad_default(&l);
    /* X's button for A: X gets A's. */
    input_layout_bind(&l, INPUT_CTL_A, INPUT_PAD_X);
    CHECK(l.bind[INPUT_CTL_A][0] == INPUT_PAD_X && l.bind[INPUT_CTL_X][0] == INPUT_PAD_A,
          "A on X's button, X on A's: %d %d", l.bind[INPUT_CTL_A][0], l.bind[INPUT_CTL_X][0]);
    /* A paddle for Back, which had none: nobody had the paddle. */
    input_layout_bind(&l, INPUT_CTL_BACK, INPUT_PAD_PADDLE1);
    CHECK(l.bind[INPUT_CTL_BACK][0] == INPUT_PAD_PADDLE1, "Back on a paddle");
    /* Back's paddle for Start: Back gets Start's, Start the paddle alone. */
    input_layout_bind(&l, INPUT_CTL_START, INPUT_PAD_PADDLE1);
    CHECK(l.bind[INPUT_CTL_START][0] == INPUT_PAD_PADDLE1 && l.bind[INPUT_CTL_BACK][0] == INPUT_PAD_START,
          "Start on the paddle, Back on Start's button");
    /* A control's input bound to it again: nothing moves. */
    input_layout_keys_default(&l);
    input_layout_bind(&l, INPUT_CTL_DPAD_UP, INPUT_KEY_W);
    CHECK(l.bind[INPUT_CTL_DPAD_UP][0] == INPUT_KEY_W && l.bind[INPUT_CTL_LSTICK_UP][0] == INPUT_KEY_W,
          "W on d-pad up again: the left stick keeps it");
    /* J for B: B has J alone, and A gets K where it had J, keeping Space. */
    input_layout_keys_default(&l);
    input_layout_bind(&l, INPUT_CTL_B, INPUT_KEY_J);
    CHECK(l.bind[INPUT_CTL_B][0] == INPUT_KEY_J && l.bind[INPUT_CTL_B][1] == INPUT_NONE, "B on J");
    CHECK(l.bind[INPUT_CTL_A][0] == INPUT_KEY_SPACE && l.bind[INPUT_CTL_A][1] == INPUT_KEY_K,
          "A on Space and K: %d %d", l.bind[INPUT_CTL_A][0], l.bind[INPUT_CTL_A][1]);
    /* Space for A: A has Space alone; J goes. */
    input_layout_keys_default(&l);
    input_layout_bind(&l, INPUT_CTL_A, INPUT_KEY_SPACE);
    CHECK(l.bind[INPUT_CTL_A][0] == INPUT_KEY_SPACE && l.bind[INPUT_CTL_A][1] == INPUT_NONE, "A on Space alone");
    /* W for A: both of W's controls get Space, as W pressed both. */
    input_layout_keys_default(&l);
    input_layout_bind(&l, INPUT_CTL_A, INPUT_KEY_W);
    CHECK(l.bind[INPUT_CTL_A][0] == INPUT_KEY_W && l.bind[INPUT_CTL_DPAD_UP][0] == INPUT_KEY_SPACE
          && l.bind[INPUT_CTL_LSTICK_UP][0] == INPUT_KEY_SPACE, "A on W: d-pad up and the stick on Space");
    /* Return for Back: Start had Return and keypad Enter, and gets Esc in
     * Return's place. Then keypad Enter for Start: Start has it alone, and
     * nothing else had it. */
    input_layout_keys_default(&l);
    input_layout_bind(&l, INPUT_CTL_BACK, INPUT_KEY_RETURN);
    CHECK(l.bind[INPUT_CTL_START][0] == INPUT_KEY_ESCAPE && l.bind[INPUT_CTL_START][1] == INPUT_KEY_KP_ENTER,
          "Start on Esc and keypad Enter");
    /* An input a control has in both its places can't come of a swap: the
     * one that would have it twice gets none in the second. */
    input_layout_keys_default(&l);
    l.bind[INPUT_CTL_B][1] = INPUT_KEY_SPACE;        /* B: K and Space */
    input_layout_bind(&l, INPUT_CTL_A, INPUT_KEY_K);  /* A was Space first */
    CHECK(l.bind[INPUT_CTL_B][0] == INPUT_NONE && l.bind[INPUT_CTL_B][1] == INPUT_KEY_SPACE,
          "B keeps Space once: %d %d", l.bind[INPUT_CTL_B][0], l.bind[INPUT_CTL_B][1]);
    /* No input, or a control out of range: as asked, or nothing. */
    input_layout_keys_default(&l);
    def = l;
    input_layout_bind(&l, INPUT_CTLS, INPUT_KEY_K);
    CHECK(!memcmp(&l, &def, sizeof l), "a control out of range changes nothing");
    input_layout_bind(&l, INPUT_CTL_B, INPUT_NONE);
    CHECK(l.bind[INPUT_CTL_B][0] == INPUT_NONE && l.bind[INPUT_CTL_A][0] == INPUT_KEY_SPACE, "B on none");
}

/* The inputs a layout uses, for the latency log. */
static void case_used(void)
{
    InputLayout l;
    uint32_t used[INPUT_HELD_WORDS];
    int i, n = 0;
    input_layout_keys_default(&l);
    input_layout_used(&l, used);
    for (i = 0; i < INPUT_KEYS; i++) n += (used[i / 32] >> (i % 32)) & 1;
    CHECK(n == 23, "the default keys: %d used", n);   /* 24 controls, 3 second keys, W A S D twice */
    CHECK(used[INPUT_KEY_AC_BACK / 32] & (1u << (INPUT_KEY_AC_BACK % 32)), "the Back key used");
    CHECK(!(used[INPUT_KEY_MOUSE / 32] & (1u << (INPUT_KEY_MOUSE % 32))), "no mouse button");
    l.bind[INPUT_CTL_Y][0] = INPUT_KEYS - 1;
    input_layout_used(&l, used);
    CHECK(used[(INPUT_KEYS - 1) / 32] & (1u << ((INPUT_KEYS - 1) % 32)), "the mouse's forward button");
}

/* ---- input_remap.c's stand-ins -------------------------------------------- */

/* remap.cfg, beside the settings file: its lines, and how many writes. A
 * line read or kept in any other file fails the case. */
static struct { char key[48], val[128]; } s_cfg[128];
static int s_ncfg, s_writes;

static int cfg_find(const char *key)
{
    int i;
    for (i = 0; i < s_ncfg; i++) if (!strcmp(s_cfg[i].key, key)) return i;
    return -1;
}
static int remap_cfg(const char *file)
{
    CHECK(file && !strcmp(file, "remap.cfg"), "a layout's line in %s, not remap.cfg", file ? file : "no file");
    return file && !strcmp(file, "remap.cfg");
}
int nv_window_file_str(const char *file, const char *key, char *buf, size_t n)
{
    int i = cfg_find(key);
    if (!remap_cfg(file) || i < 0) return 0;
    snprintf(buf, n, "%s", s_cfg[i].val);
    return 1;
}
void nv_window_file_set_str(const char *file, const char *key, const char *val)
{
    int i = cfg_find(key);
    if (!remap_cfg(file)) return;
    s_writes++;
    if (!val) {
        if (i >= 0) s_cfg[i] = s_cfg[--s_ncfg];
        return;
    }
    if (i < 0) i = s_ncfg++;
    snprintf(s_cfg[i].key, sizeof s_cfg[i].key, "%s", key);
    snprintf(s_cfg[i].val, sizeof s_cfg[i].val, "%s", val);
}
static void cfg(const char *key, const char *val) { nv_window_file_set_str("remap.cfg", key, val); s_writes = 0; }
static const char *cfg_val(const char *key) { int i = cfg_find(key); return i < 0 ? NULL : s_cfg[i].val; }

/* The window's keys: F1, F8 (a key the game binds), F9 to F11. */
#define SC_F1 58
#define SC_F8 65
#define SC_F11 68
int nv_window_key_reserved(int sc) { return sc == SC_F1 || (sc >= SC_F8 && sc <= SC_F11); }

/* The inputs' names, as SDL 2.30 gives them, for the inputs these cases
 * use: the pad's (SDL_GameControllerGetStringForButton and ForAxis), and
 * the keys' (SDL_GetScancodeName), with the menu's names for the pad and
 * the mouse's (xinput_device.c). Scancode 300 has none. */
static const char *const k_pad_kept[INPUT_PAD_INPUTS] = {
    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
    "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright",
    "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad", "lefttrigger", "righttrigger",
};
static const char *const k_pad_shown[INPUT_PAD_INPUTS] = {
    "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", "Touchpad", "LT", "RT",
};
static const struct { int sc; const char *name; } k_keys[] = {
    { 4, "A" }, { 6, "C" }, { 7, "D" }, { 8, "E" }, { 9, "F" }, { 11, "H" }, { 12, "I" }, { 13, "J" },
    { 14, "K" }, { 15, "L" }, { 20, "Q" }, { 21, "R" }, { 22, "S" }, { 25, "V" }, { 26, "W" },
    { 40, "Return" }, { 41, "Escape" }, { 44, "Space" }, { SC_F1, "F1" }, { SC_F8, "F8" },
    { 66, "F9" }, { SC_F11, "F11" }, { 79, "Right" }, { 80, "Left" }, { 81, "Down" }, { 82, "Up" },
    { 88, "Keypad Enter" }, { 224, "Left Ctrl" }, { 270, "AC Back" },
};
static const char *const k_mouse[INPUT_KEY_MOUSE_BUTTONS] = {
    "Mouse left", "Mouse middle", "Mouse right", "Mouse back", "Mouse forward",
};
const char *input_remap_name(int layout, int input, int how)
{
    unsigned i;
    if (layout == INPUT_REMAP_PAD)
        return input < 0 || input >= INPUT_PAD_INPUTS ? NULL
             : how == INPUT_NAME_SHOWN ? k_pad_shown[input] : k_pad_kept[input];
    if (input >= INPUT_KEY_MOUSE && input < INPUT_KEYS) return k_mouse[input - INPUT_KEY_MOUSE];
    for (i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++) if (k_keys[i].sc == input) return k_keys[i].name;
    return NULL;
}
int input_remap_from_name(int layout, const char *name)
{
    unsigned i;
    if (layout == INPUT_REMAP_PAD) {
        for (i = 0; i < INPUT_PAD_INPUTS; i++) if (!strcmp(name, k_pad_kept[i])) return (int)i;
        return INPUT_NONE;
    }
    for (i = 0; i < INPUT_KEY_MOUSE_BUTTONS; i++) if (!strcmp(name, k_mouse[i])) return INPUT_KEY_MOUSE + (int)i;
    for (i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++) if (!strcmp(name, k_keys[i].name)) return k_keys[i].sc;
    return INPUT_NONE;
}

/* The menu: the rows added. */
static XBOX_MENU_ITEM s_rows[64];
static int s_nrows, s_adds;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    int i;
    s_adds++;
    for (i = 0; i < count && s_nrows < 64; i++) s_rows[s_nrows++] = items[i];
}
static const XBOX_MENU_ITEM *row(const char *group, const char *label)
{
    int i;
    for (i = 0; i < s_nrows; i++)
        if (s_rows[i].group && !strcmp(s_rows[i].group, group) && !strcmp(s_rows[i].label, label))
            return &s_rows[i];
    printf("FAIL: no row %s in %s\n", label, group);
    exit(1);
}
#define PAD XBOX_GROUP_PAD_LAYOUT
#define KEYS XBOX_GROUP_KEY_LAYOUT
#define ROW_SHOWS(group, label, want) \
    CHECK(!strcmp(row(group, label)->text(), want), "%s's %s shows %s, not %s", group, label, \
          row(group, label)->text(), want)

/* The log, what input_remap.c writes to stderr, kept: run.py checks it. */

/* The rows: Controller layout's sixteen, Keyboard layout's
 * twenty-four, each page's Reset to default, each submenu first, in one
 * add; each row Input's, left alone by the presets, with a hint in a
 * sentence the strip can hold; each bind row whole; and what they show at
 * the defaults, with no settings and nothing said in the log. */
static void case_rows(void)
{
    static const char *const pad_rows[] = {
        "A", "B", "X", "Y", "Black", "White", "L trigger", "R trigger", "Start", "Back",
        "Left stick click", "Right stick click", "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    };
    int i;
    input_remap_start();
    CHECK(s_adds == 1 && s_nrows == 1 + 16 + 1 + 1 + 24 + 1, "%d adds, %d rows", s_adds, s_nrows);
    CHECK(s_rows[0].kind == XBOX_ITEM_GROUP && !strcmp(s_rows[0].label, PAD) && !s_rows[0].group,
          "Controller layout first");
    CHECK(s_rows[18].kind == XBOX_ITEM_GROUP && !strcmp(s_rows[18].label, KEYS) && !s_rows[18].group,
          "then Keyboard layout");
    for (i = 0; i < 16; i++)
        CHECK(!strcmp(s_rows[1 + i].label, pad_rows[i]), "the controller's row %d is %s", i, s_rows[1 + i].label);
    CHECK(!strcmp(s_rows[19].label, "Left stick up") && !strcmp(s_rows[27].label, "A"),
          "the keyboard's sticks, then its buttons");
    CHECK(!strcmp(s_rows[17].label, "Reset to default") && s_rows[17].kind == XBOX_ITEM_ACTION
          && !strcmp(s_rows[17].group, PAD), "the controller's reset");
    CHECK(!strcmp(s_rows[43].label, "Reset to default") && s_rows[43].kind == XBOX_ITEM_ACTION
          && !strcmp(s_rows[43].group, KEYS), "the keyboard's reset");
    for (i = 0; i < s_nrows; i++) {
        const XBOX_MENU_ITEM *r = &s_rows[i];
        size_t h = r->hint ? strlen(r->hint) : 0;
        CHECK(r->menu == XBOX_MENU_INPUT, "%s: Input's", r->label);
        CHECK(h > 0 && h <= MENU_HINT_LONGEST && r->hint[h - 1] == '.', "%s: its hint", r->label);
        CHECK(r->kind == XBOX_ITEM_GROUP || r->presets == XBOX_PRESETS_LEFT, "%s: left by the presets", r->label);
        if (r->kind == XBOX_ITEM_BIND)
            CHECK(r->bind && r->text && r->takes == (i < 18 ? XBOX_BIND_PAD : XBOX_BIND_KEYS),
                  "%s: a whole bind row", r->label);
    }
    ROW_SHOWS(PAD, "A", "A"); ROW_SHOWS(PAD, "Black", "LB"); ROW_SHOWS(PAD, "R trigger", "RT");
    ROW_SHOWS(PAD, "Back", "None"); ROW_SHOWS(PAD, "Right stick click", "R3");
    ROW_SHOWS(PAD, "D-pad left", "D-pad left");
    ROW_SHOWS(KEYS, "A", "Space, J"); ROW_SHOWS(KEYS, "Start", "Return, Keypad Enter");
    ROW_SHOWS(KEYS, "Back", "Escape, AC Back"); ROW_SHOWS(KEYS, "Left stick up", "W");
    ROW_SHOWS(KEYS, "Right stick left", "Left");
    CHECK(s_writes == 0 && s_ncfg == 0, "nothing kept");
}

/* Binding, through the rows: the swap, kept in remap.cfg at once, line
 * by line, and said in the log; what can't be bound refused, with nothing
 * changed; and Keyboard moves following the left stick's rows. */
static void case_bind_rows(void)
{
    InputLayout pad, keys, def;
    uint32_t held[INPUT_HELD_WORDS] = { 0 };
    unsigned char value[INPUT_PAD_INPUTS] = { 0 }, analog[8] = { 0 };
    WORD out = 0;
    int x, y;
    input_remap_start();
    /* The pad: X's button for A. */
    CHECK(row(PAD, "A")->bind(INPUT_PAD_X) == 1, "A takes X's button");
    ROW_SHOWS(PAD, "A", "X"); ROW_SHOWS(PAD, "X", "A");
    CHECK(s_ncfg == 2 && !strcmp(cfg_val("pad_a"), "x") && !strcmp(cfg_val("pad_x"), "a"), "kept as pad_a=x, pad_x=a");
    input_remap_layouts(&pad, NULL);
    value[INPUT_PAD_X] = 255;
    input_layout_pad(&pad, value, &out, analog);
    CHECK(analog[0] == 255 && analog[2] == 0, "X's button presses A");
    /* A trigger on Back, a paddle on Start. */
    CHECK(row(PAD, "Back")->bind(INPUT_PAD_LTRIGGER) == 1, "Back takes LT");
    ROW_SHOWS(PAD, "Back", "LT"); ROW_SHOWS(PAD, "L trigger", "None");
    CHECK(!strcmp(cfg_val("pad_back"), "lefttrigger") && !strcmp(cfg_val("pad_ltrigger"), "none"), "kept");
    /* Refused: Guide, the pad's Back, past the inputs; nothing changes. */
    s_writes = 0;
    input_remap_layouts(&def, NULL);
    CHECK(row(PAD, "Y")->bind(INPUT_PAD_GUIDE) == 0, "Guide refused");
    CHECK(row(PAD, "Y")->bind(INPUT_PAD_BACK) == 0, "the pad's Back refused");
    CHECK(row(PAD, "Y")->bind(INPUT_PAD_INPUTS) == 0 && row(PAD, "Y")->bind(-1) == 0, "no such input");
    input_remap_layouts(&pad, NULL);
    CHECK(!memcmp(&pad, &def, sizeof pad) && s_writes == 0, "a refusal changes nothing");
    /* Back to A's own button: the line goes. */
    CHECK(row(PAD, "A")->bind(INPUT_PAD_A) == 1, "A takes its own back");
    CHECK(!cfg_val("pad_a") && !cfg_val("pad_x"), "A and X back at their defaults, their lines gone");
    /* The keys: J for B, so A has Space and K. */
    CHECK(row(KEYS, "B")->bind(INPUT_KEY_J) == 1, "B takes J");
    ROW_SHOWS(KEYS, "B", "J"); ROW_SHOWS(KEYS, "A", "Space, K");
    CHECK(!strcmp(cfg_val("key_b"), "J") && !strcmp(cfg_val("key_a_2"), "K") && !cfg_val("key_a"),
          "kept as key_b=J and key_a_2=K");
    /* Refused: F1, F8, F9, F11, a key SDL has no name for, past the inputs. */
    s_writes = 0;
    CHECK(row(KEYS, "Y")->bind(SC_F1) == 0 && row(KEYS, "Y")->bind(SC_F8) == 0
          && row(KEYS, "Y")->bind(66) == 0 && row(KEYS, "Y")->bind(SC_F11) == 0, "the window's keys refused");
    CHECK(row(KEYS, "Y")->bind(300) == 0, "a key with no name refused");
    CHECK(row(KEYS, "Y")->bind(INPUT_KEYS) == 0, "no such input");
    CHECK(s_writes == 0, "a refusal keeps nothing");
    /* A mouse button, and a key past the defaults'. */
    CHECK(row(KEYS, "Y")->bind(INPUT_KEY_MOUSE + 2) == 1 && row(KEYS, "X")->bind(224) == 1, "Y and X");
    ROW_SHOWS(KEYS, "Y", "Mouse right"); ROW_SHOWS(KEYS, "X", "Left Ctrl");
    CHECK(!strcmp(cfg_val("key_y"), "Mouse right") && !strcmp(cfg_val("key_x"), "Left Ctrl"), "kept by name");
    /* Space for A: A has it alone, and its second goes as none. */
    CHECK(row(KEYS, "A")->bind(INPUT_KEY_SPACE) == 1, "A takes Space");
    ROW_SHOWS(KEYS, "A", "Space");
    CHECK(!cfg_val("key_a") && !strcmp(cfg_val("key_a_2"), "none"), "kept as key_a_2=none");
    /* Keyboard moves follow the left stick's rows: I for up, and W is the
     * d-pad's alone. */
    CHECK(row(KEYS, "Left stick up")->bind(12) == 1, "the left stick up takes I");
    input_remap_layouts(NULL, &keys);
    hold(held, 12);
    CHECK(input_layout_keys_move(&keys, held, &x, &y) == 1 && x == 0 && y == 1, "I moves up");
    memset(held, 0, sizeof held);
    hold(held, INPUT_KEY_W);
    CHECK(input_layout_keys_move(&keys, held, &x, &y) == 0 && y == 0, "W moves no more");
    memset(analog, 0, sizeof analog);
    out = 0;
    {
        short axes[4] = { 0 };
        input_layout_keys(&keys, held, &out, analog, axes);
        CHECK(out == XBOX_GAMEPAD_DPAD_UP && axes[1] == 0, "W is d-pad up alone");
    }
}

/* Reset to default, on each page: the layout as it starts, every line
 * gone, and the log says so. */
static void case_reset(void)
{
    InputLayout pad, keys, def;
    input_remap_start();
    row(PAD, "A")->bind(INPUT_PAD_Y);
    row(PAD, "Start")->bind(INPUT_PAD_PADDLE2);
    row(KEYS, "A")->bind(INPUT_KEY_MOUSE);
    row(KEYS, "Start")->bind(INPUT_KEY_SPACE);
    /* pad_a, pad_y, pad_start; key_a, key_a_2, key_start, key_start_2 */
    CHECK(s_ncfg == 7, "%d lines kept", s_ncfg);
    row(PAD, "Reset to default")->run();
    input_remap_layouts(&pad, &keys);
    input_layout_pad_default(&def);
    CHECK(!memcmp(&pad, &def, sizeof pad), "the controller back at its defaults");
    CHECK(!cfg_val("pad_a") && !cfg_val("pad_y") && !cfg_val("pad_start") && s_ncfg == 4, "its lines gone");
    input_layout_keys_default(&def);
    CHECK(memcmp(&keys, &def, sizeof keys), "the keyboard's left alone");
    row(KEYS, "Reset to default")->run();
    input_remap_layouts(NULL, &keys);
    CHECK(!memcmp(&keys, &def, sizeof keys) && s_ncfg == 0, "the keyboard back at its defaults, its lines gone");
    ROW_SHOWS(KEYS, "A", "Space, J");
}

/* At start, from remap.cfg: what the rows kept reads back as it was;
 * an unknown name, or an input a layout can't have, keeps the default and
 * the log says so; none is none. */
static void case_settings(void)
{
    InputLayout pad, keys, want;
    cfg("pad_a", "x"); cfg("pad_x", "a"); cfg("pad_back", "lefttrigger"); cfg("pad_ltrigger", "none");
    cfg("pad_b", "flipper"); cfg("pad_y", "guide"); cfg("pad_start", "back");
    cfg("key_b", "J"); cfg("key_a_2", "K"); cfg("key_y", "Mouse right"); cfg("key_x", "Left Ctrl");
    cfg("key_lthumb", "F9"); cfg("key_rthumb", "Hyper"); cfg("key_lstick_up", "I");
    input_remap_start();
    input_remap_layouts(&pad, &keys);
    input_layout_pad_default(&want);
    input_layout_bind(&want, INPUT_CTL_A, INPUT_PAD_X);
    input_layout_bind(&want, INPUT_CTL_BACK, INPUT_PAD_LTRIGGER);
    CHECK(!memcmp(&pad, &want, sizeof pad), "the controller's lines read back as the rows made them");
    input_layout_keys_default(&want);
    input_layout_bind(&want, INPUT_CTL_B, INPUT_KEY_J);
    want.bind[INPUT_CTL_Y][0] = INPUT_KEY_MOUSE + 2;
    want.bind[INPUT_CTL_X][0] = 224;
    want.bind[INPUT_CTL_LSTICK_UP][0] = 12;
    CHECK(!memcmp(&keys, &want, sizeof keys), "the keyboard's lines read back");
    ROW_SHOWS(PAD, "B", "B"); ROW_SHOWS(PAD, "Y", "Y"); ROW_SHOWS(PAD, "Start", "Start");
    ROW_SHOWS(KEYS, "Left stick click", "C"); ROW_SHOWS(KEYS, "Right stick click", "V");
    CHECK(s_writes == 0, "reading writes nothing");
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if      (!strcmp(c, "defaults")) case_defaults();
    else if (!strcmp(c, "pad"))      case_pad();
    else if (!strcmp(c, "keys"))     case_keys();
    else if (!strcmp(c, "move"))     case_move();
    else if (!strcmp(c, "binds"))    case_binds();
    else if (!strcmp(c, "bind"))     case_bind();
    else if (!strcmp(c, "used"))     case_used();
    else if (!strcmp(c, "rows"))     case_rows();
    else if (!strcmp(c, "bind_rows")) case_bind_rows();
    else if (!strcmp(c, "reset"))    case_reset();
    else if (!strcmp(c, "settings")) case_settings();
    else { printf("no case %s\n", c); return 2; }
    if (!g_fail) printf("input_layout %s: pass\n", c);
    return g_fail;
}
