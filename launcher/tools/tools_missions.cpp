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
 *   Remove: the same warning and backup, and the file goes.
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
    int           note_kind;                /* TOOLS_OK, TOOLS_WARN or TOOLS_ERROR */
    char          backup[JSRF_PATH_MAX];    /* where the last write or removal backed the file up */
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
    g.backup[0] = 0;
    if (!g.thread) {
        snprintf(g.note, sizeof g.note, "The walk couldn't be started (%s).", SDL_GetError());
        g.note_kind = TOOLS_ERROR;
    }
}

/* ---- the page ---------------------------------------------------------------------------- */

static void pick_game(void)
{
    char picked[JSRF_PATH_MAX] = "";
    int r = jsrf_pick_folder("Where is the game? Pick the folder with default.xbe and Media in it", picked,
                             sizeof picked);
    if (r == JSRF_NO_DIALOG) {
        snprintf(g.note, sizeof g.note, "No folder dialog here (zenity or kdialog): drop the game's folder on "
                                        "this window.");
        g.note_kind = TOOLS_WARN;
    } else if (r == JSRF_PICKED) {
        start_walk(picked);
    }
}

static void walked(tools_ctx *t)
{
    SDL_WaitThread(g.thread, NULL);
    g.thread = NULL;
    if (g.failed) {
        snprintf(g.note, sizeof g.note, "Nothing written: %s", g.walk.why);
        g.note_kind = TOOLS_ERROR;
        return;
    }
    if (jsrf_states_same(t->settings, g.walk.text, g.walk.len)) {
        snprintf(g.note, sizeof g.note, "Already up to date (%d of %d story missions reached).", g.walk.reached,
                 g.walk.story);
        g.note_kind = TOOLS_OK;
        jsrf_walk_free(&g.walk);
        return;
    }
    g.have = 1;
    g.warning.clear();
    g.warning.appendf("The walk through your game reached %d of its %d story missions", g.walk.reached, g.walk.story);
    if (g.walk.marked)
        g.warning.appendf(", and %d more that no exit leads to (they start at their chapter's opening)", g.walk.marked);
    g.warning.appendf(".\n\nstart-states.txt goes in:\n%s\n\n", t->settings);
    if (js_is_dir(t->settings)) {
        char file[JSRF_PATH_MAX + 40];
        js_path(file, sizeof file, t->settings, JSRF_WALK_FILE);
        if (js_is_file(file)) g.warning.appendf("The one there now is backed up first.\n");
    }
    g.warning.appendf("Restart JSRF-PC if it's running.");
    ImGui::OpenPopup("Write start-states.txt?");
}

static void draw(tools_ctx *t)
{
    char file[JSRF_PATH_MAX + 40], why[600];
    int there;
    js_path(file, sizeof file, t->settings, JSRF_WALK_FILE);
    js_path(g.backups, sizeof g.backups, t->settings, BACKUPS_NAME);
    there = js_is_file(file);

    tools_heading("Start states");
    tools_hint("Developer > Missions starts each story mission with the story up to it done.");
    tools_help_marker("JSRF-PC reads start-states.txt, beside its settings, to know what the story has done "
                      "before each mission. Without it, every mission starts with a new game's progress. This "
                      "page makes the file from your own game's mission files, so it's never in a package.");
    tools_path("Game:", t->game_files);
    tools_path("File:", there ? file : "");
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.25f));

    ImGui::BeginDisabled(walking() || g.have);
    if (tools_flow_button("Make start states", true)) {
        if (t->game_files[0]) start_walk(t->game_files);
        else pick_game();
    }
    ImGui::BeginDisabled(!there);
    if (tools_flow_button("Remove", false)) {
        g.note[0] = 0;
        g.warning.clear();
        g.warning.appendf("start-states.txt is removed from:\n%s\n\nIt's backed up first. Missions then start "
                          "with a new game's progress.\nRestart JSRF-PC if it's running.", t->settings);
        ImGui::OpenPopup("Remove start-states.txt?");
    }
    ImGui::EndDisabled();
    if (tools_flow_button("Pick game...", false)) pick_game();
    ImGui::EndDisabled();

    if (walking()) {
        t->busy = 1;    /* frames keep coming, to see the walk end */
        tools_hint("Walking the story through the game's mission files...");
    } else if (g.thread) {
        walked(t);
    }
    if (g.note[0]) {
        tools_status(g.note_kind, "%s", g.note);
        if (g.backup[0]) tools_path("Backup:", g.backup);
    }

    if (tools_warning("Write start-states.txt?", g.warning.c_str(), "Write")) {
        if (jsrf_states_write(t->settings, g.backups, g.walk.text, g.walk.len, g.backup, sizeof g.backup, why,
                              sizeof why) == 0) {
            snprintf(g.note, sizeof g.note, "Written (%d of %d story missions reached).", g.walk.reached,
                     g.walk.story);
            g.note_kind = TOOLS_OK;
        } else {
            snprintf(g.note, sizeof g.note, "%s", why);
            g.note_kind = TOOLS_ERROR;
        }
        forget();
    } else if (g.have && !ImGui::IsPopupOpen("Write start-states.txt?")) {
        forget();   /* cancelled */
    }
    if (tools_warning("Remove start-states.txt?", g.warning.c_str(), "Remove")) {
        if (jsrf_states_remove(t->settings, g.backups, g.backup, sizeof g.backup, why, sizeof why) == 0) {
            snprintf(g.note, sizeof g.note, "Removed. Missions now start with a new game's progress.");
            g.note_kind = TOOLS_OK;
        } else {
            snprintf(g.note, sizeof g.note, "%s", why);
            g.note_kind = TOOLS_ERROR;
        }
    }
}

static void dropped(tools_ctx *t, const char *path)
{
    (void)t;
    if (walking() || g.have) return;
    if (js_is_dir(path)) start_walk(path);
    else {
        snprintf(g.note, sizeof g.note, "Drop the game's folder, the one with default.xbe and Media in it.");
        g.note_kind = TOOLS_WARN;
        g.backup[0] = 0;
    }
}

extern const tools_page tools_missions_page = { "Missions", draw, dropped };
