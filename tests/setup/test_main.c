/*
 * The launcher's setup (launcher/setup/setup_core.c) without its window or the
 * network: SHA-256, the key=value files, when the engine is ready and when the
 * setup must run, the tools folder's mark, unpacking the tools' Python (and
 * refusing what would land outside its folder), a download that fails (tried
 * again after a wait, which closing the window stops), the bar's easing,
 * what Delete takes, running a program with only the environment given, and
 * the whole first start the console's way, with a stand-in for the tools'
 * Python.
 *
 *   test <work dir>
 *
 * run.py makes the work folder: the archives in tar/, and, on Linux, the
 * stand-in Python. Every check is printed; the exit status is 1 if any failed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#define setenv(k, v, o) _putenv_s(k, v)
#define unsetenv(k) _putenv_s(k, "")
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "setup.h"
#include "setup_job.h"

static int failed, passed;
static char work[4096];

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) passed++;
    else failed++;
}

static void at(char *out, size_t n, const char *rel)
{
    js_path(out, n, work, rel);
}

static void write_at(const char *rel, const char *text)
{
    char p[4096], dir[4096], *slash, *bs;
    at(p, sizeof p, rel);
    snprintf(dir, sizeof dir, "%s", p);
    slash = strrchr(dir, '/');
    bs = strrchr(dir, '\\');   /* js_path's separator on Windows */
    if (bs && (!slash || bs > slash)) slash = bs;
    if (slash) { *slash = 0; js_mkdirs(dir); }
    if (js_write_text(p, text)) { printf("FAIL couldn't write %s\n", p); exit(1); }
}

static int exists(const char *rel)
{
    char p[4096];
    at(p, sizeof p, rel);
    return js_is_file(p) || js_is_dir(p);
}

/* ---- SHA-256: FIPS 180-4's examples ---- */

static void test_sha256(void)
{
    char hex[65], p[4096];
    static char million[1000000];
    FILE *f;
    js_sha256_hex("", 0, hex);
    check(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "sha256 of nothing");
    js_sha256_hex("abc", 3, hex);
    check(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "sha256 of abc");
    js_sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    check(!strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
          "sha256 of 56 bytes (the padding in a block of its own)");
    memset(million, 'a', sizeof million);
    js_sha256_hex(million, sizeof million, hex);
    check(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), "sha256 of a million a's");
    at(p, sizeof p, "million.txt");
    f = fopen(p, "wb");
    fwrite(million, 1, sizeof million, f);
    fclose(f);
    check(!js_sha256_file(p, hex) && !strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
          "sha256 of the same, read from a file");
    at(p, sizeof p, "not-there");
    check(js_sha256_file(p, hex) != 0, "sha256 of a file that isn't there: an error");
}

/* ---- key=value files ---- */

static void test_kv(void)
{
    char p[4096], v[4096], text[12000];
    write_at("kv/a.txt", "a=1\r\nb=x=y\nkeyx=no\nkey=first\nkey=last");
    at(p, sizeof p, "kv/a.txt");
    check(js_read_kv(p, "a", v, sizeof v) && !strcmp(v, "1"), "read_kv: a CR LF line");
    check(js_read_kv(p, "b", v, sizeof v) && !strcmp(v, "x=y"), "read_kv: a value with = in it");
    check(js_read_kv(p, "key", v, sizeof v) && !strcmp(v, "last"),
          "read_kv: the last value given, from a line with no newline");
    check(!js_read_kv(p, "c", v, sizeof v) && !v[0], "read_kv: a key not there: none, and the value empty");
    check(!js_read_kv(p, "ke", v, sizeof v), "read_kv: a key that only starts another");
    at(p, sizeof p, "kv/none.txt");
    check(!js_read_kv(p, "a", v, sizeof v), "read_kv: a file not there");
    /* A path as long as Linux allows, and a line too long to be one. */
    memset(v, 'p', 4000);
    v[4000] = 0;
    snprintf(text, sizeof text, "game_dir=/%s\nscale=3\n", v);
    write_at("kv/long.txt", text);
    at(p, sizeof p, "kv/long.txt");
    check(js_read_kv(p, "game_dir", v, sizeof v) && strlen(v) == 4001, "read_kv: a 4001-byte value");
    memset(text, 'x', 9000);
    memcpy(text, "scale=4", 7);
    memcpy(text + 8100, "\nscale=5", 8);   /* past the line buffer: part of the long line */
    snprintf(text + 9000, sizeof text - 9000, "\nscale=6\n");
    write_at("kv/toolong.txt", text);
    at(p, sizeof p, "kv/toolong.txt");
    check(js_read_kv(p, "scale", v, sizeof v) && !strcmp(v, "6"),
          "read_kv: a line over 8 KB skipped whole, the next one read");
}

/* ---- when the setup runs ---- */

static void engine_at(const char *root_rel, const char *text)
{
    char rel[512];
#ifdef _WIN32
    snprintf(rel, sizeof rel, "%s/engine/jsrf_recomp.exe", root_rel);
#else
    snprintf(rel, sizeof rel, "%s/bin/jsrf_recomp", root_rel);
#endif
    write_at(rel, text);
}

static void test_ready(void)
{
    char root[4096], sha[65], t[256];
    js_sha256_hex("engine A\n", 9, sha);

    at(root, sizeof root, "ready/none");
    js_mkdirs(root);
    check(!jsrf_setup_engine_ready(root) && !jsrf_setup_needed(root), "no kit, no engine: no setup");

    at(root, sizeof root, "ready/engine-only");
    engine_at("ready/engine-only", "engine A\n");
    check(!jsrf_setup_needed(root), "no kit (a package with the engine built in): no setup");

    at(root, sizeof root, "ready/kit");
    write_at("ready/kit/kit/kit.json", "{}\n");
    snprintf(t, sizeof t, "gen_sha256=%064d\nengine_sha256=%s\n", 0, sha);
    write_at("ready/kit/kit/expected.txt", t);
    check(jsrf_setup_needed(root), "a kit and no engine: the setup");
    engine_at("ready/kit", "engine A\n");
    check(jsrf_setup_engine_ready(root) && !jsrf_setup_needed(root), "a kit and its engine: ready");
    engine_at("ready/kit", "engine B\n");
    check(jsrf_setup_needed(root), "a kit and another engine: the setup");
    snprintf(t, sizeof t, "engine_sha256=%s\n", sha);
    js_sha256_hex("engine B\n", 9, sha);
    snprintf(t, sizeof t, "engine_sha256=%s\n", sha);
    write_at("ready/kit/build/engine.sha256", t);
    check(jsrf_setup_needed(root), "another engine, built here but not accepted: the setup");
    snprintf(t, sizeof t, "engine_sha256=%s\naccepted=%s\n", sha, sha);
    write_at("ready/kit/build/engine.sha256", t);
    check(jsrf_setup_engine_ready(root), "another engine the player said to play: ready");
    engine_at("ready/kit", "engine C\n");
    check(jsrf_setup_needed(root), "an engine changed after it was accepted: the setup");

    /* A kit made before its engine's hash was known: the one built here. */
    at(root, sizeof root, "ready/unknown");
    write_at("ready/unknown/kit/kit.json", "{}\n");
    write_at("ready/unknown/kit/expected.txt", "engine_sha256=\n");
    engine_at("ready/unknown", "engine A\n");
    check(jsrf_setup_needed(root), "a kit without the engine's hash, nothing built here: the setup");
    js_sha256_hex("engine A\n", 9, sha);
    snprintf(t, sizeof t, "engine_sha256=%s\n", sha);
    write_at("ready/unknown/build/engine.sha256", t);
    check(jsrf_setup_engine_ready(root), "a kit without the engine's hash, the engine built here: ready");
}

/* ---- the tools folder ---- */

static void test_tools(void)
{
    char root[4096], t[256];
    size_t n, i;
    const js_download_item *d = js_downloads(&n);
    int ok = n == 3;
#ifdef _WIN32
    const char *py = "tools/tools/python/python.exe";
#else
    const char *py = "tools/tools/python/bin/python3";
#endif
    /* What the setup downloads is what its mark names, each pinned. */
    for (i = 0; ok && i < n; i++) {
        ok = !strncmp(d[i].url, "https://", 8) && strlen(d[i].sha256) == 64 &&
             strspn(d[i].sha256, "0123456789abcdef") == 64;
    }
    check(ok, "the three downloads: https, each with its SHA-256");
    check(n == 3 && strstr(d[0].url, "/20241016/cpython-3.13.0%2B20241016-") &&
          !strncmp(d[1].name, "capstone-5.0.9-", 15) && !strncmp(d[2].name, "ziglang-0.13.0-", 15) &&
          !strcmp(js_tools_id(), "python-3.13.0-20241016+capstone-5.0.9+ziglang-0.13.0"),
          "the tools' mark names the versions downloaded");

    at(root, sizeof root, "tools");
    js_mkdirs(root);
    check(!js_tools_ready(root), "no tools folder: not ready");
    snprintf(t, sizeof t, "tools=%s\r\n", js_tools_id());
    write_at("tools/tools/TOOLS.txt", t);
    check(!js_tools_ready(root), "the mark but no Python: not ready");
    write_at(py, "");
    check(js_tools_ready(root), "the mark (CR LF, as the Windows archive's) and Python: ready");
    write_at("tools/tools/TOOLS.txt", "tools=python-3.12.0+capstone-5.0.1+ziglang-0.12.0\n");
    check(!js_tools_ready(root), "another release's tools: not ready");
}

/* ---- a download that fails ---- */

struct dl_seen {
    int failed_try, waiting, no_curl;
    time_t waited_at;
    volatile int cancel;
};

static void dl_update(void *ctx, double from, double to, int secs, const char *text, const char *log)
{
    struct dl_seen *s = (struct dl_seen *)ctx;
    (void)from; (void)to; (void)secs;
    if (log && strstr(log, "download try 1 of 4 failed")) s->failed_try = 1;
    if (log && strstr(log, "no libcurl.so.4")) s->no_curl = 1;
    /* The first wait: the window is closed then, as a player might. */
    if (text && strstr(text, "trying again in 5 s")) {
        s->waiting = 1;
        s->waited_at = time(NULL);
        s->cancel = 1;
    }
}

static void test_download_again(void)
{
    char dir[4096], dest[4096], part[4096 + 8], err[2048] = "";
    struct dl_seen seen;
    js_progress p;
    int rc;
    memset(&seen, 0, sizeof seen);
    p.update = dl_update;
    p.cancel = &seen.cancel;
    p.ctx = &seen;
    at(dir, sizeof dir, "dl");
    js_mkdirs(dir);
    at(dest, sizeof dest, "dl/tools.bin");
    snprintf(part, sizeof part, "%s.part", dest);
    /* Nothing listens on port 9 here: every try fails at once. No proxy, so
     * the try is this machine's own. */
    setenv("no_proxy", "*", 1);
    setenv("NO_PROXY", "*", 1);
    rc = js_download("https://127.0.0.1:9/tools.bin", dest,
                     "0000000000000000000000000000000000000000000000000000000000000000", 0,
                     &p, 0.0, 0.1, err, sizeof err);
    if (seen.no_curl) {
        /* Linux without libcurl: nothing a later try would change. */
        check(rc != 0 && !seen.waiting, "a download with no libcurl: one try, no wait");
    } else {
        check(rc != 0 && seen.failed_try, "a download that fails: the failed try in the log");
        check(seen.waiting, "  tried again after a wait, which the bar says");
        check(!strcmp(err, "cancelled") && time(NULL) - seen.waited_at <= 1,
              "  and closing the window in the wait stops it at once");
    }
    check(!js_is_file(dest) && !js_is_file(part), "  nothing of it left");
}

/* ---- the bar ---- */

static int close_to(double a, double b)
{
    return a > b - 1e-9 && a < b + 1e-9;
}

static void test_bar(void)
{
    js_bar b;
    memset(&b, 0, sizeof b);
    js_bar_set(&b, 100, 0.2, 0.6, 4);
    check(close_to(js_bar_at(&b, 100), 0.2), "the bar: a step starts where it says");
    check(close_to(js_bar_at(&b, 102), 0.2 + 0.4 * 0.45) && close_to(js_bar_at(&b, 104), 0.2 + 0.4 * 0.9),
          "  eases towards its end, 90% of the way at its time");
    check(js_bar_at(&b, 1e9) < 0.6 && js_bar_at(&b, 1e9) > 0.2 + 0.4 * 0.98,
          "  and creeps on after it, never quite there");
    js_bar_set(&b, 105, -1, 0.9, 10);
    check(close_to(js_bar_at(&b, 104), 0.2 + 0.4 * 0.9), "  an update with no position changes nothing");
    js_bar_set(&b, 106, 0.5, -1, 0);
    check(close_to(js_bar_at(&b, 200), 0.5), "  a position with no end stays there");
    js_bar_set(&b, 107, 1.5, 2.0, 1);
    check(close_to(js_bar_at(&b, 107), 1.0), "  and never past the whole");
}

/* ---- unpacking ---- */

static void test_untar(void)
{
    static const char *const bad[] = {
        "dotdot", "absolute", "inner-dotdot", "link-out", "link-absolute", "link-through", "hardlink-out",
    };
    char tgz[4096], dest[4096], err[512], p[4096], name[256];
    size_t i;

    at(tgz, sizeof tgz, "tar/good.tar.gz");
    at(dest, sizeof dest, "untar/good");
    js_mkdirs(dest);
    err[0] = 0;
    check(js_untar_gz(tgz, dest, err, sizeof err) == 0, "untar: Python's layout unpacked");
    if (err[0]) printf("     %s\n", err);
    at(tgz, sizeof tgz, "tar/pax.tar.gz");
    check(js_untar_gz(tgz, dest, err, sizeof err) == 0, "untar: a pax archive unpacked");
    check(exists("untar/good/python/bin/python3.13") && exists("untar/good/python/lib/a/b.txt"),
          "untar: files and folders, a folder the archive never lists included");
    {
        char text[64] = "";
        FILE *f;
        at(p, sizeof p, "untar/good/python/lib/a/b.txt");
        f = fopen(p, "rb");
        if (f) { size_t got = fread(text, 1, sizeof text - 1, f); text[got] = 0; fclose(f); }
        check(!strcmp(text, "bee\n"), "untar: a file's bytes");
    }
    snprintf(name, sizeof name, "untar/good/python/%s/deep.txt",
             "d123456789d123456789d123456789d123456789d123456789d123456789d123456789d123456789d123456789d123456789x");
    check(exists(name), "untar: a name over 100 bytes (GNU's long name)");
    snprintf(name, sizeof name, "untar/good/python/%s/pax.txt",
             "p123456789p123456789p123456789p123456789p123456789p123456789p123456789p123456789p123456789p123456789y");
    check(exists(name), "untar: a name over 100 bytes (pax's path)");
#ifndef _WIN32
    {
        struct stat st;
        char target[256] = "";
        ssize_t l;
        at(p, sizeof p, "untar/good/python/bin/python3.13");
        check(!stat(p, &st) && (st.st_mode & 0777) == 0755, "untar: a program's mode (0755)");
        at(p, sizeof p, "untar/good/python/bin/python3");
        l = readlink(p, target, sizeof target - 1);
        if (l > 0) target[l] = 0;
        check(!strcmp(target, "python3.13"), "untar: a link beside its target (bin/python3 -> python3.13)");
        at(p, sizeof p, "untar/good/python/lib/up.txt");
        l = readlink(p, target, sizeof target - 1);
        if (l > 0) target[l] = 0;
        check(!strcmp(target, "../bin/python3.13"), "untar: a link up one folder, still inside");
        check(exists("untar/good/python/lib/hard.txt"), "untar: a hard link inside");
    }
#endif
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char what[1024];
        int rc;
#ifdef _WIN32
        if (!strncmp(bad[i], "link", 4) || !strncmp(bad[i], "hard", 4)) continue;   /* links: Linux's */
#endif
        snprintf(p, sizeof p, "tar/%s.tar.gz", bad[i]);
        at(tgz, sizeof tgz, p);
        snprintf(p, sizeof p, "untar/%s/in", bad[i]);
        at(dest, sizeof dest, p);
        js_mkdirs(dest);
        err[0] = 0;
        rc = js_untar_gz(tgz, dest, err, sizeof err);
        snprintf(p, sizeof p, "untar/%s/evil.txt", bad[i]);
        snprintf(what, sizeof what, "untar: %s refused, nothing written outside (%s)", bad[i], err);
        check(rc != 0 && !exists(p) && !exists("evil.txt") && strstr(err, "outside"), what);
    }
    at(tgz, sizeof tgz, "tar/cut.tar.gz");
    at(dest, sizeof dest, "untar/cut");
    err[0] = 0;
    check(js_untar_gz(tgz, dest, err, sizeof err) != 0 && err[0], "untar: an archive cut short");
    at(tgz, sizeof tgz, "million.txt");
    err[0] = 0;
    check(js_untar_gz(tgz, dest, err, sizeof err) != 0 && strstr(err, "isn't a .tar.gz"), "untar: not a .tar.gz");
}

/* ---- Delete ---- */

static void test_delete(void)
{
    char root[4096];
    static const char *const made[] = {
        "del/tools/python/x", "del/build/generated/a.c", "del/build/work/w", "del/build/obj/a.o",
        "del/build/tmp/t", "del/build/default.xbe",
    };
    size_t i;
    for (i = 0; i < sizeof made / sizeof made[0]; i++) write_at(made[i], "12345");
    write_at("del/build/build.log", "log\n");
    write_at("del/build/engine.sha256", "engine_sha256=x\n");
    engine_at("del", "engine\n");
    at(root, sizeof root, "del");
    check(js_tools_size(root) == 25, "Delete's size: the tools, the generated code, its objects and default.xbe");
    check(js_delete_tools(root) == 0, "Delete: done");
    check(!exists("del/tools") && !exists("del/build/generated") && !exists("del/build/work") &&
          !exists("del/build/obj") && !exists("del/build/tmp") && !exists("del/build/default.xbe"),
          "Delete: the tools and everything made from the game gone");
#ifdef _WIN32
    check(exists("del/engine/jsrf_recomp.exe") && exists("del/build/build.log") && exists("del/build/engine.sha256"),
          "Delete: the engine, the log and the stamp kept");
#else
    check(exists("del/bin/jsrf_recomp") && exists("del/build/build.log") && exists("del/build/engine.sha256"),
          "Delete: the engine, the log and the stamp kept");
#endif
}

#ifndef _WIN32
/* ---- running a program ---- */

static char g_lines[16][512];
static int g_nlines;

static void collect(void *ctx, const char *text)
{
    (void)ctx;
    if (g_nlines < 16) snprintf(g_lines[g_nlines++], sizeof g_lines[0], "%s", text);
}

static void test_run(void)
{
    char err[256] = "", cwd[4096];
    const char *argv[] = { "/bin/sh", "-c",
        "echo \"$ONLY\"; echo \"${HOME-no home}\"; echo \"${JSRF_DECOY-no decoy}\"; pwd; printf 'no newline'; exit 3", NULL };
    const char *env[] = { "ONLY=given", NULL };
    const char *missing[] = { "/nonexistent/program", NULL };
    int rc;
    at(cwd, sizeof cwd, "run");
    js_mkdirs(cwd);
    setenv("JSRF_DECOY", "set", 1);
    rc = js_run(argv, env, cwd, collect, NULL, NULL, err, sizeof err);
    check(rc == 3, "run: the exit code");
    check(g_nlines == 5 && !strcmp(g_lines[0], "given") && !strcmp(g_lines[1], "no home") &&
          !strcmp(g_lines[2], "no decoy"), "run: only the environment given (no HOME, no JSRF_DECOY)");
    {
        char real[4096];
        check(g_nlines == 5 && realpath(cwd, real) && !strcmp(g_lines[3], real), "run: in the folder given");
    }
    check(g_nlines == 5 && !strcmp(g_lines[4], "no newline"), "run: the last line, without a newline");
    err[0] = 0;
    rc = js_run(missing, env, cwd, collect, NULL, NULL, err, sizeof err);
    check(rc != 0, "run: a program that isn't there: not 0");
    unsetenv("JSRF_DECOY");
}

/* ---- the first start, the console's way ---- */

struct game { const char *path; int give; };

static int find_game(void *ctx, char *path, size_t n)
{
    struct game *g = (struct game *)ctx;
    if (!g->give) return 0;
    snprintf(path, n, "%s", g->path);
    return 1;
}

static int console(const char *rel, const char *mode, const char *answer, const char *tools,
                   const char *mismatch, int give_game, const char *test_gen)
{
    static jsrf_setup s;
    struct game g;
    char root[4096], game[4096], p[4096];
    at(root, sizeof root, rel);
    at(game, sizeof game, "game");
    js_path(p, sizeof p, root, "mode.txt");
    js_write_text(p, mode);
    memset(&s, 0, sizeof s);
    g.path = game;
    g.give = give_game;
    s.root = root;
    s.find_game = find_game;
    s.ctx = &g;
    if (answer) setenv("JSRF_SETUP_ANSWER", answer, 1); else unsetenv("JSRF_SETUP_ANSWER");
    if (tools) setenv("JSRF_SETUP_TOOLS", tools, 1); else unsetenv("JSRF_SETUP_TOOLS");
    if (mismatch) setenv("JSRF_SETUP_MISMATCH", mismatch, 1); else unsetenv("JSRF_SETUP_MISMATCH");
    if (test_gen) setenv("JSRF_SETUP_TEST_GEN", test_gen, 1); else unsetenv("JSRF_SETUP_TEST_GEN");
    printf("---- the first start in %s (%s)\n", rel, mode);
    fflush(stdout);
    return jsrf_setup_run_console(&s);
}

/* A package without the engine, with the stand-in tools (run.py's). */
static void package(const char *rel, const char *expected_engine)
{
    char from[4096], to[4096], t[512], cmd[9000];
    snprintf(t, sizeof t, "%s/kit/kit.json", rel);
    write_at(t, "{}\n");
    snprintf(t, sizeof t, "%s/kit/build_engine.py", rel);
    write_at(t, "# the stand-in Python doesn't read it\n");
    snprintf(cmd, sizeof cmd, "gen_sha256=%064d\nengine_sha256=%s\ntools=%s\n", 0, expected_engine, js_tools_id());
    snprintf(t, sizeof t, "%s/kit/expected.txt", rel);
    write_at(t, cmd);
    at(from, sizeof from, "stand-in/tools");
    at(to, sizeof to, rel);
    snprintf(cmd, sizeof cmd, "cp -R '%s' '%s/'", from, to);
    if (system(cmd)) { printf("FAIL %s\n", cmd); exit(1); }
}

static int file_has(const char *rel, const char *text)
{
    char p[4096], buf[16384];
    size_t got;
    FILE *f;
    at(p, sizeof p, rel);
    f = fopen(p, "rb");
    if (!f) return 0;
    got = fread(buf, 1, sizeof buf - 1, f);
    buf[got] = 0;
    fclose(f);
    return strstr(buf, text) != NULL;
}

static void test_console(void)
{
    char root[4096], want[65], gen[4096], args[20000];
    int rc;
    js_sha256_hex("the stand-in engine\n", 20, want);
    at(gen, sizeof gen, "gen");
    js_mkdirs(gen);
    write_at("game/default.xbe", "not the game\n");
    write_at("game/Media/x", "");

    package("c-no", want);
    rc = console("c-no", "match", "no", NULL, NULL, 1, gen);
    check(rc == JSRF_SETUP_CLOSED && !exists("c-no/bin/jsrf_recomp"), "console: No closes, nothing built");

    package("c-cancel", want);
    rc = console("c-cancel", "match", "yes", NULL, NULL, 0, gen);
    check(rc == JSRF_SETUP_CLOSED && !exists("c-cancel/bin/jsrf_recomp"),
          "console: no game given (the dialog cancelled): closed, nothing built");

    package("c-keep", want);
    rc = console("c-keep", "match", "yes", "keep", NULL, 1, gen);
    at(root, sizeof root, "c-keep");
    check(rc == JSRF_SETUP_READY && jsrf_setup_engine_ready(root) && !jsrf_setup_needed(root),
          "console: Yes builds the kit's engine: ready");
    check(exists("c-keep/tools/python/bin/python3") && exists("c-keep/build/generated/x.c"), "console: Keep keeps the tools");
    snprintf(args, sizeof args, "-E -s -u -X utf8 %s/kit/build_engine.py --root %s --xbe %s/build/default.xbe --test-gen %s",
             root, root, root, gen);
    check(file_has("c-keep/build/py-args.txt", args), "console: the tools' Python run isolated (-E -s), with the kit's script");
    {
        char envl[4600];
        at(gen, sizeof gen, "c-keep/build/tmp");
        snprintf(envl, sizeof envl, "HOME=%s\n", gen);
        check(file_has("c-keep/build/py-env.txt", "PATH=/nonexistent\n") && file_has("c-keep/build/py-env.txt", envl) &&
              !file_has("c-keep/build/py-env.txt", "JSRF_SETUP") && !file_has("c-keep/build/py-env.txt", "JSRF_DECOY"),
              "console: Python's environment its own: no PATH, HOME in build/tmp, nothing of the launcher's");
        at(gen, sizeof gen, "gen");
    }
    check(file_has("c-keep/build/build.log", "step 0.5 1 2 Compiling"), "console: the build's lines in build/build.log");

    package("c-delete", want);
    setenv("JSRF_DECOY", "set", 1);
    rc = console("c-delete", "match", "yes", "delete", NULL, 1, gen);
    unsetenv("JSRF_DECOY");
    at(root, sizeof root, "c-delete");
    check(rc == JSRF_SETUP_READY && jsrf_setup_engine_ready(root) && !exists("c-delete/tools") &&
          !exists("c-delete/build/generated") && exists("c-delete/bin/jsrf_recomp"),
          "console: Delete takes the tools and the generated code, and keeps the engine");

    package("c-stop", "1111111111111111111111111111111111111111111111111111111111111111");
    rc = console("c-stop", "match", "yes", "keep", NULL, 1, gen);
    at(root, sizeof root, "c-stop");
    check(rc == JSRF_SETUP_FAILED && jsrf_setup_needed(root),
          "console: another engine, no answer to play it: stopped, the setup again next time");
    rc = console("c-stop", "match", "yes", "keep", "play", 1, gen);
    check(rc == JSRF_SETUP_READY && jsrf_setup_engine_ready(root) && file_has("c-stop/build/engine.sha256", "accepted="),
          "console: another engine, played anyway: accepted, ready");

    package("c-error", want);
    rc = console("c-error", "error", "yes", "keep", NULL, 1, gen);
    check(rc == JSRF_SETUP_FAILED && !exists("c-error/bin/jsrf_recomp") &&
          file_has("c-error/build/build.log", "error: the stand-in stopped"), "console: the build's error: failed, said why");

    package("c-game", want);
    rc = console("c-game", "match", "yes", "keep", NULL, 1, NULL);
    check(rc == JSRF_SETUP_FAILED && file_has("c-game/build/build.log", "These files won't work with JSRF-PC"),
          "console: a game that isn't JSRF's default.xbe: refused before the build");

    package("c-notools", want);
    {
        char p[4096];
        at(p, sizeof p, "c-notools/tools/TOOLS.txt");
        remove(p);
    }
    unsetenv("JSRF_SETUP_ANSWER");
    setenv("JSRF_SETUP_ANSWER", "no", 1);
    {
        /* With no tools folder the question is the download's: asked, and No. */
        static jsrf_setup s;
        memset(&s, 0, sizeof s);
        at(root, sizeof root, "c-notools");
        s.root = root;
        printf("---- the first start in c-notools (no tools)\n");
        fflush(stdout);
        check(jsrf_setup_run_console(&s) == JSRF_SETUP_CLOSED, "console: no tools folder: the download's question, and No");
    }
}
#endif

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: test <work dir>\n"); return 2; }
    snprintf(work, sizeof work, "%s", argv[1]);
    setvbuf(stdout, NULL, _IOLBF, 0);
    test_sha256();
    test_kv();
    test_ready();
    test_tools();
    test_download_again();
    test_bar();
    test_untar();
    test_delete();
#ifndef _WIN32
    test_run();
    test_console();
#endif
    printf("setup: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
