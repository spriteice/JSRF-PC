/*
 * The shader cache -- see nv2a_shader_cache.h.
 *
 * Small enough to keep whole in memory: a title has hundreds of programs, not
 * tens of thousands, and every lookup here is a linear scan that happens when a
 * program is built, which is already the slow path. The binaries are the only
 * part that can be large (tens of kilobytes each on some drivers), so they stay
 * on disk and are read when asked for.
 */
#include "nv2a_shader_cache.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <direct.h>
#  include <io.h>
#  include <fcntl.h>
#  include <sys/stat.h>
static CRITICAL_SECTION s_lock;
static volatile LONG s_lock_init;
static void lock(void)
{
    if (InterlockedCompareExchange(&s_lock_init, 1, 0) == 0) {
        InitializeCriticalSection(&s_lock);
        InterlockedExchange(&s_lock_init, 2);
    }
    while (s_lock_init != 2) Sleep(0);
    EnterCriticalSection(&s_lock);
}
static void unlock(void) { LeaveCriticalSection(&s_lock); }
static FILE *sc_fopen(const char *path, const char *mode)
{
    wchar_t wp[2048], wm[8];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 2048)) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, 0, mode, -1, wm, 8)) return NULL;
    return _wfopen(wp, wm);
}
static void sc_mkdir(const char *path)
{
    wchar_t wp[2048];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 2048)) _wmkdir(wp);
}
static void sc_truncate(const char *path, long len)
{
    wchar_t wp[2048];
    int fd;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 2048)) return;
    fd = _wopen(wp, _O_RDWR | _O_BINARY);
    if (fd < 0) return;
    _chsize_s(fd, len);
    _close(fd);
}
#else
#  include <pthread.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static void lock(void)   { pthread_mutex_lock(&s_mutex); }
static void unlock(void) { pthread_mutex_unlock(&s_mutex); }
static FILE *sc_fopen(const char *path, const char *mode) { return fopen(path, mode); }
static void sc_mkdir(const char *path) { mkdir(path, 0755); }
static void sc_truncate(const char *path, long len) { if (truncate(path, (off_t)len)) { /* nothing */ } }
#endif

#define MAGIC_SRC  0x3153534Au   /* "JSS1": a source text */
#define MAGIC_PROG 0x3150534Au   /* "JSP1": a program, as two source hashes */
#define MAGIC_RCP  0x3152534Au   /* "JSR1": a pipeline recipe */
#define MAGIC_BIN  0x3242534Au   /* "JSB2": a compiled program, with a hash of its bytes */
#define MAGIC_DROP 0x5842534Au   /* "JSBX": a compiled program to forget */

#define MAX_PROGRAMS_FILE (64L << 20)
#define MAX_BINARIES_FILE (256L << 20)
#define MAX_TEXT          (512u << 10)
#define MAX_BLOB          (8u << 20)

typedef struct { uint64_t h; uint32_t len; char *text; } Src;
typedef struct { uint64_t key; int vi, fi; } Prog;
typedef struct { uint64_t key; uint32_t recipe; } Rcp;
typedef struct { uint64_t key, hash; uint32_t format, len; long off; } Bin;

static struct {
    int   open, bin_open;
    char  pfile[1200], rfile[1200], bfile[1400];
    FILE *pf, *rf, *bf;            /* appended to */
    long  psize, bsize;
    Src  *src;  int nsrc,  capsrc;
    Prog *prog; int nprog, capprog;
    Rcp  *rcp;  int nrcp,  caprcp;
    Bin  *bin;  int nbin,  capbin;
    int   added;
    int   pbad, rbad, bbad;          /* a write failed (a full disk): no more appends this run */
} C;

static uint64_t fnv64(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    while (n--) { h ^= *b++; h *= 0x100000001B3ull; }
    return h;
}
static uint64_t text_hash(const char *s)
{
    return fnv64(0xCBF29CE484222325ull, s, strlen(s));
}

uint64_t nv2a_sc_key(const char *vsrc, const char *fsrc)
{
    uint64_t v = text_hash(vsrc ? vsrc : ""), f = text_hash(fsrc ? fsrc : "");
    uint64_t k = fnv64(0xCBF29CE484222325ull, &v, sizeof v);
    k = fnv64(k, &f, sizeof f);
    return k ? k : 1;
}

static int grow(void **arr, int *cap, int n, size_t each)
{
    void *p;
    int nc;
    if (n < *cap) return 1;
    nc = *cap ? *cap * 2 : 256;
    p = realloc(*arr, (size_t)nc * each);
    if (!p) return 0;
    *arr = p; *cap = nc;
    return 1;
}

static int src_find(uint64_t h)
{
    int i;
    for (i = 0; i < C.nsrc; i++) if (C.src[i].h == h) return i;
    return -1;
}
static int src_put(uint64_t h, const char *text, uint32_t len)
{
    int i = src_find(h);
    char *t;
    if (i >= 0) return i;
    if (!grow((void **)&C.src, &C.capsrc, C.nsrc, sizeof *C.src)) return -1;
    t = (char *)malloc((size_t)len + 1);
    if (!t) return -1;
    memcpy(t, text, len); t[len] = 0;
    C.src[C.nsrc].h = h; C.src[C.nsrc].len = len; C.src[C.nsrc].text = t;
    return C.nsrc++;
}
static int prog_find(uint64_t key)
{
    int i;
    for (i = 0; i < C.nprog; i++) if (C.prog[i].key == key) return i;
    return -1;
}

/* Read the programs file into memory. Returns the length of the part that is
 * whole records; a damaged tail is cut off. */
static long load_programs(FILE *f)
{
    long good = 0;
    char *buf = NULL;
    for (;;) {
        uint32_t magic;
        if (fread(&magic, 4, 1, f) != 1) break;
        if (magic == MAGIC_SRC) {
            uint64_t h; uint32_t len;
            if (fread(&h, 8, 1, f) != 1 || fread(&len, 4, 1, f) != 1 || len > MAX_TEXT) break;
            buf = (char *)realloc(buf, (size_t)len + 1);
            if (!buf || (len && fread(buf, 1, len, f) != len)) break;
            buf[len] = 0;
            if (text_hash(buf) != h || src_put(h, buf, len) < 0) break;
        } else if (magic == MAGIC_PROG) {
            uint64_t key, vh, fh;
            int vi, fi;
            if (fread(&key, 8, 1, f) != 1 || fread(&vh, 8, 1, f) != 1 || fread(&fh, 8, 1, f) != 1) break;
            vi = src_find(vh); fi = src_find(fh);
            if (vi < 0 || fi < 0) break;
            if (prog_find(key) < 0) {
                if (!grow((void **)&C.prog, &C.capprog, C.nprog, sizeof *C.prog)) break;
                C.prog[C.nprog].key = key; C.prog[C.nprog].vi = vi; C.prog[C.nprog].fi = fi;
                C.nprog++;
            }
        } else {
            break;
        }
        good = ftell(f);
    }
    free(buf);
    return good;
}

static long load_recipes(FILE *f)
{
    long good = 0;
    for (;;) {
        uint32_t magic, recipe; uint64_t key;
        if (fread(&magic, 4, 1, f) != 1 || magic != MAGIC_RCP) break;
        if (fread(&key, 8, 1, f) != 1 || fread(&recipe, 4, 1, f) != 1) break;
        if (!grow((void **)&C.rcp, &C.caprcp, C.nrcp, sizeof *C.rcp)) break;
        C.rcp[C.nrcp].key = key; C.rcp[C.nrcp].recipe = recipe; C.nrcp++;
        good = ftell(f);
    }
    return good;
}

static void bin_forget(uint64_t key)
{
    int i;
    for (i = 0; i < C.nbin; i++)
        if (C.bin[i].key == key) { C.bin[i] = C.bin[--C.nbin]; i--; }
}

/* The binaries stay on disk: only where each one is, and a hash of it, which
 * nv2a_sc_bin_get checks -- a driver handed a damaged binary is not something
 * to count on catching it. */
static long load_binaries(FILE *f)
{
    long good = 0, end;
    if (fseek(f, 0, SEEK_END)) return 0;
    end = ftell(f);
    rewind(f);
    for (;;) {
        uint32_t magic, format, len; uint64_t key, hash; long off;
        if (fread(&magic, 4, 1, f) != 1) break;
        if (magic == MAGIC_DROP) {
            if (fread(&key, 8, 1, f) != 1) break;
            bin_forget(key);
        } else if (magic == MAGIC_BIN) {
            if (fread(&key, 8, 1, f) != 1 || fread(&format, 4, 1, f) != 1 || fread(&len, 4, 1, f) != 1 ||
                fread(&hash, 8, 1, f) != 1) break;
            if (len == 0 || len > MAX_BLOB) break;
            off = ftell(f);
            /* the file must hold all of it: a short last record ends the read */
            if (off < 0 || off + (long)len > end || fseek(f, (long)len, SEEK_CUR)) break;
            bin_forget(key);
            if (!grow((void **)&C.bin, &C.capbin, C.nbin, sizeof *C.bin)) break;
            C.bin[C.nbin].key = key; C.bin[C.nbin].hash = hash; C.bin[C.nbin].format = format;
            C.bin[C.nbin].len = len; C.bin[C.nbin].off = off;
            C.nbin++;
        } else {
            break;
        }
        good = ftell(f);
    }
    return good;
}

/* Open `path` for reading, load it with `load`, cut off a damaged tail, and
 * open it again for appending. The size goes to *size. */
static FILE *open_log(const char *path, long (*load)(FILE *), long *size)
{
    FILE *f = sc_fopen(path, "rb");
    long good = 0, end = 0;
    if (f) {
        good = load(f);
        if (!fseek(f, 0, SEEK_END)) end = ftell(f);
        fclose(f);
        if (end > good) {
            fprintf(stderr, "  [SHADERS] %s: %ld damaged bytes at the end dropped\n", path, end - good);
            sc_truncate(path, good);
        }
    }
    *size = good;
    return sc_fopen(path, "ab");
}

int nv2a_sc_open(const char *dir, const char *tag)
{
    int ok;
    lock();
    if (C.open) { unlock(); return 1; }
    sc_mkdir(dir);
    snprintf(C.pfile, sizeof C.pfile, "%s/programs-%s.dat", dir, tag);
    snprintf(C.rfile, sizeof C.rfile, "%s/recipes-%s.dat", dir, tag);
    C.pf = open_log(C.pfile, load_programs, &C.psize);
    { long rs; C.rf = open_log(C.rfile, load_recipes, &rs); }
    ok = C.pf != NULL;
    if (ok) C.open = 1;
    else fprintf(stderr, "  [SHADERS] cannot write %s: %s -- no shader cache\n", C.pfile, strerror(errno));
    unlock();
    if (ok)
        fprintf(stderr, "  [SHADERS] cache %s: %d programs from earlier runs (%d sources)\n",
                C.pfile, C.nprog, C.nsrc);
    return ok;
}

int nv2a_sc_is_open(void) { return C.open; }

int nv2a_sc_count(void)
{
    int n;
    lock(); n = C.nprog; unlock();
    return n;
}

int nv2a_sc_entry(int i, uint64_t *key, const char **vsrc, const char **fsrc)
{
    int ok = 0;
    lock();
    if (i >= 0 && i < C.nprog) {
        *key = C.prog[i].key;
        *vsrc = C.src[C.prog[i].vi].text;
        *fsrc = C.src[C.prog[i].fi].text;
        ok = 1;
    }
    unlock();
    return ok;
}

static int write_src(uint64_t h, const char *text, uint32_t len)
{
    uint32_t magic = MAGIC_SRC;
    if (fwrite(&magic, 4, 1, C.pf) != 1 || fwrite(&h, 8, 1, C.pf) != 1 ||
        fwrite(&len, 4, 1, C.pf) != 1 || (len && fwrite(text, 1, len, C.pf) != len)) return 0;
    C.psize += 16 + (long)len;
    return 1;
}

void nv2a_sc_add(uint64_t key, const char *vsrc, const char *fsrc)
{
    uint64_t vh, fh;
    uint32_t vl, fl, magic = MAGIC_PROG;
    int vi, fi, newv, newf;
    if (!C.open || !vsrc || !fsrc) return;
    vl = (uint32_t)strlen(vsrc); fl = (uint32_t)strlen(fsrc);
    if (vl > MAX_TEXT || fl > MAX_TEXT) return;
    vh = text_hash(vsrc); fh = text_hash(fsrc);
    lock();
    if (prog_find(key) >= 0 || !C.pf || C.pbad || C.psize + (long)vl + (long)fl + 64 > MAX_PROGRAMS_FILE) { unlock(); return; }
    newv = src_find(vh) < 0; newf = src_find(fh) < 0;
    vi = src_put(vh, vsrc, vl); fi = src_put(fh, fsrc, fl);
    if (vi < 0 || fi < 0 || !grow((void **)&C.prog, &C.capprog, C.nprog, sizeof *C.prog)) { unlock(); return; }
    C.prog[C.nprog].key = key; C.prog[C.nprog].vi = vi; C.prog[C.nprog].fi = fi;
    C.nprog++;
    C.added++;
    /* A record half written would end the file for the next run's read, and
     * everything after it with it: so after a failed write, nothing more. */
    if ((!newv || write_src(vh, vsrc, vl)) && (!newf || write_src(fh, fsrc, fl)) &&
        fwrite(&magic, 4, 1, C.pf) == 1 && fwrite(&key, 8, 1, C.pf) == 1 &&
        fwrite(&vh, 8, 1, C.pf) == 1 && fwrite(&fh, 8, 1, C.pf) == 1)
        C.psize += 28;
    else
        C.pbad = 1;
    if (fflush(C.pf)) C.pbad = 1;
    unlock();
}

void nv2a_sc_recipe_add(uint64_t key, uint32_t recipe)
{
    uint32_t magic = MAGIC_RCP;
    int i;
    if (!C.open) return;
    lock();
    for (i = 0; i < C.nrcp; i++)
        if (C.rcp[i].key == key && C.rcp[i].recipe == recipe) { unlock(); return; }
    if (grow((void **)&C.rcp, &C.caprcp, C.nrcp, sizeof *C.rcp)) {
        C.rcp[C.nrcp].key = key; C.rcp[C.nrcp].recipe = recipe; C.nrcp++;
        if (C.rf && !C.rbad) {
            if (fwrite(&magic, 4, 1, C.rf) != 1 || fwrite(&key, 8, 1, C.rf) != 1 ||
                fwrite(&recipe, 4, 1, C.rf) != 1 || fflush(C.rf)) C.rbad = 1;
        }
    }
    unlock();
}

int nv2a_sc_recipes(uint64_t key, uint32_t *out, int max)
{
    int i, n = 0;
    lock();
    for (i = 0; i < C.nrcp && n < max; i++)
        if (C.rcp[i].key == key) out[n++] = C.rcp[i].recipe;
    unlock();
    return n;
}

int nv2a_sc_bin_open(const char *driver)
{
    char *p;
    uint64_t h;
    if (!C.open || !driver) return 0;
    h = text_hash(driver);
    lock();
    if (C.bin_open) { unlock(); return 1; }
    /* the programs file's name, with the driver's hash in place of the kind */
    snprintf(C.bfile, sizeof C.bfile, "%s", C.pfile);
    p = strstr(C.bfile, "/programs-");
    if (p) {
        char tail[600];
        snprintf(tail, sizeof tail, "%s", p + 10);                  /* "<tag>.dat" */
        if (strlen(tail) > 4) tail[strlen(tail) - 4] = 0;
        snprintf(p, sizeof C.bfile - (size_t)(p - C.bfile), "/binaries-%s-%08X.dat",
                 tail, (unsigned)(h ^ (h >> 32)));
    }
    C.bf = open_log(C.bfile, load_binaries, &C.bsize);
    C.bin_open = C.bf != NULL;
    unlock();
    if (C.bin_open)
        fprintf(stderr, "  [SHADERS] compiled programs for this driver: %d (%s)\n", C.nbin, C.bfile);
    return C.bin_open;
}

int nv2a_sc_bin_has(uint64_t key)
{
    int i, has = 0;
    if (!C.bin_open) return 0;
    lock();
    for (i = 0; i < C.nbin; i++) if (C.bin[i].key == key) { has = 1; break; }
    unlock();
    return has;
}

void *nv2a_sc_bin_get(uint64_t key, uint32_t *len, uint32_t *format)
{
    void *data = NULL;
    int i;
    if (!C.bin_open) return NULL;
    lock();
    for (i = C.nbin - 1; i >= 0; i--)
        if (C.bin[i].key == key) {
            FILE *f = sc_fopen(C.bfile, "rb");
            if (f && !fseek(f, C.bin[i].off, SEEK_SET) && (data = malloc(C.bin[i].len)) != NULL &&
                fread(data, 1, C.bin[i].len, f) == C.bin[i].len &&
                fnv64(0xCBF29CE484222325ull, data, C.bin[i].len) == C.bin[i].hash) {
                *len = C.bin[i].len; *format = C.bin[i].format;
            } else {
                free(data); data = NULL;
            }
            if (f) fclose(f);
            break;
        }
    unlock();
    return data;
}

void nv2a_sc_bin_put(uint64_t key, const void *data, uint32_t len, uint32_t format)
{
    uint32_t magic = MAGIC_BIN;
    uint64_t hash;
    long off;
    if (!C.bin_open || !data || !len || len > MAX_BLOB) return;
    hash = fnv64(0xCBF29CE484222325ull, data, len);
    lock();
    if (C.bbad || C.bsize + (long)len + 28 > MAX_BINARIES_FILE ||
        !grow((void **)&C.bin, &C.capbin, C.nbin, sizeof *C.bin)) { unlock(); return; }
    off = C.bsize + 28;
    if (fwrite(&magic, 4, 1, C.bf) == 1 && fwrite(&key, 8, 1, C.bf) == 1 &&
        fwrite(&format, 4, 1, C.bf) == 1 && fwrite(&len, 4, 1, C.bf) == 1 &&
        fwrite(&hash, 8, 1, C.bf) == 1 && fwrite(data, 1, len, C.bf) == len) {
        bin_forget(key);
        C.bin[C.nbin].key = key; C.bin[C.nbin].hash = hash; C.bin[C.nbin].format = format;
        C.bin[C.nbin].len = len; C.bin[C.nbin].off = off;
        C.nbin++;
        C.bsize += 28 + (long)len;
    } else {
        C.bbad = 1;                  /* the offsets after it would be wrong */
    }
    if (fflush(C.bf)) C.bbad = 1;
    unlock();
}

void nv2a_sc_bin_drop(uint64_t key)
{
    uint32_t magic = MAGIC_DROP;
    if (!C.bin_open) return;
    lock();
    bin_forget(key);
    if (!C.bbad) {
        if (fwrite(&magic, 4, 1, C.bf) == 1 && fwrite(&key, 8, 1, C.bf) == 1) C.bsize += 12;
        else C.bbad = 1;
        if (fflush(C.bf)) C.bbad = 1;
    }
    unlock();
}

void nv2a_sc_stats(int *programs, int *binaries, int *added)
{
    lock();
    if (programs) *programs = C.nprog;
    if (binaries) *binaries = C.nbin;
    if (added) *added = C.added;
    unlock();
}
