
/* ---- the test ------------------------------------------------------------ */

/* mcpx_apu_frame_thread's loop, less the work: pace a voice processor frame
 * (throttle, verbatim), note when it ran, and every eighth frame send the
 * encode processor's 256 samples to the ring, as mcpx_apu_monitor_frame does.
 * The device is the SDL ring's fake sink (verbatim): 512 frames every
 * 10.7 ms. With the argument "wall", there is no ring and no device, as with
 * XAudio2 (xa2_ring_level is -1), and throttle paces by the wall clock.
 * DirectSound waits for the next voice processor frame each time it
 * restarts a sound, so what matters is how long, from any moment, until the
 * next frame: the mean of that is the sum of the squared gaps over twice
 * their sum.
 *
 * A stall of the machine is not the pacing's, and is counted apart. The
 * machine stalled when this thread lost STALL_US in which it neither ran nor
 * had asked to wait, or when a wait of throttle's ended with the device's
 * pull that much overdue (the test's qemu_cond_timedwait notes what throttle
 * asks for, a few milliseconds at a time, and how long since the pull; the
 * fake sink is a thread of its own). A container does it for 44 to 90 ms
 * about one run in six, and one such gap alone took the mean wait past its
 * limit, or the wall clock's rate under its own, as it gives up the time it
 * lost on purpose (known bug 22). That gap, and SETTLE_US after it while the
 * ring fills again, are set aside; the pacing is measured on the rest. A gap
 * throttle waited or spun through with the device on time is the pacing's,
 * however long. */
#define SECONDS     3
#define WARM_US     500000      /* the ring fills at once at the start */
#define MAX_FRAMES  (SECONDS * 2000)
#define STALL_US    20000       /* lost to the machine, not asked for */
#define SETTLE_US   100000      /* set aside after a stall */
#define PULL_MS     (SA_BUF_FRAMES * 1000 / SA_RATE)    /* the device's, whole ms */

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* This thread's time on a CPU. */
static int64_t ran_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int by_value(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    static MCPXAPUState state;
    static int64_t at[MAX_FRAMES], asked[MAX_FRAMES], ran[MAX_FRAMES], gap[MAX_FRAMES];
    static int overdue[MAX_FRAMES];
    static uint32_t under[MAX_FRAMES];
    static int16_t eight[8 * NUM_SAMPLES_PER_FRAME * 2];
    MCPXAPUState *d = &state;
    int n = 0, first = -1, i, k = 0, stalls = 0;
    uint32_t dry = 0;
    int64_t t0, sum = 0, sq = 0, longest = 0, settled = 0;
    int wall = argc > 1 && !strcmp(argv[1], "wall");

    (void)g_dev; (void)sa_callback;         /* the real device's side: not here */
    pthread_mutex_init(&d->lock, NULL);
    pthread_cond_init(&d->cond, NULL);
    if (!wall) {
        g_active = 1;
        if (pthread_create(&g_fake_thread, NULL, sa_fake_sink, NULL) != 0) return 2;
    }

    pthread_mutex_lock(&d->lock);
    t0 = now_us();
    while (n < MAX_FRAMES && now_us() - t0 < SECONDS * 1000000LL) {
        throttle(d);
        at[n] = now_us();
        asked[n] = g_asked_us;
        ran[n] = ran_us();
        overdue[n] = g_pull_overdue_ms;
        g_pull_overdue_ms = 0;
        under[n] = atomic_load(&g_underruns);
        if (first < 0 && at[n] - t0 >= WARM_US) first = n;
        n++;
        if ((d->ep_frame_div + 1) % 8 == 0)
            xa2_submit_samples(eight, 8 * NUM_SAMPLES_PER_FRAME);
        d->ep_frame_div++;
    }
    pthread_mutex_unlock(&d->lock);
    if (first < 0 || n - first < 100) return 3;

    for (i = 1; i < n; i++) {
        int64_t g = at[i] - at[i - 1];
        if (g - (asked[i] - asked[i - 1]) - (ran[i] - ran[i - 1]) >= STALL_US ||
            overdue[i] >= PULL_MS + STALL_US / 1000) {
            /* one stall, with any more before it has settled */
            if (i > first) {
                if (at[i - 1] >= settled) stalls++;
                if (g > longest) longest = g;
            }
            settled = at[i] + SETTLE_US;
            continue;
        }
        if (i <= first || at[i - 1] < settled) continue;
        gap[k] = g;
        sum += g;
        sq += g * g;
        dry += under[i] - under[i - 1];
        k++;
    }
    printf("stalls of the machine set aside: %d, the longest %.1f ms; %.2f s measured\n",
           stalls, longest / 1000.0, sum / 1e6);
    if (k < 100) return 3;
    qsort(gap, (size_t)k, sizeof gap[0], by_value);
    printf("frames a second: %.0f\n", k * 1e6 / (double)sum);
    printf("wait for the next frame: %.2f ms on average\n", sq / (2.0 * sum) / 1000.0);
    printf("gaps: 99%% under %.2f ms, the longest %.2f ms\n",
           gap[k * 99 / 100] / 1000.0, gap[k - 1] / 1000.0);
    printf("device underruns after the first half second: %u\n", dry);
    return 0;
}
