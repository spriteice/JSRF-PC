/*
 * JSRF-PC Tools (tools.h): the window. Dear ImGui over SDL2 and OpenGL 3,
 * in the in-game menu's look (src/menu/menu_style.h), as the launcher's
 * setup window is.
 *
 *   Finding the game: started in JSRF-PC's folder (beside the launcher), it
 *     knows where the game's saves are; started anywhere else, it asks. A
 *     JSRF-PC that hasn't been given the game yet has no saves: the pages
 *     that need them say so, and the build page works all the same.
 *   Adding files: dropped on the window, or picked with the system's own
 *     dialog (file_dialog.c), handed to the page that shows.
 *   Writing: only from a page's own button, after the shell's warning
 *     (tools_warning), and after a dated backup.
 *   Size: the window resizes freely, down to a size where nothing is cut
 *     off; the layout reflows, and text and controls follow the scale, not
 *     the window's size. The scale is the system's (auto_scale), or the one
 *     picked at the top right (Scale, or Ctrl and + or -), remembered in
 *     tools.cfg: for a screen where the system's comes out wrong.
 *   Idle: the window draws while something happens (input, and a moment
 *     after it for the interface to settle) or a page is busy (tools_ctx's
 *     busy), and otherwise once a second, so an open window costs nearly
 *     nothing (a Steam Deck's battery among others).
 *
 *   Scripts: with --script, the steps in a file are played on the window
 *     (tools_script.cpp), for a test run with nobody at the machine.
 *
 *   jsrf-pc-tools [JSRF-PC's folder] [--script <steps> [--out <folder>]]
 */
#define SDL_MAIN_HANDLED   /* the entry point is the tool's own (below), with UTF-8 arguments */
#include <SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include "menu_style.h"
#include "file_dialog.h"
#include "graffiti_save.h"
#include "setup.h"
#include "setup_job.h"
#include "tools.h"
#include "../common/user_files.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#define T_GLAPI __stdcall
#define LAUNCHER_NAME "JSRF-PC.exe"
#define TOOLS_SEP "\\"
#else
#define T_GLAPI
#define LAUNCHER_NAME "jsrf-pc"
#define TOOLS_SEP "/"
#endif
typedef void (T_GLAPI *gl_viewport_fn)(int, int, int, int);
typedef void (T_GLAPI *gl_clear_color_fn)(float, float, float, float);
typedef void (T_GLAPI *gl_clear_fn)(unsigned);
#define T_GL_COLOR_BUFFER_BIT 0x00004000u

/* The smallest the window goes, at a scale of 1: the save manager's tables
 * and its row of buttons, two lines of them, with nothing cut off. */
#define MIN_W 560
#define MIN_H 400

#define SETTLE_MS 400   /* frames kept coming after input, for hovers and popups to settle */

#define TITLE_SIZE    1.45f   /* the window's title, over the font's size */
#define HEADING_SIZE  1.25f   /* a section's heading (tools_heading) */
#define TAB_SIZE      1.15f   /* the pages' tabs */
#define TAB_PADDING   1.5f    /* and the room around their names, over a button's */

/* The size the window opens at, at a scale of 1, before it is made tall
 * enough for the page it opens on (FIT_FRAMES); then kept within the
 * screen. */
#define START_W 900
#define START_H 620
#define FIT_FRAMES 4

/* The scales that can be picked (Scale, at the top right), over the tool's
 * own size; Auto is the system's. Kept in tools.cfg as "scale", in percent
 * or "auto". */
static const float SCALES[] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f };
#define NSCALES (int)(sizeof SCALES / sizeof SCALES[0])
#define SCALE_KEY "scale"
#define IDLE_MS   1000  /* the longest the window waits to draw again when idle */
#define SCRIPT_FRAME_MS 16  /* a frame's time while a script plays */

static const tools_page *const g_pages[] = { &tools_saves_page, &tools_graffiti_page, &tools_missions_page,
                                              &tools_build_page };
#define NPAGES (int)(sizeof g_pages / sizeof g_pages[0])

/* Every page but the build page works on the game's saves. */
static int needs_saves(const tools_page *p)
{
    return p != &tools_build_page;
}

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

bool tools_name_prompt(const char *id, const char *text, const char *go, char *buf, size_t n)
{
    bool pressed = false;
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.8f, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(id, NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        bool enter;
        if (text && text[0]) ImGui::TextWrapped("%s", text);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        enter = ImGui::InputTextWithHint("##name", "Name (or leave it empty)", buf, n,
                                         ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
        if (ImGui::Button(go) || enter) { pressed = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return pressed;
}

/* ---- the tool's settings --------------------------------------------------------------- */

static void settings_file(const tools_ctx *t, char *out, size_t n)
{
    js_path(out, n, t->settings, "tools.cfg");
}

int tools_setting(const tools_ctx *t, const char *key, char *out, size_t n)
{
    char p[JSRF_PATH_MAX];
    if (n) out[0] = 0;
    if (!t->settings[0]) return 0;
    settings_file(t, p, sizeof p);
    return js_read_kv(p, key, out, n);
}

void tools_set_setting(const tools_ctx *t, const char *key, const char *value)
{
    char p[JSRF_PATH_MAX];
    unsigned char *old = NULL;
    size_t size = 0, kl = strlen(key);
    ImGuiTextBuffer out;
    if (!t->settings[0]) return;
    settings_file(t, p, sizeof p);
    if (js_read_file(p, 1u << 20, &old, &size) == 0 && old) {
        /* Every other line kept as it is. */
        const char *at = (const char *)old, *end = at + size;
        while (at < end) {
            const char *nl = (const char *)memchr(at, '\n', (size_t)(end - at));
            const char *next = nl ? nl + 1 : end;
            if (!((size_t)(next - at) > kl && !strncmp(at, key, kl) && at[kl] == '=')) out.append(at, next);
            if (!nl && next > at) out.append("\n");
            at = next;
        }
    }
    free(old);
    out.appendf("%s=", key);
    for (const char *v = value; *v; v++) {
        char c[2] = { *v == '\n' || *v == '\r' ? ' ' : *v, 0 };
        out.append(c);
    }
    out.append("\n");
    js_mkdirs(t->settings);
    js_write_file(p, out.c_str(), (size_t)out.size());
}

/* ---- pictures, for the pages ------------------------------------------------------------- */

typedef void (T_GLAPI *gen_textures_fn)(int, unsigned *);
typedef void (T_GLAPI *delete_textures_fn)(int, const unsigned *);
typedef void (T_GLAPI *bind_texture_fn)(unsigned, unsigned);
typedef void (T_GLAPI *tex_parameteri_fn)(unsigned, unsigned, int);
typedef void (T_GLAPI *tex_image_2d_fn)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
typedef void (T_GLAPI *pixel_storei_fn)(unsigned, int);

static struct {
    int tried, ok;
    gen_textures_fn gen;
    delete_textures_fn del;
    bind_texture_fn bind;
    tex_parameteri_fn param;
    tex_image_2d_fn image;
    pixel_storei_fn store;
} g_gl;

static int gl_load(void)
{
    if (!g_gl.tried) {
        g_gl.tried = 1;
        g_gl.gen = (gen_textures_fn)SDL_GL_GetProcAddress("glGenTextures");
        g_gl.del = (delete_textures_fn)SDL_GL_GetProcAddress("glDeleteTextures");
        g_gl.bind = (bind_texture_fn)SDL_GL_GetProcAddress("glBindTexture");
        g_gl.param = (tex_parameteri_fn)SDL_GL_GetProcAddress("glTexParameteri");
        g_gl.image = (tex_image_2d_fn)SDL_GL_GetProcAddress("glTexImage2D");
        g_gl.store = (pixel_storei_fn)SDL_GL_GetProcAddress("glPixelStorei");
        g_gl.ok = g_gl.gen && g_gl.del && g_gl.bind && g_gl.param && g_gl.image && g_gl.store;
    }
    return g_gl.ok;
}

unsigned tools_texture_put(unsigned tex, const unsigned char *rgba, int w, int h)
{
    if (!gl_load() || !rgba) return tex;
    if (!tex) g_gl.gen(1, &tex);
    g_gl.bind(0x0DE1u, tex);                               /* GL_TEXTURE_2D */
    g_gl.param(0x0DE1u, 0x2801u, 0x2600);                  /* MIN_FILTER: NEAREST, the design's own pixels */
    g_gl.param(0x0DE1u, 0x2800u, 0x2600);                  /* MAG_FILTER */
    g_gl.param(0x0DE1u, 0x2802u, 0x812F);                  /* WRAP_S: CLAMP_TO_EDGE */
    g_gl.param(0x0DE1u, 0x2803u, 0x812F);                  /* WRAP_T */
    g_gl.store(0x0CF5u, 4);                                /* UNPACK_ALIGNMENT */
    g_gl.store(0x0CF2u, 0);                                /* UNPACK_ROW_LENGTH */
    g_gl.image(0x0DE1u, 0, 0x1908, w, h, 0, 0x1908u, 0x1401u, rgba);   /* RGBA, UNSIGNED_BYTE */
    g_gl.bind(0x0DE1u, 0);
    return tex;
}

void tools_texture_drop(unsigned *tex)
{
    if (*tex && gl_load()) g_gl.del(1, tex);
    *tex = 0;
}

/* ---- the shared look ------------------------------------------------------------------ */

void tools_heading(const char *text)
{
    const ImGuiStyle &st = ImGui::GetStyle();
    if (ImGui::GetCursorPosY() > st.WindowPadding.y + 1)
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
    /* As ImGui's SeparatorText lays it out, larger, and bold: the default
     * font has no bold of its own, so the text is drawn twice, a pixel or
     * two apart. */
    tools_script_heading(text);
    ImGui::PushFont(NULL, st.FontSizeBase * HEADING_SIZE);
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 at = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(text, NULL, true);
        float w = ImGui::GetContentRegionAvail().x, fs = ImGui::GetFontSize();
        float bold = floorf(fs / 14.0f), h = ts.y + st.SeparatorTextPadding.y * 2;
        float tx = floorf(at.x + st.SeparatorTextPadding.x), ty = floorf(at.y + st.SeparatorTextPadding.y);
        float y = floorf(at.y + h * 0.5f + 0.99999f), thick = st.SeparatorTextBorderSize;
        ImU32 green = ImGui::GetColorU32(ImGuiCol_CheckMark), line = ImGui::GetColorU32(ImGuiCol_Separator);
        if (bold < 1) bold = 1;
        if (thick > 0 && tx - st.ItemSpacing.x > at.x)
            dl->AddLine(ImVec2(at.x, y), ImVec2(tx - st.ItemSpacing.x, y), line, thick);
        dl->AddText(ImVec2(tx, ty), green, text);
        dl->AddText(ImVec2(tx + bold, ty), green, text);
        if (thick > 0 && tx + ts.x + bold + st.ItemSpacing.x < at.x + w)
            dl->AddLine(ImVec2(tx + ts.x + bold + st.ItemSpacing.x, y), ImVec2(at.x + w, y), line, thick);
        ImGui::Dummy(ImVec2(w > 0 ? w : 1, h));
    }
    ImGui::PopFont();
}

/* Where the last hint's last line ends, for a help marker after it. */
static struct { ImVec2 end; ImVec2 item_max; int frame; } g_hint;

void tools_hint(const char *fmt, ...)
{
    char buf[2048];
    const char *line, *end;
    float wrap = ImGui::GetContentRegionAvail().x;
    ImVec2 at = ImGui::GetCursorScreenPos();
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", buf);
    ImGui::PopStyleColor();
    /* The last line, as TextWrapped broke it. */
    end = buf + strlen(buf);
    line = buf;
    for (int guard = 0; guard < 200; guard++) {
        const char *next = ImGui::GetFont()->CalcWordWrapPosition(ImGui::GetFontSize(), line, end, wrap);
        if (next >= end || next <= line) break;
        line = next;
        while (line < end && (*line == ' ' || *line == '\n')) line++;
    }
    g_hint.end = ImVec2(at.x + ImGui::CalcTextSize(line, end).x, ImGui::GetItemRectMax().y - ImGui::GetTextLineHeight());
    g_hint.item_max = ImGui::GetItemRectMax();
    g_hint.frame = ImGui::GetFrameCount();
}

void tools_help_marker(const char *text)
{
    float w = ImGui::CalcTextSize("(?)").x, gap = ImGui::GetStyle().ItemSpacing.x;
    float edge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    ImVec2 after = ImGui::GetItemRectMax();
    if (g_hint.frame == ImGui::GetFrameCount() && after.x == g_hint.item_max.x && after.y == g_hint.item_max.y) {
        /* After a hint: at the end of its last line, or under it. */
        if (g_hint.end.x + gap + w <= edge) {
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(g_hint.end.x + gap, g_hint.end.y));
        }
    } else if (after.x + gap + w <= edge) {
        ImGui::SameLine();
    }
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void tools_path(const char *label, const char *path)
{
    const char *cut = path, *p;
    int seps = 0;
    char shown[JSRF_PATH_MAX + 8];
    if (label && label[0]) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
    }
    if (!path || !path[0]) {
        ImGui::TextDisabled("not found");
        return;
    }
    /* Its last two parts. */
    for (p = path + strlen(path); p > path; p--)
        if ((p[-1] == '/' || p[-1] == '\\') && ++seps == 2) { cut = p; break; }
    snprintf(shown, sizeof shown, "%s%s", cut != path ? "..." TOOLS_SEP : "", cut);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(shown);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", path);
}

void tools_status(int kind, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    ImVec4 c = kind == TOOLS_OK ? ImVec4(0.55f, 0.85f, 0.35f, 1) : kind == TOOLS_WARN ? ImVec4(0.95f, 0.75f, 0.3f, 1)
                                                                                    : ImVec4(0.95f, 0.4f, 0.35f, 1);
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextWrapped("%s", buf);
    ImGui::PopStyleColor();
}

/* ---- finding the game ---------------------------------------------------------------- */

static int has_launcher(const char *dir)
{
    char p[JSRF_PATH_MAX];
    js_path(p, sizeof p, dir, LAUNCHER_NAME);
    return js_is_file(p);
}

/* Where JSRF-PC keeps the game's saves and its settings, from the
 * launcher's folder, as the launchers find them (launcher/common/user_files.c,
 * whose js_files_find only looks): its data folder (data\saves, beside
 * data\settings.cfg), b2.2's portable folder not yet renamed to it, or,
 * with neither, the folder of the game it remembers (game_dir, in
 * %LOCALAPPDATA%\JSRF-PC\settings or ~/.config/jsrf-pc/settings), where the
 * engine keeps UDATA. t->settings is the folder the settings file is in,
 * where the engine keeps its other files and this tool its tools.cfg. With
 * both a data and a portable folder, the launchers stop until one is moved
 * away; this reads the data folder, as js_files_find does. 1 found; 0 when
 * the launcher has never been given the game. */
static int find_saves(tools_ctx *t)
{
    static js_files f;   /* nine whole paths, kept off the stack */
    char p[JSRF_PATH_MAX], *sep;
    t->saves[0] = t->game_files[0] = t->settings[0] = 0;
    js_files_find(t->game, &f);
    snprintf(t->settings, sizeof t->settings, "%s", f.settings);
    sep = strrchr(t->settings, '/');
#ifdef _WIN32
    if (strrchr(t->settings, '\\') > sep) sep = strrchr(t->settings, '\\');
#endif
    if (sep) *sep = 0;
    snprintf(t->game_files, sizeof t->game_files, "%s", f.game);
    snprintf(t->saves, sizeof t->saves, "%s", f.saves[0] ? f.saves : f.game);
    if (!t->saves[0]) return 0;
    js_path(p, sizeof p, t->saves, "UDATA");
    js_path(t->title, sizeof t->title, p, JSRF_TITLE_ID);
    js_path(t->backups, sizeof t->backups, t->saves, "Save backups");
    return 1;
}

/* ---- the window ---------------------------------------------------------------------- */

#ifndef _WIN32
/* The desktop's scale, as Linux desktops hand it to X11 programs: Xft.dpi
 * over 96 (Plasma and GNOME set it from their own scale setting); 0 where
 * it isn't set. SDL2 doesn't pass it on: with XRandR its DPI is the
 * screen's own, from its size in millimetres, which on a handheld's small
 * screen (the Ally X's 7 inches at 1080p: about 315) is three times a
 * desktop monitor's. Read once, over a connection of its own. */
static float xft_scale(void)
{
    typedef void *(*open_display_fn)(const char *);
    typedef char *(*get_default_fn)(void *, const char *, const char *);
    typedef int (*close_display_fn)(void *);
    static int done;
    static float s;
    void *lib, *dpy;
    if (done) return s;
    done = 1;
    lib = SDL_LoadObject("libX11.so.6");     /* SDL's own, already loaded; left so */
    if (!lib) return 0;
    open_display_fn open_display = (open_display_fn)SDL_LoadFunction(lib, "XOpenDisplay");
    get_default_fn get_default = (get_default_fn)SDL_LoadFunction(lib, "XGetDefault");
    close_display_fn close_display = (close_display_fn)SDL_LoadFunction(lib, "XCloseDisplay");
    if (open_display && get_default && close_display && (dpy = open_display(NULL)) != NULL) {
        const char *v = get_default(dpy, "Xft", "dpi");
        double dpi = v ? atof(v) : 0;
        if (dpi >= 48 && dpi <= 960) s = (float)(dpi / 96.0);
        close_display(dpy);
    }
    return s;
}
#endif

/* The scale the system asks for (Scale's Auto), in quarters, from 1 to 3.
 * Windows: the display's own scale setting (its DPI over 96, as a
 * per-monitor DPI-aware program is told it). Linux under X11: the
 * desktop's (xft_scale), else 1. Under Wayland: 1, as the desktop scales
 * the window itself (SDL's high-DPI drawable, which ImGui draws its text
 * to at full resolution). */
static float auto_scale(SDL_Window *w)
{
    float s = 1;
#ifdef _WIN32
    float dpi = 0;
    int d = SDL_GetWindowDisplayIndex(w);
    if (d >= 0 && SDL_GetDisplayDPI(d, &dpi, NULL, NULL) == 0 && dpi > 0) s = dpi / 96.0f;
#else
    const char *driver = SDL_GetCurrentVideoDriver();
    (void)w;
    if (driver && !strcmp(driver, "x11") && xft_scale() > 0) s = xft_scale();
#endif
    s = (float)(int)(s * 4.0f + 0.5f) / 4.0f;
    return s < 1 ? 1 : s > 3 ? 3 : s;
}

static int percent(float s)
{
    return (int)(s * 100.0f + 0.5f);
}

/* The scale picked, from tools.cfg: an index into SCALES, the nearest to
 * the percent kept; -1 for Auto. */
static int scale_setting(const tools_ctx *t)
{
    char v[32];
    int pct, best = -1;
    if (!tools_setting(t, SCALE_KEY, v, sizeof v) || !v[0] || !strcmp(v, "auto")) return -1;
    pct = atoi(v);
    if (pct <= 0) return -1;
    for (int i = 0; i < NSCALES; i++)
        if (best < 0 || abs(percent(SCALES[i]) - pct) < abs(percent(SCALES[best]) - pct)) best = i;
    return best;
}

/* The next scale up (dir 1) or down (-1) from the one showing. */
static int scale_step(float now, int dir)
{
    if (dir > 0) {
        for (int i = 0; i < NSCALES; i++)
            if (SCALES[i] > now + 0.01f) return i;
        return NSCALES - 1;
    }
    for (int i = NSCALES - 1; i >= 0; i--)
        if (SCALES[i] < now - 0.01f) return i;
    return 0;
}

/* Scale, at the right of the title's line (under it when the window is too
 * narrow), and its keys: Ctrl and + or - a step, Ctrl and 0 Auto. Returns
 * the scale picked: an index into SCALES, or -1 for Auto. */
static int scale_picker(int chosen, float now, float auto_sc, float line_y, float line_h)
{
    const ImGuiStyle &st = ImGui::GetStyle();
    ImGuiIO &io = ImGui::GetIO();
    char shown[32], item[32];
    int pick = chosen;
    float cw = ImGui::CalcTextSize("Auto (300%)").x + ImGui::GetFrameHeight() + st.FramePadding.x * 2;
    float lw = ImGui::CalcTextSize("Scale").x;
    float x = ImGui::GetWindowContentRegionMax().x - cw - st.ItemSpacing.x - lw;
    float after = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + st.ItemSpacing.x;
    if (x >= after) {
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(x, line_y + (line_h - ImGui::GetFrameHeight()) * 0.5f));
    }
    if (chosen < 0) snprintf(shown, sizeof shown, "Auto (%d%%)", percent(auto_sc));
    else snprintf(shown, sizeof shown, "%d%%", percent(SCALES[chosen]));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Scale");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(cw);
    bool open = ImGui::BeginCombo("##scale", shown, ImGuiComboFlags_HeightLargest);
    tools_script_name(ImGui::GetID("##scale"), "Scale");
    if (open) {
        snprintf(item, sizeof item, "Auto (%d%%)", percent(auto_sc));
        if (ImGui::Selectable(item, chosen < 0)) pick = -1;
        for (int i = 0; i < NSCALES; i++) {
            snprintf(item, sizeof item, "%d%%", percent(SCALES[i]));
            if (ImGui::Selectable(item, chosen == i)) pick = i;
        }
        ImGui::EndCombo();
    } else {
        ImGui::SetItemTooltip("How big the text and buttons are. Auto follows the system's scale; pick another if "
                              "that's too big or too small for this screen. Ctrl and + or - change it a step, and "
                              "Ctrl and 0 goes back to Auto.");
    }
    if (io.KeyCtrl && !io.KeyAlt && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))
            pick = scale_step(now, 1);
        else if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))
            pick = scale_step(now, -1);
        else if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false))
            pick = -1;
    }
    return pick;
}

/* The room the window has on its display: the display less the taskbar
 * and the window's own frame. */
static void room_for(SDL_Window *w, float sc, int *mw, int *mh, int *x0, int *y0)
{
    SDL_Rect r;
    int top = 0, left = 0, bottom = 0, right = 0, d = SDL_GetWindowDisplayIndex(w);
    if (d < 0 || SDL_GetDisplayUsableBounds(d, &r) != 0) { r.x = r.y = 0; r.w = 1280; r.h = 720; }
    if (SDL_GetWindowBordersSize(w, &top, &left, &bottom, &right) != 0 || top <= 0) {
        top = (int)(32 * sc);                   /* not known until it is shown, on some desktops */
        left = right = bottom = (int)(8 * sc);
    }
    *mw = r.w - left - right;
    *mh = r.h - top - bottom;
    *x0 = r.x + left;
    *y0 = r.y + top;
}

/* The smallest the window goes at a scale, within the room it has. */
static void min_size(SDL_Window *w, float sc, int *mw, int *mh)
{
    int rw, rh, x0, y0;
    room_for(w, sc, &rw, &rh, &x0, &y0);
    *mw = (int)(MIN_W * sc);
    *mh = (int)(MIN_H * sc);
    if (*mw > rw) *mw = rw;
    if (*mh > rh) *mh = rh;
}

/* An argument meant as an option: one starting with a hyphen, or with a
 * dash or minus sign that a hyphen can turn into on the way (autocorrect,
 * or copied from formatted text), or --script or --out with nothing after
 * it. Never taken for JSRF-PC's folder. */
static int looks_like_option(const char *a)
{
    const unsigned char *u = (const unsigned char *)a;
    if (u[0] == '-') return 1;
    if (u[0] == 0xE2 && u[1] == 0x80 && u[2] >= 0x90 && u[2] <= 0x95) return 1;   /* U+2010 to U+2015 */
    return u[0] == 0xE2 && u[1] == 0x88 && u[2] == 0x92;                          /* U+2212 */
}

/* The window at w by h, within the room it has, in the middle of it. */
static void place_window(SDL_Window *win, float sc, int w, int h)
{
    int mw, mh, x0, y0;
    room_for(win, sc, &mw, &mh, &x0, &y0);
    if (w > mw) w = mw;
    if (h > mh) h = mh;
    SDL_SetWindowSize(win, w, h);
    SDL_SetWindowPosition(win, x0 + (mw - w) / 2, y0 + (mh - h) / 2);
}

static int tools_main(int argc, char **argv)
{
    static tools_ctx t;
    SDL_Window *win;
    SDL_GLContext gl;
    ImGuiStyle base;
    float scale = 0, overflow = 0, auto_sc = 1;
    int page = 0, launcher = 0, found = 0, quit = 0, fit_left = FIT_FRAMES, chosen = -1, refit = 0;
    Uint32 last_input = 0;
    char *here, note[JSRF_PATH_MAX + 400] = "";
    const char *folder = NULL, *script = NULL, *script_out = NULL, *unknown = NULL;

    memset(&t, 0, sizeof t);
    /* Graffiti saves re-signed and moved by the graffiti page's code, for
     * the save manager too. */
    jsrf_graffiti_resigner = jsrf_graffiti_resign;
    jsrf_graffiti_reslotter = jsrf_graffiti_reslot;
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "jsrf-pc-tools: no window (%s)\n", SDL_GetError());
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) script_out = argv[++i];
        else if (looks_like_option(argv[i])) { if (!unknown) unknown = argv[i]; }
        else if (!folder) folder = argv[i];
    }
    /* JSRF-PC's folder: the one given, else the tool's own. */
    here = SDL_GetBasePath();
    snprintf(t.game, sizeof t.game, "%s", folder ? folder : here ? here : ".");
    SDL_free(here);
    {
        size_t l = strlen(t.game);
        while (l > 1 && (t.game[l - 1] == '/' || t.game[l - 1] == '\\')) t.game[--l] = 0;
    }
    launcher = has_launcher(t.game);
    found = launcher && find_saves(&t);
    /* Why it asks, on the page that asks: a script's path given without
     * --script before it is otherwise taken for the folder, silently. */
    if (unknown && (!strcmp(unknown, "--script") || !strcmp(unknown, "--out")))
        snprintf(note, sizeof note, "%s needs a path after it.", unknown);
    else if (unknown && unknown[0] != '-')   /* a dash, which the window's font may not even show */
        snprintf(note, sizeof note, "An option starts with a dash where two hyphens should be, as when it is "
                                    "copied from formatted text. Type --script and --out with two hyphens.");
    else if (unknown)
        snprintf(note, sizeof note, "%s isn't an option this knows. To play a script: --script <steps> "
                                    "--out <folder>.", unknown);
    else if (!launcher && folder && !js_is_dir(folder))
        snprintf(note, sizeof note, "%s isn't a folder.%s", folder,
                 js_is_file(folder) ? " To play it as a script, put --script before it." : "");
    else if (!launcher)
        snprintf(note, sizeof note, "%s has no %s in it.", t.game, LAUNCHER_NAME);
    chosen = scale_setting(&t);

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
    {
        /* The menu's tabs, with the ones not showing plainer, so the page
         * that is showing stands out. */
        ImVec4 *c = ImGui::GetStyle().Colors;
        c[ImGuiCol_Tab] = c[ImGuiCol_TabDimmed] = c[ImGuiCol_FrameBg];
        c[ImGuiCol_TabDimmedSelected] = c[ImGuiCol_TabSelected];
        c[ImGuiCol_TabDimmedSelectedOverline] = c[ImGuiCol_TabSelectedOverline];
    }
    base = ImGui::GetStyle();
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init(NULL);
    /* Its size at its scale; shown once it is tall enough for the page it
     * opens on (below). */
    {
        float sc = chosen >= 0 ? SCALES[chosen] : auto_scale(win);
        place_window(win, sc, (int)(START_W * sc), (int)(START_H * sc));
    }
    if (script && tools_script_start(script, script_out) != 0) quit = 2;
    /* A script's frames don't wait for the screen: under XWayland a window
     * the screen isn't showing (turned off, or covered) may get a frame a
     * second. They're paced at about 60 a second instead. */
    if (tools_scripting()) SDL_GL_SetSwapInterval(0);

    while (!quit) {
        SDL_Event ev;
        /* Idle: wait for something to happen, or a second; busy, as long as
         * the page said it may. */
        if (tools_scripting()) {
            static Uint32 frame_at;
            Uint32 now = SDL_GetTicks();
            if (now - frame_at < SCRIPT_FRAME_MS) SDL_Delay(SCRIPT_FRAME_MS - (now - frame_at));
            frame_at = SDL_GetTicks();
        } else if (fit_left > 0) {
            /* still being fitted: no waiting */
        } else if (!t.busy && SDL_GetTicks() - last_input > SETTLE_MS) {
            SDL_WaitEventTimeout(NULL, IDLE_MS);
        } else if (t.busy && t.wait_ms > 0) {
            SDL_WaitEventTimeout(NULL, t.wait_ms);
        }
        while (SDL_PollEvent(&ev)) {
            if (tools_script_ignores(&ev)) continue;     /* nobody's input during a script */
            last_input = SDL_GetTicks();
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE)) quit = 1;
            /* The smallest size, kept where the desktop doesn't keep it. */
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && scale > 0) {
                int w = ev.window.data1, h = ev.window.data2, mw, mh;
                min_size(win, scale, &mw, &mh);
                if (w < mw || h < mh) SDL_SetWindowSize(win, w < mw ? mw : w, h < mh ? mh : h);
            }
            if (ev.type == SDL_DROPFILE) {
                if (launcher && (found || !needs_saves(g_pages[page])) && g_pages[page]->dropped)
                    g_pages[page]->dropped(&t, ev.drop.file);
                else if (!launcher && js_is_dir(ev.drop.file)) {
                    snprintf(t.game, sizeof t.game, "%s", ev.drop.file);
                    launcher = has_launcher(t.game);
                    found = launcher && find_saves(&t);
                }
                SDL_free(ev.drop.file);
            }
        }
        {
            /* A script's drop, as a drop on the window. */
            char dropped[JSRF_PATH_MAX];
            if (tools_script_take_drop(dropped, sizeof dropped) && launcher && (found || !needs_saves(g_pages[page])) &&
                g_pages[page]->dropped)
                g_pages[page]->dropped(&t, dropped);
            if (tools_script_wants_close()) quit = 1;
        }
        /* The scale picked, or the system's, which follows the display the
         * window is on. Picked anew, the window is made again as it opens
         * at that scale (unless it fills the screen). */
        auto_sc = auto_scale(win);
        {
            float sc = chosen >= 0 ? SCALES[chosen] : auto_sc;
            if (sc != scale) {
                ImGuiStyle &st = ImGui::GetStyle();
                int mw, mh;
                st = base;
                st.ScaleAllSizes(sc);
                st.FontScaleMain = sc * 1.2f;
                scale = sc;
                t.scale = sc;
                min_size(win, sc, &mw, &mh);
                SDL_SetWindowMinimumSize(win, mw, mh);
                if (refit && !(SDL_GetWindowFlags(win) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN))) {
                    place_window(win, sc, (int)(START_W * sc), (int)(START_H * sc));
                    fit_left = FIT_FRAMES;
                    overflow = 0;
                }
            }
            refit = 0;
        }
        t.busy = t.wait_ms = 0;     /* a page sets them again while it draws, if it's still busy */
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        tools_script_input(&t, fit_left == 0);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("JSRF-PC Tools", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        tools_script_frame_begin();
        {
            float y = ImGui::GetCursorPosY();
            int pick;
            ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * TITLE_SIZE);
            ImGui::TextUnformatted("JSRF-PC Tools");
            ImGui::PopFont();
            pick = scale_picker(chosen, scale, auto_sc, y, ImGui::GetItemRectSize().y);
            if (pick != chosen) {
                char v[16];
                chosen = pick;
                refit = 1;
                if (pick < 0) snprintf(v, sizeof v, "auto");
                else snprintf(v, sizeof v, "%d", percent(SCALES[pick]));
                tools_set_setting(&t, SCALE_KEY, v);
            }
        }
        if (launcher) {
            tools_path("JSRF-PC:", t.game);
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.25f));
            /* The tabs, larger than the text under them: the font and the
             * room around it, for the bar as for each tab. */
            ImVec2 tab_pad(ImGui::GetStyle().FramePadding.x * TAB_PADDING, ImGui::GetStyle().FramePadding.y * TAB_PADDING);
            ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * TAB_SIZE);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tab_pad);
            bool bar = ImGui::BeginTabBar("pages");
            ImGui::PopStyleVar();
            ImGui::PopFont();
            if (bar) {
                for (int i = 0; i < NPAGES; i++) {
                    ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * TAB_SIZE);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tab_pad);
                    bool open = ImGui::BeginTabItem(g_pages[i]->name);
                    ImGui::PopStyleVar();
                    ImGui::PopFont();
                    if (open) {
                        page = i;
                        ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None);
                        if (found || !needs_saves(g_pages[i])) {
                            g_pages[i]->draw(&t);
                        } else {
                            /* Its saves are where the game is, which the launcher is told at its first start. */
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                            ImGui::TextWrapped("JSRF-PC hasn't been given the game yet, so it has no saves. Play "
                                               "it once, then come back.");
                            ImGui::PopStyleColor();
                            if (ImGui::Button("Look again")) found = find_saves(&t);
                        }
                        /* How far the page goes past the window, while it
                         * is fitted to it. */
                        if (fit_left > 0) {
                            float below = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y -
                                          ImGui::GetWindowHeight();
                            if (below > overflow) overflow = below;
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        } else {
            tools_heading("Where is JSRF-PC?");
            tools_hint("Pick the folder with %s in it, or drop it on this window.", LAUNCHER_NAME);
            tools_help_marker("Start JSRF-PC Tools from JSRF-PC's own folder and it finds everything by itself.");
            if (note[0]) tools_status(TOOLS_WARN, "%s", note);
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.25f));
            if (ImGui::Button("Pick folder...")) {
                char dir[JSRF_PATH_MAX];
                int r = jsrf_pick_folder("Where is JSRF-PC? Pick the folder its launcher is in", dir, sizeof dir);
                if (r == JSRF_NO_DIALOG)
                    snprintf(note, sizeof note, "There's no folder dialog here (zenity or kdialog): drop the "
                                                "folder on this window instead.");
                if (r == JSRF_PICKED) {
                    snprintf(t.game, sizeof t.game, "%s", dir);
                    if (!(launcher = has_launcher(t.game)))
                        snprintf(note, sizeof note, "%s has no %s in it.", dir, LAUNCHER_NAME);
                    else
                        found = find_saves(&t);
                }
            }
        }
        tools_script_frame_end();
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
        if (tools_scripting()) {
            int dw, dh;
            SDL_GL_GetDrawableSize(win, &dw, &dh);
            tools_script_drawn(dw, dh);
        }
        SDL_GL_SwapWindow(win);
        /* The window made tall enough for the page it opens on, over its
         * first frames (a table settles in two), then shown. */
        if (fit_left > 0) {
            if (overflow > 0) {
                int w, h;
                SDL_GetWindowSize(win, &w, &h);
                place_window(win, scale > 0 ? scale : 1, w, h + (int)(overflow + 0.99f) + (int)(4 * (scale > 0 ? scale : 1)));
                overflow = 0;
            }
            if (--fit_left == 0) SDL_ShowWindow(win);
        }
    }
    tools_build_closing();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    /* A script's result: how many of its steps failed. */
    if (quit == 2) return 100;
    return tools_scripting() ? (tools_script_failures() > 99 ? 99 : tools_script_failures()) : 0;
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
