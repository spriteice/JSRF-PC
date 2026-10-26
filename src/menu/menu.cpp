/*
 * menu.cpp -- draws the rows added with xbox_MenuAdd (menu.h).
 *
 * Built with -fno-exceptions -fno-rtti -fno-threadsafe-statics, as Dear
 * ImGui is: nothing here may need the C++ runtime library (no new, no STL),
 * so the engine still links against libc alone.
 */
#include "menu.h"
#include "imgui.h"
#include "imgui_internal.h"   /* the bar's nav state: menu_build */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const k_menu_title[] = { "File", "Display", "Graphics", "Controller", "Developer" };
static_assert(IM_ARRAYSIZE(k_menu_title) == XBOX_MENU_COUNT, "a title for each menu in menu.h");

/* The rows, and the boxes open over them -- by index: rows still arriving
 * can move them. The window's thread only. */
static ImVector<XBOX_MENU_ITEM> g_items;
static int g_ask = -1;          /* waiting for OK: its confirm text */
static int g_ask_pick;          /* ... and the choice, for a CHOICE or TOGGLES */
static int g_info = -1;         /* an INFO box to open */

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

static void take_queued(void)
{
    queue_lock();
    for (int i = 0; i < g_nqueued; i++)
        g_items.push_back(g_queued[i]);
    g_nqueued = 0;
    queue_unlock();
}

static void apply(const XBOX_MENU_ITEM *it, int pick)
{
    if (pick >= 0) it->pick(pick);
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

static double step_from(const XBOX_MENU_ITEM *it, double v, int dir)
{
    double n = (v - it->min) / it->step;
    n = dir > 0 ? floor(n + MENU_STEP_SLACK) + 1 : ceil(n - MENU_STEP_SLACK) - 1;
    v = it->min + n * it->step;
    return v < it->min ? it->min : v > it->max ? it->max : v;
}

/* A menu's NUMBER rows line up: the labels in one column, then boxes as
 * wide as the widest value any of them shows. */
struct number_cols { float label, box; };

static number_cols number_columns(int m)
{
    number_cols c = { 0, 0 };
    char text[32];
    for (const XBOX_MENU_ITEM &it : g_items) {
        if (it.menu != m || it.kind != XBOX_ITEM_NUMBER) continue;
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
    ImGui::SameLine(x + cols->label + st.ItemSpacing.x);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);   /* held: on it goes */
    if (ImGui::Button("-", ImVec2(side, side))) to = step_from(it, v, -1);
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
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(side, side))) to = step_from(it, v, 1);
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
        break;
    case XBOX_ITEM_TOGGLE:
        if (ImGui::MenuItem(it->label, NULL, it->state() != 0)) chosen(it, -1);
        break;
    case XBOX_ITEM_CHOICE:
        if (ImGui::BeginMenu(it->label)) {
            int cur = it->state(), i;
            for (i = 0; i < it->nchoices; i++)
                if (ImGui::MenuItem(it->choices[i], NULL, i == cur) && i != cur)
                    chosen(it, i);
            ImGui::EndMenu();
        }
        break;
    case XBOX_ITEM_TOGGLES:
        if (ImGui::BeginMenu(it->label)) {
            int on = it->state(), i;
            for (i = 0; i < it->nchoices; i++)
                if (ImGui::MenuItem(it->choices[i], NULL, (on >> i) & 1)) chosen(it, i);
            ImGui::EndMenu();
        }
        break;
    case XBOX_ITEM_INFO:
        if (ImGui::MenuItem(it->label)) g_info = g_items.index_from_ptr(it);
        break;
    case XBOX_ITEM_NUMBER:
        draw_number(it, cols);
        break;
    }
}

static int menu_has_items(int m)
{
    for (const XBOX_MENU_ITEM &it : g_items)
        if (it.menu == m) return 1;
    return 0;
}

/* A box ends: back to the menu it was asked from, so a pad is never left
 * with nothing selected. */
static int g_last_menu, g_reopen = -1;

static void box_closed(void)
{
    ImGui::CloseCurrentPopup();
    g_reopen = g_last_menu;
}

/* Every box sits in the middle of the window, and stays there: put back each
 * frame, so it follows the window when that changes size. */
static void box_centre(void)
{
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
}

/* A box over the menu: as big as what it says, and not to be dragged off. */
static bool box_begin(const char *title)
{
    box_centre();
    return ImGui::BeginPopupModal(title, NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
}

extern "C" void menu_build(int just_opened)
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    /* B, or Esc: ImGui's own cancel (ImGuiKey_NavGamepadCancel reads g). */
    bool back = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_NavGamepadCancel, false);
    int m, fold = 0, first = -1;
    const XBOX_MENU_ITEM *ask, *info;
    ImGuiWindow *bar;

    take_queued();
    /* Opened: the bar alone, the pad on the first title (fold, below). A
     * menu or box still open from the last time is shut: ImGui keeps it
     * while the menu is hidden. */
    if (just_opened) {
        for (m = 0; m < XBOX_MENU_COUNT && !menu_has_items(m); m++) { }
        if (m < XBOX_MENU_COUNT) fold = 1, first = m;
        if (g.OpenPopupStack.Size) ImGui::ClosePopupToLevel(0, false);
        g_ask = g_info = g_reopen = -1;
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
            number_cols cols = number_columns(m);
            for (const XBOX_MENU_ITEM &it : g_items)
                if (it.menu == m) draw_item(&it, &cols);
            ImGui::EndMenu();
        }
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

    /* Opened from the top level: a popup opened inside a menu would close
     * with it. */
    ask  = g_ask  >= 0 ? &g_items[g_ask]  : NULL;
    info = g_info >= 0 ? &g_items[g_info] : NULL;
    if (ask && !ImGui::IsPopupOpen("Are you sure?")) ImGui::OpenPopup("Are you sure?");
    if (info && !ImGui::IsPopupOpen(info->label)) ImGui::OpenPopup(info->label);

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
