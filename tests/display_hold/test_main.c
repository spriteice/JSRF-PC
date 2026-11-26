/* Run with: python tests/display_hold/run.py
 *
 * port/recomp_manual.c's sub_00025740 (verbatim, with disp_loading,
 * disp_let_go_held and guest_call_cdecl): the skip prompt's A holds back
 * the game's letting go of its display set (group 10) while the holder
 * loads it, and does it once the load is over. Called as the game's code
 * calls it, it must leave the stack as the game's own sub_00025740 does,
 * whichever way it goes: the game's function is cdecl and ends in a plain
 * `ret`, so the caller's `add esp, 8` finds the stack where it left it.
 * Known bug 87: the held path returned without its `ret`, 4 bytes low.
 */
#define HOLDER  0x00200000u
#define MANAGER 0x00100000u
#define STACK   0x003F0000u
#define STEP_RA 0x0007D29Cu         /* the flow's step at 0x0007D290: its call's return */

static int fails, gen_calls;
static uint32_t gen_ra, gen_group, gen_word;

/* The game's own sub_00025740: reads its two arguments, clobbers what a
 * callee may, and returns with a plain `ret`. */
void sub_00025740_gen(void)
{
    gen_calls++;
    gen_ra = MEM32(g_esp);
    gen_group = MEM32(g_esp + 4u);
    gen_word = MEM32(g_esp + 8u);
    g_eax = 0xDEAD0001u; g_ecx = 0xDEAD0002u; g_edx = 0xDEAD0003u;
    g_esp += 4;
}

/* The holder: idle, or serving a command for `group`. */
static void holder(int busy, uint32_t group)
{
    MEM32(HOLDER + DISP_BUSY) = (uint32_t)busy;
    MEM32(HOLDER + DISP_SERVING) = group;
}

/* The game's call: push word, push group, call (its return address), then
 * `add esp, 8`. How far esp ends from where the caller had it: 0 is right. */
static int32_t call_from_game(uint32_t group, uint32_t word)
{
    const uint32_t before = g_esp;
    g_esp -= 4; MEM32(g_esp) = word;
    g_esp -= 4; MEM32(g_esp) = group;
    g_esp -= 4; MEM32(g_esp) = STEP_RA;
    sub_00025740();
    g_esp += 8;
    return (int32_t)(g_esp - before);
}

static void check(const char *name, int ok)
{
    printf("%s: %s\n", ok ? "pass" : "FAIL", name);
    if (!ok) fails++;
}

int main(void)
{
    static uint8_t guest[0x400000];
    int32_t off;
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)guest;
    MEM32(GAME_MANAGER) = MANAGER;
    MEM32(MANAGER + GM_ACTORS + DISP_ACTOR * 4u) = HOLDER;
    g_esp = STACK;

    /* The holder idle: the game's own letting go, at once. */
    holder(0, 0);
    gen_calls = 0;
    off = call_from_game(DISP_GROUP, 0);
    check("holder idle: the game's letting go runs, with its group and word",
          gen_calls == 1 && gen_ra == STEP_RA && gen_group == DISP_GROUP && gen_word == 0);
    check("holder idle: the stack as the caller left it", off == 0);
    check("holder idle: nothing held", !s_let_go_held);

    /* Loading another group: not the prompt's; the game's runs. */
    holder(1, 0x12u);
    gen_calls = 0;
    off = call_from_game(DISP_GROUP, 0);
    check("another group loading: the game's letting go runs", gen_calls == 1 && !s_let_go_held);
    check("another group loading: the stack as the caller left it", off == 0);

    /* Loading the display set: held back, and the stack right (bug 87). */
    holder(1, DISP_GROUP);
    gen_calls = 0;
    off = call_from_game(DISP_GROUP, 0);
    check("the set loading: the game's letting go held back", gen_calls == 0 && s_let_go_held);
    check("the set loading: the stack as the caller left it (known bug 87)", off == 0);
    off = call_from_game(DISP_GROUP, 1);
    check("the set loading, let go again: still held, the stack right", gen_calls == 0 && s_let_go_held
          && off == 0);

    /* Another group let go meanwhile: the game's, at once. */
    off = call_from_game(0x12u, 1);
    check("another group let go while the set loads: the game's runs, the stack right",
          gen_calls == 1 && gen_group == 0x12u && off == 0);

    /* Still loading: the held letting go waits. */
    gen_calls = 0;
    disp_let_go_held("at the frame's end");
    check("still loading: the held letting go waits", gen_calls == 0 && s_let_go_held);

    /* The load over: done once, as the flow's own call, and the stack kept. */
    holder(0, 0);
    {
        const uint32_t before = g_esp;
        disp_let_go_held("at the frame's end");
        check("the load over: the held letting go done once, as the flow's call",
              gen_calls == 1 && gen_ra == DISP_LET_GO_RA && gen_group == DISP_GROUP && gen_word == 0
              && !s_let_go_held);
        check("the load over: the stack kept", g_esp == before);
        disp_let_go_held("at the frame's end");
        check("the load over: not done twice", gen_calls == 1);
    }

    printf(fails ? "display_hold: %d case(s) FAIL\n" : "display_hold: all cases pass\n", fails);
    return fails ? 1 : 0;
}
