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

/* The layouts (input_layout.h): the host's pads', and the keyboard's. Put
 * at their defaults before the first read. */
static InputLayout g_layout_pad, g_layout_keys;

static void layouts_start(void)
{
    static int done;
    if (done) return;
    done = 1;
    input_layout_pad_default(&g_layout_pad);
    input_layout_keys_default(&g_layout_keys);
}

/* The window's keys held, as the layout takes them: the window's twelve
 * bits and eight analog keys (nv_window_keys), a bit each. Only available in
 * a windowed run -- there is no keyboard to read otherwise -- so the symbol
 * is weak (the SDL window's, nv2a_gl.c). Returns what nv_window_keys does,
 * 1 when a key is held, and 0 without a window or on another port. */
static int kb_held(DWORD dwPort, uint32_t held[INPUT_HELD_WORDS],
                   unsigned short *kb, unsigned char ka[8])
{
    extern int nv_window_keys(unsigned short *b, unsigned char *a)
        __attribute__((weak));
    int i;
    *kb = 0;
    memset(ka, 0, 8);
    memset(held, 0, INPUT_HELD_WORDS * sizeof held[0]);
    if (!(nv_window_keys && dwPort == 0 && nv_window_keys(kb, ka))) return 0;
    for (i = 0; i < INPUT_PRESS_KEY_BITS; i++)
        if (*kb & (1u << i)) held[i / 32] |= 1u << (i % 32);
    for (i = 0; i < 8; i++)
        if (ka[i]) held[(INPUT_PRESS_KEY_BITS + i) / 32] |= 1u << ((INPUT_PRESS_KEY_BITS + i) % 32);
    return 1;
}

/* The window's keyboard, merged in rather than instead.
 *
 * Someone with a pad in their hands and someone using the keyboard should
 * both work, and a run with neither should behave exactly as before. So
 * the keys go over whatever the controller reported, through the keyboard's
 * layout: nothing held changes nothing, and a stick's key held replaces the
 * pad's stick (input_layout_keys). Returns 1 if a key was held, and *held
 * the keys held as the latency log counts them (input_press.h). */
static int kb_merge(DWORD dwPort, WORD *out, unsigned char analog[8], short axes[4],
                    uint32_t *held)
{
    uint32_t keys[INPUT_HELD_WORDS];
    unsigned short kb;
    unsigned char ka[8];
    *held = 0;
    if (!kb_held(dwPort, keys, &kb, ka)) return 0;
    *held = input_press_keys(kb, ka);
    input_layout_keys(&g_layout_keys, keys, out, analog, axes);
    return 1;
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

static SDL_GameController *g_pads[XBOX_MAX_CONTROLLERS];
static BOOL  g_controller_connected[XBOX_MAX_CONTROLLERS];
static DWORD g_packet[XBOX_MAX_CONTROLLERS];
static RumbleHold g_rumble[XBOX_MAX_CONTROLLERS];   /* each port's motors (rumble_hold.h) */
static void rumble_send(DWORD port);
static void latency_presses(DWORD port, uint32_t pad, uint32_t keys, DWORD pass);

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
                    fprintf(stderr, "[PAD] %s in slot %d\n",
                            SDL_GameControllerName(g_pads[slot])
                                ? SDL_GameControllerName(g_pads[slot]) : "controller", slot);
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

void xbox_InputInit(void)
{
    layouts_start();
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
    uint32_t pad_held = 0, keys_held = 0;   /* for the latency log */

    if (pState && input_script(dwPort, pState)) return ERROR_SUCCESS;
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pState)
        return ERROR_DEVICE_NOT_CONNECTED;
    layouts_start();

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
             * the pad's layout (input_layout.h). The pad's Back is the
             * menu's whenever there is one: the game never sees it. */
            for (b = 0; b < INPUT_PAD_BUTTONS; b++)
                value[b] = SDL_GameControllerGetButton(c, (SDL_GameControllerButton)b) ? 255 : 0;
            if (!pad_back_is_games()) value[INPUT_PAD_BACK] = 0;
            /* SDL trigger axes are 0..32767 -> Xbox analog button 0..255 */
            value[INPUT_PAD_LTRIGGER] = (unsigned char)(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
            value[INPUT_PAD_RTRIGGER] = (unsigned char)(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
            input_layout_pad(&g_layout_pad, value, &out, analog);
            /* SDL Y axes point down; the Xbox's up -- invert, as (-1 - v) so
             * v = -32768 does not overflow. */
            axes[0] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
            axes[1] = (short)(-1 - SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
            axes[2] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX);
            axes[3] = (short)(-1 - SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
            pad_held = input_press_pad(out, analog, axes);
        }
    }

    /* The window's keyboard (kb_merge, above). */
    if (kb_merge(dwPort, &out, analog, axes, &keys_held)) have_pad = 1;
    menu_gate(dwPort, &out, analog, axes);

    if (!have_pad) {
        g_controller_connected[dwPort] = FALSE;
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    if (!g_controller_connected[dwPort])
        fprintf(stderr, "[PAD] controller %u connected\n", (unsigned)dwPort);
    g_controller_connected[dwPort] = TRUE;
    pState->dwPacketNumber = ++g_packet[dwPort];
    latency_presses(dwPort, pad_held, keys_held, pState->dwPacketNumber);
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
    unsigned short kb;
    unsigned char ka[8];
    *x = *y = 0;
    if (nv_window_menu_open && nv_window_menu_open() > 0) return 0;
    layouts_start();
    kb_held(dwPort, keys, &kb, ka);
    return input_layout_keys_move(&g_layout_keys, keys, x, y);
}

/* The input latency log (RECOMP_INPUT_LATENCY): each press on the first
 * pad or the keyboard (input_press.h) that the game reads while the in-game
 * menu is shut, handed to the window, which follows it to the screen
 * (src/nv2a_gl/nv2a_latency.h) and drops it when the log is off. The
 * window's, so weak, as nv_window_keys is. pass: the read's packet number. */
static void latency_presses(DWORD port, uint32_t pad, uint32_t keys, DWORD pass)
{
    extern void nv_window_latency_press(const char *what, int key, unsigned pass)
        __attribute__((weak));
    static uint32_t pad_was, keys_was;
    uint32_t p = pad & ~pad_was, k = keys & ~keys_was;
    int i;
    if (port != 0 || !nv_window_latency_press) return;
    pad_was = pad;
    keys_was = keys;
    if ((!p && !k) || (nv_window_menu_open && nv_window_menu_open() > 0)) return;
    for (i = 0; i < INPUT_PRESS_PAD; i++)
        if (p & (1u << i)) nv_window_latency_press(input_press_pad_name(i), -1, (unsigned)pass);
    for (i = 0; i < INPUT_PRESS_KEYS; i++)
        if (k & (1u << i)) nv_window_latency_press(NULL, i, (unsigned)pass);
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
