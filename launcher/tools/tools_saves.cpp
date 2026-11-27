/*
 * JSRF-PC Tools' save manager (tools.h; b3 row 36).
 *
 *   JSRF-PC's saves: what the game loads, where its game save came from,
 *     and the designs in them, which the player can delete.
 *   Save slots: 8 copies of all of JSRF-PC's saves that the player keeps,
 *     each named: saved into (from JSRF-PC's saves, a backup, or an
 *     import), loaded back, renamed, cleared.
 *   Backups: the dated copies made before anything replaces or deletes
 *     JSRF-PC's saves: restored, copied into a slot, renamed, deleted.
 *   Other emulators: Cxbx-Reloaded and xbased, their folders and the saves
 *     in them shown when the player wants (tools.cfg's show_emulators), each
 *     folder found in its usual place or picked (and then remembered). Saves
 *     imported from them, or from any folder (a real Xbox's saves copied off
 *     it among them), into JSRF-PC's saves or a slot, are re-signed with
 *     JSRF-PC's key. Exports go to xbased (whose key is JSRF-PC's) or
 *     Cxbx-Reloaded (whose key comes from the player's own default.xbe, as
 *     Cxbx makes it without keys). No console's keys are asked for or read
 *     (spriteice, 7:46pm on 2026-11-26); xemu isn't offered (spriteice,
 *     10:55am on 2026-11-27).
 *
 * Nothing is written until the page's own button, then the shell's warning,
 * and anything that changes JSRF-PC's saves backs them up first
 * (save_store.c, save_import.c).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"

#include "file_dialog.h"
#include "graffiti_save.h"
#include "save_import.h"
#include "save_store.h"
#include "setup.h"
#include "setup_job.h"
#include "tools.h"

#define MODE_IMPORT 0
#define MODE_EXPORT 1

#define SEC_MINE     1
#define SEC_SLOTS    2
#define SEC_BACKUPS  3
#define SEC_EMUS     4

#define REREAD_S     3.0           /* how often what's on disk is read again while the page shows */

#define NEMUS        2
static const char *const SOURCE_NAMES[] = { "", "Cxbx-Reloaded", "xbased" };
static const char *const SOURCE_KEYS[] = { "", "cxbx_folder", "xbased_folder" };
static const int EMU_ORDER[NEMUS] = { JSRF_SRC_CXBX, JSRF_SRC_XBASED };
static const char *const SIZE_NAMES[] = { "SS", "S", "M", "L", "XL" };

/* The page's own types, kept to this file: each page has a page_state, and
 * without this the linker keeps one page's destructor for all of them, which
 * then frees another page's text as pointers when the tool closes. */
namespace {

struct design_row {
    unsigned slot;
    char     folder[JSRF_FOLDER_CHARS + 1];
    int      cls, w, h;            /* the preview's; cls -1 when it couldn't be read */
    unsigned tex;
    bool     ticked;
};

struct emu_row {
    char where[JSRF_PATH_MAX];     /* its folder; "" not found */
    int  picked;                   /* where is the player's pick (tools.cfg), not the usual place */
    int  ok;                       /* JSRF saves were read there */
    char what[400];                /* what's there, or why nothing */
};

struct page_state {
    int        inited;
    int        drawn_frame;        /* the frame the page last drew: a gap means it was just shown */
    double     read_at;
    jsrf_held_count mine;
    char       game_when[32];
    jsrf_origin origin;            /* where JSRF-PC's game save came from */
    ImVector<design_row> designs;
    jsrf_held  slots[JSRF_STORE_SLOTS];
    int        slot_sel;
    jsrf_held *backups;
    int        nbackups, backup_sel;
    char       backup_sel_dir[JSRF_PATH_MAX];
    int        show_emus;
    emu_row    emus[NEMUS + 1];    /* by source */
    int        emu_sel;            /* a source, 0 none */
    int        st_section, st_kind;
    char       st[JSRF_PATH_MAX + 800];
    char       name[JSRF_STORE_NAME];
    ImGuiTextBuffer text;

    /* the review of an import or export */
    int        reviewing, mode;
    char       source_name[64];
    jsrf_found found;
    int        have;
    int        chosen[512];
    char       from[JSRF_PATH_MAX];
    char       to[JSRF_PATH_MAX];
    char       backups_to[JSRF_PATH_MAX];
    uint8_t    key[JSRF_KEY_BYTES];
    jsrf_plan  plan;
    int        planned, ran;
    int        into_slot;          /* an import into a slot: its number, 1 to 8; 0 into JSRF-PC's saves */

    /* a slot picked to copy or import into */
    int        pick_slot;
};
}  /* namespace */

static page_state g;

static void status(int section, int kind, const char *fmt, ...) IM_FMTARGS(3);
static void status(int section, int kind, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g.st, sizeof g.st, fmt, ap);
    va_end(ap);
    g.st_section = section;
    g.st_kind = kind;
}

/* A date column: as wide as the widest date, so a narrow window wraps the
 * other columns instead of cutting a date short. */
static float date_width(void)
{
    return ImGui::CalcTextSize("28 Nov 2026, 12:59pm").x + ImGui::GetStyle().CellPadding.x * 2;
}

/* A cell's text, wrapped to the cell. */
static void cell_text(const char *text, bool grey)
{
    if (grey) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    if (grey) ImGui::PopStyleColor();
}

static void show_status(int section)
{
    if (g.st_section == section && g.st[0]) tools_status(g.st_kind, "%s", g.st);
}

static void slots_dir(const tools_ctx *t, char *out, size_t n)
{
    js_path(out, n, t->saves, "Save slots");
}

static void origin_file(const tools_ctx *t, char *out, size_t n)
{
    js_path(out, n, t->saves, JSRF_ORIGIN_FILE);
}

/* A slot as the origin names it: slot 2 ("Test"). */
static void slot_what(int i, char *out, size_t n)
{
    if (g.slots[i].name[0]) snprintf(out, n, "slot %d (\"%s\")", i + 1, g.slots[i].name);
    else snprintf(out, n, "slot %d", i + 1);
}

/* A backup as the origin names it: the backup "Before loading slot 1", or
 * the backup of 27 Nov 2026, 10:57am. */
static void backup_what(const jsrf_held *h, char *out, size_t n)
{
    char when[64];
    if (h->name[0]) { snprintf(out, n, "the backup \"%s\"", h->name); return; }
    jsrf_store_when_text(h->when[0] ? h->when : h->dir, when, sizeof when);
    snprintf(out, n, "the backup of %s", when);
}

/* The slot offered first: the first empty one, so nothing is replaced
 * unless the player picks it; else the one picked above; else slot 1. */
static int slot_to_offer(void)
{
    for (int i = 0; i < JSRF_STORE_SLOTS; i++)
        if (!g.slots[i].used) return i;
    return g.slot_sel >= 0 ? g.slot_sel : 0;
}

/* A slot in the slot prompt's list: "Slot 2: Test (Game save from xbased)". */
static void slot_label(int i, char *out, size_t n)
{
    jsrf_held *h = &g.slots[i];
    char what[100];
    if (!h->used) { snprintf(out, n, "Slot %d: empty", i + 1); return; }
    jsrf_store_holds_text(&h->count, h->origin.first, what, sizeof what);
    snprintf(out, n, "Slot %d: %s (%s)", i + 1, h->name[0] ? h->name : "no name", what);
}

/* Which slot to put saves into, and its name: a modal like the shell's
 * name prompt, with the 8 slots to pick from (*slot, 0 to 7). True the
 * frame go is pressed, or Enter. */
static bool slot_prompt(const char *id, const char *text, const char *go, int *slot, char *name, size_t n)
{
    bool pressed = false;
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.8f, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(id, NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        char label[JSRF_STORE_NAME + 140];
        float at = ImGui::GetCursorPosX() + ImGui::CalcTextSize("Name:").x + ImGui::GetStyle().ItemSpacing.x * 2;
        bool enter;
        if (*slot < 0 || *slot >= JSRF_STORE_SLOTS) *slot = 0;
        if (text && text[0]) ImGui::TextWrapped("%s", text);
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.25f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Slot:");
        ImGui::SameLine(at);
        ImGui::SetNextItemWidth(-FLT_MIN);
        slot_label(*slot, label, sizeof label);
        bool open = ImGui::BeginCombo("##slot", label);
        tools_script_name(ImGui::GetID("##slot"), "Slot");
        if (open) {
            for (int i = 0; i < JSRF_STORE_SLOTS; i++) {
                slot_label(i, label, sizeof label);
                ImGui::PushID(i);
                if (ImGui::Selectable(label, i == *slot)) *slot = i;
                if (i == *slot) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name:");
        ImGui::SameLine(at);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        enter = ImGui::InputTextWithHint("##name", "Name (or leave it empty)", name, n,
                                         ImGuiInputTextFlags_EnterReturnsTrue);
        if (g.slots[*slot].used)
            tools_status(TOOLS_WARN, "Slot %d's saves now are replaced, and aren't backed up.", *slot + 1);
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
        if (ImGui::Button(go) || enter) { pressed = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return pressed;
}

/* ---- reading what's on disk ------------------------------------------------------------ */

static void read_preview(const tools_ctx *t, design_row *d)
{
    static uint8_t rgba[JSRF_PREVIEW_W * JSRF_PREVIEW_H * 4];
    char dir[JSRF_PATH_MAX], p[JSRF_PATH_MAX];
    js_path(dir, sizeof dir, t->title, d->folder);
    js_path(p, sizeof p, dir, JSRF_GRAFFITI_FILE);
    d->cls = jsrf_design_preview(p, rgba, &d->w, &d->h);
    if (d->cls < 0) {
        js_path(p, sizeof p, dir, "SAVE.DAT");
        d->cls = jsrf_design_preview(p, rgba, &d->w, &d->h);
    }
    if (d->cls >= 0) d->tex = tools_texture_put(d->tex, rgba, d->w, d->h);
}

/* The designs in JSRF-PC's saves, in the garage's order. A design already
 * listed keeps its tick, and its preview unless fresh. */
static void read_designs(const tools_ctx *t, int fresh)
{
    uint8_t taken[GRAFFITI_SLOTS];
    ImVector<design_row> now;
    graffiti_slots_taken(t->title, taken);
    for (unsigned s = 0; s < GRAFFITI_SLOTS; s++) {
        design_row d;
        int had = -1;
        if (!taken[s]) continue;
        for (int i = 0; i < g.designs.Size; i++)
            if (g.designs[i].slot == s) had = i;
        if (had >= 0) {
            d = g.designs[had];
            g.designs[had].tex = 0;            /* moved to d */
            if (fresh) read_preview(t, &d);
        } else {
            memset(&d, 0, sizeof d);
            d.slot = s;
            graffiti_slot_folder(s, d.folder);
            read_preview(t, &d);
        }
        now.push_back(d);
    }
    for (int i = 0; i < g.designs.Size; i++) tools_texture_drop(&g.designs[i].tex);
    g.designs.swap(now);
}

static void read_backups(const tools_ctx *t)
{
    free(g.backups);
    g.backups = NULL;
    g.nbackups = jsrf_store_backups(t->backups, &g.backups);
    if (g.nbackups < 0) g.nbackups = 0;
    g.backup_sel = -1;
    for (int i = 0; i < g.nbackups; i++)
        if (g.backup_sel_dir[0] && !strcmp(g.backups[i].dir, g.backup_sel_dir)) g.backup_sel = i;
}

/* An emulator's folder: the player's pick when it is still there, else the
 * usual place. */
static void find_emu(const tools_ctx *t, int source)
{
    emu_row *e = &g.emus[source];
    char v[JSRF_PATH_MAX];
    static char places[4][JSRF_PATH_MAX];
    e->where[0] = 0;
    e->picked = 0;
    if (tools_setting(t, SOURCE_KEYS[source], v, sizeof v) && v[0] && (js_is_dir(v) || js_is_file(v))) {
        snprintf(e->where, sizeof e->where, "%s", v);
        e->picked = 1;
    } else if (jsrf_usual_export(source, v, sizeof v)) {
        snprintf(e->where, sizeof e->where, "%s", v);
    } else if (jsrf_usual_places(source, places, 4) >= 1) {
        snprintf(e->where, sizeof e->where, "%s", places[0]);
    }
}

static void count_found(const jsrf_found *f, jsrf_held_count *c)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < f->nsaves; i++) {
        if (f->saves[i].kind == JSRF_KIND_GAME) c->game = 1;
        else if (f->saves[i].kind == JSRF_KIND_GRAFFITI) c->designs++;
        else c->others++;
    }
}

static void read_emu(const tools_ctx *t, int source)
{
    emu_row *e = &g.emus[source];
    jsrf_found f;
    find_emu(t, source);
    e->ok = 0;
    if (!e->where[0]) {
        snprintf(e->what, sizeof e->what, "Not in its usual place: pick its folder.");
        return;
    }
    memset(&f, 0, sizeof f);
    if (jsrf_read_source(e->where, &f) == 0) {
        jsrf_held_count c;
        count_found(&f, &c);
        jsrf_store_count_text(&c, e->what, sizeof e->what);
        e->ok = 1;
    } else {
        snprintf(e->what, sizeof e->what, "%s", f.why[0] ? f.why : "No JSRF saves there.");
    }
    jsrf_found_free(&f);
}

/* Everything read again: fresh after a change, or when the page is shown
 * (the previews and the emulators too); otherwise only what's cheap. */
static void reread(tools_ctx *t, int fresh)
{
    char p[JSRF_PATH_MAX], s[JSRF_PATH_MAX];
    jsrf_store_count(t->title, &g.mine);
    g.game_when[0] = 0;
    if (jsrf_store_game_file(t->title, p, sizeof p)) jsrf_store_file_when(p, g.game_when);
    origin_file(t, p, sizeof p);
    jsrf_origin_read(p, &g.origin);
    read_designs(t, fresh);
    slots_dir(t, s, sizeof s);
    jsrf_store_slots(s, g.slots);
    read_backups(t);
    if (fresh && g.show_emus)
        for (int i = 0; i < NEMUS; i++) read_emu(t, EMU_ORDER[i]);
    g.read_at = ImGui::GetTime();
}

/* ---- JSRF-PC's saves and its designs -------------------------------------------------- */

static int ticked(void)
{
    int n = 0;
    for (int i = 0; i < g.designs.Size; i++) n += g.designs[i].ticked;
    return n;
}

static void delete_designs(tools_ctx *t)
{
    char (*folders)[JSRF_FOLDER_CHARS + 1];
    char made[JSRF_PATH_MAX], why[600], label[80];
    int n = 0, r, want = ticked();
    if (!want) return;
    folders = (char (*)[JSRF_FOLDER_CHARS + 1])malloc(sizeof *folders * (size_t)want);
    if (!folders) return;
    for (int i = 0; i < g.designs.Size; i++)
        if (g.designs[i].ticked) memcpy(folders[n++], g.designs[i].folder, sizeof *folders);
    snprintf(label, sizeof label, "Before deleting %d design%s", n, n == 1 ? "" : "s");
    r = jsrf_store_delete_saves(t->title, t->backups, label, folders, n, made, sizeof made, why, sizeof why);
    free(folders);
    if (r < 0) status(SEC_MINE, TOOLS_ERROR, "%s", why);
    else status(SEC_MINE, TOOLS_OK, "Deleted %d design%s. The backup before it is the newest under Backups.", r,
                r == 1 ? "" : "s");
    reread(t, 1);
}

static void draw_designs(tools_ctx *t)
{
    float em = ImGui::GetFontSize(), ph = em * 1.6f;
    int n = ticked();
    if (!g.designs.Size) {
        ImGui::TextDisabled("No designs yet. The Graffiti page makes them.");
        return;
    }
    {
        float row = ph + ImGui::GetStyle().CellPadding.y * 2;
        int shown = g.designs.Size < 4 ? g.designs.Size : 4;
        ImVec2 size(0, row * (shown + 0.5f) + ImGui::GetFrameHeight());
        if (g.designs.Size <= 4) size.y = 0;
        if (ImGui::BeginTable("designs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                (g.designs.Size > 4 ? ImGuiTableFlags_ScrollY : 0), size)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##tick", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableSetupColumn("Design", ImGuiTableColumnFlags_WidthFixed, ph * 4 + em * 0.5f);
            ImGui::TableSetupColumn("In the garage", ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1);
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin(g.designs.Size, row);
            while (clip.Step()) {
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                    design_row *d = &g.designs[i];
                    int page, r, c;
                    char id[32];
                    ImGui::TableNextRow(0, row);
                    ImGui::TableNextColumn();
                    snprintf(id, sizeof id, "##d%u", d->slot);
                    ImGui::Checkbox(id, &d->ticked);
                    ImGui::TableNextColumn();
                    if (d->cls >= 0 && d->tex)
                        ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)d->tex), ImVec2(ph * d->w / d->h, ph));
                    else
                        ImGui::TextDisabled("(can't read)");
                    ImGui::TableNextColumn();
                    graffiti_slot_place(d->slot, &page, &r, &c);
                    ImGui::Text("Page %d, row %d, column %d", page + 1, r + 1, c + 1);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(d->cls >= 0 ? SIZE_NAMES[d->cls] : "");
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::BeginDisabled(!n);
    {
        char label[40];
        snprintf(label, sizeof label, n ? "Delete %d###del" : "Delete###del", n);
        if (ImGui::Button(label)) {
            g.text.clear();
            g.text.appendf("%d design%s %s taken out of JSRF-PC's saves. They're backed up first, so restoring "
                           "that backup brings %s back.\n\nClose JSRF-PC before going on.",
                           n, n == 1 ? "" : "s", n == 1 ? "is" : "are", n == 1 ? "it" : "them");
            ImGui::OpenPopup("###deldesigns");
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("All")) for (int i = 0; i < g.designs.Size; i++) g.designs[i].ticked = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(!n);
    if (ImGui::Button("None")) for (int i = 0; i < g.designs.Size; i++) g.designs[i].ticked = false;
    ImGui::EndDisabled();
    {
        char id[64];
        snprintf(id, sizeof id, "Delete %d design%s?###deldesigns", n, n == 1 ? "" : "s");
        if (tools_warning(id, g.text.c_str(), "Delete")) delete_designs(t);
    }
}

static void draw_mine(tools_ctx *t)
{
    char what[100], when[64], from[400];
    ImGui::PushID("mine");
    tools_heading("JSRF-PC's saves");
    tools_hint("What the game loads. Anything here that changes them makes a backup first.");
    tools_help_marker("Close JSRF-PC before changing its saves here, so it doesn't save over what you did.");
    tools_path("Folder:", t->title);
    if (!g.mine.game && !g.mine.designs && !g.mine.others) {
        ImGui::TextDisabled("No saves yet. Play once, or load a slot or import some below.");
    } else {
        jsrf_store_count_text(&g.mine, what, sizeof what);
        if (g.game_when[0]) {
            jsrf_store_when_text(g.game_when, when, sizeof when);
            ImGui::Text("%s. Game last saved %s.", what, when);
        } else {
            ImGui::Text("%s.", what);
        }
        /* Where its game save came from, and whether the game has saved
         * over it since. */
        jsrf_origin_text(&g.origin, from, sizeof from);
        if (g.mine.game && from[0]) {
            size_t l = strlen(from);
            if (g.origin.game[0] && g.game_when[0] && strcmp(g.origin.game, g.game_when) && l < sizeof from)
                snprintf(from + l, sizeof from - l, " The game has saved since.");
            ImGui::TextWrapped("Game save: %c%s", from[0] - 'A' + 'a', from + 1);
        }
    }
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.25f));
    draw_designs(t);
    show_status(SEC_MINE);
    ImGui::PopID();
}

/* ---- save slots ------------------------------------------------------------------------ */

static void draw_slots(tools_ctx *t)
{
    char s[JSRF_PATH_MAX], when[64], what[400], id[80], why[600];
    jsrf_held *sel = g.slot_sel >= 0 ? &g.slots[g.slot_sel] : NULL;
    int has_saves = g.mine.game || g.mine.designs || g.mine.others;
    slots_dir(t, s, sizeof s);
    ImGui::PushID("slots");
    tools_heading("Save slots");
    tools_hint("Keep up to 8 sets of saves of your own, and load one back whenever you like.");
    tools_help_marker("A slot holds a whole set of saves: the game save and every design. Save JSRF-PC's into one "
                      "here, copy a backup into one under Backups, or import another emulator's straight into one. "
                      "Loading a slot makes its saves JSRF-PC's, after a backup of the ones there now. Slots are "
                      "kept in Save slots, beside Save backups.");
    if (ImGui::BeginTable("slots", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.5f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthFixed, date_width());
        ImGui::TableSetupColumn("Holds", ImGuiTableColumnFlags_WidthStretch, 2.4f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < JSRF_STORE_SLOTS; i++) {
            jsrf_held *h = &g.slots[i];
            char label[16];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            snprintf(label, sizeof label, "%d##slot", i + 1);
            ImGui::PushID(i);
            if (ImGui::Selectable(label, g.slot_sel == i, ImGuiSelectableFlags_SpanAllColumns))
                g.slot_sel = g.slot_sel == i ? -1 : i;
            if (h->used) {
                jsrf_origin_text(&h->origin, what, sizeof what);
                if (what[0]) ImGui::SetItemTooltip("%s", what);
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            if (!h->used) ImGui::TextDisabled("Empty");
            else if (h->name[0]) cell_text(h->name, false);
            else ImGui::TextDisabled("No name");
            ImGui::TableNextColumn();
            if (h->used) {
                jsrf_store_when_text(h->when, when, sizeof when);
                ImGui::TextUnformatted(when);
                ImGui::TableNextColumn();
                jsrf_store_holds_text(&h->count, h->origin.first, what, sizeof what);
                cell_text(what, false);
            } else {
                ImGui::TableNextColumn();
            }
        }
        ImGui::EndTable();
    }
    if (!sel) ImGui::TextDisabled("Pick a slot to save into or load.");
    ImGui::BeginDisabled(!sel || !has_saves);
    if (ImGui::Button("Save here")) {
        snprintf(g.name, sizeof g.name, "%s", sel->name);
        g.text.clear();
        g.text.appendf("JSRF-PC's saves now are copied into slot %d%s.", g.slot_sel + 1,
                       sel->used ? ", in place of what it holds" : "");
        ImGui::OpenPopup("###saveslot");
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!sel || !sel->used);
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        jsrf_store_when_text(sel->when, when, sizeof when);
        g.text.clear();
        g.text.appendf("JSRF-PC's saves are replaced by slot %d's%s%s%s, saved %s.\n\nThe ones there now are backed "
                       "up first. Close JSRF-PC before going on.", g.slot_sel + 1, sel->name[0] ? " (\"" : "",
                       sel->name, sel->name[0] ? "\")" : "", when);
        ImGui::OpenPopup("###loadslot");
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename")) {
        snprintf(g.name, sizeof g.name, "%s", sel->name);
        ImGui::OpenPopup("###renameslot");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        g.text.clear();
        g.text.appendf("Slot %d's saves are deleted. JSRF-PC's own saves aren't touched.\n\nThis can't be undone.",
                       g.slot_sel + 1);
        ImGui::OpenPopup("###clearslot");
    }
    ImGui::EndDisabled();

    snprintf(id, sizeof id, "Save into slot %d?###saveslot", g.slot_sel + 1);
    if (tools_name_prompt(id, g.text.c_str(), "Save", g.name, sizeof g.name) && sel) {
        jsrf_origin o;
        memset(&o, 0, sizeof o);
        snprintf(o.how, sizeof o.how, "%s", JSRF_HOW_SAVED);
        snprintf(o.what, sizeof o.what, "JSRF-PC's saves");
        jsrf_store_now(o.at);
        snprintf(o.first, sizeof o.first, "%s", g.origin.first);
        if (jsrf_store_save_slot(t->title, s, g.slot_sel, g.name, &o, why, sizeof why) == JSRF_WROTE)
            status(SEC_SLOTS, TOOLS_OK, "Saved into slot %d.", g.slot_sel + 1);
        else
            status(SEC_SLOTS, TOOLS_ERROR, "%s", why);
        reread(t, 1);
    }
    snprintf(id, sizeof id, "Load slot %d?###loadslot", g.slot_sel + 1);
    if (tools_warning(id, g.text.c_str(), "Load") && sel) {
        char label[60], made[JSRF_PATH_MAX], rec[JSRF_PATH_MAX], from[JSRF_STORE_NAME + 40];
        snprintf(label, sizeof label, "Before loading slot %d", g.slot_sel + 1);
        slot_what(g.slot_sel, from, sizeof from);
        if (jsrf_store_load(sel->dir, t->title, t->backups, label, made, sizeof made, why, sizeof why) == JSRF_WROTE) {
            origin_file(t, rec, sizeof rec);
            jsrf_origin_note(rec, t->title, JSRF_HOW_LOADED, from, sel->origin.first);
            status(SEC_SLOTS, TOOLS_OK, "Loaded slot %d.%s", g.slot_sel + 1,
                   made[0] ? " The saves from before are the newest backup." : "");
        } else {
            status(SEC_SLOTS, TOOLS_ERROR, "%s", why);
        }
        reread(t, 1);
    }
    snprintf(id, sizeof id, "Rename slot %d###renameslot", g.slot_sel + 1);
    if (tools_name_prompt(id, NULL, "Rename", g.name, sizeof g.name) && sel) {
        if (jsrf_store_rename(sel->dir, g.name) == 0) status(SEC_SLOTS, TOOLS_OK, "Renamed.");
        else status(SEC_SLOTS, TOOLS_ERROR, "Slot %d couldn't be renamed.", g.slot_sel + 1);
        reread(t, 0);
    }
    snprintf(id, sizeof id, "Clear slot %d?###clearslot", g.slot_sel + 1);
    if (tools_warning(id, g.text.c_str(), "Clear") && sel) {
        if (jsrf_store_delete(sel->dir) == 0) status(SEC_SLOTS, TOOLS_OK, "Slot %d cleared.", g.slot_sel + 1);
        else status(SEC_SLOTS, TOOLS_ERROR, "Slot %d couldn't be cleared (is a file in it open?).", g.slot_sel + 1);
        reread(t, 0);
    }
    show_status(SEC_SLOTS);
    ImGui::PopID();
}

/* ---- backups --------------------------------------------------------------------------- */

static void draw_backups(tools_ctx *t)
{
    char when[64], what[400], why[600], id[80];
    jsrf_held *sel = g.backup_sel >= 0 && g.backup_sel < g.nbackups ? &g.backups[g.backup_sel] : NULL;
    ImGui::PushID("backups");
    tools_heading("Backups");
    tools_hint("Made before anything replaces or deletes JSRF-PC's saves, newest first.");
    tools_help_marker("Restoring one makes it JSRF-PC's saves again, after a backup of the ones there now, so a "
                      "restore can be undone too. Copy to slot... keeps one in a save slot. A renamed backup keeps "
                      "its date. They're kept in Save backups, beside JSRF-PC's saves.");
    if (!g.nbackups) {
        ImGui::TextDisabled("No backups yet.");
        show_status(SEC_BACKUPS);
        ImGui::PopID();
        return;
    }
    {
        float row = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2;
        int shown = g.nbackups < 5 ? g.nbackups : 5;
        ImVec2 size(0, g.nbackups > 5 ? row * (shown + 1.5f) : 0);
        if (ImGui::BeginTable("backups", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                (g.nbackups > 5 ? ImGuiTableFlags_ScrollY : 0), size)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Made", ImGuiTableColumnFlags_WidthFixed, date_width());
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn("Holds", ImGuiTableColumnFlags_WidthStretch, 2.4f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin(g.nbackups);
            while (clip.Step()) {
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                    jsrf_held *h = &g.backups[i];
                    const char *base = h->dir + strlen(h->dir);
                    while (base > h->dir && base[-1] != '/' && base[-1] != '\\') base--;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (h->when[0]) jsrf_store_when_text(h->when, when, sizeof when);
                    else snprintf(when, sizeof when, "%s", base);
                    ImGui::PushID(i);
                    if (ImGui::Selectable(when, g.backup_sel == i, ImGuiSelectableFlags_SpanAllColumns)) {
                        g.backup_sel = g.backup_sel == i ? -1 : i;
                        snprintf(g.backup_sel_dir, sizeof g.backup_sel_dir, "%s", g.backup_sel >= 0 ? h->dir : "");
                    }
                    jsrf_origin_text(&h->origin, what, sizeof what);
                    if (what[0]) ImGui::SetItemTooltip("Its game save: %c%s\n%s", what[0] - 'A' + 'a', what + 1, h->dir);
                    else ImGui::SetItemTooltip("%s", h->dir);
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    if (h->name[0]) cell_text(h->name, false);
                    else ImGui::TextDisabled("No name");
                    ImGui::TableNextColumn();
                    jsrf_store_holds_text(&h->count, h->origin.first, what, sizeof what);
                    cell_text(what, false);
                }
            }
            ImGui::EndTable();
        }
    }
    sel = g.backup_sel >= 0 && g.backup_sel < g.nbackups ? &g.backups[g.backup_sel] : NULL;
    if (sel) jsrf_store_when_text(sel->when[0] ? sel->when : sel->dir, when, sizeof when);
    ImGui::BeginDisabled(!sel);
    if (ImGui::Button("Restore")) {
        g.text.clear();
        g.text.appendf("JSRF-PC's saves are replaced by the backup from %s%s%s%s.\n\nThe ones there now are backed "
                       "up first. Close JSRF-PC before going on.", when, sel->name[0] ? " (\"" : "", sel->name,
                       sel->name[0] ? "\")" : "");
        ImGui::OpenPopup("Restore this backup?");
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy to slot...")) {
        if (sel->name[0]) snprintf(g.name, sizeof g.name, "%s", sel->name);
        else snprintf(g.name, sizeof g.name, "Backup of %s", when);
        g.pick_slot = slot_to_offer();
        g.text.clear();
        g.text.appendf("The backup from %s%s%s%s goes into the slot you pick, to load whenever you like. JSRF-PC's "
                       "own saves aren't touched.", when, sel->name[0] ? " (\"" : "", sel->name,
                       sel->name[0] ? "\")" : "");
        ImGui::OpenPopup("Copy this backup to a slot");
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename")) {
        snprintf(g.name, sizeof g.name, "%s", sel->name);
        ImGui::OpenPopup("Rename this backup");
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        g.text.clear();
        g.text.appendf("The backup from %s is deleted. JSRF-PC's own saves aren't touched.\n\nThis can't be undone.",
                       when);
        ImGui::OpenPopup("Delete this backup?");
    }
    ImGui::EndDisabled();
    if (tools_warning("Restore this backup?", g.text.c_str(), "Restore") && sel) {
        char label[120], made[JSRF_PATH_MAX], rec[JSRF_PATH_MAX], from[JSRF_STORE_NAME + 80];
        jsrf_held was = *sel;
        snprintf(label, sizeof label, "Before restoring %s", when);
        backup_what(&was, from, sizeof from);
        if (jsrf_store_load(was.dir, t->title, t->backups, label, made, sizeof made, why, sizeof why) == JSRF_WROTE) {
            origin_file(t, rec, sizeof rec);
            jsrf_origin_note(rec, t->title, JSRF_HOW_RESTORED, from, was.origin.first);
            status(SEC_BACKUPS, TOOLS_OK, "Restored.%s", made[0] ? " The saves from before are the newest backup." : "");
        } else {
            status(SEC_BACKUPS, TOOLS_ERROR, "%s", why);
        }
        reread(t, 1);
    }
    if (slot_prompt("Copy this backup to a slot", g.text.c_str(), "Copy", &g.pick_slot, g.name, sizeof g.name) && sel) {
        char slots[JSRF_PATH_MAX], from[JSRF_PATH_MAX];
        jsrf_origin o;
        memset(&o, 0, sizeof o);
        snprintf(o.how, sizeof o.how, "%s", JSRF_HOW_COPIED);
        backup_what(sel, o.what, sizeof o.what);
        jsrf_store_now(o.at);
        snprintf(o.first, sizeof o.first, "%s", sel->origin.first);
        slots_dir(t, slots, sizeof slots);
        js_path(from, sizeof from, sel->dir, JSRF_TITLE_ID);
        if (jsrf_store_save_slot(from, slots, g.pick_slot, g.name, &o, why, sizeof why) == JSRF_WROTE) {
            status(SEC_BACKUPS, TOOLS_OK, "Copied into slot %d.", g.pick_slot + 1);
            g.slot_sel = g.pick_slot;
        } else {
            status(SEC_BACKUPS, TOOLS_ERROR, "%s", why);
        }
        reread(t, 0);
    }
    (void)id;
    if (tools_name_prompt("Rename this backup", NULL, "Rename", g.name, sizeof g.name) && sel) {
        if (jsrf_store_rename(sel->dir, g.name) == 0) status(SEC_BACKUPS, TOOLS_OK, "Renamed.");
        else status(SEC_BACKUPS, TOOLS_ERROR, "The backup couldn't be renamed.");
        reread(t, 0);
    }
    if (tools_warning("Delete this backup?", g.text.c_str(), "Delete") && sel) {
        if (jsrf_store_delete(sel->dir) == 0) status(SEC_BACKUPS, TOOLS_OK, "Deleted.");
        else status(SEC_BACKUPS, TOOLS_ERROR, "The backup couldn't be deleted (is a file in it open?).");
        g.backup_sel_dir[0] = 0;
        reread(t, 0);
    }
    show_status(SEC_BACKUPS);
    ImGui::PopID();
}

/* ---- the review: an import or export ----------------------------------------------------- */

static void forget(void)
{
    if (g.planned) jsrf_plan_free(&g.plan);
    if (g.have) jsrf_found_free(&g.found);
    g.have = g.planned = g.ran = g.reviewing = g.into_slot = 0;
    g.from[0] = g.to[0] = 0;
}

static void replan(void)
{
    if (g.planned) jsrf_plan_free(&g.plan);
    g.planned = 0;
    if (!g.have || !g.to[0]) return;
    for (int i = g.found.nsaves; i < (int)(sizeof g.chosen / sizeof g.chosen[0]); i++) g.chosen[i] = 0;
    g.planned = jsrf_plan_make(&g.found, g.chosen, g.to, &g.plan) == 0;
}

/* The saves read (r 0), shown for the player to choose from. */
static void review(int r, const char *source_name)
{
    if (r != 0) {
        status(SEC_EMUS, TOOLS_WARN, "%s", g.found.why);
        jsrf_found_free(&g.found);
        g.have = 0;
        return;
    }
    g.have = 1;
    g.ran = 0;
    g.reviewing = 1;
    g.st[0] = 0;
    snprintf(g.source_name, sizeof g.source_name, "%s", source_name);
    for (int i = 0; i < g.found.nsaves && i < (int)(sizeof g.chosen / sizeof g.chosen[0]); i++)
        g.chosen[i] = g.found.saves[i].kind != JSRF_KIND_OTHER;
    replan();
}

static void import_from(tools_ctx *t, const char *path, const char *source_name)
{
    forget();
    g.mode = MODE_IMPORT;
    snprintf(g.from, sizeof g.from, "%s", path);
    snprintf(g.to, sizeof g.to, "%s", t->title);
    snprintf(g.backups_to, sizeof g.backups_to, "%s", t->backups);
    memcpy(g.key, jsrf_signature_key, sizeof g.key);
    review(jsrf_read_source(path, &g.found), source_name);
}

static void pick_one(void *ctx, const char *path)
{
    char *out = (char *)ctx;
    if (!out[0]) snprintf(out, JSRF_PATH_MAX, "%s", path);
}

static void no_dialog(void)
{
    status(SEC_EMUS, TOOLS_WARN, "There's no folder dialog here (zenity or kdialog): drop the folder on this window "
                                 "instead.");
}

/* Where an emulator is, picked with the system's dialog, opening where it
 * is now or usually is; remembered. 1 picked. */
static int pick_emu(tools_ctx *t, int source)
{
    char picked[JSRF_PATH_MAX] = "", q[200];
    emu_row *e = &g.emus[source];
    int r;
    if (!e->where[0]) find_emu(t, source);
    snprintf(q, sizeof q, "Where is %s? Pick its folder (the one with %s in it)", SOURCE_NAMES[source],
             source == JSRF_SRC_CXBX ? "EmuDisk" : "hdd");
    r = jsrf_pick_folder_in(q, e->where, picked, sizeof picked);
    if (r == JSRF_NO_DIALOG) { no_dialog(); return 0; }
    if (r != JSRF_PICKED) return 0;
    tools_set_setting(t, SOURCE_KEYS[source], picked);
    read_emu(t, source);
    return 1;
}

static void import_emu(tools_ctx *t, int source)
{
    emu_row *e = &g.emus[source];
    if (!e->where[0] && !pick_emu(t, source)) return;
    if (!e->ok) read_emu(t, source);
    if (!e->ok) {
        status(SEC_EMUS, TOOLS_WARN, "%s: %s Pick where it is with Change...", SOURCE_NAMES[source], e->what);
        return;
    }
    import_from(t, e->where, SOURCE_NAMES[source]);
}

static void export_emu(tools_ctx *t, int target)
{
    char title[JSRF_PATH_MAX], why[400], game[JSRF_PATH_MAX];
    static uint8_t head[JSRF_XBE_HEAD];
    size_t got = 0;
    emu_row *e = &g.emus[target];
    if (!e->where[0] && !pick_emu(t, target)) return;
    if (!jsrf_export_title(target, e->where, title, sizeof title)) {
        status(SEC_EMUS, TOOLS_WARN, "That isn't %s's folder: it has no %s\\Partition1. Pick where it is with Change...",
               SOURCE_NAMES[target], target == JSRF_SRC_CXBX ? "EmuDisk" : "hdd");
        return;
    }
    if (target == JSRF_SRC_CXBX) {
        /* Cxbx's key, from the player's own default.xbe: read now, kept only
         * while the page is open. */
        snprintf(game, sizeof game, "%s", t->game_files);
        if (!game[0] || jsrf_read_xbe_head(game, head, &got, why, sizeof why) != 0) {
            static const char *const pats[] = { "default.xbe", "*.iso", "*.xiso", NULL };
            game[0] = 0;
            if (jsrf_pick_files_in("Where is the game? Pick its default.xbe or its disc image", t->game_files,
                                   "The game", pats, 0, pick_one, game) != JSRF_PICKED) {
                status(SEC_EMUS, TOOLS_WARN, "Exporting to Cxbx-Reloaded needs the game's default.xbe, to sign the "
                                             "saves as Cxbx does.");
                return;
            }
            if (game[0] && js_is_file(game) && strstr(game, ".xbe")) {
                FILE *f = js_fopen(game, "rb");
                got = f ? fread(head, 1, sizeof head, f) : 0;
                if (f) fclose(f);
            } else if (jsrf_read_xbe_head(game, head, &got, why, sizeof why) != 0) {
                status(SEC_EMUS, TOOLS_WARN, "%s", why);
                return;
            }
        }
    }
    forget();
    g.mode = MODE_EXPORT;
    if (target == JSRF_SRC_CXBX) {
        if (jsrf_cxbx_key(head, got, g.key) != 0) {
            status(SEC_EMUS, TOOLS_WARN, "That default.xbe isn't Jet Set Radio Future's.");
            return;
        }
    } else {
        memcpy(g.key, jsrf_signature_key, sizeof g.key);
    }
    snprintf(g.to, sizeof g.to, "%s", title);
    js_path(g.backups_to, sizeof g.backups_to, t->backups, SOURCE_NAMES[target]);
    snprintf(g.from, sizeof g.from, "%s", t->title);
    review(jsrf_read_title_dir(t->title, &g.found), SOURCE_NAMES[target]);
}

static const char *kind_name(int kind)
{
    return kind == JSRF_KIND_GAME ? "Game save" : kind == JSRF_KIND_GRAFFITI ? "Design" : "Not a save";
}

static const char *action_name(int action)
{
    switch (action) {
    case JSRF_DO_WRITE: return "Copied in";
    case JSRF_DO_REPLACE: return "Replaces the one there";
    case JSRF_DO_RESLOT: return "Into a free spot";
    default: return "Skipped";
    }
}

/* The saves ticked that can be written. */
static int ticked_saves(void)
{
    int n = 0;
    for (int i = 0; i < g.found.nsaves && i < (int)(sizeof g.chosen / sizeof g.chosen[0]); i++)
        n += g.chosen[i] && g.found.saves[i].kind != JSRF_KIND_OTHER;
    return n;
}

/* Whether the run wrote a game save. */
static int wrote_game(void)
{
    for (int k = 0; k < g.plan.n; k++) {
        const jsrf_plan_item *it = &g.plan.items[k];
        if (g.found.saves[it->save].kind == JSRF_KIND_GAME && it->action != JSRF_DO_SKIP &&
            (it->result == JSRF_WROTE || it->result == JSRF_REPLACED)) return 1;
    }
    return 0;
}

/* The ticked saves imported into slot i (0 to 7), named name, in place of
 * what it holds; only once every one is written. */
static void import_into_slot(tools_ctx *t, int i, const char *name)
{
    char slots[JSRF_PATH_MAX], into[JSRF_PATH_MAX], why[600];
    jsrf_origin o;
    slots_dir(t, slots, sizeof slots);
    if (jsrf_store_slot_begin(slots, i, into, sizeof into, why, sizeof why) != 0) {
        status(SEC_EMUS, TOOLS_ERROR, "%s", why);
        return;
    }
    snprintf(g.to, sizeof g.to, "%s", into);
    g.into_slot = i + 1;
    replan();
    if (!g.planned || !g.plan.writes) {
        jsrf_store_slot_drop(slots, i);
        status(SEC_EMUS, TOOLS_WARN, "Nothing ticked can go into a slot.");
        g.into_slot = 0;
        return;
    }
    g.ran = 1;
    if (jsrf_plan_run(&g.found, &g.plan, g.to, "", g.key) != 0) {
        jsrf_store_slot_drop(slots, i);
        status(SEC_EMUS, TOOLS_ERROR, "Not every save could be written, so slot %d is as it was: the list says what.",
               i + 1);
        return;
    }
    memset(&o, 0, sizeof o);
    snprintf(o.how, sizeof o.how, "%s", JSRF_HOW_IMPORTED);
    snprintf(o.what, sizeof o.what, "%s", g.source_name);
    jsrf_store_now(o.at);
    if (wrote_game()) snprintf(o.first, sizeof o.first, "%s", g.source_name);
    if (jsrf_store_slot_finish(slots, i, name, &o, why, sizeof why) != JSRF_WROTE) {
        status(SEC_EMUS, TOOLS_ERROR, "%s", why);
        return;
    }
    g.slot_sel = i;
    status(SEC_EMUS, TOOLS_OK, "Imported into slot %d. Load it from Save slots to play it.", i + 1);
}

static void draw_review(tools_ctx *t)
{
    float em = ImGui::GetFontSize();
    char head[120], go[60];
    ImGui::PushID("review");
    if (g.mode == MODE_IMPORT) snprintf(head, sizeof head, "Import from %s", g.source_name);
    else snprintf(head, sizeof head, "Export to %s", g.source_name);
    tools_heading(head);
    tools_hint(g.mode == MODE_IMPORT ? "Tick the saves to bring in, to JSRF-PC's saves or a slot. Each is re-signed for JSRF-PC."
                                     : "Tick the saves to send. Each is signed so the emulator loads it.");
    {
        char from[80], to[80];
        snprintf(from, sizeof from, "From %s:", g.mode == MODE_IMPORT ? g.source_name : "JSRF-PC");
        snprintf(to, sizeof to, "To %s:", g.mode == MODE_IMPORT ? "JSRF-PC" : g.source_name);
        tools_path(from, g.from);
        if (g.into_slot) ImGui::Text("To slot %d.", g.into_slot);
        else tools_path(to, g.to);
    }
    {
        /* The table takes what the window has left, above the button row. */
        float below = ImGui::GetFrameHeightWithSpacing() * 2 + em * (g.ran ? 4 : 1);
        ImVec2 size(0, ImGui::GetContentRegionAvail().y - below);
        if (size.y < em * 5) size.y = em * 5;
        if (ImGui::BeginTable("review", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                               ImGuiTableFlags_SizingStretchProp, size)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn("What happens", ImGuiTableColumnFlags_WidthStretch, 4);
            ImGui::TableHeadersRow();
            for (int i = 0; i < g.found.nsaves; i++) {
                const jsrf_save *s = &g.found.saves[i];
                const jsrf_plan_item *it = NULL;
                char label[600];
                bool on = g.chosen[i] != 0;
                if (g.planned)
                    for (int k = 0; k < g.plan.n; k++) if (g.plan.items[k].save == i) it = &g.plan.items[k];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                snprintf(label, sizeof label, "%s##%d", s->name[0] ? s->name : s->from, i);
                ImGui::BeginDisabled(s->kind == JSRF_KIND_OTHER || g.ran);
                if (ImGui::Checkbox(label, &on)) { g.chosen[i] = on; replan(); }
                ImGui::EndDisabled();
                if (s->kind != JSRF_KIND_OTHER)
                    ImGui::SetItemTooltip("%s", s->signed_here ? "Signed for JSRF-PC already" : "Signed with another key");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kind_name(s->kind));
                ImGui::TableNextColumn();
                if (it) {
                    if (g.ran || (it->note[0] && it->action != JSRF_DO_SKIP)) ImGui::TextWrapped("%s", it->note);
                    else if (it->note[0]) ImGui::TextWrapped("%s: %s", action_name(it->action), it->note);
                    else ImGui::TextWrapped("%s", action_name(it->action));
                } else if (s->kind == JSRF_KIND_OTHER) {
                    ImGui::TextWrapped("%s", s->why);
                } else {
                    ImGui::TextDisabled("Not ticked");
                }
            }
            ImGui::EndTable();
        }
    }
    if (g.ran) {
        show_status(SEC_EMUS);
        if (g.plan.backup[0]) tools_path("Backup:", g.plan.backup);
        if (g.plan.why[0]) tools_status(TOOLS_ERROR, "%s", g.plan.why);
        if (ImGui::Button("Done")) { forget(); g.st[0] = 0; reread(t, 1); }
        ImGui::PopID();
        return;
    }
    snprintf(go, sizeof go, "%s %d save%s###go", g.mode == MODE_IMPORT ? "Import" : "Export",
             g.planned ? g.plan.writes : 0, g.planned && g.plan.writes == 1 ? "" : "s");
    ImGui::BeginDisabled(!g.planned || !g.plan.writes);
    if (ImGui::Button(go)) {
        g.text.clear();
        g.text.appendf("%d save%s %s written into %s's saves.", g.plan.writes, g.plan.writes == 1 ? "" : "s",
                       g.plan.writes == 1 ? "is" : "are", g.mode == MODE_IMPORT ? "JSRF-PC" : g.source_name);
        if (g.plan.replaces) {
            int k, first = 1;
            g.text.appendf(" %s in place of the one%s there now: ", g.plan.replaces == 1 ? "One goes" : "These go",
                           g.plan.replaces == 1 ? "" : "s");
            for (k = 0; k < g.plan.n; k++)
                if (g.plan.items[k].action == JSRF_DO_REPLACE) {
                    g.text.appendf("%s%s", first ? "" : ", ", g.found.saves[g.plan.items[k].save].name);
                    first = 0;
                }
            g.text.appendf(".");
        }
        if (g.mode == MODE_IMPORT)
            g.text.appendf("\n\nJSRF-PC's saves are backed up first; the backup shows under Backups.\n\n"
                           "Close JSRF-PC before going on.");
        else
            g.text.appendf("\n\n%s's saves are backed up first, into a folder named after it under Save "
                           "backups.\n\nClose JSRF-PC and %s before going on.", g.source_name, g.source_name);
        ImGui::OpenPopup(g.mode == MODE_IMPORT ? "Import these saves?###run" : "Export these saves?###run");
    }
    ImGui::EndDisabled();
    if (g.mode == MODE_IMPORT) {
        ImGui::SameLine();
        ImGui::BeginDisabled(!ticked_saves());
        if (ImGui::Button("Import into a slot...")) {
            g.pick_slot = slot_to_offer();
            snprintf(g.name, sizeof g.name, "From %s", g.source_name);
            ImGui::OpenPopup("Import into a slot###runslot");
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back")) { forget(); reread(t, 1); ImGui::PopID(); return; }
    {
        char text[300];
        int n = ticked_saves();
        snprintf(text, sizeof text, "The %d ticked save%s go%s into the slot you pick, re-signed for JSRF-PC, "
                 "ready to load. JSRF-PC's own saves aren't touched.", n, n == 1 ? "" : "s", n == 1 ? "es" : "");
        if (slot_prompt("Import into a slot###runslot", text, "Import", &g.pick_slot, g.name, sizeof g.name))
            import_into_slot(t, g.pick_slot, g.name);
    }
    if (tools_warning(g.mode == MODE_IMPORT ? "Import these saves?###run" : "Export these saves?###run",
                      g.text.c_str(), g.mode == MODE_IMPORT ? "Import" : "Export")) {
        int r = jsrf_plan_run(&g.found, &g.plan, g.to, g.backups_to, g.key);
        g.ran = 1;
        if (g.mode == MODE_IMPORT && g.plan.backup[0]) {
            char label[100];
            snprintf(label, sizeof label, "Before importing from %s", g.source_name);
            jsrf_store_rename(g.plan.backup, label);
        }
        if (g.mode == MODE_IMPORT && wrote_game()) {
            char rec[JSRF_PATH_MAX];
            origin_file(t, rec, sizeof rec);
            jsrf_origin_note(rec, t->title, JSRF_HOW_IMPORTED, g.source_name, g.source_name);
        }
        if (r == 0) status(SEC_EMUS, TOOLS_OK, "%s.", g.mode == MODE_IMPORT ? "Imported" : "Exported");
        else status(SEC_EMUS, TOOLS_ERROR, "Not everything was written: the list says what.");
    }
    ImGui::PopID();
}

/* ---- other emulators -------------------------------------------------------------------- */

static void draw_emus(tools_ctx *t)
{
    bool show = g.show_emus != 0;
    int sel = g.emu_sel;
    emu_row *e = sel ? &g.emus[sel] : NULL;
    ImGui::PushID("emus");
    tools_heading("Other emulators");
    tools_hint("Bring saves in from Cxbx-Reloaded or xbased, into JSRF-PC's saves or a slot, or send JSRF-PC's to them.");
    tools_help_marker("Import shows the saves there to tick, then puts them in JSRF-PC's saves or, with Import into "
                      "a slot..., in a save slot. Saves brought in are re-signed for JSRF-PC, and a design whose "
                      "spot in the garage is taken goes to a free one. Sending saves to Cxbx-Reloaded needs the "
                      "game's default.xbe, to sign them as Cxbx does. A real Xbox signs saves with the console's own "
                      "keys, which JSRF-PC Tools never asks for, so it can't send saves to one.");
    if (ImGui::Checkbox("Show their folders and saves here", &show)) {
        g.show_emus = show;
        tools_set_setting(t, "show_emulators", show ? "1" : "0");
        if (show) for (int i = 0; i < NEMUS; i++) read_emu(t, EMU_ORDER[i]);
    }
    if (g.show_emus) {
        if (ImGui::BeginTable("emus", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Emulator", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::CalcTextSize("Cxbx-Reloaded").x + ImGui::GetStyle().CellPadding.x * 2);
            ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch, 2.4f);
            ImGui::TableSetupColumn("Saves there", ImGuiTableColumnFlags_WidthStretch, 3.4f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < NEMUS; i++) {
                int s = EMU_ORDER[i];
                emu_row *r = &g.emus[s];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(s);
                if (ImGui::Selectable(SOURCE_NAMES[s], g.emu_sel == s, ImGuiSelectableFlags_SpanAllColumns))
                    g.emu_sel = g.emu_sel == s ? 0 : s;
                ImGui::PopID();
                ImGui::TableNextColumn();
                tools_path("", r->where);
                ImGui::TableNextColumn();
                cell_text(r->what, !r->ok);
                if (!r->ok && r->what[0]) ImGui::SetItemTooltip("%s", r->what);
            }
            ImGui::EndTable();
        }
        sel = g.emu_sel;
        e = sel ? &g.emus[sel] : NULL;
        ImGui::BeginDisabled(!e);
        if (ImGui::Button("Import")) import_emu(t, sel);
        ImGui::SameLine();
        if (ImGui::Button("Export")) export_emu(t, sel);
        ImGui::SameLine();
        if (ImGui::Button(e && e->where[0] ? "Change...###where" : "Pick...###where")) pick_emu(t, sel);
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (tools_flow_button("Import a folder...", !g.show_emus)) {
        char picked[JSRF_PATH_MAX] = "";
        int r = jsrf_pick_folder("Pick a folder of JSRF saves (5345000a, or a folder above it)", picked, sizeof picked);
        if (r == JSRF_PICKED) import_from(t, picked, "a folder");
        if (r == JSRF_NO_DIALOG) no_dialog();
    }
    show_status(SEC_EMUS);
    ImGui::PopID();
}

/* ---- the page ------------------------------------------------------------------------------ */

static void draw(tools_ctx *t)
{
    int frame = ImGui::GetFrameCount();
    if (!g.inited) {
        char v[8];
        g.inited = 1;
        g.drawn_frame = -2;
        g.slot_sel = g.backup_sel = -1;
        g.show_emus = !tools_setting(t, "show_emulators", v, sizeof v) || v[0] != '0';
    }
    if (frame - g.drawn_frame > 1) reread(t, 1);                 /* just shown */
    else if (ImGui::GetTime() - g.read_at > REREAD_S && !g.reviewing) reread(t, 0);
    g.drawn_frame = frame;
    if (g.reviewing) {
        draw_review(t);
        return;
    }
    draw_mine(t);
    draw_slots(t);
    draw_backups(t);
    draw_emus(t);
}

static void dropped(tools_ctx *t, const char *path)
{
    if (g.reviewing && g.ran) return;
    if (!js_is_dir(path)) {
        status(SEC_EMUS, TOOLS_WARN, "Drop the folder the saves are in, not a file.");
        return;
    }
    import_from(t, path, "a folder");
}

extern const tools_page tools_saves_page = { "Save manager", draw, dropped };
