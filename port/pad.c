/* The pad: the game's pad API answered from the host's pads (pad.h says
 * how it reaches the game). */
#include "pad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freecam.h"
#include "keyboard_move.h"
#include "photo_mode.h"
#include "playback.h"

/* What the game's pad functions return (Win32's error numbers). */
#define XPP_ERROR_SUCCESS              0x000u
#define XPP_ERROR_INVALID_PARAMETER    0x057u
#define XPP_ERROR_DEVICE_NOT_CONNECTED 0x48Fu

/* A handle is the base in its high half and the port in its low bits. */
#define XPP_HANDLE_HIGH 0xFFFF0000u
#define XPP_HANDLE_PORT 3u

/* A device type's descriptor, in the game's memory: its masks of ports. */
#define XPP_DESC_PRESENT  0x0u        /* connected now */
#define XPP_DESC_PENDING  0x4u        /* a change not yet reported */
#define XPP_DESC_REPORTED 0x8u        /* as last reported */

/* XINPUT_CAPABILITIES. */
#define XPP_CAPS_SIZE     28u
#define XPP_CAPS_SUBTYPE  0u          /* XINPUT_DEVSUBTYPE_GC_GAMEPAD: 1 */
#define XPP_CAPS_BUTTONS  4u          /* In.wButtons */
#define XPP_CAPS_ANALOG   6u          /* In.bAnalogButtons[8] */
#define XPP_CAPS_THUMBS   14u         /* In.sThumbLX ... sThumbRY */
#define XPP_CAPS_RUMBLE   22u         /* Out.Rumble's two motors */

/* XINPUT_STATE. */
#define XPP_STATE_PACKET  0u          /* dwPacketNumber */
#define XPP_STATE_BUTTONS 4u          /* Gamepad.wButtons */
#define XPP_STATE_ANALOG  6u          /* Gamepad.bAnalogButtons[8] */
#define XPP_STATE_LX      14u
#define XPP_STATE_LY      16u
#define XPP_STATE_RX      18u
#define XPP_STATE_RY      20u

/* XINPUT_FEEDBACK: see pad_set_state. */
#define XPP_FEEDBACK_STATUS 0x00u     /* Header.dwStatus */
#define XPP_FEEDBACK_EVENT  0x04u     /* Header.hEvent */
#define XPP_FEEDBACK_LEFT   0x42u     /* Rumble.wLeftMotorSpeed, after the header */
#define XPP_FEEDBACK_RIGHT  0x44u     /* Rumble.wRightMotorSpeed */

#define W32(mem, a) (*(uint32_t *)((mem) + (a)))
#define W16(mem, a) (*(uint16_t *)((mem) + (a)))

double pad_now(void)
{
    static struct timespec t0; struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    if (!t0.tv_sec) t0 = t;
    return (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9;
}

int pad_read(uint32_t port, XBOX_INPUT_STATE *st)
{
    static int inited;
    if (!inited) { inited = 1; xbox_InputInit(); pad_test_start(); }
    { static int polls; if (++polls == 1 || polls == 600)
        fprintf(stderr, "[PAD] title polled the pad (%d times, t=%.1fs)\n",
                polls, pad_now()); }
    memset(st, 0, sizeof *st);
    if (pad_test_inputs(port, st)) return 1;
    if (xbox_InputGetState(port, st) == 0) return 1;
    return port == 0;   /* port 0 always "present", idle, so the title never waits for a pad */
}

static uint32_t pad_mask(uint32_t type) { return type == XPP_TYPE_GAMEPAD ? 1u : 0u; }
/* Seed a device-type descriptor the way the console has it by the time a
 * title looks: present mask and last-reported mask equal, no change pending.
 * Idempotent -- only the first sight of a descriptor seeds it. */
static uint32_t pad_descriptor_sync(uint8_t *m, uint32_t type)
{
    static uint32_t seeded[4]; static int nseeded;
    uint32_t mask = pad_mask(type);
    int i;

    if (!type) return mask;
    for (i = 0; i < nseeded; i++)
        if (seeded[i] == type) return mask;
    if (nseeded < 4) seeded[nseeded++] = type;
    W32(m, type + XPP_DESC_PRESENT)  = mask;   /* connected now                 */
    W32(m, type + XPP_DESC_PENDING)  = 0;      /* no change pending             */
    W32(m, type + XPP_DESC_REPORTED) = mask;   /* ... and already reported once */
    return mask;
}

/* XGetDevices(type) */
uint32_t pad_get_devices(uint8_t *m, uint32_t type)
{
    uint32_t mask = pad_descriptor_sync(m, type);
    if (type) W32(m, type + XPP_DESC_REPORTED) = W32(m, type + XPP_DESC_PRESENT);
    return mask;
}

/* XGetDeviceChanges(type, &ins, &rem)
 *
 * Reports what has been plugged in or unplugged since the last call, and
 * returns non-zero when anything has. JSRF's device enumeration
 * (sub_001669A0) treats that non-zero as E_FAIL and gives up -- it wants a
 * settled device list, and on a console it gets one, because the pad was
 * enumerated during XInitDevices long before the title asks.
 *
 * So the pad is modelled as already connected at boot rather than arriving
 * during the first poll: the descriptor's "currently present" and "last
 * reported" masks are seeded together, and this reports no change. Reporting
 * the insertion here instead cost the title its whole input, font and UI
 * init, and the first visible symptom was a 3 GB allocation four subsystems
 * away.
 */
uint32_t pad_get_device_changes(uint8_t *m, uint32_t type, uint32_t pins, uint32_t prem)
{
    uint32_t now = 0, last = 0, ins, rem;

    pad_descriptor_sync(m, type);
    if (type) { now = W32(m, type + XPP_DESC_PRESENT); last = W32(m, type + XPP_DESC_REPORTED); }
    ins = now & ~last;
    rem = ~now & last;
    if (pins) W32(m, pins) = ins;
    if (prem) W32(m, prem) = rem;
    if (type) { W32(m, type + XPP_DESC_PENDING) = 0; W32(m, type + XPP_DESC_REPORTED) = now; }
    return (ins | rem) ? 1 : 0;
}

/* XInputOpen(type, port, slot, polling) */
uint32_t pad_open(uint32_t type, uint32_t port)
{
    uint32_t h = (pad_mask(type) >> port) & 1 ? (XPP_HANDLE_BASE | port) : 0;
    if (getenv("JSRF_DBG")) fprintf(stderr, "[PAD] XInputOpen(type=%08X port=%u) = %08X\n", type, port, h);
    return h;
}

/* XInputGetCapabilities(h, caps) */
uint32_t pad_get_capabilities(uint8_t *m, uint32_t h, uint32_t caps)
{
    if ((h & XPP_HANDLE_HIGH) != XPP_HANDLE_BASE || !caps) return XPP_ERROR_INVALID_PARAMETER;
    memset(m + caps, 0, XPP_CAPS_SIZE);
    m[caps + XPP_CAPS_SUBTYPE] = 1;                            /* XINPUT_DEVSUBTYPE_GC_GAMEPAD */
    W16(m, caps + XPP_CAPS_BUTTONS) = 0x00FF;                  /* In.wButtons: all digital */
    memset(m + caps + XPP_CAPS_ANALOG, 0xFF, 8);               /* In.bAnalogButtons */
    for (int k = 0; k < 4; k++) *(int16_t *)(m + caps + XPP_CAPS_THUMBS + 2 * k) = 0x7FFF;
    W16(m, caps + XPP_CAPS_RUMBLE) = 0xFFFF; W16(m, caps + XPP_CAPS_RUMBLE + 2) = 0xFFFF; /* rumble */
    return XPP_ERROR_SUCCESS;
}

/* XInputGetState(h, state)
 *
 * The state the game reads is decided here: the host's pad (pad_read), with
 * the keys' moves, is recorded, or the recording's takes its place
 * (port/playback.c); then the free camera takes its stick from it, so that a
 * recording has the free camera's stick and plays it back. */
uint32_t pad_get_state(uint8_t *m, uint32_t h, uint32_t st)
{
    XBOX_INPUT_STATE s;
    static uint16_t last_btn; static uint8_t last_a;
    uint32_t port = h & XPP_HANDLE_PORT;
    int connected;
    if ((h & XPP_HANDLE_HIGH) != XPP_HANDLE_BASE || !st) return XPP_ERROR_INVALID_PARAMETER;
    connected = pad_read(port, &s);
    if (connected) kbmove_pad(port, &s.Gamepad.sThumbLX, &s.Gamepad.sThumbLY);  /* the keys', while they steer the skater */
    connected = playback_pad(port, &s, connected);
    if (!connected) {
        freecam_pad(port, NULL, NULL);
        return XPP_ERROR_DEVICE_NOT_CONNECTED;
    }
    freecam_pad(port, &s.Gamepad.sThumbRX, &s.Gamepad.sThumbRY);   /* the free camera's, while it steers */
    photo_mode_pad(&s);                     /* none of it, while photo mode holds the world */
    W32(m, st + XPP_STATE_PACKET) = s.dwPacketNumber;
    W16(m, st + XPP_STATE_BUTTONS) = s.Gamepad.wButtons;
    memcpy(m + st + XPP_STATE_ANALOG, s.Gamepad.bAnalogButtons, 8);
    *(int16_t *)(m + st + XPP_STATE_LX) = s.Gamepad.sThumbLX; *(int16_t *)(m + st + XPP_STATE_LY) = s.Gamepad.sThumbLY;
    *(int16_t *)(m + st + XPP_STATE_RX) = s.Gamepad.sThumbRX; *(int16_t *)(m + st + XPP_STATE_RY) = s.Gamepad.sThumbRY;
    if (getenv("JSRF_DBG") && (s.Gamepad.wButtons != last_btn || s.Gamepad.bAnalogButtons[0] != last_a)) {
        last_btn = s.Gamepad.wButtons; last_a = s.Gamepad.bAnalogButtons[0];
        fprintf(stderr, "[PAD] state buttons=%04X A=%u B=%u X=%u Y=%u\n", last_btn, last_a,
                s.Gamepad.bAnalogButtons[1], s.Gamepad.bAnalogButtons[2], s.Gamepad.bAnalogButtons[3]);
    }
    return XPP_ERROR_SUCCESS;
}

/* XInputSetState(h, feedback)
 *
 * The feedback block is a 0x42-byte header, then the two motors' speeds, a
 * word each. The XDK's own XInputSetState (the code at this address) writes
 * the pad's USB report header, its number and length (00 06), into the
 * header's last two bytes and sends the six bytes from there: the report the
 * pad takes, 00 06 then the left motor then the right. So the speeds are at
 * +0x42 and +0x44; +0x40 is the report header, which nothing but the XDK
 * writes (JSRF zeroes the block as it sets its pad up, at 0x00167DF5). Read at
 * +0x40 and +0x42, the pad's big left motor never ran and its small right
 * one played the game's left strength. JSRF's pad update (sub_00167180) sets
 * both speeds at +0x42 in one write and calls this only when they change:
 * the input layer holds them until then (xbox_InputSetState). */
uint32_t pad_set_state(uint8_t *m, uint32_t h, uint32_t fb)
{
    if (fb) {
        uint32_t ev = W32(m, fb + XPP_FEEDBACK_EVENT);
        W32(m, fb + XPP_FEEDBACK_STATUS) = XPP_ERROR_SUCCESS;
        XBOX_VIBRATION v = { W16(m, fb + XPP_FEEDBACK_LEFT), W16(m, fb + XPP_FEEDBACK_RIGHT) };
        xbox_InputSetState(h & XPP_HANDLE_PORT, &v);
        if (ev) { static int once; if (!once++) fprintf(stderr, "[PAD] XInputSetState with hEvent=%08X (not signalled)\n", ev); }
    }
    return XPP_ERROR_SUCCESS;
}
