/*
 * JSRF's saves, as JSRF-PC Tools reads and writes them (jsrf_save.h).
 *
 * What the game's XAPI does, read from its code and checked on a JSRF-PC
 * save (b3 row 36; custom-graffiti/FORMAT.md in the project's notes):
 *
 *   - XCalculateSignatureBegin takes the kernel's XboxSignatureKey as it is
 *     for the HMAC-SHA1 key, and the game asks for roamable signatures, so
 *     no console's hard disk key is mixed in. JSRFDATA.SAV's first 20 bytes
 *     sign the 41,068 after them. A graffiti save carries seven signatures,
 *     laid out by graffiti_save.c (b3 row 35).
 *   - A save's folder is named by a hash of its name, with no key, so every
 *     console and emulator gives a save the same folder. A save imported
 *     into the folder its name gives is the one the game saves back into.
 *
 * Writing never goes over anything the player has without being asked: a
 * save goes into a folder of its own first, and is swapped in only when the
 * whole of it is written, the folder it replaces (only when the player asked
 * for that, after jsrf_backup) removed last.
 */
#include "jsrf_save.h"
#include "setup.h"
#include "setup_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const uint8_t jsrf_signature_key[JSRF_KEY_BYTES] = { 0 };

jsrf_graffiti_resign_fn jsrf_graffiti_resigner;
jsrf_graffiti_reslot_fn jsrf_graffiti_reslotter;

static void say(char *why, size_t n, const char *fmt, const char *a, const char *b)
{
    if (!n) return;
    snprintf(why, n, fmt, a ? a : "", b ? b : "");
    why[n - 1] = 0;
}

/* ---- signatures and names -------------------------------------------------- */

void jsrf_sign(const uint8_t key[JSRF_KEY_BYTES], const void *data, size_t n, uint8_t sig[JSRF_SIG_BYTES])
{
    jsrf_hmac_sha1(key, JSRF_KEY_BYTES, data, n, sig);
}

int jsrf_file_verifies(const uint8_t key[JSRF_KEY_BYTES], const uint8_t *file, size_t n)
{
    uint8_t sig[JSRF_SIG_BYTES];
    if (!file || n < JSRF_SIG_BYTES) return -1;
    jsrf_sign(key, file + JSRF_SIG_BYTES, n - JSRF_SIG_BYTES, sig);
    return memcmp(sig, file, JSRF_SIG_BYTES) == 0;
}

int jsrf_file_resign(const uint8_t key[JSRF_KEY_BYTES], uint8_t *file, size_t n)
{
    uint8_t sig[JSRF_SIG_BYTES];
    if (!file || n < JSRF_SIG_BYTES) return -1;
    jsrf_sign(key, file + JSRF_SIG_BYTES, n - JSRF_SIG_BYTES, sig);
    memcpy(file, sig, JSRF_SIG_BYTES);
    return 0;
}

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* The XBE's header: "XBEH", the base address at 0x104, the certificate's
 * address at 0x118. The certificate: the title ID at 8, the signature key's
 * 16 bytes at 0xC0. */
int jsrf_cxbx_key(const uint8_t *xbe, size_t n, uint8_t key[JSRF_KEY_BYTES])
{
    static const uint8_t cert_key[JSRF_KEY_BYTES] = { 0 };   /* Cxbx's without a keys.bin */
    uint8_t digest[JSRF_SIG_BYTES];
    uint32_t base, cert;
    if (!xbe || n < 0x11C || memcmp(xbe, "XBEH", 4)) return -1;
    base = le32(xbe + 0x104);
    cert = le32(xbe + 0x118);
    if (cert < base || cert - base > n || n - (cert - base) < 0xD0) return -1;
    cert -= base;
    if (le32(xbe + cert + 8) != 0x5345000Au) return -1;
    jsrf_hmac_sha1(cert_key, JSRF_KEY_BYTES, xbe + cert + 0xC0, 16, digest);
    memcpy(key, digest, JSRF_KEY_BYTES);
    return 0;
}

void jsrf_folder_name(const uint16_t *name, size_t units, char out[JSRF_FOLDER_CHARS + 1])
{
    static const char hex[] = "0123456789ABCDEF";
    uint64_t v = 0;
    size_t i;
    int d;
    for (i = 0; i < units; i++) v = (v * 65536u + name[i]) % 0xFFFFFFFFFFC5ull;   /* v < 2^48 */
    for (d = JSRF_FOLDER_CHARS - 1; d >= 0; d--) {
        out[d] = hex[v & 15];
        v >>= 4;
    }
    out[JSRF_FOLDER_CHARS] = 0;
}

static uint16_t unit_at(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int jsrf_meta_name(const uint8_t *meta, size_t n, uint16_t *name, size_t cap)
{
    static const char key[] = "Name=";
    size_t at = 0, units, i, k;
    if (!meta) return -1;
    if (n >= 2 && meta[0] == 0xFF && meta[1] == 0xFE) at = 2;
    units = (n - at) / 2;
    for (i = 0; i < units; ) {
        size_t end = i;
        while (end < units && unit_at(meta + at + 2 * end) != '\r' && unit_at(meta + at + 2 * end) != '\n') end++;
        for (k = 0; k < 5 && i + k < end && unit_at(meta + at + 2 * (i + k)) == (uint16_t)key[k]; k++) {}
        if (k == 5) {
            size_t len = end - i - 5, j;
            if (len > cap) return -1;
            for (j = 0; j < len; j++) name[j] = unit_at(meta + at + 2 * (i + 5 + j));
            return (int)len;
        }
        i = end;
        while (i < units && (unit_at(meta + at + 2 * i) == '\r' || unit_at(meta + at + 2 * i) == '\n')) i++;
    }
    return -1;
}

size_t jsrf_meta_build(const uint16_t *name, size_t units, uint8_t *out, size_t cap)
{
    static const char key[] = "Name=";
    size_t need = 2 + 2 * (5 + units + 2), at = 2, i;
    if (need > cap) return 0;
    out[0] = 0xFF;
    out[1] = 0xFE;
#define PUT(u) do { out[at] = (uint8_t)(u); out[at + 1] = (uint8_t)((u) >> 8); at += 2; } while (0)
    for (i = 0; i < 5; i++) PUT((uint16_t)key[i]);
    for (i = 0; i < units; i++) PUT(name[i]);
    PUT('\r');
    PUT('\n');
#undef PUT
    return need;
}

void jsrf_utf16_to_utf8(const uint16_t *s, size_t units, char *out, size_t cap)
{
    size_t i, o = 0;
    if (!cap) return;
    for (i = 0; i < units; i++) {
        uint32_t c = s[i];
        unsigned char b[4];
        size_t nb, k;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < units && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        } else if (c >= 0xD800 && c < 0xE000) {
            c = 0xFFFD;
        }
        if (c < 0x20) c = 0xFFFD;                      /* nothing that moves the cursor */
        if (c < 0x80) { b[0] = (unsigned char)c; nb = 1; }
        else if (c < 0x800) { b[0] = (unsigned char)(0xC0 | (c >> 6)); b[1] = (unsigned char)(0x80 | (c & 63)); nb = 2; }
        else if (c < 0x10000) {
            b[0] = (unsigned char)(0xE0 | (c >> 12)); b[1] = (unsigned char)(0x80 | ((c >> 6) & 63));
            b[2] = (unsigned char)(0x80 | (c & 63)); nb = 3;
        } else {
            b[0] = (unsigned char)(0xF0 | (c >> 18)); b[1] = (unsigned char)(0x80 | ((c >> 12) & 63));
            b[2] = (unsigned char)(0x80 | ((c >> 6) & 63)); b[3] = (unsigned char)(0x80 | (c & 63)); nb = 4;
        }
        if (o + nb >= cap) break;
        for (k = 0; k < nb; k++) out[o++] = (char)b[k];
    }
    out[o] = 0;
}

/* ---- a save, read whole ----------------------------------------------------- */

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

jsrf_file *jsrf_save_file(jsrf_save *s, const char *name)
{
    int i;
    for (i = 0; i < s->nfiles; i++)
        if (same_name(s->files[i].name, name)) return &s->files[i];
    return NULL;
}

int jsrf_save_add(jsrf_save *s, const char *name, uint8_t *data, size_t size)
{
    jsrf_file *f;
    if (s->nfiles >= JSRF_SAVE_FILES || strlen(name) >= sizeof f->name) {
        free(data);
        return -1;
    }
    f = &s->files[s->nfiles++];
    memcpy(f->name, name, strlen(name) + 1);
    f->data = data;
    f->size = size;
    return 0;
}

static int starts_graffiti(const uint16_t *n, int units)
{
    static const char g[] = "GRAFFITI";
    int i;
    if (units != 11) return 0;
    for (i = 0; i < 8; i++) if (n[i] != (uint16_t)g[i]) return 0;
    for (i = 8; i < 11; i++) if (n[i] < '0' || n[i] > '9') return 0;
    return 1;
}

static int other(jsrf_save *s, const char *why)
{
    s->kind = JSRF_KIND_OTHER;
    say(s->why, sizeof s->why, "%s", why, NULL);
    return s->kind;
}

int jsrf_save_check(jsrf_save *s)
{
    jsrf_file *meta = jsrf_save_file(s, JSRF_META_FILE), *game, *graffiti;
    int units;
    s->kind = JSRF_KIND_OTHER;
    s->signed_here = 0;
    s->why[0] = 0;
    s->name[0] = 0;
    s->folder[0] = 0;
    s->units = 0;
    if (!meta) return other(s, "It has no SaveMeta.xbx, which names a save.");
    units = jsrf_meta_name(meta->data, meta->size, s->name16, JSRF_NAME_UNITS);
    if (units <= 0) return other(s, "Its SaveMeta.xbx gives it no name.");
    s->units = units;
    jsrf_utf16_to_utf8(s->name16, (size_t)units, s->name, sizeof s->name);
    jsrf_folder_name(s->name16, (size_t)units, s->folder);

    game = jsrf_save_file(s, JSRF_GAME_FILE);
    graffiti = jsrf_save_file(s, JSRF_GRAFFITI_FILE);
    if (game) {
        if (game->size != JSRF_GAME_SIZE || memcmp(game->data + JSRF_SIG_BYTES, "JSRF", 4))
            return other(s, "Its JSRFDATA.SAV isn't the one JSRF-PC's game writes "
                            "(41,088 bytes, \"JSRF\" after the signature).");
        s->kind = JSRF_KIND_GAME;
        s->signed_here = jsrf_file_verifies(jsrf_signature_key, game->data, game->size) == 1;
        return s->kind;
    }
    if (graffiti && starts_graffiti(s->name16, units)) {
        s->kind = JSRF_KIND_GRAFFITI;
        if (jsrf_graffiti_resigner && graffiti->size) {
            uint8_t *copy = (uint8_t *)malloc(graffiti->size);
            if (copy) {
                memcpy(copy, graffiti->data, graffiti->size);
                if (jsrf_graffiti_resigner(copy, graffiti->size, jsrf_signature_key) != 0) {
                    free(copy);
                    return other(s, "Its save.dat isn't laid out as the game's graffiti saves are.");
                }
                s->signed_here = memcmp(copy, graffiti->data, graffiti->size) == 0;
                free(copy);
            }
        }
        return s->kind;
    }
    return other(s, "It isn't one of JSRF's saves: it has no JSRFDATA.SAV, and isn't a graffiti design.");
}

int jsrf_save_resign(jsrf_save *s, const uint8_t key[JSRF_KEY_BYTES])
{
    jsrf_file *f;
    int r = -1;
    if (s->kind == JSRF_KIND_GAME) {
        f = jsrf_save_file(s, JSRF_GAME_FILE);
        r = f ? jsrf_file_resign(key, f->data, f->size) : -1;
    } else if (s->kind == JSRF_KIND_GRAFFITI && jsrf_graffiti_resigner) {
        f = jsrf_save_file(s, JSRF_GRAFFITI_FILE);
        r = f ? jsrf_graffiti_resigner(f->data, f->size, key) : -1;
    }
    if (r == 0) s->signed_here = memcmp(key, jsrf_signature_key, JSRF_KEY_BYTES) == 0;
    return r;
}

int jsrf_save_rename(jsrf_save *s, const uint16_t *name, size_t units)
{
    jsrf_file *meta = jsrf_save_file(s, JSRF_META_FILE);
    size_t cap = 2 + 2 * (5 + units + 2), got;
    uint8_t *m;
    if (!meta || units == 0 || units > JSRF_NAME_UNITS || !(m = (uint8_t *)malloc(cap))) return -1;
    got = jsrf_meta_build(name, units, m, cap);
    free(meta->data);
    meta->data = m;
    meta->size = got;
    jsrf_save_check(s);
    return 0;
}

void jsrf_save_free(jsrf_save *s)
{
    int i;
    for (i = 0; i < s->nfiles; i++) free(s->files[i].data);
    s->nfiles = 0;
}

/* ---- writing ------------------------------------------------------------------ */

static int is_there(const char *path) { return js_is_dir(path) || js_is_file(path); }

int jsrf_save_exists(const char *title, const jsrf_save *s)
{
    char dest[4200];
    if (!s->folder[0]) return 0;
    js_path(dest, sizeof dest, title, s->folder);
    return is_there(dest);
}

/* The save's files into dir, a folder just made and empty. */
static int write_files(const char *dir, const jsrf_save *s)
{
    char p[4200];
    int i;
    for (i = 0; i < s->nfiles; i++) {
        js_path(p, sizeof p, dir, s->files[i].name);
        if (js_write_file(p, s->files[i].data, s->files[i].size) != 0) return -1;
    }
    return 0;
}

int jsrf_save_write(const char *title, const jsrf_save *s, int replace, char *why, size_t n)
{
    char dest[4200], part[4300], old[4300];
    int had, r;
    if (why && n) why[0] = 0;
    if (s->kind == JSRF_KIND_OTHER || !s->folder[0]) {
        say(why, n, "%s", s->why[0] ? s->why : "It isn't a save to import.", NULL);
        return JSRF_FAILED;
    }
    if (js_mkdirs(title) != 0) {
        say(why, n, "%s couldn't be made.", title, NULL);
        return JSRF_FAILED;
    }
    js_path(dest, sizeof dest, title, s->folder);
    snprintf(part, sizeof part, "%s.import", dest);
    snprintf(old, sizeof old, "%s.replaced", dest);
    had = is_there(dest);
    if (had && !replace) return JSRF_EXISTS;
    if (had && is_there(old)) {
        say(why, n, "%s is there from an import that didn't finish. Its save is in the backup "
                    "made before it; move the folder out, and import again.", old, NULL);
        return JSRF_FAILED;
    }

    /* The save written whole beside where it goes: a folder of that name
     * left by an import that stopped part way is only ever this tool's. */
    if (is_there(part)) js_rmtree(part);
    if (js_mkdir_new(part) != 0 || write_files(part, s) != 0) {
        js_rmtree(part);
        say(why, n, "The save couldn't be written into %s.", title, NULL);
        return JSRF_FAILED;
    }
    if (!had) {
        r = js_rename_new(part, dest);
        if (r == 0) return JSRF_WROTE;
        js_rmtree(part);
        if (r == 1) return JSRF_EXISTS;
        say(why, n, "The save couldn't be moved into %s.", dest, NULL);
        return JSRF_FAILED;
    }
    if (js_rename_new(dest, old) != 0) {
        js_rmtree(part);
        say(why, n, "%s couldn't be moved aside to replace it; it is as it was.", dest, NULL);
        return JSRF_FAILED;
    }
    if (js_rename_new(part, dest) != 0) {
        if (js_rename_new(old, dest) != 0)
            say(why, n, "The save couldn't be swapped in, and the one it was to replace is "
                        "now %s. Rename it back to its old name.", old, NULL);
        else
            say(why, n, "The save couldn't be swapped in for %s; it is as it was.", dest, NULL);
        js_rmtree(part);
        return JSRF_FAILED;
    }
    js_rmtree(old);
    return JSRF_REPLACED;
}

int jsrf_title_file_write(const char *title, const jsrf_file *f, char *why, size_t n)
{
    char dest[4200], part[4300];
    int r;
    if (js_mkdirs(title) != 0) {
        say(why, n, "%s couldn't be made.", title, NULL);
        return JSRF_FAILED;
    }
    js_path(dest, sizeof dest, title, f->name);
    if (is_there(dest)) return JSRF_EXISTS;
    snprintf(part, sizeof part, "%s.import", dest);
    if (js_write_file(part, f->data, f->size) != 0) {
        say(why, n, "%s couldn't be written.", dest, NULL);
        return JSRF_FAILED;
    }
    r = js_rename_new(part, dest);
    if (r == 0) return JSRF_WROTE;
    js_rmtree(part);                        /* a file: removed alone */
    if (r == 1) return JSRF_EXISTS;
    say(why, n, "%s couldn't be written.", dest, NULL);
    return JSRF_FAILED;
}

/* ---- the backup ----------------------------------------------------------------- */

#define BACKUP_DEPTH  8           /* past any save's: a link back up stops here */

typedef struct copying { const char *from, *to; int depth, bad; } copying;

static int copy_tree(const char *from, const char *to, int depth);

static int copy_one(void *ctx, const char *name, int is_dir)
{
    copying *c = (copying *)ctx;
    char a[4200], b[4200];
    unsigned char *data;
    size_t size;
    js_path(a, sizeof a, c->from, name);
    js_path(b, sizeof b, c->to, name);
    if (is_dir) {
        if (copy_tree(a, b, c->depth + 1) != 0) c->bad = 1;
    } else if (js_read_file(a, 256u << 20, &data, &size) != 0) {
        c->bad = 1;
    } else {
        if (js_write_file(b, data, size) != 0) c->bad = 1;
        free(data);
    }
    return c->bad;
}

static int copy_tree(const char *from, const char *to, int depth)
{
    copying c;
    if (depth > BACKUP_DEPTH || js_mkdir_new(to) != 0) return -1;
    c.from = from;
    c.to = to;
    c.depth = depth;
    c.bad = 0;
    if (js_list_dir(from, copy_one, &c) != 0) return -1;
    return c.bad ? -1 : 0;
}

int jsrf_copy_tree(const char *from, const char *to)
{
    return copy_tree(from, to, 0);
}

int jsrf_backup(const char *title, const char *backups, char *made, size_t mn, char *why, size_t n)
{
    char stamp[64], dir[4200], into[4300];
    time_t now = time(NULL);
    struct tm tm;
    int i, r = 1;
    if (made && mn) made[0] = 0;
    if (!js_is_dir(title)) return JSRF_EXISTS;
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", &tm);
    if (js_mkdirs(backups) != 0) {
        say(why, n, "%s couldn't be made, so nothing was changed.", backups, NULL);
        return JSRF_FAILED;
    }
    for (i = 1; i < 100 && r == 1; i++) {
        char name[80];
        if (i == 1) snprintf(name, sizeof name, "%s", stamp);
        else snprintf(name, sizeof name, "%s (%d)", stamp, i);
        js_path(dir, sizeof dir, backups, name);
        r = js_mkdir_new(dir);
    }
    if (r != 0) {
        say(why, n, "No backup folder could be made in %s, so nothing was changed.", backups, NULL);
        return JSRF_FAILED;
    }
    js_path(into, sizeof into, dir, JSRF_TITLE_ID);
    if (copy_tree(title, into, 0) != 0) {
        js_rmtree(dir);
        say(why, n, "The saves in %s couldn't be backed up, so nothing was changed.", title, NULL);
        return JSRF_FAILED;
    }
    {
        /* Where the saves came from, beside backups, goes with them. */
        char parent[4200], origin[4300], about[4300];
        unsigned char *text = NULL;
        size_t l = strlen(backups), got = 0;
        while (l > 1 && (backups[l - 1] == '/' || backups[l - 1] == '\\')) l--;
        while (l && backups[l - 1] != '/' && backups[l - 1] != '\\') l--;
        if (l > 1 && l < sizeof parent) {
            memcpy(parent, backups, l - 1);
            parent[l - 1] = 0;
            js_path(origin, sizeof origin, parent, JSRF_ORIGIN_FILE);
            if (js_read_file(origin, 1u << 16, &text, &got) == 0 && text) {
                js_path(about, sizeof about, dir, "about.txt");
                js_write_file(about, text, got);
                free(text);
            }
        }
    }
    if (made && mn) snprintf(made, mn, "%s", dir);
    return JSRF_WROTE;
}
