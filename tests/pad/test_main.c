/* Drives port/pad.c as the game's pad calls do, on a guest memory of its
 * own, with stand-ins for what pad.c reaches (tests/pad/run.py). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pad.h"

#define DESC     0x0100u        /* a device type's descriptor */
#define INS      0x0200u
#define REM      0x0204u
#define CAPS     0x0300u
#define STATE    0x0400u

static uint8_t g_mem[0x1000];
static int g_bad;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); g_bad = 1; } } while (0)
#define W32(a) (*(uint32_t *)(g_mem + (a)))
#define W16(a) (*(uint16_t *)(g_mem + (a)))
#define S16(a) (*(int16_t *)(g_mem + (a)))

/* The input layer: a host pad on the ports in g_host_ports. */
static int g_inits, g_starts;
static unsigned g_host_ports;
static XBOX_INPUT_STATE g_host;
void xbox_InputInit(void) { g_inits++; }
DWORD xbox_InputGetState(DWORD port, XBOX_INPUT_STATE *st)
{
    if (!(g_host_ports >> port & 1)) return 1;
    *st = g_host;
    return 0;
}
DWORD xbox_InputSetState(DWORD port, const XBOX_VIBRATION *v) { (void)port; (void)v; return 0; }

/* The free camera takes the right stick, the keys the left, when set. */
static int g_freecam_null, g_cam_on, g_keys_on;
void freecam_pad(unsigned port, short *rx, short *ry)
{
    (void)port;
    if (!rx || !ry) { g_freecam_null++; return; }
    if (g_cam_on) { *rx = 111; *ry = -111; }
}
void kbmove_pad(unsigned port, short *lx, short *ly)
{
    (void)port;
    if (g_keys_on) { *lx = 222; *ly = -222; }
}

/* recomp_manual.c's test hooks: a pad of their own on port 0 when set. */
static int g_hooks_on;
void pad_test_start(void) { g_starts++; }
int pad_test_inputs(uint32_t port, XBOX_INPUT_STATE *st)
{
    if (!g_hooks_on || port != 0) return 0;
    st->Gamepad.wButtons = XBOX_GAMEPAD_START;
    st->dwPacketNumber = 77;
    return 1;
}

static void devices(void)
{
    memset(g_mem, 0xEE, sizeof g_mem);
    CHECK(pad_get_devices(g_mem, 0) == 0, "XGetDevices(0) has ports");
    CHECK(W32(0) == 0xEEEEEEEEu, "XGetDevices(0) wrote at 0");
    CHECK(pad_get_devices(g_mem, DESC) == 0, "a type that isn't the gamepad has ports");
    CHECK(W32(DESC) == 0 && W32(DESC + 4) == 0 && W32(DESC + 8) == 0, "another type's descriptor isn't empty");
}

static void gamepad(void)
{
    /* The gamepad's type is the address of its descriptor in the game, so
     * this memory holds one there. */
    static uint8_t big[XPP_TYPE_GAMEPAD + 0x20];
    uint32_t t = XPP_TYPE_GAMEPAD;
    memset(big + t, 0xEE, 0x20);
    CHECK(pad_get_devices(big, t) == 1, "XGetDevices: the gamepad isn't on port 0");
    CHECK(*(uint32_t *)(big + t) == 1 && *(uint32_t *)(big + t + 4) == 0 && *(uint32_t *)(big + t + 8) == 1,
          "the descriptor: present %08X, pending %08X, reported %08X, not 1, 0, 1",
          *(uint32_t *)(big + t), *(uint32_t *)(big + t + 4), *(uint32_t *)(big + t + 8));
    *(uint32_t *)(big + 0x10) = 0xAAAAAAAAu; *(uint32_t *)(big + 0x14) = 0xBBBBBBBBu;
    CHECK(pad_get_device_changes(big, t, 0x10, 0x14) == 0, "XGetDeviceChanges reports a change");
    CHECK(*(uint32_t *)(big + 0x10) == 0 && *(uint32_t *)(big + 0x14) == 0,
          "XGetDeviceChanges: inserted %08X, removed %08X", *(uint32_t *)(big + 0x10), *(uint32_t *)(big + 0x14));
    CHECK(pad_get_device_changes(big, t, 0, 0) == 0, "XGetDeviceChanges with no out-pointers");
}

static void open_caps(void)
{
    CHECK(pad_open(XPP_TYPE_GAMEPAD, 0) == XPP_HANDLE_BASE, "XInputOpen port 0");
    CHECK(pad_open(XPP_TYPE_GAMEPAD, 1) == 0, "XInputOpen port 1 gave a handle");
    CHECK(pad_open(DESC, 0) == 0, "XInputOpen of another type gave a handle");
    memset(g_mem, 0xEE, sizeof g_mem);
    CHECK(pad_get_capabilities(g_mem, 0x12340000u, CAPS) == 0x57, "caps: a handle that isn't one");
    CHECK(pad_get_capabilities(g_mem, XPP_HANDLE_BASE, 0) == 0x57, "caps: no block");
    CHECK(W32(CAPS) == 0xEEEEEEEEu, "caps written on a refusal");
    CHECK(pad_get_capabilities(g_mem, XPP_HANDLE_BASE, CAPS) == 0, "caps refused");
    CHECK(g_mem[CAPS] == 1 && g_mem[CAPS + 1] == 0 && W16(CAPS + 4) == 0x00FF, "caps: subtype or buttons");
    for (int k = 0; k < 8; k++) CHECK(g_mem[CAPS + 6 + k] == 0xFF, "caps: analog button %d", k);
    for (int k = 0; k < 4; k++) CHECK(S16(CAPS + 14 + 2 * k) == 0x7FFF, "caps: stick %d", k);
    CHECK(W16(CAPS + 22) == 0xFFFF && W16(CAPS + 24) == 0xFFFF, "caps: rumble");
    CHECK(W32(CAPS + 28) == 0xEEEEEEEEu, "caps wrote past its 28 bytes");
}

static void state(void)
{
    memset(g_mem, 0xEE, sizeof g_mem);
    CHECK(pad_get_state(g_mem, 0x12340000u, STATE) == 0x57, "state: a handle that isn't one");
    CHECK(pad_get_state(g_mem, XPP_HANDLE_BASE, 0) == 0x57, "state: no block");
    CHECK(g_inits == 0, "the input layer started before a read");

    /* No host pad: port 0 is there and idle, port 1 isn't. */
    g_host_ports = 0;
    CHECK(pad_get_state(g_mem, XPP_HANDLE_BASE, STATE) == 0, "port 0 with no host pad");
    CHECK(W32(STATE) == 0 && W16(STATE + 4) == 0 && S16(STATE + 14) == 0 && S16(STATE + 20) == 0,
          "port 0 with no host pad isn't idle");
    CHECK(g_inits == 1 && g_starts == 1, "started %d and %d times at the first read", g_inits, g_starts);
    g_freecam_null = 0;
    CHECK(pad_get_state(g_mem, XPP_HANDLE_BASE | 1, STATE) == 0x48F, "port 1 with no host pad");
    CHECK(g_freecam_null == 1, "the free camera wasn't told port 1 has no pad");

    /* The host's pad, at XINPUT_STATE's offsets. */
    memset(&g_host, 0, sizeof g_host);
    g_host.dwPacketNumber = 5;
    g_host.Gamepad.wButtons = XBOX_GAMEPAD_DPAD_UP | XBOX_GAMEPAD_BACK;
    for (int k = 0; k < 8; k++) g_host.Gamepad.bAnalogButtons[k] = (uint8_t)(10 + k);
    g_host.Gamepad.sThumbLX = 1; g_host.Gamepad.sThumbLY = 2;
    g_host.Gamepad.sThumbRX = 3; g_host.Gamepad.sThumbRY = 4;
    g_host_ports = 1;
    CHECK(pad_get_state(g_mem, XPP_HANDLE_BASE, STATE) == 0, "the host's pad refused");
    CHECK(W32(STATE) == 5 && W16(STATE + 4) == (XBOX_GAMEPAD_DPAD_UP | XBOX_GAMEPAD_BACK),
          "packet %u, buttons %04X", W32(STATE), W16(STATE + 4));
    for (int k = 0; k < 8; k++) CHECK(g_mem[STATE + 6 + k] == 10 + k, "analog button %d", k);
    CHECK(S16(STATE + 14) == 1 && S16(STATE + 16) == 2 && S16(STATE + 18) == 3 && S16(STATE + 20) == 4,
          "sticks %d %d %d %d", S16(STATE + 14), S16(STATE + 16), S16(STATE + 18), S16(STATE + 20));

    /* The free camera and the keys have the sticks after the host. */
    g_cam_on = g_keys_on = 1;
    pad_get_state(g_mem, XPP_HANDLE_BASE, STATE);
    CHECK(S16(STATE + 14) == 222 && S16(STATE + 16) == -222 && S16(STATE + 18) == 111 && S16(STATE + 20) == -111,
          "the free camera's and the keys' sticks: %d %d %d %d",
          S16(STATE + 14), S16(STATE + 16), S16(STATE + 18), S16(STATE + 20));
    g_cam_on = g_keys_on = 0;

    /* The test hooks' pad, over the host's, on port 0. */
    g_hooks_on = 1;
    pad_get_state(g_mem, XPP_HANDLE_BASE, STATE);
    CHECK(W32(STATE) == 77 && W16(STATE + 4) == XBOX_GAMEPAD_START && g_mem[STATE + 6] == 0 && S16(STATE + 14) == 0,
          "the test hooks' pad: packet %u, buttons %04X", W32(STATE), W16(STATE + 4));
    g_hooks_on = 0;
    CHECK(g_inits == 1 && g_starts == 1, "started again: %d and %d", g_inits, g_starts);
}

int main(void)
{
    devices();
    gamepad();
    open_caps();
    state();
    if (g_bad) return 1;
    printf("pad: all cases pass\n");
    return 0;
}
