/*
 * tests/mission_walk: the story walk that makes Developer > Missions'
 * start-states.txt (launcher/tools/mission_walk.c), as run.py says. No game
 * data: the mission files are made up (made_up.py), and the known answers
 * are mission-select/tools/walk3.py's and starts.py's on the same files.
 *
 *   test_main ids                 jsrf_mission_id's cases
 *   test_main story <folder>      the walk over <folder>/Media/Mission: the
 *                                 file on stdout, or "fail: <why>"; each
 *                                 mission reached on stderr (built with
 *                                 JSRF_WALK_TRACE)
 *   test_main order <folder>      the same files handed over in other orders
 *                                 give the same file
 *   test_main range               a mission numbered past 9999 is refused
 *   test_main put <folder>        start-states.txt written, replaced and
 *                                 removed in <folder>, each after its backup
 */
#include "mission_walk.h"
#include "setup.h"
#include "setup_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int ids(void)
{
    static const struct { const char *name; int id; } cases[] = {
        { "mssn0101.bin", 101 }, { "MSSN0996.BIN", 996 }, { "Mssn0000.Bin", 0 }, { "mssn9999.bin", 9999 },
        { "mssn101.bin", -1 },   { "mssn01010.bin", -1 }, { "mssnab01.bin", -1 }, { "mssn0101.binx", -1 },
        { "xssn0101.bin", -1 },  { "mssn0101.dat", -1 },  { "", -1 },             { "mssn0-01.bin", -1 },
    };
    size_t i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
        CHECK(jsrf_mission_id(cases[i].name) == cases[i].id, "jsrf_mission_id(\"%s\") is %d, not %d",
              cases[i].name, jsrf_mission_id(cases[i].name), cases[i].id);
    CHECK(jsrf_mission_id(NULL) == -1, "jsrf_mission_id(NULL)");
    return 0;
}

static int story(const char *folder)
{
    jsrf_walk w;
    if (jsrf_walk_game(folder, &w)) {
        CHECK(w.text == NULL, "a failed walk gives no text");
        printf("fail: %s\n", w.why);
        return 0;
    }
    CHECK(w.text && w.len == strlen(w.text), "the text's length");
    CHECK(w.story == w.reached + w.marked, "story %d, reached %d, marked %d", w.story, w.reached, w.marked);
    fwrite(w.text, 1, w.len, stdout);
    fprintf(stderr, "counts %d %d %d\n", w.story, w.reached, w.marked);
    jsrf_walk_free(&w);
    CHECK(w.text == NULL, "jsrf_walk_free clears the text");
    return 0;
}

typedef struct {
    char dir[4096];
    jsrf_mission_file files[2000];
    int n;
} files_ctx;

static int each(void *ctx, const char *name, int is_dir)
{
    files_ctx *f = (files_ctx *)ctx;
    char path[4200];
    unsigned char *data = NULL;
    size_t size = 0;
    int id = jsrf_mission_id(name);
    if (is_dir || id < 0 || f->n == 2000) return 0;
    js_path(path, sizeof path, f->dir, name);
    if (js_read_file(path, JSRF_WALK_FILE_MAX, &data, &size)) return 1;
    f->files[f->n].id = id;
    f->files[f->n].data = data;
    f->files[f->n].size = size;
    f->n++;
    return 0;
}

static int order(const char *folder)
{
    static files_ctx f;
    jsrf_walk game, a, b;
    jsrf_mission_file *rev;
    char media[4096];
    int i;
    js_path(media, sizeof media, folder, "Media");
    js_path(f.dir, sizeof f.dir, media, "Mission");
    CHECK(js_list_dir(f.dir, each, &f) == 0 && f.n > 0, "the made-up files read");
    CHECK(jsrf_walk_game(folder, &game) == 0, "the walk over the folder: %s", game.why);
    CHECK(jsrf_walk_states(f.files, f.n, &a) == 0, "the walk in the listing's order: %s", a.why);
    rev = (jsrf_mission_file *)malloc(sizeof *rev * (size_t)f.n);
    for (i = 0; i < f.n; i++) rev[i] = f.files[f.n - 1 - i];
    CHECK(jsrf_walk_states(rev, f.n, &b) == 0, "the walk in the other order: %s", b.why);
    CHECK(game.text && a.text && b.text && !strcmp(game.text, a.text) && !strcmp(a.text, b.text),
          "the same file whatever the order");
    jsrf_walk_free(&game);
    jsrf_walk_free(&a);
    jsrf_walk_free(&b);
    /* Twice over the same files: the same file. */
    CHECK(jsrf_walk_states(f.files, f.n, &a) == 0 && jsrf_walk_states(f.files, f.n, &b) == 0 &&
          !strcmp(a.text, b.text), "the same file each time");
    jsrf_walk_free(&a);
    jsrf_walk_free(&b);
    for (i = 0; i < f.n; i++) free((void *)f.files[i].data);
    free(rev);
    return 0;
}

static int range(void)
{
    static const uint8_t none[4] = { 0 };
    jsrf_mission_file f = { 10000, none, sizeof none };
    jsrf_walk w;
    CHECK(jsrf_walk_states(&f, 1, &w) == -1 && strstr(w.why, "outside 0000 to 9999"), "10000 refused: %s", w.why);
    f.id = -1;
    CHECK(jsrf_walk_states(&f, 1, &w) == -1 && strstr(w.why, "outside 0000 to 9999"), "-1 refused: %s", w.why);
    CHECK(jsrf_walk_states(NULL, 0, &w) == -1 && strstr(w.why, "0101"), "no files: %s", w.why);
    return 0;
}

static int read_back(const char *path, char *buf, size_t n)
{
    unsigned char *data = NULL;
    size_t size = 0;
    if (js_read_file(path, n - 1, &data, &size) != 0) return -1;
    if (size) memcpy(buf, data, size);
    buf[size] = 0;
    free(data);
    return 0;
}

static int put(const char *folder)
{
    char dir[4096], backups[4096], file[4200], fresh[4300], backup[4096], first[4096], why[400], in[200], p[4300];
    js_path(dir, sizeof dir, folder, "settings");
    js_path(backups, sizeof backups, dir, "Start states backups");
    js_path(file, sizeof file, dir, JSRF_WALK_FILE);
    snprintf(fresh, sizeof fresh, "%s.new", file);

    /* None there: written, nothing backed up. The folder is made. */
    CHECK(!jsrf_states_same(dir, "one\n", 4), "same as a file that isn't there");
    CHECK(jsrf_states_write(dir, backups, "one\n", 4, backup, sizeof backup, why, sizeof why) == 0, "the first: %s", why);
    CHECK(!backup[0] && !js_is_dir(backups), "nothing to back up the first time: %s", backup);
    CHECK(read_back(file, in, sizeof in) == 0 && !strcmp(in, "one\n"), "the first written: %s", in);
    CHECK(!js_is_file(fresh), "no .new left");
    CHECK(jsrf_states_same(dir, "one\n", 4) && !jsrf_states_same(dir, "one", 3) && !jsrf_states_same(dir, "one\nx", 5) &&
          !jsrf_states_same(dir, "two\n", 4), "same, byte for byte");

    /* Replaced: the one there backed up first. Twice in a second: (2). */
    CHECK(jsrf_states_write(dir, backups, "two\n", 4, backup, sizeof backup, why, sizeof why) == 0, "the second: %s", why);
    snprintf(first, sizeof first, "%s", backup);
    js_path(p, sizeof p, backup, JSRF_WALK_FILE);
    CHECK(backup[0] && read_back(p, in, sizeof in) == 0 && !strcmp(in, "one\n"), "the first backed up: %s", backup);
    CHECK(read_back(file, in, sizeof in) == 0 && !strcmp(in, "two\n"), "the second written: %s", in);
    CHECK(jsrf_states_write(dir, backups, "three\n", 6, backup, sizeof backup, why, sizeof why) == 0, "the third: %s", why);
    js_path(p, sizeof p, backup, JSRF_WALK_FILE);
    CHECK(strcmp(first, backup) && read_back(p, in, sizeof in) == 0 && !strcmp(in, "two\n"), "the second backed up apart: %s", backup);
    js_path(p, sizeof p, first, JSRF_WALK_FILE);
    CHECK(read_back(p, in, sizeof in) == 0 && !strcmp(in, "one\n"), "the first backup kept");

    /* No backup can be made (the backups folder is a file): nothing changes. */
    {
        char bad[4200];
        js_path(bad, sizeof bad, folder, "a file");
        CHECK(js_write_file(bad, "x", 1) == 0, "a file where the backups go");
        CHECK(jsrf_states_write(dir, bad, "four\n", 5, backup, sizeof backup, why, sizeof why) == -1 &&
              strstr(why, "nothing was changed") && !backup[0], "no backup, no write: %s", why);
        CHECK(read_back(file, in, sizeof in) == 0 && !strcmp(in, "three\n") && !js_is_file(fresh), "left as it was: %s", in);
        CHECK(jsrf_states_remove(dir, bad, backup, sizeof backup, why, sizeof why) == -1 && js_is_file(file),
              "no backup, no remove: %s", why);
    }

    /* Removed, after its backup; again, there's nothing to do. */
    CHECK(jsrf_states_remove(dir, backups, backup, sizeof backup, why, sizeof why) == 0, "removed: %s", why);
    js_path(p, sizeof p, backup, JSRF_WALK_FILE);
    CHECK(!js_is_file(file) && backup[0] && read_back(p, in, sizeof in) == 0 && !strcmp(in, "three\n"),
          "removed after its backup: %s", backup);
    CHECK(jsrf_states_remove(dir, backups, backup, sizeof backup, why, sizeof why) == 0 && !backup[0], "removed twice");

    /* An empty file (a walk gives none, but one may be there) is backed up too. */
    CHECK(js_write_file(file, "", 0) == 0, "an empty file");
    CHECK(jsrf_states_same(dir, "", 0) && !jsrf_states_same(dir, "x", 1), "same as an empty file");
    CHECK(jsrf_states_write(dir, backups, "five\n", 5, backup, sizeof backup, why, sizeof why) == 0 && backup[0],
          "an empty one replaced: %s", why);
    js_path(p, sizeof p, backup, JSRF_WALK_FILE);
    CHECK(js_is_file(p) && read_back(p, in, sizeof in) == 0 && !in[0], "the empty one backed up");
    printf("put: done\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "ids")) ids();
    else if (argc >= 3 && !strcmp(argv[1], "story")) story(argv[2]);
    else if (argc >= 3 && !strcmp(argv[1], "order")) order(argv[2]);
    else if (argc >= 2 && !strcmp(argv[1], "range")) range();
    else if (argc >= 3 && !strcmp(argv[1], "put")) put(argv[2]);
    else {
        fprintf(stderr, "usage: test_main ids | story <folder> | order <folder> | range | put <folder>\n");
        return 2;
    }
    if (failures) printf("mission_walk: %d failed\n", failures);
    return failures ? 1 : 0;
}
