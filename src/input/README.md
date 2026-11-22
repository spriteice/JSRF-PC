# xbox_input — the Xbox gamepad, from the host's pads

> **From the xboxrecomp toolkit,** by Ned Heller (sp00nznet), written for
> the toolkit's own work, much of it on Burnout 3: Takedown, on Windows with
> Direct3D 11. JSRF-PC keeps it for how the gamepad layer works. Where it
> differs from how JSRF-PC is built or changed,
> [building-on-linux.md](../../docs/building-on-linux.md) and
> [project-rules.md](../../docs/project-rules.md) are right, and this doc is
> out of date there: say so. The generated code is never edited, and a bug
> is fixed where it starts, not stubbed out or guarded around.

Maps the Xbox controller API to the host's pads, through SDL2's GameController API on Linux and Windows. The original Xbox used `XInputGetState` with a slightly different structure layout than the XInput API on Windows; this layer keeps the Xbox's layout and fills it from SDL2, with the window's keyboard and mouse merged in, each through its layout: which of the host's buttons, or keys and mouse buttons, press each of the Xbox pad's controls (`input_layout.h`). A player sets the layouts in the in-game menu, Input > Controller layout and Input > Keyboard layout, and `remap.cfg`, beside the settings file, keeps them (`input_remap.h`). A pad's sticks go to the game stretched onto the Xbox controller's square, as Input > Behaviour > Stick shape has them by default (Square), or as SDL reads them, with Round (`stick_shape.h`), before the keyboard's keys go over them and before input playback records them. src/usb's controller (`usb_gamepad.c`) reports that state to the game's own USB driver. While the in-game menu is open it has the pad, and the game sees nothing pressed. With `RECOMP_INPUT_LATENCY`, each press on the first pad or the keyboard that the game reads is handed to the window, which follows it to the screen (`src/nv2a_gl/nv2a_latency.h`).

## Files

| File | Purpose |
|------|---------|
| `xinput_xbox.h` | Public header — types, button constants, function prototypes |
| `xinput_device.c` | Implementation: SDL2's GameController and the window's keyboard, as the Xbox gamepad's state |
| `input_layout.c`, `input_layout.h` | The layouts, the host's pads' and the keyboard's (its keys and the mouse's buttons): which inputs press each of the Xbox pad's controls, and the port's own (the free camera's switch), their defaults, a row's bind (a swap: a control's whole, or one of its two places), and the state they give (no SDL in it, for tests/input_layout) |
| `input_remap.c`, `input_remap.h` | The layouts in use: read from `remap.cfg`, beside the settings file, at start, copied to the game's thread under a lock, and the in-game menu's two pages that change them, each change kept and logged; and a press of a port's own control on a pad, from the window's events, run on the window's thread (no SDL in it, for tests/input_layout; the inputs' names are `xinput_device.c`'s) |
| `input_prompts.c`, `input_prompts.h` | The button prompts: a pad's kind from what SDL reports of it (`xinput_device.c` reads it), the style a player chose in Input > Button prompts (`button_prompts=` in the settings file; Default the game's own, which nothing changes), the art in use from it and, in Auto alone, the last thing pressed (the window tells it, `nv2a_gl.c`), and what each of the game's icons shows from the layouts; the renderer draws them (`src/nv2a_gl/nv2a_prompts.h`), and the menu's strip takes the style alone, Default's as Auto's (no SDL in it, for tests/input_prompts) |
| `prompt_art.c`, `prompt_art.h`, `prompts/` | The button prompts' pictures: Juliocacko's Free Input Prompts and Xelu's Free Controller & Key Prompts, both CC0, the Xbox and PlayStation ones shaded (`prompts/README.md`, with the script that makes them, `prompts/make.py`), built into the engine from `prompts/`'s PNGs (`prompts/embed.cmake`, run by CMakeLists.txt) and decoded with miniz (`third_party/miniz`), which this library builds; each set's picture of a pad's input or a key (no SDL or OpenGL in it, for tests/prompts) |
| `rumble_hold.c`, `rumble_hold.h` | When to send the vibration the game set to the host's pad, so it holds as the console's does (no SDL in it, for tests/rumble) |
| `stick_shape.c`, `stick_shape.h` | The sticks' shape, Input > Behaviour > Stick shape (`stick_shape=` in the settings file): Square, the default, each stick's circle stretched onto the square, its direction kept and a full push full on both axes at any angle, or Round, the pad's sticks as they come; and its row, the last in Behaviour (no SDL in it, for tests/stick_shape) |
| `input_press.c`, `input_press.h` | What the input latency log (`RECOMP_INPUT_LATENCY`) counts as a press on the first pad or the keyboard: a control held at a read that wasn't at the one before (no SDL in it, for tests/input_latency) |

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

// Set vibration motors: held at these speeds until set again, as the
// console's pad holds them (sent again before SDL's time for them runs out,
// and to a pad found later)
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

The pad's layout as it starts (`input_layout_pad_default`), which Input > Controller layout changes. The sticks are the pad's as they are, or stretched onto the square with Input > Behaviour > Stick shape (`stick_shape.h`); only the keyboard's layout pushes a stick.

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
| Back | none | The pad's Back is the in-game menu's button whenever there is a menu, so the game never sees it then, and no layout has it; with no menu, it is the game's Back. The keyboard's Back (Esc) is the game's: F1 opens the menu. Controller layout can give the game's Back a button |
| D-pad | D-pad | Digital only |
| L Stick | L Stick | Click = L3 |
| R Stick | R Stick | Click: none; R3 is the port's own, the free camera's switch (Toggle free camera: `port/freecam.c`), which the game never sees |

## Ports

```c
#define XBOX_MAX_CONTROLLERS  4   // Ports 0-3
```

The Xbox supports 4 controllers. Each port can have a controller with optional memory units and other accessories. This layer only handles the gamepad; memory unit emulation is not needed for recompiled games.
