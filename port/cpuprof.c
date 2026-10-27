/* The CPU profile (Developer > Logging > RECOMP_CPU_PROFILE): where each
 * thread's time goes, by function.
 *
 * The GPU time profile times the renderer's own steps. In a crowd's busiest
 * seconds it finds the renderer waiting for the game half the time, and what
 * the game's thread is doing then it cannot say: the recompiled code has no
 * clock in it, and the guest lock's numbers show that thread holding the lock
 * 97 to 99% of the time even in quiet scenes, where it spins until the next
 * frame. A sampling profile names the functions the time goes to without
 * adding anything to them.
 *
 * Linux: the process's CPU-time interval timer (ITIMER_PROF) raises SIGPROF
 * for each millisecond of CPU time the process uses, on the thread using it.
 * The handler notes that thread and the instruction it was at, and nothing
 * else. A thread of ours files each sample once a second.
 *
 * Windows has no such timer. A thread of ours wakes each millisecond and looks
 * at every other thread of the process: one that has used CPU cycles since the
 * last look (QueryThreadCycleTime) is stopped for as long as it takes to read
 * the instruction it is at (SuspendThread, GetThreadContext), and goes on.
 * Nothing else is done while it is stopped, as it may hold a lock anything
 * else would need. The sample weighs as much as the cycles the thread used, so
 * a thread that ran the whole millisecond counts as one sample a millisecond
 * does on Linux, and one that ran a moment counts for little. Its blind spot:
 * a thread that ran and then went to wait is found at the wait, so the moment
 * before is counted there, in ntdll.dll.
 *
 * Both: each sample goes under the function it falls in, from the
 * executable's own symbol table (ELF's .symtab; COFF's on Windows, which the
 * package keeps: launcher/windows/package.sh strips debug information only),
 * so the game's functions come out as sub_<Xbox address>; outside the
 * executable, under the library. Every CPUPROF_WINDOW_S seconds, and at exit,
 * each thread's share of a core and its busiest functions go to the log.
 *
 * Linux and Windows only for now; elsewhere the switch says so. Off, nothing
 * is set up. RECOMP_CPU_PROFILE_SECS=<n> sets another window (the tests use 1).
 */
#if defined(__linux__) && !defined(__ANDROID__) && (defined(__x86_64__) || defined(__aarch64__))
#define _GNU_SOURCE        /* dl_iterate_phdr, dladdr, REG_RIP */
#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#define CPUPROF_LINUX 1
#elif defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64))
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CPUPROF_WINDOWS 1
#else
#include <stdio.h>
#include <stdlib.h>
#endif

void cpuprof_start(void);
void cpuprof_stop(void);

#if defined(CPUPROF_LINUX) || defined(CPUPROF_WINDOWS)

/* A sample for each millisecond of CPU time: enough to see a function that
 * takes a twentieth of a frame within a few seconds, and few enough that the
 * handler's cost stays far below the noise. */
#define CPUPROF_SAMPLE_US 1000
/* Ten seconds a report: the length of the pacing report's window ([PACE],
 * 600 frames at 60 fps), so the two can be read side by side. */
#define CPUPROF_WINDOW_S 10
#define CPUPROF_THREADS 48
#define CPUPROF_LIBS 64
#define CPUPROF_TOP 12

#if defined(CPUPROF_LINUX)
typedef pid_t Tid;
#else
typedef DWORD Tid;
#endif

typedef struct {
    uintptr_t lo, hi;
    const char *name;
} Sym;

/* A thread's samples since the last report, and their weight (one each on
 * Linux, the cycles the thread used on Windows) by function. */
typedef struct {
    Tid tid;
    unsigned long samples;
    unsigned long long weight;
    unsigned long long *fn;                /* for each symbol */
    unsigned long long lib[CPUPROF_LIBS];  /* and each library outside the executable */
    unsigned long long unknown;
} Thread;

/* Every thread's CPU time, in seconds, at the start of the window, sampled
 * or not: a thread that isn't here started during it. */
typedef struct {
    Tid tid;
    double cpu;
} Base;

static Sym *s_sym;
static size_t s_nsym;
static const char *s_lib[CPUPROF_LIBS];
static int s_nlib;
static Thread s_thr[CPUPROF_THREADS];
static int s_nthr;
static Base s_base[2 * CPUPROF_THREADS];
static int s_nbase;
static unsigned long s_lost;
static int s_window_s = CPUPROF_WINDOW_S;
static double s_window_start;

/* Each system's, below. */
static double clock_now(void);
static int find_library(uintptr_t pc);
static int thread_cpu(Tid tid, double *cpu, char *name, size_t n);
static void snapshot(void);

static int sym_cmp(const void *a, const void *b)
{
    const Sym *x = a, *y = b;
    return x->lo < y->lo ? -1 : x->lo > y->lo;
}

/* The symbol pc is in, or -1. */
static long find_symbol(uintptr_t pc)
{
    size_t lo = 0, hi = s_nsym;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_sym[mid].lo <= pc) lo = mid + 1;
        else hi = mid;
    }
    if (!lo || pc >= s_sym[lo - 1].hi) return -1;
    return (long)lo - 1;
}

static Thread *thread_slot(Tid tid)
{
    Thread *t;
    int i;
    for (i = 0; i < s_nthr; i++)
        if (s_thr[i].tid == tid) return &s_thr[i];
    if (s_nthr == CPUPROF_THREADS) return NULL;
    t = &s_thr[s_nthr];
    memset(t, 0, sizeof *t);
    t->tid = tid;
    t->fn = calloc(s_nsym ? s_nsym : 1, sizeof *t->fn);
    return t->fn ? &s_thr[s_nthr++] : NULL;
}

/* A sample of thread tid at pc, filed under its thread and function. */
static void file_sample(Tid tid, uintptr_t pc, unsigned long long weight)
{
    Thread *t = thread_slot(tid);
    long k;
    if (!t) { s_lost++; return; }
    t->samples++;
    t->weight += weight;
    if ((k = find_symbol(pc)) >= 0) t->fn[k] += weight;
    else if ((k = find_library(pc)) >= 0) t->lib[k] += weight;
    else t->unknown += weight;
}

typedef struct { unsigned long long count; const char *name; } Top;

static void top_insert(Top *top, int *n, unsigned long long count, const char *name)
{
    int i;
    if (*n == CPUPROF_TOP && count <= top[CPUPROF_TOP - 1].count) return;
    i = *n < CPUPROF_TOP ? (*n)++ : CPUPROF_TOP - 1;
    while (i > 0 && top[i - 1].count < count) { top[i] = top[i - 1]; i--; }
    top[i].count = count;
    top[i].name = name;
}

/* Each thread's share of a core over the window, and where it went. */
static void report(void)
{
    double now = clock_now(), secs = now - s_window_start;
    char line[2048];
    int i;
    s_window_start = now;
    if (secs <= 0.0) return;
    fprintf(stderr, "[CPUPROF] the last %.1f s, by thread (share of a core) and function "
                    "(share of the thread's samples)%s\n", secs,
            s_lost ? "; some samples lost" : "");
    s_lost = 0;
    for (i = 0; i < s_nthr;) {
        Thread *t = &s_thr[i];
        Top top[CPUPROF_TOP];
        int ntop = 0, len, k;
        double cpu, core = -1.0;
        char name[64] = "exited";
        size_t j;
        if (!t->samples) {
            /* Idle the whole window, or gone: its place goes to the next
             * thread that runs. */
            free(t->fn);
            *t = s_thr[--s_nthr];
            continue;
        }
        if (thread_cpu(t->tid, &cpu, name, sizeof name)) {
            double base = 0.0;
            for (k = 0; k < s_nbase; k++)
                if (s_base[k].tid == t->tid) base = s_base[k].cpu;
            core = (cpu - base) / secs;
        }
        for (j = 0; j < s_nsym; j++)
            if (t->fn[j]) top_insert(top, &ntop, t->fn[j], s_sym[j].name);
        for (k = 0; k < s_nlib; k++)
            if (t->lib[k]) top_insert(top, &ntop, t->lib[k], s_lib[k]);
        if (t->unknown) top_insert(top, &ntop, t->unknown, "(elsewhere)");
        if (core >= 0.0)
            len = snprintf(line, sizeof line, "[CPUPROF]   thread %d (%s) %.0f%% of a core, %lu samples:",
                           (int)t->tid, name, core * 100.0, t->samples);
        else
            len = snprintf(line, sizeof line, "[CPUPROF]   thread %d (%s) %lu samples:",
                           (int)t->tid, name, t->samples);
        for (k = 0; k < ntop && len > 0 && (size_t)len < sizeof line; k++)
            len += snprintf(line + len, sizeof line - (size_t)len, "%s %s %.1f%%", k ? "," : "",
                            top[k].name, 100.0 * (double)top[k].count / (double)t->weight);
        /* One write for the line, so another thread's output can't split it. */
        fprintf(stderr, "%s\n", line);
        memset(t->fn, 0, (s_nsym ? s_nsym : 1) * sizeof *t->fn);
        memset(t->lib, 0, sizeof t->lib);
        t->unknown = 0;
        t->weight = 0;
        t->samples = 0;
        i++;
    }
    snapshot();
}

#if defined(CPUPROF_LINUX)

/* Samples between two drains, a second apart: 2^16 is twenty cores' worth. */
#define CPUPROF_RING (1u << 16)

typedef struct {
    _Atomic unsigned long seq;   /* its number + 1 once written */
    uintptr_t pc;
    pid_t tid;
} Sample;

static Sample s_ring[CPUPROF_RING];
static _Atomic unsigned long s_head;
static unsigned long s_tail;
static __thread pid_t t_tid;
static int s_on;
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;

static void on_sample(int sig, siginfo_t *si, void *ctx)
{
    ucontext_t *uc = (ucontext_t *)ctx;
    int saved = errno;
    unsigned long n = atomic_fetch_add_explicit(&s_head, 1, memory_order_relaxed);
    Sample *s = &s_ring[n & (CPUPROF_RING - 1)];
    (void)sig; (void)si;
    if (!t_tid) t_tid = (pid_t)syscall(SYS_gettid);
#if defined(__x86_64__)
    s->pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#else
    s->pc = (uintptr_t)uc->uc_mcontext.pc;
#endif
    s->tid = t_tid;
    atomic_store_explicit(&s->seq, n + 1, memory_order_release);
    errno = saved;
}

static double clock_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- the executable's functions, from its symbol table ---- */

static int main_bias(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    *(uintptr_t *)data = (uintptr_t)info->dlpi_addr;
    return 1;                    /* the first object is the executable */
}

/* The functions in /proc/self/exe's .symtab, at the addresses they run at.
 * The file stays mapped: the names point into it. */
static void load_symbols(void)
{
    int fd = open("/proc/self/exe", O_RDONLY);
    struct stat st;
    const unsigned char *f;
    const Elf64_Ehdr *eh;
    const Elf64_Shdr *sh;
    uintptr_t bias = 0;
    size_t i, n = 0;
    if (fd < 0) return;
    if (fstat(fd, &st) || (f = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
        close(fd);
        return;
    }
    close(fd);
    eh = (const Elf64_Ehdr *)f;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) || eh->e_ident[EI_CLASS] != ELFCLASS64) return;
    sh = (const Elf64_Shdr *)(f + eh->e_shoff);
    dl_iterate_phdr(main_bias, &bias);
    for (i = 0; i < eh->e_shnum; i++) {
        const Elf64_Sym *sym;
        const char *str;
        size_t j, count;
        if (sh[i].sh_type != SHT_SYMTAB) continue;
        sym = (const Elf64_Sym *)(f + sh[i].sh_offset);
        str = (const char *)(f + sh[sh[i].sh_link].sh_offset);
        count = sh[i].sh_size / sizeof *sym;
        s_sym = calloc(count, sizeof *s_sym);
        if (!s_sym) return;
        for (j = 0; j < count; j++) {
            if (ELF64_ST_TYPE(sym[j].st_info) != STT_FUNC || !sym[j].st_value) continue;
            s_sym[n].lo = bias + sym[j].st_value;
            s_sym[n].hi = s_sym[n].lo + sym[j].st_size;
            s_sym[n].name = str + sym[j].st_name;
            n++;
        }
    }
    /* A function the table gives no size (_init, the odd one in assembly)
     * gets none here either: stretched to the next symbol, _init took in the
     * calls into shared libraries that pass through the PLT after it. */
    qsort(s_sym, n, sizeof *s_sym, sym_cmp);
    s_nsym = n;
}

/* The library pc is in, by its file name, or -1. */
static int find_library(uintptr_t pc)
{
    Dl_info info;
    const char *name;
    int i;
    if (!dladdr((void *)pc, &info) || !info.dli_fname) return -1;
    name = strrchr(info.dli_fname, '/');
    name = name ? name + 1 : info.dli_fname;
    if (!*name) name = "(the executable, outside its functions)";
    for (i = 0; i < s_nlib; i++)
        if (!strcmp(s_lib[i], name)) return i;
    if (s_nlib == CPUPROF_LIBS) return -1;
    s_lib[s_nlib] = name;
    return s_nlib++;
}

/* Every sample since the last drain, filed under its thread and function. */
static void drain(void)
{
    unsigned long head = atomic_load_explicit(&s_head, memory_order_acquire);
    if (head - s_tail > CPUPROF_RING) {
        s_lost += head - s_tail - CPUPROF_RING;
        s_tail = head - CPUPROF_RING;
    }
    for (; s_tail != head; s_tail++) {
        Sample *s = &s_ring[s_tail & (CPUPROF_RING - 1)];
        if (atomic_load_explicit(&s->seq, memory_order_acquire) != s_tail + 1) {
            s_lost++;                 /* still being written, or written over */
            continue;
        }
        file_sample(s->tid, s->pc, 1);
    }
}

/* utime + stime of one of our threads, in seconds, and its name. */
static int thread_cpu(pid_t tid, double *cpu, char *name, size_t n)
{
    char path[64], buf[512], *p;
    unsigned long long ut, stt;
    FILE *f;
    size_t len;
    snprintf(path, sizeof path, "/proc/self/task/%d/stat", (int)tid);
    if (!(f = fopen(path, "r"))) return 0;
    len = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[len] = 0;
    /* "tid (name) state ...": the name may hold spaces, so from its last ')'. */
    if (!(p = strrchr(buf, ')'))) return 0;
    {
        char *open = strchr(buf, '(');
        size_t k = open ? (size_t)(p - open - 1) : 0;
        if (k >= n) k = n - 1;
        memcpy(name, open ? open + 1 : "", k);
        name[k] = 0;
    }
    /* After the name: state ppid pgrp session tty_nr tpgid flags minflt
     * cminflt majflt cmajflt utime stime. */
    if (sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &stt) != 2) return 0;
    *cpu = (double)(ut + stt) / (double)sysconf(_SC_CLK_TCK);
    return 1;
}

/* Every thread's CPU time now, as the next window's start. */
static void snapshot(void)
{
    DIR *d = opendir("/proc/self/task");
    struct dirent *e;
    char name[32];
    s_nbase = 0;
    if (!d) return;
    while ((e = readdir(d)) && s_nbase < (int)(sizeof s_base / sizeof s_base[0])) {
        pid_t tid = (pid_t)atoi(e->d_name);
        if (tid > 0 && thread_cpu(tid, &s_base[s_nbase].cpu, name, sizeof name))
            s_base[s_nbase++].tid = tid;
    }
    closedir(d);
}

static void *reporter(void *arg)
{
    (void)arg;
    for (;;) {
        struct timespec second = { 1, 0 };
        while (nanosleep(&second, &second) && errno == EINTR) {}
        pthread_mutex_lock(&s_mu);
        if (!s_on) { pthread_mutex_unlock(&s_mu); return NULL; }
        drain();
        if (clock_now() - s_window_start >= s_window_s) report();
        pthread_mutex_unlock(&s_mu);
    }
}

static void timer_set(long us)
{
    struct itimerval it;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = us;
    it.it_value = it.it_interval;
    setitimer(ITIMER_PROF, &it, NULL);
}

/* The last part of a window, at exit. */
static void at_exit(void)
{
    pthread_mutex_lock(&s_mu);
    if (s_on) {
        timer_set(0);
        drain();
        report();
        s_on = 0;
    }
    pthread_mutex_unlock(&s_mu);
}

void cpuprof_start(void)
{
    const char *e = getenv("RECOMP_CPU_PROFILE");
    struct sigaction sa;
    pthread_t th;
    if (!e || !atoi(e) || s_on) return;
    if ((e = getenv("RECOMP_CPU_PROFILE_SECS")) && atoi(e) > 0) s_window_s = atoi(e);
    load_symbols();
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_sample;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, NULL)) return;
    snapshot();
    s_window_start = clock_now();
    s_on = 1;
    if (pthread_create(&th, NULL, reporter, NULL)) { s_on = 0; return; }
    pthread_detach(th);
    atexit(at_exit);
    timer_set(CPUPROF_SAMPLE_US);
    fprintf(stderr, "[CPUPROF] on: a sample each %d us of CPU time, %zu functions known, "
                    "a report every %d s\n", CPUPROF_SAMPLE_US, s_nsym, s_window_s);
}

/* Before the game starts itself again (developer_restart): the last part of
 * the window, as at exit, which exec skips. And an interval timer outlives
 * exec, and SIGPROF's default action ends the process, so the new one would
 * die at its first sample before it could set a handler. Ignored, a signal
 * stays ignored across exec and a pending one is dropped. */
void cpuprof_stop(void)
{
    pthread_mutex_lock(&s_mu);
    if (s_on) {
        timer_set(0);
        signal(SIGPROF, SIG_IGN);
        drain();
        report();
        s_on = 0;
    }
    pthread_mutex_unlock(&s_mu);
}

#else /* CPUPROF_WINDOWS */

#if defined(__x86_64__) || defined(_M_X64)
#define CONTEXT_PC(c) ((uintptr_t)(c).Rip)
#else
#define CONTEXT_PC(c) ((uintptr_t)(c).Pc)
#endif

/* A thread of the process, with the cycles it had used at the last look. */
typedef struct {
    DWORD tid;
    HANDLE h;
    ULONG64 cycles;
} Watched;

/* Where a library is, so that most samples outside the executable need no
 * call to the loader. */
typedef struct {
    uintptr_t lo, hi;
    int lib;
} Range;

typedef HRESULT (WINAPI *GetThreadDescriptionFn)(HANDLE, PWSTR *);

static Watched s_watch[2 * CPUPROF_THREADS];
static int s_nwatch;
static Range s_range[CPUPROF_LIBS];
static int s_nrange;
static GetThreadDescriptionFn s_describe;   /* Windows 10 1607 on */
static HANDLE s_sampler;
static DWORD s_self;
static volatile LONG s_stop;

static double clock_now(void)
{
    LARGE_INTEGER now, hz;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&hz);
    return (double)now.QuadPart / (double)hz.QuadPart;
}

/* ---- the executable's functions, from its symbol table ---- */

/* The functions in the executable's COFF symbol table, at the addresses they
 * run at. The table gives no sizes: each runs to the next, or to the end of
 * its section. The file stays mapped: the longer names point into it, and
 * those of eight characters or fewer, which the table holds unterminated,
 * are copied out. */
static void load_symbols(void)
{
    const unsigned char *image = (const unsigned char *)GetModuleHandleW(NULL), *f;
    const IMAGE_NT_HEADERS *nt;
    const IMAGE_SECTION_HEADER *sec;
    const IMAGE_SYMBOL *sym;
    const char *str;
    wchar_t path[MAX_PATH];
    LARGE_INTEGER size;
    HANDLE file, map = NULL;
    DWORD i, count, strsize, nsec;
    size_t n = 0, end;
    char *shorts;
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    if (GetFileSizeEx(file, &size)) map = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(file);
    if (!map) return;
    f = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(map);
    if (!f) return;
    end = (size_t)size.QuadPart;
    if (end < sizeof(IMAGE_DOS_HEADER) || ((const IMAGE_DOS_HEADER *)f)->e_magic != IMAGE_DOS_SIGNATURE
        || (size_t)((const IMAGE_DOS_HEADER *)f)->e_lfanew + sizeof *nt > end)
        return;
    nt = (const IMAGE_NT_HEADERS *)(f + ((const IMAGE_DOS_HEADER *)f)->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || !nt->FileHeader.PointerToSymbolTable) return;
    sec = IMAGE_FIRST_SECTION(nt);
    nsec = nt->FileHeader.NumberOfSections;
    count = nt->FileHeader.NumberOfSymbols;
    if ((size_t)nt->FileHeader.PointerToSymbolTable + (size_t)count * IMAGE_SIZEOF_SYMBOL + sizeof(DWORD) > end)
        return;
    sym = (const IMAGE_SYMBOL *)(f + nt->FileHeader.PointerToSymbolTable);
    str = (const char *)(sym + count);
    memcpy(&strsize, str, sizeof strsize);       /* the string table's size, itself included */
    if ((size_t)(str - (const char *)f) + strsize > end) return;
    s_sym = calloc(count, sizeof *s_sym);
    shorts = calloc(count, IMAGE_SIZEOF_SHORT_NAME + 1);
    if (!s_sym || !shorts) {
        free(s_sym);
        free(shorts);
        s_sym = NULL;
        return;
    }
    for (i = 0; i < count; i += 1 + sym[i].NumberOfAuxSymbols) {
        const IMAGE_SECTION_HEADER *in;
        if (!ISFCN(sym[i].Type) || sym[i].SectionNumber <= 0 || (DWORD)sym[i].SectionNumber > nsec) continue;
        if (sym[i].N.Name.Short) {
            char *name = shorts + (size_t)n * (IMAGE_SIZEOF_SHORT_NAME + 1);
            memcpy(name, sym[i].N.ShortName, IMAGE_SIZEOF_SHORT_NAME);
            s_sym[n].name = name;
        } else if (sym[i].N.Name.Long < strsize) {
            s_sym[n].name = str + sym[i].N.Name.Long;
        } else {
            continue;
        }
        in = &sec[sym[i].SectionNumber - 1];
        s_sym[n].lo = (uintptr_t)image + in->VirtualAddress + sym[i].Value;
        s_sym[n].hi = (uintptr_t)image + in->VirtualAddress + in->Misc.VirtualSize;
        n++;
    }
    qsort(s_sym, n, sizeof *s_sym, sym_cmp);
    for (i = 0; i + 1 < n; i++)
        if (s_sym[i + 1].lo < s_sym[i].hi) s_sym[i].hi = s_sym[i + 1].lo;
    s_nsym = n;
}

/* The library pc is in, by its file name, or -1. */
static int find_library(uintptr_t pc)
{
    HMODULE m;
    const IMAGE_NT_HEADERS *nt;
    char path[MAX_PATH];
    const char *name;
    int i;
    for (i = 0; i < s_nrange; i++)
        if (pc >= s_range[i].lo && pc < s_range[i].hi) return s_range[i].lib;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)pc, &m)
        || !GetModuleFileNameA(m, path, sizeof path))
        return -1;
    if (m == GetModuleHandleW(NULL)) {
        name = "(the executable, outside its functions)";
    } else {
        name = strrchr(path, '\\');
        name = name ? name + 1 : path;
    }
    for (i = 0; i < s_nlib && strcmp(s_lib[i], name); i++) {}
    if (i == s_nlib) {
        if (s_nlib == CPUPROF_LIBS || !(s_lib[s_nlib] = _strdup(name))) return -1;
        s_nlib++;
    }
    nt = (const IMAGE_NT_HEADERS *)((const char *)m + ((const IMAGE_DOS_HEADER *)m)->e_lfanew);
    if (s_nrange < CPUPROF_LIBS) {
        s_range[s_nrange].lo = (uintptr_t)m;
        s_range[s_nrange].hi = (uintptr_t)m + nt->OptionalHeader.SizeOfImage;
        s_range[s_nrange].lib = i;
        s_nrange++;
    }
    return i;
}

/* ---- the threads ---- */

/* The newest thread watched with this id: one that ended may have left it to
 * another. */
static Watched *watched(DWORD tid)
{
    int i;
    for (i = s_nwatch - 1; i >= 0; i--)
        if (s_watch[i].tid == tid) return &s_watch[i];
    return NULL;
}

static int ended(const Watched *w)
{
    return WaitForSingleObject(w->h, 0) != WAIT_TIMEOUT;
}

/* The process's threads, listed once a second: a new one is watched from now
 * on, so for up to a second after it starts it goes unseen, and one that has
 * ended is let go after the next report (snapshot). The
 * libraries' places are found again too, in case one has gone and another
 * taken its place. */
static void find_threads(void)
{
    HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    DWORD pid = GetCurrentProcessId();
    THREADENTRY32 te;
    BOOL more;
    s_nrange = 0;
    if (list == INVALID_HANDLE_VALUE) return;
    te.dwSize = sizeof te;
    for (more = Thread32First(list, &te); more; more = Thread32Next(list, &te)) {
        Watched *w;
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == s_self) continue;
        if ((w = watched(te.th32ThreadID)) && !ended(w)) continue;
        if (s_nwatch == (int)(sizeof s_watch / sizeof s_watch[0])) { s_lost++; continue; }
        w = &s_watch[s_nwatch];
        w->h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION
                          | SYNCHRONIZE, FALSE, te.th32ThreadID);       /* SYNCHRONIZE: ended() */
        if (!w->h) continue;
        w->tid = te.th32ThreadID;
        if (!QueryThreadCycleTime(w->h, &w->cycles)) w->cycles = 0;
        s_nwatch++;
    }
    CloseHandle(list);
}

/* One look at every thread: each that has run since the last is stopped, read
 * and let go, and nothing else is done while it is stopped. */
static void sample_threads(void)
{
    int i;
    for (i = 0; i < s_nwatch; i++) {
        Watched *w = &s_watch[i];
        ULONG64 cycles, used;
        CONTEXT ctx;
        BOOL read;
        if (!QueryThreadCycleTime(w->h, &cycles) || cycles == w->cycles) continue;
        used = cycles - w->cycles;
        w->cycles = cycles;
        if (SuspendThread(w->h) == (DWORD)-1) continue;
        ctx.ContextFlags = CONTEXT_CONTROL;
        read = GetThreadContext(w->h, &ctx);
        ResumeThread(w->h);
        if (read) file_sample(w->tid, CONTEXT_PC(ctx), used);
    }
}

/* Kernel + user time of a thread, in seconds. */
static int thread_times(HANDLE h, double *cpu)
{
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(h, &created, &exited, &kernel, &user)) return 0;
    *cpu = ((double)(((ULONGLONG)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime)
            + (double)(((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime)) / 1e7;   /* 100 ns */
    return 1;
}

/* A thread's CPU time and its name, which it may not have. */
static int thread_cpu(DWORD tid, double *cpu, char *name, size_t n)
{
    Watched *w = watched(tid);
    PWSTR desc = NULL;
    char utf8[256];
    if (!w || !thread_times(w->h, cpu)) return 0;
    snprintf(name, n, "unnamed");
    if (s_describe && SUCCEEDED(s_describe(w->h, &desc)) && desc) {
        if (*desc && WideCharToMultiByte(CP_UTF8, 0, desc, -1, utf8, (int)sizeof utf8, NULL, NULL) > 0)
            snprintf(name, n, "%s", utf8);
        LocalFree(desc);
    }
    return 1;
}

/* The threads that have ended let go, and every other's CPU time now, as the
 * next window's start. */
static void snapshot(void)
{
    int i;
    s_nbase = 0;
    for (i = 0; i < s_nwatch;) {
        if (ended(&s_watch[i])) {
            CloseHandle(s_watch[i].h);
            s_watch[i] = s_watch[--s_nwatch];
            continue;
        }
        if (s_nbase < (int)(sizeof s_base / sizeof s_base[0]) && thread_times(s_watch[i].h, &s_base[s_nbase].cpu))
            s_base[s_nbase++].tid = s_watch[i].tid;
        i++;
    }
}

static DWORD WINAPI sampler(LPVOID timer)
{
    LARGE_INTEGER due;
    double listed = clock_now();
    due.QuadPart = -(LONGLONG)CPUPROF_SAMPLE_US * 10;      /* from now, in 100 ns */
    while (!InterlockedCompareExchange(&s_stop, 0, 0)) {
        double now;
        if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
        else Sleep(CPUPROF_SAMPLE_US / 1000);
        sample_threads();
        now = clock_now();
        /* Listed before each report too, so that every thread there at the
         * next window's start has its CPU time noted then. */
        if (now - listed >= 1.0 || now - s_window_start >= s_window_s) {
            find_threads();
            listed = now;
        }
        if (now - s_window_start >= s_window_s) report();
    }
    report();
    if (timer) CloseHandle(timer);
    return 0;
}

void cpuprof_start(void)
{
    const char *e = getenv("RECOMP_CPU_PROFILE");
    HANDLE timer;
    if (!e || !atoi(e) || s_sampler) return;
    if ((e = getenv("RECOMP_CPU_PROFILE_SECS")) && atoi(e) > 0) s_window_s = atoi(e);
    s_describe = (GetThreadDescriptionFn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                        "GetThreadDescription");
    load_symbols();
    /* A sample each millisecond needs the high-resolution timer (Windows 10
     * 1803 on); a plain one, at the 1 ms the game asks for (timeBeginPeriod),
     * before that. */
    timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
    /* Made stopped, so that it knows its own id before the threads are listed,
     * and above the game's threads, so that its looks come on time. */
    s_sampler = CreateThread(NULL, 0, sampler, timer, CREATE_SUSPENDED, &s_self);
    if (!s_sampler) {
        if (timer) CloseHandle(timer);
        return;
    }
    SetThreadPriority(s_sampler, THREAD_PRIORITY_HIGHEST);
    find_threads();
    snapshot();
    s_window_start = clock_now();
    atexit(cpuprof_stop);
    ResumeThread(s_sampler);
    fprintf(stderr, "[CPUPROF] on: each thread that ran, looked at each %d us and weighed by the cycles "
                    "it used, %zu functions known, a report every %d s\n",
            CPUPROF_SAMPLE_US, s_nsym, s_window_s);
}

/* At exit, and before the game starts itself again (developer_restart): the
 * sampler writes the last part of the window and ends. */
void cpuprof_stop(void)
{
    if (!s_sampler) return;
    InterlockedExchange(&s_stop, 1);
    WaitForSingleObject(s_sampler, INFINITE);
    CloseHandle(s_sampler);
    s_sampler = NULL;
}

#endif

#else

void cpuprof_start(void)
{
    const char *e = getenv("RECOMP_CPU_PROFILE");
    if (e && atoi(e))
        fprintf(stderr, "[CPUPROF] not on this system: the CPU profile is Linux and Windows only for now\n");
}

void cpuprof_stop(void) {}

#endif
