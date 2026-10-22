/* Run with: python3 tests/shader_cache/run.py
 *
 * What other drivers and engines left in the shader cache's folder
 * (nv2a_sc_sweep): every binaries file but the one this run keeps is removed,
 * while program and recipe files, and anything that is not the cache's, stay.
 * argv[1] is a fresh folder.
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

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    static const char blob[] = "a compiled program";
    char sub[1200];
    uint32_t len = 0, format = 0;
    void *back;
    /* An earlier driver's binaries, and an older engine's files. */
    put(dir, "binaries-t-AAAAAAAA.dat");
    put(dir, "binaries-old-0BADF00D.dat");
    put(dir, "programs-old.dat");
    put(dir, "recipes-old.dat");
    /* Not the cache's. */
    put(dir, "notes.dat");
    put(dir, "binaries-t-AAAAAAAA.txt");
    snprintf(sub, sizeof sub, "%s/binaries-folder.dat", dir);
    CHECK(make_dir(sub) == 0);

    CHECK(nv2a_sc_sweep() == 0);                         /* not open: does nothing */
    CHECK(nv2a_sc_open(dir, "t"));
    CHECK(nv2a_sc_sweep() == 0);                         /* keeps no binaries: nothing either */
    CHECK(there(dir, "binaries-t-AAAAAAAA.dat") && there(dir, "binaries-old-0BADF00D.dat"));

    CHECK(nv2a_sc_bin_open("a driver"));                 /* binaries-t-<its hash>.dat */
    nv2a_sc_bin_put(1, blob, sizeof blob, 7);
    CHECK(nv2a_sc_sweep() == 2);
    CHECK(!there(dir, "binaries-t-AAAAAAAA.dat") && !there(dir, "binaries-old-0BADF00D.dat"));
    CHECK(there(dir, "programs-t.dat") && there(dir, "recipes-t.dat")
          && there(dir, "programs-old.dat") && there(dir, "recipes-old.dat")
          && there(dir, "notes.dat") && there(dir, "binaries-t-AAAAAAAA.txt"));
    /* This run's binaries file is still there, and still read. */
    back = nv2a_sc_bin_get(1, &len, &format);
    CHECK(back && len == sizeof blob && format == 7 && !memcmp(back, blob, sizeof blob));
    free(back);
    CHECK(nv2a_sc_sweep() == 0);
    fprintf(stderr, g_fail ? "shader cache sweep test FAILED\n" : "shader cache sweep test passed\n");
    return g_fail;
}
