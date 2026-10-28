/* Run with: python tests/fence_wait/run.py
 *
 * The game thread's wait for a fence, asleep (nv2a_pb_wait_fence and
 * fence_written in src/kernel/nv2a_pb_exec.c, verbatim), with the events of
 * src/platform/win32_compat.c. A GPU thread writes the fence word and then
 * calls fence_written, as the executor does, while the game thread hands out
 * numbers and waits for them, holding a guest lock everywhere but in the
 * wait. The numbers start just short of 2^32, so they wrap.
 *
 * - Before any fence is written, the wait declines (the game spins).
 * - 200,000 waits against a GPU thread that pauses at random, from not at
 *   all to 0.2 ms: each must end with the word at its number, and none may
 *   hang (a wake-up lost).
 * - 200 waits against a GPU thread that pauses 5 ms before each fence, as
 *   frame pacing does: the game thread must be asleep, using under a tenth of
 *   the time it waits, and another guest thread must get the guest lock
 *   meanwhile. Half of them must be woken within 0.3 ms of their fence (the
 *   timer thread's tick, which the game's own sleeping branch waits for, is
 *   1 ms); the worst is shown, not checked, as a busy machine can hold any
 *   thread back.
 * - At raised IRQL, and while another thread waits, it declines.
 *
 * With "off" as its argument (and RECOMP_FENCE_WAIT=0), it must decline
 * every time.
 */
#include <pthread.h>
#include <time.h>

static int by_value(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

#define WORD 0x1000u

static void fail(const char *what)
{
    printf("FAIL: %s\n", what);
    fflush(stdout);
    _exit(1);
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double thread_cpu_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static volatile uint32_t *word(void) { return (volatile uint32_t *)(g_guest + WORD); }

/* The GPU thread: writes each number the game hands out, in order, then
 * calls fence_written, as nv2a_pb_exec_method does. */
static volatile uint32_t g_handed, g_stop;
static volatile int g_pause_us = -1;            /* -1: random, 0 to 200 us */
static double g_written_at[64];                 /* by number / 2, mod 64 */
static unsigned g_seed = 1;

static void pause_us(int us)
{
    if (us > 0) { struct timespec ts = { 0, us * 1000L }; nanosleep(&ts, NULL); }
}

static void *gpu(void *arg)
{
    (void)arg;
    while (!g_stop) {
        uint32_t w = *word(), h = __atomic_load_n(&g_handed, __ATOMIC_ACQUIRE);
        if (w == h) { sched_yield(); continue; }
        if (g_pause_us < 0) {
            g_seed = g_seed * 1103515245u + 12345u;
            if ((g_seed >> 16) % 3) pause_us((int)((g_seed >> 8) % 200));
        } else {
            pause_us(g_pause_us);
        }
        g_written_at[((w + 2) >> 1) & 63] = now_s();
        *(volatile uint32_t *)((uint8_t *)xbox_GetMemoryOffset() + WORD) = w + 2;
        fence_written(w + 2);
    }
    return NULL;
}

/* Another guest thread: counts the times it gets the guest lock. */
static volatile unsigned long g_other_ran;
static void *other(void *arg)
{
    (void)arg;
    while (!g_stop) {
        pthread_mutex_lock(&g_guest_lock);
        g_other_ran++;
        pthread_mutex_unlock(&g_guest_lock);
        pause_us(100);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int off = argc > 1 && !strcmp(argv[1], "off");
    uint32_t top = 0xFFFFF000u;
    pthread_t g, o;
    unsigned long i, declined = 0;
    double waited = 0.0, cpu, late[200];
    int held;

    xbox_guest_lock_retake(1);                 /* the game thread holds it */
    *word() = top;
    g_handed = top;
    if (nv2a_pb_wait_fence(WORD, top + 2, top + 2))
        fail("waited before any fence was written");
    printf("before the first fence: declines\n");

    pthread_create(&g, NULL, gpu, NULL);
    for (i = 0; i < 200000; i++) {
        uint32_t time;
        top += 2;
        __atomic_store_n(&g_handed, top, __ATOMIC_RELEASE);
        g_seed = g_seed * 1103515245u + 12345u;
        time = top - 2u * ((g_seed >> 16) % 3);
        if (!nv2a_pb_wait_fence(WORD, top, time)) {
            if (!off && __atomic_load_n(&s_fences_written, __ATOMIC_ACQUIRE))
                fail("declined a wait");
            declined++;
            while (top - *word() > top - time) { }
        }
        if (top - *word() > top - time) fail("returned before the word reached the number");
    }
    if (off) {
        if (declined != i) fail("RECOMP_FENCE_WAIT=0, and it waited");
        g_stop = 1;
        pthread_join(g, NULL);
        printf("RECOMP_FENCE_WAIT=0: declines every time\n");
        printf("fence_wait off: all cases pass\n");
        return 0;
    }
    printf("waits against a GPU thread that pauses at random: %lu, numbers wrapped: %s, "
           "wake-ups lost: 0\n", i, top < 0xFFFFF000u ? "yes" : "no");

    /* Frame pacing: 5 ms before each fence. */
    while (*word() != top) sched_yield();
    s_fw.slept = 0; s_fw.us = 0; s_fw.longest_us = 0;
    g_pause_us = 5000;
    pthread_create(&o, NULL, other, NULL);
    cpu = thread_cpu_s();
    for (i = 0; i < 200; i++) {
        double t0 = now_s();
        top += 2;
        __atomic_store_n(&g_handed, top, __ATOMIC_RELEASE);
        if (!nv2a_pb_wait_fence(WORD, top, top)) fail("declined a wait");
        late[i] = now_s() - g_written_at[(top >> 1) & 63];
        waited += now_s() - t0;
    }
    qsort(late, 200, sizeof *late, by_value);
    cpu = thread_cpu_s() - cpu;
    g_stop = 1;
    pthread_join(g, NULL);
    /* The other guest thread may be waiting for the guest lock, which this
     * thread holds: given up, it sees g_stop. */
    held = xbox_guest_lock_drop();
    pthread_join(o, NULL);
    xbox_guest_lock_retake(held);
    printf("200 waits of 5 ms: game thread busy %.1f%% of the wait, woken %.0f us after "
           "its fence (half of them), %.0f us at worst; another guest thread got the lock "
           "%lu times; report: %lu asleep, the longest %.1f ms\n",
           100.0 * cpu / waited, late[100] * 1e6, late[199] * 1e6, g_other_ran,
           s_fw.slept, s_fw.longest_us / 1000.0);
    if (cpu > waited * 0.1) fail("the game thread spun while it waited");
    if (late[100] > 0.0003) fail("half the waits woken more than 0.3 ms after their fence");
    if (g_other_ran < 200) fail("no other guest thread got the lock while the game waited");
    if (s_fw.slept != 200 || s_fw.longest_us < 4000) fail("the report's counts are wrong");

    {
        KIRQL was = t_irql;
        t_irql = DISPATCH_LEVEL;
        if (nv2a_pb_wait_fence(WORD, top + 2, top + 2)) fail("waited at raised IRQL");
        t_irql = was;
        s_fw.busy = 1;
        if (nv2a_pb_wait_fence(WORD, top + 2, top + 2)) fail("waited while another thread waits");
        s_fw.busy = 0;
        printf("at raised IRQL, and while another thread waits: declines\n");
    }
    printf("fence_wait: all cases pass\n");
    return 0;
}
