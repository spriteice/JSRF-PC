/* Run with: python3 tests/menu/run.py
 *
 * The in-game menu, driven with a pad and no window: Dear ImGui's null
 * backends, the menu's own drawing (src/menu/menu.cpp), and rows standing in
 * for the window's settings. Checks the order of the menus (Gameplay after
 * Input), that it opens to the bar alone with a pad on File's title, and
 * that each kind of row reaches its owner's function -- through the
 * confirmation box when it has one. Then
 * rows that arrive once the menu is up: from one of its own rows, and from
 * another thread while a box is open over the rows it was asked from -- all
 * above a row marked last, which arrived before any of them. Rows placed
 * under another (under): two added before the row they name, drawn right
 * below it in the order they were added; one under the row marked last,
 * drawn below it, at the very end; and one naming a row that isn't there,
 * drawn below the late rows and above the last. Then
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
 * the longest hint a row may have. Then submenus of rows (group), as
 * Gameplay's Skips and Display's Enhancements, in a menu of their own: at
 * its foot, each with the rows that name it, added before it or after; one
 * with nothing in it, and a second of the same name, not shown; a row naming
 * one that isn't there, drawn in the menu; and a row chosen in one, which
 * stays open, B going back a level at a time. File > Presets: the preset
 * every row is at ticked, or none; each preset setting each row through
 * its own function, within what the row has, a row in a submenu as any
 * other, a row of ticks a tick at a time, only those that differ, and a
 * (Restart) row kept, not run, with a restart asked for; and rows that say
 * the wrong thing about presets left alone, and said in the log (run.py).
 * Long lists, at 1920 x 1080 and 640 x 480: every menu and list of
 * choices between the bar and the strip, scrolling where it is taller,
 * and a list opened at the room's foot going up to fit. Rows that take
 * the next press (XBOX_ITEM_BIND), as the layouts' pages: the box, which
 * hands the press the window found to the row, once, and shuts back on
 * the row; the pad held off the menu from the box until it is let go, so
 * the press that binds, or hands it none, does nothing more -- B there
 * is a button for the row, not Back; a press the row can't have, and the
 * box waiting on; Esc shutting only a box that waits for a pad, handing
 * nothing; the pad's Back none for a pad's row; the strip with the one
 * way to leave a control none; a row with no bind doing nothing; a row
 * that takes a second press (bind_second): its first, then the box
 * waiting on for the second, refusing as the first does, and F1 or the
 * pad's Back none at either, with no cancel; and File > Presets
 * leaving the rows alone (run.py: and saying nothing of them).
 * Last, the busy box (menu_build_busy) with no menu, in the middle of the
 * window.
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

/* Every field, in order, as menu_sdl.cpp's File rows: C++11 has no
 * designated initializers. File's and Developer's rows say nothing about
 * File > Presets, which never sets them. The rows standing in for the other
 * menus' say nothing either, but for the presets' own, at the end: the log
 * says so once, for the first (run.py), and no preset sets them. */
#define NO_PRESETS XBOX_PRESETS_UNSAID, { 0, 0, 0 }, NULL
#define NO_BIND    XBOX_BIND_NONE, NULL

static int s_full, s_wide, s_scale = 1, s_exit, s_full_runs, s_wide_runs, s_picks = -1;
/* Render scale's choices' hints (menu.h, choice_hints): 3x's own, and the
 * row's for the others. */
static const char *const k_scale_choice_hints[] = { NULL, NULL, "Draws at three times the Xbox's.", NULL };
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
    "Came late.", NO_PRESETS, NULL, NULL, NULL, NO_BIND };

/* A row marked last, as Display's submenus: added before the others, and
 * drawn below them all. */
static int  last_state(void) { s_last_at = ++s_drawn_seq; return 0; }
static void last_run(void)   { }
static const XBOX_MENU_ITEM k_last = {
    XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Last row", last_state, last_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 1, NULL,
    "Starts at the title screen.", NO_PRESETS, NULL, NULL, NULL, NO_BIND };

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
      0, 0, 0, NULL, 0, "Render scale", "The first under it.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Under two", under1_state, under_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, "Render scale", "The second under it.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Unplaced", unplaced_state, under_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, "No such row", "Has nowhere to go.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Under the last", under_last_state, under_run, NULL, NULL, 0, NULL, NULL,
      NULL, NULL, 0, 0, 0, NULL, 0, "Last row", "Goes under the last.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
};

/* Submenus (group), as Gameplay's Skips and Display's Enhancements, both in
 * Audio here, which nothing else uses: their rows, some added before them
 * (as at the game's start) and one after; a row in Audio itself; and the
 * submenus, at its foot, with one that nothing goes in and a second of the
 * same name. */
static int s_skip_runs, s_music_runs, s_see_runs;
static int  in_state(void)    { return 0; }
static void skip_run(void)    { s_skip_runs++; }
static void music_run(void)   { s_music_runs++; }
static void see_run(void)     { s_see_runs++; }
static const XBOX_MENU_ITEM k_in_first[] = {
    { XBOX_MENU_AUDIO, XBOX_ITEM_TOGGLE, "Skip intro", in_state, skip_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Skips the logos.", NO_PRESETS, "Skips", NULL, NULL, NO_BIND },
    { XBOX_MENU_AUDIO, XBOX_ITEM_TOGGLE, "Music info", in_state, music_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Shows the song.", NO_PRESETS, "Enhancements", NULL, NULL, NO_BIND },
    { XBOX_MENU_AUDIO, XBOX_ITEM_TOGGLE, "Lost", in_state, under_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Has no submenu to go in.", NO_PRESETS, "Nowhere", NULL, NULL, NO_BIND },
    { XBOX_MENU_AUDIO, XBOX_ITEM_TOGGLE, "Effects", in_state, under_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Plays the effects.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
};
static const XBOX_MENU_ITEM k_groups[] = {
    { XBOX_MENU_AUDIO, XBOX_ITEM_GROUP, "Skips", NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 1, NULL, "Leaves out parts.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_AUDIO, XBOX_ITEM_GROUP, "Enhancements", NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 1, NULL, "Goes further.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_AUDIO, XBOX_ITEM_GROUP, "Enhancements", NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 1, NULL, "A second of its name.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_AUDIO, XBOX_ITEM_GROUP, "Empty", NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Has nothing in it.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
};
static const XBOX_MENU_ITEM k_in_after = {
    XBOX_MENU_AUDIO, XBOX_ITEM_TOGGLE, "See through", in_state, see_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
    0, 0, 0, NULL, 0, NULL, "Shows what is behind glass.", NO_PRESETS, "Enhancements", NULL, NULL, NO_BIND };
/* A choice row that tells its pick which row it is (menu.h, pick_with), as
 * Developer > Missions' levels do: in Skips, after Skip intro, with no plain
 * pick and nothing ticked. */
static int s_with_index = -1, s_with_calls;
static const void *s_with_data;
static const char k_with_data[] = "this row";
static int  with_state(void)                   { return -1; }
static void with_pick(const void *data, int i) { s_with_data = data; s_with_index = i; s_with_calls++; }
static const char *const k_with_choices[] = { "0110 (free play)", "0112 (race, Beat)" };
static XBOX_MENU_ITEM with_row(void)
{
    XBOX_MENU_ITEM it = {};
    it.menu = XBOX_MENU_AUDIO;
    it.kind = XBOX_ITEM_CHOICE;
    it.label = "Dogenzaka Hill (stg12)";
    it.state = with_state;
    it.choices = k_with_choices;
    it.nchoices = 2;
    it.hint = "Tells its pick which row it is.";
    it.group = "Skips";
    it.pick_with = with_pick;
    it.data = k_with_data;
    return it;
}

/* Long lists, as Developer > Missions' Character and its levels: in
 * Developer, added at the end. A choice row with more choices than fit
 * between the bar and the strip at any size; more rows than fit; and at
 * their foot a short list, whose choices open beside the room's foot. */
#define LONG_CHOICES  100
#define LONG_ROWS     80
#define SHORT_CHOICES 8
static char s_long_names[LONG_CHOICES][16], s_row_names[LONG_ROWS][16];
static const char *s_long_choices[LONG_CHOICES];
static int  long_state(void) { return -1; }
static void long_pick(int i) { (void)i; }
static void long_run(void)   { }
static XBOX_MENU_ITEM long_row(XBOX_ITEM_KIND kind, const char *label, int nchoices, const char *hint)
{
    XBOX_MENU_ITEM it = {};
    it.menu = XBOX_MENU_DEVELOPER;
    it.kind = kind;
    it.label = label;
    it.state = kind == XBOX_ITEM_TOGGLE ? in_state : long_state;
    it.run = long_run;
    it.pick = long_pick;
    it.choices = s_long_choices;
    it.nchoices = nchoices;
    it.hint = hint;
    return it;
}

/* Rows that take the next press (XBOX_ITEM_BIND), as Input's layouts: a
 * page of the pad's, two rows, and one of the keyboard's, with a row that
 * has no bind, and one that takes a second key (bind_second), as
 * Keyboard layout's do, in Gameplay here, which has one row of its own.
 * Each row keeps what it was handed; the pad's refuse the Guide button's
 * number, as the layout will, and the second key a key with no name. The
 * presets leave them alone (XBOX_PRESETS_LEFT). */
#define BIND_GUIDE 5                    /* SDL_CONTROLLER_BUTTON_GUIDE */
#define BIND_B     1                    /* SDL_CONTROLLER_BUTTON_B */
#define BIND_ESC   41                   /* SDL_SCANCODE_ESCAPE */
#define BIND_SPACE 44                   /* SDL_SCANCODE_SPACE */
#define BIND_J     13                   /* SDL_SCANCODE_J */
#define BIND_NAMELESS 300               /* a key SDL has no name for */
static int s_bind_a = -1, s_bind_b = -2, s_bind_key = -1, s_binds;
static int s_two_first = -2, s_two_second = -2, s_twos;
static int bind_pad(int *to, int input)
{
    s_binds++;
    if (input == BIND_GUIDE) return 0;
    *to = input;
    return 1;
}
static int bind_a(int input)            { return bind_pad(&s_bind_a, input); }
static int bind_b(int input)            { return bind_pad(&s_bind_b, input); }
static int bind_key(int input)          { s_binds++; s_bind_key = input; return 1; }
static const char *bind_a_now(void)     { return s_bind_a < 0 ? "A" : "Changed"; }
static const char *bind_b_now(void)     { return "B"; }
static const char *bind_key_now(void)   { return "W"; }
static int bind_two(int input)          { s_twos++; s_two_first = input; return 1; }
static int bind_two_second(int input)
{
    s_twos++;
    if (input == BIND_NAMELESS) return 0;
    s_two_second = input;
    return 1;
}
static const char *bind_two_now(void)   { return "Space, J"; }
#define BIND_LEFT XBOX_PRESETS_LEFT, { 0, 0, 0 }, NULL
static const XBOX_MENU_ITEM k_binds[] = {
    { XBOX_MENU_GAMEPLAY, XBOX_ITEM_GROUP, "Controller layout", NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Sets which button presses each control.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_GAMEPLAY, XBOX_ITEM_BIND, "A", NULL, NULL, NULL, NULL, 0, bind_a_now, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Jumps.", BIND_LEFT, "Controller layout", NULL, NULL, XBOX_BIND_PAD, bind_a },
    { XBOX_MENU_GAMEPLAY, XBOX_ITEM_BIND, "B", NULL, NULL, NULL, NULL, 0, bind_b_now, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Sprays.", BIND_LEFT, "Controller layout", NULL, NULL, XBOX_BIND_PAD, bind_b },
    { XBOX_MENU_GAMEPLAY, XBOX_ITEM_GROUP, "Keyboard and mouse layout", NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL,
      NULL, 0, 0, 0, NULL, 0, NULL, "Sets which key presses each control.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_GAMEPLAY, XBOX_ITEM_BIND, "Left stick up", NULL, NULL, NULL, NULL, 0, bind_key_now, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Skates forward.", BIND_LEFT, "Keyboard and mouse layout", NULL, NULL,
      XBOX_BIND_KEYS, bind_key },
    { XBOX_MENU_GAMEPLAY, XBOX_ITEM_BIND, "Broken", NULL, NULL, NULL, NULL, 0, bind_key_now, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Has no bind.", BIND_LEFT, "Keyboard and mouse layout", NULL, NULL,
      XBOX_BIND_KEYS, NULL },
    { XBOX_MENU_GAMEPLAY, XBOX_ITEM_BIND, "Jump", NULL, NULL, NULL, NULL, 0, bind_two_now, NULL, NULL, NULL,
      0, 0, 0, NULL, 0, NULL, "Takes two keys.", BIND_LEFT, "Keyboard and mouse layout", NULL, NULL,
      XBOX_BIND_KEYS, bind_two, bind_two_second },
};

/* The pad's navigation is the menu's: on, or held off (menu_bind_waiting). */
static int pad_navigates(void)
{
    return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;
}

/* Numbers, as the Input menu's: a sensitivity in whole steps, and a
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

/* File > Presets' rows (menu.h): a toggle, a choice with fewer choices on
 * this device than the presets name, a number kept as a fraction, as
 * Input > Free camera > Deadzone, and two (Restart) rows, which keep their
 * values: a choice whose state is the scale this run started at, as Render
 * scale's, and a toggle whose state is what it kept, as Audio effects'. A
 * toggle in a submenu, as Display > Enhancements' See through fix. And a row
 * of two ticks, as Graphics > Upscaling > Cleanup, the first ticked in
 * JSRF-PC default and both in Max quality. */
static int    p_smaa = 1, p_effects = 1, p_glass = 1;
static int    p_ticks = 1, p_tick_picks;
static int    p_feel = 1, p_feel_runs;   /* a row Xbox mode alone sets */
static int    p_aniso = 2;              /* 4x: JSRF-PC default's 8x is past this device's most */
static int    p_scale = 2;              /* 3x */
static double p_dz = 0.15;
static int    p_smaa_runs, p_aniso_picks, p_dz_sets, p_scale_picks, p_scale_keeps, p_scale_kept = -1;
static int    p_effects_runs, p_effects_keeps, p_glass_runs, p_bad_runs;
static int    p_smaa_state(void)       { return p_smaa; }
static void   p_smaa_run(void)         { p_smaa = !p_smaa; p_smaa_runs++; }
static int    p_aniso_state(void)      { return p_aniso; }
static void   p_aniso_pick(int i)      { p_aniso = i; p_aniso_picks++; }
static double p_dz_value(void)         { return p_dz * 100; }
static void   p_dz_set(double v)       { p_dz = v / 100; p_dz_sets++; }
static int    p_scale_state(void)      { return p_scale; }
static void   p_scale_pick(int i)      { (void)i; p_scale_picks++; }
static void   p_scale_keep(int i)      { p_scale_kept = i; p_scale_keeps++; }
static int    p_effects_state(void)    { return p_effects; }
static void   p_effects_run(void)      { p_effects_runs++; }
static void   p_effects_keep(int on)   { p_effects = on; p_effects_keeps++; }
static int    p_glass_state(void)      { return p_glass; }
static void   p_glass_run(void)        { p_glass = !p_glass; p_glass_runs++; }
static int    p_ticks_state(void)      { return p_ticks; }
static void   p_ticks_pick(int i)      { p_ticks ^= 1 << i; p_tick_picks++; }
static int    p_feel_state(void)       { return p_feel; }
static void   p_feel_run(void)         { p_feel = !p_feel; p_feel_runs++; }
static int    p_bad_state(void)        { return 0; }
static void   p_bad_run(void)          { p_bad_runs++; }
static void   p_bad_pick(int i)        { (void)i; p_bad_runs++; }
static const char *const k_p_aniso[] = { "Off", "2x", "4x" };     /* 8x and 16x not on this device */
static const char *const k_p_scales[] = { "1x", "2x", "3x", "4x", "5x", "6x" };
static const char *const k_p_ticks[] = { "Deblock", "Dedither" };
/* A row of more ticks than File > Presets can set (MENU_PRESET_TICKS), each
 * named in main. */
static const char *k_p_many[MENU_PRESET_TICKS + 1];
#define P_ASK "Apply selection and restart, losing unsaved progress?"
static const XBOX_MENU_ITEM k_presets[] = {
    { XBOX_MENU_GRAPHICS, XBOX_ITEM_TOGGLE, "SMAA", p_smaa_state, p_smaa_run, NULL, NULL, 0, NULL, NULL,
      NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Smooths edges.", XBOX_PRESETS_SET, { 0, 1, 1 }, NULL, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_GRAPHICS, XBOX_ITEM_CHOICE, "Filtering", p_aniso_state, NULL, p_aniso_pick, k_p_aniso, 3, NULL, NULL,
      NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Sharpens textures.", XBOX_PRESETS_SET, { 0, 3, XBOX_PRESET_MOST }, NULL, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_INPUT, XBOX_ITEM_NUMBER, "Deadzone %", NULL, NULL, NULL, NULL, 0, NULL, NULL,
      p_dz_value, p_dz_set, 0, 100, 5, "%g%%", 0, NULL, "Sets the stick's slack.", XBOX_PRESETS_SET, { 0, 15, 15 }, NULL, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_DISPLAY, XBOX_ITEM_CHOICE, "Scale (Restart)", p_scale_state, NULL, p_scale_pick, k_p_scales, 6, NULL,
      P_ASK, NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Sets the scale.", XBOX_PRESETS_SET, { 0, 2, XBOX_PRESET_MOST },
      p_scale_keep, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_AUDIO, XBOX_ITEM_TOGGLE, "Effects (Restart)", p_effects_state, p_effects_run, NULL, NULL, 0, NULL,
      P_ASK, NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Adds the effects.", XBOX_PRESETS_SET, { 1, 1, 1 }, p_effects_keep, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_AUDIO, XBOX_ITEM_TOGGLE, "Glass", p_glass_state, p_glass_run, NULL, NULL, 0, NULL, NULL,
      NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Tints what is behind.", XBOX_PRESETS_SET, { 0, 1, 1 }, NULL,
      "Enhancements", NULL, NULL, NO_BIND },
    { XBOX_MENU_GRAPHICS, XBOX_ITEM_TOGGLES, "Cleanup", p_ticks_state, NULL, p_ticks_pick, k_p_ticks, 2, NULL,
      NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Cleans the copies.", XBOX_PRESETS_SET,
      { 0, 1, XBOX_PRESET_MOST }, NULL, NULL, NULL, NULL, NO_BIND },
    /* A toggle Xbox mode sets, off, and JSRF-PC default and Max quality
     * leave, as Input > Behaviour > Camera (menu.h, leave): their values,
     * which would differ from the row's, are never read. */
    { XBOX_MENU_INPUT, XBOX_ITEM_TOGGLE, "Feel", p_feel_state, p_feel_run, NULL, NULL, 0, NULL, NULL,
      NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Sets the feel.", XBOX_PRESETS_SET, { 0, 0, 0 }, NULL, NULL, NULL,
      NULL, NO_BIND, NULL, NULL, XBOX_PRESET_BIT(XBOX_PRESET_JSRF_PC) | XBOX_PRESET_BIT(XBOX_PRESET_MAX) },
    /* Three that say the wrong thing, which no preset can set, and are left
     * alone: one that asks first but keeps nothing, told once in the log,
     * then a button and a row of more ticks than an int has bits for, which
     * the same check has told of already (the rows standing in for the
     * other menus', which say nothing, are told too, run.py). */
    { XBOX_MENU_GRAPHICS, XBOX_ITEM_TOGGLE, "Asks, keeps nothing", p_bad_state, p_bad_run, NULL, NULL, 0, NULL,
      P_ASK, NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Asks first.", XBOX_PRESETS_SET, { 1, 1, 1 }, NULL, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_GRAPHICS, XBOX_ITEM_ACTION, "A button", NULL, p_bad_run, NULL, NULL, 0, NULL, NULL,
      NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Does a thing.", XBOX_PRESETS_SET, { 1, 1, 1 }, NULL, NULL, NULL, NULL, NO_BIND },
    { XBOX_MENU_GRAPHICS, XBOX_ITEM_TOGGLES, "Too many ticks", p_bad_state, NULL, p_bad_pick,
      k_p_many, MENU_PRESET_TICKS + 1, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Has too many.",
      XBOX_PRESETS_SET, { 1, 1, 1 }, NULL, NULL, NULL, NULL, NO_BIND },
    /* And one every preset leaves, which is told once and set by none. */
    { XBOX_MENU_GRAPHICS, XBOX_ITEM_TOGGLE, "Left by all", p_bad_state, p_bad_run, NULL, NULL, 0, NULL, NULL,
      NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Is left.", XBOX_PRESETS_SET, { 1, 1, 1 }, NULL, NULL, NULL,
      NULL, NO_BIND, NULL, NULL, XBOX_PRESET_BIT(XBOX_PRESETS) - 1 },
};

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

/* Every menu open, and every list of choices, in the room between the bar
 * and the strip, over neither; and whether the last one opened scrolls. */
static int menus_in_room(void)
{
    ImGuiWindow *bar = ImGui::FindWindowByName("##MainMenuBar"), *strip = ImGui::FindWindowByName("##hints");
    float top = bar->Pos.y + bar->Size.y, foot = strip->Pos.y;
    int in = 1, n = 0;
    for (const ImGuiPopupData &p : ImGui::GetCurrentContext()->OpenPopupStack) {
        const ImGuiWindow *w = p.Window;
        if (!w || !w->Active) continue;
        n++;
        fprintf(stderr, "  %s: %.0f to %.0f, the room %.0f to %.0f%s\n", w->Name, w->Pos.y, w->Pos.y + w->Size.y,
                top, foot, w->ScrollbarY ? ", scrolls" : "");
        in &= w->Pos.y >= top && w->Pos.y + w->Size.y <= foot;
    }
    return n && in;
}
static int last_scrolls(void)
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    return g.OpenPopupStack.Size && g.OpenPopupStack.back().Window && g.OpenPopupStack.back().Window->ScrollbarY;
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
    /* Developer first, then Gameplay, then Input, then Display, then File:
     * the order on screen is the enum's, not the order rows arrive in.
     * Before them all, Display's last row. */
    const XBOX_MENU_ITEM developer[] = {
        { XBOX_MENU_DEVELOPER, XBOX_ITEM_TOGGLE, "Log to file", log_state, log_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
          "Writes the log to a file.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
        { XBOX_MENU_DEVELOPER, XBOX_ITEM_TOGGLES, "Logging", switches_state, NULL, switch_pick, k_switches, 3, NULL, NULL,
          NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Adds lines to the log.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
        { XBOX_MENU_DEVELOPER, XBOX_ITEM_TOGGLE, "Asks (Restart)", asked_state, asked_run, NULL, NULL, 0, NULL,
          "The game restarts to apply it.", NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Asks first.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    };
    const XBOX_MENU_ITEM gameplay[] = {
        { XBOX_MENU_GAMEPLAY, XBOX_ITEM_TOGGLE, "Indicator", in_state, under_run, NULL, NULL, 0, NULL, NULL, NULL,
          NULL, 0, 0, 0, NULL, 0, NULL, "Shows how a challenge is going.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    };
    const XBOX_MENU_ITEM input[] = {
        { XBOX_MENU_INPUT, XBOX_ITEM_NUMBER, "Sensitivity", NULL, NULL, NULL, NULL, 0, NULL, NULL,
          sens_value, sens_set, 1, 10, 1, "%g", 0, NULL, "Sets how fast the camera turns.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
        { XBOX_MENU_INPUT, XBOX_ITEM_NUMBER, "Deadzone", NULL, NULL, NULL, NULL, 0, NULL, NULL,
          dead_value, dead_set, 0, 100, 5, "%g%%", 0, NULL, "Sets how far the stick moves first.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    };
    const XBOX_MENU_ITEM display[] = {
        { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Fullscreen", full_state, full_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
          "Fills the screen.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
        { XBOX_MENU_DISPLAY, XBOX_ITEM_TOGGLE, "Widescreen (16:9)", wide_state, wide_run, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
          k_wide_hint, NO_PRESETS, NULL, NULL, NULL, NO_BIND },
        { XBOX_MENU_DISPLAY, XBOX_ITEM_CHOICE, "Render scale", scale_state, NULL, scale_pick, k_scales, 4, NULL,
          "Apply selection and restart, losing unsaved progress?", NULL, NULL, 0, 0, 0, NULL, 0, NULL,
          k_scale_hint, NO_PRESETS, NULL, NULL, NULL, NO_BIND, NULL, k_scale_choice_hints },
    };
    const XBOX_MENU_ITEM file[] = {
        { XBOX_MENU_FILE, XBOX_ITEM_INFO, "About", NULL, NULL, NULL, NULL, 0, about_text, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
          "Shows the version.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
        { XBOX_MENU_FILE, XBOX_ITEM_SEPARATOR, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL, NULL, NO_PRESETS, NULL, NULL, NULL, NO_BIND },
        { XBOX_MENU_FILE, XBOX_ITEM_ACTION, "Exit", NULL, exit_run, NULL, NULL, 0, NULL,
          "Close the game, losing unsaved progress?", NULL, NULL, 0, 0, 0, NULL, 0, NULL, "Closes the game.", NO_PRESETS, NULL, NULL, NULL, NO_BIND },
    };
    xbox_MenuAdd(&k_last, 1);
    xbox_MenuAdd(k_under, 4);
    xbox_MenuAdd(developer, 3);
    xbox_MenuAdd(gameplay, 1);
    xbox_MenuAdd(input, 2);
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
    CHECK(hint_is(k_scale_hint));       /* 1x: a choice with none of its own, the row's */
    frame(ImGuiKey_GamepadDpadDown); frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Draws at three times the Xbox's."));     /* 3x: its own */
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

    /* Numbers. Right to Input; A opens it on the
     * first row's -. A: one step down, set once. Right to +, A twice: two
     * steps up. */
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "input: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Input") != NULL);
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
    CHECK(strstr(open_menu(), "Input") != NULL);
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
    CHECK(strstr(open_menu(), "Input") != NULL);

    /* B to Input's title, Right: Gameplay, after Input; A opens it on its
     * row. */
    s_bare = 0;
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "gameplay: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Gameplay") != NULL);
    CHECK(hint_is("Shows how a challenge is going."));

    /* Switches. B to Gameplay's title, Right to the bar's last menu,
     * Developer; A opens it on Log to file, and A flips that, once. */
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

    /* Submenus. Audio's rows arrive: some before their submenus, then the
     * submenus, then a row for one of them. B to Display's title, Right to
     * Audio's, A: Audio, the pad on Effects. Down: the row whose submenu
     * isn't there, then Skips and Enhancements, at the foot in the order
     * added; Down again wraps to Effects -- the empty one and the second
     * Enhancements aren't shown. */
    xbox_MenuAdd(k_in_first, 4);
    xbox_MenuAdd(k_groups, 4);
    xbox_MenuAdd(&k_in_after, 1);
    {
        XBOX_MENU_ITEM with = with_row();
        xbox_MenuAdd(&with, 1);
    }
    s_bare = 0;
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "audio: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Audio") != NULL);
    CHECK(hint_is("Plays the effects."));
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Has no submenu to go in."));
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Leaves out parts."));
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Goes further."));
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Plays the effects."));
    /* Up, to Enhancements; Right into it, the pad on Music info, added
     * before it; Down, See through, added after it; Down wraps to Music
     * info: those two alone. Down, A: See through runs, once, and
     * Enhancements stays open. */
    frame(ImGuiKey_GamepadDpadUp); frames(1);
    CHECK(hint_is("Goes further."));
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    fprintf(stderr, "in enhancements: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Enhancements") != NULL);
    CHECK(hint_is("Shows the song."));
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Shows what is behind glass."));
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Shows the song."));
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "see through runs %d, music runs %d, menu [%s]\n", s_see_runs, s_music_runs, open_menu());
    CHECK(s_see_runs == 1 && s_music_runs == 0);
    CHECK(strstr(open_menu(), "Enhancements") != NULL);
    /* B: back to Audio, on Enhancements. Up, Skips; Right into it, A: Skip
     * intro runs, once. B, B: Audio's title on the bar, the pad never left
     * with nothing to stand on. */
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    fprintf(stderr, "B in enhancements: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Audio") != NULL);
    CHECK(hint_is("Goes further."));
    frame(ImGuiKey_GamepadDpadUp); frames(1);
    CHECK(hint_is("Leaves out parts."));
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    CHECK(strstr(open_menu(), "Skips") != NULL);
    CHECK(hint_is("Skips the logos."));
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(s_skip_runs == 1 && s_see_runs == 1 && s_music_runs == 0);
    /* Down, the choice row that tells its pick which row it is; Right into
     * its choices, the pad on the first, nothing ticked; Down, A: its
     * pick_with gets its data and the second choice, once. B: back in
     * Skips. */
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Tells its pick which row it is."));
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    CHECK(strstr(open_menu(), "Dogenzaka Hill (stg12)") != NULL);
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "pick_with: %d calls, choice %d, its row's data %s\n", s_with_calls, s_with_index,
            s_with_data == k_with_data ? "yes" : "no");
    CHECK(s_with_calls == 1 && s_with_index == 1 && s_with_data == k_with_data);
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    CHECK(strstr(open_menu(), "Skips") != NULL);
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    CHECK(strstr(open_menu(), "Audio") != NULL);
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    fprintf(stderr, "B, B: [%s] %s, frames bare %d\n", open_menu(), nav_state(), s_bare);
    CHECK(!strcmp(open_menu(), "") && bar_focused());
    CHECK(s_bare == 0);

    /* File > Presets (menu.h). No row sets anything: no preset is ticked.
     * Then the presets' rows, at JSRF-PC default's values, which is ticked:
     * the choice's 8x is the most this device has, 4x; and Feel, on, which
     * JSRF-PC default leaves, not read for it. */
    CHECK(menu_preset_now() == -1);
    for (int i = 0; i < MENU_PRESET_TICKS + 1; i++) k_p_many[i] = "A tick";
    xbox_MenuAdd(k_presets, (int)(sizeof k_presets / sizeof k_presets[0]));
    frame();
    fprintf(stderr, "presets: %d now\n", menu_preset_now());
    CHECK(menu_preset_now() == XBOX_PRESET_JSRF_PC);
    CHECK(menu_preset_apply(-1) == 0 && menu_preset_apply(XBOX_PRESETS) == 0);
    CHECK(p_smaa_runs + p_aniso_picks + p_dz_sets + p_scale_keeps + p_effects_keeps + p_tick_picks == 0);
    /* Xbox mode: each row through its own function, once, the one in a
     * submenu too; the scale kept for the next start, not picked, and a
     * restart asked for. The effects are already on, so not kept again.
     * Until the restart, the scale is this run's, so nothing is ticked;
     * after it, Xbox mode is. */
    CHECK(menu_preset_apply(XBOX_PRESET_XBOX) == 1);
    fprintf(stderr, "presets: Xbox mode: smaa %d (%d runs), filtering %d (%d), deadzone %g (%d), "
            "scale kept %d (%d keeps, %d picks), effects %d (%d keeps, %d runs), glass %d (%d runs), "
            "ticks %d (%d picks)\n",
            p_smaa, p_smaa_runs, p_aniso, p_aniso_picks, p_dz, p_dz_sets, p_scale_kept, p_scale_keeps,
            p_scale_picks, p_effects, p_effects_keeps, p_effects_runs, p_glass, p_glass_runs,
            p_ticks, p_tick_picks);
    CHECK(p_smaa == 0 && p_smaa_runs == 1 && p_aniso == 0 && p_aniso_picks == 1);
    CHECK(p_dz == 0 && p_dz_sets == 1);
    CHECK(p_scale_kept == 0 && p_scale_keeps == 1 && p_scale_picks == 0);
    CHECK(p_effects == 1 && p_effects_keeps == 0 && p_effects_runs == 0);
    CHECK(p_glass == 0 && p_glass_runs == 1);
    CHECK(p_ticks == 0 && p_tick_picks == 1);
    CHECK(p_feel == 0 && p_feel_runs == 1);
    CHECK(menu_preset_now() == -1);
    p_scale = p_scale_kept;
    CHECK(menu_preset_now() == XBOX_PRESET_XBOX);
    /* Feel on by hand: no longer Xbox mode's; the presets that leave it
     * leave it on, and are ticked with it on (below). */
    p_feel_run();
    CHECK(p_feel == 1 && menu_preset_now() == -1);
    /* Max quality: the choice and the scale at their last, and every tick,
     * past what the presets name: both ticks, each picked. */
    CHECK(menu_preset_apply(XBOX_PRESET_MAX) == 1);
    p_scale = p_scale_kept;
    CHECK(p_smaa == 1 && p_aniso == 2 && fabs(p_dz - 0.15) < 1e-9 && p_scale == 5 && p_glass == 1);
    CHECK(p_ticks == 3 && p_tick_picks == 3);
    CHECK(p_feel == 1 && p_feel_runs == 2);
    CHECK(menu_preset_now() == XBOX_PRESET_MAX);
    /* Max quality again: every row is there already, so nothing is set and
     * no restart. */
    {
        int runs = p_smaa_runs, picks = p_aniso_picks, sets = p_dz_sets, keeps = p_scale_keeps;
        int ticks = p_tick_picks;
        CHECK(menu_preset_apply(XBOX_PRESET_MAX) == 0);
        CHECK(p_smaa_runs == runs && p_aniso_picks == picks && p_dz_sets == sets && p_scale_keeps == keeps);
        CHECK(p_tick_picks == ticks);
    }
    /* A row changed by hand: the settings are the player's own. Back to
     * JSRF-PC default: 15% kept as 0.15 reads back a hair over 15, which is
     * still its 15. */
    p_dz_set(20);
    CHECK(menu_preset_now() == -1);
    CHECK(menu_preset_apply(XBOX_PRESET_JSRF_PC) == 1);
    p_scale = p_scale_kept;
    CHECK(p_scale == 2 && menu_preset_now() == XBOX_PRESET_JSRF_PC);
    /* From both ticks to the first: only the second is picked. */
    CHECK(p_ticks == 1 && p_tick_picks == 4);
    /* The effects off by hand: the (Restart) toggle kept on, not run. */
    p_effects = 0;
    CHECK(menu_preset_apply(XBOX_PRESET_JSRF_PC) == 1);
    CHECK(p_effects == 1 && p_effects_keeps == 1 && p_effects_runs == 0);
    /* SMAA off by hand, and back to JSRF-PC default: only a row that applies
     * at once differs, so it is run and nothing restarts. */
    CHECK(menu_preset_now() == XBOX_PRESET_JSRF_PC);
    p_smaa_run();
    CHECK(p_smaa == 0 && menu_preset_now() == -1);
    {
        int runs = p_smaa_runs, keeps = p_scale_keeps + p_effects_keeps;
        CHECK(menu_preset_apply(XBOX_PRESET_JSRF_PC) == 0);
        CHECK(p_smaa == 1 && p_smaa_runs == runs + 1 && p_scale_keeps + p_effects_keeps == keeps);
        CHECK(menu_preset_now() == XBOX_PRESET_JSRF_PC);
    }
    /* The second tick by hand: the settings are the player's again, and
     * back to JSRF-PC default the tick is taken off, at once. */
    p_ticks_pick(1);
    CHECK(p_ticks == 3 && menu_preset_now() == -1);
    CHECK(menu_preset_apply(XBOX_PRESET_JSRF_PC) == 0);
    CHECK(p_ticks == 1 && p_tick_picks == 6 && menu_preset_now() == XBOX_PRESET_JSRF_PC);
    /* Feel was never set again: JSRF-PC default and Max quality left it. */
    CHECK(p_feel == 1 && p_feel_runs == 2);
    /* The three that said the wrong thing were never set (run.py finds
     * their lines in the log). */
    CHECK(p_bad_runs == 0);

    /* Long lists (above), at 1920 x 1080 and at 640 x 480: every menu and
     * list of choices stays between the bar and the strip, and scrolls
     * where it is taller. From Audio's title, Right three times (Input,
     * Gameplay): Developer's. A opens it, on Log to file: it is taller than
     * the room, so it scrolls, hanging from the bar as before. Down three
     * times, Right: the long list, beside its row, in the room, scrolling.
     * B; Up four times wraps to the foot, the short list; Right: its
     * choices, opened beside the room's foot, go up to stay in it. B, B:
     * Developer's title again. */
    for (int i = 0; i < LONG_CHOICES; i++) {
        snprintf(s_long_names[i], sizeof s_long_names[i], "Choice %d", i + 1);
        s_long_choices[i] = s_long_names[i];
    }
    {
        XBOX_MENU_ITEM it = long_row(XBOX_ITEM_CHOICE, "Long list", LONG_CHOICES, "Has more choices than fit.");
        xbox_MenuAdd(&it, 1);
        for (int i = 0; i < LONG_ROWS; i++) {
            snprintf(s_row_names[i], sizeof s_row_names[i], "Row %d", i + 1);
            it = long_row(XBOX_ITEM_TOGGLE, s_row_names[i], 0, "One of many rows.");
            xbox_MenuAdd(&it, 1);
        }
        it = long_row(XBOX_ITEM_CHOICE, "Short list", SHORT_CHOICES, "Opens at the foot.");
        xbox_MenuAdd(&it, 1);
    }
    for (int i = 0; i < 3; i++) { frame(ImGuiKey_GamepadDpadRight); frames(2); }
    for (int small = 0; small < 2; small++) {
        g_display = small ? ImVec2(640.0f, 480.0f) : ImVec2(0.0f, 0.0f);
        frames(2);
        frame(ImGuiKey_GamepadFaceDown); frames(2);
        fprintf(stderr, "long lists at %.0fx%.0f: [%s]\n", io.DisplaySize.x, io.DisplaySize.y, open_menu());
        CHECK(strstr(open_menu(), "Developer") != NULL && hint_is("Writes the log to a file."));
        CHECK(menus_in_room() && last_scrolls());
        {
            ImGuiWindow *bar = ImGui::FindWindowByName("##MainMenuBar");
            ImGuiWindow *dev = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
            CHECK(dev && dev->Pos.y == bar->Pos.y + bar->Size.y);
        }
        for (int i = 0; i < 3; i++) { frame(ImGuiKey_GamepadDpadDown); frames(1); }
        CHECK(hint_is("Has more choices than fit."));
        frame(ImGuiKey_GamepadDpadRight); frames(2);
        CHECK(strstr(open_menu(), "Long list") != NULL);
        CHECK(menus_in_room() && last_scrolls());
        frame(ImGuiKey_GamepadFaceRight); frames(2);
        CHECK(strstr(open_menu(), "Developer") != NULL);
        for (int i = 0; i < 4; i++) { frame(ImGuiKey_GamepadDpadUp); frames(1); }
        CHECK(hint_is("Opens at the foot."));
        frame(ImGuiKey_GamepadDpadRight); frames(2);
        CHECK(strstr(open_menu(), "Short list") != NULL);
        CHECK(menus_in_room() && !last_scrolls());
        frame(ImGuiKey_GamepadFaceRight); frames(2);
        frame(ImGuiKey_GamepadFaceRight); frames(2);
        CHECK(!strcmp(open_menu(), "") && bar_focused());
    }
    g_display = ImVec2(0.0f, 0.0f);
    frames(2);

    /* Rows by their labels (menu.h, menu_path_run), as input playback's
     * steps choose them: each kind, a submenu, a row added since the last
     * frame, and every way a path can be refused, with nothing done. */
    {
        int full = s_full, runs = s_full_runs, flips = s_flips, sw = s_switches, sets = s_sets;
        CHECK(menu_path_check("Display > Fullscreen") == MENU_PATH_OK && s_full_runs == runs);
        CHECK(menu_path_run("Display > Fullscreen") == MENU_PATH_OK && s_full == !full && s_full_runs == runs + 1);
        CHECK(menu_path_run("Display > Fullscreen > Off") == MENU_PATH_OK && s_full == 0);
        CHECK(menu_path_run("Display > Fullscreen > Off") == MENU_PATH_OK && s_full == 0);
        CHECK(menu_path_run("Display > Fullscreen > On") == MENU_PATH_OK && s_full == 1);
        CHECK(menu_path_run("Display > Fullscreen > Maybe") == MENU_PATH_NO_CHOICE && s_full == 1);
        CHECK(menu_path_run("Display > Fullscreen > On > On") == MENU_PATH_TOO_LONG);
        CHECK(menu_path_run("Display > Render scale > 2x  (1280 x 960)") == MENU_PATH_ASKS);
        CHECK(menu_path_run("File > Exit") == MENU_PATH_ASKS && !s_exit);
        CHECK(menu_path_run("File > About") == MENU_PATH_NOT_RUN);
        CHECK(menu_path_run("Input > Sensitivity > 3") == MENU_PATH_OK && s_sens == 3 && s_sets == sets + 1);
        CHECK(menu_path_run("Input > Sensitivity > 11") == MENU_PATH_BAD_NUMBER && s_sens == 3);
        CHECK(menu_path_run("Input > Sensitivity > fast") == MENU_PATH_BAD_NUMBER);
        CHECK(menu_path_run("Input > Sensitivity") == MENU_PATH_NOT_RUN);
        CHECK(menu_path_run("Developer > Logging > Second (Restart)") == MENU_PATH_OK
              && s_switches == (sw ^ 2) && s_flips == flips + 1);
        CHECK(menu_path_run("Developer > Logging > Second (Restart) > On") == MENU_PATH_OK && (s_switches & 2));
        CHECK(menu_path_run("Developer > Logging > Second (Restart) > On") == MENU_PATH_OK && (s_switches & 2));
        CHECK(menu_path_run("Developer > Logging > Second (Restart) > Off") == MENU_PATH_OK && !(s_switches & 2));
        CHECK(menu_path_run("Developer > Logging > Fourth") == MENU_PATH_NO_CHOICE);
        CHECK(menu_path_run("Developer > Logging") == MENU_PATH_NOT_RUN);
        s_with_calls = 0;
        CHECK(menu_path_run("Audio > Skips > Dogenzaka Hill (stg12) > 0112 (race, Beat)") == MENU_PATH_OK
              && s_with_calls == 1 && s_with_index == 1 && s_with_data == k_with_data);
        CHECK(menu_path_run("Audio > Skips") == MENU_PATH_NOT_RUN);
        CHECK(menu_path_run("Audio > Dogenzaka Hill (stg12) > 0110 (free play)") == MENU_PATH_NO_ROW);
        CHECK(menu_path_run("Audio > Empty > Effects") == MENU_PATH_NO_ROW);
        CHECK(menu_path_run("Audio > Effects > Skips") == MENU_PATH_NO_CHOICE);
        CHECK(menu_path_run("Audio > Nowhere") == MENU_PATH_NO_ROW);
        CHECK(menu_path_run("Audio") == MENU_PATH_NO_ROW);
        CHECK(menu_path_run("Sound > Effects") == MENU_PATH_NO_MENU);
        CHECK(menu_path_run("") == MENU_PATH_NO_MENU);
        CHECK(menu_path_run("A > B > C > D > E > F > G > H > I") == MENU_PATH_TOO_LONG);
        {
            int see = s_see_runs;
            CHECK(menu_path_run("Audio > Enhancements > See through") == MENU_PATH_OK && s_see_runs == see + 1);
        }
        {
            /* Added after the last frame: found all the same. */
            static const XBOX_MENU_ITEM added = {
                XBOX_MENU_AUDIO, XBOX_ITEM_ACTION, "Just added", NULL, skip_run, NULL, NULL, 0, NULL, NULL, NULL, NULL,
                0, 0, 0, NULL, 0, NULL, "Was added a moment ago.", NO_PRESETS, NULL, NULL, NULL, NO_BIND };
            int skips = s_skip_runs;
            xbox_MenuAdd(&added, 1);
            CHECK(menu_path_run("Audio > Just added") == MENU_PATH_OK && s_skip_runs == skips + 1);
            CHECK(menu_path_run("Audio > Just added > Now") == MENU_PATH_TOO_LONG && s_skip_runs == skips + 1);
        }
        CHECK(strcmp(menu_path_why(MENU_PATH_ASKS), "the row asks first") == 0);
        CHECK(strcmp(menu_path_why(99), "not a result") == 0);
        frames(2);
    }
    /* Rows that take the next press (above). From Developer's title, Left:
     * Gameplay; A, Down, Right: Controller layout, the pad on A, which
     * shows what it has. A: the box, titled with its page, waiting for a
     * pad; no hint, and in the strip the one way to leave it none; the
     * pad's navigation held off. */
    xbox_MenuAdd(k_binds, (int)(sizeof k_binds / sizeof k_binds[0]));
    frame(ImGuiKey_GamepadDpadLeft); frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(strstr(open_menu(), "Gameplay") != NULL);
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Sets which button presses each control."));
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    fprintf(stderr, "binds: [%s] %s\n", open_menu(), nav_state());
    CHECK(strstr(open_menu(), "Controller layout") != NULL && hint_is("Jumps."));
    CHECK(menu_bind_waiting() == 0 && pad_navigates());
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    fprintf(stderr, "bind A: [%s], waiting %d\n", open_menu(), menu_bind_waiting());
    CHECK(menu_bind_waiting() == XBOX_BIND_PAD && s_binds == 0);
    CHECK(strstr(open_menu(), "###bind") && centred("Controller layout###bind"));
    CHECK(hint_is(NULL) && strip_fits(0) && !strip_has_a());
    CHECK(!pad_navigates());
    /* B in the box is no Back: the window hands it to the row (below). */
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    CHECK(menu_bind_waiting() == XBOX_BIND_PAD);
    /* The press, as the window finds it, before the frame that reads the
     * pad: B, held. The row has it, once; the box shuts, back on A in its
     * page; and B, still held, neither goes back a level nor moves the
     * pad, until let go. */
    {
        ImGuiIO &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, true);
        menu_bind_press(BIND_B);
        CHECK(menu_bind_waiting() == 0);
        frames(6);
        fprintf(stderr, "bound A to B, B held: [%s] %s, binds %d, A has %d\n", open_menu(), nav_state(), s_binds,
                s_bind_a);
        CHECK(s_binds == 1 && s_bind_a == BIND_B);
        CHECK(!strstr(open_menu(), "###bind"));
        CHECK(strstr(open_menu(), "Controller layout") != NULL && hint_is("Jumps."));
        CHECK(!pad_navigates());
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, false);
        frames(2);
    }
    CHECK(pad_navigates() && strstr(open_menu(), "Controller layout") != NULL && hint_is("Jumps."));
    CHECK(strip_has_a());
    /* Down: B, a step away. A: its box; the Guide button's number, which
     * the row refuses: the box waits on. Esc: the box shuts, back on B,
     * and nothing more handed to it. */
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Sprays."));
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(menu_bind_waiting() == XBOX_BIND_PAD);
    menu_bind_press(BIND_GUIDE);
    frames(2);
    CHECK(menu_bind_waiting() == XBOX_BIND_PAD && s_binds == 2 && s_bind_b == -2);
    frame(ImGuiKey_Escape); frames(3);
    fprintf(stderr, "Esc in B's box: [%s], waiting %d, binds %d\n", open_menu(), menu_bind_waiting(), s_binds);
    CHECK(menu_bind_waiting() == 0 && s_binds == 2 && s_bind_b == -2);
    CHECK(strstr(open_menu(), "Controller layout") != NULL && hint_is("Sprays.") && pad_navigates());
    /* A again, and the pad's Back, as the window hands it over: none for
     * B, and the pad held off the menu until let go. */
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(menu_bind_waiting() == XBOX_BIND_PAD);
    {
        ImGuiIO &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_GamepadBack, true);
        menu_bind_none();
        frames(3);
        CHECK(menu_bind_waiting() == 0 && s_binds == 3 && s_bind_b == XBOX_BIND_NO_INPUT && !pad_navigates());
        io.AddKeyEvent(ImGuiKey_GamepadBack, false);
        frames(2);
    }
    CHECK(strstr(open_menu(), "Controller layout") != NULL && hint_is("Sprays.") && pad_navigates());
    /* With no box, a press or none is no one's. */
    menu_bind_press(BIND_B);
    menu_bind_none();
    frames(2);
    CHECK(s_binds == 3 && pad_navigates());
    /* A menu path stops at a bind row: no box, and nothing bound. */
    CHECK(menu_path_check("Gameplay > Controller layout > A") == MENU_PATH_NOT_RUN);
    CHECK(menu_path_run("Gameplay > Controller layout > A") == MENU_PATH_NOT_RUN);
    CHECK(menu_path_run("Gameplay > Controller layout > A > B") == MENU_PATH_NOT_RUN);
    frames(2);
    CHECK(menu_bind_waiting() == 0 && s_binds == 3 && pad_navigates());
    /* B, Down, Right: the keyboard's page, on Left stick up. A: its box,
     * waiting for a key; on the keyboard, the strip has F1 for none. Esc,
     * as the window hands it over: the row's, once. */
    frame(ImGuiKey_GamepadFaceRight); frames(2);
    CHECK(strstr(open_menu(), "Gameplay") != NULL);
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Sets which key presses each control."));
    frame(ImGuiKey_GamepadDpadRight); frames(2);
    CHECK(strstr(open_menu(), "Keyboard and mouse layout") != NULL && hint_is("Skates forward."));
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(menu_bind_waiting() == XBOX_BIND_KEYS && centred("Keyboard and mouse layout###bind"));
    g_pad = 0;
    frames(1);
    CHECK(strip_fits(0) && !strip_has_a());
    menu_bind_press(BIND_ESC);
    frames(3);
    g_pad = 1;
    CHECK(menu_bind_waiting() == 0 && s_binds == 4 && s_bind_key == BIND_ESC);
    CHECK(strstr(open_menu(), "Keyboard and mouse layout") != NULL && hint_is("Skates forward."));
    /* Down: the row with no bind. A: no box. */
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Has no bind."));
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(menu_bind_waiting() == 0 && !strstr(open_menu(), "###bind"));
    /* Down: the row that takes two keys. A: its box, at its first press.
     * Space: the row's first, and the box waits on for the second, with
     * the strip still fitting and the pad still held off. A key with no
     * name: refused there, waiting on. J: the second, and the box shuts,
     * back on the row. */
    frame(ImGuiKey_GamepadDpadDown); frames(1);
    CHECK(hint_is("Takes two keys."));
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(menu_bind_waiting() == XBOX_BIND_KEYS && !menu_bind_second() && s_twos == 0);
    menu_bind_press(BIND_SPACE);
    frames(2);
    fprintf(stderr, "two keys, first: [%s], waiting %d, second %d, first %d\n", open_menu(),
            menu_bind_waiting(), menu_bind_second(), s_two_first);
    CHECK(s_twos == 1 && s_two_first == BIND_SPACE && s_two_second == -2);
    CHECK(menu_bind_waiting() == XBOX_BIND_KEYS && menu_bind_second());
    CHECK(strstr(open_menu(), "###bind") && centred("Keyboard and mouse layout###bind"));
    CHECK(strip_fits(0) && !pad_navigates());
    menu_bind_press(BIND_NAMELESS);
    frames(2);
    CHECK(s_twos == 2 && s_two_second == -2 && menu_bind_second());
    menu_bind_press(BIND_J);
    frames(3);
    CHECK(s_twos == 3 && s_two_second == BIND_J && menu_bind_waiting() == 0 && !menu_bind_second());
    CHECK(!strstr(open_menu(), "###bind") && hint_is("Takes two keys."));
    /* A again, Space, then F1 or the pad's Back, as the window hands them
     * over: none for the second, and the box shuts. */
    frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    menu_bind_press(BIND_SPACE);
    menu_bind_none();
    frames(3);
    CHECK(s_twos == 5 && s_two_second == XBOX_BIND_NO_INPUT && menu_bind_waiting() == 0);
    CHECK(!strstr(open_menu(), "###bind") && hint_is("Takes two keys."));
    /* A again, and F1 or the pad's Back at the first press: none for the
     * first, and the box waits on for the second; again: none for that,
     * and the box shuts. There is no cancel. */
    frames(2);
    frame(ImGuiKey_GamepadFaceDown); frames(2);
    CHECK(menu_bind_waiting() == XBOX_BIND_KEYS);
    menu_bind_none();
    frames(2);
    CHECK(s_twos == 6 && s_two_first == XBOX_BIND_NO_INPUT && menu_bind_second());
    CHECK(strstr(open_menu(), "###bind") != NULL);
    menu_bind_none();
    frames(3);
    CHECK(s_twos == 7 && s_two_second == XBOX_BIND_NO_INPUT && menu_bind_waiting() == 0);
    CHECK(!strstr(open_menu(), "###bind") && hint_is("Takes two keys."));
    /* B, B, B: Gameplay's title, the pad never left with nothing to stand
     * on. File > Presets: still at JSRF-PC default, and a preset hands the
     * rows nothing. */
    s_bare = 0;
    for (int i = 0; i < 3; i++) { frame(ImGuiKey_GamepadFaceRight); frames(2); }
    fprintf(stderr, "binds done: [%s] %s, frames bare %d\n", open_menu(), nav_state(), s_bare);
    CHECK(!strcmp(open_menu(), "") && bar_focused() && s_bare == 0);
    CHECK(menu_preset_now() == XBOX_PRESET_JSRF_PC);
    CHECK(menu_preset_apply(XBOX_PRESET_JSRF_PC) == 0 && s_binds == 4 && s_twos == 7);

    /* The busy box, centred. */
    busy_frame();
    busy_frame();
    CHECK(centred("##busy"));

    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    fprintf(stderr, g_fail ? "menu test FAILED\n" : "menu test passed\n");
    return g_fail;
}
