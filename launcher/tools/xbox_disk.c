/*
 * Reading an Xbox hard disk image, and the FATX filesystems on it -- see
 * xbox_disk.h.
 *
 * qcow2, as QEMU's docs/interop/qcow2.txt lays it out:
 *
 *   - A big-endian header: "QFI\xfb", the version (2 or 3), the backing
 *     file's offset (0: none), the cluster size as a power of two, the
 *     disk's length, the encryption method, and the L1 table's length and
 *     offset. Version 3 adds three sets of feature bits and the header's
 *     length; past 104 bytes, the compression type. Of the incompatible
 *     features only "dirty" is taken: it says the reference counts may be
 *     stale, and a reader never looks at them.
 *   - A guest offset splits into an L1 index, an L2 index and the offset in
 *     the cluster. An L1 entry holds an L2 table's offset, 0 when none was
 *     made; an L2 entry holds a cluster's. 0 is a cluster never written,
 *     which reads as zeros as there is no backing file; bit 0 (version 3)
 *     one written as zeros; bit 62 a compressed one, whose low bits are its
 *     offset and whose next bits up to 61 are how many 512-byte sectors it
 *     runs on past its first. Its data is a raw deflate stream.
 *
 * A raw image is the disk, byte for byte.
 *
 * FATX, as the console formats a partition:
 *
 *   - 4 KB of superblock: "FATX", the volume's id, sectors per cluster, and
 *     the root directory's first cluster, little-endian.
 *   - The FAT from 4 KB on: an entry for each cluster, and one before them,
 *     16 bits each when the partition holds fewer than 0xFFF0 clusters and
 *     32 bits otherwise, rounded up to 4 KB. An entry gives the next cluster
 *     of its chain; 0 is a free cluster, and 0xFFF0 (0xFFFFFFF0) and up end
 *     a chain.
 *   - The clusters, numbered from 1, after the FAT.
 *   - A directory is a chain of 64-byte entries: the name's length (0x00 or
 *     0xFF ends the directory, 0xE5 marks a deleted entry), the attributes
 *     (0x10 a directory), 42 bytes of name, the first cluster, the size, and
 *     the FAT time and date it was written, made and read.
 *
 * The image is the player's own file and may be damaged, or not a disk at
 * all, so nothing in it is trusted: every offset and size is checked
 * against the image and the partition, and a chain that comes back to a
 * cluster it has been through is damaged, so a loop in the FAT can't hang a
 * read or repeat a file's clusters.
 */
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#include "xbox_disk.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "../../third_party/miniz/miniz.h"

/* ---- small helpers ---------------------------------------------------------- */

static void say(char *err, size_t errn, const char *fmt, ...)
{
    va_list ap;
    if (!err || !errn) return;
    va_start(ap, fmt);
    vsnprintf(err, errn, fmt, ap);
    va_end(ap);
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t be64(const uint8_t *p)
{
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | le16(p + 2) << 16;
}

static int seek_to(FILE *f, uint64_t off)
{
#if defined(_WIN32)
    return _fseeki64(f, (long long)off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}

static int file_length(FILE *f, uint64_t *len)
{
#if defined(_WIN32)
    long long at;
    if (_fseeki64(f, 0, SEEK_END)) return -1;
    at = _ftelli64(f);
#else
    off_t at;
    if (fseeko(f, 0, SEEK_END)) return -1;
    at = ftello(f);
#endif
    if (at < 0) return -1;
    *len = (uint64_t)at;
    return 0;
}

/* n bytes of the image file at off: 0, or -1 short of them. */
static int file_read(FILE *f, uint64_t off, void *buf, size_t n)
{
    if (seek_to(f, off)) return -1;
    return fread(buf, 1, n, f) == n ? 0 : -1;
}

/* ---- qcow2 ------------------------------------------------------------------ */

#define QCOW2_MAGIC            0x514649FBu   /* "QFI\xfb" */
#define QCOW2_HEADER_V2        72u           /* a version 2 header's length */
#define QCOW2_HEADER_V3        104u          /* a version 3 header's least length */
#define QCOW2_MIN_CLUSTER_BITS 9u
#define QCOW2_MAX_CLUSTER_BITS 21u
#define QCOW2_INCOMPAT_DIRTY   (1ull << 0)
#define QCOW2_OFFSET_MASK      0x00FFFFFFFFFFFE00ull   /* bits 9 to 55 */
#define QCOW2_COMPRESSED       (1ull << 62)
#define QCOW2_ZERO             (1ull << 0)
#define QCOW2_SECTOR           512u

struct xbox_disk {
    FILE     *f;
    uint64_t  file_len;
    uint64_t  size;          /* the guest disk's length */
    int       qcow2;
    /* qcow2 only */
    unsigned  cluster_bits;
    uint64_t  cluster;       /* bytes */
    uint64_t  l2_entries;    /* per L2 table */
    uint64_t *l1;
    uint32_t  l1_size;
    uint64_t *l2;            /* the L2 table last read, and where it was */
    uint64_t  l2_at;
    uint8_t  *data;          /* the cluster last read, and which */
    uint64_t  data_index;
    int       data_valid;
    uint8_t  *packed;        /* a compressed cluster's bytes, before inflating */
};

static int qcow2_open(xbox_disk *d, const uint8_t *h, size_t hn, char *err, size_t errn)
{
    uint32_t version = be32(h + 4), header_len = QCOW2_HEADER_V2;
    uint64_t backing = be64(h + 8), l1_at = be64(h + 40), table_bytes;
    uint32_t crypt = be32(h + 32);
    uint32_t i;

    if (version != 2 && version != 3) {
        say(err, errn, "the hard disk image is qcow2 version %u, which can't be read", version);
        return -1;
    }
    d->cluster_bits = be32(h + 20);
    if (d->cluster_bits < QCOW2_MIN_CLUSTER_BITS || d->cluster_bits > QCOW2_MAX_CLUSTER_BITS) {
        say(err, errn, "the hard disk image's clusters are an unknown size (2^%u bytes)", d->cluster_bits);
        return -1;
    }
    if (backing) {
        say(err, errn, "the hard disk image is a qcow2 image on top of another one, which can't be read");
        return -1;
    }
    if (crypt) {
        say(err, errn, "the hard disk image is encrypted");
        return -1;
    }
    if (version == 3) {
        uint64_t incompat;
        if (hn < QCOW2_HEADER_V3) {
            say(err, errn, "the hard disk image's header is cut short");
            return -1;
        }
        incompat = be64(h + 72);
        header_len = be32(h + 100);
        if (incompat & ~QCOW2_INCOMPAT_DIRTY) {
            say(err, errn, "the hard disk image uses qcow2 features that can't be read (0x%llx)",
                (unsigned long long)(incompat & ~QCOW2_INCOMPAT_DIRTY));
            return -1;
        }
        if (header_len > QCOW2_HEADER_V3 && hn > QCOW2_HEADER_V3 && h[QCOW2_HEADER_V3] != 0) {
            say(err, errn, "the hard disk image is compressed in a way that can't be read");
            return -1;
        }
    }
    (void)header_len;

    d->qcow2 = 1;
    d->cluster = 1ull << d->cluster_bits;
    d->l2_entries = d->cluster / 8;
    d->size = be64(h + 24);
    d->l1_size = be32(h + 36);
    /* Enough L1 entries for the whole disk, and the table inside the file. */
    if ((uint64_t)d->l1_size * d->l2_entries * d->cluster < d->size
        || (l1_at & (d->cluster - 1)) || l1_at >= d->file_len
        || (uint64_t)d->l1_size * 8 > d->file_len - l1_at) {
        say(err, errn, "the hard disk image's tables don't fit it");
        return -1;
    }
    table_bytes = (uint64_t)d->l1_size * 8;
    d->l1 = malloc(table_bytes ? (size_t)table_bytes : 1);
    d->l2 = malloc((size_t)d->cluster);
    d->data = malloc((size_t)d->cluster);
    d->packed = malloc((size_t)d->cluster + QCOW2_SECTOR);
    if (!d->l1 || !d->l2 || !d->data || !d->packed) {
        say(err, errn, "out of memory reading the hard disk image");
        return -1;
    }
    if (table_bytes && file_read(d->f, l1_at, d->l1, (size_t)table_bytes)) {
        say(err, errn, "the hard disk image can't be read");
        return -1;
    }
    for (i = 0; i < d->l1_size; i++) d->l1[i] = be64((const uint8_t *)&d->l1[i]);
    return 0;
}

/* The guest's cluster `index` into d->data. 0, or -1 when the image is
 * damaged there. */
static int qcow2_cluster(xbox_disk *d, uint64_t index)
{
    uint64_t l1i = index / d->l2_entries, l2i = index % d->l2_entries, l2_at, e;

    if (d->data_valid && d->data_index == index) return 0;
    d->data_valid = 0;
    if (l1i >= d->l1_size) return -1;
    l2_at = d->l1[l1i] & QCOW2_OFFSET_MASK;
    if (!l2_at) {
        memset(d->data, 0, (size_t)d->cluster);
        goto done;
    }
    if (l2_at != d->l2_at) {
        uint64_t i;
        if (l2_at > d->file_len || d->cluster > d->file_len - l2_at
            || file_read(d->f, l2_at, d->l2, (size_t)d->cluster)) return -1;
        for (i = 0; i < d->l2_entries; i++) d->l2[i] = be64((const uint8_t *)&d->l2[i]);
        d->l2_at = l2_at;
    }
    e = d->l2[l2i];
    if (e & QCOW2_COMPRESSED) {
        unsigned x = 62 - (d->cluster_bits - 8);
        uint64_t at = e & ((1ull << x) - 1);
        uint64_t sectors = ((e & ~QCOW2_COMPRESSED & ((1ull << 62) - 1)) >> x) + 1;
        uint64_t len = sectors * QCOW2_SECTOR - (at & (QCOW2_SECTOR - 1));
        size_t got;
        if (at >= d->file_len) return -1;
        if (len > d->cluster + QCOW2_SECTOR) len = d->cluster + QCOW2_SECTOR;
        if (len > d->file_len - at) len = d->file_len - at;   /* the last one may end the file */
        if (file_read(d->f, at, d->packed, (size_t)len)) return -1;
        got = tinfl_decompress_mem_to_mem(d->data, (size_t)d->cluster, d->packed, (size_t)len, 0);
        if (got != d->cluster) return -1;
    } else {
        uint64_t at = e & QCOW2_OFFSET_MASK;
        if (!at || (e & QCOW2_ZERO)) {
            memset(d->data, 0, (size_t)d->cluster);
        } else if (at > d->file_len || d->cluster > d->file_len - at
                   || file_read(d->f, at, d->data, (size_t)d->cluster)) {
            return -1;
        }
    }
done:
    d->data_index = index;
    d->data_valid = 1;
    return 0;
}

/* ---- the image -------------------------------------------------------------- */

xbox_disk *xbox_disk_open(FILE *f, char *err, size_t errn)
{
    uint8_t h[QCOW2_HEADER_V3 + 8];
    size_t hn;
    xbox_disk *d;

    if (!f) {
        say(err, errn, "no hard disk image");
        return NULL;
    }
    d = calloc(1, sizeof *d);
    if (!d) {
        say(err, errn, "out of memory reading the hard disk image");
        return NULL;
    }
    d->f = f;
    d->l2_at = 0;
    if (file_length(f, &d->file_len) || seek_to(f, 0)) {
        say(err, errn, "the hard disk image can't be read");
        xbox_disk_close(d);
        return NULL;
    }
    hn = fread(h, 1, sizeof h, f);
    if (hn >= QCOW2_HEADER_V2 && be32(h) == QCOW2_MAGIC) {
        if (qcow2_open(d, h, hn, err, errn)) {
            xbox_disk_close(d);
            return NULL;
        }
    } else {
        d->size = d->file_len;
    }
    return d;
}

uint64_t xbox_disk_size(const xbox_disk *d)
{
    return d ? d->size : 0;
}

int xbox_disk_is_qcow2(const xbox_disk *d)
{
    return d && d->qcow2;
}

int xbox_disk_read(xbox_disk *d, uint64_t off, void *buf, size_t n)
{
    uint8_t *out = buf;

    if (!d || off > d->size || n > d->size - off) return -1;
    if (!d->qcow2) return n ? file_read(d->f, off, buf, n) : 0;
    while (n) {
        uint64_t index = off >> d->cluster_bits, in = off & (d->cluster - 1);
        size_t take = (size_t)(d->cluster - in < n ? d->cluster - in : n);
        if (qcow2_cluster(d, index)) return -1;
        memcpy(out, d->data + in, take);
        out += take;
        off += take;
        n -= take;
    }
    return 0;
}

void xbox_disk_close(xbox_disk *d)
{
    if (!d) return;
    free(d->l1);
    free(d->l2);
    free(d->data);
    free(d->packed);
    free(d);
}

/* ---- FATX ------------------------------------------------------------------- */

#define FATX_MAGIC             "FATX"
#define FATX_SUPERBLOCK        0x1000u
#define FATX_FAT_ALIGN         0x1000u
#define FATX_MAX_SECTORS_PER_CLUSTER 1024u
#define FATX_FAT16_LIMIT       0xFFF0u     /* clusters below this: 16-bit FAT */
#define FATX_END16             0xFFF0u     /* entries from these up end a chain */
#define FATX_END32             0xFFFFFFF0u
#define FATX_DIRENT            64u
#define FATX_DIRENT_END        0x00u
#define FATX_DIRENT_END2       0xFFu
#define FATX_DIRENT_DELETED    0xE5u
#define FATX_MAX_DEPTH         32

struct fatx {
    xbox_disk *d;
    uint64_t   base, length;
    uint32_t   root;
    uint32_t   cluster;      /* bytes */
    uint64_t   clusters;     /* how many the partition holds */
    uint64_t   data;         /* the first cluster's offset in the partition */
    int        wide;         /* a 32-bit FAT */
    uint8_t   *fat;
    uint8_t   *buf;          /* one cluster */
};

fatx *fatx_open(xbox_disk *d, uint64_t base, uint64_t length, char *err, size_t errn)
{
    uint8_t sb[16];
    uint32_t spc;
    uint64_t entries, fat_bytes;
    fatx *f;

    if (!d || base > xbox_disk_size(d) || length > xbox_disk_size(d) - base
        || length < FATX_SUPERBLOCK) {
        say(err, errn, "the disk is too small to hold that partition");
        return NULL;
    }
    if (xbox_disk_read(d, base, sb, sizeof sb)) {
        say(err, errn, "the partition can't be read");
        return NULL;
    }
    if (memcmp(sb, FATX_MAGIC, 4)) {
        say(err, errn, "the partition isn't formatted (no FATX filesystem)");
        return NULL;
    }
    spc = le32(sb + 8);
    if (!spc || spc > FATX_MAX_SECTORS_PER_CLUSTER || (spc & (spc - 1))) {
        say(err, errn, "the partition's clusters are an unknown size (%u sectors)", spc);
        return NULL;
    }
    f = calloc(1, sizeof *f);
    if (!f) {
        say(err, errn, "out of memory reading the partition");
        return NULL;
    }
    f->d = d;
    f->base = base;
    f->length = length;
    f->root = le32(sb + 12);
    f->cluster = spc * (uint32_t)XBOX_DISK_SECTOR;
    entries = length / f->cluster;
    f->wide = entries >= FATX_FAT16_LIMIT;
    fat_bytes = (entries + 1) * (f->wide ? 4u : 2u);
    fat_bytes = (fat_bytes + FATX_FAT_ALIGN - 1) / FATX_FAT_ALIGN * FATX_FAT_ALIGN;
    f->data = FATX_SUPERBLOCK + fat_bytes;
    if (f->data >= length) {
        say(err, errn, "the partition's FAT doesn't fit it");
        fatx_close(f);
        return NULL;
    }
    f->clusters = (length - f->data) / f->cluster;
    if (f->root < 1 || f->root > f->clusters) {
        say(err, errn, "the partition's root directory is outside it");
        fatx_close(f);
        return NULL;
    }
    f->fat = malloc((size_t)fat_bytes);
    f->buf = malloc(f->cluster);
    if (!f->fat || !f->buf) {
        say(err, errn, "out of memory reading the partition");
        fatx_close(f);
        return NULL;
    }
    if (xbox_disk_read(d, base + FATX_SUPERBLOCK, f->fat, (size_t)fat_bytes)) {
        say(err, errn, "the partition's FAT can't be read");
        fatx_close(f);
        return NULL;
    }
    return f;
}

/* The cluster after c in its chain: 0 at the chain's end, and also for a
 * free or out-of-range entry, which a damaged FAT can hold. */
static uint32_t fatx_next(const fatx *f, uint32_t c)
{
    uint32_t n;
    if (c < 1 || c > f->clusters) return 0;
    if (f->wide) {
        n = le32(f->fat + (size_t)c * 4);
        if (n >= FATX_END32) return 0;
    } else {
        n = le16(f->fat + (size_t)c * 2);
        if (n >= FATX_END16) return 0;
    }
    return n >= 1 && n <= f->clusters ? n : 0;
}

/* A chain walked once: each cluster marked as it is visited, so a FAT that
 * loops back is found the first time a cluster comes round again. NULL when
 * out of memory. */
static uint8_t *fatx_seen(const fatx *f)
{
    return calloc((size_t)(f->clusters / 8 + 1), 1);
}

static int fatx_visit(uint8_t *seen, uint32_t c)
{
    uint8_t bit = (uint8_t)(1u << (c & 7));
    if (seen[c >> 3] & bit) return -1;
    seen[c >> 3] |= bit;
    return 0;
}

static int fatx_cluster(fatx *f, uint32_t c)
{
    if (c < 1 || c > f->clusters) return -1;
    return xbox_disk_read(f->d, f->base + f->data + (uint64_t)(c - 1) * f->cluster, f->buf, f->cluster);
}

fatx_entry fatx_root(const fatx *f)
{
    fatx_entry e;
    memset(&e, 0, sizeof e);
    e.attr = FATX_ATTR_DIR;
    e.first = f ? f->root : 0;
    return e;
}

/* A name this side can copy into a folder: not "." or "..", no separator, no
 * control character. */
static int fatx_name_ok(const char *s)
{
    if (!s[0] || !strcmp(s, ".") || !strcmp(s, "..")) return 0;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 0x20 || ch == 0x7F || ch == '/' || ch == '\\') return 0;
    }
    return 1;
}

int fatx_list(fatx *f, const fatx_entry *dir, fatx_entry **out)
{
    fatx_entry *list = NULL;
    int count = 0, cap = 0;
    uint32_t c;
    uint8_t *seen;

    *out = NULL;
    if (!f || !dir || !(dir->attr & FATX_ATTR_DIR) || !(seen = fatx_seen(f))) return -1;
    for (c = dir->first; c; c = fatx_next(f, c)) {
        uint32_t i;
        if (fatx_visit(seen, c) || fatx_cluster(f, c)) goto damaged;
        for (i = 0; i < f->cluster / FATX_DIRENT; i++) {
            const uint8_t *p = f->buf + (size_t)i * FATX_DIRENT;
            fatx_entry e;
            if (p[0] == FATX_DIRENT_END || p[0] == FATX_DIRENT_END2) goto end;
            if (p[0] == FATX_DIRENT_DELETED) continue;
            if (p[0] > FATX_NAME_MAX) goto damaged;
            memset(&e, 0, sizeof e);
            memcpy(e.name, p + 2, p[0]);
            if (strlen(e.name) != p[0] || !fatx_name_ok(e.name)) goto damaged;
            e.attr = p[1];
            e.first = le32(p + 44);
            e.size = le32(p + 48);
            e.written = le16(p + 54) << 16 | le16(p + 52);
            if (e.first > f->clusters) goto damaged;
            if (count == cap) {
                int grown = cap ? cap * 2 : 16;
                fatx_entry *more = realloc(list, (size_t)grown * sizeof *list);
                if (!more) goto damaged;
                list = more;
                cap = grown;
            }
            list[count++] = e;
        }
    }
end:
    free(seen);
    *out = list;
    return count;
damaged:
    free(seen);
    free(list);
    return -1;
}

int fatx_find(fatx *f, const char *path, fatx_entry *out)
{
    fatx_entry at;
    int depth = 0;

    if (!f || !path || !out) return -1;
    at = fatx_root(f);
    while (*path) {
        char part[FATX_NAME_MAX + 1];
        size_t n = 0;
        fatx_entry *list;
        int count, i, found = -1;

        while (*path == '/' || *path == '\\') path++;
        if (!*path) break;
        while (path[n] && path[n] != '/' && path[n] != '\\') n++;
        if (n > FATX_NAME_MAX || ++depth > FATX_MAX_DEPTH) return 0;
        memcpy(part, path, n);
        part[n] = 0;
        path += n;
        if (!(at.attr & FATX_ATTR_DIR)) return 0;
        count = fatx_list(f, &at, &list);
        if (count < 0) return -1;
        for (i = 0; i < count && found < 0; i++) {
            size_t k;
            if (strlen(list[i].name) != n) continue;
            for (k = 0; k < n && tolower((unsigned char)list[i].name[k]) == tolower((unsigned char)part[k]); k++)
                ;
            if (k == n) found = i;
        }
        if (found >= 0) at = list[found];
        free(list);
        if (found < 0) return 0;
    }
    *out = at;
    return 1;
}

int fatx_read(fatx *f, const fatx_entry *file, uint8_t **data)
{
    uint8_t *buf, *seen;
    uint32_t c, left;

    *data = NULL;
    if (!f || !file || (file->attr & FATX_ATTR_DIR) || file->size > FATX_FILE_MAX) return -1;
    if (!file->size) return 0;
    buf = malloc(file->size);
    seen = fatx_seen(f);
    if (!buf || !seen) {
        free(buf);
        free(seen);
        return -1;
    }
    left = file->size;
    for (c = file->first; c && left; c = fatx_next(f, c)) {
        uint32_t take = left < f->cluster ? left : f->cluster;
        if (fatx_visit(seen, c) || fatx_cluster(f, c)) break;
        memcpy(buf + (file->size - left), f->buf, take);
        left -= take;
    }
    free(seen);
    if (left) {
        free(buf);
        return -1;
    }
    *data = buf;
    return 0;
}

void fatx_close(fatx *f)
{
    if (!f) return;
    free(f->fat);
    free(f->buf);
    free(f);
}
