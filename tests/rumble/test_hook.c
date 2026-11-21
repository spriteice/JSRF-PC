/* XInputSetState's replacement (port/pad.c), on a feedback block as JSRF
 * lays it out. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pad.h"

#define FEEDBACK 0x1000u        /* the pad object's +0x3F4, where the block is */
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

/* What pad.c's other functions reach, which this test doesn't. */
void xbox_InputInit(void) {}
DWORD xbox_InputGetState(DWORD port, XBOX_INPUT_STATE *st) { (void)port; (void)st; return 1; }
void freecam_pad(unsigned port, short *rx, short *ry) { (void)port; (void)rx; (void)ry; }
void kbmove_pad(unsigned port, short *lx, short *ly) { (void)port; (void)lx; (void)ly; }
void photo_mode_pad(XBOX_INPUT_STATE *st) { (void)st; }
void pad_test_start(void) {}
int pad_test_inputs(uint32_t port, XBOX_INPUT_STATE *st) { (void)port; (void)st; return 0; }
int playback_pad(uint32_t port, XBOX_INPUT_STATE *st, int connected) { (void)port; (void)st; return connected; }

/* As sub_00167180 does: the speeds at +0x42, left in the low word and right
 * in the high, written as one dword (from its +0x47C), into a block the
 * game zeroed when it made the pad; then the call, stdcall (h, feedback). */
static int check(uint16_t left, uint16_t right)
{
    uint32_t both = (uint32_t)left | (uint32_t)right << 16;
    uint32_t eax;
    memset(g_mem + FEEDBACK, 0, 0x8C);
    memcpy(g_mem + FEEDBACK + 0x42, &both, 4);
    g_calls = 0;
    eax = pad_set_state(g_mem, HANDLE, FEEDBACK);
    if (g_calls != 1 || g_got_port != 0 || g_got.wLeftMotorSpeed != left
     || g_got.wRightMotorSpeed != right || eax != 0) {
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
    bad |= check(0xFFFF, 0);            /* the big motor alone */
    bad |= check(0, 0x8000);            /* the small one alone */
    bad |= check(0x1234, 0xABCD);
    bad |= check(0, 0);
    if (bad) return 1;
    printf("rumble hook: all cases pass\n");
    return 0;
}
