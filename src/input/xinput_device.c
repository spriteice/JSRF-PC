/**
 * Xbox Input Compatibility Layer
 *
 * Translates the Xbox controller API to the host's pads, through SDL2's
 * GameController API on Linux and Windows. Handles the structural
 * differences between the Xbox gamepad (analog buttons as bytes, separate
 * trigger channels) and the host (digital face buttons, trigger axes).
 */

#include "xinput_xbox.h"
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

/* The window's keyboard, merged in rather than instead.
 *
 * Someone with a pad in their hands and someone using the keyboard should
 * both work, and a run with neither should behave exactly as before. So
 * the keys are OR-ed over whatever the controller reported: nothing held
 * changes nothing. Only available in a windowed run -- there is no keyboard
 * to read otherwise -- so the symbol is weak (the SDL window's, nv2a_gl.c).
 * Returns 1 if a key was held. */
static int kb_merge(DWORD dwPort, WORD *out, unsigned char analog[8], short axes[4])
{
    extern int nv_window_keys(unsigned short *b, unsigned char *a)
        __attribute__((weak));
    unsigned short kb = 0;
    unsigned char ka[8] = { 0 };
    int i;
    if (!(nv_window_keys && dwPort == 0 && nv_window_keys(&kb, ka))) return 0;
    if (kb & 0x0001u) *out |= XBOX_GAMEPAD_DPAD_UP;
    if (kb & 0x0002u) *out |= XBOX_GAMEPAD_DPAD_DOWN;
    if (kb & 0x0004u) *out |= XBOX_GAMEPAD_DPAD_LEFT;
    if (kb & 0x0008u) *out |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (kb & 0x0010u) *out |= XBOX_GAMEPAD_START;
    if (kb & 0x0020u) *out |= XBOX_GAMEPAD_BACK;
    if (kb & 0x0400u) *out |= XBOX_GAMEPAD_LEFT_THUMB;
    if (kb & 0x0800u) *out |= XBOX_GAMEPAD_RIGHT_THUMB;
    for (i = 0; i < 8; i++) if (ka[i]) analog[i] = ka[i];
    /* The same four keys also push the left stick, and the arrows the right
     * one: the skater only reads the stick (the d-pad is for menus). Full
     * deflection, a diagonal scaled onto the circle. A held key replaces the
     * pad's stick; nothing held leaves it. */
    {
        int lx = ((kb & 0x0008u) ? 1 : 0) - ((kb & 0x0004u) ? 1 : 0);
        int ly = ((kb & 0x0001u) ? 1 : 0) - ((kb & 0x0002u) ? 1 : 0);
        int rx = ((kb & 0x0200u) ? 1 : 0) - ((kb & 0x0100u) ? 1 : 0);
        int ry = ((kb & 0x0040u) ? 1 : 0) - ((kb & 0x0080u) ? 1 : 0);
        if (lx || ly) {
            short v = (lx && ly) ? 23170 : 32767;
            axes[0] = (short)(lx * v); axes[1] = (short)(ly * v);
        }
        if (rx || ry) {
            short v = (rx && ry) ? 23170 : 32767;
            axes[2] = (short)(rx * v); axes[3] = (short)(ry * v);
        }
    }
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

static SDL_GameController *g_pads[XBOX_MAX_CONTROLLERS];
static BOOL  g_controller_connected[XBOX_MAX_CONTROLLERS];
static DWORD g_packet[XBOX_MAX_CONTROLLERS];

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

    if (pState && input_script(dwPort, pState)) return ERROR_SUCCESS;
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pState)
        return ERROR_DEVICE_NOT_CONNECTED;

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
            SDL_GameControllerUpdate();
            have_pad = 1;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP))    out |= XBOX_GAMEPAD_DPAD_UP;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  out |= XBOX_GAMEPAD_DPAD_DOWN;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  out |= XBOX_GAMEPAD_DPAD_LEFT;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) out |= XBOX_GAMEPAD_DPAD_RIGHT;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START))      out |= XBOX_GAMEPAD_START;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK)
             && pad_back_is_games())                                            out |= XBOX_GAMEPAD_BACK;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSTICK))  out |= XBOX_GAMEPAD_LEFT_THUMB;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) out |= XBOX_GAMEPAD_RIGHT_THUMB;
            /* analog[] in the order kb_merge fills it and the end maps it:
             * A B X Y Black White LT RT; the shoulders are Black and White. */
            analog[0] = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A) ? 255 : 0;
            analog[1] = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B) ? 255 : 0;
            analog[2] = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X) ? 255 : 0;
            analog[3] = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y) ? 255 : 0;
            analog[4] = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? 255 : 0;
            analog[5] = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? 255 : 0;
            /* SDL trigger axes are 0..32767 -> Xbox analog button 0..255 */
            analog[6] = (unsigned char)(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
            analog[7] = (unsigned char)(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
            /* SDL Y axes point down; the Xbox's up -- invert, as (-1 - v) so
             * v = -32768 does not overflow. */
            axes[0] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
            axes[1] = (short)(-1 - SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
            axes[2] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX);
            axes[3] = (short)(-1 - SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
        }
    }

    /* The window's keyboard (kb_merge, above). */
    if (kb_merge(dwPort, &out, analog, axes)) have_pad = 1;
    menu_gate(dwPort, &out, analog, axes);

    if (!have_pad) {
        g_controller_connected[dwPort] = FALSE;
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    if (!g_controller_connected[dwPort])
        fprintf(stderr, "[PAD] controller %u connected\n", (unsigned)dwPort);
    g_controller_connected[dwPort] = TRUE;
    pState->dwPacketNumber = ++g_packet[dwPort];
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

DWORD xbox_InputSetState(DWORD dwPort, const XBOX_VIBRATION *pVibration)
{
    SDL_GameController *c;
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pVibration)
        return ERROR_DEVICE_NOT_CONNECTED;
    c = g_pads[dwPort];
    if (!c) return g_controller_connected[dwPort] ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
    /* SDL rumble needs a duration; refresh for ~1s on each call (the game
     * polls vibration continuously). */
    SDL_GameControllerRumble(c, pVibration->wLeftMotorSpeed,
                             pVibration->wRightMotorSpeed, 1000);
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
