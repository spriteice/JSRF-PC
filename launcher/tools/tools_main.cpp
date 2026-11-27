/*
 * JSRF-PC Tools (tools.h): the window. Dear ImGui over SDL2 and OpenGL 3,
 * in the in-game menu's look (src/menu/menu_style.h), as the launcher's
 * setup window is.
 *
 *   Finding the game: started in JSRF-PC's folder (beside the launcher), it
 *     knows where the game's saves are; started anywhere else, it asks.
 *   Adding files: dropped on the window, or picked with the system's own
 *     dialog (file_dialog.c), handed to the page that shows.
 *   Writing: only from a page's own button, after the shell's warning
 *     (tools_warning), and after a dated backup.
 *   Size: the window resizes freely, down to a size where nothing is cut
 *     off; the layout reflows, and text and controls follow the display's
 *     scale, not the window's size.
 *   Idle: the window draws while something happens (input, and a moment
 *     after it for the interface to settle) or a page is busy (tools_ctx's
 *     busy), and otherwise once a second, so an open window costs nearly
 *     nothing (a Steam Deck's battery among others).
 *
 *   jsrf-pc-tools [JSRF-PC's folder]
 */
#define SDL_MAIN_HANDLED   /* the entry point is the tool's own (below), with UTF-8 arguments */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include "menu_style.h"
#include "file_dialog.h"
#include "setup.h"
#include "setup_job.h"
#include "tools.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#define T_GLAPI __stdcall
#define LAUNCHER_NAME "JSRF-PC.exe"
#else
#define T_GLAPI
#define LAUNCHER_NAME "jsrf-pc"
#endif
typedef void (T_GLAPI *gl_viewport_fn)(int, int, int, int);
typedef void (T_GLAPI *gl_clear_color_fn)(float, float, float, float);
typedef void (T_GLAPI *gl_clear_fn)(unsigned);
#define T_GL_COLOR_BUFFER_BIT 0x00004000u

/* The smallest the window goes, at a scale of 1: the saves page's table
 * and its row of buttons, two lines of them, with nothing cut off. */
#define MIN_W 560
#define MIN_H 400

#define SETTLE_MS 400   /* frames kept coming after input, for hovers and popups to settle */
#define IDLE_MS   1000  /* the longest the window waits to draw again when idle */

static const tools_page *const g_pages[] = { &tools_saves_page, &tools_missions_page };
#define NPAGES (int)(sizeof g_pages / sizeof g_pages[0])

/* ---- the shell's helpers ------------------------------------------------------------- */

bool tools_flow_button(const char *label, bool first)
{
    ImGuiStyle &st = ImGui::GetStyle();
    float w = ImGui::CalcTextSize(label, NULL, true).x + st.FramePadding.x * 2;
    if (!first) {
        float right = ImGui::GetItemRectMax().x + st.ItemSpacing.x + w;
        float edge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (right <= edge) ImGui::SameLine();
    }
    return ImGui::Button(label);
}

bool tools_warning(const char *id, const char *text, const char *go)
{
    bool pressed = false;
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.8f, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(id, NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("%s", text);
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
        if (ImGui::Button(go)) { pressed = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return pressed;
}

/* ---- finding the game ---------------------------------------------------------------- */

static int has_launcher(const char *dir)
{
    char p[JSRF_PATH_MAX];
    js_path(p, sizeof p, dir, LAUNCHER_NAME);
    return js_is_file(p);
}

/* Where JSRF-PC keeps the game's saves and its settings, from the
 * launcher's folder: its portable folder (portable\saves, beside
 * portable\settings.cfg), or, without one, the folder of the game it
 * remembers (game_dir, in %LOCALAPPDATA%\JSRF-PC\settings or
 * ~/.config/jsrf-pc/settings), where the engine keeps UDATA. Read only.
 *
 * For now the launchers' own rules, as b2.2 has them; the thread "Make
 * portable mode the default for b3" is moving them into one shared unit
 * (launcher/common/user_files.c), whose read-only find() this becomes, and
 * which adds b3's data\ folder. 1 found; 0 when the launcher has never been
 * given the game. */
static int find_saves(tools_ctx *t)
{
    char p[JSRF_PATH_MAX], cfg[JSRF_PATH_MAX], v[JSRF_PATH_MAX];
    int portable;
    t->saves[0] = t->game_files[0] = t->settings[0] = 0;
    js_path(p, sizeof p, t->game, "portable");
    portable = js_is_dir(p);
    if (portable) {
        js_path(t->saves, sizeof t->saves, p, "saves");
        js_path(cfg, sizeof cfg, p, "settings.cfg");
    } else {
        char base[JSRF_PATH_MAX];
#ifdef _WIN32
        if (!jsrf_env("LOCALAPPDATA", base, sizeof base)) return 0;
        js_path(p, sizeof p, base, "JSRF-PC");
#else
        if (jsrf_env("XDG_CONFIG_HOME", base, sizeof base)) js_path(p, sizeof p, base, "jsrf-pc");
        else if (jsrf_env("HOME", base, sizeof base)) js_path(p, sizeof p, base, ".config/jsrf-pc");
        else return 0;
#endif
        js_path(cfg, sizeof cfg, p, "settings");
    }
    snprintf(t->settings, sizeof t->settings, "%s", p);
    if (js_read_kv(cfg, "game_dir", v, sizeof v) && v[0]) {
        /* In portable mode a game inside the folder is kept relative to it. */
        if (v[0] != '/' && v[0] != '\\' && !(v[0] && v[1] == ':')) js_path(t->game_files, sizeof t->game_files, t->game, v);
        else snprintf(t->game_files, sizeof t->game_files, "%s", v);
        if (!portable) snprintf(t->saves, sizeof t->saves, "%s", t->game_files);
    }
    if (!t->saves[0]) return 0;
    js_path(p, sizeof p, t->saves, "UDATA");
    js_path(t->title, sizeof t->title, p, JSRF_TITLE_ID);
    js_path(t->backups, sizeof t->backups, t->saves, "Save backups");
    return 1;
}

/* ---- the window ---------------------------------------------------------------------- */

/* The display's scale: its DPI over 96, in quarters, from 1 to 3. */
static float display_scale(SDL_Window *w)
{
    float dpi = 0, s;
    int d = SDL_GetWindowDisplayIndex(w);
    if (d < 0 || SDL_GetDisplayDPI(d, &dpi, NULL, NULL) != 0 || dpi <= 0) return 1;
    s = (float)(int)(dpi / 96.0f * 4.0f + 0.5f) / 4.0f;
    return s < 1 ? 1 : s > 3 ? 3 : s;
}

static int tools_main(int argc, char **argv)
{
    static tools_ctx t;
    SDL_Window *win;
    SDL_GLContext gl;
    ImGuiStyle base;
    float scale = 0;
    int page = 0, found = 0, quit = 0;
    Uint32 last_input = 0;
    char *here, note[JSRF_PATH_MAX + 400] = "";

    memset(&t, 0, sizeof t);
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "jsrf-pc-tools: no window (%s)\n", SDL_GetError());
        return 1;
    }
    /* JSRF-PC's folder: the one given, else the tool's own. */
    here = SDL_GetBasePath();
    snprintf(t.game, sizeof t.game, "%s", argc > 1 ? argv[1] : here ? here : ".");
    SDL_free(here);
    {
        size_t l = strlen(t.game);
        while (l > 1 && (t.game[l - 1] == '/' || t.game[l - 1] == '\\')) t.game[--l] = 0;
    }
    found = has_launcher(t.game) && find_saves(&t);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    win = SDL_CreateWindow("JSRF-PC Tools", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 900, 620,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!win) {
        fprintf(stderr, "jsrf-pc-tools: no window (%s)\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    gl = SDL_GL_CreateContext(win);
    if (!gl) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        gl = SDL_GL_CreateContext(win);
    }
    if (!gl) {
        fprintf(stderr, "jsrf-pc-tools: no OpenGL (%s)\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);
    gl_viewport_fn gl_viewport = (gl_viewport_fn)SDL_GL_GetProcAddress("glViewport");
    gl_clear_color_fn gl_clear_color = (gl_clear_color_fn)SDL_GL_GetProcAddress("glClearColor");
    gl_clear_fn gl_clear = (gl_clear_fn)SDL_GL_GetProcAddress("glClear");
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontDefaultVector();
    menu_style_apply(&ImGui::GetStyle());
    base = ImGui::GetStyle();
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init(NULL);
    SDL_ShowWindow(win);

    while (!quit) {
        SDL_Event ev;
        /* Idle: wait for something to happen, or a second. */
        if (!t.busy && SDL_GetTicks() - last_input > SETTLE_MS) SDL_WaitEventTimeout(NULL, IDLE_MS);
        while (SDL_PollEvent(&ev)) {
            last_input = SDL_GetTicks();
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE)) quit = 1;
            /* The smallest size, kept where the desktop doesn't keep it. */
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && scale > 0) {
                int w = ev.window.data1, h = ev.window.data2, mw = (int)(MIN_W * scale), mh = (int)(MIN_H * scale);
                if (w < mw || h < mh) SDL_SetWindowSize(win, w < mw ? mw : w, h < mh ? mh : h);
            }
            if (ev.type == SDL_DROPFILE) {
                if (found && g_pages[page]->dropped) g_pages[page]->dropped(&t, ev.drop.file);
                else if (!found && js_is_dir(ev.drop.file)) {
                    snprintf(t.game, sizeof t.game, "%s", ev.drop.file);
                    found = has_launcher(t.game) && find_saves(&t);
                }
                SDL_free(ev.drop.file);
            }
        }
        /* The scale follows the display the window is on. */
        {
            float sc = display_scale(win);
            if (sc != scale) {
                ImGuiStyle &st = ImGui::GetStyle();
                st = base;
                st.ScaleAllSizes(sc);
                st.FontScaleMain = sc * 1.2f;
                scale = sc;
                t.scale = sc;
                SDL_SetWindowMinimumSize(win, (int)(MIN_W * sc), (int)(MIN_H * sc));
            }
        }
        t.busy = 0;     /* a page sets it again while it draws, if it's still busy */
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("JSRF-PC Tools", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::TextUnformatted("JSRF-PC Tools");
        if (found) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("JSRF-PC's saves: %s", t.title);
            ImGui::PopStyleColor();
            if (ImGui::BeginTabBar("pages")) {
                for (int i = 0; i < NPAGES; i++) {
                    if (ImGui::BeginTabItem(g_pages[i]->name)) {
                        page = i;
                        ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None);
                        g_pages[i]->draw(&t);
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        } else {
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));
            ImGui::TextWrapped("Where is JSRF-PC? Pick the folder its launcher (%s) is in, or drop the "
                               "folder on this window. Start JSRF-PC Tools from that folder to skip this.",
                               LAUNCHER_NAME);
            if (note[0]) {
                ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
                ImGui::TextWrapped("%s", note);
            }
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
            if (ImGui::Button("Pick JSRF-PC's folder")) {
                char dir[JSRF_PATH_MAX];
                int r = jsrf_pick_folder("Where is JSRF-PC? Pick the folder its launcher is in", dir, sizeof dir);
                if (r == JSRF_NO_DIALOG)
                    snprintf(note, sizeof note, "There's no folder dialog here (zenity or kdialog): drop the "
                                                "folder on this window instead.");
                if (r == JSRF_PICKED) {
                    snprintf(t.game, sizeof t.game, "%s", dir);
                    if (!has_launcher(t.game))
                        snprintf(note, sizeof note, "%s has no %s in it.", dir, LAUNCHER_NAME);
                    else if (!(found = find_saves(&t)))
                        snprintf(note, sizeof note, "JSRF-PC in %s hasn't been given the game yet: play it "
                                                    "once, then come back.", dir);
                }
            }
        }
        ImGui::End();
        ImGui::Render();
        if (gl_viewport && gl_clear_color && gl_clear) {
            int dw, dh;
            SDL_GL_GetDrawableSize(win, &dw, &dh);
            gl_viewport(0, 0, dw, dh);
            gl_clear_color(0.06f, 0.06f, 0.08f, 1);
            gl_clear(T_GL_COLOR_BUFFER_BIT);
        }
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

#ifdef _WIN32
/* Windows: the arguments as UTF-8, from the wide command line, so that a
 * folder given in any language arrives as the rest of the tool takes paths
 * (setup.h's rules). */
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, PWSTR cmdline, int show)
{
    int argc = 0, i, r;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    char **argv;
    (void)hi; (void)prev; (void)cmdline; (void)show;
    if (!wargv) argc = 0;
    argv = (char **)calloc((size_t)argc + 1, sizeof *argv);
    if (!argv) return 1;
    for (i = 0; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        argv[i] = (char *)malloc(n > 0 ? (size_t)n : 1);
        if (!argv[i]) return 1;
        if (n <= 0 || !WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], n, NULL, NULL)) argv[i][0] = 0;
    }
    LocalFree(wargv);
    SDL_SetMainReady();
    r = tools_main(argc, argv);
    for (i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    return r;
}
#else
int main(int argc, char **argv)
{
    SDL_SetMainReady();
    return tools_main(argc, argv);
}
#endif
