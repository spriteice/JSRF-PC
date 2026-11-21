/*
 * menu.cpp -- draws the rows added with xbox_MenuAdd (menu.h).
 *
 * Built with -fno-exceptions -fno-rtti -fno-threadsafe-statics, as Dear
 * ImGui is: nothing here may need the C++ runtime library (no new, no STL),
 * so the engine still links against libc alone.
 */
#include "menu.h"
#include "menu_style.h"
#include "imgui.h"
#include "imgui_internal.h"   /* the bar's nav state, and the strip: menu_build */
#include "platform/recomp_check.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const k_menu_title[] = { "File", "Display", "Graphics", "Audio", "Input", "Gameplay",
                                            "Developer" };
static_assert(IM_ARRAYSIZE(k_menu_title) == XBOX_MENU_COUNT, "a title for each menu in menu.h");

/* The rows, and the boxes open over them -- by index: rows still arriving
 * can move them. The window's thread only. */
static ImVector<XBOX_MENU_ITEM> g_items;
static int g_ask = -1;          /* waiting for OK: its confirm text */
static int g_ask_pick;          /* ... and the choice, for a CHOICE or TOGGLES */
static int g_info = -1;         /* an INFO box to open */
static int g_bind = -1;         /* a BIND row's box: waiting for a press */
static int g_bind_over;         /* ... which has come, or been cancelled: it shuts */
static int g_bind_refused;      /* ... and the last press was one it can't have */
static int g_bind_second;       /* ... and it has its first, and waits for its second */
static int g_pad_quiet;         /* since then, a pad's button or stick held */
static int g_reopen_row = -1;   /* ... and the pad back on its row: box_closed */

/* Rows come from any thread -- the input layer's, the game's -- and are drawn
 * on the window's, so xbox_MenuAdd only queues them, and each frame takes the
 * queue first. The lock is held for a copy and nothing else, so a spin is
 * enough; the queue is the C library's, not ImGui's, which is not meant for
 * other threads. */
static XBOX_MENU_ITEM *g_queued;
static int             g_nqueued;
static char            g_queue_lock;

static void queue_lock(void)   { while (__atomic_test_and_set(&g_queue_lock, __ATOMIC_ACQUIRE)) { } }
static void queue_unlock(void) { __atomic_clear(&g_queue_lock, __ATOMIC_RELEASE); }

extern "C" void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    XBOX_MENU_ITEM *q;
    if (count <= 0) return;
    queue_lock();
    q = (XBOX_MENU_ITEM *)realloc(g_queued, (size_t)(g_nqueued + count) * sizeof *q);
    if (q) {
        memcpy(q + g_nqueued, items, (size_t)count * sizeof *q);
        g_queued = q;
        g_nqueued += count;
    }
    queue_unlock();
    if (!q) fprintf(stderr, "[MENU] out of memory: %d rows not added\n", count);
}

/* For each row, whether the log has said its row to go under (menu.h,
 * under), or its submenu (group), isn't there. */
static ImVector<unsigned char> g_under_told;

/* A bind row with all it needs (menu.h, takes): what it takes, its bind,
 * and its text. */
static int bind_whole(const XBOX_MENU_ITEM &it)
{
    return (it.takes == XBOX_BIND_PAD || it.takes == XBOX_BIND_KEYS) && it.bind && it.text;
}

static void take_queued(void)
{
    int from = g_items.Size;
    queue_lock();
    for (int i = 0; i < g_nqueued; i++)
        g_items.push_back(g_queued[i]);
    g_nqueued = 0;
    queue_unlock();
    g_under_told.resize(g_items.Size, 0);
    /* Every row but a separator says what it does, briefly (menu.h, hint).
     * Every row of Display, Graphics, Audio, Input and Gameplay gives its
     * value in each of File > Presets' presets (menu.h, presets). One the
     * presets set is a TOGGLE, a CHOICE with choices, a NUMBER or a row of
     * ticks (TOGGLES) with no more than a bit of an int each, and one that
     * asks first (for a restart) is a TOGGLE or a CHOICE that keeps its
     * value instead (keep), so a preset restarts the game once, at the end.
     * Any other is told once, and no preset sets it. */
    for (int i = from; i < g_items.Size; i++) {
        const XBOX_MENU_ITEM &it = g_items[i];
        const char *name = it.label ? it.label : "a row";
        int hinted = it.kind == XBOX_ITEM_SEPARATOR || (it.hint && strlen(it.hint) <= MENU_HINT_LONGEST);
        /* A submenu row (menu.h, group) holds rows and is no setting itself. */
        int said = it.presets != XBOX_PRESETS_UNSAID || it.kind == XBOX_ITEM_SEPARATOR
                || it.kind == XBOX_ITEM_GROUP || it.menu == XBOX_MENU_FILE || it.menu == XBOX_MENU_DEVELOPER;
        int settable = it.presets != XBOX_PRESETS_SET
                    || ((it.kind == XBOX_ITEM_TOGGLE || (it.kind == XBOX_ITEM_CHOICE && it.nchoices > 0)
                         || it.kind == XBOX_ITEM_NUMBER
                         || (it.kind == XBOX_ITEM_TOGGLES && it.nchoices > 0 && it.nchoices <= MENU_PRESET_TICKS))
                        && (!it.confirm || (it.keep && (it.kind == XBOX_ITEM_TOGGLE || it.kind == XBOX_ITEM_CHOICE))));
        RECOMP_CHECK(hinted, "%s: %s", name, it.hint ? "its hint is too long" : "no hint");
        RECOMP_CHECK(said, "%s: nothing said about presets", name);
        if (!RECOMP_CHECK(settable, "%s: %s", name,
                          it.confirm ? "asks first, and keeps nothing for a preset" : "not a setting a preset can set"))
            g_items[i].presets = XBOX_PRESETS_UNSAID;
        /* A submenu goes in its menu, not in another (menu.h, group). */
        RECOMP_CHECK(it.kind != XBOX_ITEM_GROUP || !it.group, "%s: a submenu in a submenu", name);
        /* A bind row says what it takes, and has its bind and its text;
         * one without does nothing when chosen (bind_whole). */
        RECOMP_CHECK(it.kind != XBOX_ITEM_BIND || bind_whole(it),
                     "%s: a bind row without what it takes, its bind or its text", name);
    }
}

/* The submenu a row's group names (menu.h, group): the first of its menu
 * with that label. NULL: none. */
static const XBOX_MENU_ITEM *group_row(int m, const char *label)
{
    for (const XBOX_MENU_ITEM &o : g_items)
        if (o.menu == m && o.kind == XBOX_ITEM_GROUP && !o.group && o.label && !strcmp(o.label, label)) return &o;
    return NULL;
}

/* The submenu a row is drawn in: NULL for the menu itself, as for a row
 * whose submenu isn't there. */
static const char *drawn_in(const XBOX_MENU_ITEM &it)
{
    return it.group && group_row(it.menu, it.group) ? it.group : NULL;
}

static int drawn_in_is(const XBOX_MENU_ITEM &it, const char *group)
{
    const char *in = drawn_in(it);
    return in == group || (in && group && !strcmp(in, group));
}

/* The hint of the row the pad is on, and of the one the pointer is on:
 * taken as the rows are drawn, and shown below them in the same frame. */
static const char *g_hint_pad, *g_hint_pointer, *g_hint_shown;

/* After each of a row's items: a row's choices, and a number's - and +,
 * are the row's. */
static void take_hint(const XBOX_MENU_ITEM *it)
{
    if (ImGui::IsItemFocused())      g_hint_pad = it->hint;
    else if (ImGui::IsItemHovered()) g_hint_pointer = it->hint;
}

/* A choice or a tick, through the row's own function: pick_with, told its
 * row, where the row has one (menu.h). */
static void row_pick(const XBOX_MENU_ITEM &it, int i)
{
    if (it.pick_with) it.pick_with(it.data, i);
    else              it.pick(i);
}

static void apply(const XBOX_MENU_ITEM *it, int pick)
{
    if (pick >= 0) row_pick(*it, pick);
    else           it->run();
}

/* Chosen: straight away, or once confirmed. The box is opened from the top
 * level (below) -- a popup opened from inside a menu closes with it. */
static void chosen(const XBOX_MENU_ITEM *it, int pick)
{
    if (it->confirm) { g_ask = g_items.index_from_ptr(it); g_ask_pick = pick; }
    else             apply(it, pick);
}

/* A NUMBER's - and +: the step below or above v, counted from min, so a
 * value between two steps (written into the settings file by hand) goes to
 * the next one either way. Within min and max. */
#define MENU_STEP_SLACK 1e-6    /* of a step: what floating point leaves over */

/* ---- File > Presets (menu.h) ---------------------------------------------- */

/* A row of ticks' every tick, as state() gives them (bit i for choice i). */
static int row_ticks(const XBOX_MENU_ITEM &it)
{
    return (int)((1u << it.nchoices) - 1);
}

/* A row's value now, as preset[] gives it: a row of ticks' those it has. */
static double row_now(const XBOX_MENU_ITEM &it)
{
    switch (it.kind) {
    case XBOX_ITEM_TOGGLE:  return it.state() != 0;
    case XBOX_ITEM_CHOICE:  return it.state();
    case XBOX_ITEM_TOGGLES: return it.state() & row_ticks(it);
    default:                return it.value();
    }
}

/* Preset p's value for a row, within the row's own: a choice it has, a
 * number from min to max, ticks it has. */
static double row_preset(const XBOX_MENU_ITEM &it, int p)
{
    double v = it.preset[p];
    switch (it.kind) {
    case XBOX_ITEM_TOGGLE:  return v != 0;
    case XBOX_ITEM_CHOICE:  return v < 0 ? 0 : v > it.nchoices - 1 ? it.nchoices - 1 : floor(v);
    case XBOX_ITEM_TOGGLES: return v < 0 ? 0 : v > row_ticks(it) ? row_ticks(it) : floor(v);
    default:                return v < it.min ? it.min : v > it.max ? it.max : v;
    }
}

/* What a preset's line in the log says a row of ticks goes to: the ticks it
 * has, by name, or none. */
static void ticks_shown(const XBOX_MENU_ITEM &it, int ticks, char *shown, size_t n)
{
    size_t at = 0;
    shown[0] = 0;
    for (int i = 0; i < it.nchoices && at < n; i++)
        if ((ticks >> i) & 1)
            at += (size_t)snprintf(shown + at, n - at, "%s%s", at ? ", " : "", it.choices[i]);
    if (!shown[0]) snprintf(shown, n, "none");
}

/* The row is at preset p's value: a number within what floating point
 * leaves over of a step, as 15% kept as 0.15. */
static int row_at(const XBOX_MENU_ITEM &it, int p)
{
    double now = row_now(it), want = row_preset(it, p);
    return it.kind == XBOX_ITEM_NUMBER ? fabs(now - want) <= MENU_STEP_SLACK * it.step : now == want;
}

extern "C" int menu_preset_now(void)
{
    for (int p = 0; p < XBOX_PRESETS; p++) {
        int rows = 0, at = 0;
        for (const XBOX_MENU_ITEM &it : g_items)
            if (it.presets == XBOX_PRESETS_SET) {
                rows++;
                if (!row_at(it, p)) break;
                at++;
            }
        if (rows && at == rows) return p;
    }
    return -1;
}

extern "C" int menu_preset_apply(int p)
{
    int restart = 0;
    char shown[128];
    if (p < 0 || p >= XBOX_PRESETS) return 0;
    /* By index: no row's own function adds rows, but nothing is assumed. */
    for (int i = 0; i < g_items.Size; i++) {
        const XBOX_MENU_ITEM it = g_items[i];
        double v;
        if (it.presets != XBOX_PRESETS_SET || row_at(it, p)) continue;
        v = row_preset(it, p);
        if (it.kind == XBOX_ITEM_CHOICE) snprintf(shown, sizeof shown, "%s", it.choices[(int)v]);
        else if (it.kind == XBOX_ITEM_TOGGLE) snprintf(shown, sizeof shown, "%s", v != 0 ? "on" : "off");
        else if (it.kind == XBOX_ITEM_TOGGLES) ticks_shown(it, (int)v, shown, sizeof shown);
        else snprintf(shown, sizeof shown, it.format, v);
        fprintf(stderr, "[MENU] preset: %s to %s%s\n", it.label, shown, it.keep ? ", from the next start" : "");
        if (it.keep) {
            it.keep((int)v);
            restart = 1;
        } else if (it.kind == XBOX_ITEM_TOGGLE) it.run();
        else if (it.kind == XBOX_ITEM_CHOICE)   row_pick(it, (int)v);
        else if (it.kind == XBOX_ITEM_TOGGLES) {
            /* Each tick that differs, through the row's own pick, as
             * choosing it would. */
            int differ = (int)row_now(it) ^ (int)v;
            for (int i = 0; i < it.nchoices; i++)
                if ((differ >> i) & 1) row_pick(it, i);
        } else                                  it.set(v);
    }
    return restart;
}

/* ---- A row by its labels (menu.h, menu_path_run) ------------------------ */

#define MENU_PATH_SEP    " > "
#define MENU_PATH_PARTS  8      /* the menu, a submenu, a row, a choice, On: room to spare */
#define MENU_PATH_ON     "On"
#define MENU_PATH_OFF    "Off"

/* The path's parts, split at each " > ", into buf. The count, or -1 for
 * more parts than MENU_PATH_PARTS or a path that doesn't fit. */
static int path_split(const char *path, char *buf, size_t n, const char **part)
{
    int count = 0;
    char *at, *sep;
    if (!path || strlen(path) >= n) return -1;
    strcpy(buf, path);
    at = buf;
    for (;;) {
        if (count == MENU_PATH_PARTS) return -1;
        part[count++] = at;
        sep = strstr(at, MENU_PATH_SEP);
        if (!sep) return count;
        *sep = 0;
        at = sep + strlen(MENU_PATH_SEP);
    }
}

/* The row with that label shown in menu m, in submenu group (NULL for the
 * menu itself), as the menu draws it: the first, and no separator. */
static const XBOX_MENU_ITEM *path_row(int m, const char *group, const char *label)
{
    for (const XBOX_MENU_ITEM &it : g_items)
        if (it.menu == m && it.kind != XBOX_ITEM_SEPARATOR && it.label && !strcmp(it.label, label)
            && drawn_in_is(it, group))
            return &it;
    return NULL;
}

static int path_choice(const XBOX_MENU_ITEM *it, const char *name)
{
    for (int i = 0; i < it->nchoices; i++)
        if (it->choices[i] && !strcmp(it->choices[i], name)) return i;
    return -1;
}

/* -1 for neither On nor Off. */
static int path_on(const char *word)
{
    return !strcmp(word, MENU_PATH_ON) ? 1 : !strcmp(word, MENU_PATH_OFF) ? 0 : -1;
}

/* What the path chooses, and done when `run` is set. */
static int path_do(const char *path, int run)
{
    char buf[512];
    const char *part[MENU_PATH_PARTS];
    const XBOX_MENU_ITEM *it = NULL;
    const char *group = NULL;
    int n = path_split(path, buf, sizeof buf, part), m, at;

    take_queued();
    if (n < 0) return MENU_PATH_TOO_LONG;
    for (m = 0; m < XBOX_MENU_COUNT && strcmp(k_menu_title[m], part[0]); m++) { }
    if (m == XBOX_MENU_COUNT) return MENU_PATH_NO_MENU;
    /* Down through a submenu, to the row. */
    for (at = 1; ; at++) {
        if (at == n) return it ? MENU_PATH_NOT_RUN : MENU_PATH_NO_ROW;
        it = path_row(m, group, part[at]);
        if (!it) return MENU_PATH_NO_ROW;
        if (it->kind != XBOX_ITEM_GROUP) break;
        if (group || group_row(m, it->label) != it) return MENU_PATH_NO_ROW;
        group = it->label;
    }
    at++;                               /* the parts after the row's own */
    if (it->confirm) return MENU_PATH_ASKS;
    switch (it->kind) {
    case XBOX_ITEM_ACTION:
        if (at != n) return MENU_PATH_TOO_LONG;
        if (run) it->run();
        return MENU_PATH_OK;
    case XBOX_ITEM_TOGGLE: {
        int want;
        if (at == n) { if (run) it->run(); return MENU_PATH_OK; }
        if (at + 1 != n) return MENU_PATH_TOO_LONG;
        want = path_on(part[at]);
        if (want < 0) return MENU_PATH_NO_CHOICE;
        if (run && (it->state() != 0) != want) it->run();
        return MENU_PATH_OK;
    }
    case XBOX_ITEM_CHOICE: {
        int i;
        if (at == n) return MENU_PATH_NOT_RUN;
        if (at + 1 != n) return MENU_PATH_TOO_LONG;
        i = path_choice(it, part[at]);
        if (i < 0) return MENU_PATH_NO_CHOICE;
        if (run && i != it->state()) row_pick(*it, i);
        return MENU_PATH_OK;
    }
    case XBOX_ITEM_TOGGLES: {
        int i, want = -1;
        if (at == n) return MENU_PATH_NOT_RUN;
        if (at + 2 < n) return MENU_PATH_TOO_LONG;
        i = path_choice(it, part[at]);
        if (i < 0) return MENU_PATH_NO_CHOICE;
        if (at + 2 == n && (want = path_on(part[at + 1])) < 0) return MENU_PATH_NO_CHOICE;
        if (run && (want < 0 || ((it->state() >> i) & 1) != want)) row_pick(*it, i);
        return MENU_PATH_OK;
    }
    case XBOX_ITEM_NUMBER: {
        char *end;
        double v;
        if (at == n) return MENU_PATH_NOT_RUN;
        if (at + 1 != n) return MENU_PATH_TOO_LONG;
        v = strtod(part[at], &end);
        if (end == part[at] || *end || !(v >= it->min - MENU_STEP_SLACK * it->step)
            || !(v <= it->max + MENU_STEP_SLACK * it->step))
            return MENU_PATH_BAD_NUMBER;
        if (run) it->set(v < it->min ? it->min : v > it->max ? it->max : v);
        return MENU_PATH_OK;
    }
    case XBOX_ITEM_BIND:    /* its box waits for a press, which a path can't give */
    default:
        return MENU_PATH_NOT_RUN;
    }
}

extern "C" int menu_path_check(const char *path) { return path_do(path, 0); }
extern "C" int menu_path_run(const char *path)   { return path_do(path, 1); }

extern "C" const char *menu_path_why(int result)
{
    switch (result) {
    case MENU_PATH_OK:         return "done";
    case MENU_PATH_NO_MENU:    return "no menu has that title";
    case MENU_PATH_NO_ROW:     return "nothing there has that label";
    case MENU_PATH_NO_CHOICE:  return "the row has no such choice, or it isn't On or Off";
    case MENU_PATH_BAD_NUMBER: return "not a number, or past the row's least or most";
    case MENU_PATH_ASKS:       return "the row asks first";
    case MENU_PATH_NOT_RUN:    return "nothing to choose there";
    case MENU_PATH_TOO_LONG:   return "more after what is chosen";
    default:                   return "not a result";
    }
}

static double step_from(const XBOX_MENU_ITEM *it, double v, int dir)
{
    double n = (v - it->min) / it->step;
    n = dir > 0 ? floor(n + MENU_STEP_SLACK) + 1 : ceil(n - MENU_STEP_SLACK) - 1;
    v = it->min + n * it->step;
    return v < it->min ? it->min : v > it->max ? it->max : v;
}

/* A menu's NUMBER rows line up, or a submenu's (group): the labels in one
 * column, then boxes as wide as the widest value any of them shows. */
struct number_cols { float label, box; };

static number_cols number_columns(int m, const char *group)
{
    number_cols c = { 0, 0 };
    char text[32];
    for (const XBOX_MENU_ITEM &it : g_items) {
        if (it.menu != m || it.kind != XBOX_ITEM_NUMBER || !drawn_in_is(it, group)) continue;
        const double shown[3] = { it.min, it.max, it.value() };
        c.label = ImMax(c.label, ImGui::CalcTextSize(it.label).x);
        for (double v : shown) {
            snprintf(text, sizeof text, it.format, v);
            c.box = ImMax(c.box, ImGui::CalcTextSize(text).x);
        }
    }
    c.box += ImGui::GetStyle().FramePadding.x * 2;
    return c;
}

static void draw_number(const XBOX_MENU_ITEM *it, const number_cols *cols)
{
    const ImGuiStyle &st = ImGui::GetStyle();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    double v = it->value(), to = v;
    float x = ImGui::GetCursorPosX(), side = ImGui::GetFrameHeight();
    char text[32];
    ImVec2 p, t;
    ImGui::PushID(g_items.index_from_ptr(it));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(it->label);
    take_hint(it);
    ImGui::SameLine(x + cols->label + st.ItemSpacing.x);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);   /* held: on it goes */
    if (ImGui::Button("-", ImVec2(side, side))) to = step_from(it, v, -1);
    take_hint(it);
    ImGui::SameLine();
    /* The value, in a box to read, not to type in: a pad has no keyboard. */
    snprintf(text, sizeof text, it->format, v);
    p = ImGui::GetCursorScreenPos();
    t = ImGui::CalcTextSize(text);
    dl->AddRectFilled(p, ImVec2(p.x + cols->box, p.y + side),
                      ImGui::GetColorU32(ImGuiCol_FrameBg), st.FrameRounding);
    dl->AddText(ImVec2(p.x + (cols->box - t.x) * 0.5f, p.y + st.FramePadding.y),
                ImGui::GetColorU32(ImGuiCol_Text), text);
    ImGui::Dummy(ImVec2(cols->box, side));
    take_hint(it);
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(side, side))) to = step_from(it, v, 1);
    take_hint(it);
    ImGui::PopItemFlag();
    ImGui::PopID();
    if (to != v) it->set(to);
}

static void draw_item(const XBOX_MENU_ITEM *it, const number_cols *cols)
{
    switch (it->kind) {
    case XBOX_ITEM_SEPARATOR:
        ImGui::Separator();
        break;
    case XBOX_ITEM_ACTION:
        if (ImGui::MenuItem(it->label)) chosen(it, -1);
        take_hint(it);
        break;
    case XBOX_ITEM_TOGGLE:
        if (ImGui::MenuItem(it->label, NULL, it->state() != 0)) chosen(it, -1);
        take_hint(it);
        break;
    case XBOX_ITEM_CHOICE: {
        bool open = ImGui::BeginMenu(it->label);
        take_hint(it);                  /* BeginMenu leaves the row the last item */
        if (open) {
            int cur = it->state(), i;
            for (i = 0; i < it->nchoices; i++) {
                if (ImGui::MenuItem(it->choices[i], NULL, i == cur) && i != cur)
                    chosen(it, i);
                take_hint(it);
            }
            ImGui::EndMenu();
        }
        break;
    }
    case XBOX_ITEM_TOGGLES: {
        bool open = ImGui::BeginMenu(it->label);
        take_hint(it);
        if (open) {
            int on = it->state(), i;
            for (i = 0; i < it->nchoices; i++) {
                if (ImGui::MenuItem(it->choices[i], NULL, (on >> i) & 1)) chosen(it, i);
                take_hint(it);
            }
            ImGui::EndMenu();
        }
        break;
    }
    case XBOX_ITEM_INFO:
        if (ImGui::MenuItem(it->label)) g_info = g_items.index_from_ptr(it);
        take_hint(it);
        break;
    case XBOX_ITEM_NUMBER:
        draw_number(it, cols);
        break;
    case XBOX_ITEM_GROUP: {
        /* Its rows, in the order added; shown with one at least, and only
         * the first of its name (menu.h, group). Opened again, with its
         * menu, when a bind row's box in it has shut (box_closed). */
        int any = 0;
        for (const XBOX_MENU_ITEM &r : g_items)
            any |= r.menu == it->menu && drawn_in_is(r, it->label);
        if (!any || group_row(it->menu, it->label) != it) break;
        ImGuiWindow *in = ImGui::GetCurrentWindow();
        bool reopen = g_reopen_row >= 0 && g_items[g_reopen_row].menu == it->menu
                   && drawn_in_is(g_items[g_reopen_row], it->label);
        if (reopen) ImGui::OpenPopup(it->label);
        bool open = ImGui::BeginMenu(it->label);
        /* Opened again, B goes back to this row, where the pad was, as it
         * would had the pad opened it: not to the menu's first. */
        if (reopen) in->NavLastIds[ImGuiNavLayer_Main] = ImGui::GetItemID();
        take_hint(it);
        if (open) {
            number_cols in = number_columns(it->menu, it->label);
            for (const XBOX_MENU_ITEM &r : g_items)
                if (r.menu == it->menu && drawn_in_is(r, it->label)) draw_item(&r, &in);
            ImGui::EndMenu();
        }
        break;
    }
    case XBOX_ITEM_BIND:
        /* The input beside the control, as a shortcut is drawn, but in the
         * text's own colour: it is what the row says, not an aside. */
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        if (ImGui::MenuItem(it->label, it->text ? it->text() : NULL) && bind_whole(*it)) {
            g_bind = g_items.index_from_ptr(it);
            g_bind_over = g_bind_refused = g_bind_second = 0;
        }
        ImGui::PopStyleColor();
        /* Back from its box: the pad on it again (box_closed). */
        if (g_items.index_from_ptr(it) == g_reopen_row) ImGui::SetItemDefaultFocus();
        take_hint(it);
        break;
    }
}

/* The row a row goes under (menu.h, under): the first of its menu with
 * that label that isn't under another itself, or in a submenu. NULL: none. */
static const XBOX_MENU_ITEM *under_row(const XBOX_MENU_ITEM *it)
{
    for (const XBOX_MENU_ITEM &o : g_items)
        if (o.menu == it->menu && !o.under && !o.group && o.label && !strcmp(o.label, it->under)) return &o;
    return NULL;
}

/* A row, then the rows under it, in the order they were added. */
static void draw_row(const XBOX_MENU_ITEM *it, const number_cols *cols)
{
    draw_item(it, cols);
    for (const XBOX_MENU_ITEM &u : g_items)
        if (u.under && !u.group && u.menu == it->menu && under_row(&u) == it) draw_item(&u, cols);
}

/* A row whose row to go under, or submenu to go in, isn't there: drawn
 * below the others, and said once. */
static void draw_unplaced(const XBOX_MENU_ITEM *it, const number_cols *cols)
{
    int i = g_items.index_from_ptr(it);
    if (!g_under_told[i]) {
        g_under_told[i] = 1;
        fprintf(stderr, "[MENU] %s: no %s \"%s\" to go %s; drawn below the others\n",
                it->label ? it->label : "a row", it->group ? "submenu" : "row",
                it->group ? it->group : it->under, it->group ? "in" : "under");
    }
    draw_item(it, cols);
}

static int menu_has_items(int m)
{
    for (const XBOX_MENU_ITEM &it : g_items)
        if (it.menu == m) return 1;
    return 0;
}

/* A box ends: back to the menu it was asked from, so a pad is never left
 * with nothing selected. A bind row's goes back to the row, in its submenu
 * where it has one, so the next control is a step away. */
static int g_last_menu, g_reopen = -1;

static void box_closed(int row = -1)
{
    ImGui::CloseCurrentPopup();
    g_reopen = g_last_menu;
    g_reopen_row = row;
}

/* ---- the room between the bar and the strip ------------------------------ */

/* Every menu, submenu and list of choices stays in the room between the bar
 * along the top and the strip along the bottom, over neither, and scrolls
 * where it is taller. Dear ImGui sizes them to that room already: the
 * viewport's work area, which the bar and the strip take theirs from (as of
 * the last frame). But it places them anywhere in the window
 * (GetPopupAllowedExtentRect), so a list opened low down, or one as tall as
 * the room, went over the strip. So while the menus are built the viewport
 * is that room, with the safe area Dear ImGui keeps inside it
 * (DisplaySafeAreaPadding) added round it: they are placed in the room
 * itself, as they are sized. The bar is built by then, the strip after, and
 * the viewport is the whole window again for both. */
struct menu_room { ImVec2 pos, size; };

static menu_room room_begin(void)
{
    ImGuiViewport *vp = ImGui::GetMainViewport();
    const float safe = ImGui::GetStyle().DisplaySafeAreaPadding.y;
    const menu_room whole = { vp->Pos, vp->Size };
    vp->Pos.y = vp->WorkPos.y - safe;
    vp->Size.y = vp->WorkSize.y + safe * 2;
    return whole;
}

static void room_end(const menu_room &whole)
{
    ImGuiViewport *vp = ImGui::GetMainViewport();
    vp->Pos = whole.pos;
    vp->Size = whole.size;
}

/* ---- the strip along the bottom: the row's hint, and the buttons --------- */

/* The pad's buttons, or the keys, and what each does in the menu; in a
 * bind row's box, the one that cancels it, as every other is the row's. */
struct prompt { const char *name; menu_cap cap; const char *does; };
#define MENU_PROMPTS 3
static const prompt k_pad_prompts[MENU_PROMPTS] = {
    { "A", MENU_CAP_A, "Select" }, { "B", MENU_CAP_B, "Back" }, { "Back", MENU_CAP_KEY, "Close" } };
static const prompt k_key_prompts[MENU_PROMPTS] = {
    { "Enter", MENU_CAP_KEY, "Select" }, { "Esc", MENU_CAP_KEY, "Back" }, { "F1", MENU_CAP_KEY, "Close" } };
static const prompt k_pad_bind_prompt = { "Back", MENU_CAP_KEY, "None" };
static const prompt k_key_bind_prompt = { "F1", MENU_CAP_KEY, "None" };

/* A cap is this many times the text's height across, so its name has room
 * round it: a round one for a letter, and for a word, one this much of its
 * height wider than the word. */
#define MENU_CAP_SIZE 1.25f
#define MENU_CAP_WORD 0.6f

/* The n prompts from at.x rightwards, centred on at.y; their width. With dl
 * NULL, only the width. */
static float draw_prompts(ImDrawList *dl, const prompt *ps, int n, ImVec2 at)
{
    const ImGuiStyle &st = ImGui::GetStyle();
    float h = ImGui::GetFontSize() * MENU_CAP_SIZE, x = at.x;
    for (int i = 0; i < n; i++) {
        const prompt &p = ps[i];
        ImVec2 name = ImGui::CalcTextSize(p.name), does = ImGui::CalcTextSize(p.does);
        float w = p.name[1] ? name.x + h * MENU_CAP_WORD : h;
        if (i) x += st.ItemSpacing.x * 2;
        if (dl) {
            dl->AddRectFilled(ImVec2(x, at.y - h * 0.5f), ImVec2(x + w, at.y + h * 0.5f),
                              ImGui::GetColorU32(menu_style_cap(p.cap)), h * 0.5f);
            dl->AddText(ImVec2(x + (w - name.x) * 0.5f, at.y - name.y * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text), p.name);
            dl->AddText(ImVec2(x + w + st.ItemInnerSpacing.x, at.y - does.y * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text), p.does);
        }
        x += w + st.ItemInnerSpacing.x + does.x;
    }
    return x - at.x;
}

/* Along the bottom, as the bar is along the top: the hint on the left,
 * wrapped short of the prompts on the right. As tall as the longest hint
 * of any row needs, so it stays still as the pad moves. */
static void draw_strip(int pad)
{
    const ImGuiIO &io = ImGui::GetIO();
    const ImGuiStyle &st = ImGui::GetStyle();
    const prompt *ps = pad ? k_pad_prompts : k_key_prompts;
    int n = MENU_PROMPTS;
    if (g_bind >= 0) {
        ps = pad ? &k_pad_bind_prompt : &k_key_bind_prompt;
        n = 1;
    }
    float prompts = draw_prompts(NULL, ps, n, ImVec2(0.0f, 0.0f));
    float wrap = io.DisplaySize.x - st.WindowPadding.x * 2 - st.ItemSpacing.x * 2 - prompts;
    float tall = ImGui::GetFontSize() * MENU_CAP_SIZE;
    g_hint_shown = g_hint_pad ? g_hint_pad : g_hint_pointer;
    for (const XBOX_MENU_ITEM &it : g_items)
        if (it.hint) tall = ImMax(tall, ImGui::CalcTextSize(it.hint, NULL, false, wrap).y);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    /* In whole pixels, as the window's place is, so it meets the window's
     * foot. */
    if (ImGui::BeginViewportSideBar("##hints", NULL, ImGuiDir_Down, ImCeil(tall + st.WindowPadding.y * 2),
                                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoFocusOnAppearing |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImVec2 at = ImGui::GetCursorScreenPos();
        if (g_hint_shown) {
            float h = ImGui::CalcTextSize(g_hint_shown, NULL, false, wrap).y;
            ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + (tall - h) * 0.5f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
            ImGui::TextUnformatted(g_hint_shown);
            ImGui::PopTextWrapPos();
        }
        draw_prompts(ImGui::GetWindowDrawList(), ps, n,
                     ImVec2(io.DisplaySize.x - st.WindowPadding.x - prompts, at.y + tall * 0.5f));
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

extern "C" const char *menu_hint(void)
{
    return g_hint_shown;
}

/* ---- a bind row's box (menu.h, XBOX_ITEM_BIND) --------------------------- */

extern "C" int menu_bind_waiting(void)
{
    return g_bind >= 0 && !g_bind_over ? g_items[g_bind].takes : 0;
}

/* The first press to bind(), and with a bind_second the box waits on for
 * the second, handed to that. */
extern "C" void menu_bind_press(int input)
{
    if (!menu_bind_waiting()) return;
    const XBOX_MENU_ITEM &it = g_items[g_bind];
    g_bind_refused = !(g_bind_second ? it.bind_second(input) : it.bind(input));
    if (g_bind_refused) return;
    if (!g_bind_second && it.bind_second) g_bind_second = 1;
    else                                  g_bind_over = g_pad_quiet = 1;
}

extern "C" void menu_bind_none(void)
{
    menu_bind_press(XBOX_BIND_NO_INPUT);
}

extern "C" int menu_bind_second(void)
{
    return menu_bind_waiting() && g_bind_second;
}

/* A pad's button or stick held, as Dear ImGui's SDL backend reads them: all
 * of the pad's, any pad's, so its navigation comes back with none held. */
static int pad_held(void)
{
    for (int k = ImGuiKey_Gamepad_BEGIN; k < ImGuiKey_Gamepad_END; k++)
        if (ImGui::IsKeyDown((ImGuiKey)k)) return 1;
    return 0;
}

/* Every box sits in the middle of the window, and stays there: put back each
 * frame, so it follows the window when that changes size. */
static void box_centre(void)
{
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
}

/* A bind row's box: its id, after the title it shows (menu_build). */
#define MENU_BIND_BOX "###bind"

/* A box over the menu: as big as what it says, and not to be dragged off. */
static bool box_begin(const char *title)
{
    box_centre();
    return ImGui::BeginPopupModal(title, NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
}

extern "C" void menu_build(int just_opened, int pad)
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    ImGuiIO &io = ImGui::GetIO();
    /* A bind row's box, and the pad held after it (menu_bind_waiting): the
     * pad isn't the menu's. Its press was the row's, or the box's cancel. */
    if (g_pad_quiet && g_bind < 0 && !pad_held()) g_pad_quiet = 0;
    const bool pad_not_menus = g_bind >= 0 || g_pad_quiet;
    /* B, or Esc: ImGui's own cancel (ImGuiKey_NavGamepadCancel reads g). */
    bool back = ImGui::IsKeyPressed(ImGuiKey_Escape, false)
             || (!pad_not_menus && ImGui::IsKeyPressed(ImGuiKey_NavGamepadCancel, false));
    int m, fold = 0, first = -1;
    const XBOX_MENU_ITEM *ask, *info, *bind;
    char bind_title[128];
    ImGuiWindow *bar;

    take_queued();
    g_hint_pad = g_hint_pointer = NULL;
    /* The game dimmed behind the menu, as Dear ImGui dims what is behind a
     * box (ModalWindowDimBg, menu_style.cpp), and fading with the menu. */
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize,
                                                  ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
    /* Opened: the bar alone, the pad on the first title (fold, below). A
     * menu or box still open from the last time is shut: ImGui keeps it
     * while the menu is hidden. */
    if (just_opened) {
        for (m = 0; m < XBOX_MENU_COUNT && !menu_has_items(m); m++) { }
        if (m < XBOX_MENU_COUNT) fold = 1, first = m;
        if (g.OpenPopupStack.Size) ImGui::ClosePopupToLevel(0, false);
        g_ask = g_info = g_reopen = g_reopen_row = g_bind = -1;
    }
    if (ImGui::BeginMainMenuBar()) {
        bar = ImGui::GetCurrentWindow();
        /* The menu stays until F1 or Back (menu_sdl.cpp). B is a cancel, a
         * level at a time: it shuts a box, as Cancel does (below); ImGui
         * shuts a choice list, back to its menu, and a menu -- but leaves the
         * pad nowhere, so the pad goes to that menu's title on the bar
         * (fold), where B does no more. A click outside the menus -- in the
         * game, or on the bar past its titles -- has ImGui shut them all, and
         * the pad goes to the title of the one that was open, the same way.
         * Not while the player walks the bar with Left / Right (the bar's
         * titles have the pad), nor while the mouse moves from one menu to
         * the next (the next is open, not yet drawn). */
        if (g_reopen < 0
         && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)
         && (back || !(ImGui::IsWindowFocused() && g.NavLayer == ImGuiNavLayer_Menu)))
            fold = 1;
        /* A row chosen leaves its menu open: the tick moves where it can be
         * seen, and a pad stays where it was. */
        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
        const menu_room whole = room_begin();
        for (m = 0; m < XBOX_MENU_COUNT; m++) {
            if (!menu_has_items(m)) continue;
            if (m == g_reopen) ImGui::OpenPopup(k_menu_title[m]);
            bool open = ImGui::BeginMenu(k_menu_title[m]);
            /* Left / Right in a menu go on from its title on the bar, which
             * ImGui keeps only for a menu the pad opened there: not for one
             * the mouse opened, or one opened above. The title is the last
             * item still (BeginMenu keeps it so). Opening, the pad goes to
             * the first title from here too. */
            if (open || m == first) {
                bar->NavLastIds[ImGuiNavLayer_Menu] = ImGui::GetItemID();
                bar->NavRectRel[ImGuiNavLayer_Menu] =
                    ImGui::WindowRectAbsToRel(bar, ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
            }
            if (!open) continue;
            g_last_menu = m;
            if (m == g_reopen) g_reopen = -1;
            number_cols cols = number_columns(m, NULL);
            /* The rows in the order added, each with those under it; then
             * any whose row to go under, or submenu, isn't there; then the
             * rows marked last, below the rest. A submenu's rows are drawn
             * in it (draw_item). */
            for (const XBOX_MENU_ITEM &it : g_items)
                if (it.menu == m && !it.group && !it.under && !it.last) draw_row(&it, &cols);
            for (const XBOX_MENU_ITEM &it : g_items)
                if (it.menu == m && (it.group ? !drawn_in(it) : it.under && !under_row(&it)))
                    draw_unplaced(&it, &cols);
            for (const XBOX_MENU_ITEM &it : g_items)
                if (it.menu == m && !it.group && !it.under && it.last) draw_row(&it, &cols);
            ImGui::EndMenu();
        }
        room_end(whole);
        /* The pad to the title it left, as ImGui does when Left / Right run
         * off a menu (EndMenuBar). After the titles: B on the bar has ImGui
         * ask for the bar's first title, and one drawn after this would take
         * the pad next frame. */
        if (fold) {
            ImGui::FocusWindow(bar);
            ImGui::SetNavID(bar->NavLastIds[ImGuiNavLayer_Menu], ImGuiNavLayer_Menu, 0,
                            bar->NavRectRel[ImGuiNavLayer_Menu]);
            ImGui::SetNavCursorVisibleAfterMove();
        }
        ImGui::PopItemFlag();
        ImGui::EndMainMenuBar();
    }
    draw_strip(pad);
    /* Back on a bind row: once, as its menu and submenu open again. */
    if (g_reopen < 0) g_reopen_row = -1;

    /* Opened from the top level: a popup opened inside a menu would close
     * with it. */
    ask  = g_ask  >= 0 ? &g_items[g_ask]  : NULL;
    info = g_info >= 0 ? &g_items[g_info] : NULL;
    bind = g_bind >= 0 ? &g_items[g_bind] : NULL;
    if (ask && !ImGui::IsPopupOpen("Are you sure?")) ImGui::OpenPopup("Are you sure?");
    if (info && !ImGui::IsPopupOpen(info->label)) ImGui::OpenPopup(info->label);
    if (bind && !ImGui::IsPopupOpen(MENU_BIND_BOX)) ImGui::OpenPopup(MENU_BIND_BOX);

    if (ask && box_begin("Are you sure?")) {
        ImGui::TextUnformatted(g_ask_pick >= 0 ? ask->choices[g_ask_pick] : ask->label);
        ImGui::Spacing();
        ImGui::TextUnformatted(ask->confirm);
        ImGui::Spacing();
        bool ok = ImGui::Button("OK");
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel");
        ImGui::SetItemDefaultFocus();   /* a pad starts on Cancel: OK cannot be undone */
        if (ok) apply(ask, g_ask_pick);
        if (ok || cancel || back) { g_ask = -1; box_closed(); }
        ImGui::EndPopup();
    }

    if (info && box_begin(info->label)) {
        ImGui::TextUnformatted(info->text());
        ImGui::Spacing();
        bool ok = ImGui::Button("OK");
        ImGui::SetItemDefaultFocus();
        if (ok || back) { g_info = -1; box_closed(); }
        ImGui::EndPopup();
    }

    /* A bind row's: no buttons, as the next press is the row's (menu.h,
     * menu_bind_waiting). Titled with its submenu, as the layout's page,
     * where it has one. Esc reaches it only while it waits for a pad, and
     * then shuts it, handing over nothing more. */
    if (bind) snprintf(bind_title, sizeof bind_title, "%s" MENU_BIND_BOX, bind->group ? bind->group : bind->label);
    if (bind && box_begin(bind_title)) {
        ImGui::Text(bind->takes == XBOX_BIND_PAD ? "Press a %sbutton for %s, or %s for none."
                                                 : "Press a %skey or mouse button for %s, or %s for none.",
                    g_bind_second ? "second " : "", bind->label, pad ? "Back" : "F1");
        if (g_bind_refused) {
            ImGui::Spacing();
            ImGui::TextUnformatted("That one can't be used.");
        }
        if (g_bind_over || back) {
            int row = g_bind;
            g_bind = -1;
            g_pad_quiet = 1;
            box_closed(row);
        }
        ImGui::EndPopup();
    }
    /* The pad's navigation off from the next frame while the pad isn't the
     * menu's, and on again with nothing held. */
    if (g_bind >= 0 || g_pad_quiet) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    else                            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
}

/* Open, the fade is never quite 0: Dear ImGui skips the rows of a window
 * begun at 0 (Begin), and the menu must take the pad from its first frame,
 * where the time since the last is none. */
#define MENU_FADE_LEAST (1.0f / 255.0f)

extern "C" float menu_fade(float fade, int open, float seconds)
{
    float step = seconds / MENU_FADE_SECONDS;
    if (open) return ImClamp(fade + step, MENU_FADE_LEAST, 1.0f);
    /* Shut, the controls are the game's again (xbox_MenuIsOpen): the menu,
     * still in sight as it fades, must not act on them too. */
    ImGui::GetIO().ClearEventsQueue();
    return ImMax(fade - step, 0.0f);
}

extern "C" void menu_build_busy(const char *what, int done, int total)
{
    const ImGuiIO &io = ImGui::GetIO();
    char line[32];
    box_centre();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.5f, 0.0f), ImGuiCond_Always);
    if (ImGui::Begin("##busy", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted(what);
        snprintf(line, sizeof line, "%d of %d", done, total);
        ImGui::ProgressBar(total > 0 ? (float)done / (float)total : 0.0f, ImVec2(-1.0f, 0.0f), line);
    }
    ImGui::End();
}
