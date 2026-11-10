/*
 * The setup's window (launcher/setup/setup.h): Dear ImGui over SDL2 and
 * OpenGL 3, as the in-game menu (src/menu), with the same look
 * (src/menu/menu_style.h). The work is setup_core.c's, on a thread of its
 * own; this draws where it is and asks the player what only they can
 * answer.
 *
 * With no window to open (or JSRF_SETUP_ANSWER set), the console's way
 * instead (setup_core.c's jsrf_setup_run_console).
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include "menu_style.h"
#include "setup.h"
#include "setup_job.h"

/* The three GL calls of the window's own, through SDL: nothing to link
 * against (Dear ImGui's backend loads the rest itself). */
#ifdef _WIN32
#define JS_GLAPI __stdcall
#else
#define JS_GLAPI
#endif
typedef void (JS_GLAPI *gl_viewport_fn)(int, int, int, int);
typedef void (JS_GLAPI *gl_clear_color_fn)(float, float, float, float);
typedef void (JS_GLAPI *gl_clear_fn)(unsigned);
#define JS_GL_COLOR_BUFFER_BIT 0x00004000u

/* What the worker tells the window, under g_mu. */
struct Shared {
    SDL_mutex *mu;
    double from, to;      /* the bar: from, moving towards to... */
    int secs;             /* ...over about this many seconds */
    Uint64 since;         /* when that step started */
    char text[512];       /* what is happening, for the player */
    char lines[8][256];   /* the log's last lines */
    int nlines;
    volatile int cancel;
    volatile int finished;
    int rc;
};
static Shared g;

static void on_update(void *ctx, double from, double to, int secs, const char *text, const char *log)
{
    (void)ctx;
    SDL_LockMutex(g.mu);
    if (from >= 0) {
        g.from = from;
        g.to = to >= 0 ? to : from;
        g.secs = secs;
        g.since = SDL_GetTicks64();
    }
    if (text) { strncpy(g.text, text, sizeof g.text - 1); g.text[sizeof g.text - 1] = 0; }
    if (log) {
        if (g.nlines == 8) { memmove(g.lines[0], g.lines[1], sizeof g.lines[0] * 7); g.nlines = 7; }
        strncpy(g.lines[g.nlines], log, sizeof g.lines[0] - 1);
        g.lines[g.nlines][sizeof g.lines[0] - 1] = 0;
        g.nlines++;
    }
    SDL_UnlockMutex(g.mu);
}

static int worker(void *arg)
{
    js_job *j = (js_job *)arg;
    int rc = js_job_run(j);
    if (j->log) { fclose(j->log); j->log = NULL; }
    SDL_LockMutex(g.mu);
    g.rc = rc;
    g.finished = 1;
    SDL_UnlockMutex(g.mu);
    return 0;
}

/* The bar: where the step started, easing towards where it ends, never
 * quite there until the next step says so. */
static float bar_now(void)
{
    double f;
    SDL_LockMutex(g.mu);
    f = g.from;
    if (g.to > g.from && g.secs > 0) {
        double t = (double)(SDL_GetTicks64() - g.since) / 1000.0 / g.secs;
        double e = t < 1 ? t * 0.9 : 0.9 + 0.09 * (1 - 1 / (1 + (t - 1)));
        f = g.from + (g.to - g.from) * e;
    }
    SDL_UnlockMutex(g.mu);
    return (float)(f < 0 ? 0 : f > 1 ? 1 : f);
}

enum Step { ASK, WORK, FAILED, MISMATCH, FINISH, DONE };

static void centre_text(const char *t)
{
    float w = ImGui::CalcTextSize(t).x;
    float avail = ImGui::GetContentRegionAvail().x;
    if (w < avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w) * 0.5f);
    ImGui::TextUnformatted(t);
}

static int two_buttons(const char *a, const char *b)
{
    ImGuiStyle &st = ImGui::GetStyle();
    float bw = ImGui::GetFontSize() * 7.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - bw * 2 - st.ItemSpacing.x) * 0.5f);
    if (ImGui::Button(a, ImVec2(bw, 0))) return 1;
    ImGui::SameLine();
    if (ImGui::Button(b, ImVec2(bw, 0))) return 2;
    return 0;
}

extern "C" int jsrf_setup_run(jsrf_setup *s)
{
    SDL_Window *win;
    SDL_GLContext gl;
    SDL_Thread *th = NULL;
    static js_job job;
    Step step = ASK;
    int result = JSRF_SETUP_CLOSED, local, quit = 0;
    char msg[sizeof job.err + sizeof job.log_path + 64] = "", sizes[128] = "";
    ImGuiStyle base;
    float scale = 0;

    if (getenv("JSRF_SETUP_ANSWER")) return jsrf_setup_run_console(s);
    local = js_tools_ready(s->root);
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "setup: no window (%s): the console's way instead\n", SDL_GetError());
        return jsrf_setup_run_console(s);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    win = SDL_CreateWindow("JSRF-PC", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 720, 400,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "setup: no window (%s): the console's way instead\n", SDL_GetError());
        SDL_Quit();
        return jsrf_setup_run_console(s);
    }
    gl = SDL_GL_CreateContext(win);
    if (!gl) {
        /* OpenGL 3.0 not there: 2.1, which Dear ImGui's backend also draws with. */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        gl = SDL_GL_CreateContext(win);
    }
    if (!gl) {
        fprintf(stderr, "setup: no OpenGL (%s): the console's way instead\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return jsrf_setup_run_console(s);
    }
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);
    gl_viewport_fn gl_viewport = (gl_viewport_fn)SDL_GL_GetProcAddress("glViewport");
    gl_clear_color_fn gl_clear_color = (gl_clear_color_fn)SDL_GL_GetProcAddress("glClearColor");
    gl_clear_fn gl_clear = (gl_clear_fn)SDL_GL_GetProcAddress("glClear");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.Fonts->AddFontDefaultVector();
    menu_style_apply(&ImGui::GetStyle());
    base = ImGui::GetStyle();
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init(NULL);

    memset(&g, 0, sizeof g);
    g.mu = SDL_CreateMutex();

    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE)) {
                if (step == WORK) {
                    /* Closing the window stops the build: the old engine,
                     * if any, stays as it was. */
                    g.cancel = 1;
                    js_kill_child();
                }
                quit = 1;
                if (step != DONE) result = step == WORK ? JSRF_SETUP_CLOSED : result;
            }
        }
        if (step == WORK && g.finished) {
            SDL_WaitThread(th, NULL);
            th = NULL;
            if (g.rc != 0) {
                snprintf(msg, sizeof msg, "%s\n\nThe whole log is in:\n%s", job.err, job.log_path);
                step = FAILED;
            } else if (!strcmp(job.engine_check, "mismatch")) {
                step = MISMATCH;
            } else {
                double mb = (double)js_tools_size(s->root) / 1e6;
                snprintf(sizes, sizeof sizes, "%.0f MB", mb);
                step = FINISH;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        {
            float sc = io.DisplaySize.y / 400.0f;
            if (sc < 1) sc = 1;
            if (sc != scale) {
                ImGuiStyle &st = ImGui::GetStyle();
                st = base;
                st.ScaleAllSizes(sc);
                st.FontScaleMain = sc * 1.35f;
                scale = sc;
            }
        }
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("setup", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 1.5f));
        centre_text("JSRF-PC");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));

        switch (step) {
        case ASK: {
            centre_text("No matching binary detected.");
            centre_text(local ? "Build now with local tools?" : "Download tools now and build?");
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));
            int b = two_buttons("Yes", "No");
            if (b == 2) { result = JSRF_SETUP_CLOSED; quit = 1; }
            if (b == 1) {
                /* Where the game is: the launcher's own way (its dialog). */
                if (!s->find_game || !s->find_game(s->ctx, s->game, sizeof s->game)) {
                    result = JSRF_SETUP_CLOSED;
                    quit = 1;
                    break;
                }
                memset(&job, 0, sizeof job);
                job.s = s;
                job.download = !local;
                job.prog.update = on_update;
                job.prog.cancel = &g.cancel;
                g.from = g.to = 0;
                g.finished = 0;
                snprintf(g.text, sizeof g.text, "%s", local ? "Starting" : "Downloading the tools");
                th = SDL_CreateThread(worker, "setup", &job);
                step = WORK;
            }
            break;
        }
        case WORK: {
            char t[512];
            float f = bar_now();
            SDL_LockMutex(g.mu);
            snprintf(t, sizeof t, "%s", g.text);
            SDL_UnlockMutex(g.mu);
            centre_text("Building the engine from your copy of the game.");
            centre_text("This takes a few minutes, once.");
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));
            ImGui::ProgressBar(f, ImVec2(-1, 0));
            ImGui::TextWrapped("%s", t);
            break;
        }
        case FAILED:
            centre_text("The engine couldn't be built.");
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
            ImGui::TextWrapped("%s", msg);
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));
            {
                float bw = ImGui::GetFontSize() * 7.0f;
                ImGui::SetCursorPosX((ImGui::GetWindowWidth() - bw) * 0.5f);
                if (ImGui::Button("Close", ImVec2(bw, 0))) { result = JSRF_SETUP_FAILED; quit = 1; }
            }
            break;
        case MISMATCH:
            centre_text("The engine built here isn't the one this release builds.");
            ImGui::TextWrapped("It may still work: the game you have may differ from the one the "
                               "release was made with. Its SHA-256 is %s. The log is in:\n%s",
                               job.engine_sha, job.log_path);
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));
            centre_text("Play anyway?");
            {
                int b = two_buttons("Yes", "No");
                if (b == 1) {
                    js_accept_engine(s->root, job.engine_sha);
                    snprintf(sizes, sizeof sizes, "%.0f MB", (double)js_tools_size(s->root) / 1e6);
                    step = FINISH;
                }
                if (b == 2) { result = JSRF_SETUP_FAILED; quit = 1; }
            }
            break;
        case FINISH:
            centre_text("The engine is built.");
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
            ImGui::TextWrapped("Delete the tools and the code made from your game (%s)? "
                               "The engine stays. Keep them to build again faster, for a "
                               "new release.", sizes);
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));
            {
                int b = two_buttons("Delete", "Keep");
                if (b == 1) js_delete_tools(s->root);
                if (b) { result = JSRF_SETUP_READY; step = DONE; quit = 1; }
            }
            break;
        case DONE:
            break;
        }
        ImGui::End();
        ImGui::Render();
        if (gl_viewport && gl_clear_color && gl_clear) {
            int dw, dh;
            SDL_GL_GetDrawableSize(win, &dw, &dh);
            gl_viewport(0, 0, dw, dh);
            gl_clear_color(0.06f, 0.06f, 0.08f, 1);
            gl_clear(JS_GL_COLOR_BUFFER_BIT);
        }
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
        if (step == WORK) SDL_Delay(15);
    }
    if (th) {
        g.cancel = 1;
        js_kill_child();
        SDL_WaitThread(th, NULL);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_DestroyMutex(g.mu);
    SDL_Quit();
    return result;
}
