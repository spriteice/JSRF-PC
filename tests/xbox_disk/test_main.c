/*
 * tests/xbox_disk: launcher/tools/xbox_disk.c against disk images built
 * here, as run.py says.
 *
 * Each image holds a FATX partition with a save laid out as the game lays
 * one out (UDATA\5345000a\<save>\SaveMeta.xbx and JSRFDATA.SAV, its clusters
 * out of order), a deleted entry and an empty file. The partitions are built
 * to the layout xbox_disk.c's comment gives; a real image of xemu's is
 * checked on the PC (the test list).
 */
#include "xbox_disk.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../third_party/miniz/miniz.h"
#include "disk_build.h"

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

/* What every image's partition must read as. */
static void check_tree(xbox_disk *d, uint64_t base, uint64_t length, const char *what)
{
    char err[256] = "";
    fatx *f = fatx_open(d, base, length, err, sizeof err);
    fatx_entry e, root, *list = NULL;
    uint8_t *data = NULL;
    int n;

    CHECK(f != NULL, "%s: fatx_open: %s", what, err);
    if (!f) return;
    root = fatx_root(f);
    n = fatx_list(f, &root, &list);
    CHECK(n == 2, "%s: the root lists %d entries, not 2 (the deleted one left out)", what, n);
    free(list);

    CHECK(fatx_find(f, "UDATA/5345000a/" BUILT_SAVE "/JSRFDATA.SAV", &e) == 1, "%s: the save isn't found", what);
    CHECK(e.size == SAVE_SIZE && !(e.attr & FATX_ATTR_DIR), "%s: the save's entry is wrong (%u)", what, e.size);
    CHECK(e.written == (0x5B7Au << 16 | 0x6000u), "%s: the save's time is wrong (%08X)", what, e.written);
    CHECK(fatx_read(f, &e, &data) == 0 && data && matches(data, 3, SAVE_SIZE),
          "%s: the save's bytes are wrong", what);
    free(data);
    data = NULL;

    CHECK(fatx_find(f, "\\udata\\5345000A\\91566539302d\\savemeta.xbx", &e) == 1,
          "%s: a path in another case, with backslashes, isn't found", what);
    CHECK(fatx_read(f, &e, &data) == 0 && data && matches(data, 1, META_SIZE),
          "%s: SaveMeta.xbx's bytes are wrong", what);
    free(data);
    data = NULL;

    CHECK(fatx_find(f, "UDATA/5345000a/" BUILT_SAVE "/empty.bin", &e) == 1, "%s: the empty file isn't found", what);
    CHECK(fatx_read(f, &e, &data) == 0 && data == NULL, "%s: the empty file doesn't read as empty", what);

    CHECK(fatx_find(f, "UDATA/5345000a/" BUILT_SAVE "", &e) == 1, "%s: the save folder isn't found", what);
    n = fatx_list(f, &e, &list);
    CHECK(n == 3, "%s: the save folder lists %d entries, not 3", what, n);
    free(list);
    CHECK(fatx_read(f, &e, &data) == -1, "%s: a folder read as a file", what);

    CHECK(fatx_find(f, "TDATA/5345000a", &e) == 1, "%s: TDATA's title folder isn't found", what);
    n = fatx_list(f, &e, &list);
    CHECK(n == 0, "%s: an empty folder lists %d entries", what, n);
    free(list);

    CHECK(fatx_find(f, "UDATA/4d530064", &e) == 0, "%s: a missing folder is found", what);
    CHECK(fatx_find(f, "UDATA/5345000a/TitleMeta.xbx/x", &e) == 0, "%s: a path through a file is found", what);
    CHECK(fatx_find(f, "", &e) == 1 && (e.attr & FATX_ATTR_DIR), "%s: \"\" isn't the root", what);
    fatx_close(f);
}

/* ---- image files ------------------------------------------------------------ */

/* The images go in the folder run.py gives, not tmpfile()'s, which on
 * Windows is the drive's root and often can't be written. */
static const char *g_dir = ".";

static FILE *temp_with(const void *head, size_t n)
{
    static int count;
    char path[1024];
    FILE *t;
    snprintf(path, sizeof path, "%s/image%d.bin", g_dir, count++);
    t = fopen(path, "w+b");
    CHECK(t != NULL, "can't make %s", path);
    if (!t) exit(1);
    if (n) fwrite(head, 1, n, t);
    fflush(t);
    return t;
}

/* A raw disk: 1 MB of junk, then the partition, the file as long as both. */
static void test_raw(uint64_t length, uint32_t spc, const char *what)
{
    part p = build(length, spc);
    const uint64_t base = 1u << 20;
    uint8_t *junk = malloc((size_t)base);
    FILE *t;
    xbox_disk *d;
    char err[256] = "";
    uint8_t byte = 0;

    memset(junk, 0x5A, (size_t)base);
    t = temp_with(junk, (size_t)base);
    fwrite(p.bytes, 1, (size_t)p.used, t);
    fseek(t, (long)(base + length - 1), SEEK_SET);
    fwrite(&byte, 1, 1, t);
    fflush(t);
    d = xbox_disk_open(t, err, sizeof err);
    CHECK(d != NULL, "%s: xbox_disk_open: %s", what, err);
    if (d) {
        uint8_t b[4];
        CHECK(!xbox_disk_is_qcow2(d), "%s: a raw image taken for qcow2", what);
        CHECK(xbox_disk_size(d) == base + length, "%s: the size is wrong", what);
        check_tree(d, base, length, what);
        CHECK(xbox_disk_read(d, base + length - 2, b, 4) == -1, "%s: a read past the end works", what);
        xbox_disk_close(d);
    }
    fclose(t);
    free(junk);
    free(p.bytes);
}

/* ---- qcow2 ------------------------------------------------------------------ */

static xbox_disk *open_q(const qimg *q, FILE **t, char *err, size_t errn)
{
    *t = temp_with(q->b, q->n);
    return xbox_disk_open(*t, err, errn);
}

/* The retail E: partition, at its place on a retail-sized disk. */
static void test_qcow2(uint32_t version)
{
    const uint64_t size = XBOX_DISK_E_OFFSET + XBOX_DISK_E_LENGTH;
    part p = build(XBOX_DISK_E_LENGTH, 32);
    qopts o = { version, 0, 0, 0, -1 };
    qimg q = qcow2_of(&p, XBOX_DISK_E_OFFSET, size, o);
    char err[256] = "", what[32];
    FILE *t;
    xbox_disk *d;

    snprintf(what, sizeof what, "qcow2 v%u", version);
    CHECK(p.wide, "%s: E: should have a 32-bit FAT", what);
    CHECK(q.compressed > 0 && q.plain > 0, "%s: the image should mix compressed and plain clusters", what);
    CHECK(version != 3 || q.zero > 0, "%s: the image should have clusters marked as zeros", what);
    d = open_q(&q, &t, err, sizeof err);
    CHECK(d != NULL, "%s: xbox_disk_open: %s", what, err);
    if (d) {
        uint8_t *back = malloc((size_t)p.used), b[16];
        CHECK(xbox_disk_is_qcow2(d), "%s: not taken for qcow2", what);
        CHECK(xbox_disk_size(d) == size, "%s: the size is wrong", what);
        /* The whole partition's start in one read, across clusters, plain,
         * compressed and zero alike. */
        CHECK(xbox_disk_read(d, XBOX_DISK_E_OFFSET, back, (size_t)p.used) == 0
              && !memcmp(back, p.bytes, (size_t)p.used), "%s: the partition reads back wrong", what);
        /* Where nothing was written: zeros. */
        memset(b, 0xAA, sizeof b);
        CHECK(xbox_disk_read(d, 12345, b, sizeof b) == 0 && !b[0] && !b[15], "%s: unwritten space isn't zeros", what);
        CHECK(xbox_disk_read(d, size - 8, b, 16) == -1, "%s: a read past the end works", what);
        check_tree(d, XBOX_DISK_E_OFFSET, XBOX_DISK_E_LENGTH, what);
        free(back);
        xbox_disk_close(d);
    }
    fclose(t);
    free(q.b);
    free(p.bytes);
}

/* qcow2 images this reader mustn't guess at. */
static void test_qcow2_refused(void)
{
    static const struct { qopts o; const char *what; } cases[] = {
        { { 3, 0x10000, 0, 0, -1 }, "a backing file" },
        { { 3, 0, 1, 0, -1 }, "encryption" },
        { { 3, 0, 0, 1ull << 2, -1 }, "an external data file" },
        { { 3, 0, 0, 1ull << 4, -1 }, "extended L2 entries" },
        { { 3, 0, 0, 1ull << 3, 1 }, "zstd compression" },
        { { 4, 0, 0, 0, -1 }, "version 4" },
    };
    part p = build(64u << 20, 32);
    size_t i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        qimg q = qcow2_of(&p, 0, 64u << 20, cases[i].o);
        char err[256] = "";
        FILE *t;
        xbox_disk *d = open_q(&q, &t, err, sizeof err);
        CHECK(d == NULL && err[0], "qcow2 with %s isn't refused", cases[i].what);
        xbox_disk_close(d);
        fclose(t);
        free(q.b);
    }
    /* Dirty (the reference counts may be stale) is read as any other. */
    {
        qopts o = { 3, 0, 0, 1, -1 };
        qimg q = qcow2_of(&p, 0, 64u << 20, o);
        char err[256] = "";
        FILE *t;
        xbox_disk *d = open_q(&q, &t, err, sizeof err);
        CHECK(d != NULL, "a dirty qcow2 image is refused: %s", err);
        if (d) check_tree(d, 0, 64u << 20, "dirty qcow2");
        xbox_disk_close(d);
        fclose(t);
        free(q.b);
    }
    free(p.bytes);
}

/* ---- damage ------------------------------------------------------------------ */

static xbox_disk *open_part(const part *p, FILE **t)
{
    char err[256] = "";
    uint8_t byte = 0;
    *t = temp_with(p->bytes, (size_t)p->used);
    fseek(*t, (long)(p->length - 1), SEEK_SET);
    fwrite(&byte, 1, 1, *t);
    fflush(*t);
    return xbox_disk_open(*t, err, sizeof err);
}

static void test_damage(void)
{
    const uint64_t length = 64u << 20;
    char err[256];
    FILE *t;
    xbox_disk *d;
    fatx *f;
    fatx_entry e, *list;
    uint8_t *data;
    part p;

    /* A chain that loops back. */
    p = build(length, 32);
    {
        uint32_t second = 0;
        second = p.wide ? 0 : (uint32_t)(p.bytes[0x1000 + p.save_first * 2] | p.bytes[0x1000 + p.save_first * 2 + 1] << 8);
        fat_set(&p, second, p.save_first);
    }
    d = open_part(&p, &t);
    f = fatx_open(d, 0, length, err, sizeof err);
    CHECK(f && fatx_find(f, "UDATA/5345000a/" BUILT_SAVE "/JSRFDATA.SAV", &e) == 1, "loop: the save isn't found");
    CHECK(f && fatx_read(f, &e, &data) == -1, "loop: a looping chain reads");
    fatx_close(f);
    xbox_disk_close(d);
    fclose(t);
    free(p.bytes);

    /* Names that would leave a folder, or run past their field. */
    {
        static const char *bad[] = { "..", ".", "a/b", "a\\b" };
        size_t i;
        for (i = 0; i <= sizeof bad / sizeof bad[0]; i++) {
            p = build(length, 32);
            if (i < sizeof bad / sizeof bad[0]) dirent(cluster_at(&p, 1) + 64, bad[i], 0x10, 2, 0);
            else cluster_at(&p, 1)[64] = 43;
            d = open_part(&p, &t);
            f = fatx_open(d, 0, length, err, sizeof err);
            e = fatx_root(f);
            CHECK(f && fatx_list(f, &e, &list) == -1, "a name %s is taken", i < 4 ? bad[i] : "43 long");
            fatx_close(f);
            xbox_disk_close(d);
            fclose(t);
            free(p.bytes);
        }
    }

    /* No FATX there, and a root outside the partition. */
    p = build(length, 32);
    memcpy(p.bytes, "FATZ", 4);
    d = open_part(&p, &t);
    err[0] = 0;
    CHECK(fatx_open(d, 0, length, err, sizeof err) == NULL && err[0], "no FATX: opened");
    xbox_disk_close(d);
    fclose(t);
    memcpy(p.bytes, "FATX", 4);
    put32(p.bytes + 12, 0x7FFFFFFF);
    d = open_part(&p, &t);
    err[0] = 0;
    CHECK(fatx_open(d, 0, length, err, sizeof err) == NULL && err[0], "a root outside: opened");
    CHECK(fatx_open(d, 0, length * 2, err, sizeof err) == NULL, "a partition longer than the disk: opened");
    xbox_disk_close(d);
    fclose(t);
    free(p.bytes);
}

int main(int argc, char **argv)
{
    if (argc > 1) g_dir = argv[1];
    test_raw(64u << 20, 32, "raw, 16-bit FAT");
    test_raw(40u << 20, 1, "raw, 32-bit FAT");
    test_qcow2(3);
    test_qcow2(2);
    test_qcow2_refused();
    test_damage();
    if (failures) {
        printf("xbox_disk: %d failures\n", failures);
        return 1;
    }
    printf("xbox_disk: all cases pass\n");
    return 0;
}
