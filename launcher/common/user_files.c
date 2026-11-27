/*
 * Where the player's files are kept (user_files.h): the layout from what is
 * beside the launcher and in the environment, the folders made, and the game
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
#else
#include <unistd.h>
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

void js_files_lay_out(const js_files_places *p, js_files *f)
{
    const int w = p->windows;
    char base[JS_FILES_PATH];
    f->root[0] = f->saves[0] = 0;
    put(f->here, sizeof f->here, "%s", p->here);
    if (p->portable) {
        f->layout = JS_FILES_PORTABLE;
        join(f->root, sizeof f->root, w, p->here, "portable");
        join(f->settings, sizeof f->settings, w, f->root, "settings.cfg");
        join(f->saves, sizeof f->saves, w, f->root, "saves");
        join(f->cache, sizeof f->cache, w, f->root, "cache");
        join(f->games, sizeof f->games, w, f->root, "games");
        join(f->logs, sizeof f->logs, w, f->root, "logs");
        join(f->log, sizeof f->log, w, f->logs, "last-run.log");
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

int js_files_find(const char *here, js_files *f)
{
    js_files_places p;
    char look[JS_FILES_PATH], v[JS_FILES_PATH];
#ifdef _WIN32
    char la[JS_FILES_PATH] = "";
    const int windows = 1;
#else
    const int windows = 0;
#endif
    memset(&p, 0, sizeof p);
    p.windows = windows;
    p.here = here;
    join(look, sizeof look, windows, here, "portable");
    p.portable = js_is_dir(look);
#ifdef _WIN32
    if (!p.portable) {
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
    f->found = p.portable || js_is_file(f->settings);
    f->game[0] = 0;
    f->game_state = JS_GAME_UNSET;
    if (js_read_kv(f->settings, "game_dir", v, sizeof v) && v[0]) {
        js_files_game(f, v, f->game, sizeof f->game);
        f->game_state = js_is_dir(f->game) ? JS_GAME_THERE : JS_GAME_GONE;
    }
    return f->found;
}

int js_files_prepare(const js_files *f, char *err, size_t n)
{
#ifdef _WIN32
    wchar_t w[JS_FILES_PATH];
    if (f->layout == JS_FILES_PORTABLE) {
        wchar_t probe[MAX_PATH];
        widen(f->logs, w, JS_FILES_PATH);
        SHCreateDirectoryExW(NULL, w, NULL);
        widen(f->saves, w, JS_FILES_PATH);
        SHCreateDirectoryExW(NULL, w, NULL);
        /* Written to, to be sure: a temporary file made and deleted. */
        widen(f->root, w, JS_FILES_PATH);
        if (!GetTempFileNameW(w, L"jsr", 0, probe)) {
            put(err, n, "The portable folder can't be written to:\n\n%s\n\nMake it writable, or remove it to "
                        "keep the settings, saves and logs in %%LOCALAPPDATA%%\\JSRF-PC instead.", f->root);
            return -1;
        }
        DeleteFileW(probe);
    } else {
        widen(f->games, w, JS_FILES_PATH);
        SHCreateDirectoryExW(NULL, w, NULL);
    }
#else
    char dir[JS_FILES_PATH], *slash;
    put(dir, sizeof dir, "%s", f->settings);
    if ((slash = strrchr(dir, '/')) != NULL) *slash = 0;
    js_mkdirs(dir);
    js_mkdirs(f->cache);
    js_mkdirs(f->logs);
    if (f->layout == JS_FILES_PORTABLE) {
        js_mkdirs(f->saves);
        if (access(f->root, W_OK)) {
            put(err, n, "The portable folder can't be written to:\n\n%s\n\nMake it writable, or remove it to keep "
                        "the settings, saves and logs in your home folder instead.", f->root);
            return -1;
        }
    }
#endif
    if (n) err[0] = 0;
    return 0;
}

void js_files_game(const js_files *f, const char *value, char *out, size_t n)
{
#ifdef _WIN32
    const int relative = value[0] && value[0] != '\\' && value[0] != '/' && value[1] != ':';
#else
    const int relative = value[0] && value[0] != '/';
#endif
    if (f->layout == JS_FILES_PORTABLE && relative) {
#ifdef _WIN32
        join(out, n, 1, f->here, value);
#else
        join(out, n, 0, f->here, value);
#endif
    } else {
        put(out, n, "%s", value);
    }
}

void js_files_keep(const js_files *f, const char *game, char *out, size_t n)
{
#ifdef _WIN32
    wchar_t wg[JS_FILES_PATH], wh[JS_FILES_PATH];
    size_t lh;
    widen(game, wg, JS_FILES_PATH);
    widen(f->here, wh, JS_FILES_PATH);
    lh = wcslen(wh);
    if (f->layout == JS_FILES_PORTABLE && !_wcsnicmp(wg, wh, lh) && wg[lh] == L'\\') {
        narrow(wg + lh + 1, out, (int)n);
        return;
    }
#else
    size_t lh = strlen(f->here);
    if (f->layout == JS_FILES_PORTABLE && !strncmp(game, f->here, lh) && game[lh] == '/') {
        put(out, n, "%s", game + lh + 1);
        return;
    }
#endif
    put(out, n, "%s", game);
}
