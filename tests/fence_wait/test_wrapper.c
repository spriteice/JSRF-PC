/* Run with: python tests/fence_wait/run.py
 *
 * port/recomp_manual.c's sub_00191440 (verbatim, with guest_call_cdecl), the
 * game's side of the wait, against stand-ins for the game functions it calls.
 * Where the game's code would spin, it must make the game's own calls in the
 * game's order, with the same registers and return addresses, then wait for
 * the number the spin waits for, then run the game's code with the registers
 * as they came in. Where the game's code would not spin, it must not wait.
 */
static char g_log[512];
static uint32_t g_dist, g_rec;

static void say(const char *fmt, uint32_t a, uint32_t b, uint32_t c)
{
    size_t n = strlen(g_log);
    snprintf(g_log + n, sizeof g_log - n, fmt, a, b, c);
}

/* Stand-ins: each checks what the game's code would hand it, then clobbers
 * the registers a callee may clobber and returns the way the game's does. */
void sub_00191390(void)                 /* stdcall, one argument: a fence for top */
{
    say("390(ra %08X, arg %u) ", MEM32(g_esp), MEM32(g_esp + 4), 0);
    MEMW32(DEV + 0x30, MEM32(DEV + 0x30) + 2);
    g_eax = 0xDEAD0001u; g_ecx = 0xDEAD0002u; g_edx = 0xDEAD0003u;
    g_esp += 8;
}
void sub_00191150(void)                 /* eax: the number; returns its fence */
{
    say("150(ra %08X, eax %08X) ", MEM32(g_esp), g_eax, 0);
    g_eax = g_rec; g_ecx = 0xDEAD0004u; g_edx = 0xDEAD0005u;
    g_esp += 4;
}
void sub_001910E0(void)                 /* ecx: the device, edx: the fence */
{
    say("0E0(ra %08X, ecx %08X, edx %08X) ", MEM32(g_esp), g_ecx, g_edx);
    g_eax = g_dist; g_ecx = 0xDEAD0006u; g_edx = 0xDEAD0007u;
    g_esp += 4;
}
int nv2a_pb_wait_fence(uint32_t va, uint32_t top, uint32_t time)
{
    say("wait(%08X, top %08X, time %08X) ", va, top, time);
    return 1;
}
static uint32_t g_in[8];
void sub_00191440_gen(void)
{
    uint32_t now[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
    say(memcmp(now, g_in, sizeof now) ? "gen(registers changed)" : "gen", 0, 0, 0);
}

static int fails;
static void one(const char *what, uint32_t time, uint32_t top, uint32_t done, uint32_t rec,
                uint32_t dist, const char *want)
{
    MEMW32(0x0019DCE0u, DEV);
    MEMW32(DEV + 0x30, top);
    MEMW32(DEV + 0x34, FENCE);
    MEMW32(FENCE, done);
    g_rec = rec;
    g_dist = dist;
    g_log[0] = 0;
    g_esp = STACK;
    MEMW32(g_esp + 8, 0);                   /* the flag */
    MEMW32(g_esp + 4, time);
    MEMW32(g_esp, 0x00012345u);             /* the caller's return address */
    g_eax = 0x11111111u; g_ecx = 0x22222222u; g_edx = 0x33333333u; g_ebx = 0x44444444u;
    g_ebp = 0x55555555u; g_esi = 0x66666666u; g_edi = 0x77777777u;
    {
        uint32_t in[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
        memcpy(g_in, in, sizeof in);
    }
    sub_00191440();
    if (strcmp(g_log, want)) {
        fails++;
        printf("FAIL %s:\n  got  %s\n  want %s\n", what, g_log, want);
    } else {
        printf("ok: %s\n", what);
    }
}

int main(void)
{
    static uint8_t guest[0x400000];
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)guest;

    one("the GPU is there already: the game's code only", 0x100, 0x110, 0x104, 0, 0, "gen");
    one("exactly there", 0x104, 0x110, 0x104, 0, 0, "gen");
    one("short of it, the fence close: the game's calls, then a wait for it", 0x108, 0x110,
        0x104, 0x00201234u, 0x7FFF,
        "150(ra 00191478, eax 00000108) 0E0(ra 00191489, ecx 00200000, edx 00201234) "
        "wait(00300000, top 00000110, time 00000108) gen");
    one("waiting for the newest: its fence first", 0x110, 0x110, 0x104, 0x00201234u, 0x10,
        "390(ra 0019146F, arg 0) 150(ra 00191478, eax 00000110) "
        "0E0(ra 00191489, ecx 00200000, edx 00201234) wait(00300000, top 00000112, time 00000110) gen");
    one("the fence 32 KB away: the game's own sleep, no wait here", 0x108, 0x110, 0x104,
        0x00201234u, 0x8000,
        "150(ra 00191478, eax 00000108) 0E0(ra 00191489, ecx 00200000, edx 00201234) gen");
    one("no fence found: the game returns, no wait here", 0x108, 0x110, 0x104, 0, 0,
        "150(ra 00191478, eax 00000108) gen");
    one("numbers that wrap", 0xFFFFFFFEu, 0x00000004u, 0xFFFFFFFCu, 0x00201234u, 0x100,
        "150(ra 00191478, eax FFFFFFFE) 0E0(ra 00191489, ecx 00200000, edx 00201234) "
        "wait(00300000, top 00000004, time FFFFFFFE) gen");
    one("wrapped and there", 0xFFFFFFFEu, 0x00000004u, 0x00000002u, 0, 0, "gen");

    printf(fails ? "fence_wait wrapper: %d case(s) FAIL\n" : "fence_wait wrapper: all cases pass\n",
           fails);
    return fails ? 1 : 0;
}
