/*
 * jsrf-pc, the Linux launcher (b2 row 4b: "Launchers, Linux one in C too",
 * 2026-11-09). It does what the shell script before it did, step for step,
 * and shares the setup with JSRF-PC.exe (launcher/setup).
 *
 * Finds your copy of Jet Set Radio Future (the folder with default.xbe in it),
 * remembers it, and starts the game with your settings.
 *
 *   ./jsrf-pc                  play (asks where the game is, the first time)
 *   ./jsrf-pc /path/to/game    use that folder (or a disc image -- see below), and remember it
 *   ./jsrf-pc --choose         ask where the game is again
 *   ./jsrf-pc --scale N        internal resolution, 1 to 6 times 640x480 (default 2); remembered
 *   ./jsrf-pc --fullscreen     start full screen
 *   ./jsrf-pc --install        add JSRF-PC to your applications menu
 *   ./jsrf-pc --version        which version this is
 *
 * A disc image (.iso) is unpacked once, by the game itself: nothing else to install.
 * Portable: with a folder named "portable" beside the launcher, the settings,
 * saves, and logs are kept in it, and nothing in your home folder.
 * In game: F1 the in-game menu, F11 (or Alt+Return) full screen, F10 4:3 or your
 * screen's shape (the others in Display > Aspect ratio), F9 resolution, and F8
 * the free camera.
 *
 * In a package without the engine (one with a kit/ folder,
 * launcher/firststart/package.sh), the setup builds it first, when
 * bin/jsrf_recomp is missing or isn't the one the kit builds.
 *
 * Dialogs are zenity's or kdialog's when there is a desktop and one of them
 * is installed, else the terminal, as before. Every program is started by
 * its path with its own arguments, never through a shell.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../setup/setup.h"

/* The scales the engine draws at, 1x to this: its NV2A_SCALE_MAX
 * (src/nv2a_gl/nv2a_scale.h), one digit. */
#define SCALE_MAX 6
#define FOLD 72

static const char HELP[] =
    "JSRF-PC -- the Linux launcher.\n"
    "\n"
    "Finds your copy of Jet Set Radio Future (the folder with default.xbe in it),\n"
    "remembers it, and starts the game with your settings.\n"
    "\n"
    "  ./jsrf-pc                  play (asks where the game is, the first time)\n"
    "  ./jsrf-pc /path/to/game    use that folder (or a disc image -- see below), and remember it\n"
    "  ./jsrf-pc --choose         ask where the game is again\n"
    "  ./jsrf-pc --scale N        internal resolution, 1 to 6 times 640x480 (default 2); remembered\n"
    "  ./jsrf-pc --fullscreen     start full screen\n"
    "  ./jsrf-pc --install        add JSRF-PC to your applications menu\n"
    "  ./jsrf-pc --version        which version this is\n"
    "\n"
    "A disc image (.iso) is unpacked once, by the game itself: nothing else to install.\n"
    "Portable: with a folder named \"portable\" beside this launcher, the settings,\n"
    "saves, and logs are kept in it, and nothing in your home folder.\n"
    "In game: F1 the in-game menu, F11 (or Alt+Return) full screen, F10 4:3 or your\n"
    "screen's shape (the others in Display > Aspect ratio), F9 resolution, and F8\n"
    "the free camera.\n";

static char g_here[PATH_MAX];
static char g_portable[PATH_MAX];   /* "" when not portable */
static char g_cfg[PATH_MAX + 64], g_data[PATH_MAX + 64], g_save[PATH_MAX + 64];
static char g_state[PATH_MAX + 64], g_logs[PATH_MAX + 64];
static char g_engine[PATH_MAX + 32], g_libpath[PATH_MAX * 2];

/* ---- small things ----------------------------------------------------------- */

static void fmt(char *out, size_t n, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    vsnprintf(out, n, f, ap);
    va_end(ap);
}

static int has_suffix_ci(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcasecmp(s + a - b, suf);
}

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static int is_game(const char *dir)
{
    char p[PATH_MAX + 32];
    if (!dir || !dir[0]) return 0;
    fmt(p, sizeof p, "%s/default.xbe", dir);
    if (!js_is_file(p)) return 0;
    fmt(p, sizeof p, "%s/Media", dir);
    return js_is_dir(p);
}

static int gui(void)
{
    const char *d = getenv("DISPLAY"), *w = getenv("WAYLAND_DISPLAY");
    return (d && *d) || (w && *w);
}

/* A program on PATH, as the shell's command -v finds it. */
static int on_path(const char *name, char *out, size_t n)
{
    const char *p = getenv("PATH"), *e;
    if (!p || !*p) return 0;
    for (;; p = e + 1) {
        size_t len;
        e = strchr(p, ':');
        len = e ? (size_t)(e - p) : strlen(p);
        if (len) fmt(out, n, "%.*s/%s", (int)len, p, name);
        else fmt(out, n, "./%s", name);          /* an empty entry: here */
        if (!access(out, X_OK) && js_is_file(out)) return 1;
        if (!e) return 0;
    }
}

/* `fold -s -w 72`: lines longer than that broken after the last space
 * within them, for the dialogs. */
static void fold(const char *in, char *out, size_t n)
{
    size_t o = 0, start = 0, i;   /* start: where out's last line begins */
    if (!n) return;
    for (i = 0; in[i] && o + 2 < n; i++) {
        if (in[i] != '\n' && o - start == FOLD) {
            size_t k = o;
            while (k > start && out[k - 1] != ' ') k--;
            if (k > start) {          /* after the last space: the rest goes down */
                memmove(out + k + 1, out + k, o - k);
                out[k] = '\n';
                o++;
                start = k + 1;
            } else {                  /* no space: cut where it is */
                out[o++] = '\n';
                start = o;
            }
            if (o + 2 >= n) break;
        }
        out[o++] = in[i];
        if (in[i] == '\n') start = o;
    }
    out[o] = 0;
}

static void chomp(char *s)
{
    size_t l = strlen(s);
    while (l && (s[l - 1] == '\n' || s[l - 1] == '\r')) s[--l] = 0;
}

/* ---- running programs ---------------------------------------------------------- */

/* argv run with stdin from in_fd (-1: as ours), stdout to out_fd (-1: as
 * ours; -2: /dev/null) and stderr to /dev/null when quiet. Its pid, or -1. */
static pid_t spawn(char *const argv[], int in_fd, int out_fd, int quiet, const char *cwd)
{
    pid_t pid = fork();
    if (pid != 0) return pid;
    if (in_fd >= 0) { dup2(in_fd, 0); close(in_fd); }
    if (out_fd == -2) {
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, 1); close(nul); }
    } else if (out_fd >= 0) {
        dup2(out_fd, 1);
        close(out_fd);
    }
    if (quiet) {
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, 2); close(nul); }
    }
    if (cwd && chdir(cwd)) _exit(127);
    signal(SIGPIPE, SIG_DFL);
    execv(argv[0], argv);
    _exit(127);
}

/* The exit status as the shell gives it: 128 + the signal for one killed. */
static int wait_status(pid_t pid)
{
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR) return 127;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return 127;
}

/* argv run, its output (stdout only) in out, the rest dropped. */
static int capture(char *const argv[], char *out, size_t n)
{
    int fd[2];
    size_t have = 0;
    pid_t pid;
    if (n) out[0] = 0;
    if (pipe(fd)) return 127;
    pid = spawn(argv, -1, fd[1], 1, NULL);
    close(fd[1]);
    if (pid < 0) { close(fd[0]); return 127; }
    for (;;) {
        char buf[4096];
        ssize_t got = read(fd[0], buf, sizeof buf);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        if (n && have + 1 < n) {
            size_t k = (size_t)got < n - 1 - have ? (size_t)got : n - 1 - have;
            memcpy(out + have, buf, k);
            have += k;
        }
    }
    close(fd[0]);
    if (n) out[have] = 0;
    return wait_status(pid);
}

/* ---- what the player sees -------------------------------------------------------- */

/* A message the player can see: a dialog when there is a desktop to show one. */
static void say(const char *text)
{
    char prog[PATH_MAX], *t = (char *)text;
    if (gui() && on_path("zenity", prog, sizeof prog)) {
        char *a[] = { prog, "--info", "--no-wrap", "--title=JSRF-PC", NULL, NULL };
        char arg[8192];
        fmt(arg, sizeof arg, "--text=%s", t);
        a[4] = arg;
        { pid_t p = spawn(a, -1, -1, 1, NULL); if (p > 0) wait_status(p); }
    } else if (gui() && on_path("kdialog", prog, sizeof prog)) {
        char *a[] = { prog, "--title", "JSRF-PC", "--msgbox", t, NULL };
        pid_t p = spawn(a, -1, -1, 1, NULL);
        if (p > 0) wait_status(p);
    } else {
        fprintf(stderr, "%s\n", text);
    }
}

static void fail(const char *text)
{
    say(text);
    exit(1);
}

static void failf(const char *f, ...)
{
    char raw[8192], text[9000];
    va_list ap;
    va_start(ap, f);
    vsnprintf(raw, sizeof raw, f, ap);
    va_end(ap);
    fold(raw, text, sizeof text);
    chomp(text);          /* as the script's $(...) took the last newlines off */
    fail(text);
}

/* ---- the settings file: key=value lines, the last one counting ------------------ */

static int cfg_get(const char *key, char *val, size_t n)
{
    return js_read_kv(g_cfg, key, val, n);
}

/* Replace or append, keeping every other line. */
static void cfg_set(const char *key, const char *val)
{
    char tmp[PATH_MAX + 80], line[8192];
    size_t kl = strlen(key);
    FILE *in = fopen(g_cfg, "r"), *out;
    fmt(tmp, sizeof tmp, "%s.tmp", g_cfg);
    out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); return; }
    if (in) {
        while (fgets(line, sizeof line, in))
            if (strncmp(line, key, kl) || line[kl] != '=') fputs(line, out);
        fclose(in);
    }
    fprintf(out, "%s=%s\n", key, val);
    if (fclose(out) == 0) rename(tmp, g_cfg);
}

/* ---- the engine ---------------------------------------------------------------- */

static void engine_env(void)
{
    setenv("LD_LIBRARY_PATH", g_libpath, 1);
}

/* The engine's answer to --check-game or --check-iso: its exit code, what it
 * printed in why. */
static int engine_check(const char *opt, const char *what, char *why, size_t n)
{
    char *a[] = { g_engine, (char *)opt, (char *)what, NULL };
    int rc = capture(a, why, n);
    chomp(why);
    return rc;
}

/* Files that are not the game JSRF-PC runs: say what they are (the
 * engine's own check, port/xbe_check.c, names them) and forget them. */
static void refuse(const char *why)
{
    cfg_set("game_dir", "");
    failf("These files won't work with JSRF-PC.\n\n%s\n", why);
}

/* ---- a disc image ------------------------------------------------------------- */

/* The engine's progress while it unpacks ("progress <done> <total>") for
 * zenity (zfd >= 0: "# text" first and the percentage last -- zenity closes on
 * 100) or the terminal; everything else, its answer, into unpack.out. 0 if
 * zenity went away (Cancel). */
static int relay_line(const char *line, int zfd, const char *name, FILE *answer)
{
    unsigned long long d, t;
    if (sscanf(line, "progress %llu %llu", &d, &t) == 2) {
        char out[512];
        int len;
        if (t == 0) return 1;
        if (zfd >= 0) {
            len = snprintf(out, sizeof out, "# Unpacking %s -- %llu.%llu of %llu.%llu GB\n%llu\n", name,
                           d / 1000000000ull, d / 100000000ull % 10, t / 1000000000ull, t / 100000000ull % 10,
                           d * 100 / t);
            if (len > 0 && write(zfd, out, (size_t)len) != len) return 0;
        } else {
            fprintf(stderr, "\r  %llu.%llu of %llu.%llu GB", d / 1000000000ull, d / 100000000ull % 10,
                    t / 1000000000ull, t / 100000000ull % 10);
        }
        return 1;
    }
    fprintf(answer, "%s\n", line);
    fflush(answer);
    return 1;
}

/* Unpacked once into ~/.local/share/jsrf-pc/<name> (in portable mode,
 * portable/games/<name>), by the engine (--unpack-iso: the unpacker the other
 * apps use, port/xiso.c), so nothing else has to be installed. Checked first,
 * so a zip, another game's disc or another build of the game is named before
 * anything is unpacked. The game folder in out; "" if unpacking was
 * cancelled. */
static void from_iso(const char *iso, char *out, size_t n)
{
    char name[NAME_MAX + 1], dest[PATH_MAX * 2], why[4096], ans_path[PATH_MAX + 32], zen[PATH_MAX];
    char *dot;
    int rc, efd[2], zfd[2] = { -1, -1 }, use_zenity, engine_rc, zen_rc = 0, cancelled = 0, ok = 0;
    pid_t ep, zp = -1;
    FILE *answer, *ein;

    fmt(name, sizeof name, "%s", base_name(iso));
    dot = strrchr(name, '.');
    if (dot) *dot = 0;
    if (strlen(name) >= 5 && !strcmp(name + strlen(name) - 5, ".xiso")) name[strlen(name) - 5] = 0;
    fmt(dest, sizeof dest, "%s/%s", g_data, name);
    if (is_game(dest)) { fmt(out, n, "%s", dest); return; }
    rc = engine_check("--check-iso", iso, why, sizeof why);
    if (rc == 2 || rc == 3 || rc == 4) refuse(why);
    if (rc != 0) failf("Could not read %s.", iso);
    js_rmtree(dest);
    fmt(ans_path, sizeof ans_path, "%s/unpack.out", g_state);
    answer = fopen(ans_path, "w");
    if (!answer) failf("Could not write %s.", ans_path);

    use_zenity = gui() && on_path("zenity", zen, sizeof zen);
    if (pipe(efd)) fail("Could not start the game to unpack the disc image.");
    {
        char *a[] = { g_engine, "--unpack-iso", (char *)iso, dest, NULL };
        ep = spawn(a, -1, efd[1], 1, NULL);
        close(efd[1]);
    }
    if (use_zenity) {
        char text[PATH_MAX + 64];
        char *a[] = { zen, "--progress", "--title=JSRF-PC", "--auto-close", text, NULL };
        fmt(text, sizeof text, "--text=Unpacking %s -- this happens once.", base_name(iso));
        if (pipe(zfd) == 0) {
            zp = spawn(a, zfd[0], -1, 1, NULL);
            close(zfd[0]);
        }
    } else {
        fprintf(stderr, "Unpacking %s -- this happens once. The files go to:\n  %s\n", base_name(iso), dest);
    }
    ein = fdopen(efd[0], "r");
    if (ein) {
        char line[4096];
        while (fgets(line, sizeof line, ein)) {
            chomp(line);
            if (!relay_line(line, use_zenity ? zfd[1] : -1, base_name(iso), answer)) {
                cancelled = 1;                     /* zenity's Cancel: stop unpacking */
                if (ep > 0) kill(ep, SIGTERM);
                break;
            }
        }
        fclose(ein);
    } else {
        close(efd[0]);
    }
    if (zfd[1] >= 0) close(zfd[1]);
    engine_rc = ep > 0 ? wait_status(ep) : 127;
    if (zp > 0) zen_rc = wait_status(zp);
    fclose(answer);
    if (!use_zenity) fprintf(stderr, "\n");

    /* The answer: "ok", or what the engine said. */
    why[0] = 0;
    {
        FILE *f = fopen(ans_path, "r");
        char line[1024];
        size_t l = 0;
        while (f && fgets(line, sizeof line, f)) {
            chomp(line);
            if (!strcmp(line, "ok")) { ok = 1; continue; }
            l += (size_t)snprintf(why + l, l < sizeof why ? sizeof why - l : 0, "%s%s", l ? "\n" : "", line);
            if (l >= sizeof why) l = sizeof why - 1;
        }
        if (f) fclose(f);
    }
    if (use_zenity && (cancelled || zen_rc != 0) && !ok) {   /* Cancel */
        js_rmtree(dest);
        if (n) out[0] = 0;
        return;
    }
    if (engine_rc != 0 || !is_game(dest)) {
        js_rmtree(dest);
        if (engine_rc == 2 || engine_rc == 3 || engine_rc == 4) refuse(why);
        if (why[0]) failf("The disc image could not be unpacked.\n\n%s\n", why);
        failf("The disc image could not be unpacked.\n\n(exit %d)\n", engine_rc);
    }
    fmt(out, n, "%s", dest);
}

/* ---- the game ------------------------------------------------------------------ */

/* What was given or picked -> the game: the folder itself, the folder of a
 * default.xbe, a disc image (any file: the engine says what it is if it is not
 * one), or a folder with one disc image in it. With unpack, a disc image is
 * unpacked and out is its folder ("" if cancelled); without, out is the image
 * (the setup reads default.xbe out of it). */
static void pick(const char *p, char *out, size_t n, int unpack)
{
    char path[PATH_MAX * 2];
    fmt(path, sizeof path, "%s", p);
    if (js_is_file(path)) {
        if (!strcasecmp(base_name(path), "default.xbe")) {
            char *s = strrchr(path, '/');
            if (s == path) s[1] = 0;
            else if (s) *s = 0;
            else fmt(path, sizeof path, ".");
        } else {
            if (unpack) from_iso(path, out, n);
            else fmt(out, n, "%s", path);
            return;
        }
    }
    if (js_is_dir(path) && !is_game(path)) {
        DIR *d = opendir(path);
        struct dirent *e;
        char *imgs[64];
        int k = 0, total = 0;
        while (d && (e = readdir(d)) != NULL) {
            char f[PATH_MAX * 2 + NAME_MAX];
            if (!has_suffix_ci(e->d_name, ".iso") && !has_suffix_ci(e->d_name, ".xiso")) continue;
            fmt(f, sizeof f, "%s/%s", path, e->d_name);
            if (!js_is_file(f)) continue;
            total++;
            if (k < 64) imgs[k++] = strdup(f);
        }
        if (d) closedir(d);
        if (total == 1) {
            char one[PATH_MAX * 2 + NAME_MAX];
            fmt(one, sizeof one, "%s", imgs[0]);
            free(imgs[0]);
            if (unpack) from_iso(one, out, n);
            else fmt(out, n, "%s", one);
            return;
        }
        while (k) free(imgs[--k]);
        if (total > 1) {
            char text[160];
            fmt(text, sizeof text, "That folder has %d disc images in it: give the one of Jet Set Radio Future.", total);
            fail(text);
        }
    }
    fmt(out, n, "%s", path);
}

static int ask_game(char *out, size_t n)
{
    static char q[] = "Where is Jet Set Radio Future? Pick the folder that contains default.xbe, "
                      "or the folder your disc image (.iso) is in.";
    char prog[PATH_MAX], title[256];
    out[0] = 0;
    if (gui() && on_path("zenity", prog, sizeof prog)) {
        char *a[] = { prog, "--file-selection", "--directory", title, NULL };
        fmt(title, sizeof title, "--title=%s", q);
        capture(a, out, n);
    } else if (gui() && on_path("kdialog", prog, sizeof prog)) {
        const char *h = getenv("HOME");
        char *a[] = { prog, "--getexistingdirectory", (char *)(h ? h : "/"), "--title", q, NULL };
        capture(a, out, n);
    } else if (isatty(0)) {
        fprintf(stderr, "%s\nFolder: ", q);
        if (!fgets(out, (int)n, stdin)) out[0] = 0;
    }
    /* Only the first line: the folder (a newline in a name isn't one). */
    out[strcspn(out, "\r\n")] = 0;
    return out[0] != 0;
}

/* The game remembered: in portable mode, one inside this folder is kept
 * relative to it, so the whole folder can move (another path, an SD card)
 * and still find it. */
static void last_game(char *out, size_t n)
{
    char v[PATH_MAX];
    out[0] = 0;
    if (!cfg_get("game_dir", v, sizeof v) || !v[0]) return;
    if (g_portable[0] && v[0] != '/') fmt(out, n, "%s/%s", g_here, v);
    else fmt(out, n, "%s", v);
}

/* ---- the setup ------------------------------------------------------------------ */

struct game_ctx {
    const char *arg;   /* a game given on the command line */
    int choose;        /* --choose: ask again */
};

/* The game, for the setup: as below, but a disc image stays one (the setup
 * reads default.xbe out of it, and the game unpacks it afterwards). */
static int setup_find_game(void *vctx, char *path, size_t n)
{
    struct game_ctx *c = (struct game_ctx *)vctx;
    char got[PATH_MAX * 2];
    got[0] = 0;
    if (c->arg) fmt(got, sizeof got, "%s", c->arg);
    else if (!c->choose) {
        last_game(got, sizeof got);
        if (!is_game(got)) got[0] = 0;
    }
    if (!got[0] && !ask_game(got, sizeof got)) return 0;
    pick(got, path, n, 0);
    {
        char real[PATH_MAX];
        if (realpath(path, real)) fmt(path, n, "%s", real);
    }
    return path[0] != 0;
}

/* ---- --install ------------------------------------------------------------------- */

static void install_desktop(void)
{
    char apps[PATH_MAX + 64], file[PATH_MAX + 96];
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    FILE *f;
    /* The menu is in the home folder, which portable mode leaves alone. */
    if (g_portable[0])
        failf("JSRF-PC is in portable mode (it has a \"portable\" folder), so it does not add itself to the "
              "applications menu, which is kept in your home folder.");
    if (x && *x) fmt(apps, sizeof apps, "%s/applications", x);
    else fmt(apps, sizeof apps, "%s/.local/share/applications", h ? h : "");
    js_mkdirs(apps);
    fmt(file, sizeof file, "%s/jsrf-pc.desktop", apps);
    f = fopen(file, "w");
    if (!f) failf("Could not write %s.", file);
    fprintf(f, "[Desktop Entry]\nType=Application\nName=JSRF-PC\nComment=Jet Set Radio Future, running natively\n"
               "Exec=\"%s/jsrf-pc\"\nIcon=%s/share/jsrf-pc.png\nTerminal=false\nCategories=Game;\n", g_here, g_here);
    fclose(f);
    printf("Added JSRF-PC to your applications menu (%s).\n", file);
}

/* ---- the logs and the saves ----------------------------------------------------- */

struct old_log { char path[PATH_MAX + NAME_MAX + 2]; time_t t; };

static int newest_first(const void *a, const void *b)
{
    const struct old_log *x = (const struct old_log *)a, *y = (const struct old_log *)b;
    if (x->t != y->t) return x->t < y->t ? 1 : -1;
    return strcmp(y->path, x->path);
}

/* The previous run's log is kept (five of them), then a fresh one. */
static void keep_logs(const char *log)
{
    char dest[PATH_MAX + 64], stamp[32];
    time_t now = time(NULL);
    struct tm tm;
    DIR *d;
    struct dirent *e;
    struct old_log *all = NULL;
    size_t k = 0, cap = 0;
    if (!js_is_file(log)) return;
    localtime_r(&now, &tm);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    fmt(dest, sizeof dest, "%s/run-%s.log", g_logs, stamp);
    rename(log, dest);
    d = opendir(g_logs);
    while (d && (e = readdir(d)) != NULL) {
        struct stat st;
        if (strncmp(e->d_name, "run-", 4) || strlen(e->d_name) < 8 ||
            strcmp(e->d_name + strlen(e->d_name) - 4, ".log")) continue;
        if (k == cap) {
            struct old_log *m = (struct old_log *)realloc(all, (cap = cap ? cap * 2 : 16) * sizeof *all);
            if (!m) break;
            all = m;
        }
        fmt(all[k].path, sizeof all[k].path, "%s/%s", g_logs, e->d_name);
        if (stat(all[k].path, &st) || !S_ISREG(st.st_mode)) continue;
        all[k].t = st.st_mtime;
        k++;
    }
    if (d) closedir(d);
    if (k > 5) {
        size_t i;
        qsort(all, k, sizeof *all, newest_first);
        for (i = 5; i < k; i++) unlink(all[i].path);
    }
    free(all);
}

/* cp -R: a folder and what is in it, the files' modes kept. */
static int copy_tree(const char *from, const char *to)
{
    struct stat st;
    if (lstat(from, &st)) return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d;
        struct dirent *e;
        int bad = 0;
        if (mkdir(to, st.st_mode & 07777) && errno != EEXIST) return -1;
        d = opendir(from);
        if (!d) return -1;
        while (!bad && (e = readdir(d)) != NULL) {
            char a[PATH_MAX], b[PATH_MAX];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if ((size_t)snprintf(a, sizeof a, "%s/%s", from, e->d_name) >= sizeof a ||
                (size_t)snprintf(b, sizeof b, "%s/%s", to, e->d_name) >= sizeof b) { bad = 1; break; }
            bad = copy_tree(a, b) != 0;
        }
        closedir(d);
        return bad ? -1 : 0;
    }
    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        ssize_t l = readlink(from, target, sizeof target - 1);
        if (l < 0) return -1;
        target[l] = 0;
        return symlink(target, to);
    }
    if (S_ISREG(st.st_mode)) {
        char buf[1 << 16];
        int in = open(from, O_RDONLY), out;
        ssize_t got;
        int bad = 0;
        if (in < 0) return -1;
        out = open(to, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
        if (out < 0) { close(in); return -1; }
        while ((got = read(in, buf, sizeof buf)) != 0) {
            if (got < 0) { if (errno == EINTR) continue; bad = 1; break; }
            if (write(out, buf, (size_t)got) != got) { bad = 1; break; }
        }
        close(in);
        if (close(out)) bad = 1;
        return bad ? -1 : 0;
    }
    return 0;   /* a device or a pipe: nothing an Xbox save holds */
}

/* ---- the start ---------------------------------------------------------------- */

static int scale_ok(const char *s)
{
    return s && s[0] >= '1' && s[0] <= '0' + SCALE_MAX && !s[1];
}

int main(int argc, char **argv)
{
    char exe[PATH_MAX], p[PATH_MAX + 64], game[PATH_MAX * 2], last[PATH_MAX * 2], why[4096];
    char log[PATH_MAX + 64], scale[16];
    const char *arg = NULL, *ld;
    int i, choose = 0, fullscreen = 0, status;
    ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);

    signal(SIGPIPE, SIG_IGN);
    if (l <= 0) { fprintf(stderr, "jsrf-pc: can't find where it is\n"); return 1; }
    exe[l] = 0;
    fmt(g_here, sizeof g_here, "%s", exe);
    *strrchr(g_here, '/') = 0;
    if (!g_here[0]) fmt(g_here, sizeof g_here, "/");
    fmt(g_engine, sizeof g_engine, "%s/bin/jsrf_recomp", g_here);
    ld = getenv("LD_LIBRARY_PATH");
    fmt(g_libpath, sizeof g_libpath, "%s/lib%s%s", g_here, ld && *ld ? ":" : "", ld && *ld ? ld : "");
    /* The engine finds the SDL beside it; whatever else runs, too (the
     * dialogs don't use it). */
    engine_env();

    /* Looked for first, before anything is read or made: in portable mode
     * nothing outside this folder is touched but the game's own files. */
    fmt(p, sizeof p, "%s/portable", g_here);
    if (js_is_dir(p)) {
        fmt(g_portable, sizeof g_portable, "%s", p);
        fmt(g_cfg, sizeof g_cfg, "%s/settings.cfg", g_portable);
        fmt(g_data, sizeof g_data, "%s/games", g_portable);
        fmt(g_save, sizeof g_save, "%s/cache", g_portable);
        fmt(g_state, sizeof g_state, "%s/logs", g_portable);
        fmt(g_logs, sizeof g_logs, "%s", g_state);
    } else {
        const char *h = getenv("HOME"), *x;
        if (!h) h = "";
        x = getenv("XDG_CONFIG_HOME");
        if (x && *x) fmt(g_cfg, sizeof g_cfg, "%s/jsrf-pc/settings", x);
        else fmt(g_cfg, sizeof g_cfg, "%s/.config/jsrf-pc/settings", h);
        x = getenv("XDG_DATA_HOME");
        if (x && *x) fmt(g_data, sizeof g_data, "%s/jsrf-pc", x);
        else fmt(g_data, sizeof g_data, "%s/.local/share/jsrf-pc", h);
        fmt(g_save, sizeof g_save, "%s/save", g_data);
        x = getenv("XDG_STATE_HOME");
        if (x && *x) fmt(g_state, sizeof g_state, "%s/jsrf-pc", x);
        else fmt(g_state, sizeof g_state, "%s/.local/state/jsrf-pc", h);
        fmt(g_logs, sizeof g_logs, "%s/logs", g_state);
    }
    fmt(p, sizeof p, "%s", g_cfg);
    *strrchr(p, '/') = 0;
    js_mkdirs(p);
    js_mkdirs(g_save);
    js_mkdirs(g_logs);
    if (g_portable[0]) {
        fmt(p, sizeof p, "%s/saves", g_portable);
        js_mkdirs(p);
    }
    /* A portable folder that cannot be written stops here rather than falling
     * back to the home folder, which would split the saves between two places. */
    if (g_portable[0] && access(g_portable, W_OK))
        failf("The portable folder can't be written to:\n\n%s\n\nMake it writable, or remove it to keep the "
              "settings, saves and logs in your home folder instead.", g_portable);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--choose")) choose = 1;
        else if (!strcmp(a, "--scale")) {
            const char *v = i + 1 < argc ? argv[++i] : "";
            if (scale_ok(v)) cfg_set("scale", v);
            else failf("--scale takes 1 to %d.", SCALE_MAX);
        } else if (!strcmp(a, "--fullscreen")) fullscreen = 1;
        else if (!strcmp(a, "--install")) { install_desktop(); return 0; }
        else if (!strcmp(a, "--version")) {
            FILE *f;
            fmt(p, sizeof p, "%s/VERSION", g_here);
            f = fopen(p, "r");
            if (f) {
                char v[256];
                while (fgets(v, sizeof v, f)) fputs(v, stdout);
                fclose(f);
                return 0;
            }
            {
                char *e[] = { g_engine, "--version", NULL };
                pid_t pid = spawn(e, -1, -1, 0, NULL);
                if (pid > 0) wait_status(pid);
                return 0;
            }
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { fputs(HELP, stdout); return 0; }
        else arg = a;
    }

    /* A package without the engine: the setup builds it, from the game it
     * asks for, which then goes on as if it had been given. */
    if (jsrf_setup_needed(g_here)) {
        static jsrf_setup s;
        struct game_ctx gc;
        int r;
        gc.arg = arg;
        gc.choose = choose;
        memset(&s, 0, sizeof s);
        s.root = g_here;
        s.find_game = setup_find_game;
        s.ctx = &gc;
        r = jsrf_setup_run(&s);
        if (r == JSRF_SETUP_CLOSED) return 0;
        if (r != JSRF_SETUP_READY) return 1;
        if (!arg && s.game[0]) {
            static char chosen[sizeof s.game];
            fmt(chosen, sizeof chosen, "%s", s.game);
            arg = chosen;
        }
        /* The tests stop here: the engine is built, nothing is played. */
        if (getenv("JSRF_SETUP_ONLY")) return 0;
    }

    /* Which game folder: the argument, else the remembered one, else ask. */
    last_game(last, sizeof last);
    if (arg) {
        pick(arg, game, sizeof game, 1);
    } else if (!choose && is_game(last)) {
        fmt(game, sizeof game, "%s", last);
    } else {
        char asked[PATH_MAX * 2];
        if (!ask_game(asked, sizeof asked)) return 0;
        pick(asked, game, sizeof game, 1);
    }
    if (!game[0]) return 0;              /* unpacking was cancelled */
    {
        char real[PATH_MAX];
        if (!js_is_dir(game) || !realpath(game, real)) fail("That folder does not exist.");
        fmt(game, sizeof game, "%s", real);
    }
    if (!is_game(game))
        failf("That folder has no default.xbe and Media in it, so it isn't the game's files. Pick the folder "
              "you extracted the game into, or your disc image (.iso).");

    /* The engine is the standalone US disc's program, translated address for
     * address: another build (European, Japanese, a Sega GT 2002 combo disc)
     * starts and then crashes a few seconds in. Its own check
     * (port/xbe_check.c) says what the files are; exit 3 = another build or
     * another game. */
    status = engine_check("--check-game", game, why, sizeof why);
    if (status == 2 || status == 3) refuse(why);
    {
        size_t hl = strlen(g_here);
        const char *keep = game;
        if (g_portable[0] && !strncmp(game, g_here, hl) && game[hl] == '/') keep = game + hl + 1;
        cfg_set("game_dir", keep);
    }

    /* Portable: the saves the game kept in its own folder (UDATA, TDATA) are
     * copied into portable/saves the first time, so its progress carries
     * over. Copied, not moved: the originals stay where they were. */
    if (g_portable[0]) {
        static const char *const dirs[] = { "UDATA", "TDATA" };
        for (i = 0; i < 2; i++) {
            char from[PATH_MAX * 2 + 8], to[PATH_MAX + 80];
            struct stat st;
            fmt(from, sizeof from, "%s/%s", game, dirs[i]);
            fmt(to, sizeof to, "%s/saves/%s", g_portable, dirs[i]);
            if (!js_is_dir(from) || !lstat(to, &st)) continue;
            if (copy_tree(from, to)) {
                char saves[PATH_MAX + 16];
                js_rmtree(to);
                fmt(saves, sizeof saves, "%s/saves", g_portable);
                failf("The saves in\n%s\ncould not be copied into\n%s\nso the game was not started. The originals "
                      "are untouched.", from, saves);
            }
        }
    }

    if (!cfg_get("scale", scale, sizeof scale) || !scale_ok(scale)) fmt(scale, sizeof scale, "2");

    fmt(log, sizeof log, "%s/last-run.log", g_state);
    keep_logs(log);

    /* The paths and the player's settings, and no switch: what the game
     * needs is the engine's own default (tests/launcher_env checks). */
    setenv("RECOMP_GL_SCALE", scale, 1);
    if (fullscreen) setenv("RECOMP_FULLSCREEN", "1", 1);
    setenv("JSRF_GAME_DIR", game, 1);
    setenv("JSRF_SAVE_DIR", g_save, 1);
    setenv("JSRF_SETTINGS_FILE", g_cfg, 1);
    /* Portable: the game's saves go to portable/saves, not the game folder
     * (the engine's path layer, src/kernel/kernel_path.c). */
    if (g_portable[0]) {
        fmt(p, sizeof p, "%s/saves", g_portable);
        setenv("JSRF_USER_DIR", p, 1);
    }

    /* What the engine writes by a relative name (xbox_kernel.log, the icall
     * dumps) goes to the game folder, or to the logs in portable mode. */
    {
        int out = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        char *e[] = { g_engine, game, NULL };
        pid_t pid;
        if (out < 0) failf("Could not write %s.", log);
        pid = fork();
        if (pid == 0) {
            dup2(out, 1);
            dup2(out, 2);
            close(out);
            if (chdir(g_portable[0] ? g_logs : game)) _exit(1);
            signal(SIGPIPE, SIG_DFL);
            execv(g_engine, e);
            _exit(127);
        }
        close(out);
        status = pid > 0 ? wait_status(pid) : 127;
    }
    if (status == 3) {
        /* The engine's own check stopped it: not the standalone US disc's
         * files (the log says what they are). */
        char lines[4096] = "", line[1024], raw[5000], text[6000];
        size_t k = 0;
        FILE *f = fopen(log, "r");
        while (f && fgets(line, sizeof line, f)) {
            if (strncmp(line, "[XBE] ", 6)) continue;
            k += (size_t)snprintf(lines + k, k < sizeof lines ? sizeof lines - k : 0, "%s", line + 6);
            if (k >= sizeof lines) k = sizeof lines - 1;
        }
        if (f) fclose(f);
        chomp(lines);
        cfg_set("game_dir", "");
        fmt(raw, sizeof raw, "These files won't work with JSRF-PC.\n\n%s\n", lines);
        fold(raw, text, sizeof text);
        chomp(text);
        say(text);
    } else if (status != 0) {
        char text[PATH_MAX + 256];
        fmt(text, sizeof text, "JSRF-PC stopped unexpectedly (exit %d).\n\nThe log is at:\n%s\n\n"
                               "If you report the problem, please attach it.", status, log);
        say(text);
    }
    return status;
}
