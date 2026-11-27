/*
 * tests/save_store: JSRF-PC Tools' save manager (launcher/tools/
 * save_store.c), as run.py says. No game data: the saves here are made up.
 */
#include "save_store.h"
#include "setup.h"
#include "setup_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            failures++;                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
        }                                                             \
    } while (0)

static const char *g_dir;

static void path(char *out, const char *a, const char *b) { js_path(out, 4096, a, b); }

static void put_file(const char *dir, const char *name, const void *data, size_t n)
{
    char p[4096];
    js_mkdirs(dir);
    path(p, dir, name);
    CHECK(js_write_file(p, data, n) == 0, "%s couldn't be written", p);
}

static int file_is(const char *dir, const char *name, const void *data, size_t n)
{
    char p[4096];
    unsigned char *got;
    size_t size;
    int same;
    path(p, dir, name);
    if (js_read_file(p, 1u << 24, &got, &size) != 0) return 0;
    same = size == n && (!n || !memcmp(got, data, n));
    free(got);
    return same;
}

static int count_entries(void *ctx, const char *name, int is_dir)
{
    (void)name; (void)is_dir;
    ++*(int *)ctx;
    return 0;
}

static int entries(const char *dir)
{
    int n = 0;
    if (js_list_dir(dir, count_entries, &n) != 0) return -1;
    return n;
}

static int same_tree(const char *a, const char *b);

typedef struct tree_cmp { const char *a, *b; int same; } tree_cmp;

static int cmp_entry(void *ctx, const char *name, int is_dir)
{
    tree_cmp *t = (tree_cmp *)ctx;
    char pa[4096], pb[4096];
    path(pa, t->a, name);
    path(pb, t->b, name);
    if (is_dir) t->same &= same_tree(pa, pb);
    else {
        unsigned char *d;
        size_t n;
        if (js_read_file(pa, 1u << 24, &d, &n) != 0) { t->same = 0; return 0; }
        t->same &= file_is(t->b, name, d, n);
        free(d);
    }
    return 0;
}

static int same_tree(const char *a, const char *b)
{
    tree_cmp t;
    t.a = a;
    t.b = b;
    t.same = entries(a) == entries(b);
    js_list_dir(a, cmp_entry, &t);
    return t.same;
}

static int there(const char *p) { return js_is_dir(p) || js_is_file(p); }

/* A title folder as the game leaves one: the game save, designs, a save
 * that is neither, and the title's own file. byte marks the game save. */
#define DESIGN_A "85BE4CFBC301"
#define DESIGN_B "85BE4CFBC302"
#define GAME_DIR "99271B32E8BB"
static void made_up_title(const char *title, int byte, int designs)
{
    char p[4096], game[64];
    memset(game, byte, sizeof game);
    path(p, title, GAME_DIR);
    put_file(p, "SaveMeta.xbx", "meta", 4);
    put_file(p, "jsrfdata.sav", game, sizeof game);     /* any case, as FATX */
    if (designs > 0) { path(p, title, DESIGN_A); put_file(p, "save.dat", "a", 1); put_file(p, "SaveMeta.xbx", "m", 1); }
    if (designs > 1) { path(p, title, DESIGN_B); put_file(p, "SAVE.DAT", "b", 1); }
    path(p, title, "0123456789AB");
    put_file(p, "SaveMeta.xbx", "other", 5);
    put_file(title, "TitleMeta.xbx", "title", 5);
}

static void test_words(void)
{
    char root[4096], title[4096], text[100];
    jsrf_held_count c;
    path(root, g_dir, "words");
    path(title, root, JSRF_TITLE_ID);
    jsrf_store_count(title, &c);
    CHECK(!c.game && !c.designs && !c.others, "a title folder that isn't there holds something");
    jsrf_store_count_text(&c, text, sizeof text);
    CHECK(!strcmp(text, "Nothing"), "nothing in words is \"%s\"", text);
    made_up_title(title, 1, 2);
    jsrf_store_count(title, &c);
    CHECK(c.game == 1 && c.designs == 2 && c.others == 1, "counted %d %d %d, not 1 2 1", c.game, c.designs, c.others);
    jsrf_store_count_text(&c, text, sizeof text);
    CHECK(!strcmp(text, "Game save, 2 designs"), "in words: \"%s\"", text);
    c.game = 0;
    c.designs = 1;
    jsrf_store_count_text(&c, text, sizeof text);
    CHECK(!strcmp(text, "1 design"), "in words: \"%s\"", text);

    {
        char game[4096], when[32], now[32];
        CHECK(jsrf_store_game_file(title, game, sizeof game) && strstr(game, GAME_DIR) && strstr(game, "jsrfdata.sav"),
              "the game save's file is \"%s\"", game);
        CHECK(jsrf_store_file_when(game, when) && strlen(when) == 19, "its date is \"%s\"", when);
        jsrf_store_now(now);
        CHECK(strncmp(when, now, 10) == 0, "it was written on %s, not %s", when, now);
        path(game, root, "none");
        CHECK(!jsrf_store_file_when(game, when) && !when[0], "a file that isn't there has a date");
    }
    jsrf_store_when_text("2026-11-27 09.41.12", text, sizeof text);
    CHECK(!strcmp(text, "27 Nov 2026, 9:41am"), "the date in words: \"%s\"", text);
    jsrf_store_when_text("2026-01-03 00.05.00", text, sizeof text);
    CHECK(!strcmp(text, "3 Jan 2026, 12:05am"), "midnight in words: \"%s\"", text);
    jsrf_store_when_text("2026-07-04 12.00.59 (2)", text, sizeof text);
    CHECK(!strcmp(text, "4 Jul 2026, 12:00pm"), "noon in words: \"%s\"", text);
    jsrf_store_when_text("2026-07-04 13.30.00", text, sizeof text);
    CHECK(!strcmp(text, "4 Jul 2026, 1:30pm"), "the afternoon in words: \"%s\"", text);
    jsrf_store_when_text("Cxbx-Reloaded", text, sizeof text);
    CHECK(!strcmp(text, "Cxbx-Reloaded"), "a name that isn't a date became \"%s\"", text);
    jsrf_store_when_text("2026-13-04 13.30.00", text, sizeof text);
    CHECK(!strcmp(text, "2026-13-04 13.30.00"), "a month that isn't one became \"%s\"", text);
}

static void test_slots(void)
{
    char root[4096], title[4096], slots[4096], backups[4096], p[4096], why[400], made[4096], name[300];
    jsrf_held s[JSRF_STORE_SLOTS];
    int i, r;
    path(root, g_dir, "slots");
    path(p, root, "UDATA");
    path(title, p, JSRF_TITLE_ID);
    path(slots, root, "Save slots");
    path(backups, root, "Save backups");

    r = jsrf_store_save_slot(title, slots, 0, "x", NULL, why, sizeof why);
    CHECK(r == JSRF_FAILED && why[0], "a slot was saved with no saves to save (%d)", r);
    jsrf_store_slots(slots, s);
    for (i = 0; i < JSRF_STORE_SLOTS; i++) CHECK(!s[i].used && s[i].dir[0], "slot %d is used, or has no folder", i + 1);

    made_up_title(title, 1, 2);
    r = jsrf_store_save_slot(title, slots, 1, "  Before chapter 3\r\nfor real\t", NULL, why, sizeof why);
    CHECK(r == JSRF_WROTE, "saving into slot 2 gave %d: %s", r, why);
    jsrf_store_slots(slots, s);
    CHECK(!s[0].used && s[1].used, "slot 2 isn't the one used");
    CHECK(!strcmp(s[1].name, "Before chapter 3  for real"), "slot 2's name is \"%s\"", s[1].name);
    CHECK(strlen(s[1].when) == 19, "slot 2 has no date: \"%s\"", s[1].when);
    CHECK(s[1].count.game == 1 && s[1].count.designs == 2, "slot 2 holds %d, %d", s[1].count.game, s[1].count.designs);
    path(p, s[1].dir, JSRF_TITLE_ID);
    CHECK(same_tree(title, p), "slot 2 isn't a copy of the saves");
    CHECK(entries(slots) == 1, "the slots folder holds %d, not 1 (a .new or .old left?)", entries(slots));

    /* Saved again over itself, after the saves changed; a .new left by a
     * run that stopped is cleaned up. */
    path(p, slots, "2.new");
    put_file(p, "junk", "j", 1);
    made_up_title(title, 2, 1);
    r = jsrf_store_save_slot(title, slots, 1, "Again", NULL, why, sizeof why);
    CHECK(r == JSRF_WROTE, "saving over slot 2 gave %d: %s", r, why);
    jsrf_store_slots(slots, s);
    path(p, s[1].dir, JSRF_TITLE_ID);
    CHECK(same_tree(title, p) && !strcmp(s[1].name, "Again"), "slot 2 wasn't replaced whole (\"%s\")", s[1].name);
    CHECK(entries(slots) == 1, "the slots folder holds %d, not 1", entries(slots));

    /* A swap stopped between its renames: the .old is put back. */
    path(p, slots, "5.old");
    {
        char t[4096];
        path(t, p, JSRF_TITLE_ID);
        made_up_title(t, 5, 0);
    }
    r = jsrf_store_save_slot(title, slots, 4, "Five", NULL, why, sizeof why);
    CHECK(r == JSRF_WROTE && !there(p), "slot 5 gave %d, its .old %s", r, there(p) ? "left" : "gone");

    /* Renamed: the date stays; the name is kept to its room, cut at a whole
     * character. */
    {
        char when[32];
        snprintf(when, sizeof when, "%s", s[1].when);
        CHECK(jsrf_store_rename(s[1].dir, "Chapter 3 \xc3\xa9") == 0, "slot 2 couldn't be renamed");
        jsrf_store_slots(slots, s);
        CHECK(!strcmp(s[1].name, "Chapter 3 \xc3\xa9") && !strcmp(s[1].when, when),
              "slot 2 renamed is \"%s\" from %s", s[1].name, s[1].when);
        for (i = 0; i < 150; i++) { name[2 * i] = (char)0xc3; name[2 * i + 1] = (char)0xa9; }
        name[300 - 2] = 0;
        jsrf_store_rename(s[1].dir, name);
        jsrf_store_slots(slots, s);
        CHECK(strlen(s[1].name) == JSRF_STORE_NAME - 2 && (unsigned char)s[1].name[strlen(s[1].name) - 1] == 0xa9,
              "a long name was cut to %u bytes", (unsigned)strlen(s[1].name));
        jsrf_store_rename(s[1].dir, "");
        jsrf_store_slots(slots, s);
        CHECK(!s[1].name[0] && !strcmp(s[1].when, when), "the name wasn't taken away (\"%s\")", s[1].name);
    }

    /* Loaded back: JSRF-PC's saves backed up first, under the name given,
     * then replaced whole. */
    path(p, slots, "2");
    {
        char before[4096], kept[4096], src[4096];
        jsrf_held *b = NULL;
        int nb;
        made_up_title(title, 9, 2);     /* what's there now differs from the slot */
        path(before, root, "before");
        js_mkdirs(before);
        path(kept, before, JSRF_TITLE_ID);
        CHECK(jsrf_copy_tree(title, kept) == 0, "the saves couldn't be copied for the test");
        r = jsrf_store_load(p, title, backups, "Before loading slot 2", made, sizeof made, why, sizeof why);
        CHECK(r == JSRF_WROTE && made[0], "loading slot 2 gave %d: %s", r, why);
        path(src, p, JSRF_TITLE_ID);
        CHECK(same_tree(src, title), "JSRF-PC's saves aren't slot 2's");
        nb = jsrf_store_backups(backups, &b);
        CHECK(nb == 1 && !strcmp(b[0].name, "Before loading slot 2"), "%d backups, the first named \"%s\"", nb,
              nb > 0 ? b[0].name : "");
        path(src, made, JSRF_TITLE_ID);
        CHECK(same_tree(kept, src), "the backup isn't the saves from before the load");
        path(src, root, "UDATA");
        CHECK(entries(src) == 1, "UDATA holds %d, not 1 (a .new or .old left?)", entries(src));
        free(b);
    }

    /* An empty slot loads nothing, and leaves the saves as they are. */
    path(p, slots, "3");
    r = jsrf_store_load(p, title, backups, "x", made, sizeof made, why, sizeof why);
    CHECK(r == JSRF_FAILED && why[0], "an empty slot loaded (%d)", r);
    path(p, slots, "2");
    {
        char src[4096];
        path(src, p, JSRF_TITLE_ID);
        CHECK(same_tree(src, title), "a failed load changed the saves");
    }

    /* Into a JSRF-PC that has never saved: nothing to back up. */
    js_rmtree(title);
    path(p, root, "UDATA");
    js_rmtree(p);
    path(p, slots, "2");
    r = jsrf_store_load(p, title, backups, "x", made, sizeof made, why, sizeof why);
    CHECK(r == JSRF_WROTE && !made[0] && js_is_dir(title), "loading into no saves gave %d, backup \"%s\"", r, made);

    /* Cleared. Only a slot's or backup's own folder is deleted. */
    CHECK(jsrf_store_delete(p) == 0 && !there(p), "slot 2 wasn't cleared");
    path(p, root, "not ours");
    put_file(p, "keep", "k", 1);
    CHECK(jsrf_store_delete(p) == -1 && there(p), "a folder that isn't a slot was deleted");
}

static void test_backups(void)
{
    char root[4096], title[4096], backups[4096], p[4096], why[400], made1[4096], made2[4096];
    jsrf_held *b = NULL;
    int n, r;
    path(root, g_dir, "backups");
    path(title, root, JSRF_TITLE_ID);
    path(backups, root, "Save backups");
    n = jsrf_store_backups(backups, &b);
    CHECK(n == 0, "%d backups where there's no folder", n);
    free(b);
    made_up_title(title, 1, 1);
    CHECK(jsrf_backup(title, backups, made1, sizeof made1, why, sizeof why) == JSRF_WROTE, "%s", why);
    made_up_title(title, 2, 2);
    CHECK(jsrf_backup(title, backups, made2, sizeof made2, why, sizeof why) == JSRF_WROTE, "%s", why);
    /* An export's backups of an emulator's saves, and a stray file: not
     * listed. */
    path(p, backups, "Cxbx-Reloaded/2026-11-27 09.41.12");
    {
        char t[4096];
        path(t, p, JSRF_TITLE_ID);
        made_up_title(t, 3, 0);
    }
    put_file(backups, "notes.txt", "n", 1);
    n = jsrf_store_backups(backups, &b);
    CHECK(n == 2, "%d backups listed, not 2", n);
    if (n == 2) {
        CHECK(!strcmp(b[0].dir, made2) && !strcmp(b[1].dir, made1), "not newest first: %s, then %s", b[0].dir, b[1].dir);
        CHECK(strlen(b[0].when) == 19 && b[0].used, "the newest has no date (\"%s\")", b[0].when);
        CHECK(b[0].count.designs == 2 && b[1].count.designs == 1, "designs counted %d, %d", b[0].count.designs,
              b[1].count.designs);
    }
    free(b);
    CHECK(jsrf_store_rename(made1, "Mine") == 0, "a backup couldn't be named");
    n = jsrf_store_backups(backups, &b);
    CHECK(n == 2 && !strcmp(b[1].name, "Mine") && !b[0].name[0], "the name went to the wrong one");
    free(b);

    /* Restored: the saves now backed up first, so a restore can be undone. */
    r = jsrf_store_load(made1, title, backups, "Before restoring", p, sizeof p, why, sizeof why);
    CHECK(r == JSRF_WROTE, "restoring gave %d: %s", r, why);
    {
        char src[4096];
        path(src, made1, JSRF_TITLE_ID);
        CHECK(same_tree(src, title), "the saves aren't the backup's");
    }
    n = jsrf_store_backups(backups, &b);
    CHECK(n == 3, "%d backups after a restore, not 3", n);
    free(b);
    CHECK(jsrf_store_delete(made2) == 0 && !there(made2), "a backup wasn't deleted");
}

static void test_delete_saves(void)
{
    char root[4096], title[4096], backups[4096], p[4096], why[400], made[4096];
    const char good[2][JSRF_FOLDER_CHARS + 1] = { DESIGN_A, DESIGN_B };
    const char bad[2][JSRF_FOLDER_CHARS + 1] = { DESIGN_A, "../../xyz" };
    jsrf_held *b = NULL;
    jsrf_held_count c;
    int r;
    path(root, g_dir, "delete");
    path(title, root, JSRF_TITLE_ID);
    path(backups, root, "Save backups");
    made_up_title(title, 1, 2);
    r = jsrf_store_delete_saves(title, backups, "x", bad, 2, made, sizeof made, why, sizeof why);
    CHECK(r == -1 && !there(backups), "a name that isn't a save folder's was taken (%d)", r);
    path(p, title, DESIGN_A);
    CHECK(there(p), "a design went when the list had a bad name");
    r = jsrf_store_delete_saves(title, backups, "Before deleting 2 designs", good, 2, made, sizeof made, why, sizeof why);
    CHECK(r == 2, "deleting 2 designs gave %d: %s", r, why);
    jsrf_store_count(title, &c);
    CHECK(c.game == 1 && c.designs == 0 && c.others == 1, "after: %d %d %d", c.game, c.designs, c.others);
    CHECK(jsrf_store_backups(backups, &b) == 1 && !strcmp(b[0].name, "Before deleting 2 designs") &&
              b[0].count.designs == 2, "no backup of the designs before they went");
    free(b);
    r = jsrf_store_delete_saves(title, backups, "x", good, 2, made, sizeof made, why, sizeof why);
    CHECK(r == 0, "deleting designs that aren't there gave %d", r);
}

static void test_preview(void)
{
    char root[4096], p[4096];
    uint8_t file[156 + 48 * 32 * 2], rgba[JSRF_PREVIEW_W * JSRF_PREVIEW_H * 4];
    int w = 0, h = 0, i, cls;
    path(root, g_dir, "preview");
    memset(file, 0, sizeof file);
    file[0] = 2;                        /* M: 48 across */
    file[1] = 29;
    memcpy(file + 4, "JSRF", 4);
    for (i = 0; i < 48 * 32; i++) {
        unsigned v = i == 0 ? 0xF800u : i == 1 ? 0xC618u : i == 2 ? 0x07E0u : 0x001Fu;
        file[156 + 2 * i] = (uint8_t)v;
        file[156 + 2 * i + 1] = (uint8_t)(v >> 8);
    }
    put_file(root, "save.dat", file, sizeof file);
    path(p, root, "save.dat");
    cls = jsrf_design_preview(p, rgba, &w, &h);
    CHECK(cls == 2 && w == 48 && h == 32, "the preview is class %d, %d by %d", cls, w, h);
    CHECK(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255, "red came out %d %d %d %d", rgba[0],
          rgba[1], rgba[2], rgba[3]);
    CHECK(rgba[7] == 0, "the game's clear isn't clear");
    CHECK(rgba[8] == 0 && rgba[9] == 255 && rgba[10] == 0, "green came out %d %d %d", rgba[8], rgba[9], rgba[10]);
    CHECK(rgba[4 * (48 * 32 - 1) + 2] == 255, "the last pixel isn't blue");
    put_file(root, "short.dat", file, 156 + 100);
    path(p, root, "short.dat");
    CHECK(jsrf_design_preview(p, rgba, &w, &h) == -1, "a file too short gave a preview");
    file[4] = 'X';
    put_file(root, "bad.dat", file, sizeof file);
    path(p, root, "bad.dat");
    CHECK(jsrf_design_preview(p, rgba, &w, &h) == -1, "a file that isn't a design gave a preview");
    path(p, root, "none.dat");
    CHECK(jsrf_design_preview(p, rgba, &w, &h) == -1, "no file gave a preview");
}

/* Where saves came from: JSRF-PC's own record, a backup made with it, a
 * slot saved from it or filled by an import, and the words for each. */
static void test_origins(void)
{
    char root[4096], title[4096], slots[4096], backups[4096], record[4096], p[4096], why[400], made[4096];
    char text[600], into[4096];
    jsrf_origin o;
    jsrf_held s[JSRF_STORE_SLOTS], *b = NULL;
    jsrf_held_count c;
    int r, nb;
    path(root, g_dir, "origins");
    path(p, root, "UDATA");
    path(title, p, JSRF_TITLE_ID);
    path(slots, root, "Save slots");
    path(backups, root, "Save backups");
    path(record, root, JSRF_ORIGIN_FILE);
    made_up_title(title, 3, 1);

    /* None yet. */
    CHECK(jsrf_origin_read(record, &o) == 0 && !o.how[0] && !o.first[0], "an origin was read where there's none");
    jsrf_origin_text(&o, text, sizeof text);
    CHECK(!text[0], "no origin in words is \"%s\"", text);

    /* Noted after an import: the game save's own time kept with it. */
    CHECK(jsrf_origin_note(record, title, JSRF_HOW_IMPORTED, "xbased", "xbased") == 0, "the origin wasn't written");
    CHECK(jsrf_origin_read(record, &o) == 1 && !strcmp(o.how, JSRF_HOW_IMPORTED) && !strcmp(o.what, "xbased") &&
          !strcmp(o.first, "xbased") && strlen(o.at) == 19 && strlen(o.game) == 19,
          "the origin read back is %s/%s/%s/%s/%s", o.how, o.what, o.first, o.at, o.game);
    jsrf_origin_text(&o, text, sizeof text);
    CHECK(!strncmp(text, "Imported from xbased on ", 24) && !strstr(text, "First from"),
          "an import in words is \"%s\"", text);

    /* In words, for each step; "first" said when it isn't what was just said. */
    memset(&o, 0, sizeof o);
    snprintf(o.how, sizeof o.how, "%s", JSRF_HOW_LOADED);
    snprintf(o.what, sizeof o.what, "slot 2 (\"Test\")");
    snprintf(o.at, sizeof o.at, "2026-11-27 10.59.00");
    snprintf(o.first, sizeof o.first, "xbased");
    jsrf_origin_text(&o, text, sizeof text);
    CHECK(!strcmp(text, "Loaded from slot 2 (\"Test\") on 27 Nov 2026, 10:59am. First from xbased."),
          "a load in words is \"%s\"", text);
    o.how[0] = 0;
    jsrf_origin_text(&o, text, sizeof text);
    CHECK(!strcmp(text, "First from xbased."), "only where it was first from, in words, is \"%s\"", text);
    snprintf(o.how, sizeof o.how, "made up");
    o.first[0] = 0;
    jsrf_origin_text(&o, text, sizeof text);
    CHECK(!text[0], "a step not known became \"%s\"", text);

    /* What a set holds, with where its game save came from. */
    memset(&c, 0, sizeof c);
    c.game = 1;
    c.designs = 3;
    jsrf_store_holds_text(&c, "xbased", text, sizeof text);
    CHECK(!strcmp(text, "Game save from xbased, 3 designs"), "holds in words is \"%s\"", text);
    jsrf_store_holds_text(&c, "", text, sizeof text);
    CHECK(!strcmp(text, "Game save, 3 designs"), "holds, not known from where, is \"%s\"", text);
    c.game = 0;
    jsrf_store_holds_text(&c, "xbased", text, sizeof text);
    CHECK(!strcmp(text, "3 designs"), "designs only, with a first, is \"%s\"", text);

    /* A backup takes JSRF-PC's origin with it, and keeps it when renamed. */
    jsrf_origin_note(record, title, JSRF_HOW_IMPORTED, "Cxbx-Reloaded", "Cxbx-Reloaded");
    r = jsrf_backup(title, backups, made, sizeof made, why, sizeof why);
    CHECK(r == JSRF_WROTE, "the backup gave %d: %s", r, why);
    jsrf_store_rename(made, "Before something");
    nb = jsrf_store_backups(backups, &b);
    CHECK(nb == 1 && !strcmp(b[0].name, "Before something") && !strcmp(b[0].origin.first, "Cxbx-Reloaded") &&
          !strcmp(b[0].origin.how, JSRF_HOW_IMPORTED), "the backup's origin is %s/%s (%d)",
          nb > 0 ? b[0].origin.how : "", nb > 0 ? b[0].origin.first : "", nb);
    free(b);
    b = NULL;
    /* An export's backups, a folder further down, aren't JSRF-PC's: none. */
    path(p, backups, "xbased");
    r = jsrf_backup(title, p, made, sizeof made, why, sizeof why);
    path(p, made, JSRF_STORE_ABOUT);
    CHECK(r == JSRF_WROTE && !there(p), "an export's backup was given JSRF-PC's origin");

    /* Saved into a slot with an origin: listed with it. */
    memset(&o, 0, sizeof o);
    snprintf(o.how, sizeof o.how, "%s", JSRF_HOW_SAVED);
    snprintf(o.what, sizeof o.what, "JSRF-PC's saves");
    jsrf_store_now(o.at);
    snprintf(o.first, sizeof o.first, "Cxbx-Reloaded");
    r = jsrf_store_save_slot(title, slots, 2, "Mine", &o, why, sizeof why);
    jsrf_store_slots(slots, s);
    CHECK(r == JSRF_WROTE && !strcmp(s[2].name, "Mine") && !strcmp(s[2].origin.how, JSRF_HOW_SAVED) &&
          !strcmp(s[2].origin.first, "Cxbx-Reloaded"), "slot 3's origin is %s/%s (%d: %s)", s[2].origin.how,
          s[2].origin.first, r, why);
    CHECK(jsrf_store_rename(s[2].dir, "Mine too") == 0, "slot 3 couldn't be renamed");
    jsrf_store_slots(slots, s);
    CHECK(!strcmp(s[2].name, "Mine too") && !strcmp(s[2].origin.first, "Cxbx-Reloaded"), "a rename lost the origin");

    /* A backup copied into a slot. */
    nb = jsrf_store_backups(backups, &b);
    if (nb >= 1) {
        path(p, b[0].dir, JSRF_TITLE_ID);
        r = jsrf_store_save_slot(p, slots, 3, "From the backup", &b[0].origin, why, sizeof why);
        jsrf_store_slots(slots, s);
        path(into, s[3].dir, JSRF_TITLE_ID);
        CHECK(r == JSRF_WROTE && same_tree(p, into) && !strcmp(s[3].origin.first, "Cxbx-Reloaded"),
              "the backup wasn't copied into slot 4 (%d: %s)", r, why);
    }
    free(b);
    b = NULL;
    path(p, root, "nothing here");
    CHECK(jsrf_store_save_slot(p, slots, 3, "x", NULL, why, sizeof why) == JSRF_FAILED && why[0],
          "a slot was filled from a folder that isn't there");

    /* Filled by an import: begin, write, finish; or begin, then drop, and
     * the slot is as it was. */
    r = jsrf_store_slot_begin(slots, 3, into, sizeof into, why, sizeof why);
    CHECK(r == 0 && strstr(into, "4.new") && !there(into), "begin gave %d, %s", r, into);
    made_up_title(into, 7, 0);
    memset(&o, 0, sizeof o);
    snprintf(o.how, sizeof o.how, "%s", JSRF_HOW_IMPORTED);
    snprintf(o.what, sizeof o.what, "xbased");
    snprintf(o.first, sizeof o.first, "xbased");
    r = jsrf_store_slot_finish(slots, 3, "From xbased", &o, why, sizeof why);
    jsrf_store_slots(slots, s);
    CHECK(r == JSRF_WROTE && s[3].used && !strcmp(s[3].name, "From xbased") && s[3].count.designs == 0 &&
          !strcmp(s[3].origin.first, "xbased") && strlen(s[3].when) == 19,
          "the import into slot 4 gave %d (%s), named \"%s\"", r, why, s[3].name);
    CHECK(entries(slots) == 2, "the slots folder holds %d, not 2 (a .new or .old left?)", entries(slots));
    r = jsrf_store_slot_begin(slots, 3, into, sizeof into, why, sizeof why);
    made_up_title(into, 8, 2);
    jsrf_store_slot_drop(slots, 3);
    jsrf_store_slots(slots, s);
    CHECK(r == 0 && !strcmp(s[3].name, "From xbased") && s[3].count.designs == 0 && entries(slots) == 2,
          "a dropped fill changed slot 4, or was left");
    CHECK(jsrf_store_slot_begin(slots, 8, into, sizeof into, why, sizeof why) == -1, "slot 9 was begun");
    CHECK(jsrf_store_slot_finish(slots, 3, "x", NULL, why, sizeof why) == JSRF_FAILED,
          "a fill that wasn't begun was finished");
    jsrf_store_slots(slots, s);
    CHECK(!strcmp(s[3].name, "From xbased"), "a fill that wasn't begun changed slot 4");
}

int main(int argc, char **argv)
{
    char work[4096];
    if (argc < 2) {
        printf("save_store: give a folder to work in\n");
        return 2;
    }
    path(work, argv[1], "work");
    g_dir = work;
    js_mkdirs(work);
    test_words();
    test_slots();
    test_backups();
    test_delete_saves();
    test_preview();
    test_origins();
    if (failures) {
        printf("save_store: %d failures\n", failures);
        return 1;
    }
    printf("save_store: all cases pass\n");
    return 0;
}
