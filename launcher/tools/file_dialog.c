/*
 * The system's own dialogs for picking a folder or files (file_dialog.h).
 *
 * Windows: IFileOpenDialog, FOS_ALLOWMULTISELECT for several files; where
 * there is none (an old Wine), SHBrowseForFolderW and GetOpenFileNameW, as
 * the launcher falls back to. Linux: zenity, else kdialog, run with no
 * shell, their answer read from their output, one path a line. Each can
 * open in a folder given (the old Wine's folder browser excepted).
 */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#else
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "file_dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_dir(const char *path);

/* The folder a dialog opens in, for start: start when it is a folder, else
 * the nearest folder above it that is there. 0 when there is none. */
static int start_dir(const char *start, char *out, size_t n)
{
    size_t l;
    if (!start || !start[0] || strlen(start) >= n) return 0;
    snprintf(out, n, "%s", start);
    for (;;) {
        if (is_dir(out)) return 1;
        l = strlen(out);
        while (l && out[l - 1] != '/' && out[l - 1] != '\\') l--;
        if (!l) return 0;
        while (l > 1 && (out[l - 1] == '/' || out[l - 1] == '\\')) l--;
        if (l == 2 && out[1] == ':') l = 3;     /* C:\ */
        if (l >= strlen(out)) return 0;
        out[l] = 0;
    }
}

#ifdef _WIN32

static int is_dir(const char *path)
{
    wchar_t w[4096];
    DWORD a;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 4096)) return 0;
    a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)(n > 0 ? n : 1));
    if (!w) return NULL;
    if (n <= 0) w[0] = 0;
    else MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void narrow(const wchar_t *w, char *out, size_t n)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL) && n) out[0] = 0;
}

/* The dialog opened in start's folder. */
static void open_in(IFileOpenDialog *dlg, const char *start)
{
    char dir[4096];
    wchar_t *w;
    IShellItem *item = NULL;
    if (!start_dir(start, dir, sizeof dir) || !(w = wide(dir))) return;
    if (SUCCEEDED(SHCreateItemFromParsingName(w, NULL, &IID_IShellItem, (void **)&item))) {
        IFileOpenDialog_SetFolder(dlg, item);
        IShellItem_Release(item);
    }
    free(w);
}

int jsrf_pick_folder(const char *title, char *out, size_t n)
{
    return jsrf_pick_folder_in(title, NULL, out, n);
}

int jsrf_pick_folder_in(const char *title, const char *start, char *out, size_t n)
{
    IFileOpenDialog *dlg = NULL;
    IShellItem *item = NULL;
    PWSTR path = NULL;
    DWORD opts = 0;
    wchar_t *wt = wide(title);
    int r = JSRF_CANCELLED;
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IFileOpenDialog, (void **)&dlg);
    if (SUCCEEDED(hr)) {
        IFileOpenDialog_GetOptions(dlg, &opts);
        IFileOpenDialog_SetOptions(dlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (wt) IFileOpenDialog_SetTitle(dlg, wt);
        open_in(dlg, start);
        if (SUCCEEDED(IFileOpenDialog_Show(dlg, NULL)) && SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item)) &&
            SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path))) {
            narrow(path, out, n);
            r = out[0] ? JSRF_PICKED : JSRF_CANCELLED;
        }
        if (path) CoTaskMemFree(path);
        if (item) IShellItem_Release(item);
        IFileOpenDialog_Release(dlg);
    } else {
        /* No IFileOpenDialog (an old Wine): the classic folder browser. */
        BROWSEINFOW bi;
        PIDLIST_ABSOLUTE pidl;
        wchar_t buf[MAX_PATH];
        ZeroMemory(&bi, sizeof bi);
        bi.lpszTitle = wt;
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        pidl = SHBrowseForFolderW(&bi);
        if (pidl && SHGetPathFromIDListW(pidl, buf)) {
            narrow(buf, out, n);
            r = out[0] ? JSRF_PICKED : JSRF_CANCELLED;
        }
        if (pidl) CoTaskMemFree(pidl);
    }
    free(wt);
    if (SUCCEEDED(init)) CoUninitialize();
    return r;
}

/* "*.png;*.jpg", the dialogs' way of giving several. */
static wchar_t *pattern_list(const char *const *patterns)
{
    char joined[1024] = "";
    size_t i, at = 0;
    for (i = 0; patterns && patterns[i]; i++)
        at += (size_t)snprintf(joined + at, sizeof joined - at, "%s%s", i ? ";" : "", patterns[i]);
    return wide(patterns && patterns[0] ? joined : "*.*");
}

int jsrf_pick_files(const char *title, const char *kind, const char *const *patterns, int multi,
                    void (*each)(void *ctx, const char *path), void *ctx)
{
    return jsrf_pick_files_in(title, NULL, kind, patterns, multi, each, ctx);
}

int jsrf_pick_files_in(const char *title, const char *start, const char *kind, const char *const *patterns,
                       int multi, void (*each)(void *ctx, const char *path), void *ctx)
{
    IFileOpenDialog *dlg = NULL;
    IShellItemArray *items = NULL;
    DWORD opts = 0, count = 0, i;
    wchar_t *wt = wide(title), *wk = wide(kind ? kind : "Files"), *wp = pattern_list(patterns);
    char path[4096];
    int r = JSRF_CANCELLED;
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IFileOpenDialog, (void **)&dlg);
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC spec[2];
        spec[0].pszName = wk;
        spec[0].pszSpec = wp;
        spec[1].pszName = L"All files";
        spec[1].pszSpec = L"*.*";
        IFileOpenDialog_GetOptions(dlg, &opts);
        IFileOpenDialog_SetOptions(dlg, opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
                                        (multi ? FOS_ALLOWMULTISELECT : 0));
        IFileOpenDialog_SetFileTypes(dlg, 2, spec);
        if (wt) IFileOpenDialog_SetTitle(dlg, wt);
        open_in(dlg, start);
        if (SUCCEEDED(IFileOpenDialog_Show(dlg, NULL)) && SUCCEEDED(IFileOpenDialog_GetResults(dlg, &items))) {
            IShellItemArray_GetCount(items, &count);
            for (i = 0; i < count; i++) {
                IShellItem *item = NULL;
                PWSTR p = NULL;
                if (SUCCEEDED(IShellItemArray_GetItemAt(items, i, &item)) &&
                    SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &p))) {
                    narrow(p, path, sizeof path);
                    if (path[0]) { each(ctx, path); r = JSRF_PICKED; }
                }
                if (p) CoTaskMemFree(p);
                if (item) IShellItem_Release(item);
            }
            IShellItemArray_Release(items);
        }
        IFileOpenDialog_Release(dlg);
    } else {
        /* An old Wine: the classic open dialog, its several answers a
         * folder and then each name, NUL after each, two to end. */
        static wchar_t buf[32768];
        wchar_t filter[1200], *wd = NULL;
        char dir[4096];
        OPENFILENAMEW of;
        size_t kl = wcslen(wk), pl = wcslen(wp);
        if (kl + pl + 4 < 1200) {
            memcpy(filter, wk, kl * 2);
            filter[kl] = 0;
            memcpy(filter + kl + 1, wp, pl * 2);
            filter[kl + 1 + pl] = 0;
            filter[kl + 2 + pl] = 0;
        } else {
            memcpy(filter, L"Files\0*.*\0\0", 12 * 2);
        }
        ZeroMemory(&of, sizeof of);
        buf[0] = 0;
        of.lStructSize = sizeof of;
        of.lpstrFilter = filter;
        of.lpstrFile = buf;
        of.nMaxFile = 32768;
        of.lpstrTitle = wt;
        if (start_dir(start, dir, sizeof dir)) of.lpstrInitialDir = wd = wide(dir);
        of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | (multi ? OFN_ALLOWMULTISELECT : 0);
        if (GetOpenFileNameW(&of)) {
            wchar_t *name = buf + wcslen(buf) + 1, full[4096];
            if (!*name) {                             /* one file: the whole path */
                narrow(buf, path, sizeof path);
                if (path[0]) { each(ctx, path); r = JSRF_PICKED; }
            }
            for (; *name; name += wcslen(name) + 1) {
                _snwprintf(full, 4096, L"%ls\\%ls", buf, name);
                full[4095] = 0;
                narrow(full, path, sizeof path);
                if (path[0]) { each(ctx, path); r = JSRF_PICKED; }
            }
        }
        free(wd);
    }
    free(wt);
    free(wk);
    free(wp);
    if (SUCCEEDED(init)) CoUninitialize();
    return r;
}

#else

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* A program on PATH, as the launcher finds zenity and kdialog. */
static int on_path(const char *name, char *out, size_t n)
{
    const char *path = getenv("PATH");
    char dir[2048];
    if (!path) path = "/usr/bin:/bin";
    while (*path) {
        size_t l = strcspn(path, ":");
        if (l && l < sizeof dir) {
            memcpy(dir, path, l);
            dir[l] = 0;
            snprintf(out, n, "%s/%s", dir, name);
            if (access(out, X_OK) == 0) return 1;
        }
        path += l;
        if (*path == ':') path++;
    }
    return 0;
}

/* A desktop to show a dialog on. */
static int gui(void)
{
    return (getenv("DISPLAY") && getenv("DISPLAY")[0]) || (getenv("WAYLAND_DISPLAY") && getenv("WAYLAND_DISPLAY")[0]);
}

/* argv run, its output (up to n bytes) into out. Its exit status, or -1. */
static int capture(char *const *argv, char *out, size_t n)
{
    int fd[2], status = -1;
    size_t have = 0;
    pid_t pid;
    if (pipe(fd) != 0) return -1;
    pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        dup2(fd[1], 1);
        if (null >= 0) { dup2(null, 0); dup2(null, 2); }
        close(fd[0]);
        execv(argv[0], argv);
        _exit(127);
    }
    close(fd[1]);
    for (;;) {
        char buf[4096];
        ssize_t got = read(fd[0], buf, sizeof buf);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        if (have + 1 < n) {
            size_t k = (size_t)got < n - 1 - have ? (size_t)got : n - 1 - have;
            memcpy(out + have, buf, k);
            have += k;
        }
    }
    close(fd[0]);
    out[have] = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int ask(char **argv, void (*each)(void *ctx, const char *path), void *ctx)
{
    static char out[65536];
    char *line, *next;
    int r = JSRF_CANCELLED;
    if (capture(argv, out, sizeof out) != 0) return JSRF_CANCELLED;
    for (line = out; *line; line = next) {
        next = line + strcspn(line, "\n");
        if (*next) *next++ = 0;
        if (line[0]) { each(ctx, line); r = JSRF_PICKED; }
    }
    return r;
}

typedef struct one { char *out; size_t n; } one;

static void keep_one(void *ctx, const char *path)
{
    one *o = (one *)ctx;
    if (!o->out[0]) snprintf(o->out, o->n, "%s", path);
}

/* Where a dialog opens: start's folder, else the home folder. zenity's
 * --filename wants a folder's name ended by a /. */
static void where(const char *start, char *dir, size_t n, char *zen, size_t zn)
{
    const char *home = getenv("HOME");
    if (!start_dir(start, dir, n)) snprintf(dir, n, "%s", home && home[0] ? home : "/");
    snprintf(zen, zn, "--filename=%s%s", dir, dir[0] && dir[strlen(dir) - 1] == '/' ? "" : "/");
}

int jsrf_pick_folder(const char *title, char *out, size_t n)
{
    return jsrf_pick_folder_in(title, NULL, out, n);
}

int jsrf_pick_folder_in(const char *title, const char *start, char *out, size_t n)
{
    char prog[4096], t[1024], dir[4096], zen[4200];
    one o;
    o.out = out;
    o.n = n;
    out[0] = 0;
    if (!gui()) return JSRF_NO_DIALOG;
    where(start, dir, sizeof dir, zen, sizeof zen);
    if (on_path("zenity", prog, sizeof prog)) {
        char *a[] = { prog, (char *)"--file-selection", (char *)"--directory", t, zen, NULL };
        snprintf(t, sizeof t, "--title=%s", title);
        return ask(a, keep_one, &o);
    }
    if (on_path("kdialog", prog, sizeof prog)) {
        char *a[] = { prog, (char *)"--getexistingdirectory", dir, (char *)"--title", (char *)title, NULL };
        return ask(a, keep_one, &o);
    }
    return JSRF_NO_DIALOG;
}

int jsrf_pick_files(const char *title, const char *kind, const char *const *patterns, int multi,
                    void (*each)(void *ctx, const char *path), void *ctx)
{
    return jsrf_pick_files_in(title, NULL, kind, patterns, multi, each, ctx);
}

int jsrf_pick_files_in(const char *title, const char *start, const char *kind, const char *const *patterns,
                       int multi, void (*each)(void *ctx, const char *path), void *ctx)
{
    char prog[4096], t[1024], filter[1200], kfilter[1200], joined[1024] = "", dir[4096], zen[4200];
    size_t i, at = 0;
    if (!gui()) return JSRF_NO_DIALOG;
    where(start, dir, sizeof dir, zen, sizeof zen);
    for (i = 0; patterns && patterns[i]; i++)
        at += (size_t)snprintf(joined + at, sizeof joined - at, "%s%s", i ? " " : "", patterns[i]);
    if (on_path("zenity", prog, sizeof prog)) {
        char *a[9];
        int k = 0;
        snprintf(t, sizeof t, "--title=%s", title);
        snprintf(filter, sizeof filter, "--file-filter=%s | %s", kind ? kind : "Files", joined[0] ? joined : "*");
        a[k++] = prog;
        a[k++] = (char *)"--file-selection";
        a[k++] = t;
        a[k++] = filter;
        a[k++] = zen;
        if (multi) { a[k++] = (char *)"--multiple"; a[k++] = (char *)"--separator=\n"; }
        a[k] = NULL;
        return ask(a, each, ctx);
    }
    if (on_path("kdialog", prog, sizeof prog)) {
        char *a[9];
        int k = 0;
        snprintf(kfilter, sizeof kfilter, "%s (%s)", kind ? kind : "Files", joined[0] ? joined : "*");
        a[k++] = prog;
        a[k++] = (char *)"--getopenfilename";
        a[k++] = dir;
        a[k++] = kfilter;
        if (multi) { a[k++] = (char *)"--multiple"; a[k++] = (char *)"--separate-output"; }
        a[k++] = (char *)"--title";
        a[k++] = (char *)title;
        a[k] = NULL;
        return ask(a, each, ctx);
    }
    return JSRF_NO_DIALOG;
}

#endif
