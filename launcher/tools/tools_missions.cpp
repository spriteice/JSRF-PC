/*
 * JSRF-PC Tools' missions page (tools.h; b3 row 38).
 *
 * Developer > Missions restarts the game at a story mission. With
 * start-states.txt beside the settings file, a mission starts with the story
 * up to it done; without it, with a new game's progress (port/missions.c).
 * This page makes that file from the player's own game: the story walk
 * (mission_walk.c) over the game's mission files, Media\Mission, run on its
 * own thread so the window keeps drawing.
 *
 *   Make start states: the game checked (port/xbe_check.c), the walk, then
 *     the shell's warning with what it found, and the file written after a
 *     dated backup of the one there.
 *   Remove start states: the same warning and backup, and the file goes.
 *
 * The file is made from the game's files, so it is the player's own: it is
 * never in a package, and the page writes it only on their PC.
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "imgui.h"

#include "file_dialog.h"
#include "mission_walk.h"
#include "setup.h"
#include "setup_job.h"
#include "tools.h"
extern "C" {
#include "xbe_check.h"
}

#define BACKUPS_NAME "Start states backups"   /* beside the settings file */

struct page_state {
    char          game[JSRF_PATH_MAX];      /* the game read: the shell's, or one dropped or picked */
    SDL_Thread   *thread;
    SDL_atomic_t  done;                     /* the walk's thread has finished */
    jsrf_walk     walk;                     /* its result, once done */
    int           failed;                   /* the walk's result is a failure (walk.why) */
    int           have;                     /* walk holds a file to write */
    char          note[JSRF_PATH_MAX + 800];
    char          backups[JSRF_PATH_MAX];
    ImGuiTextBuffer warning;
};
static page_state g;

/* ---- the walk, on its own thread ------------------------------------------------------- */

static int walk_thread(void *arg)
{
    char why[340];
    int r;
    (void)arg;
    r = jsrf_xbe_check_folder(g.game, why, sizeof why);
    if (r != JSRF_XBE_OK) {
        memset(&g.walk, 0, sizeof g.walk);
        snprintf(g.walk.why, sizeof g.walk.why, "%.339s%s", why,
                 r == JSRF_XBE_OTHER ? " JSRF-PC runs only the standalone US disc's files." : "");
        g.failed = 1;
    } else {
        g.failed = jsrf_walk_game(g.game, &g.walk) != 0;
    }
    SDL_AtomicSet(&g.done, 1);
    return 0;
}

static int walking(void)
{
    return g.thread && !SDL_AtomicGet(&g.done);
}

static void forget(void)
{
    if (g.thread) {
        SDL_WaitThread(g.thread, NULL);
        g.thread = NULL;
    }
    jsrf_walk_free(&g.walk);
    g.have = g.failed = 0;
}

static void start_walk(const char *game)
{
    forget();
    snprintf(g.game, sizeof g.game, "%s", game);
    g.note[0] = 0;
    SDL_AtomicSet(&g.done, 0);
    g.thread = SDL_CreateThread(walk_thread, "story walk", NULL);
    if (!g.thread) snprintf(g.note, sizeof g.note, "The walk couldn't be started (%s).", SDL_GetError());
}

/* ---- the page ---------------------------------------------------------------------------- */

static void pick_game(void)
{
    char picked[JSRF_PATH_MAX] = "";
    int r = jsrf_pick_folder("Where is the game? Pick the folder with default.xbe and Media in it", picked,
                             sizeof picked);
    if (r == JSRF_NO_DIALOG)
        snprintf(g.note, sizeof g.note, "There's no folder dialog here (zenity or kdialog): drop the game's "
                                        "folder on this window.");
    else if (r == JSRF_PICKED) start_walk(picked);
}

static void walked(tools_ctx *t)
{
    SDL_WaitThread(g.thread, NULL);
    g.thread = NULL;
    if (g.failed) {
        snprintf(g.note, sizeof g.note, "Nothing was written. %s", g.walk.why);
        return;
    }
    if (jsrf_states_same(t->settings, g.walk.text, g.walk.len)) {
        snprintf(g.note, sizeof g.note, "The story walk reached %d of the story's %d missions. That's the "
                                        "start-states.txt there now, byte for byte, so there's nothing to write.",
                 g.walk.reached, g.walk.story);
        jsrf_walk_free(&g.walk);
        return;
    }
    g.have = 1;
    g.warning.clear();
    g.warning.appendf("The story walk reached %d of the story's %d missions from New Game", g.walk.reached, g.walk.story);
    if (g.walk.marked)
        g.warning.appendf(", and %d more no exit leads to. Those start at their chapter's opening, and Developer > "
                          "Missions marks them as off the story's way", g.walk.marked);
    g.warning.appendf(".\n\nstart-states.txt will be written into\n%s\n\n", t->settings);
    if (js_is_dir(t->settings)) {
        char file[JSRF_PATH_MAX + 40];
        js_path(file, sizeof file, t->settings, JSRF_WALK_FILE);
        if (js_is_file(file)) g.warning.appendf("First, the one there now is copied into a dated backup in\n%s\n\n", g.backups);
    }
    g.warning.appendf("JSRF-PC reads it when it starts, so restart JSRF-PC if it's running.");
    ImGui::OpenPopup("Before start-states.txt is written");
}

static void draw(tools_ctx *t)
{
    float em = ImGui::GetFontSize();
    char file[JSRF_PATH_MAX + 40], backup[JSRF_PATH_MAX], why[600];
    int there;
    js_path(file, sizeof file, t->settings, JSRF_WALK_FILE);
    js_path(g.backups, sizeof g.backups, t->settings, BACKUPS_NAME);
    there = js_is_file(file);

    ImGui::TextWrapped("Developer > Missions restarts JSRF-PC at a story mission, for testing. With start-states.txt "
                       "beside the settings, each mission starts with the story up to it done; without it, with a "
                       "new game's progress. This page makes start-states.txt from your game's own mission files.");
    ImGui::Dummy(ImVec2(0, em * 0.25f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (t->game_files[0]) ImGui::TextWrapped("The game: %s", t->game_files);
    else ImGui::TextWrapped("The game: not known yet. Pick its folder, or drop it on this window.");
    ImGui::TextWrapped("start-states.txt: %s", there ? file : "not there, so missions start with a new game's progress");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, em * 0.25f));

    ImGui::BeginDisabled(walking() || g.have);
    if (tools_flow_button("Make start states", true)) {
        if (t->game_files[0]) start_walk(t->game_files);
        else pick_game();
    }
    if (tools_flow_button("Pick the game's folder...", false)) pick_game();
    ImGui::BeginDisabled(!there);
    if (tools_flow_button("Remove start states", false)) {
        g.note[0] = 0;
        g.warning.clear();
        g.warning.appendf("start-states.txt will be removed from\n%s\n\nFirst, it is copied into a dated backup in\n%s\n\n"
                          "Developer > Missions then starts each mission with a new game's progress. JSRF-PC reads "
                          "it when it starts, so restart JSRF-PC if it's running.", t->settings, g.backups);
        ImGui::OpenPopup("Before start-states.txt is removed");
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (walking()) {
        t->busy = 1;    /* frames keep coming, to see the walk end */
        ImGui::Dummy(ImVec2(0, em * 0.25f));
        ImGui::TextWrapped("Walking the story through %s's mission files...", g.game);
    } else if (g.thread) {
        walked(t);
    }
    if (g.note[0]) {
        ImGui::Dummy(ImVec2(0, em * 0.25f));
        ImGui::TextWrapped("%s", g.note);
    }

    if (tools_warning("Before start-states.txt is written", g.warning.c_str(), there ? "Back up, then write" : "Write")) {
        if (jsrf_states_write(t->settings, g.backups, g.walk.text, g.walk.len, backup, sizeof backup, why, sizeof why) == 0)
            snprintf(g.note, sizeof g.note, "Written. Developer > Missions now starts each story mission with the story "
                                            "up to it done.%s%s", backup[0] ? " The one that was there is backed up in " : "",
                     backup);
        else
            snprintf(g.note, sizeof g.note, "%s", why);
        forget();
    } else if (g.have && !ImGui::IsPopupOpen("Before start-states.txt is written")) {
        forget();   /* cancelled */
    }
    if (tools_warning("Before start-states.txt is removed", g.warning.c_str(), "Back up, then remove")) {
        if (jsrf_states_remove(t->settings, g.backups, backup, sizeof backup, why, sizeof why) == 0)
            snprintf(g.note, sizeof g.note, "Removed. Developer > Missions now starts each mission with a new game's "
                                            "progress.%s%s", backup[0] ? " It is backed up in " : "", backup);
        else
            snprintf(g.note, sizeof g.note, "%s", why);
    }
}

static void dropped(tools_ctx *t, const char *path)
{
    (void)t;
    if (walking() || g.have) return;
    if (js_is_dir(path)) start_walk(path);
    else snprintf(g.note, sizeof g.note, "Drop the game's folder, the one with default.xbe and Media in it.");
}

extern const tools_page tools_missions_page = { "Missions", draw, dropped };
