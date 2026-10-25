/* The CPU profile (Developer > Logging > CPU profile, RECOMP_CPU_PROFILE):
 * where each thread's time goes, by function.
 *
 * The GPU time profile times the renderer's own steps. In a crowd's busiest
 * seconds it finds the renderer waiting for the game half the time, and what
 * the game's thread is doing then it cannot say: the recompiled code has no
 * clock in it, and the guest lock's numbers show that thread holding the lock
 * 97 to 99% of the time even in quiet scenes, where it spins until the next
 * frame. A sampling profile names the functions the time goes to without
 * adding anything to them.
 *
 * The process's CPU-time interval timer (ITIMER_PROF) raises SIGPROF for each
 * millisecond of CPU time the process uses, on the thread using it. The
 * handler notes that thread and the instruction it was at, and nothing else.
 * A thread of ours files each sample once a second under the function it
 * falls in, from the executable's own symbol table, so the game's functions
 * come out as sub_<Xbox address>; outside the executable, under the library.
 * Every CPUPROF_WINDOW_S seconds, and at exit, it writes each thread's share
 * of a core and its busiest functions to the log.
 *
 * Linux only for now; elsewhere the switch says so. Off, nothing is set up.
 * RECOMP_CPU_PROFILE_SECS=<n> sets another window (the tests use 1).
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
#else
#include <stdio.h>
#include <stdlib.h>
#endif

void cpuprof_start(void);
void cpuprof_stop(void);

#if defined(CPUPROF_LINUX)

/* A sample for each millisecond of CPU time: enough to see a function that
 * takes a twentieth of a frame within a few seconds, and few enough that the
 * handler's cost stays far below the noise. */
#define CPUPROF_SAMPLE_US 1000
/* Ten seconds a report: the length of the pacing report's window ([PACE],
 * 600 frames at 60 fps), so the two can be read side by side. */
#define CPUPROF_WINDOW_S 10
/* Samples between two drains, a second apart: 2^16 is twenty cores' worth. */
#define CPUPROF_RING (1u << 16)
#define CPUPROF_THREADS 48
#define CPUPROF_LIBS 64
#define CPUPROF_TOP 12

typedef struct {
    _Atomic unsigned long seq;   /* its number + 1 once written */
    uintptr_t pc;
    pid_t tid;
} Sample;

typedef struct {
    uintptr_t lo, hi;
    const char *name;
} Sym;

typedef struct {
    pid_t tid;
    unsigned long total;
    unsigned *fn;                /* a count for each symbol */
    unsigned lib[CPUPROF_LIBS];  /* and for each library outside the executable */
    unsigned unknown;
} Thread;

/* Every thread's CPU time (utime + stime, in clock ticks) at the start of the
 * window, sampled or not: a thread that isn't here started during it. */
typedef struct {
    pid_t tid;
    unsigned long long ticks;
} Base;

static Sample s_ring[CPUPROF_RING];
static _Atomic unsigned long s_head;
static unsigned long s_tail, s_lost;
static __thread pid_t t_tid;

static Sym *s_sym;
static size_t s_nsym;
static const char *s_lib[CPUPROF_LIBS];
static int s_nlib;
static Thread s_thr[CPUPROF_THREADS];
static int s_nthr;
static Base s_base[2 * CPUPROF_THREADS];
static int s_nbase;
static int s_window_s = CPUPROF_WINDOW_S;
static int s_on;
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static struct timespec s_window_start;

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

/* ---- the executable's functions, from its symbol table ---- */

static int main_bias(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    *(uintptr_t *)data = (uintptr_t)info->dlpi_addr;
    return 1;                    /* the first object is the executable */
}

static int sym_cmp(const void *a, const void *b)
{
    const Sym *x = a, *y = b;
    return x->lo < y->lo ? -1 : x->lo > y->lo;
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

static Thread *thread_slot(pid_t tid)
{
    int i;
    for (i = 0; i < s_nthr; i++)
        if (s_thr[i].tid == tid) return &s_thr[i];
    if (s_nthr == CPUPROF_THREADS) return NULL;
    s_thr[s_nthr].tid = tid;
    s_thr[s_nthr].fn = calloc(s_nsym ? s_nsym : 1, sizeof(unsigned));
    return s_thr[s_nthr].fn ? &s_thr[s_nthr++] : NULL;
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
        Thread *t;
        long k;
        if (atomic_load_explicit(&s->seq, memory_order_acquire) != s_tail + 1) {
            s_lost++;                 /* still being written, or written over */
            continue;
        }
        if (!(t = thread_slot(s->tid))) { s_lost++; continue; }
        t->total++;
        if ((k = find_symbol(s->pc)) >= 0) t->fn[k]++;
        else if ((k = find_library(s->pc)) >= 0) t->lib[k]++;
        else t->unknown++;
    }
}

/* utime + stime of one of our threads, in clock ticks, and its name. */
static int thread_cpu(pid_t tid, unsigned long long *ticks, char *name, size_t n)
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
    *ticks = ut + stt;
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
        if (tid > 0 && thread_cpu(tid, &s_base[s_nbase].ticks, name, sizeof name))
            s_base[s_nbase++].tid = tid;
    }
    closedir(d);
}

typedef struct { unsigned count; const char *name; } Top;

static void top_insert(Top *top, int *n, unsigned count, const char *name)
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
    struct timespec now;
    double secs, hz = (double)sysconf(_SC_CLK_TCK);
    char line[2048];
    int i;
    clock_gettime(CLOCK_MONOTONIC, &now);
    secs = (double)(now.tv_sec - s_window_start.tv_sec) + (now.tv_nsec - s_window_start.tv_nsec) / 1e9;
    s_window_start = now;
    if (secs <= 0.0) return;
    fprintf(stderr, "[CPUPROF] the last %.1f s, by thread (share of a core) and function "
                    "(share of the thread's samples)%s\n", secs,
            s_lost ? "; some samples lost" : "");
    s_lost = 0;
    for (i = 0; i < s_nthr; i++) {
        Thread *t = &s_thr[i];
        Top top[CPUPROF_TOP];
        int ntop = 0, len, k;
        unsigned long long ticks = 0;
        char name[32] = "exited";
        double core = -1.0;
        size_t j;
        if (!t->total) continue;
        if (thread_cpu(t->tid, &ticks, name, sizeof name)) {
            unsigned long long base = 0;
            for (k = 0; k < s_nbase; k++)
                if (s_base[k].tid == t->tid) base = s_base[k].ticks;
            core = (double)(ticks - base) / hz / secs;
        }
        for (j = 0; j < s_nsym; j++)
            if (t->fn[j]) top_insert(top, &ntop, t->fn[j], s_sym[j].name);
        for (k = 0; k < s_nlib; k++)
            if (t->lib[k]) top_insert(top, &ntop, t->lib[k], s_lib[k]);
        if (t->unknown) top_insert(top, &ntop, t->unknown, "(elsewhere)");
        if (core >= 0.0)
            len = snprintf(line, sizeof line, "[CPUPROF]   thread %d (%s) %.0f%% of a core, %lu samples:",
                           (int)t->tid, name, core * 100.0, t->total);
        else
            len = snprintf(line, sizeof line, "[CPUPROF]   thread %d (%s) %lu samples:",
                           (int)t->tid, name, t->total);
        for (k = 0; k < ntop && len > 0 && (size_t)len < sizeof line; k++)
            len += snprintf(line + len, sizeof line - (size_t)len, "%s %s %.1f%%", k ? "," : "",
                            top[k].name, 100.0 * top[k].count / (double)t->total);
        /* One write for the line, so another thread's output can't split it. */
        fprintf(stderr, "%s\n", line);
        memset(t->fn, 0, (s_nsym ? s_nsym : 1) * sizeof(unsigned));
        memset(t->lib, 0, sizeof t->lib);
        t->unknown = 0;
        t->total = 0;
    }
    snapshot();
}

static void *reporter(void *arg)
{
    struct timespec now;
    (void)arg;
    for (;;) {
        struct timespec second = { 1, 0 };
        while (nanosleep(&second, &second) && errno == EINTR) {}
        pthread_mutex_lock(&s_mu);
        if (!s_on) { pthread_mutex_unlock(&s_mu); return NULL; }
        drain();
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - s_window_start.tv_sec >= s_window_s) report();
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
    clock_gettime(CLOCK_MONOTONIC, &s_window_start);
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

#else

void cpuprof_start(void)
{
    const char *e = getenv("RECOMP_CPU_PROFILE");
    if (e && atoi(e))
        fprintf(stderr, "[CPUPROF] not on this system: the CPU profile is Linux only for now\n");
}

void cpuprof_stop(void) {}

#endif
