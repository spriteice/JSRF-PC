/*
 * A FATX partition built in memory, laid out as xbox_disk.c's comment gives
 * one, with a save in it as the game lays one out: UDATA\5345000a\<save>\
 * SaveMeta.xbx and JSRFDATA.SAV (its clusters out of order), TitleMeta.xbx
 * beside the save, a deleted entry, an empty file and an empty TDATA title.
 * The files hold a pattern, not a real save. And a qcow2 image of a disk
 * holding such a partition (qcow2_of). For tests/xbox_disk and
 * tests/save_import.
 */
#ifndef DISK_BUILD_H
#define DISK_BUILD_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../third_party/miniz/miniz.h"

#define BUILT_SAVE     "91566539302D"   /* the folder of a save named "Test Save" */

/* ---- bytes ------------------------------------------------------------------ */

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
static void bput32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void bput64(uint8_t *p, uint64_t v) { bput32(p, (uint32_t)(v >> 32)); bput32(p + 4, (uint32_t)v); }

/* A file's bytes, a pattern that differs at every offset (so a cluster read
 * from the wrong place shows) and that deflate still shrinks. */
static uint8_t pattern(uint32_t seed, uint32_t i)
{
    return (uint8_t)(seed + (i & 0xFF) + (i >> 8) * 3 + (i >> 16) * 7);
}

/* ---- a FATX partition ------------------------------------------------------- */

#define SAVE_SIZE      41088u          /* JSRFDATA.SAV's size, 20 + 41068 */
#define META_SIZE      54u
#define TITLE_SIZE     40u

typedef struct part {
    uint8_t  *bytes;     /* the partition's start, through the last cluster used */
    uint64_t  used;
    uint64_t  length;    /* the whole partition */
    uint32_t  cluster;
    uint64_t  data;      /* the first cluster's offset */
    int       wide;
    uint32_t  next;      /* the next cluster to hand out */
    uint32_t  save_first;
} part;

static void fat_set(part *p, uint32_t c, uint32_t v)
{
    if (p->wide) put32(p->bytes + 0x1000 + (size_t)c * 4, v);
    else put16(p->bytes + 0x1000 + (size_t)c * 2, v & 0xFFFF);
}

static uint32_t fat_end(const part *p) { return p->wide ? 0xFFFFFFFFu : 0xFFFFu; }

static uint8_t *cluster_at(part *p, uint32_t c)
{
    return p->bytes + p->data + (uint64_t)(c - 1) * p->cluster;
}

/* One cluster, handed out in order and ending its chain. */
static uint32_t alloc1(part *p)
{
    uint32_t c = p->next++;
    fat_set(p, c, fat_end(p));
    return c;
}

static void dirent(uint8_t *at, const char *name, uint8_t attr, uint32_t first, uint32_t size)
{
    size_t n = strlen(name);
    memset(at, 0xFF, 64);
    at[0] = (uint8_t)n;
    at[1] = attr;
    memcpy(at + 2, name, n);
    put32(at + 44, first);
    put32(at + 48, size);
    put16(at + 52, 0x6000);        /* 12:00:00 */
    put16(at + 54, 0x5B7A);        /* 2025-11-26: (25 << 9) | (11 << 5) | 26 */
    memset(at + 56, 0, 8);
}

/* A file's clusters, filled with the pattern; with `shuffle`, handed out
 * two at a time in reverse, so its chain doesn't run in order. */
static uint32_t write_file(part *p, uint32_t seed, uint32_t size, int shuffle)
{
    uint32_t n = (size + p->cluster - 1) / p->cluster, i, first = 0, prev = 0;
    uint32_t *cs = malloc(n * sizeof *cs);
    for (i = 0; i < n; i++) cs[i] = alloc1(p);
    if (shuffle)
        for (i = 0; i + 1 < n; i += 2) { uint32_t t = cs[i]; cs[i] = cs[i + 1]; cs[i + 1] = t; }
    for (i = 0; i < n; i++) {
        uint32_t k, at = i * p->cluster;
        for (k = 0; k < p->cluster && at + k < size; k++) cluster_at(p, cs[i])[k] = pattern(seed, at + k);
        if (prev) fat_set(p, prev, cs[i]);
        else first = cs[i];
        fat_set(p, cs[i], fat_end(p));
        prev = cs[i];
    }
    free(cs);
    return first;
}

/* A partition of `length` bytes, `spc` sectors a cluster, with the save. */
static part build(uint64_t length, uint32_t spc)
{
    part p;
    uint64_t entries, fat_bytes;
    uint32_t root, udata, tdata, title, save, meta, tmeta, empty_dir;
    uint8_t *d;

    memset(&p, 0, sizeof p);
    p.length = length;
    p.cluster = spc * 512;
    entries = length / p.cluster;
    p.wide = entries >= 0xFFF0;
    fat_bytes = ((entries + 1) * (p.wide ? 4 : 2) + 0xFFF) / 0x1000 * 0x1000;
    p.data = 0x1000 + fat_bytes;
    /* Enough clusters for the tree and the files. */
    p.used = p.data + (uint64_t)p.cluster * (8 + (SAVE_SIZE + p.cluster - 1) / p.cluster + 4);
    p.bytes = calloc(1, (size_t)p.used);
    p.next = 1;
    memcpy(p.bytes, "FATX", 4);
    put32(p.bytes + 4, 0x12345678);
    put32(p.bytes + 8, spc);
    root = alloc1(&p);
    put32(p.bytes + 12, root);
    memset(p.bytes + 18, 0xFF, 0x1000 - 18);
    fat_set(&p, 0, p.wide ? 0xFFFFFFF8u : 0xFFF8u);

    udata = alloc1(&p);
    tdata = alloc1(&p);
    title = alloc1(&p);
    save = alloc1(&p);
    empty_dir = alloc1(&p);
    memset(cluster_at(&p, root), 0xFF, p.cluster);
    memset(cluster_at(&p, udata), 0xFF, p.cluster);
    memset(cluster_at(&p, tdata), 0xFF, p.cluster);
    memset(cluster_at(&p, title), 0xFF, p.cluster);
    memset(cluster_at(&p, save), 0xFF, p.cluster);
    memset(cluster_at(&p, empty_dir), 0xFF, p.cluster);

    meta = write_file(&p, 1, META_SIZE, 0);
    tmeta = write_file(&p, 2, TITLE_SIZE, 0);
    p.save_first = write_file(&p, 3, SAVE_SIZE, 1);

    d = cluster_at(&p, root);
    dirent(d, "gone", 0, 0, 0);
    d[0] = 0xE5;                                   /* deleted */
    dirent(d + 64, "UDATA", 0x10, udata, 0);
    dirent(d + 128, "TDATA", 0x10, tdata, 0);
    d = cluster_at(&p, udata);
    dirent(d, "5345000a", 0x10, title, 0);
    d = cluster_at(&p, tdata);
    dirent(d, "5345000a", 0x10, empty_dir, 0);
    d = cluster_at(&p, title);
    dirent(d, "TitleMeta.xbx", 0, tmeta, TITLE_SIZE);
    dirent(d + 64, BUILT_SAVE, 0x10, save, 0);
    d = cluster_at(&p, save);
    dirent(d, "SaveMeta.xbx", 0, meta, META_SIZE);
    dirent(d + 64, "JSRFDATA.SAV", 0, p.save_first, SAVE_SIZE);
    dirent(d + 128, "empty.bin", 0, 0, 0);
    return p;
}

static int matches(const uint8_t *data, uint32_t seed, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < size; i++) if (data[i] != pattern(seed, i)) return 0;
    return 1;
}

/* ---- a qcow2 image ---------------------------------------------------------- */

#define QBITS     16u
#define QCLUSTER  (1u << QBITS)
#define QL2       (QCLUSTER / 8)

typedef struct qimg {
    uint8_t *b;
    size_t   n, cap;
    int      compressed, zero, plain;
} qimg;

static void qgrow(qimg *q, size_t to)
{
    if (to > q->cap) {
        size_t cap = q->cap ? q->cap : QCLUSTER * 4;
        while (cap < to) cap *= 2;
        q->b = realloc(q->b, cap);
        memset(q->b + q->cap, 0, cap - q->cap);
        q->cap = cap;
    }
    if (to > q->n) q->n = to;
}

/* A cluster-aligned place at the end. */
static size_t qcluster(qimg *q)
{
    size_t at = (q->n + QCLUSTER - 1) / QCLUSTER * QCLUSTER;
    qgrow(q, at + QCLUSTER);
    return at;
}

typedef struct qopts {
    uint32_t version;
    uint64_t backing;
    uint32_t crypt;
    uint64_t incompat;
    int      compression_type;    /* -1: no such field */
} qopts;

/* A qcow2 image of a disk `size` long, whose bytes from `at` are the
 * partition's p.used bytes and are zeros everywhere else. Clusters of data
 * are stored plain and compressed by turns; a cluster of zeros is left out,
 * or (version 3) marked as zeros. */
static qimg qcow2_of(const part *p, uint64_t at, uint64_t size, qopts o)
{
    qimg q;
    uint64_t l1_size = (size + (uint64_t)QL2 * QCLUSTER - 1) / ((uint64_t)QL2 * QCLUSTER);
    size_t l1_at, header = o.version == 3 ? (o.compression_type >= 0 ? 112 : 104) : 72;
    uint64_t first = at / QCLUSTER, last = (at + p->used + QCLUSTER - 1) / QCLUSTER, c;
    uint8_t *raw = calloc(1, QCLUSTER), *packed = malloc(QCLUSTER * 2);
    int turn = 0;

    memset(&q, 0, sizeof q);
    qgrow(&q, QCLUSTER);                           /* the header's cluster */
    l1_at = qcluster(&q);
    qgrow(&q, l1_at + (size_t)l1_size * 8);
    for (c = first; c < last; c++) {
        uint64_t l1i = c / QL2, l2i = c % QL2, l2_at = 0, e, k, from = c * QCLUSTER;
        int any = 0;
        memset(raw, 0, QCLUSTER);
        for (k = 0; k < QCLUSTER; k++) {
            uint64_t o2 = from + k;
            if (o2 >= at && o2 - at < p->used) raw[k] = p->bytes[o2 - at];
            any |= raw[k];
        }
        if (q.b[l1_at + l1i * 8] | q.b[l1_at + l1i * 8 + 7]) {
            uint64_t v = 0;
            for (k = 0; k < 8; k++) v = v << 8 | q.b[l1_at + l1i * 8 + k];
            l2_at = v & 0x00FFFFFFFFFFFE00ull;
        }
        if (!l2_at) {
            l2_at = qcluster(&q);
            bput64(q.b + l1_at + l1i * 8, l2_at | 1ull << 63);
        }
        if (!any) {
            if (o.version == 3) {
                /* Written as zeros: by turns with no cluster, and with one
                 * the image still holds old bytes in, as a preallocated
                 * image does, which must not be read. */
                uint64_t keep = 0;
                if (q.zero & 1) {
                    keep = qcluster(&q);
                    memset(q.b + keep, 0xEE, QCLUSTER);
                }
                bput64(q.b + l2_at + l2i * 8, keep | (keep ? 1ull << 63 : 0) | 1);
                q.zero++;
            }
            continue;
        }
        if (turn++ & 1) {
            size_t clen = tdefl_compress_mem_to_mem(packed, QCLUSTER * 2, raw, QCLUSTER, TDEFL_DEFAULT_MAX_PROBES);
            size_t where = q.n | 0x40;             /* not on a sector's start */
            uint64_t sectors = ((where & 511) + clen + 511) / 512 - 1;
            if (clen && clen < QCLUSTER) {
                unsigned x = 62 - (QBITS - 8);
                qgrow(&q, where + clen);
                memcpy(q.b + where, packed, clen);
                e = 1ull << 62 | sectors << x | (uint64_t)where;
                bput64(q.b + l2_at + l2i * 8, e);
                q.compressed++;
                continue;
            }
        }
        e = qcluster(&q);
        memcpy(q.b + e, raw, QCLUSTER);
        bput64(q.b + l2_at + l2i * 8, e | 1ull << 63);
        q.plain++;
    }
    bput32(q.b, 0x514649FBu);
    bput32(q.b + 4, o.version);
    bput64(q.b + 8, o.backing);
    bput32(q.b + 20, QBITS);
    bput64(q.b + 24, size);
    bput32(q.b + 32, o.crypt);
    bput32(q.b + 36, (uint32_t)l1_size);
    bput64(q.b + 40, l1_at);
    if (o.version == 3) {
        bput64(q.b + 72, o.incompat);
        bput32(q.b + 96, 4);
        bput32(q.b + 100, (uint32_t)header);
        if (o.compression_type >= 0) q.b[104] = (uint8_t)o.compression_type;
    }
    free(raw);
    free(packed);
    return q;
}

#endif
