/*
 * Unpacking an Xbox disc image (XDVDFS) -- see xiso.h.
 *
 * The layout, as extract-xiso and xemu read it:
 *
 *   - Sectors are 2 KB. 64 KB into the game partition is the volume
 *     descriptor: "MICROSOFT*XBOX*MEDIA", the root directory's first sector
 *     and size (little-endian), a timestamp, and the same twenty bytes again
 *     at the end of the sector.
 *   - The game partition starts at 0 in an XISO and further in on a full
 *     disc image, which has a video partition first: 0x18300000 on an
 *     original Xbox disc (0xFD90000 and 0x2080000 are the two 360 layouts,
 *     tried as well since images get mislabelled).
 *   - A directory is a table of entries forming a binary tree: left and
 *     right child as offsets into the table in 4-byte units (0 = none), the
 *     first sector and size of the file or subdirectory, attributes (0x10 =
 *     directory), the name's length, the name. 0xFFFF where an entry would
 *     start is padding: an empty directory, or the end of a sector.
 *
 * The image is somebody's download, so nothing in it is trusted: every
 * offset and size is checked against the table and the file it points into,
 * names that would leave the destination ("..", a slash) are refused, and
 * the walk is bounded in depth and in entries so a looping tree cannot hang
 * it. The right-hand chain is followed in a loop rather than by recursion.
 */
#include "xiso.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
/* Windows (the launcher unpacks a disc image with this too): the same calls
 * in binary mode, with 64-bit offsets -- a full disc image is 7.8 GB -- and
 * UTF-8 paths through the wide-character file API. */
#include <io.h>
#include <direct.h>
#include <windows.h>
typedef long long x_ssize;
static int x_wide(const char *path, wchar_t *w, int n)
{
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n) > 0;
}
static int x_open(const char *path, int flags, int mode)
{
    wchar_t w[2048];
    (void)mode;
    if (!x_wide(path, w, 2048)) { errno = ENOENT; return -1; }
    return _wopen(w, flags | _O_BINARY, _S_IREAD | _S_IWRITE);
}
static int x_mkdir(const char *path, int mode)
{
    wchar_t w[2048];
    (void)mode;
    if (!x_wide(path, w, 2048)) { errno = ENOENT; return -1; }
    return _wmkdir(w);
}
static x_ssize x_pread(int fd, void *b, size_t n, uint64_t off)
{
    if (_lseeki64(fd, (long long)off, SEEK_SET) < 0) return -1;
    return _read(fd, b, (unsigned)(n > (1u << 30) ? (1u << 30) : n));
}
static int x_fsize(int fd, uint64_t *size)
{
    struct _stat64 st;
    if (_fstat64(fd, &st)) return -1;
    *size = (uint64_t)st.st_size;
    return 0;
}
#define open(path, flags, ...) x_open((path), (flags), 0)
#define mkdir(path, mode)      x_mkdir((path), (mode))
#define write(fd, b, n)        _write((fd), (b), (unsigned)(n))
#define close(fd)              _close(fd)
#else
#include <unistd.h>
typedef ssize_t x_ssize;
#define x_pread(fd, b, n, off) pread((fd), (b), (n), (off_t)(off))
static int x_fsize(int fd, uint64_t *size)
{
    struct stat st;
    if (fstat(fd, &st)) return -1;
    *size = (uint64_t)st.st_size;
    return 0;
}
#endif

#define XISO_SECTOR        2048u
#define XISO_HEADER_OFFSET 0x10000u
#define XISO_ATTR_DIR      0x10u
#define XISO_MAX_DEPTH     32
#define XISO_MAX_ENTRIES   200000u
#define XISO_MAX_TABLE     (16u << 20)
#define XISO_COPY_CHUNK    (1u << 20)

static const char k_magic[20] = { 'M','I','C','R','O','S','O','F','T','*',
                                  'X','B','O','X','*','M','E','D','I','A' };
static const uint64_t k_partitions[] = { 0, 0x18300000ull, 0xFD90000ull, 0x2080000ull };

typedef struct {
    int fd;
    uint64_t base, size;
    xiso_progress_fn progress;
    void *ctx;
    uint64_t done, total, next_note;
    unsigned entries;
    int counting;                 /* first pass: sizes only, nothing written */
    unsigned char *buf;
    char *err;
    size_t errlen;
} Xiso;

static int fail(Xiso *x, const char *fmt, ...)
{
    va_list ap;
    if (x->err && x->errlen && !x->err[0]) {
        va_start(ap, fmt);
        vsnprintf(x->err, x->errlen, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_at(int fd, void *b, size_t n, uint64_t off)
{
    size_t got = 0;
    while (got < n) {
        x_ssize r = x_pread(fd, (char *)b + got, n - got, off + got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static int write_all(int fd, const void *b, size_t n)
{
    size_t put = 0;
    while (put < n) {
        x_ssize w = write(fd, (const char *)b + put, n - put);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        put += (size_t)w;
    }
    return 0;
}

/* The game partition's offset and root directory, or 0 if this is not an
 * Xbox disc image. */
static int find_volume(int fd, uint64_t size, uint64_t *base,
                       uint32_t *root_sector, uint32_t *root_size)
{
    unsigned char h[XISO_SECTOR];
    size_t i;
    for (i = 0; i < sizeof k_partitions / sizeof k_partitions[0]; i++) {
        uint64_t b = k_partitions[i];
        if (b + XISO_HEADER_OFFSET + XISO_SECTOR > size) continue;
        if (read_at(fd, h, sizeof h, b + XISO_HEADER_OFFSET)) continue;
        if (memcmp(h, k_magic, sizeof k_magic) || memcmp(h + 0x7EC, k_magic, sizeof k_magic))
            continue;
        *base = b;
        *root_sector = rd32(h + 20);
        *root_size = rd32(h + 24);
        return 1;
    }
    return 0;
}

static int note(Xiso *x)
{
    if (!x->progress || x->done < x->next_note) return 0;
    x->next_note = x->done + (8u << 20);
    return x->progress(x->ctx, x->done, x->total);
}

static int copy_file(Xiso *x, uint32_t sector, uint32_t size, const char *path)
{
    uint64_t src = x->base + (uint64_t)sector * XISO_SECTOR, left = size;
    int out;
    if (src + size > x->size)
        return fail(x, "The disc image is shorter than its own directory says "
                       "(a download that did not finish?).");
    if (x->counting) { x->total += size; return 0; }
    out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) return fail(x, "Could not create %s: %s", path, strerror(errno));
    while (left) {
        size_t n = left < XISO_COPY_CHUNK ? (size_t)left : XISO_COPY_CHUNK;
        if (read_at(x->fd, x->buf, n, src)) {
            close(out);
            return fail(x, "Could not read the disc image: %s", strerror(errno));
        }
        if (write_all(out, x->buf, n)) {
            int e = errno;
            close(out);
            return fail(x, e == ENOSPC ? "Not enough free space to unpack the game "
                                         "(it needs about as much again as the image)."
                                       : "Could not write %s: %s", path, strerror(e));
        }
        src += n; left -= n; x->done += n;
        if (note(x)) { close(out); return fail(x, "Cancelled."); }
    }
    if (close(out)) return fail(x, "Could not write %s: %s", path, strerror(errno));
    return 0;
}

static int walk_dir(Xiso *x, uint32_t sector, uint32_t size, const char *path, int depth);

/* The entry at off and everything to its left and right, in order. */
static int walk_entries(Xiso *x, const unsigned char *tab, uint32_t len, uint32_t off,
                        const char *path, int depth, int left)
{
    char *child = NULL;
    size_t plen = strlen(path);
    int rc = 0;

    for (;;) {
        uint16_t l, r;
        uint32_t sec, fsize;
        unsigned attr, nlen, i;

        if (++x->entries > XISO_MAX_ENTRIES) { rc = fail(x, "The disc image's directory does not end (damaged image?)."); break; }
        if ((uint64_t)off + 14 > len) { rc = fail(x, "A directory entry lies outside its directory (damaged image?)."); break; }
        l = rd16(tab + off);
        r = rd16(tab + off + 2);
        if (l == 0xFFFF) break;                         /* padding: nothing here */
        sec = rd32(tab + off + 4);
        fsize = rd32(tab + off + 8);
        attr = tab[off + 12];
        nlen = tab[off + 13];
        if ((uint64_t)off + 14 + nlen > len) { rc = fail(x, "A file name runs past its directory (damaged image?)."); break; }

        /* Left: recursion, bounded -- a tree written by any tool is close to
         * balanced (a few dozen levels for thousands of files). */
        if (l && left >= 1024) { rc = fail(x, "The disc image's directory is malformed."); break; }
        if (l && (rc = walk_entries(x, tab, len, (uint32_t)l * 4u, path, depth, left + 1))) break;

        /* This entry. A name that is empty, "." or "..", or has a slash or a
         * NUL in it would land somewhere other than inside dest: refused. */
        if (!nlen || (nlen == 1 && tab[off + 14] == '.')
            || (nlen == 2 && tab[off + 14] == '.' && tab[off + 15] == '.')) {
            rc = fail(x, "The disc image has a file name that is not allowed.");
            break;
        }
        for (i = 0; i < nlen; i++) {
            unsigned char c = tab[off + 14 + i];
            if (c == '/' || c == '\\' || c == 0) break;
        }
        if (i < nlen) { rc = fail(x, "The disc image has a file name that is not allowed."); break; }
        free(child);
        child = (char *)malloc(plen + 1 + nlen + 1);
        if (!child) { rc = fail(x, "Out of memory."); break; }
        memcpy(child, path, plen);
        child[plen] = '/';
        memcpy(child + plen + 1, tab + off + 14, nlen);
        child[plen + 1 + nlen] = 0;

        if (attr & XISO_ATTR_DIR) {
            if (!x->counting && mkdir(child, 0755) && errno != EEXIST) {
                rc = fail(x, "Could not create the folder %s: %s", child, strerror(errno));
                break;
            }
            if (fsize && (rc = walk_dir(x, sec, fsize, child, depth + 1))) break;
        } else if ((rc = copy_file(x, sec, fsize, child))) {
            break;
        }

        if (!r) break;
        off = (uint32_t)r * 4u;                         /* the right-hand chain: a loop */
    }
    free(child);
    return rc;
}

static int walk_dir(Xiso *x, uint32_t sector, uint32_t size, const char *path, int depth)
{
    uint64_t at = x->base + (uint64_t)sector * XISO_SECTOR;
    unsigned char *tab;
    int rc;
    if (depth > XISO_MAX_DEPTH) return fail(x, "The disc image's folders are nested too deeply (damaged image?).");
    if (!size) return 0;
    if (size > XISO_MAX_TABLE || at + size > x->size)
        return fail(x, "A directory lies outside the disc image (damaged or incomplete image?).");
    tab = (unsigned char *)malloc(size);
    if (!tab) return fail(x, "Out of memory.");
    if (read_at(x->fd, tab, size, at)) { free(tab); return fail(x, "Could not read the disc image: %s", strerror(errno)); }
    rc = walk_entries(x, tab, size, 0, path, depth, 0);
    free(tab);
    return rc;
}

static int mkdirs(const char *dir)
{
    char *p = strdup(dir), *s;
    int rc = 0;
    if (!p) return -1;
    for (s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(p, 0755) && errno != EEXIST) rc = -1;
        *s = '/';
    }
    if (mkdir(p, 0755) && errno != EEXIST) rc = -1;
    free(p);
    return rc;
}
/* The image: a path, or "fd:N" for one the caller already has open -- an
 * Android app gets a descriptor from the system's file picker, not a path. A
 * duplicate, so it is closed like any other. */
static int open_image(const char *iso_path)
{
#if !defined(_WIN32)
    if (!strncmp(iso_path, "fd:", 3)) return dup(atoi(iso_path + 3));
#endif
    return open(iso_path, O_RDONLY);
}


long xiso_head(const char *path, unsigned char *buf, size_t n)
{
    x_ssize r;
    int fd = open_image(path);
    if (fd < 0) return -1;
    r = x_pread(fd, buf, n, 0);
    close(fd);
    return (long)r;
}

int xiso_probe(const char *iso_path)
{
    uint64_t base, size;
    uint32_t rs, rz;
    int fd = open_image(iso_path), ok;
    if (fd < 0) return 0;
    ok = x_fsize(fd, &size) == 0 && find_volume(fd, size, &base, &rs, &rz);
    close(fd);
    return ok;
}

/* The entry called name in a directory table, searched the way walk_entries
 * walks it (every entry, not by the tree's order: a table is small, and a
 * tool that sorted it differently would still be found): 0 and its sector,
 * size and attributes if it is there, 1 if not, -1 if the table is damaged. */
static int find_entry(Xiso *x, const unsigned char *tab, uint32_t len, uint32_t off,
                      const char *name, uint32_t *sec, uint32_t *fsize, unsigned *attr, int left)
{
    size_t want = strlen(name);
    for (;;) {
        uint16_t l, r;
        unsigned nlen, i;
        int rc;
        if (++x->entries > XISO_MAX_ENTRIES) return fail(x, "The disc image's directory does not end (damaged image?).");
        if ((uint64_t)off + 14 > len) return fail(x, "A directory entry lies outside its directory (damaged image?).");
        l = rd16(tab + off);
        r = rd16(tab + off + 2);
        if (l == 0xFFFF) return 1;                      /* padding: nothing here */
        nlen = tab[off + 13];
        if ((uint64_t)off + 14 + nlen > len) return fail(x, "A file name runs past its directory (damaged image?).");
        if (nlen == want) {
            for (i = 0; i < nlen; i++)
                if (toupper(tab[off + 14 + i]) != toupper((unsigned char)name[i])) break;
            if (i == nlen) {
                *sec = rd32(tab + off + 4);
                *fsize = rd32(tab + off + 8);
                *attr = tab[off + 12];
                return 0;
            }
        }
        if (l) {
            if (left >= 1024) return fail(x, "The disc image's directory is malformed.");
            rc = find_entry(x, tab, len, (uint32_t)l * 4u, name, sec, fsize, attr, left + 1);
            if (rc <= 0) return rc;
        }
        if (!r) return 1;
        off = (uint32_t)r * 4u;
    }
}

int xiso_read_file(const char *iso_path, const char *name,
                   unsigned char *buf, size_t bufsize, size_t *got, uint64_t *size,
                   char *err, size_t errlen)
{
    Xiso x;
    uint32_t root_sector, root_size, sec = 0, fsize = 0;
    unsigned attr = 0;
    uint64_t at;
    unsigned char *tab;
    size_t n;
    int rc;

    memset(&x, 0, sizeof x);
    x.err = err; x.errlen = errlen;
    if (err && errlen) err[0] = 0;
    if (got) *got = 0;
    if (size) *size = 0;
    x.fd = open_image(iso_path);
    if (x.fd < 0) return fail(&x, "Could not open the file: %s", strerror(errno));
    if (x_fsize(x.fd, &x.size)) { close(x.fd); return fail(&x, "Could not read the file: %s", strerror(errno)); }
    if (!find_volume(x.fd, x.size, &x.base, &root_sector, &root_size)) {
        close(x.fd);
        return fail(&x, "That file is not an Xbox disc image.");
    }
    at = x.base + (uint64_t)root_sector * XISO_SECTOR;
    if (!root_size) { close(x.fd); return 1; }
    if (root_size > XISO_MAX_TABLE || at + root_size > x.size) {
        close(x.fd);
        return fail(&x, "The disc image's top folder lies outside it (damaged or incomplete image?).");
    }
    tab = (unsigned char *)malloc(root_size);
    if (!tab) { close(x.fd); return fail(&x, "Out of memory."); }
    if (read_at(x.fd, tab, root_size, at)) {
        free(tab); close(x.fd);
        return fail(&x, "Could not read the disc image: %s", strerror(errno));
    }
    rc = find_entry(&x, tab, root_size, 0, name, &sec, &fsize, &attr, 0);
    free(tab);
    if (rc) { close(x.fd); return rc; }
    if (attr & XISO_ATTR_DIR) { close(x.fd); return 1; }
    at = x.base + (uint64_t)sec * XISO_SECTOR;
    if (at + fsize > x.size) {
        close(x.fd);
        return fail(&x, "The disc image is shorter than its own directory says "
                        "(a download that did not finish?).");
    }
    n = fsize < bufsize ? (size_t)fsize : bufsize;
    if (n && read_at(x.fd, buf, n, at)) {
        close(x.fd);
        return fail(&x, "Could not read the disc image: %s", strerror(errno));
    }
    close(x.fd);
    if (got) *got = n;
    if (size) *size = fsize;
    return 0;
}

int xiso_extract(const char *iso_path, const char *dest_dir,
                 xiso_progress_fn progress, void *ctx, char *err, size_t errlen)
{
    Xiso x;
    uint32_t root_sector, root_size;
    int rc;

    memset(&x, 0, sizeof x);
    x.err = err; x.errlen = errlen;
    if (err && errlen) err[0] = 0;
    x.progress = progress; x.ctx = ctx;
    x.fd = open_image(iso_path);
    if (x.fd < 0) return fail(&x, "Could not open the disc image: %s", strerror(errno));
    if (x_fsize(x.fd, &x.size)) { close(x.fd); return fail(&x, "Could not read the disc image: %s", strerror(errno)); }
    if (!find_volume(x.fd, x.size, &x.base, &root_sector, &root_size)) {
        close(x.fd);
        return fail(&x, "That file is not an Xbox disc image.");
    }
    x.buf = (unsigned char *)malloc(XISO_COPY_CHUNK);
    if (!x.buf) { close(x.fd); return fail(&x, "Out of memory."); }

    /* Twice: once for the total (the progress bar's 100%), once to write. */
    x.counting = 1;
    rc = walk_dir(&x, root_sector, root_size, dest_dir, 0);
    if (!rc && !x.total) rc = fail(&x, "The disc image has no files in it.");
    if (!rc) {
        x.counting = 0;
        x.entries = 0;
        if (mkdirs(dest_dir)) rc = fail(&x, "Could not create %s: %s", dest_dir, strerror(errno));
        else rc = walk_dir(&x, root_sector, root_size, dest_dir, 0);
    }
    if (!rc && progress) progress(ctx, x.total, x.total);
    free(x.buf);
    close(x.fd);
    return rc;
}

#ifdef XISO_MAIN
/* A command-line wrapper, for checking this against extract-xiso:
 *   cc -DXISO_MAIN -o xiso xiso.c && ./xiso game.iso outdir */
static int show(void *ctx, uint64_t done, uint64_t total)
{
    (void)ctx;
    fprintf(stderr, "\r%llu / %llu MB", (unsigned long long)(done >> 20),
            (unsigned long long)(total >> 20));
    return 0;
}
int main(int argc, char **argv)
{
    char err[512];
    if (argc != 3) { fprintf(stderr, "usage: %s image.iso dest\n", argv[0]); return 2; }
    if (xiso_extract(argv[1], argv[2], show, NULL, err, sizeof err)) {
        fprintf(stderr, "\n%s\n", err);
        return 1;
    }
    fprintf(stderr, "\ndone\n");
    return 0;
}
#endif
