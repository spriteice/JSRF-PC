/* Run with: python3 tests/shader_cache/run.py
 *
 * The shader cache's files, one start a run of this program (argv[2] says
 * which; argv[1] is the folder):
 *
 *   sweep   what other drivers and engines left in the folder (nv2a_sc_sweep):
 *           every binaries file but the one this run keeps, and this
 *           backend's programs and recipes files for another translator, are
 *           removed; another backend's files, states.dat and anything that is
 *           not the cache's stay.
 *   record  a fresh folder: programs and NV2A states kept.
 *   keep    the next start: the states come back as they were kept,
 *           nv2a_sc_keep forgets the programs not asked for, and
 *           nv2a_sc_state_keep the states of another layout, on disk too.
 *   reload  the start after: only the programs and states kept, and those
 *           added since, with the sources they use and no others. Then a state
 *           half written at the end of states.dat, as a run stopped mid-write
 *           leaves;
 *   damaged the start after that drops it and keeps the rest.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#  include <direct.h>
#  define make_dir(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  define make_dir(p) mkdir(p, 0755)
#endif
#include "nv2a_shader_cache.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail = 1; } } while (0)

#define TAG "t-11111111"

static void put(const char *dir, const char *name)
{
    char path[1200];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "ab");
    CHECK(f != NULL);
    if (f) fclose(f);
}

static int there(const char *dir, const char *name)
{
    char path[1200];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "rb"))) return 0;
    fclose(f);
    return 1;
}

static long size_of(const char *dir, const char *name)
{
    char path[1200];
    FILE *f;
    long n = -1;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "rb"))) return -1;
    if (!fseek(f, 0, SEEK_END)) n = ftell(f);
    fclose(f);
    return n;
}

/* Program i of the cache is `key`, built from v and f. */
static int entry_is(int i, uint64_t key, const char *v, const char *f)
{
    uint64_t k;
    const char *ev, *ef;
    return nv2a_sc_entry(i, &k, &ev, &ef) && k == key && !strcmp(ev, v) && !strcmp(ef, f);
}

/* State i is (layout, kind, bytes). */
static int state_is(int i, uint32_t layout, uint32_t kind, const char *bytes)
{
    uint32_t l, k, len;
    const void *d;
    return nv2a_sc_state(i, &l, &k, &d, &len) && l == layout && k == kind
        && len == strlen(bytes) && !memcmp(d, bytes, len);
}

static void sweep(const char *dir)
{
    static const char blob[] = "a compiled program";
    char sub[1200];
    uint32_t len = 0, format = 0;
    void *back;
    /* An earlier driver's binaries, an older engine's binaries, and this
     * backend's files for another translator. */
    put(dir, "binaries-t-AAAAAAAA.dat");
    put(dir, "binaries-old-0BADF00D.dat");
    put(dir, "programs-t-0BADF00D.dat");
    put(dir, "recipes-t-0BADF00D.dat");
    /* Another backend's, which a Mac switches to. */
    put(dir, "programs-u-0BADF00D.dat");
    put(dir, "recipes-u-0BADF00D.dat");
    /* Not the cache's. */
    put(dir, "notes.dat");
    put(dir, "binaries-t-AAAAAAAA.txt");
    put(dir, "programs-t-0BADF00D.txt");
    snprintf(sub, sizeof sub, "%s/binaries-folder.dat", dir);
    CHECK(make_dir(sub) == 0);
    snprintf(sub, sizeof sub, "%s/programs-t-folder.dat", dir);
    CHECK(make_dir(sub) == 0);

    CHECK(nv2a_sc_sweep() == 0);                         /* not open: does nothing */
    CHECK(nv2a_sc_open(dir, TAG));
    CHECK(nv2a_sc_sweep() == 2);                         /* keeps no binaries: programs and recipes only */
    CHECK(!there(dir, "programs-t-0BADF00D.dat") && !there(dir, "recipes-t-0BADF00D.dat"));
    CHECK(there(dir, "binaries-t-AAAAAAAA.dat") && there(dir, "binaries-old-0BADF00D.dat"));

    CHECK(nv2a_sc_bin_open("a driver"));                 /* binaries-t-11111111-<its hash>.dat */
    nv2a_sc_bin_put(1, blob, sizeof blob, 7);
    CHECK(nv2a_sc_sweep() == 2);
    CHECK(!there(dir, "binaries-t-AAAAAAAA.dat") && !there(dir, "binaries-old-0BADF00D.dat"));
    CHECK(there(dir, "programs-" TAG ".dat") && there(dir, "recipes-" TAG ".dat") && there(dir, "states.dat")
          && there(dir, "programs-u-0BADF00D.dat") && there(dir, "recipes-u-0BADF00D.dat")
          && there(dir, "notes.dat") && there(dir, "binaries-t-AAAAAAAA.txt")
          && there(dir, "programs-t-0BADF00D.txt"));
    /* This run's binaries file is still there, and still read. */
    back = nv2a_sc_bin_get(1, &len, &format);
    CHECK(back && len == sizeof blob && format == 7 && !memcmp(back, blob, sizeof blob));
    free(back);
    CHECK(nv2a_sc_sweep() == 0);
}

static void record(const char *dir)
{
    CHECK(nv2a_sc_state_count() == -1);                  /* not open: no states file */
    CHECK(nv2a_sc_open(dir, TAG));
    CHECK(nv2a_sc_state_count() == 0);
    nv2a_sc_add(11, "v1", "f1");
    nv2a_sc_add(22, "v2", "f2");
    nv2a_sc_add(33, "v1", "f3");                         /* shares v1 with program 11 */
    nv2a_sc_state_add(1, 0, "a", 1);
    nv2a_sc_state_add(1, 1, "bb", 2);
    nv2a_sc_state_add(1, 1, "bb", 2);                    /* kept once */
    nv2a_sc_state_add(2, 0, "a", 1);                     /* another layout: another state */
    CHECK(nv2a_sc_state_count() == 3);
}

static void keep(const char *dir)
{
    static const uint64_t one[] = { 11 }, both[] = { 44, 11 };
    long before;
    CHECK(nv2a_sc_open(dir, TAG));
    CHECK(nv2a_sc_count() == 3);
    CHECK(nv2a_sc_state_count() == 3);
    CHECK(state_is(0, 1, 0, "a") && state_is(1, 1, 1, "bb") && state_is(2, 2, 0, "a"));
    before = size_of(dir, "programs-" TAG ".dat");
    CHECK(nv2a_sc_keep(one, 1) == 2);
    CHECK(nv2a_sc_count() == 1 && entry_is(0, 11, "v1", "f1"));
    /* As a fresh file holds program 11: its two sources (16-byte headers),
     * then the program (28 bytes). */
    CHECK(size_of(dir, "programs-" TAG ".dat") == 16 + 2 + 16 + 2 + 28 && before > 16 + 2 + 16 + 2 + 28);
    nv2a_sc_add(44, "v4", "f1");                         /* f1 is on disk already */
    CHECK(size_of(dir, "programs-" TAG ".dat") == 16 + 2 + 16 + 2 + 28 + 16 + 2 + 28);
    CHECK(nv2a_sc_keep(both, 2) == 0);                   /* nothing to forget: the file stays */
    CHECK(size_of(dir, "programs-" TAG ".dat") == 16 + 2 + 16 + 2 + 28 + 16 + 2 + 28);

    /* Layout 1's states (24-byte headers) and not layout 2's, as a fresh file
     * holds them. */
    CHECK(nv2a_sc_state_keep(1) == 1);
    CHECK(nv2a_sc_state_count() == 2 && state_is(0, 1, 0, "a") && state_is(1, 1, 1, "bb"));
    CHECK(size_of(dir, "states.dat") == 24 + 1 + 24 + 2);
    nv2a_sc_state_add(1, 0, "dd", 2);
    CHECK(size_of(dir, "states.dat") == 24 + 1 + 24 + 2 + 24 + 2);
    CHECK(nv2a_sc_state_keep(1) == 0);                   /* nothing to drop: the file stays */
    CHECK(size_of(dir, "states.dat") == 24 + 1 + 24 + 2 + 24 + 2);
}

static void reload(const char *dir)
{
    char path[1200];
    FILE *f;
    CHECK(nv2a_sc_open(dir, TAG));
    CHECK(nv2a_sc_count() == 2 && entry_is(0, 11, "v1", "f1") && entry_is(1, 44, "v4", "f1"));
    CHECK(nv2a_sc_state_count() == 3 && state_is(0, 1, 0, "a") && state_is(1, 1, 1, "bb")
          && state_is(2, 1, 0, "dd"));
    /* A half-written state at the end, as a run stopped mid-write leaves: the
     * next open drops it and keeps the rest. */
    snprintf(path, sizeof path, "%s/states.dat", dir);
    if ((f = fopen(path, "ab")) != NULL) { fwrite("JSN1\1\0", 1, 6, f); fclose(f); }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".", *phase = argc > 2 ? argv[2] : "sweep";
    if (!strcmp(phase, "sweep")) sweep(dir);
    else if (!strcmp(phase, "record")) record(dir);
    else if (!strcmp(phase, "keep")) keep(dir);
    else if (!strcmp(phase, "reload")) reload(dir);
    else if (!strcmp(phase, "damaged")) {
        CHECK(nv2a_sc_open(dir, TAG));
        CHECK(nv2a_sc_state_count() == 3 && state_is(2, 1, 0, "dd"));
    } else {
        fprintf(stderr, "unknown phase %s\n", phase);
        return 2;
    }
    fprintf(stderr, g_fail ? "shader cache test %s FAILED\n" : "shader cache test %s passed\n", phase);
    return g_fail;
}
