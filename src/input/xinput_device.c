/**
 * Xbox Input Compatibility Layer
 *
 * Translates the Xbox controller API to the host's pads, through SDL2's
 * GameController API on Linux and Windows. Handles the structural
 * differences between the Xbox gamepad (analog buttons as bytes, separate
 * trigger channels) and the host (digital face buttons, trigger axes).
 */

#include "xinput_xbox.h"
#include "input_press.h"
#include "input_layout.h"
#include "input_remap.h"
#include "input_prompts.h"
#include <string.h>
#include <stdio.h>


/* ---- scripted input ------------------------------------------------------
 *
 * A way to press buttons without hands.
 *
 * "Does it get past the title screen" cannot be answered by a headless run,
 * and answering it by hand means a person watching a window for two minutes
 * every time something changes. RECOMP_AUTO_INPUT is a list of
 * <seconds>:<button> pairs -- "12:start,16:a,20:a" -- each held for half a
 * second at that point after the process starts.
 *
 * It overrides the real pad only while a press is due, so a controller in
 * someone's hands still works alongside it.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

typedef struct { double at; WORD buttons; BYTE analog_index; } ScriptedPress;

static double input_now(void)
{
    static double t0 = -1.0;
    struct timespec ts;
    double now;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    if (t0 < 0.0) t0 = now;
    return now - t0;
}

/* Returns 1 if it filled pState, 0 to let the real pad answer. */
static int input_script(DWORD port, XBOX_INPUT_STATE *pState)
{
    #define SCRIPT_MAX 16
    static ScriptedPress press[SCRIPT_MAX];
    static int n = -1;
    static const double hold = 0.5;
    double t;
    int i;

    if (n < 0) {
        const char *spec = getenv("RECOMP_AUTO_INPUT");
        n = 0;
        if (spec) {
            char buf[512], *p, *save = NULL;
            snprintf(buf, sizeof buf, "%s", spec);
            for (p = strtok_r(buf, ",", &save); p && n < SCRIPT_MAX;
                 p = strtok_r(NULL, ",", &save)) {
                char name[32] = "";
                double at = 0.0;
                if (sscanf(p, "%lf:%31s", &at, name) != 2) continue;
                press[n].at = at;
                press[n].buttons = 0;
                press[n].analog_index = 0xFF;
                if      (!strcmp(name, "start")) press[n].buttons = XBOX_GAMEPAD_START;
                else if (!strcmp(name, "back"))  press[n].buttons = XBOX_GAMEPAD_BACK;
                else if (!strcmp(name, "up"))    press[n].buttons = XBOX_GAMEPAD_DPAD_UP;
                else if (!strcmp(name, "down"))  press[n].buttons = XBOX_GAMEPAD_DPAD_DOWN;
                else if (!strcmp(name, "left"))  press[n].buttons = XBOX_GAMEPAD_DPAD_LEFT;
                else if (!strcmp(name, "right")) press[n].buttons = XBOX_GAMEPAD_DPAD_RIGHT;
                else if (!strcmp(name, "a"))     press[n].analog_index = XBOX_BUTTON_A;
                else if (!strcmp(name, "b"))     press[n].analog_index = XBOX_BUTTON_B;
                else continue;
                n++;
            }
            fprintf(stderr, "[INPUT] scripted: %d press(es) from \"%s\"\n",
                    n, spec);
        }
    }
    if (!n || port != 0) return 0;

    t = input_now();
    for (i = 0; i < n; i++) {
        if (t < press[i].at || t > press[i].at + hold) continue;
        {
            static int said[SCRIPT_MAX];
            if (!said[i]) { said[i] = 1;
                fprintf(stderr, "[INPUT] scripted press %d at %.1fs\n", i, t); }
        }
        memset(pState, 0, sizeof *pState);
        pState->dwPacketNumber = (DWORD)(t * 1000.0);
        pState->Gamepad.wButtons = press[i].buttons;
        if (press[i].analog_index != 0xFF)
            pState->Gamepad.bAnalogButtons[press[i].analog_index] = 255;
        return 1;
    }
    return 0;
}

/* The window's keys and mouse buttons held, as the keyboard's layout takes
 * them: a bit for each key by its place, then each mouse button
 * (input_layout.h). Only available in a windowed run -- there is no
 * keyboard to read otherwise -- so the symbol is weak (the SDL window's,
 * nv2a_gl.c). Returns 1 when one is held, and 0 without a window or on
 * another port. */
static int kb_held(DWORD dwPort, uint32_t held[INPUT_HELD_WORDS])
{
    extern int nv_window_keys(uint32_t *held, int words) __attribute__((weak));
    memset(held, 0, INPUT_HELD_WORDS * sizeof held[0]);
    return nv_window_keys && dwPort == 0 && nv_window_keys(held, INPUT_HELD_WORDS);
}

/* The window's keyboard and mouse, merged in rather than instead.
 *
 * Someone with a pad in their hands and someone using the keyboard should
 * both work, and a run with neither should behave exactly as before. So
 * the keys go over whatever the controller reported, through the keyboard's
 * layout: nothing held changes nothing, and a stick's key held replaces the
 * pad's stick (input_layout_keys). Returns 1 if a key the layout uses was
 * held, and held[] the keys and mouse buttons held, for the latency log. */
static int kb_merge(DWORD dwPort, const InputLayout *l, WORD *out, unsigned char analog[8],
                    short axes[4], uint32_t held[INPUT_HELD_WORDS])
{
    if (!kb_held(dwPort, held)) return 0;
    return input_layout_keys(l, held, out, analog, axes);
}

/* RECOMP_HOST_PAD=0 leaves the host's controllers alone entirely, for
 * unattended runs driven by a script. */
static int host_pad_off(void)
{
    static int off = -1;
    if (off < 0) {
        const char *e = getenv("RECOMP_HOST_PAD");
        off = (e && e[0] == '0') ? 1 : 0;
    }
    return off;
}

/* ======================================================================== */
/* ====================  SDL2 GameController backend  ===================== */
/* ======================================================================== */

#include <SDL.h>
#include "rumble_hold.h"
#include "stick_shape.h"

static SDL_GameController *g_pads[XBOX_MAX_CONTROLLERS];
static BOOL  g_controller_connected[XBOX_MAX_CONTROLLERS];
static DWORD g_packet[XBOX_MAX_CONTROLLERS];
static RumbleHold g_rumble[XBOX_MAX_CONTROLLERS];   /* each port's motors (rumble_hold.h) */
static void rumble_send(DWORD port);
static void latency_presses(DWORD port, uint32_t pad, const uint32_t keys[INPUT_HELD_WORDS],
                            const InputLayout *kl, DWORD pass);

/* Open the attached game controllers into free slots, in the order SDL lists
 * them, and let go of any that went away -- so a pad plugged in (or paired)
 * after the game started is picked up. */
static void list_devices(void)
{
    /* What SDL sees, whenever that changes: a pad that is listed but has no
     * controller mapping is not used, and one that is not listed at all was
     * never seen -- two different reports. Names and USB ids only: a device
     * path can carry a Bluetooth address, and logs end up in public issues. */
    static int last = -1;
    int i, n = SDL_NumJoysticks();
    if (n == last) return;
    last = n;
    fprintf(stderr, "[PAD] SDL sees %d input device(s)\n", n < 0 ? 0 : n);
    for (i = 0; i < n; i++) {
        const char *nm = SDL_JoystickNameForIndex(i);
        fprintf(stderr, "[PAD]   %d: %s (%04x:%04x)%s\n", i, nm ? nm : "unnamed",
                SDL_JoystickGetDeviceVendor(i), SDL_JoystickGetDeviceProduct(i),
                SDL_IsGameController(i) ? "" : " -- no controller mapping, not used");
    }
}

/* A pad's kind, for the button prompts (input_prompts.h), from what SDL
 * reports of it. */
_Static_assert((int)PROMPT_SDL_PS3 == (int)SDL_CONTROLLER_TYPE_PS3
            && (int)PROMPT_SDL_PS4 == (int)SDL_CONTROLLER_TYPE_PS4
            && (int)PROMPT_SDL_PS5 == (int)SDL_CONTROLLER_TYPE_PS5
            && (int)PROMPT_SDL_SWITCH_PRO == (int)SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO
            && (int)PROMPT_SDL_JOYCON_LEFT == (int)SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT
            && (int)PROMPT_SDL_JOYCON_RIGHT == (int)SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT
            && (int)PROMPT_SDL_JOYCON_PAIR == (int)SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR,
               "the prompts' kinds of pad are SDL's");
static const char *const k_kind_names[PROMPT_PADS] = {
    [PROMPT_PAD_XBOX] = "Xbox", [PROMPT_PAD_PLAYSTATION] = "PlayStation",
    [PROMPT_PAD_NINTENDO] = "Nintendo",
};

static int pad_kind(SDL_GameController *c)
{
    return input_prompts_kind((int)SDL_GameControllerGetType(c));
}

int xbox_InputPadKind(int instance)
{
    SDL_GameController *c = SDL_GameControllerFromInstanceID((SDL_JoystickID)instance);
    return c ? pad_kind(c) : PROMPT_PAD_XBOX;
}

static void open_controllers(void)
{
    int slot, i;
    list_devices();
    for (slot = 0; slot < XBOX_MAX_CONTROLLERS; slot++)
        if (g_pads[slot] && !SDL_GameControllerGetAttached(g_pads[slot])) {
            fprintf(stderr, "[PAD] controller %d gone\n", slot);
            SDL_GameControllerClose(g_pads[slot]);
            g_pads[slot] = NULL;
        }
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_JoystickID id;
        int have = 0;
        if (!SDL_IsGameController(i)) continue;
        id = SDL_JoystickGetDeviceInstanceID(i);
        for (slot = 0; slot < XBOX_MAX_CONTROLLERS; slot++)
            if (g_pads[slot] && SDL_JoystickInstanceID(
                    SDL_GameControllerGetJoystick(g_pads[slot])) == id) have = 1;
        if (have) continue;
        for (slot = 0; slot < XBOX_MAX_CONTROLLERS; slot++)
            if (!g_pads[slot]) {
                g_pads[slot] = SDL_GameControllerOpen(i);
                if (g_pads[slot])
                    fprintf(stderr, "[PAD] %s in slot %d: SDL's type %d, prompts' kind %s\n",
                            SDL_GameControllerName(g_pads[slot])
                                ? SDL_GameControllerName(g_pads[slot]) : "controller", slot,
                            (int)SDL_GameControllerGetType(g_pads[slot]),
                            k_kind_names[pad_kind(g_pads[slot])]);
                break;
            }
    }
}

/* The layout's pad inputs are SDL's buttons, in its order, then its two
 * triggers (input_layout.h). As ints: they are two enums. */
#define SAME_AS_SDL(in, sdl) ((int)(in) == (int)(sdl))
_Static_assert(SAME_AS_SDL(INPUT_PAD_A, SDL_CONTROLLER_BUTTON_A)
            && SAME_AS_SDL(INPUT_PAD_BACK, SDL_CONTROLLER_BUTTON_BACK)
            && SAME_AS_SDL(INPUT_PAD_START, SDL_CONTROLLER_BUTTON_START)
            && SAME_AS_SDL(INPUT_PAD_LSHOULDER, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            && SAME_AS_SDL(INPUT_PAD_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_UP)
            && SAME_AS_SDL(INPUT_PAD_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
            && SAME_AS_SDL(INPUT_PAD_MISC1, SDL_CONTROLLER_BUTTON_MISC1)
            && SAME_AS_SDL(INPUT_PAD_PADDLE1, SDL_CONTROLLER_BUTTON_PADDLE1)
            && SAME_AS_SDL(INPUT_PAD_TOUCHPAD, SDL_CONTROLLER_BUTTON_TOUCHPAD)
            && SAME_AS_SDL(INPUT_PAD_BUTTONS, SDL_CONTROLLER_BUTTON_MAX),
               "the layout's pad buttons are SDL's, in its order");
/* And its keys SDL's places for them, then the mouse's buttons, as the
 * window holds them (nv2a_gl.c, nv_window_keys) and the menu hands a bind
 * row them (src/menu/menu.h, XBOX_BIND_TAKES). */
_Static_assert(SAME_AS_SDL(INPUT_KEY_A, SDL_SCANCODE_A) && SAME_AS_SDL(INPUT_KEY_C, SDL_SCANCODE_C)
            && SAME_AS_SDL(INPUT_KEY_D, SDL_SCANCODE_D) && SAME_AS_SDL(INPUT_KEY_E, SDL_SCANCODE_E)
            && SAME_AS_SDL(INPUT_KEY_H, SDL_SCANCODE_H)
            && SAME_AS_SDL(INPUT_KEY_J, SDL_SCANCODE_J) && SAME_AS_SDL(INPUT_KEY_K, SDL_SCANCODE_K)
            && SAME_AS_SDL(INPUT_KEY_Q, SDL_SCANCODE_Q)
            && SAME_AS_SDL(INPUT_KEY_R, SDL_SCANCODE_R) && SAME_AS_SDL(INPUT_KEY_S, SDL_SCANCODE_S)
            && SAME_AS_SDL(INPUT_KEY_T, SDL_SCANCODE_T) && SAME_AS_SDL(INPUT_KEY_U, SDL_SCANCODE_U)
            && SAME_AS_SDL(INPUT_KEY_V, SDL_SCANCODE_V) && SAME_AS_SDL(INPUT_KEY_W, SDL_SCANCODE_W)
            && SAME_AS_SDL(INPUT_KEY_RETURN, SDL_SCANCODE_RETURN)
            && SAME_AS_SDL(INPUT_KEY_ESCAPE, SDL_SCANCODE_ESCAPE)
            && SAME_AS_SDL(INPUT_KEY_SPACE, SDL_SCANCODE_SPACE)
            && SAME_AS_SDL(INPUT_KEY_RIGHT, SDL_SCANCODE_RIGHT) && SAME_AS_SDL(INPUT_KEY_LEFT, SDL_SCANCODE_LEFT)
            && SAME_AS_SDL(INPUT_KEY_DOWN, SDL_SCANCODE_DOWN) && SAME_AS_SDL(INPUT_KEY_UP, SDL_SCANCODE_UP)
            && SAME_AS_SDL(INPUT_KEY_KP_ENTER, SDL_SCANCODE_KP_ENTER)
            && SAME_AS_SDL(INPUT_KEY_LCTRL, SDL_SCANCODE_LCTRL)
            && SAME_AS_SDL(INPUT_KEY_LSHIFT, SDL_SCANCODE_LSHIFT)
            && SAME_AS_SDL(INPUT_KEY_RCTRL, SDL_SCANCODE_RCTRL)
            && SAME_AS_SDL(INPUT_KEY_AC_BACK, SDL_SCANCODE_AC_BACK)
            && SAME_AS_SDL(INPUT_KEY_SCANCODES, SDL_NUM_SCANCODES)
            && SAME_AS_SDL(INPUT_KEY_MOUSE_BUTTONS, SDL_BUTTON_X2 - SDL_BUTTON_LEFT + 1)
            && SAME_AS_SDL(INPUT_KEY_MOUSE_MIDDLE - INPUT_KEY_MOUSE_LEFT, SDL_BUTTON_MIDDLE - SDL_BUTTON_LEFT)
            && SAME_AS_SDL(INPUT_KEY_MOUSE_RIGHT - INPUT_KEY_MOUSE_LEFT, SDL_BUTTON_RIGHT - SDL_BUTTON_LEFT),
               "the layout's keys are SDL's places, then the mouse's buttons");

/* The inputs' names (input_remap.h): the pad's as the menu shows them, in
 * SDL's order, then its triggers; and the mouse's buttons', which SDL has
 * none for. A key is SDL's name for it, shown and kept. */
static const char *const k_pad_shown[INPUT_PAD_INPUTS] = {
    "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", "Touchpad", "LT", "RT",
};
static const char *const k_mouse_names[INPUT_KEY_MOUSE_BUTTONS] = {
    "Mouse left", "Mouse middle", "Mouse right", "Mouse back", "Mouse forward",
};

const char *input_remap_name(int layout, int input, int how)
{
    const char *n;
    if (layout == INPUT_REMAP_PAD) {
        if (input < 0 || input >= INPUT_PAD_INPUTS) return NULL;
        if (how == INPUT_NAME_SHOWN) return k_pad_shown[input];
        if (input == INPUT_PAD_LTRIGGER) return SDL_GameControllerGetStringForAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        if (input == INPUT_PAD_RTRIGGER) return SDL_GameControllerGetStringForAxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        return SDL_GameControllerGetStringForButton((SDL_GameControllerButton)input);
    }
    if (input >= INPUT_KEY_MOUSE && input < INPUT_KEYS) return k_mouse_names[input - INPUT_KEY_MOUSE];
    if (input <= SDL_SCANCODE_UNKNOWN || input >= INPUT_KEY_SCANCODES) return NULL;
    n = SDL_GetScancodeName((SDL_Scancode)input);
    return n && *n ? n : NULL;
}

int input_remap_from_name(int layout, const char *name)
{
    int i;
    if (!name || !*name) return INPUT_NONE;
    if (layout == INPUT_REMAP_PAD) {
        SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(name);
        SDL_GameControllerAxis a;
        if (b != SDL_CONTROLLER_BUTTON_INVALID) return (int)b;
        a = SDL_GameControllerGetAxisFromString(name);
        return a == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? INPUT_PAD_LTRIGGER
             : a == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? INPUT_PAD_RTRIGGER : INPUT_NONE;
    }
    for (i = 0; i < INPUT_KEY_MOUSE_BUTTONS; i++)
        if (!SDL_strcasecmp(name, k_mouse_names[i])) return INPUT_KEY_MOUSE + i;
    {
        SDL_Scancode sc = SDL_GetScancodeFromName(name);
        return sc == SDL_SCANCODE_UNKNOWN ? INPUT_NONE : (int)sc;
    }
}

void xbox_InputInit(void)
{
    if (host_pad_off()) return;
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
#if defined(_WIN32)
        /*
         * Windows delivers a pad's input to a window. SDL makes that window
         * on the thread that starts its joystick support -- here the game's
         * own thread, which asks for the pad first -- and reads it only when
         * that thread pumps its messages, which the game's thread never does
         * (the window's events are pumped by the GPU thread). An Xbox pad is
         * claimed by SDL's raw-input driver, so it was found, opened, and
         * never moved: "the controller doesn't work". With this SDL keeps a
         * thread of its own for the window and its messages, which is what
         * the hint is for, and pads plugged in later are found there too.
         */
        SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
#endif
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
            fprintf(stderr, "[PAD] no controller support: %s\n", SDL_GetError());
    }
    open_controllers();
}

/* The in-game menu (src/menu) has the pad while it is open: the game
 * sees one with nothing pressed. What was held as it shut stays unseen
 * until let go, so it is not also a press in the game. */
extern int nv_window_menu_open(void) __attribute__((weak));   /* 1 open, 0 shut, -1 none */
static WORD     g_menu_held[XBOX_MAX_CONTROLLERS];
static unsigned g_menu_held_an[XBOX_MAX_CONTROLLERS];  /* a bit per analog[] */

/* A pad's Back is the menu's own button, whenever there is a menu: the game
 * never sees it, so opening the menu is never also a Back in the game. The
 * keyboard's Back (Esc) is still the game's: F1 is the keyboard's menu key. */
static int pad_back_is_games(void)
{
    return !nv_window_menu_open || nv_window_menu_open() < 0;
}

static void menu_gate(DWORD port, WORD *out, unsigned char analog[8], short axes[4])
{
    int open = nv_window_menu_open && nv_window_menu_open() > 0, i;
    unsigned an = 0;
    for (i = 0; i < 8; i++) if (analog[i]) an |= 1u << i;
    if (open) { g_menu_held[port] = *out;  g_menu_held_an[port] = an; }
    else      { g_menu_held[port] &= *out; g_menu_held_an[port] &= an; }
    *out &= (WORD)~g_menu_held[port];
    for (i = 0; i < 8; i++) if (g_menu_held_an[port] & (1u << i)) analog[i] = 0;
    if (open) memset(axes, 0, 4 * sizeof axes[0]);
}

DWORD xbox_InputGetState(DWORD dwPort, XBOX_INPUT_STATE *pState)
{
    WORD out = 0;
    unsigned char analog[8];
    short axes[4];
    int have_pad = 0;
    uint32_t pad_held = 0, keys_held[INPUT_HELD_WORDS];   /* for the latency log */
    InputLayout pad_layout, keys_layout;

    if (pState && input_script(dwPort, pState)) return ERROR_SUCCESS;
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pState)
        return ERROR_DEVICE_NOT_CONNECTED;
    input_remap_layouts(&pad_layout, &keys_layout);

    memset(pState, 0, sizeof(XBOX_INPUT_STATE));
    memset(analog, 0, sizeof analog);
    memset(axes, 0, sizeof axes);

    if (!host_pad_off()) {
        SDL_GameController *c;
        /* Pads come and go: look again about once a second. */
        { static double next;
          double now = input_now();
          if (dwPort == 0 && now >= next) { next = now + 1.0; open_controllers(); } }
        c = g_pads[dwPort];
        if (c && SDL_GameControllerGetAttached(c)) {
            unsigned char value[INPUT_PAD_INPUTS];
            int b;
            SDL_GameControllerUpdate();
            have_pad = 1;
            rumble_send(dwPort);            /* held: see xbox_InputSetState */
            /* Every button SDL has, 255 while held, and the triggers, then
             * the pad's layout (input_layout.h). The pad's Back is no
             * control's in it: the menu's whenever there is one, when the
             * game never sees it, and otherwise the game's Back, as it
             * always was. */
            for (b = 0; b < INPUT_PAD_BUTTONS; b++)
                value[b] = SDL_GameControllerGetButton(c, (SDL_GameControllerButton)b) ? 255 : 0;
            if (value[INPUT_PAD_BACK] && pad_back_is_games()) out |= XBOX_GAMEPAD_BACK;
            value[INPUT_PAD_BACK] = 0;
            /* SDL trigger axes are 0..32767 -> Xbox analog button 0..255 */
            value[INPUT_PAD_LTRIGGER] = (unsigned char)(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
            value[INPUT_PAD_RTRIGGER] = (unsigned char)(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
            input_layout_pad(&pad_layout, value, &out, analog);
            /* SDL Y axes point down; the Xbox's up -- invert, as (-1 - v) so
             * v = -32768 does not overflow. */
            axes[0] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
            axes[1] = (short)(-1 - SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
            axes[2] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX);
            axes[3] = (short)(-1 - SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
            /* Round or square, as Input > Behaviour > Stick shape has them
             * (stick_shape.h): the pad's sticks only, before the keys go
             * over them. */
            stick_shape_pad(axes);
            pad_held = input_press_pad(out, analog, axes);
        }
    }

    /* The window's keyboard and mouse (kb_merge, above). */
    if (kb_merge(dwPort, &keys_layout, &out, analog, axes, keys_held)) have_pad = 1;
    menu_gate(dwPort, &out, analog, axes);

    if (!have_pad) {
        g_controller_connected[dwPort] = FALSE;
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    if (!g_controller_connected[dwPort])
        fprintf(stderr, "[PAD] controller %u connected\n", (unsigned)dwPort);
    g_controller_connected[dwPort] = TRUE;
    pState->dwPacketNumber = ++g_packet[dwPort];
    latency_presses(dwPort, pad_held, keys_held, &keys_layout, pState->dwPacketNumber);
    pState->Gamepad.wButtons = out;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_A]        = analog[0];
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_B]        = analog[1];
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_X]        = analog[2];
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_Y]        = analog[3];
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_BLACK]    = analog[4];
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_WHITE]    = analog[5];
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER] = analog[6];
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER] = analog[7];
    pState->Gamepad.sThumbLX = axes[0];
    pState->Gamepad.sThumbLY = axes[1];
    pState->Gamepad.sThumbRX = axes[2];
    pState->Gamepad.sThumbRY = axes[3];
    return ERROR_SUCCESS;
}

/* The movement keys alone (xinput_xbox.h): the left stick's keys in the
 * keyboard's layout. */
int xbox_InputKeyboardMove(DWORD dwPort, int *x, int *y)
{
    uint32_t keys[INPUT_HELD_WORDS];
    InputLayout l;
    *x = *y = 0;
    if (nv_window_menu_open && nv_window_menu_open() > 0) return 0;
    input_remap_layouts(NULL, &l);
    kb_held(dwPort, keys);
    return input_layout_keys_move(&l, keys, x, y);
}

/* The input latency log (RECOMP_INPUT_LATENCY): each press on the first
 * pad (input_press.h), or of a key or mouse button the keyboard's layout
 * kl uses, that the game reads while the in-game menu is shut, handed to
 * the window, which follows it to the screen (src/nv2a_gl/nv2a_latency.h)
 * and drops it when the log is off. The window's, so weak, as
 * nv_window_keys is. pass: the read's packet number. */
static void latency_presses(DWORD port, uint32_t pad, const uint32_t keys[INPUT_HELD_WORDS],
                            const InputLayout *kl, DWORD pass)
{
    extern void nv_window_latency_press(const char *what, int key, unsigned pass)
        __attribute__((weak));
    static uint32_t pad_was, keys_was[INPUT_HELD_WORDS];
    uint32_t p = pad & ~pad_was, k[INPUT_HELD_WORDS], used[INPUT_HELD_WORDS], any = 0;
    int i;
    if (port != 0 || !nv_window_latency_press) return;
    input_layout_used(kl, used);
    for (i = 0; i < INPUT_HELD_WORDS; i++) {
        k[i] = keys[i] & ~keys_was[i] & used[i];
        keys_was[i] = keys[i];
        any |= k[i];
    }
    pad_was = pad;
    if ((!p && !any) || (nv_window_menu_open && nv_window_menu_open() > 0)) return;
    for (i = 0; i < INPUT_PRESS_PAD; i++)
        if (p & (1u << i)) nv_window_latency_press(input_press_pad_name(i), -1, (unsigned)pass);
    for (i = 0; i < INPUT_KEYS; i++)
        if ((k[i / 32] >> (i % 32)) & 1u)
            nv_window_latency_press(input_remap_name(INPUT_REMAP_KEYS, i, INPUT_NAME_SHOWN), i, (unsigned)pass);
}

/* Vibration, as the game last set it, sent to the pad in the slot when it is
 * due (rumble_hold.h): when it changes, to a pad that hasn't had it, and
 * again before SDL's time for it runs out. SDL's rumble lasts a set time,
 * and the game sets the motors only when they change, so a vibration sent
 * for a second at each call stopped after a second. */
static void rumble_send(DWORD port)
{
    SDL_GameController *c = g_pads[port];
    if (c && rumble_hold_due(&g_rumble[port],
                             (long)SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)),
                             input_now()))
        SDL_GameControllerRumble(c, g_rumble[port].left, g_rumble[port].right, RUMBLE_HOLD_MS);
}

DWORD xbox_InputSetState(DWORD dwPort, const XBOX_VIBRATION *pVibration)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pVibration)
        return ERROR_DEVICE_NOT_CONNECTED;
    /* The first changes, so a log shows the game asking: enough for a few
     * trips to the game's options screen, whose Vibration on buzzes four
     * changes each time, and the hurts after them. */
    if (pVibration->wLeftMotorSpeed != g_rumble[dwPort].left
     || pVibration->wRightMotorSpeed != g_rumble[dwPort].right) {
        static int said;
        if (said < 64) {
            said++;
            fprintf(stderr, "[PAD] vibration %u: left %u, right %u%s\n", (unsigned)dwPort,
                    pVibration->wLeftMotorSpeed, pVibration->wRightMotorSpeed,
                    said == 64 ? " (no more of these)" : "");
        }
    }
    rumble_hold_set(&g_rumble[dwPort], pVibration->wLeftMotorSpeed, pVibration->wRightMotorSpeed);
    rumble_send(dwPort);
    if (!g_pads[dwPort]) return g_controller_connected[dwPort] ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
    return ERROR_SUCCESS;
}

BOOL xbox_InputIsConnected(DWORD dwPort)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS) return FALSE;
    return g_controller_connected[dwPort];
}

DWORD xbox_InputGetCapabilities(DWORD dwPort, DWORD dwFlags, XBOX_INPUT_CAPABILITIES *pCaps)
{
    (void)dwFlags;
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pCaps)
        return ERROR_DEVICE_NOT_CONNECTED;
    if (!g_pads[dwPort] && !g_controller_connected[dwPort])
        return ERROR_DEVICE_NOT_CONNECTED;

    memset(pCaps, 0, sizeof(XBOX_INPUT_CAPABILITIES));
    pCaps->Type    = 1;   /* XINPUT_DEVTYPE_GAMEPAD */
    pCaps->SubType = 1;   /* XINPUT_DEVSUBTYPE_GAMEPAD */
    pCaps->Flags   = 0;
    return ERROR_SUCCESS;
}
