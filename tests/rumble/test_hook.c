
/* The XInputSetState replacement, on a feedback block as JSRF lays it out. */

#define FEEDBACK 0x1000u        /* the pad object's +0x3F4, where the block is */
#define STACK    0x2000u
#define HANDLE   0x58580000u

static uint8_t g_mem[0x4000];
static XBOX_VIBRATION g_got;
static DWORD g_got_port;
static int g_calls;

DWORD xbox_InputSetState(DWORD port, const XBOX_VIBRATION *v)
{
    g_got = *v;
    g_got_port = port;
    g_calls++;
    return 0;
}

/* As sub_00167180 does: the speeds at +0x42, left in the low word and right
 * in the high, written as one dword (from its +0x47C), into a block the
 * game zeroed when it made the pad; then the call, stdcall (h, feedback). */
static int check(uint16_t left, uint16_t right)
{
    uint32_t both = (uint32_t)left | (uint32_t)right << 16;
    memset(g_mem + FEEDBACK, 0, 0x8C);
    memcpy(g_mem + FEEDBACK + 0x42, &both, 4);
    *(uint32_t *)(g_mem + STACK + 4) = HANDLE;
    *(uint32_t *)(g_mem + STACK + 8) = FEEDBACK;
    g_esp = STACK;
    g_calls = 0;
    sub_001C3E85();
    if (g_calls != 1 || g_got_port != 0 || g_got.wLeftMotorSpeed != left
     || g_got.wRightMotorSpeed != right || g_esp != STACK + 12) {
        printf("FAIL: the game set left %u, right %u; the input layer got left %u, right %u "
               "(%d call(s), port %u)\n", left, right, g_got.wLeftMotorSpeed,
               g_got.wRightMotorSpeed, g_calls, (unsigned)g_got_port);
        return 1;
    }
    return 0;
}

int main(void)
{
    int bad = 0;
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)g_mem;
    bad |= check(0xFFFF, 0);            /* the big motor alone */
    bad |= check(0, 0x8000);            /* the small one alone */
    bad |= check(0x1234, 0xABCD);
    bad |= check(0, 0);
    if (bad) return 1;
    printf("rumble hook: all cases pass\n");
    return 0;
}
