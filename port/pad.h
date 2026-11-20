/* The pad: the game's pad API (the XDK's XPP section), answered from the
 * host's pads.
 *
 * The XDK's pad API sits on a USB stack that talks to the OHCI controllers
 * through MMIO; nothing answers those registers here, so XGetDevices never
 * reported a pad and the title sat on its attract loop for ever. The eight
 * entry points the game calls are replaced in recomp_manual.c, the file the
 * pipeline reads for what is written by hand (tools/recomp/manual_scan.py),
 * and each calls this file with the guest memory and its arguments. On the
 * host the pad is whatever xbox_input has (SDL2's controllers, with the
 * keyboard merged over the first, src/input/xinput_device.c).
 *
 *   0x001BD5FF  XGetDevices(type)                        pad_get_devices
 *   0x001BD621  XGetDeviceChanges(type, &ins, &rem)      pad_get_device_changes
 *   0x001C3BA1  XInputOpen(type, port, slot, polling)    pad_open
 *   0x001C3C16  XInputClose(h)                           (nothing to do)
 *   0x001C3C22  XInputGetCapabilities(h, caps)           pad_get_capabilities
 *   0x001C3E14  XInputGetState(h, state)                 pad_get_state
 *   0x001C3E85  XInputSetState(h, feedback)              pad_set_state
 *   0x001C3EBD  XInputPoll(h)                            (nothing to do)
 *
 * Each returns what the game's function returns in eax. The test hooks that
 * press buttons in unattended runs (JSRF_PAD_SCRIPT, the autopilot) stay in
 * recomp_manual.c, beside the camera hook whose state they share, and come
 * in through pad_test_start and pad_test_inputs. Input playback
 * (port/playback.c) records what XInputGetState hands the game, or hands it
 * a recording's. */
#ifndef JSRF_PAD_H
#define JSRF_PAD_H

#include <stdint.h>
#include "xinput_xbox.h"

/* The device type the game asks for (its XDEVICE_TYPE_GAMEPAD), and the
 * handles XInputOpen hands out: this base with the port in the low bits. */
#define XPP_TYPE_GAMEPAD 0x001BC860u
#define XPP_HANDLE_BASE  0x58580000u

/* Seconds since the first call: the clock the pad's log and the test hooks
 * in recomp_manual.c share. */
double pad_now(void);

/* The host's pad state for a port, as the game will read it: the test
 * hooks' presses when they drive the pad, else the host's pad. 1 when the
 * port has a pad; port 0 always has one, idle when nothing is attached, so
 * the title never waits for a pad. */
int pad_read(uint32_t port, XBOX_INPUT_STATE *st);

uint32_t pad_get_devices(uint8_t *mem, uint32_t type);
uint32_t pad_get_device_changes(uint8_t *mem, uint32_t type, uint32_t ins, uint32_t rem);
uint32_t pad_open(uint32_t type, uint32_t port);
uint32_t pad_get_capabilities(uint8_t *mem, uint32_t h, uint32_t caps);
uint32_t pad_get_state(uint8_t *mem, uint32_t h, uint32_t state);
uint32_t pad_set_state(uint8_t *mem, uint32_t h, uint32_t feedback);

/* Defined by recomp_manual.c: the test hooks' part of the pad. At the first
 * read, pad_test_start loads their scripts; at every read, pad_test_inputs
 * fills *st and returns 1 while they drive the pad on that port, else 0. */
void pad_test_start(void);
int pad_test_inputs(uint32_t port, XBOX_INPUT_STATE *st);

#endif
