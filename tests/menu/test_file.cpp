/* Run with: python3 tests/menu/run.py
 *
 * File's rows (src/menu/menu_sdl.cpp), with no window, GPU or SDL: run.py
 * takes the File section from that file as it is (cut by text), into
 * menu_file_cut.inc, and SDL is stood in for below, as are the window's
 * File > Restart and File > Presets > Reset (nv2a_gl.c), counted.
 *
 * Checks the rows in order: About, a line, Presets, a submenu, then
 * Restart and Exit after a line; in Presets, a row for each preset: Xbox
 * (Restart) and Modern (Restart), a line, Performance, Balanced and
 * Quality, a line, and Reset (Restart) (spriteice, 23 November 2026). Where
 * the game can't start itself again, no Restart, no Reset and no line above
 * it, but the line after Modern. Then, added to the menu beside a row each
 * preset sets to its own value: Performance, Balanced and Quality by their
 * paths, as input playback chooses them, each set that row to its value,
 * and is the one row ticked, with no restart; with the row at none of
 * theirs, none is ticked. Xbox (Restart) and Modern (Restart) ask first, as
 * Reset does, so a path runs none of them; each one's OK sets the row,
 * ticks it, and restarts the game once. Presets alone runs nothing.
 */
#include "menu.h"
#include "imgui.h"
#include "backends/imgui_impl_null.h"
#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail = 1; } } while (0)

/* SDL: what File > About asks, and File > Exit's event, counted. */
typedef unsigned char Uint8;
typedef struct { Uint8 major, minor, patch; } SDL_version;
#define SDL_VERSION(v) ((v)->major = 2, (v)->minor = 30, (v)->patch = 12)
static void SDL_GetVersion(SDL_version *v) { SDL_VERSION(v); }
static const char *SDL_GetPlatform(void) { return "Linux"; }
static const char *SDL_GetCurrentVideoDriver(void) { return "x11"; }
enum { SDL_QUIT = 0x100 };
typedef union SDL_Event { unsigned type; Uint8 padding[56]; } SDL_Event;
#define SDL_zero(x) memset(&(x), 0, sizeof(x))
static int s_quits;
static int SDL_PushEvent(SDL_Event *e) { s_quits += e->type == SDL_QUIT; return 1; }
static const char *g_about_gpu, *g_about_gl;

#include "menu_file_cut.inc"

/* The port's and the window's: the version, and the restarts, counted. */
extern "C" const char *recomp_version(void) { return "test"; }
extern "C" void recomp_restart_self(void) { }
static int s_restarts, s_resets;
extern "C" void nv_window_restart(void) { s_restarts++; }
extern "C" void nv_window_reset(void) { s_resets++; }

/* A row each preset sets to its own value, 0 to 4, out of six: the sixth
 * is none of theirs. */
static int s_level;
static const char *const k_levels[] = { "Xbox's", "Modern's", "Least", "Between", "Most", "Other" };
#define LEVEL_NONE 5
static int  level_state(void) { return s_level; }
static void level_pick(int i) { s_level = i; }

/* Preset p's row in File's rows: Xbox's and Modern's, then the line, then
 * the others'. */
static int preset_row(int p) { return p <= XBOX_PRESET_MODERN ? 3 + p : 4 + p; }

static int row_is(const XBOX_MENU_ITEM &it, int kind, const char *label, const char *group)
{
    return it.kind == kind && (label ? it.label && !strcmp(it.label, label) : !it.label)
        && (group ? it.group && !strcmp(it.group, group) : !it.group);
}

int main(void)
{
    static const char *const presets[XBOX_PRESETS] = {
        "Xbox (Restart)", "Modern (Restart)", "Performance", "Balanced", "Quality" };
    XBOX_MENU_ITEM rows[FILE_ROWS];

    /* Where the game can't start itself again: no Restart, no Reset, and
     * no line above Reset; the line after Modern stays. */
    int n = file_rows(rows, 0);
    CHECK(n == 11);
    if (n == 11) {
        CHECK(row_is(rows[2], XBOX_ITEM_GROUP, "Presets", NULL));
        CHECK(row_is(rows[4], XBOX_ITEM_TOGGLE, "Modern (Restart)", "Presets"));
        CHECK(row_is(rows[5], XBOX_ITEM_SEPARATOR, NULL, "Presets"));
        CHECK(row_is(rows[8], XBOX_ITEM_TOGGLE, "Quality", "Presets"));
        CHECK(row_is(rows[9], XBOX_ITEM_SEPARATOR, NULL, NULL));
        CHECK(row_is(rows[10], XBOX_ITEM_ACTION, "Exit", NULL));
    }

    n = file_rows(rows, 1);
    CHECK(n == 14 && n == FILE_ROWS);
    if (n != 14) return 1;
    CHECK(row_is(rows[0], XBOX_ITEM_INFO, "About", NULL));
    CHECK(row_is(rows[1], XBOX_ITEM_SEPARATOR, NULL, NULL));
    CHECK(row_is(rows[2], XBOX_ITEM_GROUP, "Presets", NULL));
    for (int p = 0; p < XBOX_PRESETS; p++)
        CHECK(row_is(rows[preset_row(p)], XBOX_ITEM_TOGGLE, presets[p], "Presets") && rows[preset_row(p)].hint);
    CHECK(row_is(rows[5], XBOX_ITEM_SEPARATOR, NULL, "Presets"));
    /* Xbox and Modern set Audio effects (Restart), so ask first; the others
     * don't. */
    CHECK(rows[3].confirm && !strcmp(rows[3].confirm, "Apply Xbox and restart, losing unsaved progress?"));
    CHECK(rows[4].confirm && !strcmp(rows[4].confirm, "Apply Modern and restart, losing unsaved progress?"));
    for (int p = XBOX_PRESET_PERFORMANCE; p < XBOX_PRESETS; p++) CHECK(!rows[preset_row(p)].confirm);
    CHECK(row_is(rows[9], XBOX_ITEM_SEPARATOR, NULL, "Presets"));
    CHECK(row_is(rows[10], XBOX_ITEM_ACTION, "Reset (Restart)", "Presets") && rows[10].confirm);
    CHECK(row_is(rows[11], XBOX_ITEM_SEPARATOR, NULL, NULL));
    CHECK(row_is(rows[12], XBOX_ITEM_ACTION, "Restart", NULL));
    CHECK(row_is(rows[13], XBOX_ITEM_ACTION, "Exit", NULL));
    CHECK(rows[10].run == nv_window_reset && rows[12].run == nv_window_restart);

    XBOX_MENU_ITEM level = { XBOX_MENU_DISPLAY, XBOX_ITEM_CHOICE, "Level", level_state, NULL, level_pick,
                             k_levels, 6, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0, NULL,
                             "Sets the level.", XBOX_PRESETS_SET, { 0, 1, 2, 3, 4 }, NULL, NULL, NULL, NULL,
                             XBOX_BIND_NONE, NULL, NULL, NULL, 0 };
    xbox_MenuAdd(&level, 1);
    xbox_MenuAdd(rows, n);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = NULL;
    ImGui_ImplNull_Init();

    /* Performance, Balanced and Quality by their paths: the row at its
     * value, and its row alone ticked, with no restart; chosen again,
     * ticked already, nothing changes. */
    for (int p = XBOX_PRESETS - 1; p >= XBOX_PRESET_PERFORMANCE; p--) {
        char path[64];
        snprintf(path, sizeof path, "File > Presets > %s", presets[p]);
        CHECK(menu_path_run(path) == MENU_PATH_OK && s_level == p && menu_preset_now() == p);
        for (int q = 0; q < XBOX_PRESETS; q++) CHECK(rows[preset_row(q)].state() == (q == p));
        CHECK(menu_path_run(path) == MENU_PATH_OK && s_level == p && rows[preset_row(p)].state());
    }
    CHECK(s_restarts == 0);
    /* Modern (Restart) and Xbox (Restart) ask first, so their paths change
     * nothing; each one's OK (the row's run) sets the row, ticks it alone,
     * and restarts once. */
    CHECK(menu_path_run("File > Presets > Modern (Restart)") == MENU_PATH_ASKS && s_level == 2);
    CHECK(menu_path_run("File > Presets > Xbox (Restart)") == MENU_PATH_ASKS && s_level == 2);
    rows[4].run();
    CHECK(s_level == 1 && menu_preset_now() == XBOX_PRESET_MODERN && s_restarts == 1);
    for (int q = 0; q < XBOX_PRESETS; q++) CHECK(rows[preset_row(q)].state() == (q == XBOX_PRESET_MODERN));
    rows[3].run();
    CHECK(s_level == 0 && menu_preset_now() == XBOX_PRESET_XBOX && s_restarts == 2);
    for (int q = 0; q < XBOX_PRESETS; q++) CHECK(rows[preset_row(q)].state() == (q == XBOX_PRESET_XBOX));
    /* None of theirs: none ticked. */
    s_level = LEVEL_NONE;
    for (int q = 0; q < XBOX_PRESETS; q++) CHECK(!rows[preset_row(q)].state());
    /* Reset asks first; Presets alone runs nothing. */
    CHECK(menu_path_run("File > Presets > Reset (Restart)") == MENU_PATH_ASKS);
    CHECK(menu_path_check("File > Presets") == MENU_PATH_NOT_RUN);
    CHECK(s_resets == 0 && s_restarts == 2 && s_quits == 0 && s_level == LEVEL_NONE);

    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    if (!g_fail) fprintf(stderr, "file: ok\n");
    return g_fail;
}
