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
 * the same time. The menu fades in as it opens and out as it shuts
 * (menu_fade); nothing runs, and nothing is drawn, once it has faded out.
 * The window's own box, F9's (xbox_MenuBox), is drawn the same way, with
 * no menu round it.
 *
 * Built with -fno-exceptions -fno-rtti -fno-threadsafe-statics, as ImGui is:
 * nothing here may need the C++ runtime library (no new, no STL).
 */
#include "menu_sdl.h"
#include "menu.h"
#include "menu_style.h"
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
static float        g_fade;         /* how far in it has faded: menu_fade */
static int          g_pad;          /* a pad's button was the last pressed, not a key */
static volatile int g_shown;        /* the last frame built has the menu in it:
                                     * read by whichever thread swaps */
static float        g_scale;
static ImGuiStyle   g_style;        /* before scaling */
static const char  *g_about_gpu, *g_about_gl;
/* xbox_MenuBusy's box: set and built on the GL thread; the total is read by
 * whichever thread swaps, as g_open is. */
static const char  *g_busy_what;
static int          g_busy_done;
static volatile int g_busy_total;
/* xbox_MenuBox's box: put up, built and shut on the GL thread; whether it is
 * up is read by any thread, as g_open is. */
static XBOX_MENU_BOX g_box;
static volatile int  g_box_up;
static int           g_box_just;    /* its first frame is the next */

/* ---- File: About, Presets, Restart, Exit -------------------------------- */

/* The engine's version comes from the port (port/main.c), which is built
 * with it; the runtime is not. */
extern "C" const char *recomp_version(void) __attribute__((weak));

/* File > About, a section at a time: the build, the OS, the graphics card,
 * SDL, OpenGL, Dear ImGui. Every section is written the same way, and so is
 * any added later: its name on a line of its own, then each detail -- the
 * version first, where it has one -- on a line starting with "-", and a blank
 * line between one section and the next (docs/menu-format.md). */
static const char *about_text(void)
{
    static char buf[768];
    SDL_version built, running;
    SDL_VERSION(&built);
    SDL_GetVersion(&running);
    snprintf(buf, sizeof buf,
             "JSRF-PC\n"
             "-%s\n\n"
             "%s\n"
             "-Built with %s\n\n"
             "Graphics\n"
             "-%s\n\n"
             "SDL\n"
             "-%u.%u.%u\n"
             "-Built against %u.%u.%u\n"
             "-%s video\n\n"
             "OpenGL\n"
             "-%s\n\n"
             "Dear ImGui\n"
             "-%s",
             recomp_version ? recomp_version() : "dev",
             SDL_GetPlatform(),
#if defined(__GNUC__) && !defined(__clang__)
             "GCC " __VERSION__,        /* GCC's is the bare number; Clang's names it */
#elif defined(__VERSION__)
             __VERSION__,
#else
             "an unknown compiler",
#endif
             g_about_gpu ? g_about_gpu : "unknown",
             running.major, running.minor, running.patch,
             built.major, built.minor, built.patch,
             SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "no",
             g_about_gl ? g_about_gl : "unknown",
             IMGUI_VERSION);
    return buf;
}

/* The game started again (nv2a_gl.c's sdl_restart): what waits for the next
 * start applies, Developer's switches among them. Not offered where the game
 * cannot start itself again (port/main.c's). */
extern "C" void nv_window_restart(void);
extern "C" void recomp_restart_self(void) __attribute__((weak));

/* The window's close button: sdl_pump's SDL_QUIT, which reports and exits. */
static void exit_game(void)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_QUIT;
    SDL_PushEvent(&e);
}

/* File > Presets: every row the presets set (menu.h) put at the Xbox's own
 * settings, the heavy ones at their least, between, or at their most, each
 * by its owner's own function (menu_preset_apply); ticked where the
 * settings are now. Each applies at once, with no box, as Display >
 * Resolution does: the (Restart) rows, Shader cache and Audio effects, are
 * left to the player (spriteice's card, 02:00 UTC on 2026-11-21). A
 * (Restart) row a preset set would keep its value, and the game would
 * start again once, after them all. */
static const char *const k_preset_names[] = { "Xbox", "Performance", "Balanced", "Quality" };
static_assert(sizeof k_preset_names / sizeof k_preset_names[0] == XBOX_PRESETS, "a name for each preset in menu.h");

static void preset_pick(int i)
{
    fprintf(stderr, "[MENU] preset %s chosen\n", k_preset_names[i]);
    if (menu_preset_apply(i)) nv_window_restart();     /* returns only where it can't */
}

/* Every field, in order: C++11 has no designated initializers. */
#define FILE_NO_PRESETS XBOX_PRESETS_UNSAID, { 0, 0, 0, 0 }, NULL
#define FILE_NO_BIND    XBOX_BIND_NONE, NULL, NULL, NULL, 0
static const XBOX_MENU_ITEM k_file[] = {
    { XBOX_MENU_FILE, XBOX_ITEM_INFO,      "About", NULL, NULL,      NULL, NULL, 0, about_text, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
      "Shows the engine version and relevant system details.", FILE_NO_PRESETS, NULL, NULL, NULL, FILE_NO_BIND },
    { XBOX_MENU_FILE, XBOX_ITEM_SEPARATOR, NULL,    NULL, NULL,      NULL, NULL, 0, NULL,       NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
      NULL, FILE_NO_PRESETS, NULL, NULL, NULL, FILE_NO_BIND },
    { XBOX_MENU_FILE, XBOX_ITEM_CHOICE,    "Presets", menu_preset_now, NULL, preset_pick,
      k_preset_names, XBOX_PRESETS, NULL,
      NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
      "Sets many settings at once: the Xbox's own, the lightest, or the best.", FILE_NO_PRESETS, NULL, NULL, NULL, FILE_NO_BIND },
    { XBOX_MENU_FILE, XBOX_ITEM_SEPARATOR, NULL,    NULL, NULL,      NULL, NULL, 0, NULL,       NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
      NULL, FILE_NO_PRESETS, NULL, NULL, NULL, FILE_NO_BIND },
    { XBOX_MENU_FILE, XBOX_ITEM_ACTION,    "Restart", NULL, nv_window_restart, NULL, NULL, 0, NULL,
      "Restart the game, losing unsaved progress?", NULL, NULL, 0, 0, 0, NULL, 0, NULL,
      "Restarts the game, losing unsaved progress.", FILE_NO_PRESETS, NULL, NULL, NULL, FILE_NO_BIND },
    { XBOX_MENU_FILE, XBOX_ITEM_ACTION,    "Exit",  NULL, exit_game, NULL, NULL, 0, NULL,
      "Close the game, losing unsaved progress?", NULL, NULL, 0, 0, 0, NULL, 0, NULL,
      "Closes the game, losing unsaved progress.", FILE_NO_PRESETS, NULL, NULL, NULL, FILE_NO_BIND },
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

/* With g_mu held: F9's box shut, what was held let go of, as the menu's is
 * as it shuts; and gone from the next swap, which would draw the last frame
 * built, with the box in it (xbox_MenuDraw). */
static void box_shut(const char *how)
{
    ImGuiIO &io = ImGui::GetIO();
    io.ClearInputKeys();
    io.ClearInputMouse();
    g_box_up = 0;
    g_shown = 0;
    fprintf(stderr, "[MENU] %s box closed%s\n", g_box.title, how);
}

/* With g_mu held, between ImGui::NewFrame() and ImGui::Render(): a frame of
 * F9's box, and a choice handed to its pick as it shuts. */
static void box_frame(void)
{
    int r = menu_build_box(&g_box, g_box_just);
    g_box_just = 0;
    if (r == MENU_BOX_UP) return;
    if (r >= 0) {
        fprintf(stderr, "[MENU] %s: %s chosen\n", g_box.title, g_box.choices[r]);
        g_box.pick(r);
    }
    box_shut(r >= 0 ? "" : ", nothing chosen");
}

/* ---- the calls from the window (nv2a_gl.c) ------------------------------ */

extern "C" int xbox_MenuInit(SDL_Window *window, void *gl_context, const char *about_gpu,
                             const char *about_gl)
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
    menu_style_apply(&ImGui::GetStyle());
    g_style = ImGui::GetStyle();
    if (!ImGui_ImplSDL2_InitForOpenGL(window, gl_context) || !ImGui_ImplOpenGL3_Init(OV_GLSL)) {
        fprintf(stderr, "[MENU] Dear ImGui did not start: no in-game menu\n");
        return 0;
    }
    g_mu = SDL_CreateMutex();
    if (!g_mu) return 0;
    g_about_gpu = about_gpu;
    g_about_gl = about_gl;
    /* Restart only where the game can start itself again (port/main.c). */
    {
        XBOX_MENU_ITEM rows[sizeof k_file / sizeof k_file[0]];
        int n = 0;
        for (const XBOX_MENU_ITEM &it : k_file)
            if (it.run != nv_window_restart || recomp_restart_self) rows[n++] = it;
        xbox_MenuAdd(rows, n);
    }
    g_ready = 1;
    fprintf(stderr, "[MENU] in-game menu: F1, or Back on a controller (Dear ImGui %s)\n",
            IMGUI_VERSION);
    return 1;
}

/* ---- a bind row's press (menu.h, XBOX_ITEM_BIND) ------------------------- */

/* A trigger is pressed past half way, as the input layer presses a button
 * with one (src/input/input_layout.h, INPUT_LAYOUT_DOWN); each pad's two
 * as they were at their last events, by SDL's id for the pad, so a press
 * is a trigger going past it, not one held there already. */
#define OV_TRIGGER_DOWN 16384
#define OV_TRIGGER_PADS 8           /* pads told apart: their ids, modulo this */
static unsigned char g_trigger_down[OV_TRIGGER_PADS][2];

/* A trigger's event: 1 when it goes past half way, kept for the next. */
static int trigger_pressed(const SDL_ControllerAxisEvent *a)
{
    unsigned char *was;
    int down;
    if (a->axis != SDL_CONTROLLER_AXIS_TRIGGERLEFT && a->axis != SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return 0;
    was = &g_trigger_down[(unsigned)a->which % OV_TRIGGER_PADS][a->axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT];
    down = a->value >= OV_TRIGGER_DOWN;
    if (down == *was) return 0;
    *was = (unsigned char)down;
    return down;
}

/* While a bind row waits for a press (menu_bind_waiting), with g_mu held:
 * the next it takes, handed to it, and the pad's Back or F1 handing it
 * none (menu_bind_none).
 * 1: the event was the wait's, and neither the menu nor the game sees it:
 * every pad button, and every key and mouse button while it waits for
 * keys. While it waits for a pad, the keys and the mouse are the box's, as
 * any box's are: Esc cancels it there (menu_build). */
static int bind_event(const SDL_Event *ev, int takes, int trigger)
{
    switch (ev->type) {
    case SDL_CONTROLLERBUTTONDOWN:
        if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_BACK) menu_bind_none();
        else if (takes == XBOX_BIND_PAD)                      menu_bind_press(ev->cbutton.button);
        return 1;
    case SDL_CONTROLLERAXISMOTION:
        if (trigger && takes == XBOX_BIND_PAD)
            menu_bind_press(SDL_CONTROLLER_BUTTON_MAX + (ev->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
        return 0;
    case SDL_KEYDOWN:
        if (ev->key.keysym.scancode == SDL_SCANCODE_F1) {
            if (!ev->key.repeat) menu_bind_none();
            return 1;
        }
        if (takes != XBOX_BIND_KEYS) return 0;
        if (!ev->key.repeat) menu_bind_press(ev->key.keysym.scancode);
        return 1;
    case SDL_MOUSEBUTTONDOWN:
        if (takes != XBOX_BIND_KEYS) return 0;
        menu_bind_press(SDL_NUM_SCANCODES + ev->button.button - SDL_BUTTON_LEFT);
        return 1;
    default:
        return 0;
    }
}

extern "C" int xbox_MenuEvent(const SDL_Event *ev)
{
    int trigger = 0;
    if (!g_ready) return 0;
    /* Which the player has in hand, for the buttons the menu shows: the
     * last pressed, in the game or the menu. The mouse leaves it be. */
    if (ev->type == SDL_CONTROLLERBUTTONDOWN) g_pad = 1;
    else if (ev->type == SDL_KEYDOWN) g_pad = 0;
    if (ev->type == SDL_CONTROLLERAXISMOTION) trigger = trigger_pressed(&ev->caxis);
    if (g_open) {
        int takes, bound;
        SDL_LockMutex(g_mu);
        takes = menu_bind_waiting();
        bound = takes && bind_event(ev, takes, trigger);
        SDL_UnlockMutex(g_mu);
        if (bound) return 1;
    }
    /* F9's box: F9, F1 and the pad's Back shut it, with nothing chosen, and
     * don't open the menu. */
    if (g_box_up && ((ev->type == SDL_KEYDOWN && !ev->key.repeat
                      && (ev->key.keysym.scancode == SDL_SCANCODE_F9
                       || ev->key.keysym.scancode == SDL_SCANCODE_F1))
                  || (ev->type == SDL_CONTROLLERBUTTONDOWN
                      && ev->cbutton.button == SDL_CONTROLLER_BUTTON_BACK))) {
        SDL_LockMutex(g_mu);
        box_shut(", by its key");
        SDL_UnlockMutex(g_mu);
        return 1;
    }
    if ((ev->type == SDL_KEYDOWN && ev->key.keysym.scancode == SDL_SCANCODE_F1 && !ev->key.repeat)
     || (ev->type == SDL_CONTROLLERBUTTONDOWN && ev->cbutton.button == SDL_CONTROLLER_BUTTON_BACK)) {
        SDL_LockMutex(g_mu);
        set_open(!g_open);
        SDL_UnlockMutex(g_mu);
        return 1;
    }
    if (!g_open && !g_box_up) return 0; /* shut: ImGui sees nothing, queues nothing */
    SDL_LockMutex(g_mu);
    ImGui_ImplSDL2_ProcessEvent(ev);
    SDL_UnlockMutex(g_mu);
    /* A key or a mouse button let go still reaches the game, so nothing it
     * saw pressed stays held (a mouse button can press a control,
     * src/input/input_layout.h); a key pressed, typed text, clicks and the
     * wheel are the menu's. Closing the window, focus and the rest go on to
     * sdl_pump as before. */
    return ev->type == SDL_KEYDOWN || ev->type == SDL_TEXTINPUT
        || ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEWHEEL;
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
    if (!g_ready) return;
    if (!g_open && g_fade <= 0.0f && !g_busy_total && !g_box_up) {
        g_shown = 0;                    /* nothing built: nothing drawn again */
        return;
    }
    SDL_LockMutex(g_mu);
    ImGuiIO &io = ImGui::GetIO();
    /* Full screen hides the pointer (nv2a_gl.c), and the menu can switch it:
     * ImGui draws its own then, which shows only once a mouse moves. */
    io.MouseDrawCursor = (g_open || g_box_up) && SDL_ShowCursor(SDL_QUERY) == SDL_DISABLE;
    ImGui_ImplSDL2_NewFrame();
    scale_to_window();
    /* Just opened, the time since the last frame is how long it was shut,
     * with no frames: the fade starts from where it was. F9's box, up only
     * while the menu is shut, puts out at once what is left of its fade,
     * and is drawn whole: the box has the controls from its first frame. */
    if (g_box_up) g_fade = 0.0f;
    else g_fade = menu_fade(g_fade, g_open, g_just_opened ? 0.0f : io.DeltaTime);
    ImGui::GetStyle().Alpha = g_box_up ? 1.0f : g_fade;  /* the menu, its boxes and the dim behind */
    ImGui::NewFrame();
    if (g_box_up) box_frame();
    else if (g_open || g_fade > 0.0f) menu_build(g_just_opened, g_pad);
    g_just_opened = 0;
    if (g_busy_total) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);    /* not the menu's to fade */
        menu_build_busy(g_busy_what, g_busy_done, g_busy_total);
        ImGui::PopStyleVar();
    }
    ImGui::Render();
    g_shown = g_open || g_fade > 0.0f || g_box_up;
    SDL_UnlockMutex(g_mu);
}

extern "C" void xbox_MenuDraw(void)
{
    ImDrawData *dd;
    /* The menu while it is in the last frame built; the busy box only while
     * it lasts, not once more after it ends (xbox_MenuBusy). */
    if (!g_ready || (!g_shown && !g_busy_total)) return;
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

extern "C" void xbox_MenuBox(const XBOX_MENU_BOX *box)
{
    if (!g_ready || g_open || g_box_up) return;
    SDL_LockMutex(g_mu);
    g_box = *box;
    g_box_just = 1;
    /* Pads plugged in while it was down were not seen: look again, as the
     * menu does as it opens (set_open). */
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_AutoAll);
    g_box_up = 1;
    SDL_UnlockMutex(g_mu);
    fprintf(stderr, "[MENU] %s box open\n", box->title);
}

extern "C" int xbox_MenuIsOpen(void)
{
    return g_ready ? (g_open || g_box_up) : -1;
}
