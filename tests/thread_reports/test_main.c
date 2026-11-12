
/* ---- the test --------------------------------------------------------------
 *
 * The report thread (kernel_report_thread) runs as in the game, every 20 ms.
 * The picture moves for a second, then stops while this thread holds a
 * stand-in for the guest lock, as the game's main thread did in known bug 49.
 * Two threads the bridge would have spawned start first, as CRI's do, each
 * with kthread_starting, as bridge_thread_main does first: one makes its
 * first kernel call, and one, as CRI's spinner did, makes none.
 *
 * The cases:
 *   - a thread starting is listed as starting, by its worker and context,
 *     and once it has made a kernel call, its row is the call's;
 *   - nothing is said while the picture moves;
 *   - [FROZEN] is said by the report thread four seconds after the last frame,
 *     with [THREADS] after it, while the lock is held: the freeze watch rode
 *     the timer thread, and in known bug 49 that thread was waiting for the
 *     guest lock, so nothing was said;
 *   - that [THREADS] lists the thread still starting, and the other's call.
 *
 * stderr, where the reports go, is read through a pipe. A thread writes its
 * row only before the report thread reads it, ordered by the frame counter,
 * as the thread sanitizer would otherwise report the test's own writes. */

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

static size_t out_now(void)
{
    size_t n;
    pthread_mutex_lock(&g_out_lock);
    n = g_out_n;
    pthread_mutex_unlock(&g_out_lock);
    return n;
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

/* Two of CRI's threads as they start: worker 0x00147EBB, with the spinner's
 * context (0x0013B180), which makes no kernel call, and another's
 * (0x0013B1C0), which makes its first once told to. */
#define WORKER 0x00147EBBu
typedef struct {
    uint32_t ctx;
    volatile unsigned long long tid;
    volatile int started, go, called;
} Starter;
static Starter g_spinner = { 0x0013B180u, 0, 0, 0, 0 };
static Starter g_caller = { 0x0013B1C0u, 0, 0, 0, 0 };

static DWORD WINAPI starter(LPVOID param)
{
    Starter *s = param;
    kthread_starting(WORKER, s->ctx);
    __atomic_store_n(&s->tid, (unsigned long long)GetCurrentThreadId(), __ATOMIC_RELAXED);
    __atomic_store_n(&s->started, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&s->go, __ATOMIC_ACQUIRE)) usleep(1000);
    {   /* its first kernel call, as the bridge's dispatch writes it */
        KThreadState *k = kthread_slot();
        k->ordinal = 99;   /* KeDelayExecutionThread */
        k->caller = 0x00147F00u;
        k->entered_ms = (long long)GetTickCount64();
        k->left_ms = 0;
        k->calls++;
    }
    __atomic_store_n(&s->called, 1, __ATOMIC_RELEASE);
    for (;;) Sleep(1000);
    return 0;
}

static int start(Starter *s)
{
    if (!CreateThread(NULL, 0, starter, s, 0, NULL)) return 0;
    while (!__atomic_load_n(&s->started, __ATOMIC_ACQUIRE)) usleep(1000);
    return 1;
}

/* The row of a [THREADS] table from 'from' on that starts with the thread's
 * tid and goes on with 'rest', copied into line; 0 if there's none. */
static int row_of(size_t from, const Starter *s, const char *rest, char *line, size_t size)
{
    char want[160];
    long at;
    snprintf(want, sizeof want, "tid %-6llu %s",
             __atomic_load_n(&s->tid, __ATOMIC_RELAXED), rest);
    at = find_out(from, want, 2000);
    if (at < 0) return 0;
    out_line(at, line, size);
    return 1;
}

int main(void)
{
    static pthread_mutex_t guest_lock = PTHREAD_MUTEX_INITIALIZER;
    int fds[2];
    pthread_t reader;
    long long last, said_at;
    long at, threads_at;
    char line[256], rest[96];

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

    /* A thread starting is listed as starting; after its first kernel call,
     * its row is the call's. */
    {   size_t from = out_now();
        if (!start(&g_caller)) {
            printf("thread_reports: FAIL: no starting thread\n");
            return 1;
        }
        kthread_report();
        snprintf(rest, sizeof rest, "starting, worker 0x%08X (ctx=0x%08X)  for ",
                 WORKER, g_caller.ctx);
        if (!row_of(from, &g_caller, rest, line, sizeof line))
            fail("[THREADS] doesn't list a thread starting");
        else
            printf("thread_reports: starting: %s\n", line + strspn(line, " "));
        from = out_now();
        __atomic_store_n(&g_caller.go, 1, __ATOMIC_RELEASE);
        while (!__atomic_load_n(&g_caller.called, __ATOMIC_ACQUIRE)) usleep(1000);
        kthread_report();
        if (!row_of(from, &g_caller, "ordinal 99   from 0x00147F00  INSIDE for ", line,
                    sizeof line))
            fail("after its first call, [THREADS] doesn't show the call");
        else
            printf("thread_reports: after its first call: %s\n", line + strspn(line, " "));
        if (find_out(from, "starting, worker", 0) >= 0)
            fail("after its first call, the thread is still listed as starting");
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

    /* A thread starts and makes no kernel call, as CRI's spinner did. */
    if (!start(&g_spinner)) {
        printf("thread_reports: FAIL: no starting thread\n");
        return 1;
    }

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

    /* That [THREADS] names the thread still starting, and the other's call. */
    if (threads_at >= 0) {
        snprintf(rest, sizeof rest, "starting, worker 0x%08X (ctx=0x%08X)  for ",
                 WORKER, g_spinner.ctx);
        if (!row_of((size_t)threads_at, &g_spinner, rest, line, sizeof line))
            fail("[THREADS] after [FROZEN] doesn't list the thread still starting");
        else
            printf("thread_reports: [FROZEN]'s [THREADS] lists it: %s\n",
                   line + strspn(line, " "));
        if (!row_of((size_t)threads_at, &g_caller, "ordinal 99   from 0x00147F00  INSIDE for ",
                    line, sizeof line))
            fail("[THREADS] after [FROZEN] doesn't show the other thread's call");
    }
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
