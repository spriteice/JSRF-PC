
/* ---- the test --------------------------------------------------------------
 *
 * The report thread (kernel_report_thread) runs as in the game, every 20 ms.
 * The picture moves for a second, then stops while this thread holds a
 * stand-in for the guest lock, as the game's main thread did in known bug 49.
 *
 * The cases:
 *   - nothing is said while the picture moves;
 *   - [FROZEN] is said by the report thread four seconds after the last frame,
 *     with [THREADS] after it, while the lock is held: the freeze watch rode
 *     the timer thread, and in known bug 49 that thread was waiting for the
 *     guest lock, so nothing was said.
 *
 * stderr, where the reports go, is read through a pipe. */

static volatile unsigned long g_frames;
unsigned long nv2a_gl_progress(void)
{
    return __atomic_load_n(&g_frames, __ATOMIC_ACQUIRE);
}
static void frame(void)
{
    __atomic_fetch_add(&g_frames, 1, __ATOMIC_RELEASE);
}

static pthread_mutex_t g_out_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_out[1 << 16];
static size_t g_out_n;

static void *read_stderr(void *fd)
{
    char buf[4096];
    ssize_t n;
    while ((n = read((int)(intptr_t)fd, buf, sizeof buf)) > 0) {
        pthread_mutex_lock(&g_out_lock);
        if (g_out_n + (size_t)n < sizeof g_out - 1) {
            memcpy(g_out + g_out_n, buf, (size_t)n);
            g_out_n += (size_t)n;
            g_out[g_out_n] = 0;
        }
        pthread_mutex_unlock(&g_out_lock);
    }
    return NULL;
}

/* Where text first appears in what stderr has said from offset 'from' on, or
 * -1; waits up to 'ms' for it. */
static long find_out(size_t from, const char *text, long long ms)
{
    long long until = (long long)GetTickCount64() + ms;
    for (;;) {
        long at = -1;
        pthread_mutex_lock(&g_out_lock);
        if (from <= g_out_n) {
            const char *p = strstr(g_out + from, text);
            if (p) at = (long)(p - g_out);
        }
        pthread_mutex_unlock(&g_out_lock);
        if (at >= 0 || (long long)GetTickCount64() >= until) return at;
        usleep(5000);
    }
}

/* The line of stderr's text that 'at' is in, copied out. */
static void out_line(long at, char *line, size_t size)
{
    const char *start, *end;
    size_t n;
    pthread_mutex_lock(&g_out_lock);
    start = g_out + at;
    while (start > g_out && start[-1] != '\n') start--;
    end = strchr(g_out + at, '\n');
    if (!end) end = g_out + g_out_n;
    n = (size_t)(end - start) < size - 1 ? (size_t)(end - start) : size - 1;
    memcpy(line, start, n);
    line[n] = 0;
    pthread_mutex_unlock(&g_out_lock);
}

static int g_failed;
static void fail(const char *what)
{
    printf("thread_reports: FAIL: %s\n", what);
    g_failed = 1;
}

int main(void)
{
    static pthread_mutex_t guest_lock = PTHREAD_MUTEX_INITIALIZER;
    int fds[2];
    pthread_t reader;
    long long last, said_at;
    long at, threads_at;
    char line[256];

    setvbuf(stdout, NULL, _IONBF, 0);
    if (pipe(fds) != 0 || dup2(fds[1], 2) < 0) {
        printf("thread_reports: FAIL: no pipe for stderr\n");
        return 1;
    }
    close(fds[1]);
    if (pthread_create(&reader, NULL, read_stderr, (void *)(intptr_t)fds[0]) != 0) {
        printf("thread_reports: FAIL: no reader thread\n");
        return 1;
    }

    if (!CreateThread(NULL, 0, kernel_report_thread, NULL, 0, NULL)) {
        printf("thread_reports: FAIL: no report thread\n");
        return 1;
    }

    /* The picture moves for a second: nothing to say. */
    {   long long until = (long long)GetTickCount64() + 1000;
        while ((long long)GetTickCount64() < until) {
            frame();
            usleep(16000);
        }
    }
    if (find_out(0, "[FROZEN]", 0) >= 0)
        fail("[FROZEN] while the picture moved");
    else
        printf("thread_reports: nothing said while the picture moved\n");

    /* The last frame, and the picture stops while the lock is held. */
    pthread_mutex_lock(&guest_lock);
    frame();
    last = (long long)GetTickCount64();
    at = find_out(0, "[FROZEN] nothing has been presented", 10000);
    said_at = (long long)GetTickCount64();
    if (at < 0) {
        fail("no [FROZEN] in 10 s with the picture stopped and the lock held");
    } else {
        out_line(at, line, sizeof line);
        printf("thread_reports: said %lld ms after the last frame, with the lock "
               "held: %s\n", said_at - last, line + strspn(line, " "));
        if (said_at - last < 3950)
            fail("[FROZEN] under four seconds after the last frame");
        if (said_at - last > 8000)
            fail("[FROZEN] over eight seconds after the last frame");
    }
    threads_at = at < 0 ? -1 : find_out((size_t)at, "[THREADS]", 2000);
    if (at >= 0 && threads_at < 0)
        fail("no [THREADS] after [FROZEN]");

    pthread_mutex_unlock(&guest_lock);

    printf("---- stderr ----\n");
    pthread_mutex_lock(&g_out_lock);
    fwrite(g_out, 1, g_out_n, stdout);
    pthread_mutex_unlock(&g_out_lock);
    printf("----------------\n");
    if (g_failed) return 1;
    printf("thread_reports: all cases pass\n");
    return 0;
}
