/*
 * JSRF-PC Tools' saves page (tools.h; b3 row 36).
 *
 *   Import: saves from Cxbx-Reloaded, xemu, xbased, or any folder or disk
 *     image (a real Xbox's saves copied off it among them), into JSRF-PC,
 *     each re-signed with JSRF-PC's key.
 *   Export: JSRF-PC's saves into xbased (whose key is JSRF-PC's) or
 *     Cxbx-Reloaded (whose key comes from the player's own default.xbe, as
 *     Cxbx makes it without keys). No console's keys are asked for or read,
 *     so xemu and a real Xbox aren't offered (spriteice, 7:46pm on
 *     2026-11-26).
 *
 * Nothing is written until the page's own button, then the shell's warning,
 * which names every save that replaces one, then a dated backup of where
 * they go (save_import.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"

#include "file_dialog.h"
#include "save_import.h"
#include "setup.h"
#include "setup_job.h"
#include "tools.h"

#define MODE_IMPORT 0
#define MODE_EXPORT 1

static const char *const SOURCE_NAMES[] = { "", "Cxbx-Reloaded", "xbased", "xemu" };

struct page_state {
    int        mode;
    jsrf_found found;               /* the saves shown */
    int        have;                /* found holds something read */
    int        chosen[512];
    char       from[JSRF_PATH_MAX]; /* where they were read from */
    char       to[JSRF_PATH_MAX];   /* the title folder they go to */
    char       backups[JSRF_PATH_MAX];
    int        target;              /* for an export: JSRF_SRC_CXBX or JSRF_SRC_XBASED */
    uint8_t    key[JSRF_KEY_BYTES]; /* what they're signed with where they go */
    jsrf_plan  plan;
    int        planned, ran;
    char       note[JSRF_PATH_MAX + 800];           /* the page's last word to the player */
    ImGuiTextBuffer warning;
};
static page_state g;   /* zeroed as a static; its ImGuiTextBuffer starts empty */

static void forget(void)
{
    if (g.planned) jsrf_plan_free(&g.plan);
    if (g.have) jsrf_found_free(&g.found);
    g.have = g.planned = g.ran = 0;
    g.from[0] = g.to[0] = 0;
}

static void replan(void)
{
    int i;
    if (g.planned) jsrf_plan_free(&g.plan);
    g.planned = 0;
    if (!g.have || !g.to[0]) return;
    for (i = g.found.nsaves; i < (int)(sizeof g.chosen / sizeof g.chosen[0]); i++) g.chosen[i] = 0;
    g.planned = jsrf_plan_make(&g.found, g.chosen, g.to, &g.plan) == 0;
}

static void shown(int r, tools_ctx *t)
{
    int i;
    if (r != 0) {
        snprintf(g.note, sizeof g.note, "%s", g.found.why);
        jsrf_found_free(&g.found);
        g.have = 0;
        return;
    }
    g.have = 1;
    g.ran = 0;
    g.note[0] = 0;
    for (i = 0; i < g.found.nsaves && i < (int)(sizeof g.chosen / sizeof g.chosen[0]); i++)
        g.chosen[i] = g.found.saves[i].kind != JSRF_KIND_OTHER;
    if (g.mode == MODE_IMPORT) {
        snprintf(g.to, sizeof g.to, "%s", t->title);
        snprintf(g.backups, sizeof g.backups, "%s", t->backups);
        memcpy(g.key, jsrf_signature_key, sizeof g.key);
    }
    replan();
}

/* ---- import ---------------------------------------------------------------------------- */

static void read_from(tools_ctx *t, const char *path)
{
    forget();
    snprintf(g.from, sizeof g.from, "%s", path);
    shown(jsrf_read_source(path, &g.found), t);
}

static void pick_one(void *ctx, const char *path)
{
    char *out = (char *)ctx;
    if (!out[0]) snprintf(out, JSRF_PATH_MAX, "%s", path);
}

static void from_source(tools_ctx *t, int source)
{
    static char places[4][JSRF_PATH_MAX];
    char picked[JSRF_PATH_MAX] = "";
    int n = jsrf_usual_places(source, places, 4), r;
    if (n >= 1) { read_from(t, places[0]); return; }
    if (source == JSRF_SRC_XEMU) {
        static const char *const pats[] = { "*.qcow2", "*.img", "*.toml", NULL };
        r = jsrf_pick_files("Where is xemu's hard disk? Pick its image (xbox_hdd.qcow2) or its xemu.toml",
                            "xemu's hard disk", pats, 0, pick_one, picked);
    } else {
        char title[200];
        snprintf(title, sizeof title, "Where is %s? Pick its folder (the one with %s in it)",
                 SOURCE_NAMES[source], source == JSRF_SRC_CXBX ? "EmuDisk" : "hdd");
        r = jsrf_pick_folder(title, picked, sizeof picked);
    }
    if (r == JSRF_NO_DIALOG)
        snprintf(g.note, sizeof g.note, "%s isn't in its usual place, and there's no file dialog here "
                                        "(zenity or kdialog): drop its folder or file on this window.",
                 SOURCE_NAMES[source]);
    else if (r == JSRF_PICKED) read_from(t, picked);
}

/* ---- export ----------------------------------------------------------------------------- */

static void export_to(tools_ctx *t, int target, const char *picked)
{
    char title[JSRF_PATH_MAX], why[400];
    static uint8_t head[JSRF_XBE_HEAD];
    size_t got;
    forget();
    g.target = target;
    if (!jsrf_export_title(target, picked, title, sizeof title)) {
        snprintf(g.note, sizeof g.note, "%s isn't %s's folder: it has no %s\\Partition1.", picked,
                 SOURCE_NAMES[target], target == JSRF_SRC_CXBX ? "EmuDisk" : "hdd");
        return;
    }
    if (target == JSRF_SRC_CXBX) {
        /* Cxbx's key, from the player's own default.xbe: read now, kept only
         * while the page is open. */
        char game[JSRF_PATH_MAX];
        snprintf(game, sizeof game, "%s", t->game_files);
        if (!game[0] || jsrf_read_xbe_head(game, head, &got, why, sizeof why) != 0) {
            static const char *const pats[] = { "default.xbe", "*.iso", "*.xiso", NULL };
            game[0] = 0;
            if (jsrf_pick_files("Where is the game? Pick its default.xbe or its disc image",
                                "The game", pats, 0, pick_one, game) != JSRF_PICKED) {
                snprintf(g.note, sizeof g.note, "Exporting to Cxbx-Reloaded needs the game's default.xbe, "
                                                "to sign the saves as Cxbx does.");
                return;
            }
            if (game[0] && js_is_file(game) && strstr(game, ".xbe")) {
                FILE *f = js_fopen(game, "rb");
                got = f ? fread(head, 1, sizeof head, f) : 0;
                if (f) fclose(f);
            } else if (jsrf_read_xbe_head(game, head, &got, why, sizeof why) != 0) {
                snprintf(g.note, sizeof g.note, "%s", why);
                return;
            }
        }
        if (jsrf_cxbx_key(head, got, g.key) != 0) {
            snprintf(g.note, sizeof g.note, "That default.xbe isn't Jet Set Radio Future's.");
            return;
        }
    } else {
        memcpy(g.key, jsrf_signature_key, sizeof g.key);
    }
    snprintf(g.to, sizeof g.to, "%s", title);
    js_path(g.backups, sizeof g.backups, t->backups, SOURCE_NAMES[target]);
    snprintf(g.from, sizeof g.from, "%s", t->title);
    shown(jsrf_read_title_dir(t->title, &g.found), t);
}

static void to_target(tools_ctx *t, int target)
{
    char folder[JSRF_PATH_MAX], picked[JSRF_PATH_MAX] = "", q[200];
    int r;
    if (jsrf_usual_export(target, folder, sizeof folder)) { export_to(t, target, folder); return; }
    snprintf(q, sizeof q, "Where is %s? Pick its folder (the one with %s in it)", SOURCE_NAMES[target],
             target == JSRF_SRC_CXBX ? "EmuDisk" : "hdd");
    r = jsrf_pick_folder(q, picked, sizeof picked);
    if (r == JSRF_NO_DIALOG)
        snprintf(g.note, sizeof g.note, "%s isn't in its usual place, and there's no folder dialog here "
                                        "(zenity or kdialog): drop its folder on this window.", SOURCE_NAMES[target]);
    else if (r == JSRF_PICKED) export_to(t, target, picked);
}

/* ---- the page ----------------------------------------------------------------------------- */

static const char *kind_name(int kind)
{
    return kind == JSRF_KIND_GAME ? "Game" : kind == JSRF_KIND_GRAFFITI ? "Graffiti" : "Not a save";
}

static const char *action_name(int action)
{
    switch (action) {
    case JSRF_DO_WRITE: return "Copied in";
    case JSRF_DO_REPLACE: return "Replaces the one there";
    case JSRF_DO_RESLOT: return "Into a free slot";
    default: return "Skipped";
    }
}

static void warning_text(void)
{
    int k;
    g.warning.clear();
    g.warning.appendf("%d save%s will be written into\n%s\n\n", g.plan.writes, g.plan.writes == 1 ? "" : "s", g.to);
    if (g.plan.replaces) {
        g.warning.appendf("These replace a save that is there now:\n");
        for (k = 0; k < g.plan.n; k++)
            if (g.plan.items[k].action == JSRF_DO_REPLACE)
                g.warning.appendf("  %s (%s)\n", g.found.saves[g.plan.items[k].save].name,
                                  g.found.saves[g.plan.items[k].save].folder);
        g.warning.appendf("\n");
    }
    g.warning.appendf("First, every save there is copied into a dated backup in\n%s\n\nClose JSRF-PC%s before going on.",
                      g.backups, g.mode == MODE_EXPORT ? " and the emulator" : "");
}

static void draw(tools_ctx *t)
{
    int k;
    float em = ImGui::GetFontSize();
    if (ImGui::RadioButton("Import into JSRF-PC", g.mode == MODE_IMPORT) && g.mode != MODE_IMPORT) {
        forget();
        g.mode = MODE_IMPORT;
        g.note[0] = 0;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Export from JSRF-PC", g.mode == MODE_EXPORT) && g.mode != MODE_EXPORT) {
        forget();
        g.mode = MODE_EXPORT;
        g.note[0] = 0;
    }
    ImGui::Dummy(ImVec2(0, em * 0.25f));
    if (g.mode == MODE_IMPORT) {
        ImGui::TextWrapped("Bring saves in from another emulator, or from a folder of saves copied off an "
                           "Xbox. Drop a folder or disk image on the window, or:");
        if (tools_flow_button("Cxbx-Reloaded", true)) from_source(t, JSRF_SRC_CXBX);
        if (tools_flow_button("xemu", false)) from_source(t, JSRF_SRC_XEMU);
        if (tools_flow_button("xbased", false)) from_source(t, JSRF_SRC_XBASED);
        if (tools_flow_button("A folder...", false)) {
            char picked[JSRF_PATH_MAX] = "";
            int r = jsrf_pick_folder("Pick a folder of JSRF saves (5345000a, or a folder above it)", picked, sizeof picked);
            if (r == JSRF_PICKED) read_from(t, picked);
            if (r == JSRF_NO_DIALOG) snprintf(g.note, sizeof g.note, "There's no folder dialog here: drop the folder on this window.");
        }
    } else {
        ImGui::TextWrapped("Send JSRF-PC's saves to another emulator, signed so it loads them. Only "
                           "emulators that need no console's keys are offered.");
        if (tools_flow_button("To xbased", true)) to_target(t, JSRF_SRC_XBASED);
        if (tools_flow_button("To Cxbx-Reloaded", false)) to_target(t, JSRF_SRC_CXBX);
    }
    if (g.note[0]) {
        ImGui::Dummy(ImVec2(0, em * 0.25f));
        ImGui::TextWrapped("%s", g.note);
    }
    if (!g.have) return;

    ImGui::Separator();
    ImGui::TextWrapped("From %s", g.from);
    ImGui::TextWrapped("To %s", g.to);
    {
        /* The table takes what the window has left, above the button row. */
        float below = g.ran ? ImGui::GetFrameHeightWithSpacing() * 2 + em * 5 : ImGui::GetFrameHeightWithSpacing() * 2 + em;
        ImVec2 size(0, ImGui::GetContentRegionAvail().y - below);
        if (size.y < em * 4) size.y = em * 4;
        if (ImGui::BeginTable("saves", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                              ImGuiTableFlags_SizingStretchProp, size)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("Signed", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("What happens", ImGuiTableColumnFlags_WidthStretch, 4);
            ImGui::TableHeadersRow();
            for (int i = 0; i < g.found.nsaves; i++) {
                const jsrf_save *s = &g.found.saves[i];
                const jsrf_plan_item *it = NULL;
                char label[600];
                bool on = g.chosen[i] != 0;
                if (g.planned)
                    for (k = 0; k < g.plan.n; k++) if (g.plan.items[k].save == i) it = &g.plan.items[k];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                snprintf(label, sizeof label, "%s##%d", s->name[0] ? s->name : s->from, i);
                ImGui::BeginDisabled(s->kind == JSRF_KIND_OTHER || g.ran);
                if (ImGui::Checkbox(label, &on)) { g.chosen[i] = on; replan(); }
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kind_name(s->kind));
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", s->kind == JSRF_KIND_OTHER ? "" : s->signed_here ? "For JSRF-PC" : "With another key");
                ImGui::TableNextColumn();
                if (it) {
                    if (g.ran || (it->note[0] && it->action != JSRF_DO_SKIP)) ImGui::TextWrapped("%s", it->note);
                    else if (it->note[0]) ImGui::TextWrapped("%s: %s", action_name(it->action), it->note);
                    else ImGui::TextWrapped("%s", action_name(it->action));
                } else if (s->kind == JSRF_KIND_OTHER) {
                    ImGui::TextWrapped("%s", s->why);
                } else {
                    ImGui::TextDisabled("Not chosen");
                }
            }
            ImGui::EndTable();
        }
    }
    if (g.ran) {
        if (g.plan.backup[0]) ImGui::TextWrapped("The saves that were there are backed up in %s", g.plan.backup);
        if (g.plan.why[0]) ImGui::TextWrapped("%s", g.plan.why);
        if (ImGui::Button("Done")) forget();
        return;
    }
    ImGui::BeginDisabled(!g.planned || !g.plan.writes);
    if (ImGui::Button(g.mode == MODE_IMPORT ? "Import to save" : "Export")) {
        warning_text();
        ImGui::OpenPopup("Before anything is written");
    }
    ImGui::EndDisabled();
    if (tools_warning("Before anything is written", g.warning.c_str(), g.mode == MODE_IMPORT ? "Back up, then import" : "Back up, then export")) {
        int r = jsrf_plan_run(&g.found, &g.plan, g.to, g.backups, g.key);
        g.ran = 1;
        snprintf(g.note, sizeof g.note, "%s", r == 0 ? (g.mode == MODE_IMPORT ? "Imported." : "Exported.")
                                                     : "Not everything was written: the list says what.");
    }
}

static void dropped(tools_ctx *t, const char *path)
{
    if (g.mode == MODE_IMPORT) {
        read_from(t, path);
    } else if (js_is_dir(path)) {
        /* Which emulator's: Cxbx-Reloaded's folder has EmuDisk in it. */
        char title[JSRF_PATH_MAX];
        int target = jsrf_export_title(JSRF_SRC_CXBX, path, title, sizeof title) &&
                     !jsrf_export_title(JSRF_SRC_XBASED, path, title, sizeof title) ? JSRF_SRC_CXBX : JSRF_SRC_XBASED;
        export_to(t, target, path);
    }
}

extern const tools_page tools_saves_page = { "Saves", draw, dropped };
