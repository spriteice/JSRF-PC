/* Run with: python tests/pcores/run.py
 *
 * port/pcores.c without the game, built by run.py with a processor of its own
 * making in place of /sys/devices/system/cpu. What to do is the first
 * argument; the switch, RECOMP_P_CORES, comes from the environment as the
 * game's does.
 *
 *   kinds N   the kinds of core, and whether each of CPUs 0 to N-1 is a P core
 *   cores     the CPUs this may run on: at the start, once on the P cores as
 *             the game thread is, in a thread it starts before and after that
 *             thread gives them back, and after the stop
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pcores_start(void);
void pcores_stop(void);
void pcores_thread(const char *who);
void pcores_unpin(void);
int  pcores_kinds(char *cpus, size_t n);
int  pcores_is_p(int cpu);

/* The calling thread's CPUs, as the kernel writes a list: 0-3,8. */
static void show_cpus(const char *when)
{
    cpu_set_t set;
    char out[256];
    size_t len = 0;
    int cpu, first;
    out[0] = 0;
    if (sched_getaffinity(0, sizeof set, &set)) { perror("sched_getaffinity"); exit(2); }
    for (cpu = 0; cpu < CPU_SETSIZE && len < sizeof out; cpu++) {
        if (!CPU_ISSET(cpu, &set)) continue;
        first = cpu;
        while (cpu + 1 < CPU_SETSIZE && CPU_ISSET(cpu + 1, &set)) cpu++;
        len += (size_t)snprintf(out + len, sizeof out - len, first == cpu ? "%s%d" : "%s%d-%d",
                                len ? "," : "", first, cpu);
    }
    fprintf(stderr, "[TEST] %s: %s\n", when, out);
}

static void *child(void *arg)
{
    (void)arg;
    show_cpus("child");
    pcores_unpin();
    show_cpus("child unpinned");
    return NULL;
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "";
    if (!strcmp(what, "kinds") && argc > 2) {
        char cpus[256];
        int kinds = pcores_kinds(cpus, sizeof cpus), n = atoi(argv[2]), cpu;
        fprintf(stderr, "[TEST] kinds %d, P cores '%s', each:", kinds, cpus);
        for (cpu = -1; cpu <= n; cpu++) fprintf(stderr, " %d", pcores_is_p(cpu));
        fprintf(stderr, "\n");
        return 0;
    }
    if (!strcmp(what, "cores")) {
        pthread_t th;
        pcores_start();
        show_cpus("start");
        pcores_thread("the game");
        show_cpus("held");
        pthread_create(&th, NULL, child, NULL);
        pthread_join(th, NULL);
        pcores_stop();
        show_cpus("stopped");
        return 0;
    }
    fprintf(stderr, "usage: %s kinds N | cores\n", argv[0]);
    return 2;
}
