/*
 * menu_sdl.cpp -- the in-game menu over the SDL window: F1, or Back on a
 * controller (Game Mode on a handheld has no keyboard). Back is the menu's
 * own button: the input layer keeps the pad's Back from the game.
 *
 * Dear ImGui with its own SDL2 and OpenGL 3 backends, as they come
 * (third_party/imgui). The frame is built on the GL thread, which owns the
 * window -- SDL's mouse and keyboard calls want that thread -- and drawn by
 * whichever thread swaps: the GL thread, or on Windows the presenter, with a
 * context of its own (nv2a_gl.c). One mutex keeps the two out of ImGui at
 * the same time. Nothing runs, and nothing is drawn, while the menu is shut.
 *
 * Built with -fno-exceptions -fno-rtti -fno-threadsafe-statics, as ImGui is:
 * nothing here may need the C++ runtime library (no new, no STL).
 */
#include "menu_sdl.h"
#include "menu.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <stdio.h>

/* The GLSL ImGui's shaders are compiled as: the context the window asks for
 * (nv2a_gl.c's gl_init), not ImGui's default guess. */
#if defined(IMGUI_IMPL_OPENGL_ES3)
#  define OV_GLSL "#version 300 es"
#else
#  define OV_GLSL "#version 330 core"
#endif

/* The menu is sized as the game's own text is: by the window's height
 * against the Xbox's 480 lines. */
#define OV_GAME_LINES 480.0f

static SDL_mutex   *g_mu;
static volatile int g_ready;        /* both read by the input layer's thread */
static volatile int g_open;
static int          g_just_opened;
static float        g_scale;
static ImGuiStyle   g_style;        /* before scaling */
static const char  *g_about_gl;
/* xbox_MenuBusy's box: set and built on the GL thread; the total is read by
 * whichever thread swaps, as g_open is. */
static const char  *g_busy_what;
static int          g_busy_done;
static volatile int g_busy_total;

/* ---- File: About, Exit -------------------------------------------------- */

/* The engine's version comes from the port (port/main.c), which is built
 * with it; the runtime is not. */
extern "C" const char *recomp_version(void) __attribute__((weak));

static const char *about_text(void)
{
    static char buf[768];
    SDL_version built, running;
    SDL_VERSION(&built);
    SDL_GetVersion(&running);
    snprintf(buf, sizeof buf,
             "JSRF Unleashed %s\n"
             "%s, built with %s\n\n"
             "SDL %u.%u.%u (built against %u.%u.%u), %s video\n"
             "%s\n"
             "Dear ImGui %s",
             recomp_version ? recomp_version() : "dev",
             SDL_GetPlatform(),
#if defined(__VERSION__)
             __VERSION__,
#else
             "an unknown compiler",
#endif
             running.major, running.minor, running.patch,
             built.major, built.minor, built.patch,
             SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "no",
             g_about_gl ? g_about_gl : "",
             IMGUI_VERSION);
    return buf;
}

/* The window's close button: sdl_pump's SDL_QUIT, which reports and exits. */
static void exit_game(void)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_QUIT;
    SDL_PushEvent(&e);
}

/* Every field, in order: C++11 has no designated initializers. */
static const XBOX_MENU_ITEM k_file[] = {
    { XBOX_MENU_FILE, XBOX_ITEM_INFO,      "About", NULL, NULL,      NULL, NULL, 0, about_text, NULL, NULL, NULL, 0, 0, 0, NULL },
    { XBOX_MENU_FILE, XBOX_ITEM_SEPARATOR, NULL,    NULL, NULL,      NULL, NULL, 0, NULL,       NULL, NULL, NULL, 0, 0, 0, NULL },
    { XBOX_MENU_FILE, XBOX_ITEM_ACTION,    "Exit",  NULL, exit_game, NULL, NULL, 0, NULL,
      "Anything since your last save is lost.", NULL, NULL, 0, 0, 0, NULL },
};

/* ---- opening and closing ------------------------------------------------ */

/* With g_mu held. */
static void set_open(int on)
{
    ImGuiIO &io = ImGui::GetIO();
    if (on == g_open) return;
    if (on) {
        /* Pads plugged in while the menu was shut were not seen: look again. */
        ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_AutoAll);
        g_just_opened = 1;
    } else {
        /* What was held when it shut is let go of, not remembered. */
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
    g_open = on;
    fprintf(stderr, "[MENU] %s\n", on ? "open" : "closed");
}

/* ---- the calls from the window (nv2a_gl.c) ------------------------------ */

extern "C" int xbox_MenuInit(SDL_Window *window, void *gl_context, const char *about_gl)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;              /* nothing written beside the game */
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    /* The window keeps its own rule for the pointer (hidden in full screen). */
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.Fonts->AddFontDefaultVector();   /* the scalable one: see scale_to_window */
    ImGui::StyleColorsDark();
    g_style = ImGui::GetStyle();
    if (!ImGui_ImplSDL2_InitForOpenGL(window, gl_context) || !ImGui_ImplOpenGL3_Init(OV_GLSL)) {
        fprintf(stderr, "[MENU] Dear ImGui did not start: no in-game menu\n");
        return 0;
    }
    g_mu = SDL_CreateMutex();
    if (!g_mu) return 0;
    g_about_gl = about_gl;
    xbox_MenuAdd(k_file, (int)(sizeof k_file / sizeof k_file[0]));
    g_ready = 1;
    fprintf(stderr, "[MENU] in-game menu: F1, or Back on a controller (Dear ImGui %s)\n",
            IMGUI_VERSION);
    return 1;
}

extern "C" int xbox_MenuEvent(const SDL_Event *ev)
{
    if (!g_ready) return 0;
    if ((ev->type == SDL_KEYDOWN && ev->key.keysym.scancode == SDL_SCANCODE_F1 && !ev->key.repeat)
     || (ev->type == SDL_CONTROLLERBUTTONDOWN && ev->cbutton.button == SDL_CONTROLLER_BUTTON_BACK)) {
        SDL_LockMutex(g_mu);
        set_open(!g_open);
        SDL_UnlockMutex(g_mu);
        return 1;
    }
    if (!g_open) return 0;              /* shut: ImGui sees nothing, queues nothing */
    SDL_LockMutex(g_mu);
    ImGui_ImplSDL2_ProcessEvent(ev);
    SDL_UnlockMutex(g_mu);
    /* A key let go still reaches the game, so nothing it saw pressed stays
     * held; a key pressed, typed text and clicks are the menu's. Closing the
     * window, focus and the rest go on to sdl_pump as before. */
    return ev->type == SDL_KEYDOWN || ev->type == SDL_TEXTINPUT
        || ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP
        || ev->type == SDL_MOUSEWHEEL;
}

static void scale_to_window(void)
{
    float s = ImGui::GetIO().DisplaySize.y / OV_GAME_LINES;
    if (s < 1.0f) s = 1.0f;
    if (s == g_scale) return;
    ImGuiStyle &st = ImGui::GetStyle();
    st = g_style;
    st.ScaleAllSizes(s);
    st.FontScaleMain = s;
    g_scale = s;
}

extern "C" void xbox_MenuFrame(void)
{
    if (!g_ready || (!g_open && !g_busy_total)) return;
    SDL_LockMutex(g_mu);
    /* Full screen hides the pointer (nv2a_gl.c), and the menu can switch it:
     * ImGui draws its own then, which shows only once a mouse moves. */
    ImGui::GetIO().MouseDrawCursor = g_open && SDL_ShowCursor(SDL_QUERY) == SDL_DISABLE;
    ImGui_ImplSDL2_NewFrame();
    scale_to_window();
    ImGui::NewFrame();
    if (g_open) menu_build(g_just_opened);
    g_just_opened = 0;
    if (g_busy_total) menu_build_busy(g_busy_what, g_busy_done, g_busy_total);
    ImGui::Render();
    SDL_UnlockMutex(g_mu);
}

extern "C" void xbox_MenuDraw(void)
{
    ImDrawData *dd;
    if (!g_ready || (!g_open && !g_busy_total)) return;
    SDL_LockMutex(g_mu);
    dd = ImGui::GetDrawData();
    if (dd) {
        /* The renderer's GL objects are made (the first time) and used here,
         * in the context of the thread that swaps: the GL thread never
         * touches them, so nothing crosses between the two contexts. */
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplOpenGL3_RenderDrawData(dd);
    }
    SDL_UnlockMutex(g_mu);
}

extern "C" void xbox_MenuBusy(const char *what, int done, int total)
{
    g_busy_what = what;
    g_busy_done = done;
    g_busy_total = total;
}

extern "C" int xbox_MenuIsOpen(void)
{
    return g_ready ? g_open : -1;
}
