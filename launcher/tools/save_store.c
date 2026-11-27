/*
 * JSRF-PC Tools' save manager: save slots, backups, and save folders taken
 * out of JSRF-PC's saves (save_store.h).
 *
 * A folder is replaced the same way everywhere here: the new one is made
 * whole beside it (<name>.new), the old one renamed aside (<name>.old), the
 * new one renamed in, and only then the old one removed. If the second
 * rename fails, the old one is renamed back. A .new or .old left by a run
 * that stopped halfway is cleaned up, or put back, before the next one.
 */
#include "save_store.h"
#include "setup.h"
#include "setup_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

static void say(char *why, size_t n, const char *fmt, const char *a)
{
    if (why && n) snprintf(why, n, fmt, a);
}

static int there(const char *path) { return js_is_dir(path) || js_is_file(path); }

static void stamp(time_t t, char out[32]);

/* ---- what a title folder holds ----------------------------------------------- */

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

typedef struct finding { const char *name; int found; char as[64]; } finding;

static int find_one(void *ctx, const char *name, int is_dir)
{
    finding *f = (finding *)ctx;
    const char *a = name, *b = f->name;
    if (is_dir) return 0;
    /* FATX matches names without regard to case, and so do copies of it. */
    while (*a && *b && lower(*a) == lower(*b)) { a++; b++; }
    if (!*a && !*b) { f->found = 1; snprintf(f->as, sizeof f->as, "%s", name); return 1; }
    return 0;
}

static int has_file(const char *dir, const char *name)
{
    finding f;
    f.name = name;
    f.found = 0;
    js_list_dir(dir, find_one, &f);
    return f.found;
}

typedef struct counting { const char *title; jsrf_held_count *c; } counting;

static int count_one(void *ctx, const char *name, int is_dir)
{
    counting *k = (counting *)ctx;
    char p[JSRF_PATH_MAX];
    if (!is_dir) return 0;              /* the title's own files: TitleMeta.xbx and so on */
    js_path(p, sizeof p, k->title, name);
    if (has_file(p, JSRF_GAME_FILE)) k->c->game = 1;
    else if (has_file(p, JSRF_GRAFFITI_FILE)) k->c->designs++;
    else k->c->others++;
    return 0;
}

void jsrf_store_count(const char *title, jsrf_held_count *c)
{
    counting k;
    memset(c, 0, sizeof *c);
    k.title = title;
    k.c = c;
    if (js_is_dir(title)) js_list_dir(title, count_one, &k);
}

void jsrf_store_count_text(const jsrf_held_count *c, char *out, size_t n)
{
    char d[40] = "";
    if (c->designs) snprintf(d, sizeof d, "%d design%s", c->designs, c->designs == 1 ? "" : "s");
    if (c->game && d[0]) snprintf(out, n, "Game save, %s", d);
    else if (c->game) snprintf(out, n, "Game save");
    else if (d[0]) snprintf(out, n, "%c%s", d[0], d + 1);
    else snprintf(out, n, "%s", c->others ? "Other saves only" : "Nothing");
}

/* ---- dates ------------------------------------------------------------------------ */

static int parse_when(const char *s, int v[6])
{
    char tail;
    if (!s || strlen(s) < 19) return 0;
    if (sscanf(s, "%4d-%2d-%2d %2d.%2d.%2d%c", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &tail) < 6) return 0;
    return v[1] >= 1 && v[1] <= 12 && v[2] >= 1 && v[2] <= 31 && v[3] >= 0 && v[3] < 24 && v[4] >= 0 && v[4] < 60;
}

void jsrf_store_when_text(const char *when, char *out, size_t n)
{
    static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int v[6];
    if (!parse_when(when, v)) { snprintf(out, n, "%s", when ? when : ""); return; }
    snprintf(out, n, "%d %s %d, %d:%02d%s", v[2], months[v[1] - 1], v[0], v[3] % 12 ? v[3] % 12 : 12, v[4],
             v[3] < 12 ? "am" : "pm");
}

void jsrf_store_now(char out[32])
{
    stamp(time(NULL), out);
}

static void stamp(time_t t, char out[32])
{
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    strftime(out, 32, "%Y-%m-%d %H.%M.%S", &tm);
}

int jsrf_store_file_when(const char *path, char out[32])
{
#ifdef _WIN32
    wchar_t w[4096];
    WIN32_FILE_ATTRIBUTE_DATA a;
    ULARGE_INTEGER u;
    out[0] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 4096) || !GetFileAttributesExW(w, GetFileExInfoStandard, &a))
        return 0;
    u.LowPart = a.ftLastWriteTime.dwLowDateTime;
    u.HighPart = a.ftLastWriteTime.dwHighDateTime;
    stamp((time_t)((u.QuadPart - 116444736000000000ULL) / 10000000ULL), out);   /* from 1601 to 1970 */
    return 1;
#else
    struct stat st;
    out[0] = 0;
    if (stat(path, &st) != 0) return 0;
    stamp(st.st_mtime, out);
    return 1;
#endif
}

typedef struct game_finding { const char *title; char *out; size_t n; int found; } game_finding;

static int game_one(void *ctx, const char *name, int is_dir)
{
    game_finding *g = (game_finding *)ctx;
    char p[JSRF_PATH_MAX];
    finding f;
    if (!is_dir) return 0;
    js_path(p, sizeof p, g->title, name);
    f.name = JSRF_GAME_FILE;
    f.found = 0;
    js_list_dir(p, find_one, &f);
    if (!f.found) return 0;
    /* The file's own name, in the case it has there. */
    g->found = 1;
    js_path(g->out, g->n, p, f.as);
    return 1;
}

int jsrf_store_game_file(const char *title, char *out, size_t n)
{
    game_finding g;
    g.title = title;
    g.out = out;
    g.n = n;
    g.found = 0;
    if (n) out[0] = 0;
    if (js_is_dir(title)) js_list_dir(title, game_one, &g);
    return g.found;
}

/* ---- about.txt ------------------------------------------------------------------- */

/* name made one line, and cut to fit at a whole character. */
static void clean_name(const char *name, char *out, size_t n)
{
    size_t i, k = 0;
    for (i = 0; name && name[i] && k + 1 < n; i++) {
        unsigned char c = (unsigned char)name[i];
        out[k++] = c < 0x20 || c == 0x7f ? ' ' : (char)c;
    }
    out[k] = 0;
    if (name && name[i]) {
        /* Cut: back off a character left without its last bytes. */
        while (k && ((unsigned char)out[k - 1] & 0xC0) == 0x80) k--;
        if (k && ((unsigned char)out[k - 1] & 0xC0) == 0xC0) k--;
        out[k] = 0;
    }
    while (k && out[k - 1] == ' ') out[--k] = 0;
    for (i = 0; out[i] == ' '; i++) {}
    if (i) memmove(out, out + i, strlen(out + i) + 1);
}

static int write_about(const char *dir, const char *name, const char *saved)
{
    char p[JSRF_PATH_MAX], text[JSRF_STORE_NAME + 80];
    js_path(p, sizeof p, dir, JSRF_STORE_ABOUT);
    snprintf(text, sizeof text, "name=%s\n%s%s%s", name, saved && saved[0] ? "saved=" : "", saved ? saved : "",
             saved && saved[0] ? "\n" : "");
    return js_write_text(p, text) == 0 ? 0 : -1;
}

static void read_about(jsrf_held *h)
{
    char p[JSRF_PATH_MAX], v[1024];
    int t[6];
    js_path(p, sizeof p, h->dir, JSRF_STORE_ABOUT);
    h->name[0] = 0;
    if (js_read_kv(p, "name", v, sizeof v)) clean_name(v, h->name, sizeof h->name);
    if (js_read_kv(p, "saved", v, sizeof v) && parse_when(v, t)) snprintf(h->when, sizeof h->when, "%.19s", v);
}

int jsrf_store_rename(const char *dir, const char *name)
{
    char p[JSRF_PATH_MAX], saved[1024] = "", clean[JSRF_STORE_NAME];
    if (!js_is_dir(dir)) return -1;
    js_path(p, sizeof p, dir, JSRF_STORE_ABOUT);
    js_read_kv(p, "saved", saved, sizeof saved);
    clean_name(name, clean, sizeof clean);
    return write_about(dir, clean, saved);
}

/* ---- listing --------------------------------------------------------------------- */

static void held_fill(jsrf_held *h)
{
    char t[JSRF_PATH_MAX];
    js_path(t, sizeof t, h->dir, JSRF_TITLE_ID);
    h->used = js_is_dir(t);
    read_about(h);
    if (h->used) jsrf_store_count(t, &h->count);
    else memset(&h->count, 0, sizeof h->count);
}

void jsrf_store_slots(const char *slots, jsrf_held out[JSRF_STORE_SLOTS])
{
    int i;
    for (i = 0; i < JSRF_STORE_SLOTS; i++) {
        char num[8];
        memset(&out[i], 0, sizeof out[i]);
        snprintf(num, sizeof num, "%d", i + 1);
        js_path(out[i].dir, sizeof out[i].dir, slots, num);
        held_fill(&out[i]);
    }
}

typedef struct listing { const char *dir; jsrf_held *items; int n, cap, bad; } listing;

static int list_one(void *ctx, const char *name, int is_dir)
{
    listing *l = (listing *)ctx;
    char p[JSRF_PATH_MAX], t[JSRF_PATH_MAX];
    int v[6];
    jsrf_held *h;
    if (!is_dir) return 0;
    js_path(p, sizeof p, l->dir, name);
    js_path(t, sizeof t, p, JSRF_TITLE_ID);
    if (!js_is_dir(t)) return 0;
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 16;
        jsrf_held *more = (jsrf_held *)realloc(l->items, sizeof *more * (size_t)cap);
        if (!more) { l->bad = 1; return 1; }
        l->items = more;
        l->cap = cap;
    }
    h = &l->items[l->n++];
    memset(h, 0, sizeof *h);
    snprintf(h->dir, sizeof h->dir, "%s", p);
    if (parse_when(name, v)) snprintf(h->when, sizeof h->when, "%.19s", name);
    held_fill(h);
    return 0;
}

/* Newest first: the folders' names are their dates, and " (2)" comes after
 * the one it follows. */
static int newest_first(const void *a, const void *b)
{
    const char *x = strrchr(((const jsrf_held *)a)->dir, '/'), *y = strrchr(((const jsrf_held *)b)->dir, '/');
#ifdef _WIN32
    const char *x2 = strrchr(((const jsrf_held *)a)->dir, '\\'), *y2 = strrchr(((const jsrf_held *)b)->dir, '\\');
    if (x2 > x) x = x2;
    if (y2 > y) y = y2;
#endif
    x = x ? x + 1 : ((const jsrf_held *)a)->dir;
    y = y ? y + 1 : ((const jsrf_held *)b)->dir;
    return strcmp(y, x);
}

int jsrf_store_backups(const char *backups, jsrf_held **out)
{
    listing l;
    memset(&l, 0, sizeof l);
    l.dir = backups;
    *out = NULL;
    if (js_is_dir(backups)) js_list_dir(backups, list_one, &l);
    if (l.bad) { free(l.items); return -1; }
    if (l.n > 1) qsort(l.items, (size_t)l.n, sizeof *l.items, newest_first);
    *out = l.items;
    return l.n;
}

/* ---- replacing a folder ------------------------------------------------------------ */

/* A .new or .old of final's left by a run that stopped: the .new is
 * removed; the .old is removed when final is there (the swap was done), or
 * put back as final when it isn't (the swap stopped between its renames). */
static void tidy(const char *final, const char *made, const char *old)
{
    if (there(made)) js_rmtree(made);
    if (there(old)) {
        if (there(final)) js_rmtree(old);
        else js_rename_new(old, final);
    }
}

/* made swapped in for final (which may not be there). 0, or -1 and final
 * as it was, made removed. */
static int swap_in(const char *final, const char *made, const char *old)
{
    int had = there(final);
    if (had && js_rename_new(final, old) != 0) { js_rmtree(made); return -1; }
    if (js_rename_new(made, final) != 0) {
        if (had) js_rename_new(old, final);
        js_rmtree(made);
        return -1;
    }
    if (had) js_rmtree(old);
    return 0;
}

static void sibling(char *out, size_t n, const char *path, const char *ext)
{
    snprintf(out, n, "%s%s", path, ext);
}

/* ---- slots ------------------------------------------------------------------------- */

int jsrf_store_save_slot(const char *title, const char *slots, int i, const char *name, char *why, size_t n)
{
    char final[JSRF_PATH_MAX], made[JSRF_PATH_MAX + 8], old[JSRF_PATH_MAX + 8], into[JSRF_PATH_MAX + 20];
    char num[8], now[32], clean[JSRF_STORE_NAME];
    if (i < 0 || i >= JSRF_STORE_SLOTS) { say(why, n, "%s", "There's no such slot."); return JSRF_FAILED; }
    if (!js_is_dir(title)) {
        say(why, n, "%s", "JSRF-PC has no saves yet: play it once, then save them here.");
        return JSRF_FAILED;
    }
    if (js_mkdirs(slots) != 0) { say(why, n, "%s couldn't be made.", slots); return JSRF_FAILED; }
    snprintf(num, sizeof num, "%d", i + 1);
    js_path(final, sizeof final, slots, num);
    sibling(made, sizeof made, final, ".new");
    sibling(old, sizeof old, final, ".old");
    tidy(final, made, old);
    js_path(into, sizeof into, made, JSRF_TITLE_ID);
    jsrf_store_now(now);
    clean_name(name, clean, sizeof clean);
    if (js_mkdir_new(made) != 0 || jsrf_copy_tree(title, into) != 0 || write_about(made, clean, now) != 0) {
        js_rmtree(made);
        say(why, n, "JSRF-PC's saves couldn't be copied into %s, so the slot is as it was.", slots);
        return JSRF_FAILED;
    }
    if (swap_in(final, made, old) != 0) {
        say(why, n, "%s couldn't be replaced, so the slot is as it was.", final);
        return JSRF_FAILED;
    }
    return JSRF_WROTE;
}

int jsrf_store_load(const char *from, const char *title, const char *backups, const char *label,
                    char *made, size_t mn, char *why, size_t n)
{
    char src[JSRF_PATH_MAX], next[JSRF_PATH_MAX + 8], old[JSRF_PATH_MAX + 8], parent[JSRF_PATH_MAX];
    char *cut;
    if (made && mn) made[0] = 0;
    js_path(src, sizeof src, from, JSRF_TITLE_ID);
    if (!js_is_dir(src)) { say(why, n, "%s holds no saves.", from); return JSRF_FAILED; }
    sibling(next, sizeof next, title, ".new");
    sibling(old, sizeof old, title, ".old");
    tidy(title, next, old);
    if (js_is_dir(title)) {
        char b[JSRF_PATH_MAX];
        if (jsrf_backup(title, backups, b, sizeof b, why, n) == JSRF_FAILED) return JSRF_FAILED;
        if (label && label[0]) jsrf_store_rename(b, label);
        if (made && mn) snprintf(made, mn, "%s", b);
    }
    /* UDATA, for a JSRF-PC that has never saved. */
    snprintf(parent, sizeof parent, "%s", title);
    cut = strrchr(parent, '/');
#ifdef _WIN32
    if (strrchr(parent, '\\') > cut) cut = strrchr(parent, '\\');
#endif
    if (cut) { *cut = 0; js_mkdirs(parent); }
    if (jsrf_copy_tree(src, next) != 0) {
        js_rmtree(next);
        say(why, n, "%s couldn't be copied, so JSRF-PC's saves are as they were.", src);
        return JSRF_FAILED;
    }
    if (swap_in(title, next, old) != 0) {
        say(why, n, "%s couldn't be replaced (is JSRF-PC still running?), so its saves are as they were.", title);
        return JSRF_FAILED;
    }
    return JSRF_WROTE;
}

int jsrf_store_delete(const char *dir)
{
    char p[JSRF_PATH_MAX];
    /* Only a slot's or a backup's folder: one with a 5345000a or about.txt. */
    js_path(p, sizeof p, dir, JSRF_TITLE_ID);
    if (!js_is_dir(p)) {
        js_path(p, sizeof p, dir, JSRF_STORE_ABOUT);
        if (!js_is_file(p)) return -1;
    }
    js_rmtree(dir);
    return there(dir) ? -1 : 0;
}

/* ---- taking saves out ------------------------------------------------------------ */

static int is_folder_name(const char *s)
{
    int i;
    for (i = 0; i < JSRF_FOLDER_CHARS; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'F') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return s[i] == 0;
}

int jsrf_store_delete_saves(const char *title, const char *backups, const char *label,
                            const char (*folders)[JSRF_FOLDER_CHARS + 1], int nfolders,
                            char *made, size_t mn, char *why, size_t n)
{
    char b[JSRF_PATH_MAX];
    int i, done = 0;
    if (made && mn) made[0] = 0;
    for (i = 0; i < nfolders; i++)
        if (!is_folder_name(folders[i])) { say(why, n, "%s isn't a save's folder.", folders[i]); return -1; }
    if (!js_is_dir(title)) return 0;
    if (jsrf_backup(title, backups, b, sizeof b, why, n) == JSRF_FAILED) return -1;
    if (label && label[0]) jsrf_store_rename(b, label);
    if (made && mn) snprintf(made, mn, "%s", b);
    for (i = 0; i < nfolders; i++) {
        char p[JSRF_PATH_MAX];
        js_path(p, sizeof p, title, folders[i]);
        if (!there(p)) continue;
        js_rmtree(p);
        if (there(p)) {
            say(why, n, "%s couldn't be deleted (is JSRF-PC still running?).", p);
            return -1;
        }
        done++;
    }
    return done;
}

/* ---- a design's preview ------------------------------------------------------------ */

int jsrf_design_preview(const char *save_dat, uint8_t *rgba, int *w, int *h)
{
    static const int widths[5] = { 32, 32, 48, 64, 128 };
    enum { HEAD = 16 + 7 * 20 };
    uint8_t buf[HEAD + JSRF_PREVIEW_W * JSRF_PREVIEW_H * 2];
    size_t got, need;
    int cls, i;
    FILE *f = js_fopen(save_dat, "rb");
    if (!f) return -1;
    got = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (got < HEAD || memcmp(buf + 4, "JSRF", 4) != 0 || buf[0] > 4) return -1;
    cls = buf[0];
    need = HEAD + (size_t)widths[cls] * JSRF_PREVIEW_H * 2;
    if (got < need) return -1;
    *w = widths[cls];
    *h = JSRF_PREVIEW_H;
    for (i = 0; i < *w * *h; i++) {
        unsigned v = buf[HEAD + 2 * i] | (buf[HEAD + 2 * i + 1] << 8);
        uint8_t *p = rgba + 4 * i;
        p[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
        p[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
        p[2] = (uint8_t)((v & 31) * 255 / 31);
        p[3] = v == 0xC618u ? 0 : 255;      /* GRAFFITI_CLEAR */
    }
    return cls;
}
