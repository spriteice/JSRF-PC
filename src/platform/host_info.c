/*
 * host_info.c - what the host says about a run, for the log (host_info.h).
 */
#if !defined(_WIN32)
#define _GNU_SOURCE        /* syscall, gmtime_r */
#endif
#include "host_info.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#define HOST_ENVIRON _environ
#else
#include <sys/syscall.h>
#include <unistd.h>
extern char **environ;
#define HOST_ENVIRON environ
#endif

/* The most RECOMP_ and JSRF_ variables the [RUN] line names; past it, the
 * line says how many it left out. */
#define HOST_SWITCHES_MAX 256

static double s_t0 = -1.0;
static volatile long s_tids[HOST_THREADS];

static double mono_seconds(void)
{
#if defined(_WIN32)
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

long long host_info_us(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (long long)(now.QuadPart / freq.QuadPart * 1000000
                       + now.QuadPart % freq.QuadPart * 1000000 / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

double host_info_seconds(void)
{
    return s_t0 < 0.0 ? 0.0 : mono_seconds() - s_t0;
}

/* ── The switches a run started with ───────────────────────────────────── */

static int is_switch(const char *s)
{
    return !strncmp(s, "RECOMP_", 7) || !strncmp(s, "JSRF_", 5);
}

static int by_text(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int host_info_switches(char *const *env, char *out, size_t n)
{
    const char *found[HOST_SWITCHES_MAX];
    int count = 0, kept = 0, i;
    size_t len = 0;

    if (!n) return 0;
    out[0] = '\0';
    for (; env && *env; env++) {
        if (!is_switch(*env)) continue;
        if (kept < HOST_SWITCHES_MAX) found[kept++] = *env;
        count++;
    }
    if (!count) {
        snprintf(out, n, "none");
        return 0;
    }
    qsort(found, (size_t)kept, sizeof found[0], by_text);
    for (i = 0; i < kept; i++) {
        size_t need = strlen(found[i]) + (i ? 1 : 0);
        /* Room for this one and, if any come after it, " ..." */
        if (len + need + 4 >= n) {
            snprintf(out + len, n - len, " ...");
            return count;
        }
        len += (size_t)snprintf(out + len, n - len, "%s%s", i ? " " : "", found[i]);
    }
    if (count > kept)
        snprintf(out + len, n - len, " and %d more", count - kept);
    return count;
}

/* ── The power state: Linux's files ────────────────────────────────────── */

/* The first line of a file under root, without its newline; 0 if there is
 * none. */
static int read_first_line(const char *root, const char *path, char *buf, size_t n)
{
    char full[512];
    FILE *f;
    size_t len;
    snprintf(full, sizeof full, "%s%s", root, path);
    f = fopen(full, "r");
    if (!f) return 0;
    if (!fgets(buf, (int)n, f)) { fclose(f); return 0; }
    fclose(f);
    len = strlen(buf);
    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' '))
        buf[--len] = '\0';
    return len > 0;
}

static long read_number(const char *root, const char *path)
{
    char buf[64];
    if (!read_first_line(root, path, buf, sizeof buf)) return -1;
    return strtol(buf, NULL, 10);
}

static void lower(char *s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* The supplies in /sys/class/power_supply: a mains or USB supply online, and
 * the system's first battery. A device's own battery (a pad's, whose scope
 * is Device) isn't the system's. */
static size_t power_source_linux(const char *root, char *out, size_t n)
{
    char dir[512], path[640], type[32], scope[32], status[32] = "";
    int ac_listed = 0, ac_online = 0, battery = 0;
    long capacity = -1;
    DIR *d;
    struct dirent *e;

    snprintf(dir, sizeof dir, "%s/sys/class/power_supply", root);
    d = opendir(dir);
    while (d && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", e->d_name);
        if (!read_first_line(root, path, type, sizeof type)) continue;
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/scope", e->d_name);
        if (read_first_line(root, path, scope, sizeof scope) && !strcmp(scope, "Device"))
            continue;
        if (!strcmp(type, "Battery")) {
            if (battery++) continue;
            snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", e->d_name);
            capacity = read_number(root, path);
            snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", e->d_name);
            if (!read_first_line(root, path, status, sizeof status)) status[0] = '\0';
            lower(status);
        } else {
            ac_listed = 1;
            snprintf(path, sizeof path, "/sys/class/power_supply/%s/online", e->d_name);
            if (read_number(root, path) == 1) ac_online = 1;
        }
    }
    if (d) closedir(d);

    {
        const char *source =
            ac_online ? "on mains"
            : battery && !strcmp(status, "discharging") ? "on battery"
            : ac_listed ? "on battery"
            : !battery ? "no power supply listed"
            : "the power source unknown";
        size_t len = (size_t)snprintf(out, n, "%s", source);
        if (battery && len < n) {
            if (capacity >= 0)
                len += (size_t)snprintf(out + len, n - len, " (battery %ld%%%s%s)", capacity,
                                        status[0] ? ", " : "", status);
            else if (status[0])
                len += (size_t)snprintf(out + len, n - len, " (battery %s)", status);
        }
        return len < n ? len : n - 1;
    }
}

/* The core a thread last ran on: /proc's stat for it, field 39. Its name,
 * the second field, is in parentheses and may hold spaces, so the fields
 * are counted from its closing one. */
static int thread_cpu_linux(const char *root, long tid)
{
    char path[640], buf[1024], *p;   /* root (the tests' long folders) and the stat path */
    int field;
    FILE *f;
    size_t got;
    snprintf(path, sizeof path, "%s/proc/self/task/%ld/stat", root, tid);
    f = fopen(path, "r");
    if (!f) return -1;
    got = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[got] = '\0';
    p = strrchr(buf, ')');
    if (!p) return -1;
    /* After ')' comes field 3; field 39 is 36 fields on. */
    for (field = 2; field < 39 && p; field++)
        p = strchr(p + 1, ' ');
    return p ? atoi(p + 1) : -1;
}

void host_info_power_linux(const char *root, const long tids[HOST_THREADS],
                           char *out, size_t n)
{
    static const char *const who[HOST_THREADS] = { "the game's thread", "the renderer's" };
    char buf[128], path[160];
    size_t len;
    int i;

    if (!n) return;
    len = power_source_linux(root, out, n);
    if (len + 1 < n && read_first_line(root, "/sys/firmware/acpi/platform_profile", buf, sizeof buf))
        len += (size_t)snprintf(out + len, n - len, "; platform profile %s", buf);
    for (i = 0; i < HOST_THREADS && len + 1 < n; i++) {
        int cpu = tids[i] ? thread_cpu_linux(root, tids[i]) : -1;
        long khz;
        if (!tids[i]) {
            len += (size_t)snprintf(out + len, n - len, "; %s not started", who[i]);
            continue;
        }
        if (cpu < 0) {
            len += (size_t)snprintf(out + len, n - len, "; %s on a core not known", who[i]);
            continue;
        }
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
        khz = read_number(root, path);
        if (khz >= 0)
            len += (size_t)snprintf(out + len, n - len, "; %s on cpu %d, %ld MHz", who[i], cpu,
                                    (khz + 500) / 1000);
        else
            len += (size_t)snprintf(out + len, n - len, "; %s on cpu %d, its clock not known",
                                    who[i], cpu);
        if (len + 1 >= n) break;
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);
        if (read_first_line(root, path, buf, sizeof buf)) {
            len += (size_t)snprintf(out + len, n - len, " (%s", buf);
            snprintf(path, sizeof path,
                     "/sys/devices/system/cpu/cpu%d/cpufreq/energy_performance_preference", cpu);
            if (len + 1 < n && read_first_line(root, path, buf, sizeof buf))
                len += (size_t)snprintf(out + len, n - len, ", epp %s", buf);
            if (len + 1 < n) len += (size_t)snprintf(out + len, n - len, ")");
        }
    }
}

/* ── The power state: Windows' calls ───────────────────────────────────── */

#if defined(_WIN32)
/* powrprof.dll is part of Windows, but loaded when first asked for rather
 * than imported, so the engine's imports stay those launcher/check_package.py
 * lists. */
typedef LONG  (WINAPI *HostNtPowerInfo)(int, PVOID, ULONG, PVOID, ULONG);
typedef DWORD (WINAPI *HostActiveScheme)(HKEY, GUID **);
typedef DWORD (WINAPI *HostFriendlyName)(HKEY, const GUID *, const GUID *, const GUID *,
                                         UCHAR *, DWORD *);
typedef DWORD (WINAPI *HostOverlayScheme)(GUID *);

/* CallNtPowerInformation's ProcessorInformation, one entry a processor. */
#define HOST_PROCESSOR_INFORMATION 11
typedef struct {
    ULONG number, max_mhz, current_mhz, mhz_limit, max_idle, current_idle;
} HostProcessorPower;

/* Windows' power modes (Settings > System > Power), by their overlay
 * scheme's GUID; Balanced is no overlay at all. */
static const struct { GUID id; const char *name; } k_power_modes[] = {
    { { 0x961CC777, 0x2547, 0x4F9D, { 0x81, 0x74, 0x7D, 0x86, 0x18, 0x1B, 0x8A, 0x7A } },
      "best power efficiency" },
    { { 0x3AF9B8D9, 0x7C97, 0x431D, { 0xAD, 0x78, 0x34, 0xA8, 0xBF, 0xEA, 0x43, 0x9F } },
      "better performance" },
    { { 0xDED574B5, 0x45A0, 0x4F42, { 0x87, 0x37, 0x46, 0x34, 0x5C, 0x09, 0xC2, 0x38 } },
      "best performance" },
};

static void host_power_windows(char *out, size_t n)
{
    static HMODULE powrprof;
    static int tried;
    SYSTEM_POWER_STATUS ps;
    size_t len = 0;

    if (!tried++) powrprof = LoadLibraryA("powrprof.dll");
    if (GetSystemPowerStatus(&ps)) {
        len = (size_t)snprintf(out, n, "%s",
                               ps.ACLineStatus == 1 ? "on mains"
                               : ps.ACLineStatus == 0 ? "on battery"
                               : "the power source unknown");
        if (ps.BatteryFlag == 128 || ps.BatteryFlag == 255)
            len += (size_t)snprintf(out + len, n - len, " (no battery)");
        else if (ps.BatteryLifePercent <= 100)
            len += (size_t)snprintf(out + len, n - len, " (battery %u%%%s)",
                                    (unsigned)ps.BatteryLifePercent,
                                    (ps.BatteryFlag & 8) ? ", charging" : "");
    } else {
        len = (size_t)snprintf(out, n, "the power source unknown");
    }
    if (powrprof && len + 1 < n) {
        HostActiveScheme active = (HostActiveScheme)(void *)GetProcAddress(powrprof, "PowerGetActiveScheme");
        HostFriendlyName name = (HostFriendlyName)(void *)GetProcAddress(powrprof, "PowerReadFriendlyName");
        HostOverlayScheme overlay = (HostOverlayScheme)(void *)GetProcAddress(powrprof, "PowerGetEffectiveOverlayScheme");
        GUID *scheme = NULL;
        if (active && name && active(NULL, &scheme) == ERROR_SUCCESS && scheme) {
            WCHAR wide[128];
            char utf8[256];
            DWORD size = sizeof wide;
            if (name(NULL, scheme, NULL, NULL, (UCHAR *)wide, &size) == ERROR_SUCCESS
                && WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof utf8, NULL, NULL) > 0)
                len += (size_t)snprintf(out + len, n - len, "; power plan %s", utf8);
            LocalFree(scheme);
        }
        if (overlay && len + 1 < n) {
            GUID mode;
            static const GUID none;
            if (overlay(&mode) == ERROR_SUCCESS) {
                const char *what = NULL;
                size_t i;
                if (!memcmp(&mode, &none, sizeof mode)) what = "balanced";
                for (i = 0; !what && i < sizeof k_power_modes / sizeof k_power_modes[0]; i++)
                    if (!memcmp(&mode, &k_power_modes[i].id, sizeof mode)) what = k_power_modes[i].name;
                if (what)
                    len += (size_t)snprintf(out + len, n - len, "; power mode %s", what);
                else
                    len += (size_t)snprintf(out + len, n - len,
                                            "; power mode %08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                                            (unsigned long)mode.Data1, mode.Data2, mode.Data3,
                                            mode.Data4[0], mode.Data4[1], mode.Data4[2], mode.Data4[3],
                                            mode.Data4[4], mode.Data4[5], mode.Data4[6], mode.Data4[7]);
            }
        }
    }
    if (powrprof && len + 1 < n) {
        HostNtPowerInfo info = (HostNtPowerInfo)(void *)GetProcAddress(powrprof, "CallNtPowerInformation");
        SYSTEM_INFO si;
        HostProcessorPower *cpus;
        GetSystemInfo(&si);
        cpus = (HostProcessorPower *)calloc(si.dwNumberOfProcessors, sizeof *cpus);
        if (info && cpus
            && info(HOST_PROCESSOR_INFORMATION, NULL, 0, cpus,
                    (ULONG)(si.dwNumberOfProcessors * sizeof *cpus)) == 0) {
            ULONG lo = (ULONG)-1, hi = 0, top = 0, limit = (ULONG)-1;
            DWORD i;
            for (i = 0; i < si.dwNumberOfProcessors; i++) {
                if (cpus[i].current_mhz < lo) lo = cpus[i].current_mhz;
                if (cpus[i].current_mhz > hi) hi = cpus[i].current_mhz;
                if (cpus[i].max_mhz > top) top = cpus[i].max_mhz;
                if (cpus[i].mhz_limit < limit) limit = cpus[i].mhz_limit;
            }
            len += (size_t)snprintf(out + len, n - len,
                                    "; the %lu cores at %lu to %lu MHz, of %lu (limited to %lu)",
                                    (unsigned long)si.dwNumberOfProcessors, (unsigned long)lo,
                                    (unsigned long)hi, (unsigned long)top, (unsigned long)limit);
        }
        free(cpus);
    }
    (void)len;
}
#endif

/* ── The lines ─────────────────────────────────────────────────────────── */

void host_info_thread(int which)
{
    if (which < 0 || which >= HOST_THREADS) return;
#if defined(_WIN32)
    s_tids[which] = (long)GetCurrentThreadId();
#else
    s_tids[which] = (long)syscall(SYS_gettid);
#endif
}

void host_info_report(FILE *f, const char *prefix)
{
    char power[1024];
#if defined(_WIN32)
    host_power_windows(power, sizeof power);
#else
    long tids[HOST_THREADS];
    int i;
    for (i = 0; i < HOST_THREADS; i++) tids[i] = s_tids[i];
    host_info_power_linux("", tids, power, sizeof power);
#endif
    fprintf(f, "%st=%.1fs: %s\n", prefix, host_info_seconds(), power);
}

void host_info_start(void)
{
    char when[64] = "at a time not known";
    char switches[4096];
    time_t now = time(NULL);
    struct tm tm;
    int ok;

    s_t0 = mono_seconds();
#if defined(_WIN32)
    {   /* the C runtime's gmtime keeps its answer per thread on Windows */
        struct tm *t = gmtime(&now);
        ok = t != NULL;
        if (ok) tm = *t;
    }
#else
    ok = gmtime_r(&now, &tm) != NULL;
#endif
    if (ok) strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S UTC", &tm);
    host_info_switches(HOST_ENVIRON, switches, sizeof switches);
    fprintf(stderr, "[RUN] started %s; switches: %s\n", when, switches);
    host_info_report(stderr, "[HOST] ");
    fflush(stderr);
}
