/*
 * JSRF-PC Tools' script mode (tools.h):
 *
 *   jsrf-pc-tools [JSRF-PC's folder] --script <steps file> [--out <folder>]
 *
 * plays a list of steps on the window as a person would: it picks tabs,
 * presses buttons, rows and boxes found by their names, types, answers the
 * file dialogs, waits for the work to end, checks what the window says and
 * what's on disk, and takes screenshots. Real input to the window is ignored
 * meanwhile, so someone at the machine can't upset a run. Each step goes to
 * <out>/script.log as it ends; the tool's exit code is the number of steps
 * that failed. docs/jsrf-pc-tools.md lists the steps.
 *
 * The window's items are found through Dear ImGui's own item hooks (built
 * with IMGUI_ENABLE_TEST_ENGINE: ImGuiTestEngineHook_ItemAdd and _ItemInfo,
 * defined here), and its text through ImGui's log to a buffer, each frame.
 * Presses go in as the mouse and keyboard do, through ImGui's input queue.
 *
 * Paths in a script may use $HERE (the script's folder), $OUT, $GAME
 * (JSRF-PC's folder), $SAVES and $SETTINGS; a relative path is the
 * script's folder's.
 */
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "../../third_party/miniz/miniz.h"

#include "file_dialog.h"
#include "setup.h"
#include "setup_job.h"
#include "tools.h"

#ifdef _WIN32
#include <windows.h>
#define T_GLAPI __stdcall
#else
#include <unistd.h>
#define T_GLAPI
#endif

#define CLICK_SETTLE   3        /* frames after a press, for the page to act on it */
#define TEXT_WAIT_MS   5000     /* how long a check waits for text to show (or go) */
#define FIND_WAIT_MS   5000     /* and for an item to show */
#define WORK_WAIT_MS   120000   /* wait's default limit */
#define RUN_LIMIT_MS   (30 * 60 * 1000)

namespace {

struct item {
    ImGuiID      id;
    ImRect       bb;
    ImGuiWindow *window;
    char         label[160];    /* the whole label, ## and all; "" until ItemInfo gives it */
    char         section[96];   /* the heading above it on its page */
    bool         disabled;
    bool         is_window;     /* a window's own entry, not an item in it */
};

struct step {
    int   line;
    char  verb[24];
    char  arg[1024];
};

enum phase { IDLE, APPROACH, CLICK_DOWN, CLICK_UP, SETTLE, KEYS_UP, HOVERING };

struct runner {
    bool   on;
    char   script[JSRF_PATH_MAX], here[JSRF_PATH_MAX], out[JSRF_PATH_MAX], log[JSRF_PATH_MAX];
    ImVector<step> steps;
    int    at;                  /* the step being played */
    int    failed, passed;
    int    phase;
    int    frames;              /* frames left in this phase */
    Uint32 since;               /* when this step started */
    Uint32 started;
    ImVec2 mouse;
    int    key_mods;
    ImGuiKey key;
    int    quiet;               /* frames in a row the tool hasn't been busy */
    int    want_close;
    char   answers[8][4096];    /* the next file dialogs' answers, in order; "" cancels */
    int    nanswers;
    char   drop[JSRF_PATH_MAX];
    char   shot[JSRF_PATH_MAX]; /* a screenshot to take after this frame */
    int    scrolled;            /* times the current target was scrolled to */
    Uint32 until;               /* a hover's end */
    ImVec2 target;              /* where the mouse goes after APPROACH */
    int    then;                /* and the phase after it */
    float  scale;               /* the window's scale, for the screenshots' notes */
};

runner R;
ImVector<item> g_items, g_last;  /* this frame's items, and the last whole frame's */
struct named { ImGuiID id; char label[160]; };
named g_pending[16];             /* labels given before their item (a tab's) */
int g_npending;
char g_section[96];
ImGuiTextBuffer g_text, g_last_text;

}  /* namespace */

/* ---- ImGui's hooks ------------------------------------------------------------------------ */

void ImGuiTestEngineHook_ItemAdd(ImGuiContext *ctx, ImGuiID id, const ImRect &bb, const ImGuiLastItemData *data)
{
    item it = item();
    it.id = id;
    it.bb = bb;
    it.window = ctx->CurrentWindow;
    it.disabled = data && (data->ItemFlags & ImGuiItemFlags_Disabled);
    it.is_window = data == NULL;
    snprintf(it.section, sizeof it.section, "%s", g_section);
    for (int i = 0; i < g_npending; i++)
        if (g_pending[i].id == id) {
            snprintf(it.label, sizeof it.label, "%s", g_pending[i].label);
            g_pending[i] = g_pending[--g_npending];
            break;
        }
    g_items.push_back(it);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext *ctx, ImGuiID id, const char *label, ImGuiItemStatusFlags flags)
{
    (void)ctx; (void)flags;
    tools_script_name(id, label);
}

/* An item's label: to the item added already, or kept for when it is (a
 * tab gives its label first). */
void tools_script_name(unsigned id, const char *label)
{
    if (!R.on || !id) return;
    for (int i = g_items.Size - 1, n = 0; i >= 0 && n < 8; i--, n++)
        if (g_items[i].id == id) {
            snprintf(g_items[i].label, sizeof g_items[i].label, "%s", label ? label : "");
            return;
        }
    if (g_npending == (int)(sizeof g_pending / sizeof g_pending[0])) g_npending--;
    g_pending[g_npending].id = id;
    snprintf(g_pending[g_npending].label, sizeof g_pending[0].label, "%s", label ? label : "");
    g_npending++;
}

void ImGuiTestEngineHook_Log(ImGuiContext *ctx, const char *fmt, ...)
{
    (void)ctx; (void)fmt;
}

const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *ctx, ImGuiID id)
{
    (void)ctx; (void)id;
    return NULL;
}

/* ---- the log ------------------------------------------------------------------------------ */

static void say(const char *fmt, ...)
{
    char buf[16384];
    va_list ap;
    FILE *f;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    fflush(stdout);
    if (R.log[0] && (f = js_fopen(R.log, "ab")) != NULL) {
        fprintf(f, "%s\n", buf);
        fclose(f);
    }
}

static const step *cur(void)
{
    return R.at < R.steps.Size ? &R.steps[R.at] : NULL;
}

static void next_step(void)
{
    R.at++;
    R.phase = IDLE;
    R.frames = 0;
    R.scrolled = 0;
    R.quiet = 0;
    R.since = SDL_GetTicks();
}

static void pass(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    R.passed++;
    say("PASS  %3d  %s %s%s%s", cur()->line, cur()->verb, cur()->arg, buf[0] ? "  -- " : "", buf);
    next_step();
}

/* fatal: the steps after it can't be played (an item that isn't there). */
static void fail(int fatal, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    R.failed++;
    say("FAIL  %3d  %s %s  -- %s", cur()->line, cur()->verb, cur()->arg, buf);
    if (fatal) {
        say("STOP  the steps after line %d aren't played, as they need this one", cur()->line);
        R.at = R.steps.Size;
        R.want_close = 1;
    }
    next_step();
}

/* ---- the script --------------------------------------------------------------------------- */

static void expand(const tools_ctx *t, const char *in, char *out, size_t n)
{
    static const struct { const char *name; int which; } vars[] = {
        { "$HERE", 0 }, { "$OUT", 1 }, { "$GAME", 2 }, { "$SAVES", 3 }, { "$SETTINGS", 4 },
    };
    size_t o = 0;
    while (*in && o + 1 < n) {
        int done = 0;
        for (size_t v = 0; v < sizeof vars / sizeof vars[0] && !done; v++) {
            size_t l = strlen(vars[v].name);
            if (!strncmp(in, vars[v].name, l)) {
                const char *val = vars[v].which == 0 ? R.here : vars[v].which == 1 ? R.out
                                : vars[v].which == 2 ? t->game : vars[v].which == 3 ? t->saves : t->settings;
                o += (size_t)snprintf(out + o, n - o, "%s", val);
                if (o >= n) o = n - 1;
                in += l;
                done = 1;
            }
        }
        if (!done) out[o++] = *in++;
    }
    out[o] = 0;
}

/* A path from a script: expanded, and a relative one the script's folder's. */
static void script_path(const tools_ctx *t, const char *in, char *out, size_t n)
{
    char e[JSRF_PATH_MAX];
    expand(t, in, e, sizeof e);
    if (e[0] == '/' || e[0] == '\\' || (e[0] && e[1] == ':')) snprintf(out, n, "%s", e);
    else js_path(out, n, R.here, e);
#ifdef _WIN32
    for (char *s = out; *s; s++) if (*s == '/') *s = '\\';   /* $HERE/pictures, as Windows writes it */
#endif
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static int answer(char *out, size_t n)
{
    if (R.nanswers == 0) {
        say("NOTE       a file dialog opened with no answer for it: cancelled");
        if (n) out[0] = 0;
        return 0;
    }
    snprintf(out, n, "%s", R.answers[0]);
    memmove(R.answers[0], R.answers[1], sizeof R.answers[0] * (size_t)(R.nanswers - 1));
    R.nanswers--;
    say("NOTE       a file dialog was answered: %s", out[0] ? out : "(cancelled)");
    return out[0] != 0;
}

/* A folder made whole, from where the tool was started. The script's folder and
 * the out folder stand for $HERE and $OUT, and a relative path in a script is
 * taken as the script folder's: were they left relative, "scripts\general.txt"
 * would look for its pictures in scripts\scripts\pictures. */
static void whole_path(char *p, size_t n)
{
#ifdef _WIN32
    wchar_t w[JSRF_PATH_MAX], f[JSRF_PATH_MAX];
    char u[JSRF_PATH_MAX];
    DWORD l;
    if (!MultiByteToWideChar(CP_UTF8, 0, p, -1, w, JSRF_PATH_MAX)) return;
    l = GetFullPathNameW(w, JSRF_PATH_MAX, f, NULL);
    if (l == 0 || l >= JSRF_PATH_MAX) return;
    if (!WideCharToMultiByte(CP_UTF8, 0, f, -1, u, (int)sizeof u, NULL, NULL)) return;
    snprintf(p, n, "%s", u);
#else
    char cwd[JSRF_PATH_MAX], j[JSRF_PATH_MAX];
    if (p[0] == '/' || !getcwd(cwd, sizeof cwd)) return;
    if (!strcmp(p, ".")) snprintf(j, sizeof j, "%s", cwd);
    else js_path(j, sizeof j, cwd, p[0] == '.' && p[1] == '/' ? p + 2 : p);
    snprintf(p, n, "%s", j);
#endif
}

int tools_script_start(const char *script, const char *out)
{
    unsigned char *data = NULL;
    size_t size = 0;
    char *text, *line, *next, *slash;
    int ln = 0;
    R.mouse = ImVec2(-FLT_MAX, -FLT_MAX);
    snprintf(R.script, sizeof R.script, "%s", script);
    if (js_read_file(script, 1u << 20, &data, &size) != 0 || !data) {
        fprintf(stderr, "jsrf-pc-tools: can't read the script %s\n", script);
        return -1;
    }
    snprintf(R.here, sizeof R.here, "%s", script);
    slash = R.here + strlen(R.here);
    while (slash > R.here && slash[-1] != '/' && slash[-1] != '\\') slash--;
    if (slash > R.here) slash[-1] = 0;
    else snprintf(R.here, sizeof R.here, ".");
    whole_path(R.here, sizeof R.here);
    if (out && out[0]) snprintf(R.out, sizeof R.out, "%s", out);
    else js_path(R.out, sizeof R.out, R.here, "script-out");
    whole_path(R.out, sizeof R.out);
    js_mkdirs(R.out);
    js_path(R.log, sizeof R.log, R.out, "script.log");
    js_write_file(R.log, "", 0);
    text = (char *)realloc(data, size + 1);
    if (!text) { free(data); return -1; }
    text[size] = 0;
    for (line = text; line && *line; line = next) {
        char *s, *sp;
        step st;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        ln++;
        s = trim(line);
        if (!*s || *s == '#') continue;
        memset(&st, 0, sizeof st);
        st.line = ln;
        sp = s;
        while (*sp && *sp != ' ' && *sp != '\t') sp++;
        snprintf(st.verb, sizeof st.verb, "%.*s", (int)(sp - s), s);
        snprintf(st.arg, sizeof st.arg, "%s", trim(sp));
        R.steps.push_back(st);
    }
    free(text);
    R.on = true;
    R.started = R.since = SDL_GetTicks();
    jsrf_dialog_answer = answer;
    say("SCRIPT %s: %d steps, out %s", R.script, R.steps.Size, R.out);
    return 0;
}

bool tools_scripting(void)
{
    return R.on;
}

int tools_script_failures(void)
{
    return R.failed;
}

/* ---- finding items ------------------------------------------------------------------------ */

/* The label as shown: up to its ##. */
static void shown(const char *label, char *out, size_t n)
{
    const char *h = strstr(label, "##");
    snprintf(out, n, "%.*s", (int)(h ? (size_t)(h - label) : strlen(label)), label);
}

static bool in_window(ImGuiWindow *w, ImGuiWindow *root)
{
    for (; w; w = w->ParentWindow)
        if (w == root) return true;
    return false;
}

static bool matches(const item *it, const char *sec, const char *lab, bool tabs_only)
{
    char s[160];
    if (it->is_window || !it->label[0]) return false;
    if (tabs_only && it->window && (it->window->Flags & ImGuiWindowFlags_ChildWindow)) return false;
    if (sec[0] && strcmp(it->section, sec)) return false;
    shown(it->label, s, sizeof s);
    if (lab[0] == '~') return strstr(strstr(lab, "##") ? it->label : s, lab + 1) != NULL;
    if (strstr(lab, "##")) return !strcmp(it->label, lab);
    return !strcmp(s, lab);
}

/* target: [<section> > ]<label>[@<n>]; label exact as shown, ~text for one
 * that has text in it (in its whole label when text has ## in it, as
 * ~###go), or a whole label with its ##; @n the nth of them.
 * While a modal is open, only its items; while another popup is open (a
 * list), its items first, then the rest. */
static const item *find(const char *target, bool tabs_only, char *why, size_t wn)
{
    char sec[96] = "", lab[256], *at;
    const char *gt = strstr(target, " > ");
    int nth = 1;
    ImGuiContext &g = *GImGui;
    ImGuiWindow *modal = ImGui::GetTopMostAndVisiblePopupModal(), *popup = NULL;
    if (gt) {
        snprintf(sec, sizeof sec, "%.*s", (int)(gt - target), target);
        snprintf(lab, sizeof lab, "%s", gt + 3);
    } else {
        snprintf(lab, sizeof lab, "%s", target);
    }
    if ((at = strrchr(lab, '@')) != NULL && at[1] >= '1' && at[1] <= '9') { nth = atoi(at + 1); *at = 0; }
    if (!modal && g.OpenPopupStack.Size > 0) popup = g.OpenPopupStack.back().Window;
    for (int scope = 0; scope < 2; scope++) {
        ImGuiWindow *within = modal ? modal : scope == 0 ? popup : NULL;
        int seen = 0;
        if (scope == 0 && !within) continue;
        for (int i = 0; i < g_last.Size; i++) {
            const item *it = &g_last[i];
            if (within && !in_window(it->window, within)) continue;
            if (matches(it, sec, lab, tabs_only) && ++seen == nth) return it;
        }
        if (modal) break;
    }
    snprintf(why, wn, "no item \"%s\"%s%s%s on screen", lab, sec[0] ? " under \"" : "", sec, sec[0] ? "\"" : "");
    return NULL;
}

/* What there was to press, for a script's writer, when an item wasn't found. */
static void list_items(void)
{
    ImGuiTextBuffer b;
    int n = 0;
    for (int i = 0; i < g_last.Size && n < 120; i++) {
        const item *it = &g_last[i];
        char s[160];
        if (it->is_window || !it->label[0]) continue;
        shown(it->label, s, sizeof s);
        b.appendf("%s%s%s%s%s", n++ ? " | " : "", it->section[0] ? it->section : "", it->section[0] ? " > " : "",
                  s[0] ? s : it->label, it->disabled ? " (greyed)" : "");
    }
    say("ITEMS      %s", n ? b.c_str() : "none");
}

/* Whether an item can be pressed where it is: inside its window's view. */
static bool visible(const item *it)
{
    ImGuiWindow *w = it->window;
    ImVec2 c = it->bb.GetCenter();
    for (; w; w = (w->Flags & ImGuiWindowFlags_ChildWindow) ? w->ParentWindow : NULL)
        if (!w->InnerClipRect.Contains(c)) return false;
    return true;
}

/* ---- keys ---------------------------------------------------------------------------------- */

static int parse_keys(const char *in, int *mods, ImGuiKey *key)
{
    static const struct { const char *name; ImGuiKey key; } names[] = {
        { "enter", ImGuiKey_Enter }, { "escape", ImGuiKey_Escape }, { "tab", ImGuiKey_Tab },
        { "space", ImGuiKey_Space }, { "backspace", ImGuiKey_Backspace }, { "delete", ImGuiKey_Delete },
        { "up", ImGuiKey_UpArrow }, { "down", ImGuiKey_DownArrow }, { "left", ImGuiKey_LeftArrow },
        { "right", ImGuiKey_RightArrow }, { "home", ImGuiKey_Home }, { "end", ImGuiKey_End },
        { "equal", ImGuiKey_Equal }, { "minus", ImGuiKey_Minus }, { "plus", ImGuiKey_KeypadAdd },
    };
    char buf[64], *part, *next;
    *mods = 0;
    *key = ImGuiKey_None;
    snprintf(buf, sizeof buf, "%s", in);
    for (part = buf; part && *part; part = next) {
        if ((next = strchr(part, '+')) != NULL) *next++ = 0;
        if (!strcmp(part, "ctrl")) { *mods |= ImGuiMod_Ctrl; continue; }
        if (!strcmp(part, "shift")) { *mods |= ImGuiMod_Shift; continue; }
        if (!strcmp(part, "alt")) { *mods |= ImGuiMod_Alt; continue; }
        if (part[0] >= 'a' && part[0] <= 'z' && !part[1]) { *key = (ImGuiKey)(ImGuiKey_A + (part[0] - 'a')); continue; }
        if (part[0] >= '0' && part[0] <= '9' && !part[1]) { *key = (ImGuiKey)(ImGuiKey_0 + (part[0] - '0')); continue; }
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            if (!strcmp(part, names[i].name)) *key = names[i].key;
        if (*key == ImGuiKey_None) return 0;
    }
    return *key != ImGuiKey_None;
}

static void put_mods(ImGuiIO &io, int mods, bool down)
{
    if (mods & ImGuiMod_Ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, down);
    if (mods & ImGuiMod_Shift) io.AddKeyEvent(ImGuiMod_Shift, down);
    if (mods & ImGuiMod_Alt) io.AddKeyEvent(ImGuiMod_Alt, down);
}

/* ---- text on screen ------------------------------------------------------------------------- */

/* The frame's text, with its runs of spaces and line breaks as one space. */
static void flatten(const char *in, ImGuiTextBuffer *out)
{
    bool space = false;
    out->clear();
    for (; *in; in++) {
        if (*in == ' ' || *in == '\n' || *in == '\r' || *in == '\t') { space = true; continue; }
        if (space && !out->empty()) out->append(" ");
        space = false;
        out->append(in, in + 1);
    }
}

void tools_script_frame_begin(void)
{
    if (!R.on) return;
    ImGui::LogToBuffer(0);       /* no tree opened by it */
}

void tools_script_frame_end(void)
{
    ImGuiContext &g = *GImGui;
    if (!R.on) return;
    if (g.LogEnabled) {
        flatten(g.LogBuffer.c_str(), &g_text);
        ImGui::LogFinish();
    }
}

void tools_script_heading(const char *text)
{
    if (!R.on) return;
    snprintf(g_section, sizeof g_section, "%s", text);
    /* A heading is drawn with the draw list, so ImGui's log doesn't see it:
     * it goes into the frame's text here. */
    if (GImGui->LogEnabled)
        ImGui::LogText("\n%.*s\n", (int)(ImGui::FindRenderedTextEnd(text) - text), text);
}

bool tools_script_ignores(const SDL_Event *ev)
{
    if (!R.on) return false;
    switch (ev->type) {
    case SDL_MOUSEMOTION: case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEWHEEL:
    case SDL_KEYDOWN: case SDL_KEYUP: case SDL_TEXTINPUT: case SDL_TEXTEDITING:
    case SDL_FINGERDOWN: case SDL_FINGERUP: case SDL_FINGERMOTION:
    case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: case SDL_CONTROLLERAXISMOTION:
        return true;
    case SDL_WINDOWEVENT:
        return ev->window.event == SDL_WINDOWEVENT_FOCUS_LOST || ev->window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
               ev->window.event == SDL_WINDOWEVENT_ENTER || ev->window.event == SDL_WINDOWEVENT_LEAVE;
    default:
        return false;
    }
}

int tools_script_take_drop(char *out, size_t n)
{
    if (!R.on || !R.drop[0]) return 0;
    snprintf(out, n, "%s", R.drop);
    R.drop[0] = 0;
    return 1;
}

int tools_script_wants_close(void)
{
    return R.on && R.want_close;
}

/* ---- each frame ----------------------------------------------------------------------------- */

static int file_check(const tools_ctx *t, const char *arg, char *path, size_t n)
{
    char e[JSRF_PATH_MAX];
    expand(t, arg, e, sizeof e);
    snprintf(path, n, "%s", e);
    return js_is_file(path) || js_is_dir(path);
}

/* Before ImGui's frame, after the backend's: this frame's input, and the
 * step's next move, from what the last frame showed. */
void tools_script_input(tools_ctx *t, bool settled)
{
    ImGuiIO &io = ImGui::GetIO();
    const step *s;
    char why[512];
    Uint32 now = SDL_GetTicks(), in_step;
    if (!R.on) return;
    R.scale = t->scale;
    io.AddMousePosEvent(R.mouse.x, R.mouse.y);     /* over the backend's, every frame */
    g_last.swap(g_items);
    g_items.resize(0);
    g_last_text.Buf.swap(g_text.Buf);
    g_text.clear();
    g_section[0] = 0;
    g_npending = 0;
    GImGui->TestEngineHookItems = true;
    if (!settled) return;                          /* the window still being fitted */
    if (now - R.started > RUN_LIMIT_MS && R.at < R.steps.Size) {
        fail(1, "the script has run for 30 minutes");
        return;
    }
    if (R.frames > 0) { R.frames--; return; }
    s = cur();
    if (!s) {
        if (!R.want_close) {
            say("END   %d steps: %d passed, %d failed", R.passed + R.failed, R.passed, R.failed);
            R.want_close = 1;
        }
        return;
    }
    in_step = now - R.since;
    switch (R.phase) {
    case APPROACH:
        R.mouse = R.target;
        io.AddMousePosEvent(R.mouse.x, R.mouse.y);
        R.phase = R.then;
        if (R.phase == HOVERING) R.until = now + 1200;
        else R.frames = 1;
        return;
    case CLICK_DOWN:
        io.AddMouseButtonEvent(0, true);
        R.phase = CLICK_UP;
        R.frames = 1;
        return;
    case CLICK_UP:
        io.AddMouseButtonEvent(0, false);
        R.phase = SETTLE;
        R.frames = CLICK_SETTLE;
        return;
    case KEYS_UP:
        io.AddKeyEvent(R.key, false);
        put_mods(io, R.key_mods, false);
        R.phase = SETTLE;
        R.frames = CLICK_SETTLE;
        return;
    case HOVERING:
        if (now >= R.until) pass("");
        return;
    case SETTLE:
        R.mouse = ImVec2(-FLT_MAX, -FLT_MAX);
        pass("");
        return;
    default:
        break;
    }

    if (!strcmp(s->verb, "click") || !strcmp(s->verb, "tab") || !strcmp(s->verb, "hover")) {
        const item *it = find(s->arg, !strcmp(s->verb, "tab"), why, sizeof why);
        if (!it) {
            if (in_step > FIND_WAIT_MS) {
                list_items();
                fail(1, "%s", why);
            }
            return;
        }
        if (it->disabled && strcmp(s->verb, "hover")) { fail(1, "it's greyed"); return; }
        if (!visible(it)) {
            ImGuiWindow *w = it->window;
            while (w && w->ScrollMax.y <= 0 && (w->Flags & ImGuiWindowFlags_ChildWindow)) w = w->ParentWindow;
            if (R.scrolled++ > 5 || !w || w->ScrollMax.y <= 0) { fail(1, "it's off the window and can't be scrolled to"); return; }
            ImGui::SetScrollY(w, w->Scroll.y + (it->bb.GetCenter().y - w->InnerClipRect.GetCenter().y));
            R.frames = 2;
            return;
        }
        /* The mouse comes to the item from a few pixels off, as a hand would:
         * ImGui shows hovers again only after the mouse moves, once keys have
         * been used, and a tooltip only once it's been still a moment. */
        R.target = it->bb.GetCenter();
        R.mouse = ImVec2(R.target.x - 4, R.target.y - 3);
        io.AddMousePosEvent(R.mouse.x, R.mouse.y);
        R.phase = APPROACH;
        R.then = !strcmp(s->verb, "hover") ? HOVERING : CLICK_DOWN;   /* a hover: long enough for its tooltip */
        R.frames = 1;
        return;
    }
    if (!strcmp(s->verb, "expect-greyed") || !strcmp(s->verb, "expect-enabled")) {
        const item *it = find(s->arg, false, why, sizeof why);
        bool want = !strcmp(s->verb, "expect-greyed");
        if (!it) { if (in_step > FIND_WAIT_MS) fail(0, "%s", why); return; }
        if (it->disabled == want) pass("");
        else if (in_step > TEXT_WAIT_MS) fail(0, "it's %s", it->disabled ? "greyed" : "not greyed");
        return;
    }
    if (!strcmp(s->verb, "type")) {
        char e[1024];
        expand(t, s->arg, e, sizeof e);
        io.AddInputCharactersUTF8(e);
        R.phase = SETTLE;
        R.frames = CLICK_SETTLE;
        return;
    }
    if (!strcmp(s->verb, "key")) {
        if (!parse_keys(s->arg, &R.key_mods, &R.key)) { fail(1, "not a key this knows"); return; }
        put_mods(io, R.key_mods, true);
        io.AddKeyEvent(R.key, true);
        R.phase = KEYS_UP;
        R.frames = 1;
        return;
    }
    if (!strcmp(s->verb, "answer") || !strcmp(s->verb, "cancel-dialog")) {
        if (R.nanswers >= 8) { fail(1, "more than 8 answers waiting"); return; }
        R.answers[R.nanswers][0] = 0;
        if (!strcmp(s->verb, "answer")) {
            /* Several paths, for files, between " | ". */
            char buf[4096], *p = buf, *bar, one[JSRF_PATH_MAX];
            size_t o = 0;
            snprintf(buf, sizeof buf, "%s", s->arg);
            for (;;) {
                bar = strstr(p, " | ");
                if (bar) *bar = 0;
                script_path(t, trim(p), one, sizeof one);
                o += (size_t)snprintf(R.answers[R.nanswers] + o, sizeof R.answers[0] - o, "%s%s", o ? "\n" : "", one);
                if (!bar || o >= sizeof R.answers[0]) break;
                p = bar + 3;
            }
        }
        R.nanswers++;
        pass("");
        return;
    }
    if (!strcmp(s->verb, "drop")) {
        script_path(t, s->arg, R.drop, sizeof R.drop);
        R.phase = SETTLE;
        R.frames = CLICK_SETTLE;
        return;
    }
    if (!strcmp(s->verb, "wait")) {
        Uint32 limit = s->arg[0] ? (Uint32)(atof(s->arg) * 1000) : WORK_WAIT_MS;
        if (!t->busy && !ImGui::IsAnyItemActive()) R.quiet++;
        else R.quiet = 0;
        if (R.quiet >= 10) pass("after %.1f s", in_step / 1000.0);
        else if (in_step > limit) fail(1, "still busy after %.0f s", limit / 1000.0);
        return;
    }
    if (!strcmp(s->verb, "pause")) {
        if (in_step >= (Uint32)(atof(s->arg) * 1000)) pass("");
        return;
    }
    if (!strcmp(s->verb, "expect-text") || !strcmp(s->verb, "expect-no-text")) {
        char e[1024];
        bool want = !strcmp(s->verb, "expect-text");
        ImGuiTextBuffer flat;
        expand(t, s->arg, e, sizeof e);
        flatten(e, &flat);
        bool there = strstr(g_last_text.c_str(), flat.c_str()) != NULL;
        if (there == want) pass("");
        else if (in_step > TEXT_WAIT_MS) fail(0, want ? "not on screen" : "still on screen");
        return;
    }
    if (!strcmp(s->verb, "expect-file") || !strcmp(s->verb, "expect-no-file")) {
        char path[JSRF_PATH_MAX];
        bool want = !strcmp(s->verb, "expect-file"), there = file_check(t, s->arg, path, sizeof path) != 0;
        if (there == want) pass("");
        else if (in_step > TEXT_WAIT_MS) fail(0, "%s %s", path, there ? "is there" : "isn't there");
        return;
    }
    if (!strcmp(s->verb, "shot")) {
        char name[256];
        snprintf(name, sizeof name, "%s.png", s->arg);
        js_path(R.shot, sizeof R.shot, R.out, name);
        R.phase = SETTLE;
        R.frames = 1;
        return;
    }
    if (!strcmp(s->verb, "note")) {
        say("NOTE  %3d  %s", s->line, s->arg);
        next_step();
        return;
    }
    if (!strcmp(s->verb, "close")) {
        say("PASS  %3d  close", s->line);
        R.passed++;
        R.at = R.steps.Size;
        say("END   %d steps: %d passed, %d failed", R.passed + R.failed, R.passed, R.failed);
        R.want_close = 1;
        return;
    }
    fail(1, "not a step this knows");
}

/* After the frame is drawn, before it's shown: a screenshot, if one is due. */
void tools_script_drawn(int w, int h)
{
    typedef void (T_GLAPI *read_pixels_fn)(int, int, int, int, unsigned, unsigned, void *);
    typedef void (T_GLAPI *pixel_storei_fn)(unsigned, int);
    read_pixels_fn read_pixels;
    pixel_storei_fn store;
    unsigned char *pix;
    void *png;
    size_t len = 0;
    if (!R.on || !R.shot[0] || w <= 0 || h <= 0) return;
    read_pixels = (read_pixels_fn)SDL_GL_GetProcAddress("glReadPixels");
    store = (pixel_storei_fn)SDL_GL_GetProcAddress("glPixelStorei");
    pix = (unsigned char *)malloc((size_t)w * (size_t)h * 3);
    if (read_pixels && store && pix) {
        store(0x0D05u, 1);                                         /* PACK_ALIGNMENT */
        read_pixels(0, 0, w, h, 0x1907u, 0x1401u, pix);            /* RGB, UNSIGNED_BYTE */
        png = tdefl_write_image_to_png_file_in_memory_ex(pix, w, h, 3, &len, 6, 1);   /* flipped: GL's rows go up */
        if (png && js_write_file(R.shot, png, len) == 0) {
            /* With where the window is, and the room the screen has for it. */
            SDL_Window *win = SDL_GL_GetCurrentWindow();
            SDL_Rect room = { 0, 0, 0, 0 };
            int x = 0, y = 0, ww = 0, wh = 0;
            if (win) {
                SDL_GetWindowPosition(win, &x, &y);
                SDL_GetWindowSize(win, &ww, &wh);
                SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(win), &room);
            }
            say("NOTE       screenshot %s (%dx%d): the window %dx%d at %d,%d, scale %d%%; the screen's room %dx%d at %d,%d",
                R.shot, w, h, ww, wh, x, y, (int)(R.scale * 100 + 0.5f), room.w, room.h, room.x, room.y);
        } else say("NOTE       screenshot %s not written", R.shot);
        mz_free(png);
    }
    free(pix);
    R.shot[0] = 0;
}
