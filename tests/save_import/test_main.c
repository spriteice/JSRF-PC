/*
 * tests/save_import: JSRF-PC Tools' save core (launcher/tools/jsrf_save.c)
 * and its sources (save_sources.c), as run.py says. No game data: the saves
 * here are made up, laid out as the game's are, and the known answers are
 * Python's hmac and hashlib's.
 */
#include "jsrf_save.h"
#include "save_sources.h"
#include "save_import.h"
#include "setup.h"
#include "setup_job.h"
#include "xbox_disk.h"
#include "disk_build.h"

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

static void hex(const uint8_t *b, size_t n, char *out)
{
    size_t i;
    for (i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
    out[2 * n] = 0;
}

static size_t units_of(const char *ascii, uint16_t *out)
{
    size_t n = 0;
    while (ascii[n]) { out[n] = (uint8_t)ascii[n]; n++; }
    return n;
}

/* ---- signatures ---------------------------------------------------------------- */

/* A JSRFDATA.SAV laid out as the game's: 20 bytes of signature (0xAA, so
 * not one), then "JSRF" and a pattern, 41,088 bytes in all. */
static uint8_t *made_up_game_file(void)
{
    uint8_t *f = (uint8_t *)malloc(JSRF_GAME_SIZE);
    uint32_t i;
    memset(f, 0xAA, JSRF_SIG_BYTES);
    memcpy(f + JSRF_SIG_BYTES, "JSRF", 4);
    for (i = 4; i < JSRF_GAME_SIZE - JSRF_SIG_BYTES; i++) f[JSRF_SIG_BYTES + i] = (uint8_t)(i * 7 + 3);
    return f;
}

static void test_signatures(void)
{
    static const uint8_t other_key[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    uint8_t sig[20], *f = made_up_game_file(), xbe[0x400], key[16];
    char h[64];
    size_t i;

    for (i = 0; i < 16; i++) CHECK(jsrf_signature_key[i] == 0, "the key's byte %zu isn't zero", i);
    jsrf_sign(jsrf_signature_key, "", 0, sig);
    hex(sig, 20, h);
    CHECK(!strcmp(h, "fbdb1d1b18aa6c08324b7d64b71fb76370690e1d"), "HMAC of nothing: %s", h);
    jsrf_sign(jsrf_signature_key, "abc", 3, sig);
    hex(sig, 20, h);
    CHECK(!strcmp(h, "9b4a918f398d74d3e367970aba3cbe54e4d2b5d9"), "HMAC of abc: %s", h);

    CHECK(jsrf_file_verifies(jsrf_signature_key, f, JSRF_GAME_SIZE) == 0, "an unsigned save verifies");
    CHECK(jsrf_file_resign(jsrf_signature_key, f, JSRF_GAME_SIZE) == 0, "the re-sign failed");
    hex(f, 20, h);
    CHECK(!strcmp(h, "5afb81e3fece2cdcde3d0f6b8398da77d23f5ee4"), "the re-signed save's signature: %s", h);
    CHECK(jsrf_file_verifies(jsrf_signature_key, f, JSRF_GAME_SIZE) == 1, "the re-signed save doesn't verify");
    CHECK(f[20] == 'J' && f[24] == (uint8_t)(4 * 7 + 3), "the re-sign changed the body");
    CHECK(jsrf_file_resign(other_key, f, JSRF_GAME_SIZE) == 0, "the re-sign under another key failed");
    hex(f, 20, h);
    CHECK(!strcmp(h, "173d48ca1488702ee1aff4c4ffe29359ee102341"), "under another key: %s", h);
    CHECK(jsrf_file_verifies(jsrf_signature_key, f, JSRF_GAME_SIZE) == 0, "another key's save verifies with ours");
    CHECK(jsrf_file_verifies(jsrf_signature_key, f, 19) == -1, "a file shorter than a signature verifies");
    CHECK(jsrf_file_resign(jsrf_signature_key, f, 19) == -1, "a file shorter than a signature is re-signed");
    free(f);

    /* Cxbx-Reloaded's key: a made-up XBE, its certificate 0x200 in. */
    memset(xbe, 0, sizeof xbe);
    memcpy(xbe, "XBEH", 4);
    xbe[0x104 + 2] = 0x01;                          /* base 0x10000 */
    xbe[0x118 + 0] = 0x00; xbe[0x118 + 1] = 0x02; xbe[0x118 + 2] = 0x01;   /* the certificate at 0x10200 */
    xbe[0x200 + 8] = 0x0A; xbe[0x200 + 9] = 0x00; xbe[0x200 + 10] = 0x45; xbe[0x200 + 11] = 0x53;
    for (i = 0; i < 16; i++) xbe[0x200 + 0xC0 + i] = (uint8_t)(0x10 + i);
    CHECK(jsrf_cxbx_key(xbe, sizeof xbe, key) == 0, "Cxbx's key wasn't made");
    hex(key, 16, h);
    CHECK(!strcmp(h, "d01a768e4f5dae893fd4cfc29dbf84ab"), "Cxbx's key: %s", h);
    CHECK(jsrf_cxbx_key(xbe, 0x200 + 0xCF, key) == -1, "a certificate cut short gives a key");
    xbe[0x200 + 8] = 0x0B;
    CHECK(jsrf_cxbx_key(xbe, sizeof xbe, key) == -1, "another title's XBE gives a key");
    xbe[0x200 + 8] = 0x0A;
    xbe[0x118 + 2] = 0x00;                          /* the certificate before the base */
    CHECK(jsrf_cxbx_key(xbe, sizeof xbe, key) == -1, "a certificate outside the file gives a key");
    xbe[0x118 + 2] = 0x01;
    xbe[0] = 'Y';
    CHECK(jsrf_cxbx_key(xbe, sizeof xbe, key) == -1, "a file that isn't an XBE gives a key");
}

/* ---- names ------------------------------------------------------------------------ */

static void test_names(void)
{
    static const struct { const char *name; const char *folder; } cases[] = {
        { "Test Save", "91566539302D" }, { "A", "000000000041" },
        { "GRAFFITI001", "85BE4CFBC301" }, { "GRAFFITI280", "85C04D03C300" },
    };
    uint16_t u[200], back[200];
    uint8_t meta[600];
    char folder[13], utf8[200];
    size_t i, n, m;
    int got;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        n = units_of(cases[i].name, u);
        jsrf_folder_name(u, n, folder);
        CHECK(!strcmp(folder, cases[i].folder), "%s's folder: %s, not %s", cases[i].name, folder, cases[i].folder);
    }
    jsrf_folder_name(u, 0, folder);
    CHECK(!strcmp(folder, "000000000000"), "no name's folder: %s", folder);
    for (i = 0; i < 128; i++) u[i] = 'z';
    jsrf_folder_name(u, 128, folder);
    CHECK(!strcmp(folder, "976E46158970"), "128 z's folder: %s", folder);
    for (i = 0; i < 10; i++) u[i] = 0xFFFF;
    jsrf_folder_name(u, 10, folder);
    CHECK(!strcmp(folder, "00032242FFFF"), "ten 0xFFFF's folder: %s", folder);
    {
        static const uint16_t accented[] = { 'S', 'a', 'v', 0xE9, ' ', 0x65E5, 0x672C };
        jsrf_folder_name(accented, 7, folder);
        CHECK(!strcmp(folder, "167B811C057A"), "a name past ASCII's folder: %s", folder);
        jsrf_utf16_to_utf8(accented, 7, utf8, sizeof utf8);
        CHECK(!strcmp(utf8, "Sav\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC"), "a name past ASCII in UTF-8: %s", utf8);
    }

    /* SaveMeta.xbx */
    n = units_of("Test Save", u);
    m = jsrf_meta_build(u, n, meta, sizeof meta);
    CHECK(m == 2 + 2 * (5 + 9 + 2) && meta[0] == 0xFF && meta[1] == 0xFE && meta[2] == 'N' && meta[3] == 0 &&
          meta[m - 2] == '\n', "SaveMeta.xbx built wrong (%zu bytes)", m);
    got = jsrf_meta_name(meta, m, back, 200);
    CHECK(got == 9 && !memcmp(back, u, 9 * 2), "SaveMeta.xbx's name read back as %d units", got);
    CHECK(jsrf_meta_name(meta, m, back, 8) == -1, "a name longer than its room is taken");
    CHECK(jsrf_meta_build(u, n, meta, m - 1) == 0, "SaveMeta.xbx built past its room");
    CHECK(jsrf_meta_name(meta + 2, m - 2, back, 200) == 9, "SaveMeta.xbx without its byte order mark isn't read");
    CHECK(jsrf_meta_name(meta, m + 1, back, 200) == 9, "SaveMeta.xbx with an odd byte after isn't read");
    {
        /* Another line first, LF only, and no end of line after the name. */
        static const char text[] = "Title=x\nName=Hi";
        uint8_t raw[64];
        for (i = 0; text[i]; i++) { raw[2 * i] = (uint8_t)text[i]; raw[2 * i + 1] = 0; }
        got = jsrf_meta_name(raw, 2 * i, back, 200);
        CHECK(got == 2 && back[0] == 'H' && back[1] == 'i', "the Name line after another isn't read (%d)", got);
        CHECK(jsrf_meta_name(raw, 2 * 7, back, 200) == -1, "a SaveMeta.xbx without Name gives one");
        raw[2 * 8] = 'n';                           /* name=, not Name= */
        CHECK(jsrf_meta_name(raw, 2 * i, back, 200) == -1, "name= is taken for Name=");
    }
    {
        static const uint16_t odd[] = { 'a', 0xD800, 'b', 0xD83D, 0xDE00, 7 };
        jsrf_utf16_to_utf8(odd, 6, utf8, sizeof utf8);
        CHECK(!strcmp(utf8, "a\xEF\xBF\xBD" "b\xF0\x9F\x98\x80\xEF\xBF\xBD"), "odd UTF-16 in UTF-8: %s", utf8);
        jsrf_utf16_to_utf8(odd, 6, utf8, 3);
        CHECK(!strcmp(utf8, "a"), "UTF-8 past its room: %s", utf8);
    }
}

/* ---- saves on disk ------------------------------------------------------------------- */

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

static void meta_for(const char *name, uint8_t *meta, size_t *m)
{
    uint16_t u[128];
    *m = jsrf_meta_build(u, units_of(name, u), meta, 600);
}

/* A save in memory, as a source's reading gives one. */
static void made_up_save(jsrf_save *s, const char *name, int byte)
{
    uint8_t meta[600], *f = made_up_game_file(), *m;
    size_t mn;
    memset(s, 0, sizeof *s);
    f[100] = (uint8_t)byte;
    meta_for(name, meta, &mn);
    m = (uint8_t *)malloc(mn);
    memcpy(m, meta, mn);
    jsrf_save_add(s, "SaveMeta.xbx", m, mn);
    jsrf_save_add(s, "JSRFDATA.SAV", f, JSRF_GAME_SIZE);
    jsrf_save_check(s);
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

static void test_write(void)
{
    char root[4096], title[4096], dest[4096], p[4300], why[400];
    jsrf_save a, b, junk;
    jsrf_file *fa, *fb;
    jsrf_file tf;
    int r;

    path(root, g_dir, "write");
    path(title, root, "UDATA/" JSRF_TITLE_ID);
    made_up_save(&a, "Test Save", 1);
    made_up_save(&b, "Test Save", 2);
    fa = jsrf_save_file(&a, "jsrfdata.sav");
    fb = jsrf_save_file(&b, JSRF_GAME_FILE);
    CHECK(fa && fb, "a save's file isn't found by name");
    CHECK(a.kind == JSRF_KIND_GAME && !strcmp(a.folder, "91566539302D") && !strcmp(a.name, "Test Save") &&
          !a.signed_here, "the made-up save checks as kind %d, %s, %s, signed %d", a.kind, a.folder, a.name, a.signed_here);
    CHECK(jsrf_save_resign(&a, jsrf_signature_key) == 0 && a.signed_here, "the save's re-sign failed");
    jsrf_save_check(&a);
    CHECK(a.signed_here, "the re-signed save doesn't check as signed");

    CHECK(!jsrf_save_exists(title, &a), "a save is there before any was written");
    r = jsrf_save_write(title, &a, 0, why, sizeof why);
    CHECK(r == JSRF_WROTE, "the first write gave %d: %s", r, why);
    path(dest, title, a.folder);
    CHECK(file_is(dest, JSRF_GAME_FILE, fa->data, fa->size) && entries(dest) == 2, "the written save isn't the one given");
    CHECK(entries(title) == 1, "the title folder holds %d entries, not the one save", entries(title));

    r = jsrf_save_write(title, &b, 0, why, sizeof why);
    CHECK(r == JSRF_EXISTS, "a second save of that name gave %d", r);
    CHECK(file_is(dest, JSRF_GAME_FILE, fa->data, fa->size), "a save that was there was written over");

    r = jsrf_save_write(title, &b, 1, why, sizeof why);
    CHECK(r == JSRF_REPLACED, "replacing gave %d: %s", r, why);
    CHECK(file_is(dest, JSRF_GAME_FILE, fb->data, fb->size), "the replacing save isn't there");
    CHECK(entries(title) == 1, "replacing left %d entries in the title folder", entries(title));

    /* A folder an import left part way is cleared; a .replaced one stops a replace. */
    snprintf(p, sizeof p, "%s.import", dest);
    put_file(p, "stale", "x", 1);
    snprintf(p, sizeof p, "%s.replaced", dest);
    put_file(p, JSRF_GAME_FILE, "y", 1);
    r = jsrf_save_write(title, &a, 1, why, sizeof why);
    CHECK(r == JSRF_FAILED && strstr(why, ".replaced"), "a replace over a .replaced folder gave %d", r);
    CHECK(file_is(dest, JSRF_GAME_FILE, fb->data, fb->size) && file_is(p, JSRF_GAME_FILE, "y", 1),
          "a refused replace changed something");
    js_rmtree(p);
    r = jsrf_save_write(title, &a, 1, why, sizeof why);
    CHECK(r == JSRF_REPLACED && file_is(dest, JSRF_GAME_FILE, fa->data, fa->size), "replace after clearing gave %d", r);
    CHECK(entries(title) == 1, "a stale .import folder is left");

    memset(&junk, 0, sizeof junk);
    jsrf_save_add(&junk, "notes.txt", NULL, 0);
    CHECK(jsrf_save_check(&junk) == JSRF_KIND_OTHER && strstr(junk.why, "SaveMeta"), "a folder without SaveMeta.xbx checks");
    CHECK(jsrf_save_write(title, &junk, 1, why, sizeof why) == JSRF_FAILED, "a folder that isn't a save is written");
    {
        /* JSRFDATA.SAV of another length, or without "JSRF" after its signature. */
        jsrf_save odd;
        made_up_save(&odd, "Test Save", 3);
        jsrf_save_file(&odd, JSRF_GAME_FILE)->size--;
        CHECK(jsrf_save_check(&odd) == JSRF_KIND_OTHER && strstr(odd.why, "41,088"), "a short JSRFDATA.SAV is taken");
        jsrf_save_file(&odd, JSRF_GAME_FILE)->size++;
        CHECK(jsrf_save_check(&odd) == JSRF_KIND_GAME, "the made-up save no longer checks");
        jsrf_save_file(&odd, JSRF_GAME_FILE)->data[JSRF_SIG_BYTES + 3] = 'G';
        CHECK(jsrf_save_check(&odd) == JSRF_KIND_OTHER, "a JSRFDATA.SAV without \"JSRF\" is taken");
        jsrf_save_free(&odd);
    }

    /* The title's own files: only when it has none of the name. */
    memset(&tf, 0, sizeof tf);
    strcpy(tf.name, "TitleMeta.xbx");
    tf.data = (uint8_t *)"first";
    tf.size = 5;
    CHECK(jsrf_title_file_write(title, &tf, why, sizeof why) == JSRF_WROTE, "TitleMeta.xbx wasn't written");
    tf.data = (uint8_t *)"second";
    tf.size = 6;
    CHECK(jsrf_title_file_write(title, &tf, why, sizeof why) == JSRF_EXISTS, "TitleMeta.xbx was written twice");
    CHECK(file_is(title, "TitleMeta.xbx", "first", 5), "TitleMeta.xbx was written over");
    CHECK(entries(title) == 2, "the title file's write left %d entries", entries(title));

    jsrf_save_free(&a);
    jsrf_save_free(&b);
    jsrf_save_free(&junk);
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

static void test_backup(void)
{
    char root[4096], title[4096], backups[4096], made1[4096], made2[4096], p[4096], why[400];
    int r;
    path(root, g_dir, "backup");
    path(title, root, "UDATA/" JSRF_TITLE_ID);
    path(backups, root, "Save backups");
    CHECK(jsrf_backup(title, backups, made1, sizeof made1, why, sizeof why) == JSRF_EXISTS,
          "a backup was made with no saves to back up");
    path(p, title, "91566539302D");
    put_file(p, "SaveMeta.xbx", "meta", 4);
    put_file(p, JSRF_GAME_FILE, "game", 4);
    put_file(title, "TitleMeta.xbx", "title", 5);
    path(p, title, "deeper/still");
    put_file(p, "f", "", 0);
    r = jsrf_backup(title, backups, made1, sizeof made1, why, sizeof why);
    CHECK(r == JSRF_WROTE, "the backup gave %d: %s", r, why);
    path(p, made1, JSRF_TITLE_ID);
    CHECK(same_tree(title, p), "the backup isn't a copy of the saves");
    r = jsrf_backup(title, backups, made2, sizeof made2, why, sizeof why);
    CHECK(r == JSRF_WROTE && strcmp(made1, made2), "a second backup gave %d, into %s", r, made2);
    path(p, made2, JSRF_TITLE_ID);
    CHECK(same_tree(title, p), "the second backup isn't a copy of the saves");
    CHECK(entries(backups) == 2, "the backups folder holds %d, not 2", entries(backups));
}

/* ---- sources -------------------------------------------------------------------------- */

static void put_save(const char *title, const char *folder, const char *name, int byte)
{
    char dir[4096];
    jsrf_save s;
    int i;
    made_up_save(&s, name, byte);
    path(dir, title, folder);
    for (i = 0; i < s.nfiles; i++) put_file(dir, s.files[i].name, s.files[i].data, s.files[i].size);
    jsrf_save_free(&s);
}

static void test_sources(void)
{
    char root[4096], title[4096], p[4096], dir[4096], data[4096];
    uint8_t meta[600];
    size_t mn;
    jsrf_found f;
    int r, i;

    /* Cxbx-Reloaded's data folder. */
    path(root, g_dir, "cxbx");
    path(title, root, "EmuDisk/Partition1/UDATA/" JSRF_TITLE_ID);
    put_save(title, "91566539302D", "Test Save", 5);
    meta_for("GRAFFITI001", meta, &mn);
    path(p, title, "85BE4CFBC301");
    put_file(p, "SaveMeta.xbx", meta, mn);
    put_file(p, "save.dat", "design", 6);
    path(p, title, "NOTASAVE");
    put_file(p, "readme.txt", "x", 1);
    path(p, title, "91566539302D.import");
    put_file(p, "SaveMeta.xbx", meta, mn);
    put_file(title, "TitleMeta.xbx", "title", 5);
    put_file(title, "desktop.ini", "x", 1);

    r = jsrf_read_source(root, &f);
    CHECK(r == 0, "Cxbx's folder wasn't read: %s", f.why);
    CHECK(f.nsaves == 3, "Cxbx's folder gave %d saves, not 3", f.nsaves);
    if (f.nsaves == 3) {
        CHECK(f.saves[0].kind == JSRF_KIND_GAME && !strcmp(f.saves[0].folder, "91566539302D") &&
              f.saves[0].nfiles == 2 && !f.saves[0].signed_here, "the game's save read wrong");
        CHECK(f.saves[1].kind == JSRF_KIND_GRAFFITI && !strcmp(f.saves[1].folder, "85BE4CFBC301"),
              "the graffiti save read as kind %d", f.saves[1].kind);
        CHECK(f.saves[2].kind == JSRF_KIND_OTHER && !strcmp(f.saves[2].from, "NOTASAVE") && f.saves[2].why[0],
              "the folder that isn't a save read as kind %d", f.saves[2].kind);
    }
    CHECK(f.ntitle == 1 && !strcmp(f.title[0].name, "TitleMeta.xbx"), "the title's files: %d", f.ntitle);
    jsrf_found_free(&f);

    path(p, root, "EmuDisk");
    CHECK(jsrf_find_title_dir(p, dir, sizeof dir) == 1 && !strcmp(dir, title), "EmuDisk doesn't lead to the saves");
    CHECK(jsrf_find_title_dir(title, dir, sizeof dir) == 1 && !strcmp(dir, title), "the title folder isn't itself");
    path(p, title, "91566539302D");
    CHECK(jsrf_find_title_dir(p, dir, sizeof dir) == 2, "a save's own folder isn't one");
    CHECK(jsrf_read_source(p, &f) == 0 && f.nsaves == 1 && f.saves[0].kind == JSRF_KIND_GAME &&
          !strcmp(f.saves[0].from, "91566539302D"), "a save's own folder wasn't read as one save");
    jsrf_found_free(&f);
    path(p, g_dir, "nothing");
    js_mkdirs(p);
    CHECK(jsrf_find_title_dir(p, dir, sizeof dir) == 0, "an empty folder has saves");
    CHECK(jsrf_read_source(p, &f) == -1 && f.why[0], "an empty folder was read");
    jsrf_found_free(&f);

    /* xbased's, its names in another case, as a copy off a console may be. */
    path(root, g_dir, "xbased");
    path(title, root, "hdd/partition1/udata/" JSRF_TITLE_ID);
    put_save(title, "91566539302D", "Test Save", 6);
    r = jsrf_find_title_dir(root, dir, sizeof dir);
#ifdef _WIN32
    CHECK(r == 1, "xbased's folder doesn't lead to the saves");
#else
    CHECK(r == 1 && !strcmp(dir, title), "xbased's folder in lower case doesn't lead to the saves (%d)", r);
#endif

    /* Where an export goes: each emulator's own layout, made if need be. */
    {
        char want[4096];
        path(p, g_dir, "cxbx_empty/EmuDisk/Partition1");
        js_mkdirs(p);
        path(p, g_dir, "cxbx_empty");
        path(want, p, "EmuDisk/Partition1/UDATA/" JSRF_TITLE_ID);
        CHECK(jsrf_export_title(JSRF_SRC_CXBX, p, dir, sizeof dir) == 1 && !strcmp(dir, want),
              "an export to a Cxbx folder without saves goes to %s", dir);
        CHECK(jsrf_export_title(JSRF_SRC_XBASED, p, dir, sizeof dir) == 0, "Cxbx's folder is taken for xbased's");
        path(p, g_dir, "xbased");
        r = jsrf_export_title(JSRF_SRC_XBASED, p, dir, sizeof dir);
#ifdef _WIN32
        /* The same folder in any case on Windows, where the export keeps
         * the names it asks for (Partition1, UDATA) once they're found. */
        CHECK(r == 1 && !_stricmp(dir, title), "an export to xbased's folder goes to %s", dir);
#else
        CHECK(r == 1 && !strcmp(dir, title), "an export to xbased's folder goes to %s", dir);
#endif
        CHECK(jsrf_export_title(JSRF_SRC_CXBX, p, dir, sizeof dir) == 0, "xbased's folder is taken for Cxbx's");
        CHECK(jsrf_export_title(JSRF_SRC_XEMU, p, dir, sizeof dir) == 0, "an export to xemu is offered");
    }

    /* A save with too many files, and one with a file too large. */
    path(root, g_dir, "odd/UDATA/" JSRF_TITLE_ID);
    put_save(root, "000000000041", "A", 7);
    path(p, root, "000000000041");
    for (i = 0; i < JSRF_SAVE_FILES; i++) {
        char n[16];
        snprintf(n, sizeof n, "extra%d", i);
        put_file(p, n, "e", 1);
    }
    put_save(root, "000000000042", "B", 8);
    path(p, root, "000000000042");
    {
        uint8_t *big = (uint8_t *)calloc(1, JSRF_FILE_MAX + 1);
        put_file(p, "big.bin", big, JSRF_FILE_MAX + 1);
        free(big);
    }
    r = jsrf_read_title_dir(root, &f);
    CHECK(r == 0 && f.nsaves == 2, "the odd saves weren't read (%d, %d)", r, f.nsaves);
    for (i = 0; i < f.nsaves; i++)
        CHECK(f.saves[i].kind == JSRF_KIND_OTHER && f.saves[i].why[0], "an odd save, %s, is taken", f.saves[i].from);
    jsrf_found_free(&f);

    /* xemu.toml */
    {
        static const char t1[] = "[general]\nhdd_path = '/not/this'\n[sys.files]\nbootrom_path = 'b'\n"
                                 "hdd_path = 'C:\\Users\\me\\xbox_hdd.qcow2'\n";
        static const char t2[] = "[ sys.files ]\n  hdd_path=\"C:\\\\Users\\\\Ren\\u00e9e\\\\hdd.qcow2\"\n";
        static const char t3[] = "[sys.files]\nhdd_path = 'hdd/xbox_hdd.qcow2'\n";
        static const char t4[] = "[sys.files]\nhdd_paths = 'x'\nhdd_path = ''\n";
        char out[4096], want[4096];
        CHECK(jsrf_xemu_hdd_path(t1, strlen(t1), "/d", out, sizeof out) == 1 &&
              !strcmp(out, "C:\\Users\\me\\xbox_hdd.qcow2"), "xemu.toml's literal path: %s", out);
        CHECK(jsrf_xemu_hdd_path(t2, strlen(t2), "/d", out, sizeof out) == 1 &&
              !strcmp(out, "C:\\Users\\Ren\xC3\xA9" "e\\hdd.qcow2"), "xemu.toml's quoted path: %s", out);
        js_path(want, sizeof want, "/d", "hdd/xbox_hdd.qcow2");
        CHECK(jsrf_xemu_hdd_path(t3, strlen(t3), "/d", out, sizeof out) == 1 && !strcmp(out, want),
              "xemu.toml's relative path: %s", out);
        CHECK(jsrf_xemu_hdd_path(t4, strlen(t4), "/d", out, sizeof out) == 0, "an empty hdd_path is taken");
        CHECK(jsrf_xemu_hdd_path(t1, 30, "/d", out, sizeof out) == 0, "hdd_path outside [sys.files] is taken");
    }

    /* xemu's disk: a qcow2 image of a retail-sized disk, the partition at
     * E:'s place (tests/xbox_disk's builder). */
    {
        part pt = build(XBOX_DISK_E_LENGTH, 32);
        qopts o = { 3, 0, 0, 0, -1 };
        qimg q = qcow2_of(&pt, XBOX_DISK_E_OFFSET, XBOX_DISK_E_OFFSET + XBOX_DISK_E_LENGTH, o);
        char image[4096], toml[4096], text[4300];
        path(image, g_dir, "xbox_hdd.qcow2");
        CHECK(js_write_file(image, q.b, q.n) == 0, "the image couldn't be made");
        free(q.b);
        snprintf(text, sizeof text, "[sys.files]\nhdd_path = '%s'\n", image);
        path(data, g_dir, "xemu");
        js_mkdirs(data);
        path(toml, data, "xemu.toml");
        js_write_text(toml, text);
        r = jsrf_read_source(toml, &f);
        CHECK(r == 0, "the image wasn't read through xemu.toml: %s", f.why);
        CHECK(f.nsaves == 1 && !strcmp(f.saves[0].from, BUILT_SAVE) && f.saves[0].nfiles == 3,
              "the image gave %d saves", f.nsaves);
        if (f.nsaves == 1) {
            jsrf_file *g = jsrf_save_file(&f.saves[0], JSRF_GAME_FILE);
            CHECK(g && g->size == SAVE_SIZE && matches(g->data, 3, SAVE_SIZE), "the image's save isn't its bytes");
            /* The built save's SaveMeta.xbx is a pattern, not a name. */
            CHECK(f.saves[0].kind == JSRF_KIND_OTHER && strstr(f.saves[0].why, "name"),
                  "a save with no name in its SaveMeta.xbx is taken");
        }
        CHECK(f.ntitle == 1 && f.title[0].size == TITLE_SIZE && matches(f.title[0].data, 2, TITLE_SIZE),
              "the image's TitleMeta.xbx: %d", f.ntitle);
        jsrf_found_free(&f);
        free(pt.bytes);
        path(p, g_dir, "not_an_image.bin");
        js_write_text(p, "hello");
        CHECK(jsrf_read_source(p, &f) == -1 && f.why[0], "a file that isn't an image was read");
        jsrf_found_free(&f);
        js_rmtree(image);
    }
}


/* ---- the plan and its run ------------------------------------------------------------ */

/* Stand-ins for graffiti_save.c's (b3 row 35): a mark of the key at the
 * file's start, and the slot after it. */
static int stub_resign(uint8_t *file, size_t n, const uint8_t key[16])
{
    if (n < 2) return -1;
    file[0] = (uint8_t)(0x5A ^ key[0]);
    return 0;
}

static int stub_reslot(uint8_t *file, size_t n, unsigned slot, const uint8_t key[16])
{
    if (n < 2) return -1;
    file[1] = (uint8_t)slot;
    return stub_resign(file, n, key);
}

static void put_design(const char *title, const char *name)
{
    char dir[4096], folder[13];
    uint8_t meta[600];
    uint16_t u[16];
    size_t mn, n = units_of(name, u);
    jsrf_folder_name(u, n, folder);
    meta_for(name, meta, &mn);
    path(dir, title, folder);
    put_file(dir, "SaveMeta.xbx", meta, mn);
    put_file(dir, "save.dat", "dd", 2);
}

static void test_plan(void)
{
    static const uint8_t cxbx[16] = { 0xC0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    char from[4096], to[4096], backups[4096], p[4096];
    jsrf_found f;
    jsrf_plan plan;
    int i, r, chosen[8];

    /* The source: the game's save, two designs, and a folder that isn't a save. */
    path(from, g_dir, "plan/from/UDATA/" JSRF_TITLE_ID);
    put_save(from, "91566539302D", "Test Save", 9);
    put_design(from, "GRAFFITI001");
    put_design(from, "GRAFFITI002");
    path(p, from, "JUNK");
    put_file(p, "x", "x", 1);
    put_file(from, "TitleMeta.xbx", "from", 4);
    path(to, g_dir, "plan/to/UDATA/" JSRF_TITLE_ID);
    path(backups, g_dir, "plan/to/Save backups");

    /* Into a JSRF-PC with nothing yet: written, the designs left out until
     * the graffiti page gives their re-sign. */
    CHECK(jsrf_read_title_dir(from, &f) == 0 && f.nsaves == 4, "the plan's source wasn't read (%d)", f.nsaves);
    CHECK(jsrf_plan_make(&f, NULL, to, &plan) == 0 && plan.n == 4, "no plan was made");
    CHECK(plan.writes == 1 && plan.replaces == 0, "a first import plans %d writes", plan.writes);
    for (i = 0; i < plan.n; i++)
        CHECK((plan.items[i].action != JSRF_DO_SKIP) == (f.saves[plan.items[i].save].kind == JSRF_KIND_GAME),
              "%s planned as %d", f.saves[plan.items[i].save].from, plan.items[i].action);
    r = jsrf_plan_run(&f, &plan, to, backups, jsrf_signature_key);
    CHECK(r == 0 && !plan.backup[0], "the first import gave %d, backup %s: %s", r, plan.backup, plan.why);
    path(p, to, "91566539302D/" JSRF_GAME_FILE);
    {
        unsigned char *d;
        size_t n;
        CHECK(js_read_file(p, 1u << 20, &d, &n) == 0 && jsrf_file_verifies(jsrf_signature_key, d, n) == 1,
              "the imported save isn't signed for JSRF-PC");
        if (n) free(d);
    }
    CHECK(file_is(to, "TitleMeta.xbx", "from", 4), "the title's file wasn't copied");
    jsrf_plan_free(&plan);
    jsrf_found_free(&f);

    /* Again, with the graffiti page's re-sign in, and JSRF-PC already
     * holding the game's save and GRAFFITI001: the save replaced after a
     * backup, GRAFFITI001 to the first free slot (GRAFFITI003, as 002 comes
     * across as it is), the folder that isn't a save skipped. */
    jsrf_graffiti_resigner = stub_resign;
    jsrf_graffiti_reslotter = stub_reslot;
    put_design(to, "GRAFFITI001");
    put_file(to, "TitleMeta.xbx", "mine", 4);
    CHECK(jsrf_read_title_dir(from, &f) == 0 && f.nsaves == 4, "the plan's source wasn't read again");
    memset(chosen, 0, sizeof chosen);
    for (i = 0; i < f.nsaves; i++) chosen[i] = 1;
    CHECK(jsrf_plan_make(&f, chosen, to, &plan) == 0, "no second plan");
    CHECK(plan.writes == 3 && plan.replaces == 1, "the second plan writes %d, replaces %d", plan.writes, plan.replaces);
    for (i = 0; i < plan.n; i++) {
        const jsrf_save *s = &f.saves[plan.items[i].save];
        int want = s->kind == JSRF_KIND_GAME ? JSRF_DO_REPLACE : s->kind == JSRF_KIND_OTHER ? JSRF_DO_SKIP :
                   !strcmp(s->name, "GRAFFITI001") ? JSRF_DO_RESLOT : JSRF_DO_WRITE;
        CHECK(plan.items[i].action == want, "%s planned as %d, not %d", s->name, plan.items[i].action, want);
        if (want == JSRF_DO_RESLOT) CHECK(plan.items[i].slot == 2, "GRAFFITI001 goes to slot %d", plan.items[i].slot);
        if (want == JSRF_DO_SKIP) CHECK(plan.items[i].note[0], "a skip has no reason");
    }
    r = jsrf_plan_run(&f, &plan, to, backups, cxbx);
    CHECK(r == 0 && plan.backup[0], "the second import gave %d: %s", r, plan.why);
    path(p, plan.backup, JSRF_TITLE_ID "/91566539302D");
    CHECK(js_is_dir(p), "the backup doesn't hold the save that was replaced");
    path(p, to, "91566539302D/" JSRF_GAME_FILE);
    {
        unsigned char *d;
        size_t n;
        CHECK(js_read_file(p, 1u << 20, &d, &n) == 0 && jsrf_file_verifies(cxbx, d, n) == 1,
              "the replacing save isn't signed under the key given");
        if (n) free(d);
    }
    {
        uint16_t u[16];
        char folder[13], dir[4096];
        uint8_t want[2] = { 0x5A ^ 0xC0, 2 }, meta[600];
        size_t mn;
        jsrf_folder_name(u, units_of("GRAFFITI003", u), folder);
        path(dir, to, folder);
        CHECK(file_is(dir, "save.dat", want, 2), "GRAFFITI001 wasn't moved to GRAFFITI003 and signed");
        meta_for("GRAFFITI003", meta, &mn);
        CHECK(file_is(dir, "SaveMeta.xbx", meta, mn), "the moved design's SaveMeta.xbx doesn't name its slot");
        jsrf_folder_name(u, units_of("GRAFFITI001", u), folder);
        path(dir, to, folder);
        CHECK(file_is(dir, "save.dat", "dd", 2), "the design JSRF-PC had was changed");
    }
    CHECK(file_is(to, "TitleMeta.xbx", "mine", 4), "JSRF-PC's own TitleMeta.xbx was written over");
    jsrf_plan_free(&plan);
    jsrf_found_free(&f);
    jsrf_graffiti_resigner = NULL;
    jsrf_graffiti_reslotter = NULL;
}

int main(int argc, char **argv)
{
    char work[4096];
    if (argc < 2) {
        printf("save_import: give a folder to work in\n");
        return 2;
    }
    path(work, argv[1], "work");
    g_dir = work;
    js_mkdirs(work);
    test_signatures();
    test_names();
    test_write();
    test_backup();
    test_sources();
    test_plan();
    if (failures) {
        printf("save_import: %d failures\n", failures);
        return 1;
    }
    printf("save_import: all cases pass\n");
    return 0;
}
