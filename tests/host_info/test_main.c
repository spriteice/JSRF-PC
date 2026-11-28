/* tests/host_info: src/platform/host_info.c's lines, driven by run.py.
 *
 *   test switches                 the [RUN] line's switches, from a few
 *                                 environments of the test's making
 *   test power <root> <game> <renderer>
 *                                 the [HOST] line's power state, from the
 *                                 files under root, for those thread ids
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/host_info.h"

static int switches(void)
{
    static char *const none[] = { "HOME=/home/deck", "PATH=/usr/bin", NULL };
    static char *const some[] = {
        "RECOMP_LOCK_WATCH=1", "HOME=/home/deck", "JSRF_SETTINGS_FILE=/x/settings.cfg",
        "RECOMP_APU_STATS=1", "RECOMPILE=no", "JSRF_=", "RECOMP_A=2", NULL
    };
    static char *const long_ones[] = {
        "RECOMP_AAAA=0123456789", "RECOMP_BBBB=0123456789", "RECOMP_CCCC=0123456789", NULL
    };
    char out[4096], small[40];
    int n;

    n = host_info_switches(none, out, sizeof out);
    printf("none: %d [%s]\n", n, out);
    n = host_info_switches(some, out, sizeof out);
    printf("some: %d [%s]\n", n, out);
    n = host_info_switches(long_ones, small, sizeof small);
    printf("cut: %d [%s] %u\n", n, small, (unsigned)strlen(small));
    n = host_info_switches(NULL, out, sizeof out);
    printf("null: %d [%s]\n", n, out);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "switches"))
        return switches();
    if (argc == 5 && !strcmp(argv[1], "power")) {
        long tids[HOST_THREADS];
        char out[1024];
        tids[HOST_THREAD_GAME] = atol(argv[3]);
        tids[HOST_THREAD_RENDERER] = atol(argv[4]);
        host_info_power_linux(argv[2], tids, out, sizeof out);
        printf("%s\n", out);
        return 0;
    }
    fprintf(stderr, "usage: test switches | test power <root> <game> <renderer>\n");
    return 2;
}
