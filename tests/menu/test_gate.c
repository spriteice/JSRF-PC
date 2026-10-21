/* Run with: python3 tests/menu/run.py
 *
 * The input layer's side of the in-game menu (menu_gate, taken verbatim from
 * src/input/xinput_device.c by run.py): while the menu is open the game sees
 * a pad with nothing pressed, and what was held as it shut stays unseen
 * until it is let go -- then works again at once. A pad's Back is the
 * menu's whenever there is one (pad_back_is_games, from the same file). */
#include <assert.h>
#include <stdio.h>

static int g_open;             /* 1 open, 0 shut, -1 no menu */
int nv_window_menu_open(void) { return g_open; }

static WORD          out;
static unsigned char analog[8];
static short         axes[4];

/* One poll of port 0 with these held; what the game would see. */
static void poll(WORD buttons, int a, int b, short lx)
{
    int i;
    out = buttons;
    for (i = 0; i < 8; i++) analog[i] = 0;
    analog[0] = a ? 255 : 0;
    analog[1] = b ? 255 : 0;
    axes[0] = lx; axes[1] = axes[2] = axes[3] = 0;
    menu_gate(0, &out, analog, axes);
}

int main(void)
{
    /* Shut, nothing ever held: everything goes through. */
    poll(XBOX_GAMEPAD_START, 1, 0, 1000);
    assert(out == XBOX_GAMEPAD_START && analog[0] == 255 && axes[0] == 1000);

    /* Open: nothing reaches the game, sticks included. */
    g_open = 1;
    poll(XBOX_GAMEPAD_START | XBOX_GAMEPAD_BACK, 1, 1, 1000);
    assert(out == 0 && analog[0] == 0 && analog[1] == 0 && axes[0] == 0);

    /* Shut with B and Start still held: still unseen; A, pressed only now,
     * and the stick go through. */
    poll(XBOX_GAMEPAD_START, 0, 1, 0);
    g_open = 0;
    poll(XBOX_GAMEPAD_START, 1, 1, 2000);
    assert(out == 0 && analog[1] == 0);
    assert(analog[0] == 255 && axes[0] == 2000);

    /* B let go and pressed again: a press. Start, still held, is not. */
    poll(XBOX_GAMEPAD_START, 0, 0, 0);
    poll(XBOX_GAMEPAD_START, 0, 1, 0);
    assert(analog[1] == 255 && out == 0);

    /* Start let go and pressed again: a press. */
    poll(0, 0, 0, 0);
    poll(XBOX_GAMEPAD_START, 0, 0, 0);
    assert(out == XBOX_GAMEPAD_START);

    /* Another port's buttons were never masked. */
    out = XBOX_GAMEPAD_BACK;
    menu_gate(1, &out, analog, axes);
    assert(out == XBOX_GAMEPAD_BACK);

    /* A pad's Back: the menu's, open or shut; the game's only with no menu
     * (built without one, or Dear ImGui did not start), where the gate also
     * lets everything through. */
    assert(!pad_back_is_games());
    g_open = 1;
    assert(!pad_back_is_games());
    poll(0, 0, 0, 0);
    g_open = -1;
    assert(pad_back_is_games());
    poll(XBOX_GAMEPAD_START | XBOX_GAMEPAD_BACK, 1, 0, 1000);
    assert(out == (XBOX_GAMEPAD_START | XBOX_GAMEPAD_BACK) && analog[0] == 255 && axes[0] == 1000);

    puts("menu gate test passed");
    return 0;
}
