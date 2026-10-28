/* Run with: python tests/cpuprof/run.py
 *
 * port/cpuprof.c without the game: two threads keep a core each busy in
 * functions of their own for a little over two report windows, then the
 * profile stops as developer_restart stops it and the program starts itself
 * again, as the game does for a new resolution. run.py checks the reports
 * name each thread's function, and that the new process lives through the
 * exec with no profile of its own.
 *
 * With CPUPROF_TEST_CPUS=<a>,<b> the worker runs on CPU a only and the main
 * thread on CPU b, for the line on the P cores: run.py builds the profile
 * with a processor of its own making, where a is a P core and b is not. */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void cpuprof_start(void);
void cpuprof_stop(void);

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static volatile unsigned long sink;

/* Each looks at the clock once in a million turns, so that the samples land
 * in the function and not in the clock's own code. */
__attribute__((noinline)) static void spin_in_worker(double secs)
{
    double end = now() + secs;
    unsigned long i;
    do for (i = 0; i < 1000000; i++) sink++; while (now() < end);
}

__attribute__((noinline)) static void spin_in_main(double secs)
{
    double end = now() + secs;
    unsigned long i;
    do for (i = 0; i < 1000000; i++) sink += 2; while (now() < end);
}

static int s_cpu[2] = { -1, -1 };     /* the worker's CPU and main's, or -1 */

static void run_on(int cpu)
{
    cpu_set_t set;
    if (cpu < 0) return;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof set, &set)) perror("sched_setaffinity");
}

static void *worker(void *arg)
{
    run_on(s_cpu[0]);
    spin_in_worker(*(double *)arg);
    return NULL;
}

int main(int argc, char **argv)
{
    static double secs = 2.5;
    pthread_t th;
    if (argc > 1 && !strcmp(argv[1], "again")) {
        /* The new process: busy for a while with no handler for SIGPROF. */
        spin_in_main(0.3);
        fprintf(stderr, "started again and lived\n");
        return 0;
    }
    if (getenv("CPUPROF_TEST_CPUS"))
        sscanf(getenv("CPUPROF_TEST_CPUS"), "%d,%d", &s_cpu[0], &s_cpu[1]);
    run_on(s_cpu[1]);
    cpuprof_start();
    pthread_create(&th, NULL, worker, &secs);
    spin_in_main(secs);
    pthread_join(th, NULL);
    cpuprof_stop();
    spin_in_main(0.2);
    fprintf(stderr, "stopped and lived\n");
    execl("/proc/self/exe", argv[0], "again", (char *)NULL);
    perror("execl");
    return 1;
}
