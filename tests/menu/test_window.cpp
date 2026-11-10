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
 * together: the box ending while the menu fades out.
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

/* Dear ImGui's SDL backend: the null one's frame (1920 x 1080, 1/60 s). */
enum ImGui_ImplSDL2_GamepadMode { ImGui_ImplSDL2_GamepadMode_AutoAll };
static void ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode) { }
static void ImGui_ImplSDL2_NewFrame(void) { ImGui_ImplNull_NewFrame(); }

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

    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    fprintf(stderr, g_fail ? "menu window test FAILED\n" : "menu window test passed\n");
    return g_fail;
}
