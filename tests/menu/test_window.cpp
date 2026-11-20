/* Run with: python3 tests/menu/run.py
 *
 * The window's side of the menu (src/menu/menu_sdl.cpp), with no window,
 * GPU or SDL: xbox_MenuFrame builds a frame, xbox_MenuDraw draws the last
 * one built, and xbox_MenuBusy puts a box up while the game waits. run.py
 * takes them from that file as they are (cut by text), with set_open and
 * the state they share, into menu_sdl_cut.inc; SDL and Dear ImGui's SDL and
 * OpenGL backends are stood in for below. They are driven as nv2a_gl.c's
 * sdl_show drives them: the frame drawn and swapped (xbox_MenuDraw), then
 * the events read and the next frame built (xbox_MenuFrame).
 *
 * Checks that the busy box (the shader cache rebuilt before the first
 * frame) is drawn while it lasts, and not once after it ends, however many
 * frames follow; that the menu is drawn only from the first frame built of
 * it, while it is open and as it fades out, and not after; and the two
 * together: the box ending while the menu fades out. Then a bind row's box
 * (menu.h, XBOX_ITEM_BIND), through xbox_MenuEvent as sdl_pump hands it
 * SDL's events: the next press it takes, a pad's button, a trigger going
 * past half way on its pad (not one held there already, or another pad's),
 * a key, a mouse button, handed to the row and kept from the menu and the
 * game; the pad's Back and F1 cancelling it, not shutting the menu; and
 * what it doesn't take left to the menu as before.
 */
#include "menu.h"
#include "imgui.h"
#include "backends/imgui_impl_null.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail = 1; } } while (0)

/* SDL: one thread here, so the mutex is only counted; the pointer shown. */
typedef struct SDL_mutex SDL_mutex;
static int s_held;
static int SDL_LockMutex(SDL_mutex *)   { s_held++; return 0; }
static int SDL_UnlockMutex(SDL_mutex *) { s_held--; return 0; }
#define SDL_QUERY   (-1)
#define SDL_DISABLE 0
static int SDL_ShowCursor(int)          { return 1; }

/* Its events, the fields xbox_MenuEvent reads, with SDL's numbers for the
 * buttons, axes and keys (SDL_gamecontroller.h, SDL_scancode.h, SDL_mouse.h). */
enum { SDL_KEYDOWN = 0x300, SDL_KEYUP, SDL_TEXTINPUT = 0x303, SDL_MOUSEBUTTONDOWN = 0x401, SDL_MOUSEBUTTONUP,
       SDL_MOUSEWHEEL, SDL_CONTROLLERAXISMOTION = 0x650, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONUP };
enum { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_BACK = 4,
       SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_MAX = 21 };
enum { SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_TRIGGERLEFT = 4, SDL_CONTROLLER_AXIS_TRIGGERRIGHT };
enum { SDL_SCANCODE_W = 26, SDL_SCANCODE_ESCAPE = 41, SDL_SCANCODE_F1 = 58, SDL_NUM_SCANCODES = 512 };
enum { SDL_BUTTON_LEFT = 1, SDL_BUTTON_RIGHT = 3 };
typedef struct { unsigned type; unsigned char repeat; struct { int scancode; } keysym; } SDL_KeyboardEvent;
typedef struct { unsigned type; int which; unsigned char button; } SDL_ControllerButtonEvent;
typedef struct { unsigned type; int which; unsigned char axis; short value; } SDL_ControllerAxisEvent;
typedef struct { unsigned type; unsigned char button; } SDL_MouseButtonEvent;
typedef union SDL_Event {
    unsigned                  type;
    SDL_KeyboardEvent         key;
    SDL_ControllerButtonEvent cbutton;
    SDL_ControllerAxisEvent   caxis;
    SDL_MouseButtonEvent      button;
} SDL_Event;

/* Dear ImGui's SDL backend: the null one's frame (1920 x 1080, 1/60 s), and
 * the events handed to it, counted. */
enum ImGui_ImplSDL2_GamepadMode { ImGui_ImplSDL2_GamepadMode_AutoAll };
static void ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode) { }
static void ImGui_ImplSDL2_NewFrame(void) { ImGui_ImplNull_NewFrame(); }
static int s_to_imgui;
static bool ImGui_ImplSDL2_ProcessEvent(const SDL_Event *) { s_to_imgui++; return true; }

/* Its OpenGL backend: each draw counted, with what it drew. */
static int s_draws, s_drew_busy, s_drew_bar;
static void ImGui_ImplOpenGL3_NewFrame(void) { }
static void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData *dd)
{
    s_draws++;
    s_drew_busy = s_drew_bar = 0;
    for (int i = 0; i < dd->CmdLists.Size; i++) {
        const char *owner = dd->CmdLists[i]->_OwnerName;
        if (owner && !strcmp(owner, "##busy")) s_drew_busy = 1;
        if (owner && !strcmp(owner, "##MainMenuBar")) s_drew_bar = 1;
    }
}

#include "menu_sdl_cut.inc"

/* One frame as sdl_show: 1 if it drew. */
static int show(void)
{
    int was = s_draws;
    xbox_MenuDraw();
    xbox_MenuFrame();
    CHECK(s_held == 0);
    return s_draws > was;
}

/* F1 or Back, as xbox_MenuEvent: set_open with the mutex held. */
static void press_back(int on)
{
    SDL_LockMutex(g_mu);
    set_open(on);
    SDL_UnlockMutex(g_mu);
}

/* Bind rows (menu.h, XBOX_ITEM_BIND) in Input, the only menu here: one for
 * a pad, one for the keys, each keeping what it was handed. */
static int s_pad_input = -1, s_key_input = -1, s_handed;
static int bind_pad(int input) { s_pad_input = input; s_handed++; return 1; }
static int bind_key(int input) { s_key_input = input; s_handed++; return 1; }
static const char *bind_now(void) { return "A"; }
static XBOX_MENU_ITEM bind_row(const char *label, XBOX_BIND_TAKES takes, int (*bind)(int))
{
    XBOX_MENU_ITEM it = {};
    it.menu = XBOX_MENU_INPUT;
    it.kind = XBOX_ITEM_BIND;
    it.label = label;
    it.text = bind_now;
    it.hint = "Sets what presses it.";
    it.presets = XBOX_PRESETS_LEFT;
    it.takes = takes;
    it.bind = bind;
    return it;
}

/* A frame with a pad's button held, as the backend reads the pad, then one
 * with it let go. */
static void pad_frame(ImGuiKey key)
{
    ImGui::GetIO().AddKeyEvent(key, true);
    show();
    ImGui::GetIO().AddKeyEvent(key, false);
    show();
}

/* An event, as sdl_pump hands it over: 1 when the menu took it. */
static int event(unsigned type, int a, int b = 0, int which = 0)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = type;
    if (type == SDL_KEYDOWN || type == SDL_KEYUP) { e.key.keysym.scancode = a; e.key.repeat = (unsigned char)b; }
    else if (type == SDL_CONTROLLERBUTTONDOWN)     { e.cbutton.button = (unsigned char)a; e.cbutton.which = which; }
    else if (type == SDL_CONTROLLERAXISMOTION)     { e.caxis.axis = (unsigned char)a; e.caxis.value = (short)b;
                                                     e.caxis.which = which; }
    else                                           e.button.button = (unsigned char)a;
    return xbox_MenuEvent(&e);
}

int main(void)
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplNull_Init();
    g_style = ImGui::GetStyle();            /* as xbox_MenuInit keeps it */
    (void)g_about_gpu; (void)g_about_gl;    /* File > About's, not here */
    g_ready = 1;

    /* Shut, and no box: nothing drawn. */
    {
        int drawn = 0;
        for (int i = 0; i < 10; i++) drawn += show();
        CHECK(drawn == 0);
    }

    /* The shader cache rebuilt before the first frame (sdl_shader_cache):
     * drawn from the first frame built with it, every frame while it lasts;
     * the box in each from the second (Dear ImGui keeps a new window out of
     * its first frame, while it sizes it). */
    {
        int drawn = 0, busy = 0;
        xbox_MenuBusy("Building shaders", 0, 20);
        show();                             /* drawn before any was built */
        for (int done = 1; done < 20; done++) {
            xbox_MenuBusy("Building shaders", done, 20);
            drawn += show();
            busy += s_drew_busy;
        }
        fprintf(stderr, "the box: drawn %d of 19 frames, the box in %d\n", drawn, busy);
        CHECK(drawn == 19 && busy == 18 && s_drew_busy);
    }
    /* Over (xbox_MenuBusy(NULL, 0, 0)): the game's first frame is drawn
     * without it, and so is every frame after. 22a5376-menu1 drew its last
     * frame, the box at 19 of 20, over the game until the menu was opened. */
    {
        int drawn = 0;
        xbox_MenuBusy(NULL, 0, 0);
        for (int i = 0; i < 300; i++) drawn += show();
        fprintf(stderr, "after the box: drawn %d of 300 frames\n", drawn);
        CHECK(drawn == 0);
    }

    /* Opened: not drawn until a frame of it is built (the one drawn first
     * was built before), then every frame, faded in. */
    {
        int drawn = 0, bar = 0;
        press_back(1);
        CHECK(!show());
        for (int i = 0; i < 30; i++) {
            drawn += show();
            bar += s_drew_bar;
        }
        fprintf(stderr, "open: drawn %d of 30 frames, the bar in %d, faded in to %.2f\n", drawn, bar, g_fade);
        CHECK(drawn == 30 && bar == 30 && g_fade == 1.0f && !s_drew_busy);
    }
    /* Shut: drawn as it fades out, in MENU_FADE_SECONDS, then no more. */
    {
        int n = 0, bar = 0, drawn = 0;
        press_back(0);
        while (n < 60 && show()) {
            n++;
            bar += s_drew_bar;
        }
        for (int i = 0; i < 300; i++) drawn += show();
        fprintf(stderr, "shut: drawn %d frames as it faded out, the bar in %d; then %d of 300\n",
                n, bar, drawn);
        CHECK(g_fade == 0.0f && bar == n && drawn == 0);
        CHECK(fabsf(n / 60.0f - MENU_FADE_SECONDS) <= 1.0f / 60.0f + 1e-4f);
    }

    /* The two together: the box up with the menu open; shut, and the box
     * over as it fades out. The menu is drawn until it has faded out, the
     * box no more once a frame is built without it, and then nothing. */
    {
        int n = 0, boxed = 0, drawn = 0;
        press_back(1);
        for (int i = 0; i < 10; i++) show();
        xbox_MenuBusy("Building shaders", 1, 2);
        show();
        CHECK(show() && s_drew_busy && s_drew_bar);
        press_back(0);
        CHECK(show() && s_drew_busy && s_drew_bar);
        xbox_MenuBusy(NULL, 0, 0);
        CHECK(show() && s_drew_bar);        /* the last built, the box in it */
        while (n < 60 && show()) {
            n++;
            boxed += s_drew_busy;
        }
        for (int i = 0; i < 300; i++) drawn += show();
        fprintf(stderr, "both: the menu drawn %d frames more as it faded out, the box in %d; then %d of 300\n",
                n, boxed, drawn);
        CHECK(n > 0 && boxed == 0 && drawn == 0 && g_fade == 0.0f);
    }

    /* A bind row's box, through the window's events. The rows arrive;
     * F1 opens the menu; A opens Input, on the pad's row; A: its box, which
     * waits for a pad. */
    {
        const XBOX_MENU_ITEM rows[] = { bind_row("A", XBOX_BIND_PAD, bind_pad),
                                        bind_row("Left stick up", XBOX_BIND_KEYS, bind_key) };
        xbox_MenuAdd(rows, 2);
    }
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    CHECK(event(SDL_KEYDOWN, SDL_SCANCODE_F1) && g_open);
    /* Its right trigger held past half way before the box: tracked, by pad. */
    CHECK(!event(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30000));
    for (int i = 0; i < 3; i++) show();
    pad_frame(ImGuiKey_GamepadFaceDown); show();
    pad_frame(ImGuiKey_GamepadFaceDown); show();
    fprintf(stderr, "bind: waiting %d\n", menu_bind_waiting());
    CHECK(menu_bind_waiting() == XBOX_BIND_PAD);
    /* What it doesn't take reaches the menu as before: a key; and a mouse
     * button, as the box is the menu's. The left stick moving: no press. */
    s_to_imgui = 0;
    CHECK(event(SDL_KEYDOWN, SDL_SCANCODE_W) && s_to_imgui == 1);
    CHECK(event(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT) && s_to_imgui == 2);
    CHECK(!event(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLER_AXIS_LEFTX, 30000) && s_to_imgui == 3);
    /* The right trigger, already past half way: no press. Another pad's,
     * let go: no press, and the first's further still: none. Let go, and
     * past half way again: the press, as the right trigger. */
    CHECK(!event(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30500));
    CHECK(!event(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0, 1));
    CHECK(!event(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 31000));
    CHECK(s_handed == 0 && menu_bind_waiting() == XBOX_BIND_PAD);
    CHECK(!event(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 100));
    CHECK(!event(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 16383));
    CHECK(s_handed == 0);
    CHECK(!event(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 16384));
    fprintf(stderr, "bind: the trigger, handed %d: %d\n", s_handed, s_pad_input);
    CHECK(s_handed == 1 && s_pad_input == SDL_CONTROLLER_BUTTON_MAX + 1 && menu_bind_waiting() == 0);
    for (int i = 0; i < 3; i++) show();
    /* Again: a button, B, the row's and no one else's. */
    pad_frame(ImGuiKey_GamepadFaceDown); show();
    CHECK(menu_bind_waiting() == XBOX_BIND_PAD);
    s_to_imgui = 0;
    CHECK(event(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_B) && s_to_imgui == 0);
    CHECK(s_handed == 2 && s_pad_input == SDL_CONTROLLER_BUTTON_B && g_open);
    for (int i = 0; i < 3; i++) show();
    /* Again: the pad's Back, then F1, each cancels, and the menu stays. */
    pad_frame(ImGuiKey_GamepadFaceDown); show();
    CHECK(event(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_BACK));
    CHECK(menu_bind_waiting() == 0 && s_handed == 2 && g_open);
    for (int i = 0; i < 3; i++) show();
    pad_frame(ImGuiKey_GamepadFaceDown); show();
    CHECK(event(SDL_KEYDOWN, SDL_SCANCODE_F1));
    CHECK(menu_bind_waiting() == 0 && s_handed == 2 && g_open);
    for (int i = 0; i < 3; i++) show();
    /* Down: the keys' row. A: its box, which waits for a key. A pad's
     * button: nobody's. Esc: the row's, as is the mouse's right button
     * next time, and neither reaches the menu; a key held, repeating,
     * isn't a press. */
    pad_frame(ImGuiKey_GamepadDpadDown); show();
    pad_frame(ImGuiKey_GamepadFaceDown); show();
    CHECK(menu_bind_waiting() == XBOX_BIND_KEYS);
    s_to_imgui = 0;
    CHECK(event(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A) && s_handed == 2);
    CHECK(event(SDL_KEYDOWN, SDL_SCANCODE_ESCAPE, 1) && s_handed == 2);
    CHECK(event(SDL_KEYDOWN, SDL_SCANCODE_ESCAPE) && s_handed == 3 && s_key_input == SDL_SCANCODE_ESCAPE);
    for (int i = 0; i < 3; i++) show();
    pad_frame(ImGuiKey_GamepadFaceDown); show();
    CHECK(menu_bind_waiting() == XBOX_BIND_KEYS);
    CHECK(event(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT));
    fprintf(stderr, "bind: the mouse's right button, handed %d: %d; to the menu %d\n", s_handed, s_key_input,
            s_to_imgui);
    CHECK(s_handed == 4 && s_key_input == SDL_NUM_SCANCODES + 2 && s_to_imgui == 0);
    for (int i = 0; i < 3; i++) show();
    /* No box: F1 shuts the menu, as ever. */
    CHECK(event(SDL_KEYDOWN, SDL_SCANCODE_F1) && !g_open);
    for (int i = 0; i < 30; i++) show();

    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    fprintf(stderr, g_fail ? "menu window test FAILED\n" : "menu window test passed\n");
    return g_fail;
}
