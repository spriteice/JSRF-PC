/* Run with: python3 tests/menu/run.py
 *
 * The in-game menu, driven with a pad and no window: Dear ImGui's null
 * backends, the menu's own drawing (src/menu/menu.cpp), and rows standing in
 * for the window's settings. Checks the order of the menus, that it opens to
 * the bar alone with a pad on File's title, and that each kind of row reaches
 * its owner's function -- through the confirmation box when it has one. Then
 * rows that arrive once the menu is up: from one of its own rows, and from
 * another thread while a box is open over the rows it was asked from -- all
 * above a row marked last, which arrived before any of them. Rows placed
 * under another (under): two added before the row they name, drawn right
 * below it in the order they were added; one under the row marked last, as
 * Display's Music info in pause under Skip intro, drawn below it, at the very
 * end; and one naming a row that isn't there, drawn below the late rows and
 * above the last. Then
 * that nothing but the window (F1, Back) shuts it: B and Esc go back a level
 * at a time, to the bar and no further; a click outside the menus, in the
 * game or on the bar past its titles, shuts them all, back to the bar -- and
 * a pad still has somewhere to stand after each. Then numbers: - and + a step
 * at a time, held down, at the ends, and from a value between two steps. And
 * a submenu of switches, as Developer > Logging: each flips its own, and the
 * submenu stays open; and a toggle that asks first. The game dimmed behind
 * the menu, and the fade (menu_fade): out as the menu shuts, taking no input
 * as it goes, and in again as it opens. The strip along the bottom: the
 * hint of the row the pad or the pointer is on, a choice list's and a
 * number's their row's, none on the bar or in a box; the pad's buttons or
 * the keys; and all of it inside the strip, two lines tall at 640 x 480 for
 * the longest hint a row may have. Last, the busy box (menu_build_busy) with
 * no menu, in the middle of the window.
 */
#include "menu.h"
#include "menu_style.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_null.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#if !defined(_WIN32)
#  include <pthread.h>
#endif

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail = 1; } } while (0)

static int s_full, s_wide, s_scale = 1, s_exit, s_full_runs, s_wide_runs, s_picks = -1;
/* Each row drawn that counts takes the next number, so the order they are
 * drawn in can be told; Widescreen counts, to place the rows under it. */
static int s_drawn_seq, s_wide_at;
static float s_wide_y, s_under_y[2];
static int  full_state(void)   { return s_full; }
static void full_run(void)     { s_full = !s_full; s_full_runs++; }
static int  wide_state(void)   { s_wide_at = ++s_drawn_seq; s_wide_y = ImGui::GetCursorScreenPos().y; return s_wide; }
static void wide_run(void);
static int  scale_state(void)  { return s_scale; }
static void scale_pick(int i)  { s_picks = i; }
static void exit_run(void)     { s_exit = 1; }
static const char *about_text(void) { return "JSRF-PC test"; }
/* Two rows' hints, to find in the strip: Render scale's as long as a hint
 * may be (menu.h), for the strip's height at 640 x 480. */
static const char k_wide_hint[] = "Shows more of the world, either side.";
static const char k_scale_hint[] = "Sets how sharp the picture is: higher asks more of your graphics card.";
static_assert(sizeof k_scale_hint - 1 == MENU_HINT_LONGEST, "as long as a hint may be");
static const char *const k_scales[] = { "1x  (640 x 480)", "2x  (1280 x 960)",
                                        "3x  (1920 x 1440)", "4x  (2560 x 1920)" };

/* Late rows: toggles that count how often they are drawn. */
static int s_late_drawn, s_late_at, s_late_first_at, s_last_at;
static int  late_state(void)
{
    s_late_at = ++s_drawn_seq;
    if (s_late_drawn++ == 0) s_late_first_at = s_late_at;
    return 0;
}
static void late_run(void)   { }
static const XBOX_MENU_ITEM k_late = {
    XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Late", late_state, late_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
    "Came late." };

/* A row marked last, as Display's Skip intro: added before the others, and
 * drawn below them all. */
static int  last_state(void) { s_last_at = ++s_drawn_seq; return 0; }
static void last_run(void)   { }
static const XBOX_MENU_ITEM k_last = {
    XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Skip intro", last_state, last_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 1, NULL,
    "Starts at the title screen." };

/* Rows placed under another, as Display's Draw distance under Render scale:
 * added in one call before the row they name, and one naming a row that
 * isn't there. Render scale, a submenu, asks its state only when open, so
 * where it is drawn is told by where the rows either side are. */
static int s_under_at[2], s_unplaced_at, s_under_last_at;
static int  under0_state(void)   { s_under_at[0] = ++s_drawn_seq; s_under_y[0] = ImGui::GetCursorScreenPos().y; return 0; }
static int  under1_state(void)   { s_under_at[1] = ++s_drawn_seq; s_under_y[1] = ImGui::GetCursorScreenPos().y; return 0; }
static int  unplaced_state(void) { s_unplaced_at = ++s_drawn_seq; return 0; }
static int  under_last_state(void) { s_under_last_at = ++s_drawn_seq; return 0; }
static void under_run(void)      { }
static const XBOX_MENU_ITEM k_under[] = {
    { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Under one", under0_state, under_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, "Render scale", "The first under it." },
    { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Under two", under1_state, under_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, "Render scale", "The second under it." },
    { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Unplaced", unplaced_state, under_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, "No such row", "Has nowhere to go." },
    { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Under the last", under_last_state, under_run, NULL, NULL, 0, NULL, NULL,
      NULL, NULL, 0, 0, 0, NULL, 0, "Skip intro", "Goes under the last." },
};

/* Numbers, as the Controller menu's: a sensitivity in whole steps, and a
 * deadzone in 5% steps that starts between two (as the settings file may). */
static double s_sens = 5, s_dead = 7;
static int    s_sets;
static double sens_value(void)    { return s_sens; }
static void   sens_set(double v)  { s_sens = v; s_sets++; }
static double dead_value(void)    { return s_dead; }
static void   dead_set(double v)  { s_dead = v; s_sets++; }

/* Developer's rows: a toggle, a submenu of switches, a bit each, and a
 * toggle that asks first, as Graphics > Shader cache does. */
static int s_log = 1, s_log_runs, s_switches, s_flips, s_asked, s_asked_runs;
static int  log_state(void)       { return s_log; }
static void log_run(void)         { s_log = !s_log; s_log_runs++; }
static int  switches_state(void)  { return s_switches; }
static void switch_pick(int i)    { s_switches ^= 1 << i; s_flips++; }
static int  asked_state(void)     { return s_asked; }
static void asked_run(void)       { s_asked = !s_asked; s_asked_runs++; }
static const char *const k_switches[] = { "First (Restart)", "Second (Restart)", "Third (Restart)" };

/* Widescreen's owner adds a row as it runs, on the window's thread. */
static void wide_run(void) { s_wide = !s_wide; s_wide_runs++; xbox_MenuAdd(&k_late, 1); }

/* Another module's rows, one call each: enough for the table to move. */
#define LATE_ROWS 200
static int s_adding_done;
static void *add_late_rows(void *arg)
{
    (void)arg;
    for (int i = 0; i < LATE_ROWS; i++) xbox_MenuAdd(&k_late, 1);
    __atomic_store_n(&s_adding_done, 1, __ATOMIC_RELEASE);
    return NULL;
}

static int g_just = 1;
static int g_pad = 1;           /* the strip shows the pad's buttons, not the keys */

/* Frames drawn with no menu, or box, in them, and the bar not the player's
 * either. */
static int s_bare;
static int bar_focused(void);
static void count_bare(void)
{
    for (const ImGuiPopupData &p : ImGui::GetCurrentContext()->OpenPopupStack)
        if (p.Window && p.Window->Active) return;
    if (!bar_focused()) s_bare++;
}

/* The window's size, when not the null backend's 1920 x 1080. */
static ImVec2 g_display;

/* One frame, with these gamepad keys held for it. */
static void frame(ImGuiKey press = ImGuiKey_None)
{
    ImGuiIO &io = ImGui::GetIO();
    if (press != ImGuiKey_None) io.AddKeyEvent(press, true);
    ImGui_ImplNull_NewFrame();
    if (g_display.x > 0.0f) io.DisplaySize = g_display;
    ImGui::NewFrame();
    menu_build(g_just, g_pad);
    g_just = 0;
    count_bare();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    if (press != ImGuiKey_None) {
        io.AddKeyEvent(press, false);
        ImGui_ImplNull_NewFrame(); ImGui::NewFrame();
        menu_build(0, g_pad);
        count_bare();
        ImGui::Render();
    }
}
static void frames(int n) { while (n--) frame(); }

/* A frame as the window builds it (menu_sdl.cpp's xbox_MenuFrame) as the
 * menu opens or shuts: the fade first, then the menu at its alpha, until it
 * has faded out. hold: a key the backend reads as held this frame. */
static float g_fade = 1.0f;
static int   s_built;
static void fade_frame(int open, ImGuiKey hold = ImGuiKey_None)
{
    ImGuiIO &io = ImGui::GetIO();
    if (hold != ImGuiKey_None) io.AddKeyEvent(hold, true);
    ImGui_ImplNull_NewFrame();
    g_fade = menu_fade(g_fade, open, g_just ? 0.0f : io.DeltaTime);
    ImGui::GetStyle().Alpha = g_fade;
    ImGui::NewFrame();
    s_built = open || g_fade > 0.0f;
    if (s_built) menu_build(g_just, g_pad);
    g_just = 0;
    ImGui::Render();
}

/* The game's dim behind the menu: the background's first rectangle's alpha,
 * or -1 if it has none. */
static int dim_alpha(void)
{
    const ImDrawList *bg = ImGui::GetBackgroundDrawList();
    return bg->VtxBuffer.Size >= 4 ? (int)((bg->VtxBuffer[0].col >> IM_COL32_A_SHIFT) & 0xFF) : -1;
}

/* The hint the strip shows is this one. */
static int hint_is(const char *want)
{
    const char *h = menu_hint();
    if (!(h == want || (h && want && !strcmp(h, want))))
        fprintf(stderr, "hint: [%s], not [%s]\n", h ? h : "none", want ? want : "none");
    return h == want || (h && want && !strcmp(h, want));
}

/* The strip along the bottom: across the window at its foot, with all it
 * draws inside it, its hint `lines` lines tall (0: shorter than 2); and
 * whether it has the pad's A in it, by its colour. */
static int strip_fits(int lines)
{
    const ImGuiIO &io = ImGui::GetIO();
    const ImGuiStyle &st = ImGui::GetStyle();
    ImGuiWindow *w = ImGui::FindWindowByName("##hints");
    if (!w || !w->Active) return 0;
    float inside = w->Size.y - st.WindowPadding.y * 2, line = ImGui::GetTextLineHeight();
    int in = 1;
    for (const ImDrawVert &v : w->DrawList->VtxBuffer)
        in &= v.pos.x >= w->Pos.x && v.pos.x <= w->Pos.x + w->Size.x &&
              v.pos.y >= w->Pos.y && v.pos.y <= w->Pos.y + w->Size.y;
    fprintf(stderr, "strip: %.0f,%.0f %.0fx%.0f in %.0fx%.0f, %.1f lines inside, all in %d\n", w->Pos.x, w->Pos.y,
            w->Size.x, w->Size.y, io.DisplaySize.x, io.DisplaySize.y, inside / line, in);
    return in && w->Pos.x == 0.0f && w->Size.x == io.DisplaySize.x && w->Pos.y + w->Size.y == io.DisplaySize.y &&
           (lines ? fabsf(inside - lines * line) < 1.0f : inside < 2 * line);
}
static int strip_has_a(void)
{
    ImGuiWindow *w = ImGui::FindWindowByName("##hints");
    ImU32 a = ImGui::GetColorU32(menu_style_cap(MENU_CAP_A));
    if (w)
        for (const ImDrawVert &v : w->DrawList->VtxBuffer)
            if (v.col == a) return 1;
    return 0;
}

/* One frame of only the busy box. */
static void busy_frame(void)
{
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    menu_build_busy("Building shaders", 57, 121);
    ImGui::Render();
}

static const char *nav_state(void)
{
    static char buf[160];
    ImGuiContext &g = *ImGui::GetCurrentContext();
    snprintf(buf, sizeof buf, "nav=%s layer=%d focusedbar=%d", g.NavWindow ? g.NavWindow->Name : "-", (int)g.NavLayer,
             g.NavWindow && !strcmp(g.NavWindow->Name, "##MainMenuBar"));
    return buf;
}
static const char *open_menu(void)
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    return g.OpenPopupStack.Size ? (g.OpenPopupStack.back().Window
                                    ? g.OpenPopupStack.back().Window->Name : "(new)") : "";
}
static int bar_focused(void)
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    return g.NavWindow && !strcmp(g.NavWindow->Name, "##MainMenuBar") && g.NavLayer == ImGuiNavLayer_Menu;
}

/* The window by this name is up, in the middle of the display. */
static int centred(const char *name)
{
    const ImGuiIO &io = ImGui::GetIO();
    ImGuiWindow *w = ImGui::FindWindowByName(name);
    if (!w || !w->Active) return 0;
    float cx = w->Pos.x + w->Size.x * 0.5f, cy = w->Pos.y + w->Size.y * 0.5f;
    fprintf(stderr, "%s: centre %.0f,%.0f in %.0fx%.0f\n", name, cx, cy, io.DisplaySize.x, io.DisplaySize.y);
    return fabsf(cx - io.DisplaySize.x * 0.5f) < 2.0f && fabsf(cy - io.DisplaySize.y * 0.5f) < 2.0f;
}

/* The mouse: the pointer to x, y; a click where it is. */
static void pointer(float x, float y) { ImGui::GetIO().AddMousePosEvent(x, y); }
static void click(void)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMouseButtonEvent(0, true);  frame();
    io.AddMouseButtonEvent(0, false); frame();
}

int main(void)
{
    /* Developer first, then Controller, then Display, then File: the order
     * on screen is the enum's, not the order rows arrive in. Before them
     * all, Display's last row. */
    const XBOX_MENU_ITEM developer[] = {
        { XBOX_MENU_DEVELOPER, XBOX_ITEM_TOGGLE, "Log to file", log_state, log_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
          "Writes the log to a file." },
        { XBOX_MENU_DEVELOPER, XBOX_ITEM_TOGGLES, "Logging", switches_state, NULL, switch_pick, k_switches, 3, NULL, NULL,
          NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Adds lines to the log." },
        { XBOX_MENU_DEVELOPER, XBOX_ITEM_TOGGLE, "Asks (Restart)", asked_state, asked_run, NULL, NULL, 0, NULL,
          "The game restarts to apply it.", NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Asks first." },
    };
    const XBOX_MENU_ITEM controller[] = {
        { XBOX_MENU_CONTROLLER, XBOX_ITEM_NUMBER, "Sensitivity", NULL, NULL, NULL, NULL, 0, NULL, NULL,
          sens_value, sens_set, 1, 10, 1, "%g", 0, NULL, "Sets how fast the camera turns." },
        { XBOX_MENU_CONTROLLER, XBOX_ITEM_NUMBER, "Deadzone", NULL, NULL, NULL, NULL, 0, NULL, NULL,
          dead_value, dead_set, 0, 100, 5, "%g%%", 0, NULL, "Sets how far the stick moves first." },
    };
    const XBOX_MENU_ITEM display[] = {
        { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Fullscreen", full_state, full_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
          "Fills the screen." },
        { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Widescreen (16:9)", wide_state, wide_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
          k_wide_hint },
        { XBOX_MENU_DISPLAY, XBOX_ITEM_CHOICE, "Render scale", scale_state, NULL, scale_pick, k_scales, 4, NULL,
          "Apply selection and restart, losing unsaved progress?", NULL, NULL, 0, 0, 0, NULL, 0, NULL,
          k_scale_hint },
    };
    const XBOX_MENU_ITEM file[] = {
        { XBOX_MENU_FILE, XBOX_ITEM_INFO, "About", NULL, NULL, NULL, NULL, 0, about_text, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
          "Shows the version." },
        { XBOX_MENU_FILE, XBOX_ITEM_SEPARATOR, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL, NULL },
        { XBOX_MENU_FILE, XBOX_ITEM_ACTION, "Exit", NULL, exit_run, NULL, NULL, 0, NULL,
          "Close the game, losing unsaved progress?", NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Closes the game." },
    };
    xbox_MenuAdd(&k_last, 1);
    xbox_MenuAdd(k_under, 4);
    xbox_MenuAdd(developer, 3);
    xbox_MenuAdd(controller, 2);
    xbox_MenuAdd(display, 3);
    xbox_MenuAdd(file, 3);

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplNull_Init();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    /* The fade on its own: in or out in MENU_FADE_SECONDS, by the time since
     * the last frame, from where it was when it turns back, and within 0 and
     * 1 however long that time; open, never 0, even with no time since the
     * last frame (Dear ImGui would skip its rows). */
    CHECK(menu_fade(0.0f, 1, 0.0f) > 0.0f && menu_fade(0.0f, 1, 0.0f) < 0.01f);
    CHECK(fabsf(menu_fade(0.0f, 1, MENU_FADE_SECONDS * 0.5f) - 0.5f) < 1e-5f);
    CHECK(menu_fade(0.5f, 1, MENU_FADE_SECONDS) == 1.0f);
    CHECK(menu_fade(0.0f, 1, 3600.0f) == 1.0f);
    CHECK(fabsf(menu_fade(1.0f, 0, MENU_FADE_SECONDS * 0.25f) - 0.75f) < 1e-5f);
    CHECK(fabsf(menu_fade(0.3f, 1, MENU_FADE_SECONDS * 0.1f) - 0.4f) < 1e-5f);
    CHECK(menu_fade(0.25f, 0, MENU_FADE_SECONDS) == 0.0f);
    CHECK(menu_fade(0.0f, 0, 3600.0f) == 0.0f);

    /* Opened: the bar alone, the pad on File's title; A opens File. */
    frames(3);
    fprintf(stderr, "open popup after opening: [%s] %s, cursor %d\n", open_menu(), nav_state(),
            (int)ImGui::GetCurrentContext()->NavCursorVisible);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    CHECK(ImGui::GetCurrentContext()->NavCursorVisible);
    /* On the bar, no hint; the strip along the bottom, with the pad's
     * buttons. */
    CHECK(hint_is(NULL));
    CHECK(strip_fits(0) && strip_has_a());
    /* The game dimmed behind it: the first thing drawn, across the window,
     * in the style's colour for what is behind a box. */
    {
        const ImDrawList *bg = ImGui::GetBackgroundDrawList();
        CHECK(bg->VtxBuffer.Size >= 4);
        if (bg->VtxBuffer.Size >= 4) {
            const ImVec2 a = bg->VtxBuffer[0].pos, b = bg->VtxBuffer[2].pos;
            fprintf(stderr, "the dim: %.0f,%.0f to %.0f,%.0f, %08X\n", a.x, a.y, b.x, b.y, bg->VtxBuffer[0].col);
            CHECK(a.x == 0.0f && a.y == 0.0f && b.x == io.DisplaySize.x && b.y == io.DisplaySize.y);
            CHECK(bg->VtxBuffer[0].col == ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
        }
    }
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(strstr(open_menu(), "File") != NULL);
    {   /* menu bar order */
        ImGuiWindow *bar = ImGui::FindWindowByName("##MainMenuBar");
        CHECK(bar != NULL);
    }

    /* Right walks the bar to Display (File closes); A opens it, the cursor
     * on its first row. Down, A: Widescreen flipped, once, and Display is
     * still open with the cursor where it was. */
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    fprintf(stderr, "after right: [%s] %s\n", open_menu(), nav_state());
    CHECK(bar_focused());
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "after A: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Display") != NULL);
    CHECK(hint_is("Fills the screen."));
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is(k_wide_hint));
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "full runs %d, wide runs %d, menu [%s] %s\n", s_full_runs, s_wide_runs, open_menu(), nav_state());
    CHECK(s_wide_runs == 1 && s_full_runs == 0);
    CHECK(strstr(open_menu(), "Display") != NULL);
    fprintf(stderr, "the row Widescreen added: drawn %d times\n", s_late_drawn);
    CHECK(s_late_drawn > 0);

    /* Down to Render scale, Right into it (the cursor on 1x), down twice to
     * 3x, A: asked first, with Cancel the default... */
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is(k_scale_hint));
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    fprintf(stderr, "in render scale: [%s]\n", open_menu());
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is(k_scale_hint));       /* its choices: still the row's */
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    fprintf(stderr, "after choosing: [%s], picks %d\n", open_menu(), s_picks);
    CHECK(strstr(open_menu(), "Are you sure?") != NULL);
    CHECK(hint_is(NULL));               /* in a box, none */
    CHECK(s_picks == -1);
    /* ... so A there cancels, and Display comes back. */
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    fprintf(stderr, "after Cancel: [%s], picks %d\n", open_menu(), s_picks);
    CHECK(s_picks == -1);
    CHECK(strstr(open_menu(), "Display") != NULL);
    /* Again (Display came back with the cursor on its first row), and this
     * time Left to OK, A: picked, index 2. */
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    fprintf(stderr, "cursor before right: %s\n", nav_state());
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    fprintf(stderr, "asked again: [%s]\n", open_menu());
    /* While the box is up, another module adds its rows from its own thread,
     * as the window keeps drawing. */
#if defined(_WIN32)
    add_late_rows(NULL);
#else
    {
        pthread_t t;
        CHECK(pthread_create(&t, NULL, add_late_rows, NULL) == 0);
        while (!__atomic_load_n(&s_adding_done, __ATOMIC_ACQUIRE)) frame();
        pthread_join(t, NULL);
    }
#endif
    frames(1);
    CHECK(strstr(open_menu(), "Are you sure?") != NULL);
    frame(ImGuiKey_GamepadDpadLeft); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "after OK: picks %d, menu [%s]\n", s_picks, open_menu());
    CHECK(s_picks == 2);
    /* Display is back, with every row: Widescreen's and the other thread's. */
    s_late_drawn = 0;
    frames(1);
    fprintf(stderr, "late rows drawn in one frame: %d of %d; the last row drawn #%d, the last late row #%d\n",
            s_late_drawn, 1 + LATE_ROWS, s_last_at, s_late_at);
    CHECK(strstr(open_menu(), "Display") != NULL);
    CHECK(s_late_drawn == 1 + LATE_ROWS);
    CHECK(s_last_at > s_late_at);
    /* The rows under Render scale: right after it, in their order, before
     * the late rows -- after Widescreen with one row's height between, which
     * is Render scale's; the one whose row isn't there after the late rows,
     * above the last; and the one under the last right after it. */
    fprintf(stderr, "widescreen #%d at y %.1f, under #%d and #%d at y %.1f and %.1f, first late #%d, "
            "unplaced #%d, last #%d, under the last #%d\n", s_wide_at, s_wide_y, s_under_at[0], s_under_at[1],
            s_under_y[0], s_under_y[1], s_late_first_at, s_unplaced_at, s_last_at, s_under_last_at);
    CHECK(s_under_at[0] == s_wide_at + 1 && s_under_at[1] == s_under_at[0] + 1);
    CHECK(s_under_y[1] > s_under_y[0] && s_under_y[0] > s_wide_y);
    CHECK(fabsf((s_under_y[0] - s_wide_y) - 2.0f * (s_under_y[1] - s_under_y[0])) < 0.5f);
    CHECK(s_late_first_at == s_under_at[1] + 1);
    CHECK(s_unplaced_at == s_late_at + 1 && s_last_at == s_unplaced_at + 1);
    CHECK(s_under_last_at == s_last_at + 1 && s_under_last_at == s_drawn_seq);

    /* About: Left walks the bar back to File, A opens it, A on About: a box.
     * OK brings File back. */
    frame(ImGuiKey_GamepadDpadLeft); frames(2);
    fprintf(stderr, "left: [%s] %s\n", open_menu(), nav_state());
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "file again: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "File") != NULL);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "about: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "About") != NULL);
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    fprintf(stderr, "after about OK: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "File") != NULL);

    /* B is a cancel, a level at a time. In File: File shuts, and the pad is
     * on its title on the bar, in sight. On the bar, B and Esc do nothing:
     * A opens File again. */
    s_bare = 0;
    frame(ImGuiKey_GamepadFaceRight); frames(1);
    fprintf(stderr, "after B: [%s] %s, cursor %d\n", open_menu(), nav_state(),
            (int)ImGui::GetCurrentContext()->NavCursorVisible);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    CHECK(ImGui::GetCurrentContext()->NavCursorVisible);
    frame(ImGuiKey_GamepadFaceRight); frame(ImGuiKey_Escape); frames(2);
    fprintf(stderr, "after B, Esc on the bar: [%s] %s\n", open_menu(), nav_state());
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "A: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "File") != NULL);
    /* Right to Display's title, and B there: the pad stays on Display. */
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "Right, B, A: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Display") != NULL);
    /* In Render scale's list, B goes back to Display; B again, to its title.
     * Nothing was picked, and the bar never stood empty. */
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    fprintf(stderr, "in render scale: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Render scale") != NULL);
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    fprintf(stderr, "B: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Display") != NULL);
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "B, A: [%s], picks %d, frames bare %d\n", open_menu(), s_picks, s_bare);
    CHECK(strstr(open_menu(), "Display") != NULL);
    CHECK(s_picks == 2 && s_bare == 0);
    /* In a box, B is its Cancel: 3x asked for, B, and Display is back with
     * nothing picked. */
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    CHECK(strstr(open_menu(), "Are you sure?") != NULL);
    CHECK(centred("Are you sure?"));
    frame(ImGuiKey_GamepadFaceRight); frames(3);
    fprintf(stderr, "B in the box: [%s] %s, picks %d\n", open_menu(), nav_state(), s_picks);
    CHECK(strstr(open_menu(), "Display") != NULL);
    CHECK(s_picks == 2);
    /* Back to File; About, in the middle, and B: File again. Exit never ran.
     * Dragged by its title bar, the box stays where it is: ImGui never takes
     * it to move. */
    frame(ImGuiKey_GamepadDpadLeft); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(strstr(open_menu(), "File") != NULL);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(strstr(open_menu(), "About") != NULL);
    CHECK(centred("About"));
    {   ImGuiWindow *w = ImGui::FindWindowByName("About");
        int moving = 0;
        CHECK(w != NULL);
        if (w) {
            float x = w->Pos.x + w->Size.x * 0.5f, y = w->Pos.y + w->TitleBarHeight * 0.5f;
            pointer(x, y); frames(1);
            io.AddMouseButtonEvent(0, true); frames(1);
            for (int i = 1; i <= 4; i++) {
                pointer(x + 40.0f * i, y + 25.0f * i); frames(1);
                moving |= ImGui::GetCurrentContext()->MovingWindow == w;
            }
            io.AddMouseButtonEvent(0, false); frames(2);
        }
        fprintf(stderr, "About dragged: [%s], moving %d\n", open_menu(), moving);
        CHECK(!moving && centred("About"));
        CHECK(strstr(open_menu(), "About") != NULL);
    }
    /* The window made smaller (out of full screen) with the box up: the box
     * follows, to the middle of the new size. */
    g_display = ImVec2(1280.0f, 720.0f);
    frames(2);
    CHECK(centred("About"));
    g_display = ImVec2(0.0f, 0.0f);
    frames(2);
    frame(ImGuiKey_GamepadFaceRight); frames(3);
    fprintf(stderr, "B in About: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "File") != NULL);
    CHECK(!s_exit);

    /* The mouse. Where the titles are: slid along the bar, the pointer opens
     * each menu it passes while one is open. */
    ImGuiWindow *bar = ImGui::FindWindowByName("##MainMenuBar");
    float bar_y = bar->Pos.y + bar->Size.y * 0.5f, file_x = -1, disp_x = -1, x;
    for (x = bar->Pos.x + 1; x < bar->Pos.x + bar->Size.x && disp_x < 0; x += 1) {
        pointer(x, bar_y); frame();
        if (strstr(open_menu(), "Display")) disp_x = x;
    }
    for (x = bar->Pos.x + 1; x < disp_x && file_x < 0; x += 1) {
        pointer(x, bar_y); frame();
        if (strstr(open_menu(), "File")) file_x = x;
    }
    fprintf(stderr, "File at x=%g, Display at x=%g\n", file_x, disp_x);
    CHECK(file_x >= 0 && disp_x > file_x);
    /* Display clicked open, then back along the bar to File: File opens in
     * its place (the whole menu shut, here, before). */
    pointer(disp_x, bar_y); frame(); click(); frames(2);
    fprintf(stderr, "clicked Display: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Display") != NULL);
    /* The pointer on its second row: that row's hint. */
    {
        ImGuiWindow *w = ImGui::FindWindowByName(open_menu());
        CHECK(w != NULL);
        if (w) {
            pointer(w->Pos.x + w->Size.x * 0.5f, s_wide_y + ImGui::GetTextLineHeight() * 0.5f);
            frames(2);
            CHECK(hint_is(k_wide_hint));
        }
    }
    pointer(file_x, bar_y); frames(4);
    fprintf(stderr, "back to File: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "File") != NULL);
    /* Esc in a menu the mouse opened, with no keyboard cursor in sight (as
     * the mouse may leave it): to its title, as with a pad, and the cursor
     * shows there. */
    ImGui::SetNavCursorVisible(false);
    frame(ImGuiKey_Escape); frames(2);
    fprintf(stderr, "Esc: [%s] %s, cursor %d\n", open_menu(), nav_state(),
            (int)ImGui::GetCurrentContext()->NavCursorVisible);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    CHECK(ImGui::GetCurrentContext()->NavCursorVisible);
    /* A click in the game, with File open: File shuts, and the pad is on
     * its title on the bar -- never with nothing to stand on. */
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(strstr(open_menu(), "File") != NULL);
    s_bare = 0;
    pointer(bar->Size.x * 0.5f, bar->Pos.y + bar->Size.y * 20); frames(2);
    click(); frames(3);
    fprintf(stderr, "clicked in the game: [%s] %s, frames bare %d\n", open_menu(), nav_state(), s_bare);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    CHECK(s_bare == 0);
    /* And in Render scale's list, two levels down: both shut. */
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    CHECK(strstr(open_menu(), "Render scale") != NULL);
    click(); frames(3);
    fprintf(stderr, "clicked in the game from a list: [%s] %s, frames bare %d\n", open_menu(), nav_state(), s_bare);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    CHECK(s_bare == 0 && s_picks == 2);
    /* A click on the bar past its titles, with File open: the same. */
    frame(ImGuiKey_GamepadDpadLeft); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(strstr(open_menu(), "File") != NULL);
    pointer(bar->Pos.x + bar->Size.x - 4, bar_y); frames(2);
    click(); frames(3);
    fprintf(stderr, "clicked the bar's end: [%s] %s, frames bare %d\n", open_menu(), nav_state(), s_bare);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    CHECK(s_bare == 0);
    /* With nothing open, a click in the game does nothing: no menu opens,
     * and the pad stays on the bar. */
    pointer(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f); frames(2);
    click(); frames(3);
    fprintf(stderr, "clicked in the game with nothing open: [%s] %s, frames bare %d\n", open_menu(), nav_state(), s_bare);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    CHECK(s_bare == 0);
    /* B on the bar does nothing. */
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    fprintf(stderr, "B: [%s] %s\n", open_menu(), nav_state());
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "pad Right, A: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Display") != NULL);

    /* Numbers. Right to Controller; A opens it on the
     * first row's -. A: one step down, set once. Right to +, A twice: two
     * steps up. */
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "controller: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Controller") != NULL);
    CHECK(hint_is("Sets how fast the camera turns."));
    frame(ImGuiKey_GamepadFaceDown); frames(1);
    fprintf(stderr, "sensitivity after -: %g, sets %d\n", s_sens, s_sets);
    CHECK(s_sens == 4 && s_sets == 1);
    frame(ImGuiKey_GamepadDpadRight); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frame(ImGuiKey_GamepadFaceDown); frames(1);
    fprintf(stderr, "after + +: %g, sets %d\n", s_sens, s_sets);
    CHECK(s_sens == 6 && s_sets == 3);
    /* + held down for a second: it goes on, and stops at 10. */
    {
        ImGuiIO &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true);
        frames(60);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false);
        frames(2);
    }
    fprintf(stderr, "+ held: %g, sets %d\n", s_sens, s_sets);
    CHECK(s_sens == 10 && s_sets == 7);
    CHECK(strstr(open_menu(), "Controller") != NULL);
    /* Down to the deadzone's +, from 7 -- between 5 and 10: up goes to 10,
     * then - twice to 5 and 0, and - at 0 sets nothing. */
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(1);
    fprintf(stderr, "deadzone after +: %g\n", s_dead);
    CHECK(s_dead == 10);
    CHECK(hint_is("Sets how far the stick moves first."));
    frame(ImGuiKey_GamepadDpadLeft); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frame(ImGuiKey_GamepadFaceDown); frames(1);
    s_sets = 0;
    frame(ImGuiKey_GamepadFaceDown); frames(1);
    fprintf(stderr, "deadzone after - - -: %g, sets at 0: %d\n", s_dead, s_sets);
    CHECK(s_dead == 0 && s_sets == 0);
    CHECK(strstr(open_menu(), "Controller") != NULL);

    /* Switches. B to Controller's title, Right to the bar's last menu,
     * Developer; A opens it on Log to file, and A flips that, once. */
    s_bare = 0;
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "developer: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Developer") != NULL);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "log to file: %d, runs %d\n", s_log, s_log_runs);
    CHECK(s_log == 0 && s_log_runs == 1);
    CHECK(strstr(open_menu(), "Developer") != NULL);
    /* Down to Logging, Right into it (the pad on the first), Down, A: the
     * second flips, and Logging stays open; Down, A: the third too. */
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    fprintf(stderr, "in logging: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Logging") != NULL);
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "switches %d, flips %d, menu [%s]\n", s_switches, s_flips, open_menu());
    CHECK(s_switches == 2 && s_flips == 1);
    CHECK(strstr(open_menu(), "Logging") != NULL);
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(s_switches == 6 && s_flips == 2);
    /* A on the third again: off, and the second stays on. */
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "switches %d, flips %d\n", s_switches, s_flips);
    CHECK(s_switches == 2 && s_flips == 3);
    /* B: back to Developer, nothing flipped on the way, and the pad never
     * left with nothing to stand on. */
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    fprintf(stderr, "B in logging: [%s] %s, frames bare %d\n", open_menu(), nav_state(), s_bare);
    CHECK(strstr(open_menu(), "Developer") != NULL);
    CHECK(s_switches == 2 && s_flips == 3 && s_log_runs == 1 && s_bare == 0);
    /* Down to the toggle that asks, A: the box, with Cancel the default, and
     * nothing flipped; A cancels, and Developer comes back. */
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    fprintf(stderr, "toggle that asks: [%s], runs %d\n", open_menu(), s_asked_runs);
    CHECK(strstr(open_menu(), "Are you sure?") != NULL && s_asked_runs == 0);
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    CHECK(strstr(open_menu(), "Developer") != NULL && s_asked_runs == 0 && !s_asked);
    /* Again (the pad back on the first row), Left to OK, A: flipped, once,
     * and Developer is back. */
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    CHECK(strstr(open_menu(), "Are you sure?") != NULL);
    frame(ImGuiKey_GamepadDpadLeft); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    fprintf(stderr, "after OK: [%s], on %d, runs %d\n", open_menu(), s_asked, s_asked_runs);
    CHECK(strstr(open_menu(), "Developer") != NULL && s_asked && s_asked_runs == 1 && s_log_runs == 1);

    /* At 640 x 480, the smallest window, the strip is two lines tall for the
     * longest hint (Render scale's), and all of it fits, with the pad's
     * buttons or the keys, which are wider; at 1920 x 1080, one line. With
     * the keys, no A. */
    CHECK(strip_fits(0));
    g_display = ImVec2(640.0f, 480.0f);
    frames(2);
    CHECK(strip_fits(2) && strip_has_a());
    g_pad = 0;
    frames(2);
    CHECK(strip_fits(2) && !strip_has_a());
    g_display = ImVec2(0.0f, 0.0f);
    frames(2);
    CHECK(strip_fits(0) && !strip_has_a());
    g_pad = 1;
    frames(2);

    /* Shut on Developer, with it open and the pad on Log to file: it fades
     * out in MENU_FADE_SECONDS, still drawn, fainter each frame, the dim
     * behind it too, and taking no input -- A, held for the game, flips
     * nothing. Faded out, it is built no more. */
    {
        int n = 0, dim = dim_alpha(), drawn = 1, fainter = 1;
        float was = g_fade;
        io.ClearInputKeys();            /* as the window does, shutting it */
        io.ClearInputMouse();
        while (g_fade > 0.0f && n < 60) {
            fade_frame(0, ImGuiKey_GamepadFaceDown);
            n++;
            if (g_fade > 0.0f) {
                ImGuiWindow *w = ImGui::FindWindowByName(open_menu());
                drawn &= strstr(open_menu(), "Developer") != NULL && w && w->Active;
                fainter &= g_fade < was && dim_alpha() < dim;
                dim = dim_alpha();
            }
            was = g_fade;
        }
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false);
        fprintf(stderr, "faded out in %d frames, drawn %d, fainter %d; log to file %d, runs %d\n",
                n, drawn, fainter, s_log, s_log_runs);
        CHECK(g_fade == 0.0f && !s_built);
        CHECK(fabsf(n / 60.0f - MENU_FADE_SECONDS) <= 1.0f / 60.0f + 1e-4f);
        CHECK(drawn && fainter);
        CHECK(s_log == 0 && s_log_runs == 1);
        CHECK(!ImGui::FindWindowByName("##MainMenuBar")->Active);
    }
    /* Opened again: it fades in from nothing, all there in
     * MENU_FADE_SECONDS; the bar alone, on File's title, and Right goes on
     * from File -- not from where the pad last was. */
    {
        int n = 0, built = 1;
        float was = -1.0f, first;
        g_just = 1;
        fade_frame(1);
        first = g_fade;
        while (g_fade < 1.0f && n < 60) {
            was = g_fade;
            fade_frame(1);
            built &= s_built && g_fade > was;
            n++;
        }
        fprintf(stderr, "faded in from %.2f in %d frames\n", first, n);
        CHECK(first > 0.0f && first < 0.01f && g_fade == 1.0f && built);
        CHECK(fabsf(n / 60.0f - MENU_FADE_SECONDS) <= 1.0f / 60.0f + 1e-4f);
    }
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "opened again, Right, A: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Display") != NULL);

    /* The busy box, centred. */
    busy_frame();
    busy_frame();
    CHECK(centred("##busy"));

    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    fprintf(stderr, g_fail ? "menu test FAILED\n" : "menu test passed\n");
    return g_fail;
}
