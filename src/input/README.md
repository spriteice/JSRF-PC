# xbox_input — the Xbox gamepad, from the host's pads

> **From the xboxrecomp toolkit,** by Ned Heller (sp00nznet), written for
> the toolkit's own work, much of it on Burnout 3: Takedown, on Windows with
> Direct3D 11. JSRF-PC keeps it for how the gamepad layer works. Where it
> differs from how JSRF-PC is built or changed,
> [building-on-linux.md](../../docs/building-on-linux.md) and
> [project-rules.md](../../docs/project-rules.md) are right, and this doc is
> out of date there: say so. The generated code is never edited, and a bug
> is fixed where it starts, not stubbed out or guarded around.

Maps the Xbox controller API to the host's pads, through SDL2's GameController API on Linux and Windows. The original Xbox used `XInputGetState` with a slightly different structure layout than the XInput API on Windows; this layer keeps the Xbox's layout and fills it from SDL2, with the window's keyboard merged in. src/usb's controller (`usb_gamepad.c`) reports that state to the game's own USB driver. While the in-game menu is open it has the pad, and the game sees nothing pressed.

## Files

| File | Purpose |
|------|---------|
| `xinput_xbox.h` | Public header — types, button constants, function prototypes |
| `xinput_device.c` | Implementation: SDL2's GameController and the window's keyboard, as the Xbox gamepad's state |

## Quick Start

```c
#include "xinput_xbox.h"

// Initialize input system
xbox_InputInit();

// Poll controller state (port 0-3)
XBOX_INPUT_STATE state;
if (xbox_InputGetState(0, &state) == 0) {
    // Digital buttons
    if (state.Gamepad.wButtons & XBOX_GAMEPAD_START)
        pause_game();

    // Analog buttons (0-255 pressure)
    uint8_t trigger_r = state.Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER];
    if (trigger_r > XBOX_ANALOG_BUTTON_THRESHOLD)
        accelerate(trigger_r / 255.0f);

    // Stick axes (-32768 to +32767)
    float steer = state.Gamepad.sThumbLX / 32768.0f;
}

// Vibration feedback
XBOX_VIBRATION vib = { .wLeftMotorSpeed = 32000, .wRightMotorSpeed = 16000 };
xbox_InputSetState(0, &vib);
```

## API

```c
// Initialize (call once at startup)
void xbox_InputInit(void);

// Poll controller state (returns 0 on success, non-zero if disconnected)
DWORD xbox_InputGetState(DWORD dwPort, XBOX_INPUT_STATE *pState);

// Set vibration motors
DWORD xbox_InputSetState(DWORD dwPort, const XBOX_VIBRATION *pVibration);

// Check if controller is connected
BOOL xbox_InputIsConnected(DWORD dwPort);

// Query controller capabilities
DWORD xbox_InputGetCapabilities(DWORD dwPort, DWORD dwFlags, XBOX_INPUT_CAPABILITIES *pCaps);
```

## Types

```c
typedef struct {
    WORD  wButtons;                     // Digital button bitmask
    BYTE  bAnalogButtons[8];            // Analog button pressure (0-255)
    SHORT sThumbLX, sThumbLY;           // Left stick (-32768 to +32767)
    SHORT sThumbRX, sThumbRY;           // Right stick
} XBOX_GAMEPAD;

typedef struct {
    DWORD dwPacketNumber;               // Increments on state change
    XBOX_GAMEPAD Gamepad;
} XBOX_INPUT_STATE;

typedef struct {
    WORD wLeftMotorSpeed;               // 0-65535
    WORD wRightMotorSpeed;              // 0-65535
} XBOX_VIBRATION;
```

## Button Constants

### Digital Buttons (wButtons bitmask)

```c
XBOX_GAMEPAD_DPAD_UP         0x0001
XBOX_GAMEPAD_DPAD_DOWN       0x0002
XBOX_GAMEPAD_DPAD_LEFT       0x0004
XBOX_GAMEPAD_DPAD_RIGHT      0x0008
XBOX_GAMEPAD_START            0x0010
XBOX_GAMEPAD_BACK             0x0020
XBOX_GAMEPAD_LEFT_THUMB       0x0040    // Left stick click
XBOX_GAMEPAD_RIGHT_THUMB      0x0080    // Right stick click
```

### Analog Buttons (bAnalogButtons[] indices)

The original Xbox had pressure-sensitive face buttons (0-255):

```c
XBOX_BUTTON_A          0    // Also used for "boost" in racing games
XBOX_BUTTON_B          1
XBOX_BUTTON_X          2
XBOX_BUTTON_Y          3
XBOX_BUTTON_BLACK      4    // No equivalent on modern controllers
XBOX_BUTTON_WHITE      5    // No equivalent on modern controllers
XBOX_BUTTON_LTRIGGER   6    // Left trigger
XBOX_BUTTON_RTRIGGER   7    // Right trigger

XBOX_ANALOG_BUTTON_THRESHOLD  30   // Recommended press threshold
```

### Xbox → Modern Controller Mapping

| Xbox Button | Host Pad (SDL2) | Notes |
|-------------|------------------|-------|
| A | A | Green button; 0 or 255, as the host's has no pressure |
| B | B | Red button; 0 or 255 |
| X | X | Blue button; 0 or 255 |
| Y | Y | Yellow button; 0 or 255 |
| Black | Left Bumper | Mapped to LB; 0 or 255 |
| White | Right Bumper | Mapped to RB; 0 or 255 |
| L Trigger | Left Trigger | Analog 0-255 |
| R Trigger | Right Trigger | Analog 0-255 |
| Start | Start/Menu | |
| Back | Back/View | The in-game menu's button whenever there is a menu, so the game never sees a pad's Back then. The keyboard's Back (Esc) is the game's: F1 opens the menu |
| D-pad | D-pad | Digital only |
| L Stick | L Stick | Click = L3 |
| R Stick | R Stick | Click = R3 |

## Ports

```c
#define XBOX_MAX_CONTROLLERS  4   // Ports 0-3
```

The Xbox supports 4 controllers. Each port can have a controller with optional memory units and other accessories. This layer only handles the gamepad; memory unit emulation is not needed for recompiled games.
