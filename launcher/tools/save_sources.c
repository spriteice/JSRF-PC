/*
 * Where other emulators keep JSRF's saves, and reading them out
 * (save_sources.h).
 *
 *   Cxbx-Reloaded  <data>\EmuDisk\Partition1\UDATA\5345000a, <data> being
 *                  %APPDATA%\Cxbx-Reloaded (under Wine on Linux: the
 *                  prefix's drive_c\users\<name>\AppData\Roaming), or
 *                  Cxbx's own folder in its portable mode, or a folder of
 *                  the player's (picked)
 *   xbased         <data>\hdd\Partition1\UDATA\5345000a, <data> being
 *                  %APPDATA%\xbased, or on Linux $XDG_DATA_HOME/xbased
 *                  (~/.local/share/xbased); its own folder when it is kept
 *                  portable (picked)
 *
 * The emulators' own code isn't used: the places are from their
 * documentation and settings, and every file is read as data.
 */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "save_sources.h"
#include "setup.h"
#include "setup_job.h"
#include "xiso.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_why(jsrf_found *f, const char *fmt, const char *a)
{
    snprintf(f->why, sizeof f->why, fmt, a ? a : "");
}

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return 0;
    }
    return *a == *b;
}

void jsrf_found_free(jsrf_found *f)
{
    int i;
    for (i = 0; i < f->nsaves; i++) jsrf_save_free(&f->saves[i]);
    free(f->saves);
    f->saves = NULL;
    f->nsaves = 0;
    for (i = 0; i < f->ntitle; i++) free(f->title[i].data);
    f->ntitle = 0;
}

static jsrf_save *new_save(jsrf_found *f, const char *from)
{
    jsrf_save *grown = (jsrf_save *)realloc(f->saves, sizeof *grown * (size_t)(f->nsaves + 1));
    jsrf_save *s;
    if (!grown) return NULL;
    f->saves = grown;
    s = &f->saves[f->nsaves++];
    memset(s, 0, sizeof *s);
    snprintf(s->from, sizeof s->from, "%s", from);
    return s;
}

static int is_title_file(const char *name)
{
    return same_name(name, "TitleMeta.xbx") || same_name(name, "TitleImage.xbx") ||
           same_name(name, "SaveImage.xbx");
}

static void keep_title_file(jsrf_found *f, const char *name, uint8_t *data, size_t size)
{
    jsrf_file *t;
    if (f->ntitle >= JSRF_TITLE_FILES || strlen(name) >= sizeof t->name) { free(data); return; }
    t = &f->title[f->ntitle++];
    snprintf(t->name, sizeof t->name, "%s", name);
    t->data = data;
    t->size = size;
}

/* After a save's files are in: what it is, and the reasons the reading
 * itself found not to import it (too many files, one too large). */
static void finish(jsrf_save *s, int too_many, int too_large)
{
    jsrf_save_check(s);
    if (too_many || too_large) {
        s->kind = JSRF_KIND_OTHER;
        snprintf(s->why, sizeof s->why, "%s", too_many ? "It holds more files than any JSRF save does."
                                                       : "A file in it is larger than any JSRF save's.");
    }
}

/* Saves first, the game's before graffiti, each kind by its folder. */
static int save_order(const void *a, const void *b)
{
    const jsrf_save *x = (const jsrf_save *)a, *y = (const jsrf_save *)b;
    int kx = x->kind == JSRF_KIND_GAME ? 0 : x->kind == JSRF_KIND_GRAFFITI ? 1 : 2;
    int ky = y->kind == JSRF_KIND_GAME ? 0 : y->kind == JSRF_KIND_GRAFFITI ? 1 : 2;
    if (kx != ky) return kx - ky;
    return strcmp(x->folder[0] ? x->folder : x->from, y->folder[0] ? y->folder : y->from);
}

static void sort_saves(jsrf_found *f)
{
    if (f->nsaves > 1) qsort(f->saves, (size_t)f->nsaves, sizeof *f->saves, save_order);
}

/* ---- host folders ------------------------------------------------------------ */

/* The child of dir named name, matched without regard to case where the
 * file system minds it (a copy off a console may say udata). 1 found. */
typedef struct child_find { const char *want; char got[256]; } child_find;

static int child_match(void *ctx, const char *name, int is_dir)
{
    child_find *c = (child_find *)ctx;
    (void)is_dir;
    if (!same_name(name, c->want) || strlen(name) >= sizeof c->got) return 0;
    snprintf(c->got, sizeof c->got, "%s", name);
    return 1;
}

static int child(const char *dir, const char *name, char *out, size_t n)
{
    child_find c;
    js_path(out, n, dir, name);
    if (js_is_dir(out) || js_is_file(out)) return 1;
    c.want = name;
    c.got[0] = 0;
    if (js_list_dir(dir, child_match, &c) != 1) return 0;
    js_path(out, n, dir, c.got);
    return 1;
}

typedef struct save_reading { jsrf_save *s; const char *dir; int too_many, too_large; } save_reading;

static int read_save_file(void *ctx, const char *name, int is_dir)
{
    save_reading *r = (save_reading *)ctx;
    char p[JSRF_PATH_MAX + 300];
    unsigned char *data;
    size_t size;
    if (is_dir) return 0;
    if (r->s->nfiles >= JSRF_SAVE_FILES) { r->too_many = 1; return 0; }
    js_path(p, sizeof p, r->dir, name);
    if (js_read_file(p, JSRF_FILE_MAX, &data, &size) != 0) { r->too_large = 1; return 0; }
    jsrf_save_add(r->s, name, data, size);
    return 0;
}

static int read_one_save(jsrf_found *f, const char *dir, const char *from)
{
    save_reading r;
    jsrf_save *s = new_save(f, from);
    if (!s) return -1;
    r.s = s;
    r.dir = dir;
    r.too_many = r.too_large = 0;
    if (js_list_dir(dir, read_save_file, &r) != 0) r.too_large = 1;
    finish(s, r.too_many, r.too_large);
    return 0;
}

typedef struct title_reading { jsrf_found *f; const char *dir; int bad; } title_reading;

static int read_title_entry(void *ctx, const char *name, int is_dir)
{
    title_reading *t = (title_reading *)ctx;
    char p[JSRF_PATH_MAX + 300];
    unsigned char *data;
    size_t size;
    js_path(p, sizeof p, t->dir, name);
    if (is_dir) {
        if (strchr(name, '.')) return 0;             /* an import's own .import or .replaced */
        if (read_one_save(t->f, p, name) != 0) t->bad = 1;
    } else if (is_title_file(name) && js_read_file(p, 1u << 20, &data, &size) == 0) {
        keep_title_file(t->f, name, data, size);
    }
    return t->bad;
}

int jsrf_read_title_dir(const char *dir, jsrf_found *out)
{
    title_reading t;
    memset(out, 0, sizeof *out);
    snprintf(out->where, sizeof out->where, "%s", dir);
    t.f = out;
    t.dir = dir;
    t.bad = 0;
    if (js_list_dir(dir, read_title_entry, &t) != 0) {
        set_why(out, t.bad ? "Out of memory reading %s." : "%s couldn't be read.", dir);
        return -1;
    }
    sort_saves(out);
    if (!out->nsaves) {
        set_why(out, "%s has no saves in it.", dir);
        return -1;
    }
    return 0;
}

static const char *base_name(const char *p)
{
    const char *b = p, *q;
    for (q = p; *q; q++) if ((*q == '/' || *q == '\\') && q[1]) b = q + 1;
    return b;
}

int jsrf_read_save_dir(const char *dir, jsrf_found *out)
{
    char name[256];
    size_t l;
    memset(out, 0, sizeof *out);
    snprintf(out->where, sizeof out->where, "%s", dir);
    snprintf(name, sizeof name, "%s", base_name(dir));
    l = strlen(name);
    while (l && (name[l - 1] == '/' || name[l - 1] == '\\')) name[--l] = 0;
    if (read_one_save(out, dir, name) != 0) {
        set_why(out, "Out of memory reading %s.", dir);
        return -1;
    }
    return 0;
}

int jsrf_find_title_dir(const char *picked, char *dir, size_t n)
{
    static const char *const layouts[][5] = {
        { JSRF_TITLE_ID, NULL },
        { "UDATA", JSRF_TITLE_ID, NULL },
        { "Partition1", "UDATA", JSRF_TITLE_ID, NULL },
        { "EmuDisk", "Partition1", "UDATA", JSRF_TITLE_ID, NULL },   /* Cxbx-Reloaded's data folder */
        { "hdd", "Partition1", "UDATA", JSRF_TITLE_ID, NULL },       /* xbased's */
    };
    char at[JSRF_PATH_MAX], next[JSRF_PATH_MAX], meta[JSRF_PATH_MAX];
    size_t i, k;
    if (!js_is_dir(picked)) return 0;
    js_path(meta, sizeof meta, picked, JSRF_META_FILE);
    if (child(picked, JSRF_META_FILE, meta, sizeof meta) && js_is_file(meta)) {
        snprintf(dir, n, "%s", picked);
        return 2;
    }
    if (same_name(base_name(picked), JSRF_TITLE_ID)) {
        snprintf(dir, n, "%s", picked);
        return 1;
    }
    for (i = 0; i < sizeof layouts / sizeof layouts[0]; i++) {
        snprintf(at, sizeof at, "%s", picked);
        for (k = 0; layouts[i][k]; k++) {
            if (!child(at, layouts[i][k], next, sizeof next) || !js_is_dir(next)) break;
            snprintf(at, sizeof at, "%s", next);
        }
        if (!layouts[i][k]) {
            snprintf(dir, n, "%s", at);
            return 1;
        }
    }
    return 0;
}

/* ---- what the player picked --------------------------------------------------------- */

int jsrf_read_source(const char *path, jsrf_found *out)
{
    char dir[JSRF_PATH_MAX];
    int r;
    memset(out, 0, sizeof *out);
    snprintf(out->where, sizeof out->where, "%s", path);
    if (!js_is_dir(path)) {
        set_why(out, "%s isn't a folder: pick the folder the saves are in.", path);
        return -1;
    }
    r = jsrf_find_title_dir(path, dir, sizeof dir);
    if (r == 2) return jsrf_read_save_dir(dir, out);
    if (r == 1) return jsrf_read_title_dir(dir, out);
    set_why(out, "%s has no JSRF saves in it, or in the folders an Xbox, Cxbx-Reloaded or xbased "
                 "keeps them in under it.", path);
    return -1;
}

/* ---- the usual places ------------------------------------------------------------------ */

/* An environment variable, in UTF-8. 1 when it is set and not empty. */
int jsrf_env(const char *name, char *out, size_t n)
{
#ifdef _WIN32
    wchar_t wn[64], wv[2048];
    size_t i;
    DWORD got;
    for (i = 0; name[i] && i < 63; i++) wn[i] = (wchar_t)name[i];
    wn[i] = 0;
    got = GetEnvironmentVariableW(wn, wv, 2048);
    if (!got || got >= 2048) return 0;
    if (!WideCharToMultiByte(CP_UTF8, 0, wv, -1, out, (int)n, NULL, NULL)) return 0;
    return out[0] != 0;
#else
    const char *v = getenv(name);
    if (!v || !v[0]) return 0;
    snprintf(out, n, "%s", v);
    return 1;
#endif
}

static int add_place(char (*paths)[JSRF_PATH_MAX], int have, int max, const char *p)
{
    int i;
    if (have >= max) return have;
    for (i = 0; i < have; i++) if (!strcmp(paths[i], p)) return have;
    snprintf(paths[have], JSRF_PATH_MAX, "%s", p);
    return have + 1;
}

/* data's title folder under its layout (EmuDisk or hdd), if it's there. */
static int add_title_under(char (*paths)[JSRF_PATH_MAX], int have, int max, const char *data)
{
    char dir[JSRF_PATH_MAX];
    if (js_is_dir(data) && jsrf_find_title_dir(data, dir, sizeof dir) == 1) have = add_place(paths, have, max, dir);
    return have;
}

#ifndef _WIN32
typedef struct wine_users { char (*paths)[JSRF_PATH_MAX]; int have, max; const char *users; } wine_users;

static int wine_user(void *ctx, const char *name, int is_dir)
{
    wine_users *w = (wine_users *)ctx;
    static const char *const roaming[] = { "AppData/Roaming/Cxbx-Reloaded", "Application Data/Cxbx-Reloaded" };
    char p[JSRF_PATH_MAX], u[JSRF_PATH_MAX];
    size_t i;
    if (!is_dir || !strcmp(name, "Public")) return 0;
    js_path(u, sizeof u, w->users, name);
    for (i = 0; i < 2; i++) {
        js_path(p, sizeof p, u, roaming[i]);
        w->have = add_title_under(w->paths, w->have, w->max, p);
    }
    return 0;
}
#endif

int jsrf_usual_places(int source, char (*paths)[JSRF_PATH_MAX], int max)
{
    char base[JSRF_PATH_MAX], p[JSRF_PATH_MAX];
    int have = 0;
#ifdef _WIN32
    if (!jsrf_env("APPDATA", base, sizeof base)) return 0;
    if (source == JSRF_SRC_CXBX) {
        js_path(p, sizeof p, base, "Cxbx-Reloaded");
        have = add_title_under(paths, have, max, p);
    } else if (source == JSRF_SRC_XBASED) {
        js_path(p, sizeof p, base, "xbased");
        have = add_title_under(paths, have, max, p);
    }
#else
    char home[JSRF_PATH_MAX], data[JSRF_PATH_MAX];
    if (!jsrf_env("HOME", home, sizeof home)) return 0;
    if (!jsrf_env("XDG_DATA_HOME", data, sizeof data)) js_path(data, sizeof data, home, ".local/share");
    if (source == JSRF_SRC_CXBX) {
        wine_users w;
        if (!jsrf_env("WINEPREFIX", base, sizeof base)) js_path(base, sizeof base, home, ".wine");
        js_path(p, sizeof p, base, "drive_c/users");
        w.paths = paths;
        w.have = have;
        w.max = max;
        w.users = p;
        js_list_dir(p, wine_user, &w);
        have = w.have;
    } else if (source == JSRF_SRC_XBASED) {
        js_path(p, sizeof p, data, "xbased");
        have = add_title_under(paths, have, max, p);
    }
    (void)base;
#endif
    return have;
}

/* ---- exports ------------------------------------------------------------------------------ */

int jsrf_export_title(int source, const char *picked, char *title, size_t n)
{
    const char *drive = source == JSRF_SRC_CXBX ? "EmuDisk" : "hdd";
    char a[JSRF_PATH_MAX], b[JSRF_PATH_MAX], c[JSRF_PATH_MAX];
    if (source != JSRF_SRC_CXBX && source != JSRF_SRC_XBASED) return 0;
    if (same_name(base_name(picked), JSRF_TITLE_ID) && js_is_dir(picked)) {
        snprintf(title, n, "%s", picked);
        return 1;
    }
    /* The data folder, or its drive folder, or its Partition1: each
     * emulator's own layout only, so one isn't taken for the other. */
    if (child(picked, drive, a, sizeof a) && child(a, "Partition1", b, sizeof b) && js_is_dir(b)) {
    } else if (same_name(base_name(picked), drive) && child(picked, "Partition1", b, sizeof b) && js_is_dir(b)) {
    } else if (same_name(base_name(picked), "Partition1")) {
        snprintf(b, sizeof b, "%s", picked);
    } else {
        return 0;
    }
    if (!child(b, "UDATA", c, sizeof c)) js_path(c, sizeof c, b, "UDATA");
    js_path(title, n, c, JSRF_TITLE_ID);
    return 1;
}

int jsrf_usual_export(int source, char *folder, size_t n)
{
    char base[JSRF_PATH_MAX], p[JSRF_PATH_MAX], title[JSRF_PATH_MAX];
#ifdef _WIN32
    if (!jsrf_env("APPDATA", base, sizeof base)) return 0;
    js_path(p, sizeof p, base, source == JSRF_SRC_CXBX ? "Cxbx-Reloaded" : "xbased");
#else
    char paths[4][JSRF_PATH_MAX];
    if (source == JSRF_SRC_CXBX) {
        /* Under Wine: a prefix's Cxbx-Reloaded that has saves of the game. */
        if (jsrf_usual_places(source, paths, 4) < 1) return 0;
        snprintf(folder, n, "%s", paths[0]);
        return 1;
    }
    if (jsrf_env("XDG_DATA_HOME", base, sizeof base)) js_path(p, sizeof p, base, "xbased");
    else if (jsrf_env("HOME", base, sizeof base)) js_path(p, sizeof p, base, ".local/share/xbased");
    else return 0;
#endif
    if (!js_is_dir(p) || !jsrf_export_title(source, p, title, sizeof title)) return 0;
    snprintf(folder, n, "%s", p);
    return 1;
}

int jsrf_read_xbe_head(const char *game, uint8_t *buf, size_t *got, char *why, size_t wn)
{
    char p[JSRF_PATH_MAX], err[256];
    uint64_t size;
    FILE *f;
    *got = 0;
    if (js_is_dir(game)) {
        if (!child(game, "default.xbe", p, sizeof p) || !(f = js_fopen(p, "rb"))) {
            snprintf(why, wn, "%s has no default.xbe.", game);
            return -1;
        }
        *got = fread(buf, 1, JSRF_XBE_HEAD, f);
        fclose(f);
        return *got ? 0 : -1;
    }
    if (xiso_read_file(game, "default.xbe", buf, JSRF_XBE_HEAD, got, &size, err, sizeof err) != 0 && !*got) {
        snprintf(why, wn, "default.xbe couldn't be read from %s: %s", game, err);
        return -1;
    }
    return 0;
}
