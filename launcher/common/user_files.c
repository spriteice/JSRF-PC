/*
 * Where the player's files are kept (user_files.h): the layout from what is
 * beside the launcher and in the environment, the data folder made, renamed
 * from portable or carried over into, the saves copied in, and the game
 * remembered. Each system's ways are its own, as they were in its launcher:
 * Linux's paths are joined with '/', Windows's with '\' unless the first part
 * ends in one, and Windows compares folders without their case.
 */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>
#define SYS_WINDOWS 1
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define SYS_WINDOWS 0
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user_files.h"
#include "../setup/setup.h"

static void put(char *out, size_t n, const char *f, ...)
{
    va_list ap;
    if (!n) return;
    va_start(ap, f);
    vsnprintf(out, n, f, ap);
    va_end(ap);
    out[n - 1] = 0;
}

/* a, then b: on Windows with no second '\' after one a ends in. */
static void join(char *out, size_t n, int windows, const char *a, const char *b)
{
    size_t la = strlen(a);
    if (!windows) put(out, n, "%s/%s", a, b);
    else put(out, n, "%s%s%s", a, la && (a[la - 1] == '\\' || a[la - 1] == '/') ? "" : "\\", b);
}

static int given(const char *s)
{
    return s && *s;
}

/* The data or the portable folder's places: all of them inside it. */
static void beside(js_files *f, int windows, const char *name, int layout)
{
    f->layout = layout;
    join(f->root, sizeof f->root, windows, f->here, name);
    join(f->settings, sizeof f->settings, windows, f->root, "settings.cfg");
    join(f->saves, sizeof f->saves, windows, f->root, "saves");
    join(f->cache, sizeof f->cache, windows, f->root, "cache");
    join(f->games, sizeof f->games, windows, f->root, "games");
    join(f->logs, sizeof f->logs, windows, f->root, "logs");
    join(f->log, sizeof f->log, windows, f->logs, "last-run.log");
}

void js_files_lay_out(const js_files_places *p, js_files *f)
{
    const int w = p->windows;
    char base[JS_FILES_PATH];
    f->root[0] = f->saves[0] = 0;
    put(f->here, sizeof f->here, "%s", p->here);
    f->both = p->data && p->portable;
    if (p->data) {
        beside(f, w, "data", JS_FILES_DATA);
        return;
    }
    if (p->portable) {
        beside(f, w, "portable", JS_FILES_PORTABLE);
        return;
    }
    f->layout = JS_FILES_HOME;
    if (w) {
        join(base, sizeof base, w, p->local_app_data ? p->local_app_data : p->here, "JSRF-PC");
        put(f->games, sizeof f->games, "%s", base);
        join(f->settings, sizeof f->settings, w, base, "settings");
        join(f->cache, sizeof f->cache, w, base, "save");
        join(f->log, sizeof f->log, w, base, "last-run.log");
        join(f->logs, sizeof f->logs, w, base, "logs");
    } else {
        const char *h = p->home ? p->home : "";
        if (given(p->config_home)) put(f->settings, sizeof f->settings, "%s/jsrf-pc/settings", p->config_home);
        else put(f->settings, sizeof f->settings, "%s/.config/jsrf-pc/settings", h);
        if (given(p->data_home)) put(f->games, sizeof f->games, "%s/jsrf-pc", p->data_home);
        else put(f->games, sizeof f->games, "%s/.local/share/jsrf-pc", h);
        join(f->cache, sizeof f->cache, w, f->games, "save");
        if (given(p->state_home)) put(base, sizeof base, "%s/jsrf-pc", p->state_home);
        else put(base, sizeof base, "%s/.local/state/jsrf-pc", h);
        join(f->log, sizeof f->log, w, base, "last-run.log");
        join(f->logs, sizeof f->logs, w, base, "logs");
    }
}

/* ---- this system's files ------------------------------------------------------ */

#ifdef _WIN32
static void widen(const char *s, wchar_t *w, int n)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) w[0] = 0;
}

static void narrow(const wchar_t *w, char *s, int n)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL)) s[0] = 0;
}
#endif

/* Why the last call failed, as the system says it. */
static void reason(char *out, size_t n)
{
#ifdef _WIN32
    wchar_t w[512];
    DWORD e = GetLastError();
    size_t l;
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, w, 512, NULL)) {
        put(out, n, "Error %lu.", (unsigned long)e);
        return;
    }
    narrow(w, out, (int)n);
    l = strlen(out);
    while (l && (out[l - 1] == '\n' || out[l - 1] == '\r' || out[l - 1] == ' ')) out[--l] = 0;
#else
    put(out, n, "%s.", strerror(errno));
#endif
}

/* Anything by that name: a file, a folder, a link. */
static int there(const char *path)
{
#ifdef _WIN32
    wchar_t w[JS_FILES_PATH];
    widen(path, w, JS_FILES_PATH);
    return GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return lstat(path, &st) == 0;
#endif
}

/* Each system's own check, as each launcher had it: on Windows a temporary
 * file made and deleted, to be sure. */
static int writable(const char *dir)
{
#ifdef _WIN32
    wchar_t w[JS_FILES_PATH], probe[MAX_PATH];
    widen(dir, w, JS_FILES_PATH);
    if (!GetTempFileNameW(w, L"jsr", 0, probe)) return 0;
    DeleteFileW(probe);
    return 1;
#else
    return access(dir, W_OK) == 0;
#endif
}

static int make_dir(const char *dir)
{
#ifdef _WIN32
    wchar_t w[JS_FILES_PATH];
    widen(dir, w, JS_FILES_PATH);
    return CreateDirectoryW(w, NULL) ? 0 : -1;
#else
    return mkdir(dir, 0755);
#endif
}

/* A folder's new name, beside it; never over anything already there. On
 * Windows a file just written in it can hold it for a moment (a virus
 * scanner reading it, the search indexer), so it is tried again for up to
 * two seconds. */
static int rename_dir(const char *from, const char *to)
{
#ifdef _WIN32
    wchar_t a[JS_FILES_PATH], b[JS_FILES_PATH];
    DWORD e = 0;
    int i;
    widen(from, a, JS_FILES_PATH);
    widen(to, b, JS_FILES_PATH);
    for (i = 0; i < 20; i++) {
        if (MoveFileExW(a, b, 0)) return 0;
        e = GetLastError();
        if (e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION) break;
        Sleep(100);
    }
    SetLastError(e);
    return -1;
#else
    if (there(to)) {
        errno = EEXIST;
        return -1;
    }
    return rename(from, to);
#endif
}

static FILE *open_file(const char *path, const wchar_t *wmode, const char *mode)
{
#ifdef _WIN32
    wchar_t w[JS_FILES_PATH];
    (void)mode;
    widen(path, w, JS_FILES_PATH);
    return _wfopen(w, wmode);
#else
    (void)wmode;
    return fopen(path, mode);
#endif
}

/* key=value set in a settings file as the launchers set theirs: the key's
 * own lines replaced by one at the end, every other line kept as it was. */
static int set_kv(const char *path, const char *key, const char *val)
{
    char tmp[JS_FILES_PATH + 8], line[8192];
    size_t kl = strlen(key);
    int start = 1, dropping = 0, bad;
    FILE *in, *out;
    put(tmp, sizeof tmp, "%s.tmp", path);
    if (!(out = open_file(tmp, L"wb", "wb"))) return -1;
    if ((in = open_file(path, L"rb", "rb")) != NULL) {
        while (fgets(line, sizeof line, in)) {
            size_t l = strlen(line);
            if (start) dropping = !strncmp(line, key, kl) && line[kl] == '=';
            if (!dropping) fputs(line, out);
            start = l && line[l - 1] == '\n';
        }
        if (!start && !dropping) fputc('\n', out);
        fclose(in);
    }
    fprintf(out, "%s=%s\n", key, val);
    bad = fclose(out) != 0;
#ifdef _WIN32
    {
        wchar_t a[JS_FILES_PATH], b[JS_FILES_PATH];
        widen(tmp, a, JS_FILES_PATH);
        widen(path, b, JS_FILES_PATH);
        if (bad || !MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(a);
            return -1;
        }
    }
#else
    if (bad || rename(tmp, path)) {
        unlink(tmp);
        return -1;
    }
#endif
    return 0;
}

/* One file, or a folder and what is in it (cp -R): on Linux the files'
 * modes kept and a link inside copied as a link, and one given followed
 * when follow is set; on Windows as CopyFile copies, a link followed. Never
 * over a file already there. */
#ifdef _WIN32
static int copy_w(const wchar_t *from, const wchar_t *to, int depth)
{
    DWORD a = GetFileAttributesW(from);
    WIN32_FIND_DATAW fd;
    wchar_t pat[JS_FILES_PATH], x[JS_FILES_PATH], y[JS_FILES_PATH];
    HANDLE h;
    int bad = 0;
    if (a == INVALID_FILE_ATTRIBUTES) return -1;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return CopyFileW(from, to, TRUE) ? 0 : -1;
    if (depth > 32) {
        SetLastError(ERROR_CANT_RESOLVE_FILENAME);   /* a link back to a folder above it */
        return -1;
    }
    if (!CreateDirectoryW(to, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return -1;
    _snwprintf(pat, JS_FILES_PATH, L"%ls\\*", from);
    pat[JS_FILES_PATH - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (_snwprintf(x, JS_FILES_PATH, L"%ls\\%ls", from, fd.cFileName) < 0 ||
            _snwprintf(y, JS_FILES_PATH, L"%ls\\%ls", to, fd.cFileName) < 0) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            bad = 1;
            break;
        }
        x[JS_FILES_PATH - 1] = y[JS_FILES_PATH - 1] = 0;
        bad = copy_w(x, y, depth + 1) != 0;
    } while (!bad && FindNextFileW(h, &fd));
    FindClose(h);
    return bad ? -1 : 0;
}
#endif

static int copy_path(const char *from, const char *to, int follow)
{
#ifdef _WIN32
    wchar_t a[JS_FILES_PATH], b[JS_FILES_PATH];
    (void)follow;
    widen(from, a, JS_FILES_PATH);
    widen(to, b, JS_FILES_PATH);
    return copy_w(a, b, 0);
#else
    struct stat st;
    if (follow ? stat(from, &st) : lstat(from, &st)) return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d;
        struct dirent *e;
        int bad = 0;
        if (mkdir(to, st.st_mode & 07777) && errno != EEXIST) return -1;
        d = opendir(from);
        if (!d) return -1;
        while (!bad && (e = readdir(d)) != NULL) {
            char a[JS_FILES_PATH], b[JS_FILES_PATH];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if ((size_t)snprintf(a, sizeof a, "%s/%s", from, e->d_name) >= sizeof a ||
                (size_t)snprintf(b, sizeof b, "%s/%s", to, e->d_name) >= sizeof b) {
                errno = ENAMETOOLONG;
                bad = 1;
                break;
            }
            bad = copy_path(a, b, 0) != 0;
        }
        closedir(d);
        return bad ? -1 : 0;
    }
    if (S_ISLNK(st.st_mode)) {
        char target[JS_FILES_PATH];
        ssize_t l = readlink(from, target, sizeof target - 1);
        if (l < 0) return -1;
        target[l] = 0;
        return symlink(target, to);
    }
    if (S_ISREG(st.st_mode)) {
        char buf[1 << 16];
        int in = open(from, O_RDONLY), out, bad = 0, e = 0;
        ssize_t got;
        if (in < 0) return -1;
        out = open(to, O_WRONLY | O_CREAT | O_EXCL, st.st_mode & 07777);
        if (out < 0) {
            e = errno;
            close(in);
            errno = e;
            return -1;
        }
        while ((got = read(in, buf, sizeof buf)) != 0) {
            if (got < 0) { if (errno == EINTR) continue; bad = 1; break; }
            if (write(out, buf, (size_t)got) != got) { if (got >= 0) errno = ENOSPC; bad = 1; break; }
        }
        e = errno;
        close(in);
        if (close(out) && !bad) { e = errno; bad = 1; }
        errno = e;
        return bad ? -1 : 0;
    }
    return 0;   /* a device or a pipe: nothing a save or a setting is */
#endif
}

static int copy_tree(const char *from, const char *to)
{
    return copy_path(from, to, 0);
}

/* Each file at the top of dir but the one named skip and the logs (.log),
 * copied into into: what a link there points at, the file itself. */
static int copy_files(const char *dir, const char *skip, const char *into)
{
    char from[JS_FILES_PATH], to[JS_FILES_PATH];
#ifdef _WIN32
    wchar_t w[JS_FILES_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int bad = 0;
    join(from, sizeof from, 1, dir, "*");
    widen(from, w, JS_FILES_PATH);
    h = FindFirstFileW(w, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;          /* no folder: nothing to copy */
    do {
        char name[JS_FILES_PATH];
        size_t l;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        narrow(fd.cFileName, name, sizeof name);
        l = strlen(name);
        if (!_stricmp(name, skip) || (l >= 4 && !_stricmp(name + l - 4, ".log"))) continue;
        join(from, sizeof from, 1, dir, name);
        join(to, sizeof to, 1, into, name);
        bad = copy_path(from, to, 1) != 0;
    } while (!bad && FindNextFileW(h, &fd));
    FindClose(h);
    return bad ? -1 : 0;
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    int bad = 0;
    if (!d) return 0;                                 /* no folder: nothing to copy */
    while (!bad && (e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        struct stat st;
        if (!strcmp(e->d_name, skip) || (l >= 4 && !strcmp(e->d_name + l - 4, ".log"))) continue;
        join(from, sizeof from, 0, dir, e->d_name);
        if (stat(from, &st) || !S_ISREG(st.st_mode)) continue;     /* a file, or a link to one */
        join(to, sizeof to, 0, into, e->d_name);
        bad = copy_path(from, to, 1) != 0;
    }
    closedir(d);
    return bad ? -1 : 0;
#endif
}

/* When path is inside dir, what follows it there (after the separator). As
 * each system compares names: Windows without their case. */
static int inside(const char *path, const char *dir, char *rest, size_t n)
{
#ifdef _WIN32
    wchar_t wp[JS_FILES_PATH], wd[JS_FILES_PATH];
    size_t ld;
    widen(path, wp, JS_FILES_PATH);
    widen(dir, wd, JS_FILES_PATH);
    ld = wcslen(wd);
    if (!ld || _wcsnicmp(wp, wd, ld) || wp[ld] != L'\\') return 0;
    narrow(wp + ld + 1, rest, (int)n);
#else
    size_t ld = strlen(dir);
    if (!ld || strncmp(path, dir, ld) || path[ld] != '/') return 0;
    put(rest, n, "%s", path + ld + 1);
#endif
    return 1;
}

/* ---- finding ------------------------------------------------------------------ */

static void remembered(js_files *f)
{
    char v[JS_FILES_PATH];
    f->game[0] = 0;
    f->game_state = JS_GAME_UNSET;
    if (js_read_kv(f->settings, "game_dir", v, sizeof v) && v[0]) {
        js_files_game(f, v, f->game, sizeof f->game);
        f->game_state = js_is_dir(f->game) ? JS_GAME_THERE : JS_GAME_GONE;
    }
}

int js_files_find(const char *here, js_files *f)
{
    js_files_places p;
    char look[JS_FILES_PATH];
#ifdef _WIN32
    char la[JS_FILES_PATH] = "";
#endif
    memset(&p, 0, sizeof p);
    p.windows = SYS_WINDOWS;
    p.here = here;
    join(look, sizeof look, SYS_WINDOWS, here, "data");
    p.data = js_is_dir(look);
    join(look, sizeof look, SYS_WINDOWS, here, "portable");
    p.portable = js_is_dir(look);
#ifdef _WIN32
    if (!p.data && !p.portable) {
        PWSTR known = NULL;
        if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &known)) && known) {
            narrow(known, la, sizeof la);
            CoTaskMemFree(known);
            p.local_app_data = la;
        } else {
            const wchar_t *e = _wgetenv(L"LOCALAPPDATA");
            if (e) { narrow(e, la, sizeof la); p.local_app_data = la; }
        }
    }
#else
    p.config_home = getenv("XDG_CONFIG_HOME");
    p.data_home = getenv("XDG_DATA_HOME");
    p.state_home = getenv("XDG_STATE_HOME");
    p.home = getenv("HOME");
#endif
    js_files_lay_out(&p, f);
    f->found = p.data || p.portable || js_is_file(f->settings);
    remembered(f);
    return f->found;
}

/* ---- the launchers' start ----------------------------------------------------- */

/* After the portable folder became data: a game_dir inside it (b2.2's
 * portable/games/<name>, kept relative to the launcher's folder, or a whole
 * path) changed to the same place in data. */
static void regame(js_files *f, const char *was)
{
    char v[JS_FILES_PATH], whole[JS_FILES_PATH], rest[JS_FILES_PATH], moved[JS_FILES_PATH], keep[JS_FILES_PATH];
    if (!js_read_kv(f->settings, "game_dir", v, sizeof v) || !v[0]) return;
    js_files_game(f, v, whole, sizeof whole);
    if (!inside(whole, was, rest, sizeof rest)) return;
    join(moved, sizeof moved, SYS_WINDOWS, f->root, rest);
    js_files_keep(f, moved, keep, sizeof keep);
    set_kv(f->settings, "game_dir", keep);
}

/* A new data folder, with what b2.2 kept in the home folder (f, the home
 * layout) copied in: its settings as settings.cfg, the other files beside
 * them but the logs, and recordings/. It is made as data.new and renamed when
 * whole, so a start that stops half way leaves no data folder behind it, and
 * the next one makes it again. */
static int carry_over(const js_files *f, const char *data, char *err, size_t n)
{
    char part[JS_FILES_PATH + 8], dir[JS_FILES_PATH], from[JS_FILES_PATH], to[JS_FILES_PATH + 16];
    char why[512], *name;
    int bad;
    put(part, sizeof part, "%s.new", data);
    js_rmtree(part);
    if (make_dir(part)) {
        reason(why, sizeof why);
        put(err, n, "JSRF-PC keeps your settings, saves and logs in a folder named data beside it, and it can't "
                    "make one in\n\n%s\n\n%s\n\nMove JSRF-PC to a folder you can write to, then start it again.",
            f->here, why);
        return -1;
    }
    put(dir, sizeof dir, "%s", f->settings);
    name = strrchr(dir, SYS_WINDOWS ? '\\' : '/');
    if (name) *name++ = 0;
    else name = dir;
    join(to, sizeof to, SYS_WINDOWS, part, "settings.cfg");
    bad = js_is_file(f->settings) && copy_path(f->settings, to, 1);
    if (!bad) bad = copy_files(dir, name, part) != 0;
    join(from, sizeof from, SYS_WINDOWS, dir, "recordings");
    join(to, sizeof to, SYS_WINDOWS, part, "recordings");
    if (!bad && js_is_dir(from)) bad = copy_tree(from, to) != 0;
    if (bad) {
        reason(why, sizeof why);
        js_rmtree(part);
        put(err, n, "Your settings from an earlier JSRF-PC couldn't be copied from\n\n%s\n\ninto its data "
                    "folder:\n\n%s\n\n%s\n\nNothing was changed, and the game was not started.",
            dir, data, why);
        return -1;
    }
    if (rename_dir(part, data)) {
        reason(why, sizeof why);
        js_rmtree(part);
        put(err, n, "JSRF-PC keeps your settings, saves and logs in a folder named data beside it, and it can't "
                    "make one in\n\n%s\n\n%s\n\nMove JSRF-PC to a folder you can write to, then start it again.",
            f->here, why);
        return -1;
    }
    return 0;
}

int js_files_prepare(js_files *f, char *err, size_t n)
{
    char data[JS_FILES_PATH], was[JS_FILES_PATH], why[512];
    if (n) err[0] = 0;
    join(data, sizeof data, SYS_WINDOWS, f->here, "data");
    if (f->both) {
        char portable[JS_FILES_PATH];
        join(portable, sizeof portable, SYS_WINDOWS, f->here, "portable");
        put(err, n, "There are two folders for your settings and saves beside JSRF-PC, and it can't tell which one "
                    "has your progress:\n\n%s\n%s\n\nKeep the one you play with: move the other one out of "
                    "JSRF-PC's folder, then start JSRF-PC again. A portable folder on its own is renamed to data.",
            data, portable);
        return -1;
    }
    if (f->layout == JS_FILES_PORTABLE) {
        put(was, sizeof was, "%s", f->root);
        /* Linux's check makes nothing, so it comes first, and a folder that
         * can't be written is refused with nothing changed. Windows's makes
         * a file, which could hold the folder as it is renamed, so there it
         * waits for data (below). */
        if (!SYS_WINDOWS && !writable(was)) {
            put(err, n, "The portable folder can't be written to:\n\n%s\n\nJSRF-PC keeps your settings, saves and "
                        "logs in it, renamed to data. Make it writable, or move JSRF-PC to a folder you can write "
                        "to.", was);
            return -1;
        }
        if (rename_dir(was, data)) {
            reason(why, sizeof why);
            put(err, n, "The portable folder couldn't be renamed to data:\n\n%s\n\n%s\n\nNothing was changed. "
                        "Close any program that has a file in it open, or move JSRF-PC to a folder you can write "
                        "to, then start it again.", was, why);
            return -1;
        }
        beside(f, SYS_WINDOWS, "data", JS_FILES_DATA);
        regame(f, was);
    } else if (f->layout == JS_FILES_HOME) {
        if (carry_over(f, data, err, n)) return -1;
        beside(f, SYS_WINDOWS, "data", JS_FILES_DATA);
    }
    js_mkdirs(f->saves);
    js_mkdirs(f->logs);
    js_mkdirs(f->cache);
    if (!writable(f->root)) {
        put(err, n, "The data folder can't be written to:\n\n%s\n\nJSRF-PC keeps your settings, saves and logs in "
                    "it. Make it writable, or move JSRF-PC to a folder you can write to.", f->root);
        return -1;
    }
    f->found = 1;
    remembered(f);
    return 0;
}

int js_files_saves_in(const js_files *f, const char *game, char *err, size_t n)
{
    static const char *const dirs[] = { "UDATA", "TDATA" };
    char from[JS_FILES_PATH], to[JS_FILES_PATH];
    size_t i;
    if (n) err[0] = 0;
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        join(from, sizeof from, SYS_WINDOWS, game, dirs[i]);
        join(to, sizeof to, SYS_WINDOWS, f->saves, dirs[i]);
        if (!js_is_dir(from) || there(to)) continue;
        if (copy_tree(from, to)) {
            js_rmtree(to);
            put(err, n, "The saves in\n%s\ncould not be copied into\n%s\nso the game was not started. The originals "
                        "are untouched.", from, f->saves);
            return -1;
        }
    }
    return 0;
}

/* ---- the game remembered ------------------------------------------------------ */

void js_files_game(const js_files *f, const char *value, char *out, size_t n)
{
#ifdef _WIN32
    const int relative = value[0] && value[0] != '\\' && value[0] != '/' && value[1] != ':';
#else
    const int relative = value[0] && value[0] != '/';
#endif
    if (f->layout != JS_FILES_HOME && relative) join(out, n, SYS_WINDOWS, f->here, value);
    else put(out, n, "%s", value);
}

void js_files_keep(const js_files *f, const char *game, char *out, size_t n)
{
    if (f->layout != JS_FILES_HOME && inside(game, f->here, out, n)) return;
    put(out, n, "%s", game);
}
