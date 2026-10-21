/* Run with: python3 tests/menu/run.py
 *
 * The in-game menu, driven with a pad and no window: Dear ImGui's null
 * backends, the menu's own drawing (src/menu/menu.cpp), and rows standing in
 * for the window's settings. Checks the order of the menus, that a pad starts
 * in File, and that each kind of row reaches its owner's function -- through
 * the confirmation box when it has one. Then rows that arrive once the menu
 * is up: from one of its own rows, and from another thread while a box is
 * open over the rows it was asked from. Then that nothing but the window
 * (F1, Back) shuts it: B and Esc go back a level at a time, to the bar and
 * no further; the mouse leaving a menu, or a click in the game, leaves a
 * menu open -- and a pad still has somewhere to stand after each. Last,
 * numbers: - and + a step at a time, held down, at the ends, and from a
 * value between two steps.
 */
#include "menu.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_null.h"
#include <stdio.h>
#include <string.h>
#if !defined(_WIN32)
#  include <pthread.h>
#endif

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail = 1; } } while (0)

static int s_full, s_wide, s_scale = 1, s_exit, s_full_runs, s_wide_runs, s_picks = -1;
static int  full_state(void)   { return s_full; }
static void full_run(void)     { s_full = !s_full; s_full_runs++; }
static int  wide_state(void)   { return s_wide; }
static void wide_run(void);
static int  scale_state(void)  { return s_scale; }
static void scale_pick(int i)  { s_picks = i; }
static void exit_run(void)     { s_exit = 1; }
static const char *about_text(void) { return "JSRF Unleashed test"; }
static const char *const k_scales[] = { "1x  (640 x 480)", "2x  (1280 x 960)",
                                        "3x  (1920 x 1440)", "4x  (2560 x 1920)" };

/* Late rows: toggles that count how often they are drawn. */
static int s_late_drawn;
static int  late_state(void) { s_late_drawn++; return 0; }
static void late_run(void)   { }
static const XBOX_MENU_ITEM k_late = {
    XBOX_MENU_GRAPHICS, XBOX_ITEM_TOGGLE, "Late", late_state, late_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL };

/* Numbers, as the Controller menu's: a sensitivity in whole steps, and a
 * deadzone in 5% steps that starts between two (as the settings file may). */
static double s_sens = 5, s_dead = 7;
static int    s_sets;
static double sens_value(void)    { return s_sens; }
static void   sens_set(double v)  { s_sens = v; s_sets++; }
static double dead_value(void)    { return s_dead; }
static void   dead_set(double v)  { s_dead = v; s_sets++; }

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

/* One frame, with these gamepad keys held for it. */
static void frame(ImGuiKey press = ImGuiKey_None)
{
    ImGuiIO &io = ImGui::GetIO();
    if (press != ImGuiKey_None) io.AddKeyEvent(press, true);
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    menu_build(g_just);
    g_just = 0;
    count_bare();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    if (press != ImGuiKey_None) {
        io.AddKeyEvent(press, false);
        ImGui_ImplNull_NewFrame(); ImGui::NewFrame();
        menu_build(0);
        count_bare();
        ImGui::Render();
    }
}
static void frames(int n) { while (n--) frame(); }

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
    /* Controller first, then Graphics, then File: the order on screen is the
     * enum's, not the order rows arrive in. */
    const XBOX_MENU_ITEM controller[] = {
        { XBOX_MENU_CONTROLLER, XBOX_ITEM_NUMBER, "Sensitivity", NULL, NULL, NULL, NULL, 0, NULL, NULL,
          sens_value, sens_set, 1, 10, 1, "%g" },
        { XBOX_MENU_CONTROLLER, XBOX_ITEM_NUMBER, "Deadzone", NULL, NULL, NULL, NULL, 0, NULL, NULL,
          dead_value, dead_set, 0, 100, 5, "%g%%" },
    };
    const XBOX_MENU_ITEM graphics[] = {
        { XBOX_MENU_GRAPHICS, XBOX_ITEM_TOGGLE, "Full screen", full_state, full_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL },
        { XBOX_MENU_GRAPHICS, XBOX_ITEM_TOGGLE, "Widescreen (16:9)", wide_state, wide_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL },
        { XBOX_MENU_GRAPHICS, XBOX_ITEM_CHOICE, "Render scale", scale_state, NULL, scale_pick, k_scales, 4, NULL,
          "The game restarts at the new scale: anything since your last save is lost.", NULL, NULL, 0, 0, 0, NULL },
    };
    const XBOX_MENU_ITEM file[] = {
        { XBOX_MENU_FILE, XBOX_ITEM_INFO, "About", NULL, NULL, NULL, NULL, 0, about_text, NULL, NULL, NULL, 0, 0, 0, NULL },
        { XBOX_MENU_FILE, XBOX_ITEM_SEPARATOR, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL },
        { XBOX_MENU_FILE, XBOX_ITEM_ACTION, "Exit", NULL, exit_run, NULL, NULL, 0, NULL,
          "Anything since your last save is lost.", NULL, NULL, 0, 0, 0, NULL },
    };
    xbox_MenuAdd(controller, 2);
    xbox_MenuAdd(graphics, 3);
    xbox_MenuAdd(file, 3);

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplNull_Init();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    /* Opened: File is open, with a pad. */
    frames(3);
    fprintf(stderr, "open popup after opening: %s\n", open_menu());
    CHECK(strstr(open_menu(), "File") != NULL);
    {   /* menu bar order */
        ImGuiWindow *bar = ImGui::FindWindowByName("##MainMenuBar");
        CHECK(bar != NULL);
    }

    /* Right walks the bar to Graphics (File closes); A opens it, the cursor
     * on its first row. Down, A: Widescreen flipped, once, and Graphics is
     * still open with the cursor where it was. */
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    fprintf(stderr, "after right: [%s] %s\n", open_menu(), nav_state());
    CHECK(bar_focused());
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "after A: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Graphics") != NULL);
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "full runs %d, wide runs %d, menu [%s] %s\n", s_full_runs, s_wide_runs, open_menu(), nav_state());
    CHECK(s_wide_runs == 1 && s_full_runs == 0);
    CHECK(strstr(open_menu(), "Graphics") != NULL);
    fprintf(stderr, "the row Widescreen added: drawn %d times\n", s_late_drawn);
    CHECK(s_late_drawn > 0);

    /* Down to Render scale, Right into it (the cursor on 1x), down twice to
     * 3x, A: asked first, with Cancel the default... */
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    fprintf(stderr, "in render scale: [%s]\n", open_menu());
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    fprintf(stderr, "after choosing: [%s], picks %d\n", open_menu(), s_picks);
    CHECK(strstr(open_menu(), "Are you sure?") != NULL);
    CHECK(s_picks == -1);
    /* ... so A there cancels, and Graphics comes back. */
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    fprintf(stderr, "after Cancel: [%s], picks %d\n", open_menu(), s_picks);
    CHECK(s_picks == -1);
    CHECK(strstr(open_menu(), "Graphics") != NULL);
    /* Again (Graphics came back with the cursor on its first row), and this
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
    /* Graphics is back, with every row: Widescreen's and the other thread's. */
    s_late_drawn = 0;
    frames(1);
    fprintf(stderr, "late rows drawn in one frame: %d of %d\n", s_late_drawn, 1 + LATE_ROWS);
    CHECK(strstr(open_menu(), "Graphics") != NULL);
    CHECK(s_late_drawn == 1 + LATE_ROWS);

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
    /* Right to Graphics' title, and B there: the pad stays on Graphics. */
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "Right, B, A: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Graphics") != NULL);
    /* In Render scale's list, B goes back to Graphics; B again, to its title.
     * Nothing was picked, and the bar never stood empty. */
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    fprintf(stderr, "in render scale: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Render scale") != NULL);
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    fprintf(stderr, "B: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Graphics") != NULL);
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "B, A: [%s], picks %d, frames bare %d\n", open_menu(), s_picks, s_bare);
    CHECK(strstr(open_menu(), "Graphics") != NULL);
    CHECK(s_picks == 2 && s_bare == 0);
    /* In a box, B is its Cancel: 3x asked for, B, and Graphics is back with
     * nothing picked. */
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(3);
    CHECK(strstr(open_menu(), "Are you sure?") != NULL);
    frame(ImGuiKey_GamepadFaceRight); frames(3);
    fprintf(stderr, "B in the box: [%s] %s, picks %d\n", open_menu(), nav_state(), s_picks);
    CHECK(strstr(open_menu(), "Graphics") != NULL);
    CHECK(s_picks == 2);
    /* Back to File; About, and B: File again. Exit never ran. */
    frame(ImGuiKey_GamepadDpadLeft); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(strstr(open_menu(), "File") != NULL);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(strstr(open_menu(), "About") != NULL);
    frame(ImGuiKey_GamepadFaceRight); frames(3);
    fprintf(stderr, "B in About: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "File") != NULL);
    CHECK(!s_exit);

    /* The mouse. Where the titles are: slid along the bar, the pointer opens
     * each menu it passes while one is open. */
    ImGuiWindow *bar = ImGui::FindWindowByName("##MainMenuBar");
    float bar_y = bar->Pos.y + bar->Size.y * 0.5f, file_x = -1, gfx_x = -1, x;
    for (x = bar->Pos.x + 1; x < bar->Pos.x + bar->Size.x && gfx_x < 0; x += 1) {
        pointer(x, bar_y); frame();
        if (strstr(open_menu(), "Graphics")) gfx_x = x;
    }
    for (x = bar->Pos.x + 1; x < gfx_x && file_x < 0; x += 1) {
        pointer(x, bar_y); frame();
        if (strstr(open_menu(), "File")) file_x = x;
    }
    fprintf(stderr, "File at x=%g, Graphics at x=%g\n", file_x, gfx_x);
    CHECK(file_x >= 0 && gfx_x > file_x);
    /* Graphics clicked open, then back along the bar to File: File opens in
     * its place (the whole menu shut, here, before). */
    pointer(gfx_x, bar_y); frame(); click(); frames(2);
    fprintf(stderr, "clicked Graphics: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Graphics") != NULL);
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
    /* Out of the menu, and a click in the game: a menu is still open, in
     * every frame, and the pad goes on from there. */
    s_bare = 0;
    pointer(bar->Size.x * 0.5f, bar->Pos.y + bar->Size.y * 20); frames(2);
    click(); frames(3);
    fprintf(stderr, "clicked in the game: [%s] %s, frames bare %d\n", open_menu(), nav_state(), s_bare);
    CHECK(strstr(open_menu(), "File") != NULL);
    CHECK(s_bare == 0);
    /* A click on the bar past its titles: still File, with the pad in it. */
    pointer(bar->Pos.x + bar->Size.x - 4, bar_y); frames(2);
    click(); frames(3);
    fprintf(stderr, "clicked the bar's end: [%s] %s, frames bare %d\n", open_menu(), nav_state(), s_bare);
    CHECK(strstr(open_menu(), "File") != NULL);
    CHECK(s_bare == 0);
    /* B there too: back to File's title, with the pad. */
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    fprintf(stderr, "B: [%s] %s\n", open_menu(), nav_state());
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "pad Right, A: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Graphics") != NULL);

    /* Numbers. Right to the bar's last menu, Controller; A opens it on the
     * first row's -. A: one step down, set once. Right to +, A twice: two
     * steps up. */
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "controller: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Controller") != NULL);
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
    frame(ImGuiKey_GamepadDpadLeft); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frame(ImGuiKey_GamepadFaceDown); frames(1);
    s_sets = 0;
    frame(ImGuiKey_GamepadFaceDown); frames(1);
    fprintf(stderr, "deadzone after - - -: %g, sets at 0: %d\n", s_dead, s_sets);
    CHECK(s_dead == 0 && s_sets == 0);
    CHECK(strstr(open_menu(), "Controller") != NULL);

    /* Shut on Controller and opened again: File, and Right goes on from File
     * -- not from where the pad last was on the bar. */
    g_just = 1;
    frames(2);
    CHECK(strstr(open_menu(), "File") != NULL);
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "opened again, Right, A: [%s]\n", open_menu());
    CHECK(strstr(open_menu(), "Graphics") != NULL);

    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    fprintf(stderr, g_fail ? "menu test FAILED\n" : "menu test passed\n");
    return g_fail;
}
