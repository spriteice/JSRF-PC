/*
 * The setup's work, without its window (launcher/setup/setup.h): files,
 * SHA-256, downloads through the system's HTTPS, unpacking the tools' Python,
 * running the build with an environment of its own, and the first start's
 * steps in order (js_job_run), which the window (setup_ui.cpp) and the
 * console (jsrf_setup_run_console) both drive.
 */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#else
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "setup.h"
#include "setup_job.h"
#include "../../port/xiso.h"
#include "../../port/xbe_check.h"
#include "../../third_party/miniz/miniz.h"

/* ---- what the tools are ----------------------------------------------------- */

/* Pinned: each checked by its SHA-256 before it is used (b2 row 4b, "From the
 * tools' own sites", 2026-11-09). */
#ifdef _WIN32
static const js_download_item g_downloads[] = {
    { "python.tar.gz",
      "https://github.com/astral-sh/python-build-standalone/releases/download/20241016/"
      "cpython-3.13.0%2B20241016-x86_64-pc-windows-msvc-install_only_stripped.tar.gz",
      "c8134287496727922a5c47896b4f2b1623e3aab91cbb7c1ca64542db7593f3f1", 0 },
    { "capstone-5.0.9-py3-none-win_amd64.whl",
      "https://files.pythonhosted.org/packages/50/e6/6f06fdb6a9ed32b2f7cd9c036b92d5324112c3ef7080f2c71efc367d40dd/"
      "capstone-5.0.9-py3-none-win_amd64.whl",
      "732cedbbb56d42e723f14d7af6387f1454194a820b4b96b56d1e53f865ef85d0", 1273459 },
    { "ziglang-0.13.0-py3-none-win_amd64.whl",
      "https://files.pythonhosted.org/packages/e0/0c/66f4710aedf5544e15779ff9c6e770322986e15daf9299b1a4df0be19e09/"
      "ziglang-0.13.0-py3-none-win_amd64.whl",
      "b64b2aa936ebbfeecfbb2611df331a5474edebe1246b98c2f56d6ae4d58b71fc", 80418705 },
};
#define JS_PYTHON "tools/python/python.exe"
#else
static const js_download_item g_downloads[] = {
    { "python.tar.gz",
      "https://github.com/astral-sh/python-build-standalone/releases/download/20241016/"
      "cpython-3.13.0%2B20241016-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz",
      "b5e74d1e16402b633c6f04519618231fc0dbae7d2f9e4b1ac17c294cc3d3d076", 0 },
    { "capstone-5.0.9-py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl",
      "https://files.pythonhosted.org/packages/d1/39/17747862222bb062e86b501f1f148d5ff589b77908b080d30f7f085cbfb7/"
      "capstone-5.0.9-py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl",
      "273fd8d747d2e35c88f91450be51a603ecfaafb00d96d9f315dcb8689c86193e", 1485316 },
    { "ziglang-0.13.0-py3-none-manylinux_2_12_x86_64.manylinux2010_x86_64.musllinux_1_1_x86_64.whl",
      "https://files.pythonhosted.org/packages/0c/b7/ecfdce6368cc3664d301f7f52db4fe1004aa7da7a12c4a9bf1de534ff6ab/"
      "ziglang-0.13.0-py3-none-manylinux_2_12_x86_64.manylinux2010_x86_64.musllinux_1_1_x86_64.whl",
      "3ce0c9f16547e5d61b32e0d226926e9a2552ef4b91fccf7ab5ea1a623a77824b", 80896325 },
};
#define JS_PYTHON "tools/python/bin/python3"
#endif
#define JS_NDOWNLOADS (sizeof g_downloads / sizeof g_downloads[0])

/* The tools folder's mark: written last, so a folder that stopped halfway
 * counts as none. The tools archive carries the same. */
#define JS_TOOLS_ID "python-3.13.0-20241016+capstone-5.0.9+ziglang-0.13.0"

/* The bar's shares when the tools are downloaded, from the Windows PC's first
 * start (players-pc-build/FINDINGS.md): downloads 9%, the build the rest. */
#define JS_DOWNLOAD_SHARE 0.09

const js_download_item *js_downloads(size_t *n) { *n = JS_NDOWNLOADS; return g_downloads; }
const char *js_tools_id(void) { return JS_TOOLS_ID; }

/* ---- small things ------------------------------------------------------------ */

static void sfmt(char *out, size_t n, const char *fmt, ...)
{
    va_list ap;
    if (!n) return;
    va_start(ap, fmt);
    vsnprintf(out, n, fmt, ap);
    va_end(ap);
    out[n - 1] = 0;
}

void js_path(char *out, size_t n, const char *a, const char *b)
{
    size_t la = strlen(a);
    char *p;
    if (la && (a[la - 1] == '/' || a[la - 1] == '\\')) sfmt(out, n, "%s%s", a, b);
    else sfmt(out, n, "%s/%s", a, b);
#ifdef _WIN32
    for (p = out; *p; p++) if (*p == '/') *p = '\\';
#else
    (void)p;
#endif
}

#ifdef _WIN32
static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t) * (n > 0 ? n : 1));
    if (!w) return NULL;
    if (n <= 0) { w[0] = 0; return w; }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}
static void narrow(const wchar_t *w, char *out, size_t n)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL) && n) out[0] = 0;
}
FILE *js_fopen(const char *path, const char *mode)
{
    wchar_t *wp = wide(path), wm[8];
    FILE *f;
    int i;
    for (i = 0; mode[i] && i < 7; i++) wm[i] = (wchar_t)mode[i];
    wm[i] = 0;
    f = wp ? _wfopen(wp, wm) : NULL;
    free(wp);
    return f;
}
#else
FILE *js_fopen(const char *path, const char *mode) { return fopen(path, mode); }
#endif

int js_is_file(const char *path)
{
#ifdef _WIN32
    wchar_t *w = wide(path);
    DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
    free(w);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

int js_is_dir(const char *path)
{
#ifdef _WIN32
    wchar_t *w = wide(path);
    DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
    free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

int js_mkdirs(const char *path)
{
    char buf[4096];
    size_t i, n = strlen(path);
    if (n >= sizeof buf) return -1;
    memcpy(buf, path, n + 1);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == 0) {
            char c = buf[i];
            buf[i] = 0;
#ifdef _WIN32
            if (!(i == 2 && buf[1] == ':')) {
                wchar_t *w = wide(buf);
                if (w) { CreateDirectoryW(w, NULL); free(w); }
            }
#else
            if (buf[0]) mkdir(buf, 0755);
#endif
            buf[i] = c;
        }
    }
    return js_is_dir(path) ? 0 : -1;
}

#ifdef _WIN32
static int rmtree_w(const wchar_t *dir)
{
    WIN32_FIND_DATAW fd;
    wchar_t pat[1100], p[1100];
    HANDLE h;
    int bad = 0;
    _snwprintf(pat, 1100, L"%ls\\*", dir);
    pat[1099] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            _snwprintf(p, 1100, L"%ls\\%ls", dir, fd.cFileName);
            p[1099] = 0;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                bad |= rmtree_w(p);
            } else {
                SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL);
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (!RemoveDirectoryW(p)) bad = 1; }
                else if (!DeleteFileW(p)) bad = 1;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    SetFileAttributesW(dir, FILE_ATTRIBUTE_NORMAL);
    if (!RemoveDirectoryW(dir)) bad = 1;
    return bad;
}
#else
static int rm_one(const char *p, const struct stat *st, int flag, struct FTW *f)
{
    (void)st; (void)flag; (void)f;
    return remove(p) ? -1 : 0;
}
#endif

/* The folder and everything in it. 0 when it is gone (or wasn't there). */
int js_rmtree(const char *path)
{
#ifdef _WIN32
    wchar_t *w, lp[1100];
    if (!js_is_dir(path)) {
        if (js_is_file(path)) { w = wide(path); if (w) { SetFileAttributesW(w, FILE_ATTRIBUTE_NORMAL); DeleteFileW(w); free(w); } }
        return js_is_file(path) ? -1 : 0;
    }
    w = wide(path);
    if (!w) return -1;
    /* \\?\: the tools' deepest files are past 260 characters in a deep folder. */
    _snwprintf(lp, 1100, L"\\\\?\\%ls", w);
    lp[1099] = 0;
    free(w);
    rmtree_w(lp);
#else
    if (!js_is_dir(path)) { if (js_is_file(path)) remove(path); return js_is_file(path) ? -1 : 0; }
    nftw(path, rm_one, 32, FTW_DEPTH | FTW_PHYS);
#endif
    return js_is_dir(path) ? -1 : 0;
}

#ifdef _WIN32
static uint64_t tree_size_w(const wchar_t *dir)
{
    WIN32_FIND_DATAW fd;
    wchar_t pat[1100], p[1100];
    HANDLE h;
    uint64_t s = 0;
    _snwprintf(pat, 1100, L"%ls\\*", dir);
    pat[1099] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        _snwprintf(p, 1100, L"%ls\\%ls", dir, fd.cFileName);
        p[1099] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) s += tree_size_w(p);
        else s += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return s;
}
#else
static uint64_t g_tree_sum;
static int sum_one(const char *p, const struct stat *st, int flag, struct FTW *f)
{
    (void)p; (void)f;
    if (flag == FTW_F) g_tree_sum += (uint64_t)st->st_size;
    return 0;
}
#endif

uint64_t js_tree_size(const char *path)
{
#ifdef _WIN32
    wchar_t *w = wide(path), lp[1100];
    uint64_t s;
    if (!w) return 0;
    _snwprintf(lp, 1100, L"\\\\?\\%ls", w);
    lp[1099] = 0;
    free(w);
    s = tree_size_w(lp);
    return s;
#else
    g_tree_sum = 0;
    if (js_is_dir(path)) nftw(path, sum_one, 32, FTW_PHYS);
    return g_tree_sum;
#endif
}

int js_write_text(const char *path, const char *text)
{
    FILE *f = js_fopen(path, "wb");
    if (!f) return -1;
    fputs(text, f);
    return fclose(f) ? -1 : 0;
}

/* key=value lines: the last value given for key. 1 if found. A line too long
 * to read whole (over 8 KB, more than any path) is skipped, all of it. */
int js_read_kv(const char *path, const char *key, char *val, size_t n)
{
    char line[8192];
    size_t kl = strlen(key);
    int found = 0, whole = 1;
    FILE *f = js_fopen(path, "rb");
    if (n) val[0] = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        int was_whole = whole;
        whole = l && line[l - 1] == '\n';
        if (!whole && !feof(f)) continue;     /* the rest of it comes next */
        if (!was_whole) continue;             /* the end of one too long */
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!strncmp(line, key, kl) && line[kl] == '=') {
            sfmt(val, n, "%s", line + kl + 1);
            found = 1;
        }
    }
    fclose(f);
    return found;
}

/* ---- SHA-256 (FIPS 180-4) --------------------------------------------------------- */

typedef struct { uint32_t h[8]; uint64_t len; unsigned char buf[64]; size_t used; } sha256_ctx;

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(sha256_ctx *c, const unsigned char *p)
{
    uint32_t w[64], a, b, cc, d, e, f, g, h, t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (i = 0; i < 64; i++) {
        t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha256_init(sha256_ctx *c)
{
    static const uint32_t iv[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    memcpy(c->h, iv, sizeof iv);
    c->len = 0;
    c->used = 0;
}

static void sha256_update(sha256_ctx *c, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    c->len += n;
    if (c->used) {
        size_t take = 64 - c->used < n ? 64 - c->used : n;
        memcpy(c->buf + c->used, p, take);
        c->used += take; p += take; n -= take;
        if (c->used == 64) { sha256_block(c, c->buf); c->used = 0; }
    }
    while (n >= 64) { sha256_block(c, p); p += 64; n -= 64; }
    if (n) { memcpy(c->buf, p, n); c->used = n; }
}

static void sha256_final(sha256_ctx *c, char hex[65])
{
    uint64_t bits = c->len * 8;
    unsigned char pad = 0x80, z = 0, l[8];
    int i;
    sha256_update(c, &pad, 1);
    while (c->used != 56) sha256_update(c, &z, 1);
    for (i = 0; i < 8; i++) l[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha256_update(c, l, 8);
    for (i = 0; i < 8; i++) sprintf(hex + i * 8, "%08x", c->h[i]);
    hex[64] = 0;
}

void js_sha256_hex(const void *data, size_t n, char hex[65])
{
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, data, n);
    sha256_final(&c, hex);
}

int js_sha256_file(const char *path, char hex[65])
{
    static unsigned char buf[1 << 16];
    sha256_ctx c;
    size_t got;
    FILE *f = js_fopen(path, "rb");
    hex[0] = 0;
    if (!f) return -1;
    sha256_init(&c);
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) sha256_update(&c, buf, got);
    fclose(f);
    sha256_final(&c, hex);
    return 0;
}

/* ---- downloads --------------------------------------------------------------- */

typedef struct {
    FILE *f;
    js_progress *p;
    double f0, f1;
    uint64_t done, total;
    const char *name;
    int last_pct;
} dl_state;

static int dl_tick(dl_state *d)
{
    if (d->p && d->p->cancel && *d->p->cancel) return 1;
    if (d->p && d->total) {
        int pct = (int)(d->done * 100 / d->total);
        if (pct != d->last_pct) {
            char t[256];
            d->last_pct = pct;
            sfmt(t, sizeof t, "Downloading the tools: %s, %.1f of %.1f MB", d->name,
                 d->done / 1e6, d->total / 1e6);
            d->p->update(d->p->ctx, d->f0 + (d->f1 - d->f0) * (double)d->done / (double)d->total,
                         -1, 0, t, NULL);
        }
    }
    return 0;
}

#ifdef _WIN32
static int http_get(const char *url, dl_state *d, char *err, size_t errlen)
{
    wchar_t *wurl = wide(url), host[256], path[2048];
    URL_COMPONENTS uc;
    HINTERNET s = NULL, c = NULL, r = NULL;
    DWORD status = 0, len = sizeof status, avail;
    int rc = -1;
    static unsigned char buf[1 << 16];

    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!wurl || !WinHttpCrackUrl(wurl, 0, 0, &uc)) { sfmt(err, errlen, "a bad address: %s", url); goto out; }
    /* The system's own proxy settings, as a browser uses. */
    s = WinHttpOpen(L"JSRF-PC-setup/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"JSRF-PC-setup/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { sfmt(err, errlen, "Windows' HTTPS couldn't start (error %lu)", GetLastError()); goto out; }
    WinHttpSetTimeouts(s, 30000, 30000, 60000, 60000);
    c = WinHttpConnect(s, host, uc.nPort, 0);
    if (!c) { sfmt(err, errlen, "couldn't reach %ls (error %lu)", host, GetLastError()); goto out; }
    r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                           uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!r || !WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(r, NULL)) {
        sfmt(err, errlen, "couldn't download from %ls (error %lu): is the PC online?", host, GetLastError());
        goto out;
    }
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) { sfmt(err, errlen, "%ls answered %lu", host, status); goto out; }
    {
        wchar_t cl[32];
        DWORD cll = sizeof cl;
        if (!d->total && WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                             cl, &cll, WINHTTP_NO_HEADER_INDEX))
            d->total = _wcstoui64(cl, NULL, 10);
    }
    for (;;) {
        DWORD got = 0;
        if (!WinHttpQueryDataAvailable(r, &avail)) { sfmt(err, errlen, "the download stopped (error %lu)", GetLastError()); goto out; }
        if (!avail) break;
        if (avail > sizeof buf) avail = sizeof buf;
        if (!WinHttpReadData(r, buf, avail, &got)) { sfmt(err, errlen, "the download stopped (error %lu)", GetLastError()); goto out; }
        if (!got) break;
        if (fwrite(buf, 1, got, d->f) != got) { sfmt(err, errlen, "couldn't write the download: is the disk full?"); goto out; }
        d->done += got;
        if (dl_tick(d)) { sfmt(err, errlen, "cancelled"); goto out; }
    }
    rc = 0;
out:
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    free(wurl);
    return rc;
}
#else
/* libcurl, the system's (every desktop Linux has libcurl.so.4), opened when
 * needed: nothing to link against, and the launcher starts without it. */
typedef void CURL;
#define JS_CURLOPT_WRITEDATA        10001
#define JS_CURLOPT_URL              10002
#define JS_CURLOPT_ERRORBUFFER      10010
#define JS_CURLOPT_WRITEFUNCTION    20011
#define JS_CURLOPT_USERAGENT        10018
#define JS_CURLOPT_LOW_SPEED_LIMIT  19
#define JS_CURLOPT_LOW_SPEED_TIME   20
#define JS_CURLOPT_NOPROGRESS       43
#define JS_CURLOPT_FAILONERROR      45
#define JS_CURLOPT_FOLLOWLOCATION   52
#define JS_CURLOPT_CONNECTTIMEOUT   78
#define JS_CURLOPT_XFERINFODATA     10057
#define JS_CURLOPT_XFERINFOFUNCTION 20219
#define JS_CURLOPT_PROTOCOLS_STR    10318
static struct {
    void *lib;
    CURL *(*init)(void);
    int (*setopt)(CURL *, int, ...);
    int (*perform)(CURL *);
    void (*cleanup)(CURL *);
    const char *(*strerror)(int);
} g_curl;

static size_t curl_write(char *p, size_t sz, size_t n, void *ctx)
{
    dl_state *d = (dl_state *)ctx;
    size_t w = fwrite(p, sz, n, d->f);
    d->done += w * sz;
    return w;
}
static int curl_xfer(void *ctx, int64_t dltotal, int64_t dlnow, int64_t ut, int64_t un)
{
    dl_state *d = (dl_state *)ctx;
    (void)ut; (void)un;
    if (!d->total && dltotal > 0) d->total = (uint64_t)dltotal;
    (void)dlnow;
    return dl_tick(d);
}

/* 0 when it's downloaded; -1 when it failed, -2 when trying again
 * wouldn't change that (no libcurl). */
static int http_get(const char *url, dl_state *d, char *err, size_t errlen)
{
    CURL *c;
    char eb[256] = "";
    int rc;
    if (!g_curl.lib) {
        g_curl.lib = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
        if (!g_curl.lib) g_curl.lib = dlopen("libcurl-gnutls.so.4", RTLD_NOW | RTLD_LOCAL);
        if (!g_curl.lib) {
            sfmt(err, errlen, "this system has no libcurl.so.4 to download with: install curl's library "
                              "(libcurl4 on Ubuntu and Debian), or unpack the tools archive beside the launcher");
            return -2;
        }
        *(void **)&g_curl.init = dlsym(g_curl.lib, "curl_easy_init");
        *(void **)&g_curl.setopt = dlsym(g_curl.lib, "curl_easy_setopt");
        *(void **)&g_curl.perform = dlsym(g_curl.lib, "curl_easy_perform");
        *(void **)&g_curl.cleanup = dlsym(g_curl.lib, "curl_easy_cleanup");
        *(void **)&g_curl.strerror = dlsym(g_curl.lib, "curl_easy_strerror");
        if (!g_curl.init || !g_curl.setopt || !g_curl.perform || !g_curl.cleanup || !g_curl.strerror) {
            sfmt(err, errlen, "this system's libcurl.so.4 lacks what downloading needs");
            dlclose(g_curl.lib);
            g_curl.lib = NULL;
            return -2;
        }
    }
    c = g_curl.init();
    if (!c) { sfmt(err, errlen, "libcurl couldn't start"); return -1; }
    g_curl.setopt(c, JS_CURLOPT_URL, url);
    g_curl.setopt(c, JS_CURLOPT_PROTOCOLS_STR, "https");
    g_curl.setopt(c, JS_CURLOPT_FOLLOWLOCATION, 1L);
    g_curl.setopt(c, JS_CURLOPT_FAILONERROR, 1L);
    g_curl.setopt(c, JS_CURLOPT_USERAGENT, "JSRF-PC-setup/1");
    g_curl.setopt(c, JS_CURLOPT_CONNECTTIMEOUT, 30L);
    g_curl.setopt(c, JS_CURLOPT_LOW_SPEED_LIMIT, 1L);
    g_curl.setopt(c, JS_CURLOPT_LOW_SPEED_TIME, 60L);
    g_curl.setopt(c, JS_CURLOPT_ERRORBUFFER, eb);
    g_curl.setopt(c, JS_CURLOPT_WRITEFUNCTION, curl_write);
    g_curl.setopt(c, JS_CURLOPT_WRITEDATA, (void *)d);
    g_curl.setopt(c, JS_CURLOPT_NOPROGRESS, 0L);
    g_curl.setopt(c, JS_CURLOPT_XFERINFOFUNCTION, curl_xfer);
    g_curl.setopt(c, JS_CURLOPT_XFERINFODATA, (void *)d);
    rc = g_curl.perform(c);
    g_curl.cleanup(c);
    if (rc != 0) {
        if (d->p && d->p->cancel && *d->p->cancel) sfmt(err, errlen, "cancelled");
        else sfmt(err, errlen, "couldn't download %s: %s", url, eb[0] ? eb : g_curl.strerror(rc));
        return -1;
    }
    return 0;
}
#endif

/* The waits before the second, third and fourth tries, in seconds: a site
 * fails now and then for a moment (CI's downloads from GitHub get a 504 now
 * and then, and the next try a few seconds later works). When it's down for
 * longer, as when GitHub answered 504 to three tries in 12 s and again
 * three minutes later (the Windows PC, 9 November), the player is told to
 * try later. */
static const int g_download_waits[] = { 5, 20, 45 };
#define JS_DOWNLOAD_TRIES (1 + (int)(sizeof g_download_waits / sizeof g_download_waits[0]))

/* Waits secs seconds, a tenth at a time, so that closing the window stops it
 * at once: 1 when it was stopped, else 0. */
static int js_pause(js_progress *p, int secs)
{
    int i;
    for (i = 0; i < secs * 10; i++) {
        if (p && p->cancel && *p->cancel) return 1;
#ifdef _WIN32
        Sleep(100);
#else
        {
            struct timespec ts = { 0, 100000000L };
            nanosleep(&ts, NULL);
        }
#endif
    }
    return p && p->cancel && *p->cancel;
}

int js_download(const char *url, const char *dest, const char *sha256, uint64_t size,
                js_progress *p, double frac0, double frac1, char *err, size_t errlen)
{
    char part[4096], hex[65];
    const char *name = strrchr(dest, '/');
    const char *bs = strrchr(dest, '\\');
    dl_state d;
    int tries, rc = -1;
    if (bs && (!name || bs > name)) name = bs;
    name = name ? name + 1 : dest;
    sfmt(part, sizeof part, "%s.part", dest);
    for (tries = 0; tries < JS_DOWNLOAD_TRIES && rc != 0; tries++) {
        if (tries > 0) {
            char t[600];
            int w = g_download_waits[tries - 1];
            sfmt(t, sizeof t, "Downloading the tools: %s, trying again in %d s", name, w);
            if (p) p->update(p->ctx, frac0, -1, 0, t, NULL);
            if (js_pause(p, w)) { sfmt(err, errlen, "cancelled"); break; }
        }
        memset(&d, 0, sizeof d);
        d.p = p; d.f0 = frac0; d.f1 = frac1; d.total = size; d.name = name; d.last_pct = -1;
        if (p && p->cancel && *p->cancel) { sfmt(err, errlen, "cancelled"); return -1; }
        d.f = js_fopen(part, "wb");
        if (!d.f) { sfmt(err, errlen, "couldn't write %s", part); return -1; }
        rc = http_get(url, &d, err, errlen);
        if (fclose(d.f)) rc = -1;
        if (rc != 0 && !strcmp(err, "cancelled")) break;
        if (rc == -2) break;   /* nothing a later try would change */
        if (rc != 0 && p) {
            char t[600];
            sfmt(t, sizeof t, "download try %d of %d failed: %s", tries + 1, JS_DOWNLOAD_TRIES, err);
            p->update(p->ctx, -1, -1, 0, NULL, t);
        }
    }
    if (rc != 0) {
        js_rmtree(part);
        if (rc == -1 && strcmp(err, "cancelled") != 0) {
            char e[2048];
            sfmt(e, sizeof e, "%s\n\nThe site may be busy: try again later, or extract the "
                 "release's tools archive beside the launcher instead.", err);
            sfmt(err, errlen, "%s", e);
        }
        return -1;
    }
    js_sha256_file(part, hex);
    if (strcmp(hex, sha256) != 0) {
        sfmt(err, errlen, "%s isn't the file it should be (SHA-256 %s, not %s): nothing of it was used", name, hex, sha256);
        js_rmtree(part);
        return -1;
    }
    js_rmtree(dest);
#ifdef _WIN32
    {
        wchar_t *a = wide(part), *b = wide(dest);
        int ok = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
        free(a); free(b);
        if (!ok) { sfmt(err, errlen, "couldn't move %s into place", name); return -1; }
    }
#else
    if (rename(part, dest)) { sfmt(err, errlen, "couldn't move %s into place", name); return -1; }
#endif
    if (p) {
        char t[300];
        sfmt(t, sizeof t, "downloaded %s, SHA-256 %s", name, hex);
        p->update(p->ctx, frac1, -1, 0, NULL, t);
    }
    return 0;
}

/* ---- unpacking the tools' Python (.tar.gz) ------------------------------------------- */

static uint64_t tar_num(const char *p, size_t n)
{
    uint64_t v = 0;
    size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == 0)) i++;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) v = v * 8 + (uint64_t)(p[i] - '0');
    return v;
}

static int safe_name(const char *n)
{
    const char *s = n;
    if (!*n || n[0] == '/' || n[0] == '\\' || strchr(n, ':')) return 0;
    while (*s) {
        const char *e = s;
        while (*e && *e != '/' && *e != '\\') e++;
        if (e - s == 2 && s[0] == '.' && s[1] == '.') return 0;
        s = *e ? e + 1 : e;
    }
    return 1;
}

#ifndef _WIN32
/* A symbolic link's target, read from the folder the link is in (name's), stays
 * in the archive's folder: each ".." climbs one of the folders above it. */
static int safe_link(const char *name, const char *target)
{
    const char *s;
    int depth = 0;
    if (!*target || target[0] == '/' || target[0] == '\\') return 0;
    for (s = name; *s; s++) depth += *s == '/';
    for (s = target; *s;) {
        const char *e = s;
        while (*e && *e != '/') e++;
        if (e - s == 2 && s[0] == '.' && s[1] == '.') {
            if (--depth < 0) return 0;
        } else if (e - s && !(e - s == 1 && s[0] == '.')) {
            depth++;
        }
        s = *e ? e + 1 : e;
    }
    return 1;
}
#endif

int js_untar_gz(const char *tgz, const char *dest, char *err, size_t errlen)
{
    FILE *f = js_fopen(tgz, "rb");
    unsigned char *gz = NULL, *tar = NULL;
    size_t gzn = 0, tarn = 0, off = 10, pos = 0;
    long sz;
    char longname[4096] = "", path[4096];
    int rc = -1;

    if (!f) { sfmt(err, errlen, "couldn't open %s", tgz); return -1; }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 18 || !(gz = (unsigned char *)malloc((size_t)sz))) { fclose(f); sfmt(err, errlen, "%s is empty", tgz); return -1; }
    gzn = fread(gz, 1, (size_t)sz, f);
    fclose(f);
    if (gzn != (size_t)sz || gz[0] != 0x1f || gz[1] != 0x8b || gz[2] != 8) { sfmt(err, errlen, "%s isn't a .tar.gz", tgz); goto out; }
    if (gz[3] & 4) off += 2 + (size_t)(gz[10] | gz[11] << 8);        /* FEXTRA */
    if (gz[3] & 8) { while (off < gzn && gz[off]) off++; off++; }    /* FNAME */
    if (gz[3] & 16) { while (off < gzn && gz[off]) off++; off++; }   /* FCOMMENT */
    if (gz[3] & 2) off += 2;                                         /* FHCRC */
    if (off >= gzn) { sfmt(err, errlen, "%s is cut short", tgz); goto out; }
    tar = (unsigned char *)tinfl_decompress_mem_to_heap(gz + off, gzn - off, &tarn, 0);
    if (!tar) { sfmt(err, errlen, "%s couldn't be unpacked (its data is damaged)", tgz); goto out; }
    free(gz);
    gz = NULL;

    while (pos + 512 <= tarn) {
        const char *h = (const char *)tar + pos;
        uint64_t size, mode;
        char type, name[4096];
        size_t blocks;
        if (!h[0]) break;                                            /* the end: zero blocks */
        size = tar_num(h + 124, 12);
        mode = tar_num(h + 100, 8);
        type = h[156];
        blocks = (size_t)((size + 511) / 512) * 512;
        if (pos + 512 + blocks > tarn) { sfmt(err, errlen, "%s is cut short", tgz); goto out; }
        if (longname[0]) {
            sfmt(name, sizeof name, "%s", longname);
            longname[0] = 0;
        } else if (!memcmp(h + 257, "ustar", 5) && h[345]) {
            sfmt(name, sizeof name, "%.155s/%.100s", h + 345, h);
        } else {
            sfmt(name, sizeof name, "%.100s", h);
        }
        if (type == 'L') {                                           /* GNU: the next one's long name */
            size_t l = size < sizeof longname - 1 ? (size_t)size : sizeof longname - 1;
            memcpy(longname, tar + pos + 512, l);
            longname[l] = 0;
        } else if (type == 'x') {                                    /* pax: path= for the next one */
            const char *r = (const char *)tar + pos + 512, *end = r + size;
            while (r < end) {
                const char *sp = memchr(r, ' ', (size_t)(end - r));
                long rl = atol(r);
                if (!sp || rl <= 0 || r + rl > end) break;
                if (!strncmp(sp + 1, "path=", 5)) {
                    size_t l = (size_t)(r + rl - (sp + 6) - 1);
                    if (l >= sizeof longname) l = sizeof longname - 1;
                    memcpy(longname, sp + 6, l);
                    longname[l] = 0;
                }
                r += rl;
            }
        } else if (type == 'g') {
            /* global pax header: nothing in it matters here */
        } else {
            size_t nl = strlen(name);
            while (nl && name[nl - 1] == '/') name[--nl] = 0;
            if (nl && !safe_name(name)) { sfmt(err, errlen, "%s holds a file outside its folder: %s", tgz, name); goto out; }
            if (nl) {
                js_path(path, sizeof path, dest, name);
                if (type != '5') {
                    /* The folder it is in, which the archive may list later
                     * or not at all. */
                    char dir[4096], *s1, *s2;
                    sfmt(dir, sizeof dir, "%s", path);
                    s1 = strrchr(dir, '/'); s2 = strrchr(dir, '\\');
                    if (s2 > s1) s1 = s2;
                    if (s1) { *s1 = 0; js_mkdirs(dir); }
                }
                if (type == '5') {
                    js_mkdirs(path);
                } else if (type == '0' || type == 0 || type == '7') {
                    FILE *o;
                    o = js_fopen(path, "wb");
                    if (!o || fwrite(tar + pos + 512, 1, (size_t)size, o) != (size_t)size) {
                        if (o) fclose(o);
                        sfmt(err, errlen, "couldn't write %s: is the disk full?", path);
                        goto out;
                    }
                    if (fclose(o)) { sfmt(err, errlen, "couldn't write %s: is the disk full?", path); goto out; }
#ifndef _WIN32
                    chmod(path, (mode_t)(mode & 0777));
#else
                    (void)mode;
#endif
                } else if (type == '2' || type == '1') {
#ifndef _WIN32
                    /* Links within the folder only, as Python's own are
                     * (bin/python3 -> python3.13). */
                    char target[101], from[4096];
                    sfmt(target, sizeof target, "%.100s", h + 157);
                    if (type == '2' ? !safe_link(name, target) : !safe_name(target)) {
                        sfmt(err, errlen, "%s links outside its folder: %s", tgz, name);
                        goto out;
                    }
                    remove(path);
                    if (type == '2') {
                        if (symlink(target, path)) { sfmt(err, errlen, "couldn't make the link %s", path); goto out; }
                    } else {
                        js_path(from, sizeof from, dest, target);
                        if (link(from, path)) { sfmt(err, errlen, "couldn't make the link %s", path); goto out; }
                    }
#endif
                }
            }
        }
        pos += 512 + blocks;
    }
    rc = 0;
out:
    free(gz);
    if (tar) mz_free(tar);
    return rc;
}

/* ---- running a program -------------------------------------------------------------- */

#ifdef _WIN32
static HANDLE g_job;
#else
static volatile pid_t g_child;
#endif

void js_kill_child(void)
{
#ifdef _WIN32
    if (g_job) TerminateJobObject(g_job, 1);
#else
    if (g_child > 0) kill(-g_child, SIGKILL);
#endif
}

static void feed(char *acc, size_t *have, size_t cap, const char *data, size_t n,
                 void (*line)(void *, const char *), void *ctx)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char ch = data[i];
        if (ch == '\r') continue;
        if (ch == '\n' || *have == cap - 1) {
            acc[*have] = 0;
            line(ctx, acc);
            *have = 0;
            if (ch == '\n') continue;
        }
        acc[(*have)++] = ch;
    }
}

#ifdef _WIN32
static void quote_arg(wchar_t *out, size_t cap, size_t *len, const wchar_t *a)
{
    /* CommandLineToArgvW's rules: what the program reads back is a. */
    size_t i, n = wcslen(a), bs;
    int need = n == 0 || wcspbrk(a, L" \t\"") != NULL;
#define PUT(c) do { if (*len + 1 < cap) out[(*len)++] = (c); } while (0)
    if (*len) PUT(L' ');
    if (!need) { for (i = 0; i < n; i++) PUT(a[i]); return; }
    PUT(L'"');
    for (i = 0; i < n; i++) {
        bs = 0;
        while (i < n && a[i] == L'\\') { bs++; i++; }
        if (i == n) { while (bs--) { PUT(L'\\'); PUT(L'\\'); } break; }
        if (a[i] == L'"') { while (bs--) { PUT(L'\\'); PUT(L'\\'); } PUT(L'\\'); PUT(L'"'); }
        else { while (bs--) PUT(L'\\'); PUT(a[i]); }
    }
    PUT(L'"');
#undef PUT
}

static int env_cmp(const void *a, const void *b)
{
    return _wcsicmp(*(const wchar_t *const *)a, *(const wchar_t *const *)b);
}
#endif

int js_run(const char *const *argv, const char *const *env, const char *cwd,
           void (*line)(void *ctx, const char *text), void *ctx, volatile int *cancel,
           char *err, size_t errlen)
{
    char acc[4096];
    size_t have = 0;
#ifdef _WIN32
    static wchar_t cmd[32768];
    wchar_t *wenv, *wcwd = wide(cwd), *wexe = wide(argv[0]), *list[64];
    size_t len = 0, i, ne = 0, el = 0;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL, wr = NULL;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl;
    DWORD code = 1;
    char buf[4096];

    for (i = 0; argv[i]; i++) {
        wchar_t *w = wide(argv[i]);
        if (!w) { sfmt(err, errlen, "out of memory"); return -1; }
        quote_arg(cmd, sizeof cmd / sizeof cmd[0], &len, w);
        free(w);
    }
    cmd[len] = 0;
    for (i = 0; env[i] && ne < 63; i++) list[ne++] = wide(env[i]);
    qsort(list, ne, sizeof list[0], env_cmp);
    for (i = 0; i < ne; i++) el += wcslen(list[i]) + 1;
    wenv = (wchar_t *)calloc(el + 2, sizeof(wchar_t));
    if (wenv) {
        wchar_t *q = wenv;
        for (i = 0; i < ne; i++) { size_t l = wcslen(list[i]); memcpy(q, list[i], l * sizeof(wchar_t)); q += l + 1; }
    }
    for (i = 0; i < ne; i++) free(list[i]);

    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { sfmt(err, errlen, "couldn't make a pipe (error %lu)", GetLastError()); goto fail; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = NULL;
    si.hStdOutput = wr;
    si.hStdError = wr;
    /* In a job that ends with it: cancelling stops the build's own programs too. */
    g_job = CreateJobObjectW(NULL, NULL);
    memset(&jl, 0, sizeof jl);
    jl.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (g_job) SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &jl, sizeof jl);
    if (!CreateProcessW(wexe, cmd, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                        wenv, wcwd, &si, &pi)) {
        sfmt(err, errlen, "couldn't start %s (error %lu)", argv[0], GetLastError());
        goto fail;
    }
    if (g_job) AssignProcessToJobObject(g_job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(wr);
    wr = NULL;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(rd, buf, sizeof buf, &got, NULL) || !got) break;
        feed(acc, &have, sizeof acc, buf, got, line, ctx);
        if (cancel && *cancel) TerminateJobObject(g_job, 1);
    }
    if (have) { acc[have] = 0; line(ctx, acc); }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    if (g_job) { CloseHandle(g_job); g_job = NULL; }
    free(wenv); free(wcwd); free(wexe);
    return (int)code;
fail:
    if (rd) CloseHandle(rd);
    if (wr) CloseHandle(wr);
    if (g_job) { CloseHandle(g_job); g_job = NULL; }
    free(wenv); free(wcwd); free(wexe);
    return -1;
#else
    int fd[2], st = 0;
    pid_t pid;
    char buf[4096];
    if (pipe(fd)) { sfmt(err, errlen, "couldn't make a pipe"); return -1; }
    pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); sfmt(err, errlen, "couldn't start %s", argv[0]); return -1; }
    if (pid == 0) {
        int nul = open("/dev/null", O_RDONLY);
        setpgid(0, 0);   /* its own group: cancelling stops the build's programs too */
        if (nul >= 0) { dup2(nul, 0); close(nul); }
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        close(fd[0]);
        close(fd[1]);
        if (cwd && chdir(cwd)) _exit(126);
        execve(argv[0], (char *const *)argv, (char *const *)env);
        {
            const char m[] = "setup: couldn't run the program\n";
            ssize_t w = write(2, m, sizeof m - 1);
            (void)w;
        }
        _exit(127);
    }
    g_child = pid;
    close(fd[1]);
    for (;;) {
        ssize_t got = read(fd[0], buf, sizeof buf);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        feed(acc, &have, sizeof acc, buf, (size_t)got, line, ctx);
        if (cancel && *cancel) kill(-pid, SIGKILL);
    }
    if (have) { acc[have] = 0; line(ctx, acc); }
    close(fd[0]);
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    g_child = 0;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
#endif
}

/* ---- where things are --------------------------------------------------------------- */

void js_engine_path(const char *root, char *out, size_t n)
{
#ifdef _WIN32
    js_path(out, n, root, "engine/jsrf_recomp.exe");
#else
    js_path(out, n, root, "bin/jsrf_recomp");
#endif
}

/* The tools folder is there and whole: its mark says it's this release's. */
int js_tools_ready(const char *root)
{
    char p[4096], v[256];
    js_path(p, sizeof p, root, "tools/TOOLS.txt");
    if (!js_read_kv(p, "tools", v, sizeof v) || strcmp(v, JS_TOOLS_ID) != 0) return 0;
    js_path(p, sizeof p, root, JS_PYTHON);
    return js_is_file(p);
}

int jsrf_setup_engine_ready(const char *root)
{
    char engine[4096], p[4096], have[65], want[128];
    js_engine_path(root, engine, sizeof engine);
    if (!js_is_file(engine) || js_sha256_file(engine, have)) return 0;
    js_path(p, sizeof p, root, "kit/expected.txt");
    if (js_read_kv(p, "engine_sha256", want, sizeof want) && want[0] && !strcmp(have, want)) return 1;
    /* Not known when the release was made: the one built here, as it said. */
    if (!want[0]) {
        js_path(p, sizeof p, root, "build/engine.sha256");
        if (js_read_kv(p, "engine_sha256", want, sizeof want) && !strcmp(have, want)) return 1;
    }
    /* Another engine, which the player said to play anyway. */
    js_path(p, sizeof p, root, "build/engine.sha256");
    if (js_read_kv(p, "accepted", want, sizeof want) && !strcmp(have, want)) return 1;
    return 0;
}

int jsrf_setup_needed(const char *root)
{
    char p[4096];
    js_path(p, sizeof p, root, "kit/kit.json");
    return js_is_file(p) && !jsrf_setup_engine_ready(root);
}

/* ---- the first start's steps ------------------------------------------------------------- */

static void job_log(js_job *j, const char *text)
{
    if (j->log) { fputs(text, j->log); fputc('\n', j->log); fflush(j->log); }
    if (j->prog.update) j->prog.update(j->prog.ctx, -1, -1, 0, NULL, text);
}

static void job_line(void *ctx, const char *text)
{
    js_job *j = (js_job *)ctx;
    double a, b;
    int secs, n = 0;
    if (j->log) { fputs(text, j->log); fputc('\n', j->log); fflush(j->log); }
    if (sscanf(text, "step %lf %lf %d %n", &a, &b, &secs, &n) == 3 && n > 0) {
        double base = j->base, span = 1.0 - j->base;
        j->prog.update(j->prog.ctx, base + span * a, base + span * b, secs, text + n, NULL);
        return;
    }
    if (!strncmp(text, "done ", 5)) {
        sscanf(text + 5, "%64s %15s %64s %15s", j->engine_sha, j->engine_check, j->gen_sha, j->gen_check);
    } else if (!strncmp(text, "error: ", 7)) {
        sfmt(j->err, sizeof j->err, "%s", text + 7);
    }
    j->prog.update(j->prog.ctx, -1, -1, 0, NULL, text);
}

static int fail_job(js_job *j, const char *fmt, ...)
{
    char buf[sizeof j->err];   /* the arguments may be j->err itself */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = 0;
    memcpy(j->err, buf, sizeof buf);
    job_log(j, j->err);
    return -1;
}

/* The environment the tools' Python runs with: nothing of the system's but
 * what Windows needs to start a program. build_engine.py makes its own for
 * the programs it runs. */
static void job_env(js_job *j, char env[][4200], const char **ptrs)
{
    int n = 0;
    char tmp[4096];
    js_path(tmp, sizeof tmp, j->s->root, "build/tmp");
    js_mkdirs(tmp);
#ifdef _WIN32
    {
        wchar_t sr[MAX_PATH] = L"C:\\Windows";
        char sru[MAX_PATH * 3];
        GetEnvironmentVariableW(L"SystemRoot", sr, MAX_PATH);
        narrow(sr, sru, sizeof sru);
        sfmt(env[n++], 4200, "SystemRoot=%s", sru);
        sfmt(env[n++], 4200, "windir=%s", sru);
        sfmt(env[n++], 4200, "PATH=%s\\System32", sru);
        sfmt(env[n++], 4200, "USERPROFILE=%s", tmp);
        sfmt(env[n++], 4200, "APPDATA=%s", tmp);
        sfmt(env[n++], 4200, "LOCALAPPDATA=%s", tmp);
        {
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            sfmt(env[n++], 4200, "NUMBER_OF_PROCESSORS=%lu", (unsigned long)si.dwNumberOfProcessors);
        }
    }
#else
    sfmt(env[n++], 4200, "PATH=/nonexistent");
    sfmt(env[n++], 4200, "HOME=%s", tmp);
    sfmt(env[n++], 4200, "LC_ALL=C");
#endif
    sfmt(env[n++], 4200, "TEMP=%s", tmp);
    sfmt(env[n++], 4200, "TMP=%s", tmp);
    sfmt(env[n++], 4200, "TMPDIR=%s", tmp);
    for (int i = 0; i < n; i++) ptrs[i] = env[i];
    ptrs[n] = NULL;
}

static int run_python(js_job *j, const char **args)
{
    char py[4096], script[4096], env[16][4200], errb[512] = "";
    const char *argv[24], *envp[17];
    int n = 0, rc;
    js_path(py, sizeof py, j->s->root, JS_PYTHON);
    js_path(script, sizeof script, j->s->root, "kit/build_engine.py");
    argv[n++] = py; argv[n++] = "-E"; argv[n++] = "-s"; argv[n++] = "-u"; argv[n++] = "-X"; argv[n++] = "utf8";
    argv[n++] = script; argv[n++] = "--root"; argv[n++] = j->s->root;
    while (*args && n < 23) argv[n++] = *args++;
    argv[n] = NULL;
    job_env(j, env, envp);
    rc = js_run(argv, envp, j->s->root, job_line, j, j->prog.cancel, errb, sizeof errb);
    if (rc < 0) return fail_job(j, "%s", errb);
    return rc;
}

/* default.xbe from the game folder, or read out of the disc image, to
 * build/default.xbe; checked as the launcher checks it. */
static int job_xbe(js_job *j, char *xbe, size_t n)
{
    char why[1024] = "", src[4096];
    int code;
    js_path(xbe, n, j->s->root, "build/default.xbe");
    if (js_is_dir(j->s->game)) {
        js_path(src, sizeof src, j->s->game, "default.xbe");
        code = jsrf_xbe_check_folder(j->s->game, why, sizeof why);
        if (code != JSRF_XBE_OK) return fail_job(j, "%s %s", JSRF_XBE_HEADING ".", why);
        {
            FILE *a = js_fopen(src, "rb"), *b = js_fopen(xbe, "wb");
            static unsigned char buf[1 << 16];
            size_t got;
            int bad = !a || !b;
            while (!bad && (got = fread(buf, 1, sizeof buf, a)) > 0) bad = fwrite(buf, 1, got, b) != got;
            if (a) fclose(a);
            if (b && fclose(b)) bad = 1;
            if (bad) return fail_job(j, "couldn't copy %s", src);
        }
    } else {
        unsigned char *buf;
        size_t got = 0;
        uint64_t size = 0;
        char e[512] = "";
        FILE *b;
        code = jsrf_xbe_check_iso(j->s->game, why, sizeof why);
        if (code != JSRF_XBE_OK) return fail_job(j, "%s %s", JSRF_XBE_HEADING ".", why);
        if (xiso_read_file(j->s->game, "default.xbe", NULL, 0, &got, &size, e, sizeof e) != 0 || !size || size > (64u << 20))
            return fail_job(j, "couldn't read default.xbe in %s: %s", j->s->game, e);
        buf = (unsigned char *)malloc((size_t)size);
        if (!buf) return fail_job(j, "out of memory");
        if (xiso_read_file(j->s->game, "default.xbe", buf, (size_t)size, &got, &size, e, sizeof e) != 0 || got != size) {
            free(buf);
            return fail_job(j, "couldn't read default.xbe in %s: %s", j->s->game, e);
        }
        b = js_fopen(xbe, "wb");
        if (!b || fwrite(buf, 1, got, b) != got || fclose(b)) { free(buf); return fail_job(j, "couldn't write %s", xbe); }
        free(buf);
    }
    return 0;
}

int js_job_run(js_job *j)
{
    char p[4096], tools[4096], dl[4096], xbe[4096];
    size_t i;
    js_path(p, sizeof p, j->s->root, "build");
    js_mkdirs(p);
    js_path(p, sizeof p, j->s->root, "build/build.log");
    j->log = js_fopen(p, "wb");
    sfmt(j->log_path, sizeof j->log_path, "%s", p);
    js_path(tools, sizeof tools, j->s->root, "tools");
    j->base = j->download ? JS_DOWNLOAD_SHARE : 0.0;

    if (j->download) {
        uint64_t total = 0, at = 0;
        const double share_dl = JS_DOWNLOAD_SHARE * 0.85;   /* the rest: unpacking */
        const char *wheels[8];
        int nw = 0, rc;
        static char wpaths[JS_NDOWNLOADS][4096];
        /* What was there goes first: an old or half-made tools folder. */
        if (js_rmtree(tools)) return fail_job(j, "couldn't remove the old tools folder %s", tools);
        js_path(dl, sizeof dl, tools, "downloads");
        if (js_mkdirs(dl)) return fail_job(j, "couldn't make %s", dl);
        for (i = 0; i < JS_NDOWNLOADS; i++) total += g_downloads[i].size ? g_downloads[i].size : 25000000;
        for (i = 0; i < JS_NDOWNLOADS; i++) {
            uint64_t s = g_downloads[i].size ? g_downloads[i].size : 25000000;
            js_path(wpaths[i], sizeof wpaths[i], dl, g_downloads[i].name);
            job_log(j, g_downloads[i].url);
            if (js_download(g_downloads[i].url, wpaths[i], g_downloads[i].sha256, g_downloads[i].size,
                            (js_progress *)&j->prog,
                            share_dl * at / total, share_dl * (at + s) / total, j->err, sizeof j->err)) {
                js_rmtree(tools);   /* nothing half-downloaded left behind */
                return fail_job(j, "%s", j->err);
            }
            at += s;
        }
        j->prog.update(j->prog.ctx, share_dl, JS_DOWNLOAD_SHARE, 15, "Unpacking the tools", NULL);
        if (js_untar_gz(wpaths[0], tools, j->err, sizeof j->err)) return fail_job(j, "%s", j->err);
        for (i = 1; i < JS_NDOWNLOADS; i++) { wheels[nw++] = "--install-wheel"; wheels[nw++] = wpaths[i]; }
        wheels[nw++] = "--install-only";
        wheels[nw] = NULL;
        rc = run_python(j, wheels);
        if (rc != 0) return j->err[0] ? -1 : fail_job(j, "the tools' Python couldn't install the tools (exit %d)", rc);
        js_rmtree(dl);
        js_path(p, sizeof p, tools, "TOOLS.txt");
        {
            char t[512];
            sfmt(t, sizeof t, "tools=%s\n", JS_TOOLS_ID);
            if (js_write_text(p, t)) return fail_job(j, "couldn't write %s", p);
        }
        job_log(j, "the tools are ready");
    }
    if (!js_tools_ready(j->s->root)) return fail_job(j, "the tools folder isn't whole: %s", tools);

    j->prog.update(j->prog.ctx, j->base, j->base, 0, "Reading the game", NULL);
    {
        /* The tests without the game (tests/setup) give made-up generated
         * code instead, which the build takes in place of translating;
         * nothing else changes. */
        const char *test_gen = getenv("JSRF_SETUP_TEST_GEN");
        const char *args[] = { "--xbe", xbe, test_gen ? "--test-gen" : NULL, test_gen, NULL };
        if (test_gen) {
            js_path(xbe, sizeof xbe, j->s->root, "build/default.xbe");
            js_write_text(xbe, "not a game: the setup's test\n");
            job_log(j, "JSRF_SETUP_TEST_GEN: made-up generated code, not the game's");
        } else if (job_xbe(j, xbe, sizeof xbe)) {
            return -1;
        }
        int rc = run_python(j, args);
        if (j->prog.cancel && *j->prog.cancel) return fail_job(j, "cancelled");
        if (rc != 0 || !j->engine_sha[0]) {
            if (!j->err[0]) fail_job(j, "the build stopped (exit %d)", rc);
            return -1;
        }
    }
    js_path(p, sizeof p, j->s->root, "build/engine.sha256");
    {
        char t[256];
        sfmt(t, sizeof t, "engine_sha256=%s\n", j->engine_sha);
        js_write_text(p, t);
    }
    j->prog.update(j->prog.ctx, 1.0, 1.0, 0, "The engine is built", NULL);
    return 0;
}

/* The player said to play an engine that isn't the release's. */
void js_accept_engine(const char *root, const char *sha)
{
    char p[4096], t[256];
    js_path(p, sizeof p, root, "build/engine.sha256");
    sfmt(t, sizeof t, "engine_sha256=%s\naccepted=%s\n", sha, sha);
    js_write_text(p, t);
}

/* What "Delete" takes: the tools and everything made from the game, but
 * never the engine (and the build's log and stamp, which are small). */
int js_delete_tools(const char *root)
{
    static const char *const what[] = { "tools", "build/generated", "build/work", "build/obj", "build/tmp",
                                        "build/default.xbe" };
    char p[4096];
    size_t i;
    int bad = 0;
    for (i = 0; i < sizeof what / sizeof what[0]; i++) {
        js_path(p, sizeof p, root, what[i]);
        bad |= js_rmtree(p) != 0;
    }
    return bad ? -1 : 0;
}

uint64_t js_tools_size(const char *root)
{
    static const char *const what[] = { "tools", "build/generated", "build/work", "build/obj" };
    char p[4096];
    size_t i;
    uint64_t s = 0;
    for (i = 0; i < sizeof what / sizeof what[0]; i++) {
        js_path(p, sizeof p, root, what[i]);
        s += js_tree_size(p);
    }
    js_path(p, sizeof p, root, "build/default.xbe");
    if (js_is_file(p)) {
        FILE *f = js_fopen(p, "rb");
        if (f) { fseek(f, 0, SEEK_END); s += (uint64_t)ftell(f); fclose(f); }
    }
    return s;
}

/* ---- the console's way ---------------------------------------------------------------- */

static void con_update(void *ctx, double from, double to, int secs, const char *text, const char *log)
{
    (void)ctx; (void)to; (void)secs;
    if (text) printf("[%3d%%] %s\n", from >= 0 ? (int)(from * 100) : -1, text);
    else if (log && getenv("JSRF_SETUP_VERBOSE")) printf("       %s\n", log);
    fflush(stdout);
}

int jsrf_setup_run_console(jsrf_setup *s)
{
    static js_job job;
    static volatile int cancel;
    const char *ans = getenv("JSRF_SETUP_ANSWER");
    const char *keep = getenv("JSRF_SETUP_TOOLS");
    const char *mm = getenv("JSRF_SETUP_MISMATCH");
    int local = js_tools_ready(s->root), rc;
    printf("No matching binary detected. %s\n", local ? "Build now with local tools?" : "Download tools now and build?");
    if (!ans || strcmp(ans, "yes") != 0) { printf("No.\n"); return JSRF_SETUP_CLOSED; }
    printf("Yes.\n");
    if (!s->find_game || !s->find_game(s->ctx, s->game, sizeof s->game)) return JSRF_SETUP_CLOSED;
    printf("the game: %s\n", s->game);
    memset(&job, 0, sizeof job);
    job.s = s;
    job.download = !local;
    job.prog.update = con_update;
    job.prog.cancel = &cancel;
    rc = js_job_run(&job);
    if (job.log) { fclose(job.log); job.log = NULL; }
    if (rc != 0) {
        printf("The engine couldn't be built: %s\nThe whole log is in: %s\n", job.err, job.log_path);
        return JSRF_SETUP_FAILED;
    }
    printf("engine %s (%s), generated code %s (%s)\n", job.engine_sha, job.engine_check, job.gen_sha, job.gen_check);
    if (!strcmp(job.engine_check, "mismatch")) {
        printf("The engine built here isn't the one this release builds. Play anyway?\n");
        if (!mm || strcmp(mm, "play") != 0) { printf("No.\n"); return JSRF_SETUP_FAILED; }
        printf("Yes.\n");
        js_accept_engine(s->root, job.engine_sha);
    }
    printf("Delete the tools and the code made from your game (%.0f MB)?\n", (double)js_tools_size(s->root) / 1e6);
    if (keep && !strcmp(keep, "delete")) {
        printf("Delete: %s\n", js_delete_tools(s->root) ? "some files stayed" : "done");
    } else {
        printf("Keep.\n");
    }
    return JSRF_SETUP_READY;
}
