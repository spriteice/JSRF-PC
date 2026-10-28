/* The game on the P cores (Developer > Performance > Use P cores, on by
 * default; RECOMP_P_CORES=0 turns it off): on a processor with two kinds of
 * core, the game's two busiest threads -- the game thread and the renderer's
 * -- run on the P cores only, for as long as the game runs, without the game
 * spending a cycle on it.
 *
 * The Ally X's Ryzen Z2 Extreme has three P cores (full-size Zen 5, up to
 * 5.0 GHz) and five compact Zen 5c, up to 3.3 GHz. Each of the two threads
 * sleeps for part of every frame (frame pacing, D3D's wait for the GPU), and
 * the system is free to wake it on a compact core, where the same work takes
 * longer. Kept on the P cores, it doesn't. The clocks, the power limit and the
 * power profile are left as the player set them.
 *
 * The kinds come from each core's highest clock (cpufreq's cpuinfo_max_freq):
 * the P cores are those near the fastest's (PCORES_SHARE), and the rest
 * compact. A thread the game starts runs anywhere, as before (pcores_unpin, at
 * the start of each), though one started by a thread on the P cores would
 * otherwise inherit them. Off, nothing is set up. Linux only for now;
 * elsewhere the switch, set by hand to anything but 0, says so. The CPU
 * profile (port/cpuprof.c) says how much of each thread's time was on the P
 * cores, whether this is on or not.
 */
#if defined(__linux__) && !defined(__ANDROID__)
#define _GNU_SOURCE        /* sched_setaffinity, CPU_AND */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#define PCORES_LINUX 1
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

void pcores_start(void);
void pcores_stop(void);
void pcores_thread(const char *who);
void pcores_unpin(void);
int  pcores_kinds(char *cpus, size_t n);
int  pcores_is_p(int cpu);

#if defined(PCORES_LINUX)

/* Where the kernel lists the CPUs. The tests build this with their own. */
#ifndef PCORES_CPU_DIR
#define PCORES_CPU_DIR "/sys/devices/system/cpu"
#endif

/* ---- the kinds of core ---- */

/* A P core's highest clock is at least this share of the fastest core's. The
 * compact cores of today's processors reach 65 to 81% of the P cores' (Zen
 * 5c's 3.3 GHz to Zen 5's 5.1 in a Ryzen AI 9 HX 370, Arrow Lake's E-cores'
 * 4.6 to its P-cores' 5.7), while the cores of a processor with one kind
 * differ by a few percent (the preferred cores that amd-pstate and Turbo Boost
 * Max list a little faster), or 92% at most (a Ryzen 9 7950X3D's cache die):
 * between the two. */
#define PCORES_SHARE 0.875

static pthread_once_t s_kinds_once = PTHREAD_ONCE_INIT;
static int s_kinds;                  /* 2 for two kinds of core, 1 for one, 0 unknown */
static cpu_set_t s_p;                /* with two, the P cores' CPUs */
static long s_p_khz, s_compact_khz;

static long read_long(const char *path)
{
    FILE *f = fopen(path, "r");
    long v = -1;
    if (!f) return -1;
    if (fscanf(f, "%ld", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static void load_kinds(void)
{
    static long khz[CPU_SETSIZE];
    char path[256];
    long hi = 0;
    int cpu, n;
    CPU_ZERO(&s_p);
    /* cpu0, cpu1, ... as far as the kernel lists them. One taken offline is
     * still listed, without a clock. */
    for (n = 0; n < CPU_SETSIZE; n++) {
        snprintf(path, sizeof path, PCORES_CPU_DIR "/cpu%d", n);
        if (access(path, F_OK)) break;
        snprintf(path, sizeof path, PCORES_CPU_DIR "/cpu%d/cpufreq/cpuinfo_max_freq", n);
        khz[n] = read_long(path);
        if (khz[n] <= 0) continue;
        if (khz[n] > hi) hi = khz[n];
    }
    if (!hi) return;
    s_p_khz = hi;
    for (cpu = 0; cpu < n; cpu++) {
        if (khz[cpu] <= 0) continue;
        if ((double)khz[cpu] >= PCORES_SHARE * (double)hi) CPU_SET(cpu, &s_p);
        else if (khz[cpu] > s_compact_khz) s_compact_khz = khz[cpu];
    }
    s_kinds = s_compact_khz ? 2 : 1;
}

/* A set of CPUs as the kernel writes one: 0-5,8. */
static void cpu_list(const cpu_set_t *set, char *out, size_t n)
{
    size_t len = 0;
    int cpu, first;
    out[0] = 0;
    for (cpu = 0; cpu < CPU_SETSIZE && len < n; cpu++) {
        if (!CPU_ISSET(cpu, set)) continue;
        first = cpu;
        while (cpu + 1 < CPU_SETSIZE && CPU_ISSET(cpu + 1, set)) cpu++;
        len += (size_t)snprintf(out + len, n - len, first == cpu ? "%s%d" : "%s%d-%d",
                                len ? "," : "", first, cpu);
    }
}

/* 2 when the processor has two kinds of core, with the P cores' CPUs in cpus;
 * 1 for one kind, 0 where the clocks aren't listed. */
int pcores_kinds(char *cpus, size_t n)
{
    pthread_once(&s_kinds_once, load_kinds);
    if (cpus && n) {
        if (s_kinds == 2) cpu_list(&s_p, cpus, n);
        else cpus[0] = 0;
    }
    return s_kinds;
}

/* Whether cpu is a P core: 1 or 0, or -1 with one kind of core. */
int pcores_is_p(int cpu)
{
    pthread_once(&s_kinds_once, load_kinds);
    if (s_kinds != 2 || cpu < 0 || cpu >= CPU_SETSIZE) return -1;
    return CPU_ISSET(cpu, &s_p) ? 1 : 0;
}

/* ---- the switch ---- */

static int s_on;
static cpu_set_t s_start_mask;       /* the process's CPUs at the start */

/* The calling thread on the P cores, those of them the process may use. who
 * names it in the log. */
void pcores_thread(const char *who)
{
    cpu_set_t set;
    if (!s_on) return;
    CPU_AND(&set, &s_p, &s_start_mask);
    if (!CPU_COUNT(&set)) {
        fprintf(stderr, "[PCORES] %s's thread left as it was: none of the P cores is the game's to use\n", who);
        return;
    }
    if (sched_setaffinity(0, sizeof set, &set)) {
        fprintf(stderr, "[PCORES] %s's thread left as it was: %s\n", who, strerror(errno));
        return;
    }
    fprintf(stderr, "[PCORES] %s's thread (%d) on the P cores from now on\n", who, (int)syscall(SYS_gettid));
}

/* The calling thread back on every CPU the process had: for a thread the game
 * starts, which would otherwise inherit the P cores from its starter. */
void pcores_unpin(void)
{
    if (s_on) sched_setaffinity(0, sizeof s_start_mask, &s_start_mask);
}

/* At the start of the run, once the switches are in the environment
 * (port/developer.c), before the game starts its threads. */
void pcores_start(void)
{
    const char *v = getenv("RECOMP_P_CORES");
    char cpus[256];
    int kinds;
    if (v && (!*v || !strcmp(v, "0"))) return;     /* on unless it is 0 or empty */
    kinds = pcores_kinds(cpus, sizeof cpus);
    if (kinds == 1)
        fprintf(stderr, "[PCORES] RECOMP_P_CORES: one kind of core here, up to %.1f GHz; nothing changed\n",
                s_p_khz / 1e6);
    else if (!kinds)
        fprintf(stderr, "[PCORES] RECOMP_P_CORES: no clocks listed (cpufreq); nothing changed\n");
    else if (sched_getaffinity(0, sizeof s_start_mask, &s_start_mask))
        fprintf(stderr, "[PCORES] RECOMP_P_CORES: nothing changed: %s\n", strerror(errno));
    else {
        s_on = 1;
        fprintf(stderr, "[PCORES] RECOMP_P_CORES: the P cores are CPUs %s, up to %.1f GHz, the rest up to "
                        "%.1f GHz: the game's thread and the renderer's run on the P cores\n",
                cpus, s_p_khz / 1e6, s_compact_khz / 1e6);
    }
}

/* Before the game starts itself again (developer_restart): the thread that
 * does it back on every CPU, as exec keeps a thread's CPUs for the new run. */
void pcores_stop(void)
{
    pcores_unpin();
}

#else /* not Linux */

void pcores_start(void)
{
    const char *v = getenv("RECOMP_P_CORES");
    if (v && *v && strcmp(v, "0"))
        fprintf(stderr, "[PCORES] RECOMP_P_CORES: Linux only for now; nothing changed\n");
}
void pcores_stop(void) {}
void pcores_thread(const char *who) { (void)who; }
void pcores_unpin(void) {}
int pcores_kinds(char *cpus, size_t n)
{
    if (cpus && n) cpus[0] = 0;
    return 0;
}
int pcores_is_p(int cpu)
{
    (void)cpu;
    return -1;
}

#endif
