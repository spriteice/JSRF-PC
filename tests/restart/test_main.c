/* Run with: python tests/restart/run.py
 *
 * The restart without the game: port/main.c's recomp_restart_self as it is
 * (run.py puts it above this). The first run opens files the ways the engine
 * does and restarts; the second, with RESTART_TEST_RUN=2 in its environment,
 * says what came across. Both write to standard output, which the second run
 * must still have.
 */

#if defined(_WIN32)

/* A file from the C library, which makes it inheritable: xbox_kernel.log. */
static int first(const char *dir)
{
    char path[1024], pid[16];
    snprintf(path, sizeof path, "%s\\log.txt", dir);
    if (!fopen(path, "w")) return perror("fopen"), 1;
    snprintf(pid, sizeof pid, "%lu", GetCurrentProcessId());
    SetEnvironmentVariableA("RESTART_TEST_PARENT", pid);
    SetEnvironmentVariableA("RESTART_TEST_RUN", "2");
    recomp_restart_self();
    printf("FAIL first run: the restart did not happen\n");
    return 1;
}

/* Once the first run has ended, its file can be deleted -- unless the new
 * run was handed it, as Purge shaders once found. */
static int second(const char *dir)
{
    char path[1024], pid[16];
    HANDLE parent;
    if (GetEnvironmentVariableA("RESTART_TEST_PARENT", pid, sizeof pid)
        && (parent = OpenProcess(SYNCHRONIZE, FALSE, strtoul(pid, NULL, 10))) != NULL) {
        WaitForSingleObject(parent, 10000);
        CloseHandle(parent);
    }
    snprintf(path, sizeof path, "%s\\log.txt", dir);
    if (!DeleteFileA(path)) {
        printf("FAIL second run: the first run's file came across (error %lu)\n", GetLastError());
        return 1;
    }
    printf("second run: nothing came across\n");
    return 0;
}

#else
#if defined(__linux__)
#  include <sys/mman.h>     /* memfd_create */
#endif

/* Far past any descriptor the C library would pick next. */
#define FAR_FD 200

/* A file from fopen (xbox_kernel.log), one from open (the game's files), a
 * memfd on Linux (the guest memory), and one far up. With fail, the restart
 * cannot start the program, and every one must still be open after it. */
static int first(const char *dir, int fail)
{
    static char *missing[] = { "/nonexistent/jsrf_recomp", NULL };
    char path[1024];
    int fds[4], n = 0, i;
    FILE *f;
    snprintf(path, sizeof path, "%s/log.txt", dir);
    if (!(f = fopen(path, "w"))) return perror("fopen"), 1;
    fds[n++] = fileno(f);
    snprintf(path, sizeof path, "%s/game.bin", dir);
    if ((fds[n] = open(path, O_RDWR | O_CREAT, 0644)) < 0) return perror("open"), 1;
    n++;
#if defined(__linux__)
    if ((fds[n] = memfd_create("guest", 0)) < 0) return perror("memfd_create"), 1;
    n++;
#endif
    if (dup2(fds[0], FAR_FD) < 0) return perror("dup2"), 1;
    fds[n++] = FAR_FD;
#if defined(__GLIBC__) && defined(CLOSE_RANGE_CLOEXEC)
    printf("first run: marked by close_range\n");
#else
    printf("first run: marked one at a time\n");
#endif
    setenv("RESTART_TEST_RUN", "2", 1);
    if (fail) g_argv = missing;
    recomp_restart_self();
    if (!fail) {
        printf("FAIL first run: the restart did not happen\n");
        return 1;
    }
    for (i = 0; i < n; i++)
        if (fcntl(fds[i], F_GETFD) < 0) {
            printf("FAIL first run: descriptor %d closed by a restart that failed\n", fds[i]);
            return 1;
        }
    printf("first run: all still open\n");
    return 0;
}

static int second(const char *dir)
{
    long fd, max = sysconf(_SC_OPEN_MAX);
    int bad = 0;
    (void)dir;
    for (fd = 3; fd < max; fd++)
        if (fcntl((int)fd, F_GETFD) >= 0) {
            printf("FAIL second run: descriptor %ld came across\n", fd);
            bad = 1;
        }
    if (!bad) printf("second run: nothing came across\n");
    return bad;
}

#endif

int main(int argc, char **argv)
{
    const char *dir = getenv("RESTART_TEST_DIR"), *run = getenv("RESTART_TEST_RUN");
    const char *c = argc > 1 ? argv[1] : "";
    g_argv = argv;
    if (!dir) {
        fprintf(stderr, "no RESTART_TEST_DIR\n");
        return 2;
    }
    if (run && !strcmp(run, "2")) return second(dir);
#if defined(_WIN32)
    if (!strcmp(c, "restart")) return first(dir);
#else
    if (!strcmp(c, "restart")) return first(dir, 0);
    if (!strcmp(c, "failed")) return first(dir, 1);
#endif
    fprintf(stderr, "no case %s\n", c);
    return 2;
}
